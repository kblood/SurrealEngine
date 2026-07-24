"use strict";

const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const fsp = require("node:fs/promises");
const os = require("node:os");
const path = require("node:path");
const {
  contentSecurityPolicy,
  isAllowedExternalUrl,
  isPathInside,
  isTrustedOriginUrl,
  requireWebGL2Release,
  sha256File,
  verifyWebRelease,
} = require("./host-security.cjs");

async function main() {
const temporaryRoot = await fsp.mkdtemp(path.join(os.tmpdir(), "surreal-electron-security-"));
try {
  const index = "<!doctype html><script>globalThis.ready = true;</script>\n";
  const asset = "export const value = 1;\n";
  await fsp.mkdir(path.join(temporaryRoot, "assets"));
  await fsp.writeFile(path.join(temporaryRoot, "index.html"), index);
  await fsp.writeFile(path.join(temporaryRoot, "assets", "app.js"), asset);
  const record = async name => {
    const absolute = path.join(temporaryRoot, ...name.split("/"));
    const information = await fsp.stat(absolute);
    return { path: name, bytes: information.size, sha256: await sha256File(absolute) };
  };
  const manifest = {
    schema: "surrealengine-browser-release-v2",
    version: 2,
    build: { webgl2Renderer: true },
    files: [await record("assets/app.js"), await record("index.html")],
  };
  const manifestPath = path.join(temporaryRoot, "release-manifest.json");
  await fsp.writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
  const manifestHash = await sha256File(manifestPath);
  const verified = await verifyWebRelease(temporaryRoot, manifestHash);
  assert.equal(verified.verifiedFiles, 2);
  assert.equal(verified.manifestSha256, manifestHash);
  assert.equal(requireWebGL2Release(verified.manifest), verified.manifest);
  assert.throws(() => requireWebGL2Release({ build: { webgl2Renderer: false } }),
    /requires an audited WebGL2 browser release/);
  assert.throws(() => requireWebGL2Release({ sourceCompliance: {
    buildProvenance: { webgl2Renderer: true },
  } }), /requires an audited WebGL2 browser release/);

  const inlineDigest = crypto.createHash("sha256")
    .update("globalThis.ready = true;", "utf8").digest("base64");
  const csp = contentSecurityPolicy(index);
  assert.match(csp, new RegExp(`sha256-${inlineDigest.replace(/[+]/g, "\\+")}`));
  assert.match(csp, /'wasm-unsafe-eval'/);
  assert.doesNotMatch(csp, /'unsafe-inline'|'unsafe-eval'/);
  assert.match(csp, /object-src 'none'/);

  const allowed = new Set(["www.oldunreal.com"]);
  assert.equal(isAllowedExternalUrl("https://www.oldunreal.com/downloads/", allowed), true);
  assert.equal(isAllowedExternalUrl("https://www.oldunreal.com.evil.invalid/", allowed), false);
  assert.equal(isAllowedExternalUrl("http://www.oldunreal.com/", allowed), false);
  assert.equal(isTrustedOriginUrl("http://127.0.0.1:8123/path", "http://127.0.0.1:8123"), true);
  assert.equal(isTrustedOriginUrl("http://127.0.0.1:8124/path", "http://127.0.0.1:8123"), false);
  assert.equal(isPathInside(temporaryRoot, path.join(temporaryRoot, "assets", "app.js")), true);
  assert.equal(isPathInside(temporaryRoot, path.resolve(temporaryRoot, "..", "escape")), false);

  await fsp.writeFile(path.join(temporaryRoot, "assets", "app.js"), "tampered\n");
  await assert.rejects(verifyWebRelease(temporaryRoot, manifestHash), /integrity verification/);
  await fsp.writeFile(path.join(temporaryRoot, "assets", "app.js"), asset);
  await fsp.writeFile(path.join(temporaryRoot, "extra.txt"), "undeclared\n");
  await assert.rejects(verifyWebRelease(temporaryRoot, manifestHash), /file set/);
} finally {
  await fsp.rm(temporaryRoot, { recursive: true, force: true });
}

console.log("Electron host security and release-pin tests passed");
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
