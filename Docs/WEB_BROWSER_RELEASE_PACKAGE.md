# Shared flat/WebXR browser release package

Date: 2026-07-22

## Branch and dependencies

Branch: `release/webxr-browser-package`

This is a product/release composition branch, not an upstream pull-request
source. It starts from the smallest launcher/data stack and merges the existing
WebXR runtime topic without rewriting either history:

- `pr/web-desktop-launcher` at
  `6c614d56e66e6ea8882aada892b93bc6526e0a33`; this already contains
  `pr/web-data-persistence` at `2dda89bd7a3a0a5bfd2c2e14eccf63e75099fe61`;
- `pr/webxr-input-runtime` at
  `d472068ad5894dc8cdddeefbb4f491bd118f2592`; this already contains the flat
  web platform, view/presentation seams, WebXR provider, independent input
  composition, XR-common types, packed controller bridge, and live browser
  input adapter.

The dependency merge is `6d920b8c686b39344b2a66710cd1d7c9aec0f5ce`.
`integration/unified-engine` is not a dependency or ancestor. New changes on
this branch are confined to launcher, diagnostics, static packaging, tests, and
this release document. They do not alter WebXR provider rendering or native XR
UI capture.

## User-visible behavior

`web/surreal_app.html` is one application for both modes:

- **Desktop window** uses the normal flat WebGPU canvas and ordinary browser
  keyboard/mouse input.
- **Immersive WebXR** is offered only when a secure browser exposes WebXR,
  `immersive-vr`, `XRGPUBinding`, and an XR-compatible WebGPU adapter.

The shared WebGPU device is requested from
`navigator.gpu.requestAdapter({ xrCompatible: true })` when the WebXR provider
is otherwise available. If that request fails, the launcher reports why,
disables only immersive mode, and retries ordinary WebGPU so flat play remains
available. Failed or declined immersive-session entry also returns a visible
flat fallback instead of terminating the running engine.

Before loading the engine, the page reports secure-hosting, WebAssembly,
WebGPU, local-storage, folder-import, and WebXR capability. Fatal platform
requirements stop with an actionable message. Optional WebXR failures do not.

The existing shared data layers remain authoritative:

- the user explicitly selects a local folder; no upload or automatic download
  exists;
- typed validation recognizes UT99 and Unreal Gold without weakening either
  layout contract;
- imported commercial data remains in per-game OPFS/IndexedDB storage;
- mutable INIs, settings, logs, and strict saves remain in the separate
  allowlisted, crash-safe mutable store;
- game, map, renderer, and presentation selection become fixed validated
  native arguments.

## Static artifact layout

Create a no-data Emscripten build, then package it:

```powershell
& C:\Devstuff\emsdk\emsdk_env.ps1
emcmake cmake -S . -B build-emscripten -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" -DBUILD_TESTING=OFF
cmake --build build-emscripten --target SurrealEngine --parallel 8
node web/package_browser_release.mjs --engine-dir build-emscripten --output C:\path\to\webxr\Ports\SurrealEngine
```

The output is self-contained and position-independent:

```text
webxr/Ports/SurrealEngine/
  index.html
  browser_app.css
  browser_app.js
  browser_data_bootstrap.js
  browser_release.js
  mutable_persistence.js
  ut99_importer.js
  webxr_browser_app_adapter.js
  webxr_provider.js
  engine/
    SurrealEngine.js
    SurrealEngine.wasm
  release-manifest.json
  HOSTING.txt
  _headers
  .htaccess
```

All runtime URLs are relative, so the same directory can be hosted at the
intended `/webxr/Ports/SurrealEngine/` location or another HTTPS base path.
`_headers` and `.htaccess` provide examples for the COOP/COEP isolation needed
by the threaded WASM build and the `application/wasm` MIME type. Hosts that do
not consume either file must configure equivalent headers themselves.

The packager fails unless `CMakeCache.txt` records an empty
`SURREAL_GAMEDATA_DIR`, rejects every Emscripten `.data` payload, copies only a
fixed shell/runtime allowlist, and audits the output for UE1 game extensions.
`release-manifest.json` records dependency SHAs plus each file's length,
SHA-256, and expected MIME type. It never enumerates browser-private imports.

## Validation

Synthetic tests contain no commercial data:

```text
node --check web/browser_app.js
node --check web/browser_release.js
node --check web/webxr_browser_app_adapter.js
node --check web/package_browser_release.mjs
node web/test_release_package.mjs
python web/smoke_test_browser_app.py --base-url=http://localhost:8112
python web/smoke_test_browser_app_runtime.py --base-url=http://localhost:8112
python web/smoke_test_ut99_importer.py --base-url=http://localhost:8112
python web/smoke_test_mutable_persistence.py --base-url=http://localhost:8112
python web/smoke_test_browser_data_bootstrap.py --base-url=http://localhost:8112
node web/test_webxr_provider.mjs
```

Serve a staged artifact directly for the final layout smoke:

```text
node web/serve.mjs 8113 C:\path\to\webxr\Ports\SurrealEngine
python web/smoke_test_release_package.py --base-url=http://localhost:8113
```

Current results:

- 9 shared launcher/capability checks passed;
- 19 UT99/Unreal Gold importer checks passed;
- 13 mutable-persistence checks passed;
- 3 browser bootstrap ordering/isolation checks passed;
- provider controller lifecycle Node test passed;
- fresh no-data Emscripten compile/link passed;
- real staged package reached the import gate while cross-origin isolated and
  performed a synthetic Unreal Gold flat launch with the expected arguments;
- static package/data audit and `git diff --check` passed.

## Remaining release gates

Synthetic files prove validation and launch wiring, not real game behavior.
Before publishing:

1. Import complete user-owned UT99 and Unreal Gold folders independently;
   confirm detection, map lists, launch, switching, replacement, and persistence
   after a browser restart and package upgrade.
2. Run flat UT99 and Unreal Gold with keyboard/mouse, fullscreen/resize, INI
   changes, saves, explicit quit-and-flush, and restore.
3. On a physical Quest browser, record browser/runtime versions and verify the
   XR-compatible adapter, WebGPU-backed immersive session, stereo output,
   controllers, held-button disconnect, blur, exit, and re-entry.
4. After successful, declined, and failed XR entry, confirm the same flat canvas
   continues and keyboard/mouse still work.
5. Verify production HTTPS, COOP/COEP/CORP headers, WASM MIME, quota/persistence
   diagnostics, and storage survival at the final origin and path.

Tracked controller models, UI/cinematic quad capture, pointer lasers, weapons,
locomotion policy, audio, and physical headset presentation remain separate
runtime gates; this package does not duplicate or modify those systems.
