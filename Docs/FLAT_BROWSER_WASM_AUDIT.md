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

The original post-Play failure was reproduced exactly. `buildNativeArguments()`
returns a frozen array, but Emscripten's generated `callMain()` mutates that
array with `unshift()` to add the program name. The resulting `TypeError` was
caught by the import callback and incorrectly shown as a generic data-import
failure. The launcher now passes `Array.from(buildNativeArguments(...))` to
native startup, while folder enumeration/read exceptions are normalized as a
distinct `folder-scan` phase with permission and quota guidance.

Continuing past that boundary exposed three unrelated browser-runtime issues.
The Emscripten OpenAL implementation reports `INT_MAX` mono/stereo source
counts, so the engine now clamps allocation to its requested voice count rather
than attempting an impossible vector allocation. Browser pointer lock records
startup intent on the window thread and exposes a trusted post-start
capture/resume control while preserving direct canvas capture and uncaptured
mouse fallback. Focused user lock loss forwards the browser-reserved Escape to
the engine intro/menu path; native unlock and WebXR exit without forwarding.
The first physical Quest 3/Virtual Desktop/VDXR fallback test refined this
claim: the cursor locked and mouse fire reached the game, but relative mouse
motion did not rotate the view. Capture permission is proven; browser-relative
motion through SDL/native input remains a release blocker.
Finally, WebGPU surface creation
uses the exact configured `Module.canvas` rather than assuming `#canvas`.
These fixes are integrated and covered by the deterministic browser suites;
the qualified owner-data result below establishes flat boot and rendering.

The first corrected owner run then revealed a second, independent pre-launch
failure. The importer probed the optional experimental OPFS ABI through
`Module.ccall()` even when the default build did not export that function.
Emscripten's missing-function assertion set the generated runtime's sticky
`ABORT` flag before JavaScript caught the exception and selected MEMFS. Native
setup, package loading, map login, and main-loop registration all completed,
but Emscripten discarded every animation-frame callback at its initial
`if (ABORT) return` guard. Capability detection now checks the direct
`Module._Surreal_GetBrowserOPFSMountABIVersion` export before calling anything;
the default fallback therefore leaves the runtime healthy.

## Qualified browser candidate

Candidate `release/browser-candidate-04687fe1` was built from exact commit
`04687fe1525035f5bdff57b028f1a7fd5496ad4a` with empty bundled game data and
both experimental OPFS/worker options disabled. A preserved browser profile
restored the full 496-file GOG UT99 installation plus three mutable files. At
Play, `ABORT` remained false, ticks advanced `2, 59, 116`, WebGPU reported 41
draw calls and 52 textures, and the captured canvas was 99.83% nonuniform with
zero engine WebGPU errors or page errors. This qualifies owned-data flat boot
and rendering; it does not yet qualify physical WebXR, long gameplay, save
round trips, intro/cinematics, Unreal Gold gameplay, or human audio listening.

The deployed package exposes source archive SHA-256
`702056eb9e04a2520cde7aaf6a53c73062274451820f19b3201467399858d570`
and WASM SHA-256
`759c1f13ba46a70114ee2a47c40a2871c44c730e09a9bb428df00e4312ed9be2`.
The public-origin release smoke passes isolation headers, MIME, source/hash,
no-data gate, static collapsed legal footer, pointer-lock helper, responsive
layout, fullscreen, ordinary input, and synthetic Unreal Gold launch.

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
   The 2026-07-23 VDXR test failed this gate specifically at relative
   mouse-look despite successful pointer lock and mouse-button delivery.
3. Quit through the actual game, confirm ticks stop and the mutable-data flush
   completes, reload, and verify INI/save restoration. Then switch titles and
   repeat without storage crossover.
   The owner matrix restored INI/log/Settings data but proved that WasmFS
   `analyzePath` can throw for the Save directory, causing `.usa` files to be
   omitted even though `stat`, `readdir`, and classification work.
4. Test any locally owned demo distribution only against its experimental
   descriptor; demo detection passing does not claim full engine compatibility.
5. On real XR hardware, enter, exit to the same flat canvas, exercise a failed
   or declined entry, re-enter, and confirm keyboard/mouse still work after the
   session ends.
6. Re-run the package/source audit at the final hosted origin and complete the
   human/legal review described in `BROWSER_STATIC_RELINKING.md`.

Electron is outside this audit. No Electron runtime or packaging behavior is
claimed.
