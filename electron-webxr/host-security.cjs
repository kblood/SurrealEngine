"use strict";

const crypto = require("node:crypto");
const fs = require("node:fs");
const fsp = require("node:fs/promises");
const path = require("node:path");

const SHA256_PATTERN = /^[0-9a-f]{64}$/;

function isPathInside(root, candidate) {
  const relative = path.relative(root, candidate);
  return relative === "" || (!relative.startsWith(`..${path.sep}`) &&
    relative !== ".." && !path.isAbsolute(relative));
}

function isAllowedExternalUrl(target, allowedHosts) {
  try {
    const url = new URL(target);
    return url.protocol === "https:" && allowedHosts.has(url.hostname) &&
      !url.username && !url.password;
  } catch {
    return false;
  }
}

function isTrustedOriginUrl(target, expectedOrigin) {
  try { return new URL(target).origin === expectedOrigin; }
  catch { return false; }
}

function sha256Buffer(buffer) {
  return crypto.createHash("sha256").update(buffer).digest("hex");
}

async function sha256File(filename) {
  const hash = crypto.createHash("sha256");
  await new Promise((resolve, reject) => {
    const stream = fs.createReadStream(filename);
    stream.on("error", reject);
    stream.on("data", chunk => hash.update(chunk));
    stream.on("end", resolve);
  });
  return hash.digest("hex");
}

function portableManifestPath(value) {
  return typeof value === "string" && value.length > 0 &&
    !value.includes("\\") && !value.startsWith("/") &&
    value.split("/").every(part => part && part !== "." && part !== "..");
}

async function releaseFiles(root) {
  const files = [];
  async function visit(directory) {
    for (const entry of await fsp.readdir(directory, { withFileTypes: true })) {
      const absolute = path.join(directory, entry.name);
      if (entry.isSymbolicLink())
        throw new Error(`Bundled web release contains a symbolic link: ${path.relative(root, absolute)}`);
      if (entry.isDirectory()) await visit(absolute);
      else if (entry.isFile()) files.push(path.relative(root, absolute).split(path.sep).join("/"));
      else throw new Error(`Bundled web release contains an unsupported entry: ${path.relative(root, absolute)}`);
    }
  }
  await visit(root);
  return files.sort();
}

async function verifyWebRelease(webRoot, expectedManifestSha256) {
  if (!SHA256_PATTERN.test(expectedManifestSha256 || ""))
    throw new Error("The packaged release pin does not contain a valid manifest SHA-256.");
  const canonicalRoot = await fsp.realpath(webRoot);
  const manifestPath = path.join(canonicalRoot, "release-manifest.json");
  const manifestBytes = await fsp.readFile(manifestPath);
  const manifestSha256 = sha256Buffer(manifestBytes);
  if (manifestSha256 !== expectedManifestSha256)
    throw new Error("Bundled web release manifest does not match the ASAR-protected release pin.");

  let manifest;
  try { manifest = JSON.parse(manifestBytes.toString("utf8")); }
  catch { throw new Error("Bundled web release manifest is invalid JSON."); }
  const supportedSchema =
    (manifest.schema === "surrealengine-browser-release-v1" && manifest.version === 1) ||
    (manifest.schema === "surrealengine-browser-release-v2" && manifest.version === 2);
  if (!supportedSchema || !Array.isArray(manifest.files) || !manifest.files.length)
    throw new Error("Bundled web release manifest has an unsupported schema.");

  const recorded = new Map();
  for (const record of manifest.files) {
    if (!record || !portableManifestPath(record.path) ||
      !Number.isSafeInteger(record.bytes) || record.bytes < 0 ||
      !SHA256_PATTERN.test(record.sha256 || "") || recorded.has(record.path))
      throw new Error("Bundled web release manifest contains an invalid file record.");
    recorded.set(record.path, record);
  }

  const actual = (await releaseFiles(canonicalRoot))
    .filter(name => name !== "release-manifest.json");
  const expected = [...recorded.keys()].sort();
  if (actual.length !== expected.length || actual.some((name, index) => name !== expected[index]))
    throw new Error("Bundled web release file set does not match its pinned manifest.");

  for (const name of expected) {
    const absolute = path.join(canonicalRoot, ...name.split("/"));
    const canonicalFile = await fsp.realpath(absolute);
    if (!isPathInside(canonicalRoot, canonicalFile))
      throw new Error(`Bundled web release file escaped its root: ${name}`);
    const information = await fsp.stat(canonicalFile);
    const record = recorded.get(name);
    if (!information.isFile() || information.size !== record.bytes ||
      await sha256File(canonicalFile) !== record.sha256)
      throw new Error(`Bundled web release file failed integrity verification: ${name}`);
  }

  const indexHtml = await fsp.readFile(path.join(canonicalRoot, "index.html"), "utf8");
  return Object.freeze({ canonicalRoot, indexHtml, manifest, manifestSha256,
    verifiedFiles: expected.length });
}

function contentSecurityPolicy(indexHtml) {
  const inlineHashes = [];
  const scriptPattern = /<script(?![^>]*\bsrc\s*=)[^>]*>([\s\S]*?)<\/script>/gi;
  for (const match of indexHtml.matchAll(scriptPattern)) {
    const digest = crypto.createHash("sha256").update(match[1], "utf8").digest("base64");
    inlineHashes.push(`'sha256-${digest}'`);
  }
  return [
    "default-src 'self'",
    `script-src 'self' 'wasm-unsafe-eval' ${inlineHashes.join(" ")}`.trim(),
    "style-src 'self'",
    "img-src 'self' data: blob:",
    "media-src 'self' blob:",
    "connect-src 'self'",
    "worker-src 'self' blob:",
    "object-src 'none'",
    "base-uri 'none'",
    "frame-ancestors 'none'",
    "form-action 'none'"
  ].join("; ");
}

function requireWebGL2Release(manifest) {
  if (manifest?.build?.webgl2Renderer !== true)
    throw new Error("Electron WebXR packaging requires an audited WebGL2 browser release.");
  return manifest;
}

module.exports = Object.freeze({
  contentSecurityPolicy,
  isAllowedExternalUrl,
  isPathInside,
  isTrustedOriginUrl,
  requireWebGL2Release,
  sha256File,
  verifyWebRelease,
});
