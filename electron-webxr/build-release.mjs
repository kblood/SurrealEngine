import { packager } from "@electron/packager";
import { execFile as execFileCallback } from "node:child_process";
import { access, cp, mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import process from "node:process";
import { promisify } from "node:util";
import { fileURLToPath } from "node:url";

const appDirectory = path.dirname(fileURLToPath(import.meta.url));
const execFile = promisify(execFileCallback);

function argument(name, fallback = null) {
  const prefix = `--${name}=`;
  const found = process.argv.slice(2).find(value => value.startsWith(prefix));
  return found ? path.resolve(found.slice(prefix.length)) : fallback;
}

const webRelease = argument("web-release", process.env.SURREAL_WEB_RELEASE ? path.resolve(process.env.SURREAL_WEB_RELEASE) : null);
const output = argument("out", path.join(appDirectory, "dist"));
if (!webRelease) throw new Error("Pass --web-release=<audited browser release directory>.");
await access(path.join(webRelease, "index.html"));
const manifest = JSON.parse(await readFile(path.join(webRelease, "release-manifest.json"), "utf8"));
const repositoryRoot = path.resolve(appDirectory, "..");
const { stdout: wrapperCommitOutput } = await execFile("git", ["rev-parse", "HEAD"], { cwd: repositoryRoot });
const { stdout: wrapperStatusOutput } = await execFile("git", ["status", "--porcelain", "--untracked-files=no"], { cwd: repositoryRoot });
await mkdir(output, { recursive: true });
const stagingRoot = await mkdtemp(path.join(os.tmpdir(), "surreal-electron-webxr-"));
let paths;
try {
  const stagedWebRelease = path.join(stagingRoot, "web-release");
  await cp(webRelease, stagedWebRelease, { recursive: true });
  paths = await packager({
    dir: appDirectory,
    out: output,
    platform: "win32",
    arch: "x64",
    asar: true,
    overwrite: true,
    prune: true,
    name: "SurrealEngine WebXR Test",
    executableName: "SurrealEngine-WebXR-Test",
    extraResource: [stagedWebRelease],
    ignore: [
      /^\/build-release\.mjs$/,
      /^\/README\.md$/,
      /^\/dist(?:\/|$)/,
    ],
  });
} finally {
  await rm(stagingRoot, { recursive: true, force: true });
}

const metadata = {
  schema: "surrealengine-electron-webxr-wrapper-v1",
  generatedAt: new Date().toISOString(),
  electron: "43.2.0",
  wrapperSourceCommit: wrapperCommitOutput.trim(),
  wrapperTrackedTreeDirty: wrapperStatusOutput.trim().length > 0,
  chromiumRuntime: "Core WebXR with OpenXR forced by default; pass --no-force-openxr for automatic runtime selection or --experimental-webgpu-xr for WebGPU-WebXR incubation features",
  webRelease: {
    sourceCommit: manifest.sourceCompliance?.sourceCommit || null,
    sourceTree: manifest.sourceCompliance?.sourceTree || null,
    wasmSha256: manifest.sourceCompliance?.wasmSha256 || null,
  },
  outputDirectories: paths,
  gameDataBundled: false,
};
await writeFile(path.join(output, "electron-release-metadata.json"), `${JSON.stringify(metadata, null, 2)}\n`, "utf8");
process.stdout.write(`${JSON.stringify(metadata, null, 2)}\n`);
