"use strict";

const { app, BrowserWindow, dialog, ipcMain, shell } = require("electron");
const crypto = require("node:crypto");
const fs = require("node:fs");
const fsp = require("node:fs/promises");
const http = require("node:http");
const path = require("node:path");
const {
  contentSecurityPolicy,
  isAllowedExternalUrl,
  isPathInside,
  isTrustedOriginUrl,
  sha256File,
  verifyWebRelease,
} = require("./host-security.cjs");

const DIAGNOSTICS_ONLY = process.argv.includes("--diagnostics-only");
const EXPECT_IMMERSIVE_VR = process.argv.includes("--expect-immersive-vr");
const FORCE_OPENXR = process.argv.includes("--force-openxr");
const EXPERIMENTAL_WEBGPU_XR = process.argv.includes("--experimental-webgpu-xr");
const APP_PORT = 47991;
const MAX_GAME_DIRECTORY_DEPTH = 32;
const MAX_GAME_FILES = 50000;
const MAX_GAME_FILE_BYTES = 1024 * 1024 * 1024;
const MAX_GAME_TOTAL_BYTES = 32 * 1024 * 1024 * 1024;
const EXTERNAL_HOSTS = new Set(["www.oldunreal.com", "www.epicgames.com"]);
const MIME_TYPES = new Map([
  [".css", "text/css; charset=utf-8"],
  [".gz", "application/gzip"],
  [".html", "text/html; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".map", "application/json; charset=utf-8"],
  [".md", "text/plain; charset=utf-8"],
  [".mjs", "text/javascript; charset=utf-8"],
  [".mp3", "audio/mpeg"],
  [".mp4", "video/mp4"],
  [".ogg", "audio/ogg"],
  [".png", "image/png"],
  [".svg", "image/svg+xml"],
  [".txt", "text/plain; charset=utf-8"],
  [".wav", "audio/wav"],
  [".wasm", "application/wasm"],
  [".webm", "video/webm"],
  [".webp", "image/webp"],
]);

if (EXPERIMENTAL_WEBGPU_XR)
  app.commandLine.appendSwitch("enable-features", "WebXRIncubations,OpenXrExtendedFeatureSupport");
if (FORCE_OPENXR) app.commandLine.appendSwitch("force-webxr-runtime", "openxr");
app.enableSandbox();
app.commandLine.appendSwitch("force_high_performance_gpu");
app.commandLine.appendSwitch("disable-renderer-backgrounding");
app.commandLine.appendSwitch("enable-logging", "file");
app.commandLine.appendSwitch("log-file", path.join(app.getPath("userData"), "electron-chromium.log"));

let server;
let mainWindow;
let appOrigin;
let releaseVerification;
let approvedRoot = null;
const fileTokens = new Map();
const singleInstance = app.requestSingleInstanceLock();
if (!singleInstance) app.exit(0);
else app.on("second-instance", () => {
  if (!mainWindow) return;
  if (mainWindow.isMinimized()) mainWindow.restore();
  mainWindow.show();
  mainWindow.focus();
});

function getWebReleaseRoot() {
  const configured = process.env.SURREAL_WEB_RELEASE;
  if (configured && !app.isPackaged) return path.resolve(configured);
  return app.isPackaged
    ? path.join(process.resourcesPath, "web-release")
    : path.join(__dirname, "web-release");
}

async function getReleasePin(webRoot) {
  try { return require("./release-pin.json"); }
  catch (error) {
    if (app.isPackaged) throw new Error(`Packaged release pin is unavailable: ${error.message}`);
    return {
      schema: "surrealengine-electron-release-pin-v1",
      manifestSha256: await sha256File(path.join(webRoot, "release-manifest.json")),
      developmentOnly: true,
    };
  }
}

function applyIsolationHeaders(response, csp) {
  response.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
  response.setHeader("Cross-Origin-Opener-Policy", "same-origin");
  response.setHeader("Cross-Origin-Resource-Policy", "same-origin");
  response.setHeader("Origin-Agent-Cluster", "?1");
  response.setHeader("Permissions-Policy", "xr-spatial-tracking=(self)");
  response.setHeader("Referrer-Policy", "no-referrer");
  response.setHeader("X-Content-Type-Options", "nosniff");
  response.setHeader("Content-Security-Policy", csp);
  response.setHeader("Cache-Control", "no-store");
}

async function startStaticServer(webRoot, releasePin) {
  releaseVerification = await verifyWebRelease(webRoot, releasePin.manifestSha256);
  const canonicalRoot = releaseVerification.canonicalRoot;
  const csp = contentSecurityPolicy(releaseVerification.indexHtml);

  server = http.createServer(async (request, response) => {
    applyIsolationHeaders(response, csp);
    try {
      if (request.method !== "GET" && request.method !== "HEAD") {
        response.setHeader("Allow", "GET, HEAD");
        response.writeHead(405).end("Method not allowed");
        return;
      }
      const url = new URL(request.url, "http://127.0.0.1");
      let relativePath = decodeURIComponent(url.pathname).replace(/^\/+/, "");
      if (!relativePath) relativePath = "index.html";
      const candidate = path.resolve(canonicalRoot, relativePath);
      if (!isPathInside(canonicalRoot, candidate)) {
        response.writeHead(403).end("Forbidden");
        return;
      }
      const candidateReal = await fsp.realpath(candidate);
      if (!isPathInside(canonicalRoot, candidateReal)) {
        response.writeHead(403).end("Forbidden");
        return;
      }
      const fileStat = await fsp.stat(candidateReal);
      if (!fileStat.isFile()) throw Object.assign(new Error("Not a file"), { code: "ENOENT" });
      const portable = path.relative(canonicalRoot, candidateReal).split(path.sep).join("/");
      const record = releaseVerification.manifest.files.find(item => item.path === portable);
      response.setHeader("Content-Type", record?.expectedMime ||
        MIME_TYPES.get(path.extname(candidateReal).toLowerCase()) || "application/octet-stream");
      if (record?.contentEncoding) response.setHeader("Content-Encoding", record.contentEncoding);
      response.setHeader("Accept-Ranges", "bytes");
      if (fileStat.size === 0) {
        if (request.headers.range) {
          response.setHeader("Content-Range", "bytes */0");
          response.writeHead(416).end();
        } else {
          response.setHeader("Content-Length", 0);
          response.writeHead(200).end();
        }
        return;
      }
      let start = 0;
      let end = fileStat.size - 1;
      let status = 200;
      const range = request.headers.range;
      if (range) {
        const match = /^bytes=(\d+)-(\d*)$/.exec(range);
        if (!match) {
          response.setHeader("Content-Range", `bytes */${fileStat.size}`);
          response.writeHead(416).end();
          return;
        }
        start = Number(match[1]);
        end = match[2] ? Number(match[2]) : end;
        if (!Number.isSafeInteger(start) || !Number.isSafeInteger(end) ||
          start < 0 || end < start || start >= fileStat.size) {
          response.setHeader("Content-Range", `bytes */${fileStat.size}`);
          response.writeHead(416).end();
          return;
        }
        end = Math.min(end, fileStat.size - 1);
        status = 206;
        response.setHeader("Content-Range", `bytes ${start}-${end}/${fileStat.size}`);
      }
      response.setHeader("Content-Length", Math.max(end - start + 1, 0));
      response.writeHead(status);
      if (request.method === "HEAD") response.end();
      else {
        const stream = fs.createReadStream(candidateReal, { start, end });
        stream.on("error", error => response.destroy(error));
        request.on("aborted", () => stream.destroy());
        stream.pipe(response);
      }
    } catch (error) {
      const status = error.code === "ENOENT" ? 404 : 400;
      response.writeHead(status).end(status === 404 ? "Not found" : "Bad request");
    }
  });

  await new Promise((resolve, reject) => {
    server.once("error", error => reject(error.code === "EADDRINUSE" ?
      new Error(`SurrealEngine Electron origin port ${APP_PORT} is already in use; close the other wrapper instance or conflicting process.`) : error));
    server.listen(APP_PORT, "127.0.0.1", resolve);
  });
  const address = server.address();
  appOrigin = `http://127.0.0.1:${address.port}`;
  return `${appOrigin}/`;
}

async function scanApprovedDirectory(selectedPath) {
  const root = await fsp.realpath(selectedPath);
  const rootStat = await fsp.stat(root);
  if (!rootStat.isDirectory()) throw new Error("The selected path is not a directory.");
  approvedRoot = root;
  fileTokens.clear();
  const records = [];
  let totalBytes = 0;

  async function visit(directory, depth) {
    if (depth > MAX_GAME_DIRECTORY_DEPTH)
      throw new Error(`Game folder nesting exceeds ${MAX_GAME_DIRECTORY_DEPTH} levels.`);
    const entries = await fsp.readdir(directory, { withFileTypes: true });
    for (const entry of entries) {
      const absolutePath = path.join(directory, entry.name);
      const entryStat = await fsp.lstat(absolutePath);
      if (entry.isSymbolicLink() || entryStat.isSymbolicLink()) {
        throw new Error(`Symbolic links are not accepted in game folders: ${path.relative(root, absolutePath)}`);
      }
      if (entry.isDirectory()) {
        await visit(absolutePath, depth + 1);
        continue;
      }
      if (!entry.isFile()) continue;
      if (records.length >= MAX_GAME_FILES)
        throw new Error(`Game folder contains more than ${MAX_GAME_FILES} files.`);
      const canonicalFile = await fsp.realpath(absolutePath);
      if (!isPathInside(root, canonicalFile)) throw new Error("A game file resolved outside the approved folder.");
      const fileStat = await fsp.stat(canonicalFile);
      if (fileStat.size > MAX_GAME_FILE_BYTES)
        throw new Error(`Game file exceeds the ${MAX_GAME_FILE_BYTES}-byte import limit: ${path.relative(root, canonicalFile)}`);
      totalBytes += fileStat.size;
      if (totalBytes > MAX_GAME_TOTAL_BYTES)
        throw new Error(`Game folder exceeds the ${MAX_GAME_TOTAL_BYTES}-byte import limit.`);
      const token = crypto.randomUUID();
      fileTokens.set(token, Object.freeze({ path: canonicalFile, size: fileStat.size,
        mtimeMs: fileStat.mtimeMs, dev: fileStat.dev, ino: fileStat.ino }));
      records.push({
        path: path.relative(root, canonicalFile).split(path.sep).join("/"),
        size: fileStat.size,
        token,
      });
    }
  }

  await visit(root, 0);
  return records;
}

function assertTrustedSender(event) {
  const senderUrl = event.senderFrame?.url || "";
  if (!appOrigin || !mainWindow || event.sender !== mainWindow.webContents ||
    event.senderFrame !== mainWindow.webContents.mainFrame ||
    !isTrustedOriginUrl(senderUrl, appOrigin))
    throw new Error("Rejected IPC from an untrusted origin.");
}

function installHostBridgeHandlers() {
  ipcMain.handle("surreal:pick-game-directory", async event => {
    assertTrustedSender(event);
    const result = await dialog.showOpenDialog(mainWindow, {
      title: "Choose an Unreal Tournament or Unreal Gold folder",
      properties: ["openDirectory", "dontAddToRecent"],
    });
    if (result.canceled || result.filePaths.length !== 1) return null;
    return scanApprovedDirectory(result.filePaths[0]);
  });

  ipcMain.handle("surreal:read-game-file", async (event, token) => {
    assertTrustedSender(event);
    if (typeof token !== "string" || !fileTokens.has(token) || !approvedRoot) throw new Error("Unknown or expired game-file token.");
    const issued = fileTokens.get(token);
    const canonicalFile = await fsp.realpath(issued.path);
    if (!isPathInside(approvedRoot, canonicalFile)) throw new Error("The requested game file escaped the approved folder.");
    const fileStat = await fsp.stat(canonicalFile);
    if (!fileStat.isFile() || fileStat.size !== issued.size ||
      fileStat.mtimeMs !== issued.mtimeMs || fileStat.dev !== issued.dev || fileStat.ino !== issued.ino)
      throw new Error("The requested game file changed after directory approval; choose the folder again.");
    return new Uint8Array(await fsp.readFile(canonicalFile));
  });
}

async function collectDiagnostics() {
  const wasmPath = releaseVerification.manifest.entrypoints?.wasm ||
    releaseVerification.manifest.files.find(record =>
      /^engine\/SurrealEngine(?:\.[0-9a-f]{64})?\.wasm$/.test(record.path))?.path;
  if (!wasmPath) throw new Error("Pinned browser release has no diagnostic WASM entrypoint.");
  const wasmProbeUrl = new URL(wasmPath, `${appOrigin}/`).href;
  const renderer = await mainWindow.webContents.executeJavaScript(`(async () => {
    const pinnedWasmUrl = ${JSON.stringify(wasmProbeUrl)};
    const withTimeout = promise => Promise.race([
      promise,
      new Promise(resolve => setTimeout(() => resolve("timeout"), 8000))
    ]);
    const supported = async mode => {
      if (!navigator.xr) return false;
      try { return await withTimeout(navigator.xr.isSessionSupported(mode)); }
      catch (error) { return { error: String(error) }; }
    };
    const httpProbe = async () => {
      try {
        const indexResponse = await fetch(location.href, { cache: "no-store" });
        const headResponse = await fetch(location.href, { method: "HEAD", cache: "no-store" });
        const postResponse = await fetch(location.href, { method: "POST", body: "denied" });
        const traversalResponse = await fetch(new URL("/%2e%2e/package.json", location.href));
        const rangeResponse = await fetch(pinnedWasmUrl, { headers: { Range: "bytes=0-3" }, cache: "no-store" });
        const wasmResponse = await fetch(pinnedWasmUrl, { cache: "no-store" });
        const compileResult = await withTimeout(WebAssembly.compileStreaming(wasmResponse.clone())
          .then(() => true, error => ({ error: String(error) })));
        const result = {
          indexStatus: indexResponse.status,
          headStatus: headResponse.status,
          postStatus: postResponse.status,
          traversalStatus: traversalResponse.status,
          rangeStatus: rangeResponse.status,
          contentRange: rangeResponse.headers.get("content-range"),
          wasmMime: wasmResponse.headers.get("content-type"),
          wasmCompileStreaming: compileResult,
          coop: indexResponse.headers.get("cross-origin-opener-policy"),
          coep: indexResponse.headers.get("cross-origin-embedder-policy"),
          corp: indexResponse.headers.get("cross-origin-resource-policy"),
          csp: indexResponse.headers.get("content-security-policy"),
        };
        result.passed = result.indexStatus === 200 && result.headStatus === 200 &&
          result.postStatus === 405 && result.traversalStatus !== 200 &&
          result.rangeStatus === 206 && result.contentRange?.startsWith("bytes 0-3/") &&
          result.wasmMime === "application/wasm" && result.wasmCompileStreaming === true &&
          result.coop === "same-origin" && result.coep === "require-corp" &&
          result.corp === "same-origin" && Boolean(result.csp);
        return result;
      } catch (error) { return { passed: false, error: String(error) }; }
    };
    return {
      url: location.href,
      secureContext: window.isSecureContext,
      crossOriginIsolated: window.crossOriginIsolated,
      userAgent: navigator.userAgent,
      webgpu: Boolean(navigator.gpu),
      webxr: Boolean(navigator.xr),
      xrWebGLLayer: typeof XRWebGLLayer === "function",
      xrGpuBinding: typeof XRGPUBinding === "function",
      webgl2: (() => {
        try { return Boolean(document.createElement("canvas").getContext("webgl2")); }
        catch { return false; }
      })(),
      sessions: {
        inline: await supported("inline"),
        immersiveVr: await supported("immersive-vr"),
        immersiveAr: await supported("immersive-ar")
      },
      httpProbe: await httpProbe(),
    };
  })()`);
  const immersiveVr = renderer.sessions.immersiveVr === true;
  if (!renderer.httpProbe?.passed)
    throw new Error(`Electron localhost/WASM diagnostic probe failed: ${JSON.stringify(renderer.httpProbe)}`);
  const preferences = mainWindow.webContents.getLastWebPreferences();
  const security = {
    sandbox: preferences.sandbox === true,
    contextIsolation: preferences.contextIsolation === true,
    nodeIntegrationDisabled: preferences.nodeIntegration === false,
    webSecurity: preferences.webSecurity !== false,
    devTools: preferences.devTools === true,
  };
  if (!security.sandbox || !security.contextIsolation ||
    !security.nodeIntegrationDisabled || !security.webSecurity ||
    (app.isPackaged && security.devTools))
    throw new Error(`Electron renderer security preferences failed policy: ${JSON.stringify(security)}`);
  return {
    generatedAt: new Date().toISOString(),
    wrapper: { electron: process.versions.electron, chrome: process.versions.chrome, node: process.versions.node },
    openxrForced: FORCE_OPENXR,
    experimentalWebGPUXR: EXPERIMENTAL_WEBGPU_XR,
    immersiveExpectation: EXPECT_IMMERSIVE_VR,
    qualification: immersiveVr ? "immersive-capability-detected" :
      (EXPECT_IMMERSIVE_VR ? "failed-expected-immersive-capability" : "not-qualified-no-immersive-capability"),
    commandLine: {
      forceWebXRRuntime: app.commandLine.getSwitchValue("force-webxr-runtime") || null,
      enableFeatures: app.commandLine.getSwitchValue("enable-features") || null,
    },
    release: releaseVerification ? {
      manifestSha256: releaseVerification.manifestSha256,
      buildId: releaseVerification.manifest.buildId || null,
      sourceCommit: releaseVerification.manifest.sourceCompliance?.sourceCommit || null,
      verifiedFiles: releaseVerification.verifiedFiles,
    } : null,
    security,
    renderer,
  };
}

async function writeDiagnostics(report) {
  const destination = process.env.SURREAL_ELECTRON_DIAGNOSTICS || path.join(app.getPath("userData"), "webxr-diagnostics.json");
  await fsp.mkdir(path.dirname(destination), { recursive: true });
  await fsp.writeFile(destination, `${JSON.stringify(report, null, 2)}\n`, "utf8");
  process.stdout.write(`${JSON.stringify({ diagnostics: destination, report })}\n`);
}

async function createWindow(startUrl) {
  mainWindow = new BrowserWindow({
    width: 1440,
    height: 900,
    minWidth: 960,
    minHeight: 640,
    backgroundColor: "#11151d",
    autoHideMenuBar: true,
    show: !DIAGNOSTICS_ONLY,
    title: "SurrealEngine WebXR Test",
    webPreferences: {
      preload: path.join(__dirname, "preload.cjs"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      webSecurity: true,
      allowRunningInsecureContent: false,
      webviewTag: false,
      navigateOnDragDrop: false,
      safeDialogs: true,
      devTools: false,
      backgroundThrottling: false,
      spellcheck: false,
    },
  });

  const trustedPermission = (permission, origin) =>
    isTrustedOriginUrl(origin, appOrigin) &&
    (permission === "pointerLock" || permission === "fullscreen");
  mainWindow.webContents.session.setPermissionCheckHandler(
    (_webContents, permission, requestingOrigin) => trustedPermission(permission, requestingOrigin));
  mainWindow.webContents.session.setPermissionRequestHandler(
    (webContents, permission, callback, details) => callback(
      webContents === mainWindow.webContents && trustedPermission(permission,
        details.requestingUrl || webContents.getURL())));
  mainWindow.webContents.on("will-attach-webview", event => event.preventDefault());
  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    if (isAllowedExternalUrl(url, EXTERNAL_HOSTS)) void shell.openExternal(url);
    return { action: "deny" };
  });
  mainWindow.webContents.on("will-navigate", (event, target) => {
    if (isTrustedOriginUrl(target, appOrigin)) return;
    event.preventDefault();
    if (isAllowedExternalUrl(target, EXTERNAL_HOSTS)) void shell.openExternal(target);
  });
  mainWindow.on("closed", () => {
    approvedRoot = null;
    fileTokens.clear();
    mainWindow = null;
  });
  await mainWindow.loadURL(startUrl);
  const report = await collectDiagnostics();
  await writeDiagnostics(report);
  if (DIAGNOSTICS_ONLY) app.exit(EXPECT_IMMERSIVE_VR &&
    report.renderer.sessions.immersiveVr !== true ? 2 : 0);
}

if (singleInstance) app.whenReady().then(async () => {
  try {
    const webRoot = getWebReleaseRoot();
    const releasePin = await getReleasePin(webRoot);
    const startUrl = await startStaticServer(webRoot, releasePin);
    installHostBridgeHandlers();
    await createWindow(startUrl);
  } catch (error) {
    process.stderr.write(`SurrealEngine Electron startup failed: ${error.stack || error}\n`);
    app.exit(1);
  }
});

app.on("window-all-closed", () => app.quit());
app.on("before-quit", () => {
  if (server) server.close();
});
