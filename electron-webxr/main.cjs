"use strict";

const { app, BrowserWindow, dialog, ipcMain, shell } = require("electron");
const crypto = require("node:crypto");
const fs = require("node:fs");
const fsp = require("node:fs/promises");
const http = require("node:http");
const path = require("node:path");

const DIAGNOSTICS_ONLY = process.argv.includes("--diagnostics-only");
const FORCE_OPENXR = !process.argv.includes("--no-force-openxr");
const EXPERIMENTAL_WEBGPU_XR = process.argv.includes("--experimental-webgpu-xr");
const EXTERNAL_HOSTS = new Set(["www.oldunreal.com", "www.epicgames.com"]);
const MIME_TYPES = new Map([
  [".css", "text/css; charset=utf-8"],
  [".gz", "application/gzip"],
  [".html", "text/html; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".md", "text/plain; charset=utf-8"],
  [".txt", "text/plain; charset=utf-8"],
  [".wasm", "application/wasm"],
]);

if (EXPERIMENTAL_WEBGPU_XR)
  app.commandLine.appendSwitch("enable-features", "WebXRIncubations,OpenXrExtendedFeatureSupport");
if (FORCE_OPENXR) app.commandLine.appendSwitch("force-webxr-runtime", "openxr");
app.commandLine.appendSwitch("force_high_performance_gpu");
app.commandLine.appendSwitch("disable-renderer-backgrounding");
app.commandLine.appendSwitch("enable-logging", "file");
app.commandLine.appendSwitch("log-file", path.join(app.getPath("userData"), "electron-chromium.log"));

let server;
let mainWindow;
let appOrigin;
let approvedRoot = null;
const fileTokens = new Map();

function isPathInside(root, candidate) {
  const relative = path.relative(root, candidate);
  return relative === "" || (!relative.startsWith(`..${path.sep}`) && relative !== ".." && !path.isAbsolute(relative));
}

function getWebReleaseRoot() {
  const configured = process.env.SURREAL_WEB_RELEASE;
  if (configured) return path.resolve(configured);
  return app.isPackaged
    ? path.join(process.resourcesPath, "web-release")
    : path.join(__dirname, "web-release");
}

function applyIsolationHeaders(response) {
  response.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
  response.setHeader("Cross-Origin-Opener-Policy", "same-origin");
  response.setHeader("Cross-Origin-Resource-Policy", "same-origin");
  response.setHeader("Origin-Agent-Cluster", "?1");
  response.setHeader("Permissions-Policy", "xr-spatial-tracking=(self)");
  response.setHeader("Referrer-Policy", "no-referrer");
  response.setHeader("X-Content-Type-Options", "nosniff");
  response.setHeader("Cache-Control", "no-store");
}

async function startStaticServer(webRoot) {
  const canonicalRoot = await fsp.realpath(webRoot);
  const stat = await fsp.stat(path.join(canonicalRoot, "index.html"));
  if (!stat.isFile()) throw new Error(`Web release has no index.html: ${canonicalRoot}`);

  server = http.createServer(async (request, response) => {
    applyIsolationHeaders(response);
    try {
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
      response.setHeader("Content-Type", MIME_TYPES.get(path.extname(candidateReal).toLowerCase()) || "application/octet-stream");
      response.setHeader("Content-Length", fileStat.size);
      response.writeHead(200);
      if (request.method === "HEAD") response.end();
      else fs.createReadStream(candidateReal).pipe(response);
    } catch (error) {
      const status = error.code === "ENOENT" ? 404 : 400;
      response.writeHead(status).end(status === 404 ? "Not found" : "Bad request");
    }
  });

  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
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

  async function visit(directory) {
    const entries = await fsp.readdir(directory, { withFileTypes: true });
    for (const entry of entries) {
      const absolutePath = path.join(directory, entry.name);
      if (entry.isSymbolicLink()) {
        throw new Error(`Symbolic links are not accepted in game folders: ${path.relative(root, absolutePath)}`);
      }
      if (entry.isDirectory()) {
        await visit(absolutePath);
        continue;
      }
      if (!entry.isFile()) continue;
      const canonicalFile = await fsp.realpath(absolutePath);
      if (!isPathInside(root, canonicalFile)) throw new Error("A game file resolved outside the approved folder.");
      const fileStat = await fsp.stat(canonicalFile);
      const token = crypto.randomUUID();
      fileTokens.set(token, canonicalFile);
      records.push({
        path: path.relative(root, canonicalFile).split(path.sep).join("/"),
        size: fileStat.size,
        token,
      });
    }
  }

  await visit(root);
  return records;
}

function assertTrustedSender(event) {
  const senderUrl = event.senderFrame?.url || "";
  if (!appOrigin || !senderUrl.startsWith(`${appOrigin}/`)) throw new Error("Rejected IPC from an untrusted origin.");
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
    const canonicalFile = await fsp.realpath(fileTokens.get(token));
    if (!isPathInside(approvedRoot, canonicalFile)) throw new Error("The requested game file escaped the approved folder.");
    return new Uint8Array(await fsp.readFile(canonicalFile));
  });
}

function isAllowedExternalUrl(target) {
  try {
    const url = new URL(target);
    return url.protocol === "https:" && EXTERNAL_HOSTS.has(url.hostname);
  } catch {
    return false;
  }
}

async function collectDiagnostics() {
  const renderer = await mainWindow.webContents.executeJavaScript(`(async () => {
    const withTimeout = promise => Promise.race([
      promise,
      new Promise(resolve => setTimeout(() => resolve("timeout"), 8000))
    ]);
    const supported = async mode => {
      if (!navigator.xr) return false;
      try { return await withTimeout(navigator.xr.isSessionSupported(mode)); }
      catch (error) { return { error: String(error) }; }
    };
    return {
      url: location.href,
      secureContext: window.isSecureContext,
      crossOriginIsolated: window.crossOriginIsolated,
      userAgent: navigator.userAgent,
      webgpu: Boolean(navigator.gpu),
      webxr: Boolean(navigator.xr),
      sessions: {
        inline: await supported("inline"),
        immersiveVr: await supported("immersive-vr"),
        immersiveAr: await supported("immersive-ar")
      }
    };
  })()`);
  return {
    generatedAt: new Date().toISOString(),
    wrapper: { electron: process.versions.electron, chrome: process.versions.chrome, node: process.versions.node },
    openxrForced: FORCE_OPENXR,
    experimentalWebGPUXR: EXPERIMENTAL_WEBGPU_XR,
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
      spellcheck: false,
    },
  });

  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    if (isAllowedExternalUrl(url)) void shell.openExternal(url);
    return { action: "deny" };
  });
  mainWindow.webContents.on("will-navigate", (event, target) => {
    if (target.startsWith(`${appOrigin}/`)) return;
    event.preventDefault();
    if (isAllowedExternalUrl(target)) void shell.openExternal(target);
  });
  mainWindow.on("closed", () => {
    approvedRoot = null;
    fileTokens.clear();
    mainWindow = null;
  });
  await mainWindow.loadURL(startUrl);
  const report = await collectDiagnostics();
  await writeDiagnostics(report);
  if (DIAGNOSTICS_ONLY) app.quit();
}

app.whenReady().then(async () => {
  try {
    const startUrl = await startStaticServer(getWebReleaseRoot());
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
