# SurrealEngine Electron WebXR test wrapper

This is an experimental Windows wrapper for testing the audited SurrealEngine
WASM release against a desktop OpenXR runtime. It does not contain game data.
Electron/Chromium WebXR headset exposure is not a supported product guarantee,
so the hosted Quest browser remains the primary immersive target.

## Runtime behavior

- Serves the bundled web release from an ephemeral `127.0.0.1` origin with
  COOP/COEP headers, preserving `crossOriginIsolated` for pthreads.
- Enables Chromium's WebXR incubation and OpenXR extended feature gates.
- Forces the Chromium WebXR runtime to OpenXR by default. Start with
  `--no-force-openxr` to let Chromium choose a runtime instead.
- Uses a sandboxed renderer with context isolation and no Node.js integration.
- Exposes only opaque, session-scoped file tokens after an explicit game-folder
  choice. Symbolic links and paths outside that folder are rejected.
- Opens only the bundled release locally; allowlisted Epic and OldUnreal links
  open in the system browser.

## Test procedure

1. Install and select an OpenXR runtime (for example SteamVR, Oculus, or
   Windows Mixed Reality), then connect the headset before launching the app.
2. Run `SurrealEngine-WebXR-Test.exe`.
3. Confirm the capability panel reports WebXR. Import your own supported Unreal
   Tournament or Unreal Gold folder, select **WebXR**, and press **Play**.
4. Inspect `%APPDATA%/surrealengine-electron-webxr-test/webxr-diagnostics.json`
   and `electron-chromium.log` if immersive VR is unavailable.

For a noninteractive capability check, run:

```powershell
./SurrealEngine-WebXR-Test.exe --diagnostics-only
```

The process writes a JSON report and exits. `immersiveVr: false` means Chromium
did not see an immersive OpenXR device/runtime in that launch.

## Build

From this directory, with Node.js 22.12 or newer:

```powershell
npm ci
npm run check
npm run package:win -- --web-release=C:\path\to\audited\SurrealEngine --out=C:\path\to\output
```
