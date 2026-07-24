# SurrealEngine Electron WebXR test wrapper

This is an unsigned internal Windows diagnostic wrapper for testing an audited,
data-free SurrealEngine WebGL 2/WASM release against a desktop WebXR/OpenXR
runtime. Electron/Chromium immersive headset exposure is not a supported
product guarantee, so this package is not promoted until a named physical
headset/runtime test enters VR, renders, exits to the same flat game, and
re-enters without restarting the process.

## Runtime behavior

- Serves the bundled web release from the stable single-instance
  `http://127.0.0.1:47991` origin with COOP/COEP headers. The fixed origin keeps
  OPFS/IndexedDB imports, settings, and saves attached across wrapper launches;
  a port conflict fails instead of silently creating a new storage origin.
- Lets Chromium choose its runtime by default. Use `--force-openxr` only as a
  troubleshooting comparison; this is a Chromium implementation switch, not a
  supported Electron product contract.
- Uses core WebXR by default. Start with `--experimental-webgpu-xr` only when
  deliberately testing Chromium's WebGPU-WebXR incubation and extended OpenXR
  features; this experimental mode is not a production default.
- Uses a sandboxed renderer with context isolation and no Node.js integration.
- Pins the complete browser release manifest inside integrity-checked ASAR,
  verifies every bundled web file before serving it, applies a restrictive CSP,
  and denies renderer permissions other than same-origin pointer lock and
  fullscreen.
- Flips Electron fuses to disable RunAsNode, Node options/inspection, and
  non-ASAR app loading. The package remains unsigned and is therefore suitable
  for controlled testing, not public distribution.
- Exposes only opaque, session-scoped file tokens after an explicit game-folder
  choice. Symbolic links and paths outside that folder are rejected.
- Opens only the bundled release locally; allowlisted Epic and OldUnreal links
  open in the system browser.

## Test procedure

1. Install and select an OpenXR runtime (for example SteamVR, Oculus, or
   Windows Mixed Reality), then connect the headset before launching the app.
2. Run `SurrealEngine-WebXR-Test.exe`.
3. Confirm the capability panel reports WebXR. Import your own supported Unreal
   Tournament or Unreal Gold folder, start the game flat with WebGL 2, and use
   the post-launch **Enter VR** control.
4. Inspect `%APPDATA%/surrealengine-electron-webxr-test/webxr-diagnostics.json`
   and `electron-chromium.log` if immersive VR is unavailable.

For a noninteractive capability check, run:

```powershell
./SurrealEngine-WebXR-Test.exe --diagnostics-only
```

The process writes a JSON report and exits. `immersiveVr: false` means Chromium
did not see an immersive device/runtime in that launch; it is a
`not-qualified` hardware result, not an application failure by itself.

To compare Chromium's forced OpenXR selection against the default automatic
runtime selection, run:

```powershell
./SurrealEngine-WebXR-Test.exe --diagnostics-only --force-openxr
```

In a headset lab, add `--expect-immersive-vr`; diagnostics-only mode exits with
code 2 if Chromium cannot see an immersive runtime. A passing capability probe
still does not replace the manual Enter VR → first stereo frame → controller →
Exit VR → re-entry qualification sequence.

To compare the experimental direct WebGPU-WebXR path against the default core
WebXR/WebGL compatibility path, run:

```powershell
./SurrealEngine-WebXR-Test.exe --experimental-webgpu-xr
```

## Build

From this directory, with Node.js 22.12 or newer:

```powershell
npm ci
npm run check
npm run package:win -- --web-release=C:\path\to\audited\SurrealEngine --out=C:\Devstuff\QuestGames\SurrealEngine\out\electron-webxr-<commit>
```

Packaging requires a clean worktree, independently audits the browser release,
re-verifies the copied resources, embeds the manifest SHA-256 in ASAR, produces
a complete file inventory, and creates a ZIP plus
`electron-release-artifacts.json`. The input browser release must itself be a
clean, data-free WebGL 2 build with its corresponding-source archive.
