# Flat browser WebAssembly regression audit

Date: 2026-07-23

This audit starts at integration commit `9e7898db`. It checks that WebXR,
cinematic/intro, demo-import, and corresponding-source work did not turn the
ordinary browser build into an XR-only application. It uses generated files
and synthetic game-shaped entries only; no commercial game data is present.

## Deterministic evidence

- A clean no-data Emscripten build compiles and links `SurrealEngine.js` and
  `SurrealEngine.wasm`. Both the development page and product launcher stop at
  `waiting-for-import`; neither calls native main before a validated local
  import exists.
- The product launcher starts with `navigator.xr`, `XRGPUBinding`,
  `XRWebGPUBinding`, and `XRWebGLLayer` forced absent. Its only registered
  presentation is `flat`, and the WebGPU adapter request has no
  `xrCompatible` option. WebXR remains an optional provider.
- Launcher tests cover the default explicit intro skip
  (`--url=<safe-map>`) and the intentional normal-intro path (no `--url`). The
  synthetic staged launch selects Unreal Gold `Vortex2` and produces
  `--autoplay --url=Vortex2 --render=webgpu /gamedata`.
- Import tests cover retail UT99 and Unreal Gold. The separate demo descriptor
  test covers UT99 demo 348, Unreal demo 205, and Deus Ex demo 1002f while
  retaining their experimental markers and title-specific validation. The
  public packaged launcher still advertises UT99 and Unreal Gold only.
- Input-composition and XR UI tests prove that releasing or ending WebXR input
  removes only XR contributors and that the legacy mouse can press and release
  after tracked-pointer input. The staged Chrome test independently verifies
  ordinary keyboard, button, motion, and wheel DOM delivery to the flat canvas.
- The staged canvas follows browser viewport width. The installed Chrome
  fullscreen API is exercised through a trusted click and returns cleanly.
- Direct WebGPU and `XRWebGLLayer` provider tests cover exit and re-entry. The
  browser adapter test covers declined/failed XR activation returning a flat
  fallback instead of replacing the running desktop presentation.
- The staged package exposes a visible corresponding-source link. Chrome
  retrieves that link, and the returned archive byte count and SHA-256 match
  `source-compliance.json`. The package/data audit still rejects commercial
  UE1 payloads and mismatched source/build provenance.

## Commands

The focused repeatable matrix is:

```text
node web/test_ue1_demo_imports.mjs
node web/test_webxr_browser_app_adapter.mjs
node web/test_webxr_provider.mjs
node web/test_webxr_webgl_bridge.mjs
node web/test_webxr_webgl_fallback_provider.mjs
python web/smoke_test_browser_app.py --base-url=http://localhost:8114
python web/smoke_test_ut99_importer.py --base-url=http://localhost:8114
python web/smoke_test_mutable_persistence.py --base-url=http://localhost:8114
python web/smoke_test_browser_data_bootstrap.py --base-url=http://localhost:8114
python web/smoke_test_browser_app_runtime.py --base-url=http://localhost:8114
python web/smoke_test_no_data_boot.py --base-url=http://localhost:8114
python web/smoke_test_release_package.py --base-url=http://localhost:8115
```

The native focused set is `InputComposition`, `WebXRInputAdapterTests`,
`WebXRInputRuntimeTests`, `XRUISurfaceTests`,
`XRUISurfaceRuntimeAdapterTests`, and `XRUISurfaceEngineBindingTests`.

## Retail UT99 evidence

The GOG Unreal Tournament GOTY installation used for owner-data validation has
496 files and 659,817,346 bytes. Browser metadata validation sees all required
`System`, `Maps`, `Textures`, `Sounds`, and `Music` subfolders and selects the
retail `ut99` descriptor. A full run persists and materializes the dataset and
reaches the map/presentation launcher, which rules out folder recursion,
detection, storage, and the import gate as the current fault.

After Play, both WebGPU and null-renderer runs remain inside synchronous native
startup before `Module.callMain()` returns. Consequently the post-call canvas
reveal, presentation activation, and `surrealBooted` signal do not run. The
null-renderer result rules out WebGPU rendering as the cause. This is an open
owner-data release blocker, not a successful gameplay claim.

`smoke_test_owner_game.py` accepts `--profile-dir` so the private browser copy
can be retained between diagnostic runs, and `--renderer=webgpu|null` so native
startup can be separated from graphics. It records no game contents in Git.
The duplicate-memory cost and the proven opt-in WasmFS/OPFS alternative are
documented in `BrowserGameDataMount.md`.

## Owner-data and hardware gates

Synthetic entries establish launcher, storage, argument, and UI behavior; they
cannot establish real gameplay. Before a stable release, an owner must still:

1. Import complete user-owned UT99 and Unreal Gold installations separately,
   test direct-map and normal-intro launches, and verify intro skip, cinematic
   completion, menu return, and map travel.
2. In running flat gameplay, verify keyboard layout/text input, mouse capture,
   buttons, wheel, pointer-lock behavior, resize, fullscreen enter/exit, audio,
   focus loss, and restoration.
3. Quit through the actual game, confirm ticks stop and the mutable-data flush
   completes, reload, and verify INI/save restoration. Then switch titles and
   repeat without storage crossover.
4. Test any locally owned demo distribution only against its experimental
   descriptor; demo detection passing does not claim full engine compatibility.
5. On real XR hardware, enter, exit to the same flat canvas, exercise a failed
   or declined entry, re-enter, and confirm keyboard/mouse still work after the
   session ends.
6. Re-run the package/source audit at the final hosted origin and complete the
   human/legal review described in `BROWSER_STATIC_RELINKING.md`.

Electron is outside this audit. No Electron runtime or packaging behavior is
claimed.
