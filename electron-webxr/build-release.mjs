import { packager } from "@electron/packager";
import { flipFuses, FuseState, FuseVersion, FuseV1Options, getCurrentFuseWire } from "@electron/fuses";
import { execFile as execFileCallback } from "node:child_process";
import { access, cp, mkdir, mkdtemp, readFile, readdir, rm, stat, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import process from "node:process";
import { promisify } from "node:util";
import { fileURLToPath } from "node:url";
import { auditRelease } from "../web/package_browser_release.mjs";
import hostSecurity from "./host-security.cjs";

const appDirectory = path.dirname(fileURLToPath(import.meta.url));
const execFile = promisify(execFileCallback);
const { sha256File, verifyWebRelease } = hostSecurity;

async function packageInventory(root, excludedName) {
  const files = [];
  async function visit(directory) {
    const entries = await readdir(directory, { withFileTypes: true });
    for (const entry of entries) {
      const absolute = path.join(directory, entry.name);
      if (entry.isDirectory()) await visit(absolute);
      else if (entry.isFile()) {
        const relative = path.relative(root, absolute).split(path.sep).join("/");
        if (relative === excludedName) continue;
        const information = await stat(absolute);
        files.push({ path: relative, bytes: information.size, sha256: await sha256File(absolute) });
      } else throw new Error(`Packaged output contains an unsupported entry: ${absolute}`);
    }
  }
  await visit(root);
  return files.sort((left, right) => left.path.localeCompare(right.path));
}

function argument(name, fallback = null) {
  const prefix = `--${name}=`;
  const found = process.argv.slice(2).find(value => value.startsWith(prefix));
  return found ? path.resolve(found.slice(prefix.length)) : fallback;
}

const webRelease = argument("web-release", process.env.SURREAL_WEB_RELEASE ? path.resolve(process.env.SURREAL_WEB_RELEASE) : null);
const output = argument("out");
if (!webRelease) throw new Error("Pass --web-release=<audited browser release directory>.");
if (!output) throw new Error("Pass --out=<empty directory under SurrealEngine/out>.");
await access(path.join(webRelease, "index.html"));
await auditRelease(webRelease);
const manifest = JSON.parse(await readFile(path.join(webRelease, "release-manifest.json"), "utf8"));
if (manifest.sourceCompliance?.buildProvenance?.webgl2Renderer !== true)
  throw new Error("Electron WebXR packaging requires an audited WebGL2 browser release.");
const manifestSha256 = await sha256File(path.join(webRelease, "release-manifest.json"));
const repositoryRoot = path.resolve(appDirectory, "..");
const { stdout: wrapperCommitOutput } = await execFile("git", ["rev-parse", "HEAD"], { cwd: repositoryRoot });
const { stdout: wrapperTreeOutput } = await execFile("git", ["rev-parse", "HEAD:electron-webxr"], { cwd: repositoryRoot });
const { stdout: wrapperStatusOutput } = await execFile("git", ["status", "--porcelain=v1", "--untracked-files=all"], { cwd: repositoryRoot });
if (wrapperStatusOutput.trim())
  throw new Error("Electron packaging requires a completely clean source worktree.");
await mkdir(output, { recursive: true });
if ((await readdir(output)).length)
  throw new Error(`Electron release output must be empty: ${output}`);
const stagingRoot = await mkdtemp(path.join(os.tmpdir(), "surreal-electron-webxr-"));
let paths;
try {
  const stagedApp = path.join(stagingRoot, "app");
  const stagedWebRelease = path.join(stagingRoot, "web-release");
  await mkdir(stagedApp, { recursive: true });
  await cp(webRelease, stagedWebRelease, { recursive: true });
  for (const name of ["main.cjs", "preload.cjs", "host-security.cjs"])
    await cp(path.join(appDirectory, name), path.join(stagedApp, name));
  const packageDefinition = JSON.parse(await readFile(path.join(appDirectory, "package.json"), "utf8"));
  delete packageDefinition.devDependencies;
  delete packageDefinition.scripts;
  await writeFile(path.join(stagedApp, "package.json"), `${JSON.stringify(packageDefinition, null, 2)}\n`, "utf8");
  await writeFile(path.join(stagedApp, "release-pin.json"), `${JSON.stringify({
    schema: "surrealengine-electron-release-pin-v1",
    manifestSha256,
    buildId: manifest.buildId || null,
    sourceCommit: manifest.sourceCompliance?.sourceCommit || null,
  }, null, 2)}\n`, "utf8");
  paths = await packager({
    dir: stagedApp,
    out: output,
    platform: "win32",
    arch: "x64",
    electronVersion: "43.2.0",
    asar: true,
    overwrite: false,
    prune: true,
    name: "SurrealEngine WebXR Test",
    executableName: "SurrealEngine-WebXR-Test",
    extraResource: [stagedWebRelease],
  });
  for (const packagedDirectory of paths) {
    await verifyWebRelease(path.join(packagedDirectory, "resources", "web-release"), manifestSha256);
    const executable = path.join(packagedDirectory, "SurrealEngine-WebXR-Test.exe");
    await flipFuses(executable, {
      version: FuseVersion.V1,
      strictlyRequireAllFuses: true,
      [FuseV1Options.RunAsNode]: false,
      [FuseV1Options.EnableCookieEncryption]: true,
      [FuseV1Options.EnableNodeOptionsEnvironmentVariable]: false,
      [FuseV1Options.EnableNodeCliInspectArguments]: false,
      [FuseV1Options.EnableEmbeddedAsarIntegrityValidation]: true,
      [FuseV1Options.OnlyLoadAppFromAsar]: true,
      [FuseV1Options.LoadBrowserProcessSpecificV8Snapshot]: false,
      [FuseV1Options.GrantFileProtocolExtraPrivileges]: false,
      [FuseV1Options.WasmTrapHandlers]: true,
    });
    const fuseWire = await getCurrentFuseWire(executable);
    const expectedFuses = new Map([
      [FuseV1Options.RunAsNode, false],
      [FuseV1Options.EnableCookieEncryption, true],
      [FuseV1Options.EnableNodeOptionsEnvironmentVariable, false],
      [FuseV1Options.EnableNodeCliInspectArguments, false],
      [FuseV1Options.EnableEmbeddedAsarIntegrityValidation, true],
      [FuseV1Options.OnlyLoadAppFromAsar, true],
      [FuseV1Options.LoadBrowserProcessSpecificV8Snapshot, false],
      [FuseV1Options.GrantFileProtocolExtraPrivileges, false],
      [FuseV1Options.WasmTrapHandlers, true],
    ]);
    for (const [option, enabled] of expectedFuses) {
      if (fuseWire[option] !== (enabled ? FuseState.ENABLE : FuseState.DISABLE))
        throw new Error(`Packaged Electron fuse ${FuseV1Options[option]} did not match policy.`);
    }
  }
} finally {
  await rm(stagingRoot, { recursive: true, force: true });
}

const metadata = {
  schema: "surrealengine-electron-webxr-wrapper-v2",
  electron: "43.2.0",
  wrapperSourceCommit: wrapperCommitOutput.trim(),
  wrapperSourceTree: wrapperTreeOutput.trim(),
  wrapperTrackedTreeDirty: false,
  chromiumRuntime: "Core WebXR with automatic runtime selection; pass --force-openxr only for Chromium/OpenXR troubleshooting or --experimental-webgpu-xr for WebGPU-WebXR incubation features",
  webRelease: {
    buildId: manifest.buildId || null,
    manifestSha256,
    sourceCommit: manifest.sourceCompliance?.sourceCommit || null,
    sourceTree: manifest.sourceCompliance?.sourceTree || null,
    wasmSha256: manifest.sourceCompliance?.wasmSha256 || null,
  },
  security: {
    asar: true,
    embeddedAsarIntegrityValidation: true,
    onlyLoadAppFromAsar: true,
    runAsNode: false,
    nodeOptionsEnvironmentVariable: false,
    nodeCliInspectArguments: false,
    fileProtocolExtraPrivileges: false,
    wasmTrapHandlers: true,
    browserProcessSpecificV8Snapshot: false,
    cookieEncryption: true,
    codeSignature: "unsigned-diagnostic",
  },
  executables: await Promise.all(paths.map(async directory => {
    const executable = path.join(directory, "SurrealEngine-WebXR-Test.exe");
    return { path: `${path.basename(directory)}/SurrealEngine-WebXR-Test.exe`,
      sha256: await sha256File(executable) };
  })),
  outputDirectories: paths.map(directory => path.basename(directory)),
  gameDataBundled: false,
};
const packages = [];
for (const packagedDirectory of paths) {
  await cp(path.join(appDirectory, "README.md"), path.join(packagedDirectory, "README.md"));
  await writeFile(path.join(packagedDirectory, "electron-release-metadata.json"),
    `${JSON.stringify(metadata, null, 2)}\n`, "utf8");
  const inventoryName = "electron-file-inventory.json";
  const inventory = {
    schema: "surrealengine-electron-file-inventory-v1",
    releaseManifestSha256: manifestSha256,
    files: await packageInventory(packagedDirectory, inventoryName),
  };
  const inventoryPath = path.join(packagedDirectory, inventoryName);
  await writeFile(inventoryPath, `${JSON.stringify(inventory, null, 2)}\n`, "utf8");
  const zipName = `SurrealEngine-WebXR-Test-${manifest.buildId || wrapperCommitOutput.trim().slice(0, 12)}-win-x64.zip`;
  const zipPath = path.join(output, zipName);
  await execFile("tar.exe", ["-a", "-c", "-f", zipPath, "-C", output, path.basename(packagedDirectory)]);
  const zipInformation = await stat(zipPath);
  packages.push({
    directory: path.basename(packagedDirectory),
    inventory: { path: `${path.basename(packagedDirectory)}/${inventoryName}`,
      sha256: await sha256File(inventoryPath) },
    zip: { path: zipName, bytes: zipInformation.size, sha256: await sha256File(zipPath) },
  });
}
const artifacts = {
  schema: "surrealengine-electron-release-artifacts-v1",
  metadata,
  packages,
};
await writeFile(path.join(output, "electron-release-artifacts.json"),
  `${JSON.stringify(artifacts, null, 2)}\n`, "utf8");
process.stdout.write(`${JSON.stringify(artifacts, null, 2)}\n`);
