# SurrealEngine WebXR Port (Option A) — Detailed Implementation Plan

Companion to `PLAN.md` (high-level decision history/status log) and
`WEBXR_PORT_PLAN.md` (the Option A vs Option B comparison that led here).
This file is the granular, execute-without-supervision technical plan for the
WebXR/Option A build, mirroring `VR_IMPLEMENTATION_PLAN.md`'s role for the
native VR effort. Written 2026-07-18, the day the user said "Go with option
A, do the WebXR port" — superseding the earlier "native VR first, WebXR is
M5/deferred" decision (see memory `ut99-vr-native-port.md`).

The complete canonical milestone roadmap, including every remaining product
area through headset release, is now in
`WEBXR_COMPLETE_IMPLEMENTATION_PLAN.md`. This file remains the chronological
engineering journal and detailed record of completed probes.

Each milestone gets its detailed plan written here only once it becomes
active work — same pattern the native VR plan follows.

## Milestone roadmap

1. **M1: Emscripten build harness.** Engine compiles to WASM, boots via
   `--autoplay`, runs the real tick/input/map-load loop via
   `emscripten_set_main_loop`, with a no-op `NullRenderDevice` (no real
   graphics yet). Success = it runs in a browser tab without crashing,
   provably ticking. **Detailed below — active as of 2026-07-18.**
2. **M2: WebGPU `RenderDevice`.** New backend implementing the same abstract
   `RenderDevice` interface D3D11/Vulkan already implement
   (`RenderDevice.h:97-161`), targeting `<webgpu/webgpu.h>` via Emscripten's
   `emdawnwebgpu` port. Get everything *except* the bindless texture path
   working first (a small fixed-texture-per-draw fallback or placeholder
   texture proves geometry/shaders/pipelines before tackling binding).
3. **M3: WebGPU binding/cache optimization. DONE (2026-07-22).** M2 already
   avoided Vulkan's bindless model by using four fixed texture slots. M3
   therefore cached those per-draw WebGPU bind groups, instrumented the hot
   path, and measured geometry-buffer pressure on a larger map.
4. **M4: WebXR presentation groundwork. DONE IN AUTOMATION; PHYSICAL RUNTIME
   OPEN.** XR-compatible WebGPU device, JavaScript-texture import/readback,
   external array-layer rendering, and RAF ownership handoff.
5. **M5: simulation/view refactor. DONE.** Advance once, render independent
   per-eye views, and submit synchronously while frame-scoped browser objects
   remain valid.
6. **M6: production WebGPU/WebXR session. IMPLEMENTED; PHYSICAL RUNTIME OPEN.**
   Packed frame ABI, projection layer, preferred formats, lifecycle, failure
   cleanup, visibility, and device-loss policy.
7. **M7: tracked head/camera/world scale. DETERMINISTIC TESTS PASS; PHYSICAL
   SCALE/STEREO OPEN.**
8. **M8: controllers/gameplay. IN PROGRESS.** ABI v2, body/head/dominant-hand
   locomotion references, turning, selectable dominant hand, controller
   recenter/menu actions, world full-basis hands, scoped weapon direction/
   presentation, per-eye weapon draw, and fire haptics work; settings UI,
   two-hand policy, fixtures, and hardware tuning remain.
9. **M9: UI/comfort. IN PROGRESS.** Essential HUD state is captured once and
   replayed inside each active eye pass; the full experimental browser smoke
   now proves one update/two stereo presentations. Enable, distance, FOV,
   aspect, and safe-area settings plus the zero-work disabled lifecycle are
   implemented; browser-persisted settings UI, menus/cursor, unsupported
   primitives, comfort/loading policy, and all headset gates remain.
10. **M10: audio/data/network/deploy. IN PROGRESS.** Browser audio, a
    discontinuity-safe tracked-head listener, the schema-v1 local UT99 importer,
    and a real audited no-preload artifact/clean-profile wait gate exist. A real
    user-owned full-data import, physical audio qualification, non-game
    persistence, full launcher UX/networking scope, and HTTPS/headset PWA
    deployment/release audit remain; an allowlisted installable shell passes
    deterministic deployment tests.
11. **M11: performance/release. IN PROGRESS.** Lifecycle automation exists;
    physical Quest profiling, compatibility, sleep/wake, soak, and release
    artifact qualification remain.

## M1: Emscripten build harness — ground truth (recon results)

Two research passes grounded this milestone: an Explore agent across the
build system/CMake/main-loop/windowing code, and a Plan agent that read the
actual source (not just the recon summary) to design the concrete steps
below. File:line citations throughout, not guesses.

**The Emscripten toolchain is already installed and verified working on this
machine** — `C:\Devstuff\emsdk` (Emscripten 6.0.2, set up for the sibling
`webxr-port/` project), SDL2 support already confirmed compiling and linking
(`webxr-port/reports/03-toolchain.md`). No toolchain setup needed.
`webxr-port/web/serve.mjs` is a directly reusable local dev-server template
(COOP/COEP headers, zero dependencies).

- **Build system**: root `CMakeLists.txt` branches only
  `if(WIN32) elseif(APPLE) else()` — no Emscripten handling anywhere. Render
  backend (D3D11/Vulkan) is a **runtime** dispatch via
  `RenderDevice::Create(Widget*, RenderAPI)` (`RenderDevice.cpp:38-54`), not
  compile-time — this is the seam a `NullRenderDevice` plugs into.
- **Windowing**: `SurrealWidgets`' SDL2 backend
  (`SurrealWidgets/src/window/sdl2/sdl2_display_window.cpp`, 873 lines) is
  overwhelmingly SDL-generic — the only platform conditionals are a macOS
  Metal-window flag and an X11-only DPI hack (both inert under Emscripten's
  SDL2 port). Emscripten's SDL2 support is first-class and mature.
- **`RenderAPI::Bitmap` already exists and is cross-platform** — `Widget`
  (`SurrealWidgets/src/core/widget.cpp:16`) already special-cases it to
  construct a `BitmapCanvas`, and every windowing backend including SDL2
  already implements the "no GL/Vulkan context" window-creation path for it.
  `RenderDevice::Create()` just doesn't have a branch for it yet (throws).
  This is the reuse point for M1's no-op renderer — no new window/canvas
  plumbing needed.
- **Boot flow**: `GameApp.cpp`'s existing `--autoplay <folder>` flag
  (lines 56-71, added earlier this session for native scripted testing)
  already skips `LauncherWindow::ExecModal()` entirely and jumps straight to
  `Engine engine(info); engine.Run();`. The Launcher's modal blocking event
  pump (`DisplayWindow::RunLoop()`, `GetMessage`/`SDL_WaitEvent`-equivalent)
  is incompatible with a single browser tab's event loop without Asyncify —
  M1 forces the `--autoplay`-only path rather than attempting the Launcher UI.
- **Filesystem**: `Utils/File.cpp`'s non-Windows path is plain blocking
  `fopen`/`fread`; `PackageManager`'s constructor does synchronous
  `fs::directory_iterator` scans at startup. Both work unmodified against
  Emscripten's MEMFS once data is present — no async-ification needed for
  M1. Use `--preload-file` (zero new JS glue; Emscripten's generated loader
  fetches/unpacks into MEMFS before `main` runs).
- **Main loop**: `Engine::Run()` (`Engine.cpp:87-293`) is a classic
  `while (!quit) { pump-input(non-blocking); tick; render->DrawGame();
  check-for-map-change; }` — exactly the shape `emscripten_set_main_loop`
  needs to invert. Currently paced by the render backend's vsync-blocking
  `Present`/`vkWaitForFences` — won't exist with `NullRenderDevice`, replaced
  by `requestAnimationFrame` pacing (`fps=0` in
  `emscripten_set_main_loop_arg`).
- **SSE2**: `SurrealWidgets/src/core/canvas.cpp` (the one SSE2 hotspot still
  in M1's build graph — Vulkan/D3D11 SSE2 files are excluded) already gates
  its SSE2 code behind `#if defined(__SSE2__) || defined(_M_X64)` with a real
  scalar fallback. Emscripten doesn't define `__SSE2__` by default — already
  takes the portable path, zero changes needed.

## M1 implementation steps

### 1. CMake — root `CMakeLists.txt`

Add `elseif(EMSCRIPTEN)` (CMake sets this automatically under `emcmake`),
before the generic `else()` Unix branch:
- `if(NOT EMSCRIPTEN)` guard around `FetchContent(openxr)`,
  `add_subdirectory(SurrealGPU)`, `target_link_libraries(SurrealCommon openxr_loader)`
  — native-desktop-OpenXR (`--probexr`, `VulkanXRSession`) is unrelated to
  WebXR and unbuildable/pointless on wasm.
- `list(FILTER SURREALCOMMON_SOURCES EXCLUDE REGEX "RenderDevice/Vulkan/")`
  under Emscripten (D3D11 is already `WIN32`-gated).
- Don't build `SurrealVideo` (`SHARED` lib — real shared-lib support under
  wasm needs `MAIN_MODULE`/`SIDE_MODULE` for zero M1 payoff) or the
  `SurrealEditor`/`SurrealDebugger` executables under Emscripten.
- Add `NullRenderDevice`/`NullAudioDevice` sources to the Emscripten source list.
- `target_link_options(SurrealEngine PRIVATE -sALLOW_MEMORY_GROWTH=1
  -sSTACK_SIZE=8MB -sINITIAL_MEMORY=256MB -sINVOKE_RUN=0 -sEXIT_RUNTIME=0
  -sEXPORTED_RUNTIME_METHODS=callMain,FS,ccall -pthread -sPTHREAD_POOL_SIZE=2
  --preload-file "${SURREAL_GAMEDATA_DIR}@/gamedata")` — `SURREAL_GAMEDATA_DIR`
  is a `-D`-supplied local path, point at the existing GOG install
  (`C:\Program Files (x86)\GOG Galaxy\Games\Unreal Tournament GOTY`). `pthread`
  matches the existing non-Windows unconditional link flag (keeps
  `UInternetLink`'s DNS thread linkable) — means the dev server needs
  COOP/COEP headers even for M1 (`serve.mjs` already provides these).
- `-sINVOKE_RUN=0`: host page calls `Module.callMain(['--autoplay', '/gamedata'])`
  explicitly once preload finishes.

### 2. CMake — `SurrealWidgets/CMakeLists.txt`

Add `elseif(EMSCRIPTEN)` before the generic `else()` (X11/Wayland/dbus/
fontconfig branch — must not run under Emscripten): pull in
`SURREALWIDGETS_UNIX_SOURCES` + `-DUNIX -D_UNIX`, add
`SURREALWIDGETS_SDL2_SOURCES` directly (no `find_package(SDL2)` — no system
SDL2 under Emscripten's sysroot), put `-sUSE_SDL=2` on compile+link options
for `surrealwidgets` and `SurrealEngine`.

### 3. Render/audio stubs

- New `SurrealEngine/RenderDevice/Null/NullRenderDevice.h/.cpp`: every
  `RenderDevice` pure virtual as a no-op. `RenderDevice::Create()` gets one
  additive branch: `RenderAPI::Bitmap → NullRenderDevice`.
- `LauncherSettings.h`: add `RenderDeviceType::Null`, defaulted under
  `__EMSCRIPTEN__`.
- `GameWindow.cpp`: `GameWindow::Create()` maps `RenderDeviceType::Null →
  RenderAPI::Bitmap`.
- New `SurrealEngine/Audio/NullAudioDevice.h/.cpp`: `AudioDevice`'s ~12 pure
  virtuals as no-ops, wired under `__EMSCRIPTEN__`. (`Engine::Run()` calls
  `audiodev->InitDevice()` with no failure guard, `Engine.cpp:98` — real
  OpenAL under a browser autoplay-gesture policy is an orthogonal risk M1
  shouldn't couple to.)

### 4. Boot flow — `GameApp.cpp`

`#ifndef __EMSCRIPTEN__` guard around the `LauncherWindow::ExecModal()`
branch (replaced by a console message under Emscripten). Same guard around
the `--probexr` block (includes `VulkanXRSession.h`) — this is also what lets
the OpenXR `FetchContent` dependency drop out of the Emscripten CMake graph.

### 5. Main-loop inversion — `Engine.cpp`/`Engine.h`

Split `Engine::Run()` into `Setup()` (existing pre-loop code, unchanged),
`RunOneFrame()` (mechanical extraction of the `while(!quit)` body, no logic
change), `Shutdown()` (existing post-loop code). Native builds:
`Setup(); while(!quit) RunOneFrame(); Shutdown();` — behaviorally identical
to today. Under `__EMSCRIPTEN__`: `Setup()` then
`emscripten_set_main_loop_arg(callback, this, 0, 1)`, callback calls
`RunOneFrame()` each tick and explicitly calls `Shutdown()` +
`emscripten_cancel_main_loop()` when `quit` flips true (doesn't rely on
stack-unwind/destructor timing, which `simulate_infinite_loop=1` doesn't
preserve — standard for this style of port). Map loads stay fully
synchronous inside the loop body (blocks the tab during a load, matching
native behavior) — explicitly not solved in M1.

## Definition of "M1 done"

1. `emcmake cmake -S . -B build-emscripten -DSURREAL_GAMEDATA_DIR=<path> && cmake --build build-emscripten --target SurrealEngine` succeeds, producing `.js`/`.wasm`/`.data`.
2. Native build still works unchanged — rebuild native `SurrealEngine.exe`, re-run the `--autoplay --url=DM-Deck16][` screenshot regression check.
3. Served locally (`serve.mjs` pattern), loaded headlessly (Playwright), no uncaught JS exceptions or wasm traps.
4. Console log shows package-manager scan, `LoadEntryMap`/`LoadMap`, player login succeeding — reaches a live tick loop, not boot-then-throw.
5. An exported tick counter (incremented once per `RunOneFrame()`) demonstrably advances over several seconds.
6. An exported quit function cleanly stops the loop (`emscripten_cancel_main_loop` observed, no further ticks, no JS-level crash).
7. No rendered output required — `NullRenderDevice` is intentionally a no-op.

## Constraints that apply throughout, not just M1

- **UT99 game data is commercial, not freely redistributable.** Any local
  gamedata path (`SURREAL_GAMEDATA_DIR` or later IDBFS-populated data) must
  stay git-ignored and dev-machine-local — never committed, never baked into
  a build artifact that could be deployed publicly. The real deploy-time
  mechanism (IDBFS mount + first-run file picker, matching `webxr-port/`'s
  pattern) is host-JS work that doesn't touch engine code — a clean
  post-M1 addition, not needed now.
- Every milestone that touches shared (non-Emscripten-exclusive) files needs
  a native-build regression check before being considered done — same
  discipline already established for the native VR effort this session.

## M1 status: DONE (2026-07-19)

All 7 "Definition of M1 done" criteria verified via `web/smoke_test.py`
(Playwright, headless): build succeeds, native regression clean, boots with
no uncaught JS exceptions/wasm traps, tick counter advances live (~60/s via
RAF), `Surreal_RequestQuit()` cleanly freezes the tick counter (froze at 607
across two 2s samples), no real rendering (`NullRenderDevice` no-op).

Two wasm32-specific heap-corruption bugs were found and fixed along the way
(native never surfaced either — x64/MSVC's `_aligned_malloc` and address-0
page-fault behavior masked both):

1. **`UObject::LoadNow()`'s empty-stream bootstrap path** (`UObject.cpp`,
   used only for a handful of native-only/intrinsic classes with no real
   Core.u export data — Object, Field, Struct, State, Class, the Property
   hierarchy) copied `StructSize` from `BaseStruct` but never copied
   `StructAlignment`, silently leaving it at the default of `1`. Fixed by
   also propagating `StructAlignment`.
2. **`AlignedAlloc()`** (`Utils/AlignedAlloc.h`) passed a caller-computed
   alignment straight to `std::aligned_alloc`, including legitimate
   alignment-of-1 requests (byte-only structs). musl (wasm32's libc) silently
   returns `nullptr` for `aligned_alloc(1, ...)` — alignment below
   `sizeof(void*)` isn't accepted — whereas Windows' `_aligned_malloc`
   tolerates it. The resulting null `Data` pointer, combined with wasm's flat
   linear memory having no protected zero page, meant writes near address 0
   went silent until Emscripten's stack-cookie sentinel caught the symptom
   much later at an unrelated call site. Fixed by clamping alignment to
   `sizeof(void*)` before calling `std::aligned_alloc`.

Both were pinpointed by adding `-sSAFE_HEAP=1 -g2` temporarily (traps at the
exact invalid access with a symbolicated stack, instead of only the delayed
address-0 symptom) plus scoped `fprintf(stderr, ...)` diagnostics gated
behind a runtime flag flipped just before the crash window — both removed
again once the bugs were found. `Utils/Logger.cpp` keeps a small permanent
addition: `LogMessage` now also mirrors to `stderr` under `__EMSCRIPTEN__`,
since there's no log file to inspect after a browser crash otherwise.

**Known minor follow-up (not blocking M1):** after `RequestQuit()` has
already frozen the tick counter, the browser console shows one unhandled JS
error — `querySelector` called with a garbage/non-ASCII string from
Emscripten's SDL2-port `restoreOldStyle`/`findEventTarget` fullscreen-style
teardown path. Fires strictly after the loop has already stopped ticking
cleanly (confirmed via two consistent tick-counter samples 2s apart), so it
doesn't affect M1's core "does it boot and tick" proof, but is worth chasing
before this is considered fully polished — likely a stale/garbage C-string
pointer read during `SDL_DestroyWindow`'s resize-listener teardown, same bug
family as the two above but not yet root-caused.

Next: M2 (WebGPU `RenderDevice`), per the milestone roadmap above.

## M2: WebGPU `RenderDevice` — status: DONE (2026-07-19)

New backend under `SurrealEngine/RenderDevice/WebGPU/`, structurally mirroring
`D3D11RenderDevice`'s fixed-4-texture-slot draw model (not Vulkan's bindless
one — core WebGPU has no stable equivalent, deferred to M3): `WebGPUContext`
(device/surface acquisition — device is handed in from JS via
`Module.preinitializedWebGPUDevice` before `callMain`, since acquisition is
async and `Engine::Setup()` is not), `WebGPUShaders` (WGSL port of
`D3D11FileResource.cpp`'s Scene.vert/frag), `WebGPUPipelineCache`/
`WebGPUSamplerCache` (33-entry pipeline array keyed by the same `PolyFlags`
bit arithmetic as D3D11), `WebGPUTextureManager`/`WebGPUTextureUploader`/
`WebGPUUploadManager`/`WebGPUCachedTexture` (P8/BGRA8_LM/R5G6B5/BC1/RGB8/
BGRA8/RGBA32_F format conversion, direct port of `D3D11TextureUploader`),
and `WebGPURenderDevice` (all `RenderDevice` pure virtuals, CPU-side vertex/
index staging buffers, submit-per-flush batching — WebGPU only guarantees a
buffer write is visible to commands submitted *after* the write, unlike
D3D11's immediate-context model, so the render pass must be ended/submitted
before the vertex/index/uniform buffers can be safely overwritten).
`RenderAPI`/`RenderDeviceType` gained a `WebGPU` case at all three dispatch
points (`window.h`, `LauncherSettings.h`, `GameWindow.cpp`,
`RenderDevice.cpp`), opted into via a new `--render=webgpu` CLI flag —
`__EMSCRIPTEN__`'s default stays `Null`, keeping M1's `smoke_test.py` a
working regression guard throughout.

**Two real bugs found and fixed along the way, both invisible to plain code
review or a clean C++ compile:**

1. **WGSL uniform-control-flow shader-compile failure** silently invalidated
   every one of the 37 render pipelines built from the shared shader module,
   meaning **100% of geometry was failing to render in every earlier
   "passing" smoke-test run this session** (the tick-counter/clean-quit
   checks don't verify rendering correctness, so this went undetected until
   a full untruncated console-log capture surfaced the one-time root-cause
   message buried under Chrome's cascading "invalid due to a previous error"
   noise). Cause: `WebGPUShaders.cpp`'s fragment shader called
   `textureSample()` (implicit-derivative/LOD sampling) inside `if` branches
   keyed on a per-fragment flag value (macro/lightmap/detail/fogmap
   sampling) — legal in HLSL (D3D11's original shader does the same thing),
   but WGSL hard-errors on implicit-derivative sampling inside non-uniform
   control flow. Fixed by switching those three call sites to
   `textureSampleLevel(..., 0.0)` (explicit LOD 0, unrestricted) — visually
   indistinguishable for these low-frequency auxiliary textures.
2. **`WebGPURenderDevice::DrawTile()`'s 2D content (HUD text, console/menu
   glyphs, weapon icons) rendered vertically flipped and mirrored**, even
   though its vertex/UV generation is a byte-for-byte port of
   `D3D11RenderDevice::DrawTile`, and 3D world geometry (via
   `DrawComplexSurface`) was correctly oriented. Root-caused by a background
   investigation agent via an instrumented A/B rebuild after exhaustively
   ruling out every static candidate (texture uploader row/pitch math,
   surface config, pipeline winding/cull, shader UV sampling — all
   byte-identical to D3D11). Fixed with a targeted `V`/`VL` swap isolated to
   `DrawTile`'s vertex generation; confirmed zero effect on 3D geometry.
   **Follow-up confirmed and fixed (2026-07-19).** `DrawComplexSurface`
   *did* have the same bug class, invisible on M2's mostly-symmetric
   `DM-Deck16][` walls. Isolated using `UT-Logo-Map`'s "UNREAL TOURNAMENT"
   logo (built from 8 `DrawComplexSurface`-rendered BSP brushes, 0
   `DrawGouraudPolygon` calls, confirmed via new diagnostic counters
   `Surreal_GetWebGPUComplexSurfaces/GouraudPolygons/Tiles()`), which
   rendered upside-down. `DrawTile`'s corner-swap trick doesn't apply here
   (this function has no `[V,VL]` span); instead, `DrawComplexSurfaceFaces`
   now computes each facet's local min/max `v` and mirrors the shared `v`
   value every texture layer's coordinates derive from around that span —
   keeps the lightmap (which reuses the same `v`) mathematically in
   lockstep with the base texture, so relative alignment is unaffected.
   Verified via native-vs-WebGPU `PrintWindow`/canvas screenshot comparison
   of `UT-Logo-Map`: both now read "UNREAL TOURNAMENT" correctly (previously
   the WebGPU render showed it upside-down). **`DrawGouraudPolygon` (actor/
   weapon mesh rendering) orientation remains unconfirmed** — two comparison
   screenshots were taken (`DM-Deck16][`, native vs WebGPU) but neither had
   a weapon/actor mesh in frame (both spawns face a different random
   direction with nothing nearby), so this is inconclusive, not verified
   clean. Left as an open follow-up: re-check with a map/moment where a
   weapon view model or pickup mesh is actually visible on screen.

**Verification, all against real UT99 game data (`DM-Deck16][`) via
`web/smoke_test_webgpu.py` (Playwright, headless `channel="chrome"` — the
bundled Chromium cannot acquire a WebGPU adapter under any launch flag
tried; a real Chrome/Edge install works with zero extra flags):**

- Native Win32 build rebuilt clean, no regressions.
- `git diff --stat` on `RenderDevice/Vulkan/` and `RenderDevice/D3D11/`:
  zero changes.
- Boots, ticks live (~57/s), quits cleanly (tick counter frozen across two
  2s samples post-quit).
- `Surreal_GetWebGPUErrorCount()` (JS `uncapturederror` listener — the
  pinned emdawnwebgpu C API only supports uncaptured-error callbacks via
  `WGPUDeviceDescriptor` at device-creation time, too early for C++ since
  the device is created in JS): **0** WebGPU validation errors during the
  full run.
- `Surreal_GetWebGPUDrawCalls()`: 94 (nonzero, real batched draw activity).
- `Surreal_GetWebGPUTextureCount()`: 73-75 distinct textures cached (proves
  the real P8/BGRA8_LM conversion path ran, not just the null-texture
  fallback).
- Canvas screenshot: 99.8% non-background pixels (checked in Python via
  Pillow against the saved PNG — a synchronous in-engine `ReadPixels` export
  wasn't pursued since WebGPU buffer mapping is async-only and this build
  doesn't use Asyncify), and visually confirms real UT99 geometry (DM-Deck16
  ship-corridor ceiling/wall/floor textures, correct perspective) plus
  correctly-oriented, readable HUD text after the `DrawTile` fix.

**Post-quit `querySelector` error (M1 issue) — root-caused and fixed
(2026-07-19).** UT99's shipped ini defaults to `StartupFullscreen=True`,
which called into Emscripten's bundled SDL2 port's Fullscreen API path at
boot. That path (`Emscripten_SetWindowFullscreen()`) installs a
document-level `fullscreenchange` listener (`registerRestoreOldStyle()` in
`libhtml5.js`) that SDL's own `Emscripten_UnregisterEventHandlers()` never
removes. If that listener fires after `SDL_DestroyWindow()` has already
freed `window->driverdata`, it calls back into
`Emscripten_HandleCanvasResize()` with the freed pointer, reads a garbage
`canvas_id`, and passes it to `document.querySelector()` — throwing the
observed uncaught `SyntaxError`. Localized via a `-sSAFE_HEAP=1 -g2` scratch
build and a full stack trace (`findEventTarget` →
`_emscripten_get_element_css_size` → `Emscripten_HandleCanvasResize` →
`HTMLDocument.restoreOldStyle`). Fixed in `Engine::OpenWindow()`
(`SurrealEngine/Engine.cpp`): under `__EMSCRIPTEN__`, never request browser
fullscreen regardless of the ini setting — it was also always a no-op since
`--autoplay` boot has no user gesture to satisfy the Fullscreen API. The bug
itself lives in Emscripten's vendored SDL2 port, not this codebase, so the
fix avoids triggering it rather than patching Emscripten. Re-verified:
`web/smoke_test.py` passes clean, no uncaught JS errors (previously passed
"with warnings").

## M3: WebGPU binding/cache optimization — DONE (2026-07-22)

The original M3 framing above — "retarget `DescriptorSetManager`'s
overflow-safety-valve policy from Vulkan descriptor-set updates to WebGPU
bind-group recreation" — turned out to not match what M2 actually built.
**M2's `WebGPURenderDevice` never uses `DescriptorSetManager` at all**; it
mirrors D3D11's fixed-4-texture-slot model from the start (see "Key
architectural finding" in the M2 plan: WebGPU has no stable equivalent of
Vulkan's 16,536-entry bindless descriptor array, so M2 was designed to
sidestep it, not port it). That sidestep already happened, by construction,
before M3 was ever started — so a "bindless redesign" milestone has nothing
left to redesign.

The 2026-07-19 audit found the work that actually remained:

- **Per-draw bind-group creation**: `DrawEntry` called
  `wgpuDeviceCreateBindGroup` + `wgpuBindGroupRelease` for every draw call,
  every frame — a deliberate M2 simplification.
- **Batch fragmentation** on texture/sampler change (`SetDescriptorSet`,
  lines 401-443) is inherent to the fixed-slot model, same as D3D11 —
  acceptable unless measurement says otherwise.
- **Geometry buffer sizing** (`VertexBufferCapacity = 16*1024`,
  `IndexBufferCapacity = 32*1024`, `WebGPURenderDevice.h:96-97`) forces a
  mid-frame submit/reopen when full on larger maps — cheap to raise, worth
  measuring first.
- **`binding_array<texture_2d<f32>>`** (the actual WGSL analogue of
  bindless) was still an unshipped proposal as of the 2026-07 recon
  (`WEBXR_PORT_PLAN.md`). Not building on it now — revisit only if the
  fixed-slot model proves an actual bottleneck on Quest 3 hardware during
  M4/M5 testing.

Implementation and measured result:

- `WebGPURenderDevice` now owns a bind-group cache keyed by the four
  `WebGPUCachedTexture*` values plus the three sampler-mode integers already
  carried by `WebGPUDrawBatchEntry`. Cached groups are released before the
  texture cache destroys its views; targeted invalidation also covers
  realtime and rectangular texture updates.
- Per-frame `BindGroupsCreated`, `BindGroupCacheHits`, and
  `BufferRollovers` counters are exported to both browser harnesses.
  `BufferRollovers` counts only actual vertex/index capacity exhaustion,
  unlike the older `BuffersUsed` counter, which also includes intentional
  render-pass boundaries.
- `web/smoke_test_webgpu.py [map]` and the WebGPU/WebXR pages now accept a
  map name, and the smoke test reports observed engine tick rate plus the
  new counters.
- Desktop Chrome, `DM-Deck16][`: approximately 59 ticks/s, 95 draws, 0 bind groups
  created in the sampled warm frame, 95 cache hits, 0 geometry-buffer
  rollovers, 0 WebGPU errors.
- Desktop Chrome, larger `CTF-Darji16`: approximately 59 ticks/s, 224 draws, 1 bind
  group created, 223 cache hits, 140 cached textures, 0 geometry-buffer
  rollovers, 0 WebGPU errors. The screenshot/non-blank and clean-shutdown
  checks also passed.
- Because the heavier scene produced no capacity rollover, the existing
  16K-vertex/32K-index buffers were deliberately left unchanged. Enlarging
  them would add memory without addressing an observed bottleneck.
- The IWER Meta Quest 3 lifecycle regression still passes after the change:
  two views per frame, advancing XR frame count, and clean session teardown.

This completes the re-scoped M3. Whether the fixed-slot model is sufficient
on Quest 3 hardware remains an M4/M5 measurement; **M4 (real WebXR stereo
render integration) is the next substantial milestone.**

## M4 pre-work (2026-07-19)

Two findings ahead of the real M4 design, both empirically verified rather
than assumed:

**`XRGPUBinding` is not usable yet on any shipping browser.** Direct test in
real Chrome 150.0.7871.125 (`navigator.gpu`/`navigator.xr` exist;
`window.XRGPUBinding`, `XRGPUProjectionLayer`, `XRGPUSubImage` are all
`undefined`). Chromestatus' own tracking entry (feature 5077077997649920)
has it at "Prototype a solution" — the second of ~6 stages, no shipping
estimate, Firefox/WebKit both "No signal," last updated 2024-08. It's
reachable only in Chrome Canary behind two experiment flags (`WebXR
Projection Layers` + `WebXR/WebGPU Bindings`), per a March-2025 Google WebXR
engineer blog post — more pessimistic than this doc's prior "Editor's
Draft" phrasing suggested. Separately, `emdawnwebgpu` has no XR-specific
API surface at all (general `webgpu.h`-over-browser-WebGPU port only); the
`Module.preinitializedWebGPUDevice` handoff this project's `WebGPUContext`
already relies on is flagged in `emscripten-core/emscripten#24265` as a
legacy `-sUSE_WEBGPU`-era mechanism with no committed lifespan (still
functional today, not itself XR-aware). **Conclusion: the real M4 blocker
is the browser platform, not this project's toolchain** — M4's design
should assume XRGPUBinding is unavailable for the foreseeable future and
either wait on it or find another approach (e.g. rendering to an
`XRWebGLLayer` and copying, at a perf cost) once actual M4 work starts.

**Harness groundwork landed anyway**, porting the sibling QuakeQuest
project's WebXR session-lifecycle + IWER headless-test pattern: new
`web/webxr_session.js` (session lifecycle), `web/index_webxr.html` (boots
the real M2 engine build alongside an XR session request), and
`web/smoke_test_webxr.py` (Playwright + IWER's Meta Quest 3 emulated
device — confirms a stereo `immersive-vr` session requests, advances 2
views/frame, and tears down cleanly, headless). Since IWER has no
`XRGPUBinding` emulation either, the harness uses a throwaway offscreen
WebGL2 `XRWebGLLayer` purely to satisfy IWER's render-state requirement —
it never touches the real WebGPU canvas, and proves session
lifecycle/plumbing only, not stereo rendering. `iwer` pinned as a
`web/package.json` devDependency. This is the same "prove the plumbing
before building the real thing" pattern M1 used for the WASM boot loop
before M2 built real rendering on top of it.

## M4 presentation-path spike — IN PROGRESS (2026-07-22)

This section is the live engineering journal for M4. Record failed probes as
well as successful code so later work does not repeat browser/toolchain
experiments.

### Starting state and direct-path requirements

- Test browser: desktop Google Chrome `150.0.7871.129` on Windows.
- Ordinary launch: `navigator.xr` and `navigator.gpu` exist, an adapter
  requested with `{xrCompatible:true}` succeeds, but `XRGPUBinding` is not
  exposed.
- The current WebXR/WebGPU Editor's Draft requires all of the following for
  the direct path: request the adapter with `xrCompatible:true`; request the
  immersive session with required feature `"webgpu"`; construct
  `XRGPUBinding(session, device)`; create a projection layer; install it via
  `session.updateRenderState({layers:[...]})`; then render each view to the
  `XRGPUSubImage` returned during the session's animation frame. WebGPU
  sessions cannot use `XRWebGLLayer`/`baseLayer`. Source:
  <https://immersive-web.github.io/WebXR-WebGPU-Binding/>.
- Chromium added an explicit `webxr-webgpu-binding` about:flags entry in
  November 2024, separate from WebXR incubations, and current Chromium source
  also lists `webxr-projection-layers`. Source:
  <https://chromium.googlesource.com/chromium/src/+/1c22e8e0f1f14071f0eae28d3f7d48408c841398>.
- `web/smoke_test_webxr.py --experimental-webgpu-xr` now launches installed
  Chrome with `--enable-features=WebXRWebGPUBinding,WebXRLayers` and reports
  whether that actually exposes `XRGPUBinding`. This remains an opt-in probe;
  the default test continues to represent an ordinary browser launch.

### Probe 1 — Chromium feature flags: PASS

The opt-in launch exposes `window.XRGPUBinding` on Chrome 150; the default
launch does not. Therefore the implementation is present in this browser and
the direct WebGPU path is testable behind flags. It is not suitable as a
zero-configuration shipping path yet.

### Probe 2 — engine device compatibility: FIXED

`index_webxr.html` previously performed a successful
`requestAdapter({xrCompatible:true})` capability probe, destroyed that probe
device, then acquired the device actually handed to Emscripten from a second
plain `requestAdapter()` call. The successful probe did not make the engine's
device XR-compatible. The WebXR page now acquires the engine device itself
with `{xrCompatible:true}`, retains it as `window.surrealWebGPUDevice` for the
session bridge, and still hands that same object to
`Module.preinitializedWebGPUDevice`. The ordinary WebGPU-only page is
unchanged.

### Probe 3 — projection layer with IWER: EXPECTED HARNESS LIMIT FOUND

Added `surrealXRProbeWebGPUProjection()` and a user-gesture-capable page button.
The probe requests `requiredFeatures:["webgpu"]`, constructs the binding,
queries the preferred color format, creates a projection layer, and installs
it with `updateRenderState({layers:[layer]})`, recording progress at every
stage. The experimental Playwright path advertises `webgpu` on IWER's emulated
Quest device so the request can reach the native binding constructor.

Observed result:

```text
XRGPUBinding exposed: true
sessionCreated: true
bindingCreated: false
TypeError: Failed to construct 'XRGPUBinding': parameter 1 is not of type 'XRSession'.
```

This is the expected boundary between IWER and Blink: IWER implements
`XRSession` as a JavaScript class, while Chromium's native `XRGPUBinding`
requires a native Blink `XRSession` wrapper. IWER remains valid for lifecycle,
two-view pose, and teardown tests, but cannot validate native WebGPU projection
layers. The same run continued through the existing IWER lifecycle test (two
views/frame, advancing frame counter, clean end) with no page errors.

Next verification options, in priority order:

1. Run the new projection button against a real headset/browser with both
   Chromium flags enabled; no code change is required for the probe.
2. Investigate Chromium's native WebXR Test API/mock runtime as an automated
   source of real Blink `XRSession` objects. Its upstream harness depends on
   Chromium layout-test Mojo bindings, so availability in an ordinary Chrome
   Playwright launch must be proven rather than assumed.
3. If neither is practical, keep IWER for lifecycle regression and treat
   projection-layer creation/rendering as a headset-required integration test.

### Probe 4 — JavaScript `GPUTexture` → C++ `WGPUTexture` interop: PASS

The pinned Emdawnwebgpu port's generated runtime includes
`WebGPU.importJsTexture(jsTexture, parentDevice)`. This allocates the C-side
wrapper used by `webgpu.h` while retaining the existing JavaScript
`GPUTexture` as its backing object. That is exactly the handoff needed for an
`XRGPUSubImage.colorTexture`; it does not require copying through the canvas.

Before a native XR session is available, validate the bridge with an ordinary
render-attachment `GPUTexture` created from the same XR-compatible engine
device. The test imports it, creates/releases a `WGPUTextureView` in C++,
releases the imported wrapper, destroys the JavaScript texture, and reports a
boolean result through the Playwright harness. This separates Emdawnwebgpu
interop risk from XR session/runtime availability.

Implemented as the exported `Surreal_TestWebGPUTextureImport()` diagnostic.
The experimental Playwright run returned `1`: C++ received a valid imported
texture, created and released an `rgba8unorm` 2D view, and released the wrapper;
JavaScript then destroyed the underlying test texture. The subsequent WebXR
lifecycle test completed normally. This proves there is no required canvas or
CPU-copy hop between `XRGPUSubImage.colorTexture` and the C++ renderer.

The relevant subimage layout is also simple enough to reproduce C-side: the
WebXR/WebGPU draft specifies a 2D texture array, one layer per view, and
`getViewDescriptor()` selects one slice using `baseArrayLayer` with
`arrayLayerCount=1`. The XR viewport must still be applied separately.

### Probe 5 — render/readback across the JS/C++ texture bridge: PASS

Upgrade Probe 4 from object-lifetime validation to an observable GPU command:
C++ clears the imported texture to a known RGBA value in a render pass and
submits it on the engine queue. JavaScript copies the same browser-owned
texture into a mapped readback buffer, verifies the first pixel, and only then
destroys it. Also require the uncaptured WebGPU error count to remain zero.

First attempt returned `[0,0,0,0]` because the diagnostic texture descriptor
omitted `GPUTextureUsage.COPY_SRC` even though the verification step copied
from it. That is a test construction error, not evidence about the import
bridge. After adding `COPY_SRC`, the same test returned:

```text
import result: 1
readback pixel: [64,127,191,255]
expected pixel: [64,128,191,255] (one-byte rounding tolerance)
uncaptured WebGPU errors: 0
```

The clear was encoded and submitted entirely through the C++ `webgpu.h`
interface, while allocation and readback used the original JavaScript
`GPUTexture`. This proves that the imported handle is not merely valid for
object creation: commands submitted by the engine operate on the same GPU
resource seen by browser JavaScript.

### Probe 6 — complete UT frame into a browser-owned array layer: PASS

The renderer now has a real external-target seam instead of relying only on
Probe 5's isolated clear pass:

- `WebGPURenderDevice::QueueExternalRenderTarget()` accepts ownership of an
  imported `WGPUTexture` wrapper, target dimensions, and an array-layer index.
- The next `Lock()` bypasses the canvas surface, creates a 2D view for the
  requested array layer, sizes the engine-owned depth buffer to match, and
  runs the ordinary UT scene/overlay pipeline.
- `Unlock()` submits and releases the imported wrapper, records diagnostic
  counters, restores the prior fixed render size, and leaves subsequent
  frames on the canvas path.
- The JavaScript `GPUTexture` remains browser-owned throughout. This matches
  the ownership split required for an `XRGPUSubImage.colorTexture`.

The Playwright test deliberately creates a two-layer `bgra8unorm` texture and
queues **layer 1**, not layer 0, to exercise the same view-selection behavior
needed for per-eye XR array layers. It waits for the external-frame counter,
copies only layer 1 to a mapped buffer, and requires real draw calls plus
non-black, varied output. Chrome 150 with the experimental WebXR/WebGPU flags
produced:

```text
external target queued: 1
external frames completed: +1
draw calls in external frame: 95
non-black pixels: 300885 / 307200
distinct colors in the 1/257 sample: 878
array layer read: 1
uncaptured WebGPU errors: 0
```

The same run then completed the expected IWER lifecycle regression: two views
per frame, frame counter `2 -> 182` over three seconds, and clean teardown.
The native projection probe still stops at IWER's JavaScript/native
`XRSession` type boundary documented in Probe 3.

This establishes the zero-copy presentation half of M4: a browser-owned XR-
shaped texture can be rendered by the complete C++ engine. It does **not** yet
establish real headset presentation. A real `XRGPUSubImage` is valid in the
native session frame where it is acquired, so production integration cannot
queue it for a later ordinary window animation frame. The current WebGPU
pipelines also target `bgra8unorm`, so the projection layer must be created
with that compatible color format unless the pipeline cache is generalized
first.

### Probe 7 — XR frame-loop ownership handoff: PASS

Added two small Emscripten engine exports for the timing half of the native XR
integration:

- `Surreal_SetXRFrameLoopActive(true)` pauses Emscripten's ordinary window
  animation loop; passing `false` resumes it.
- `Surreal_RunXRFrame()` executes exactly one normal `Engine::RunOneFrame()`
  while XR owns scheduling. It uses the same quit/shutdown callback as the
  window loop rather than introducing a second engine-frame implementation.

This matters independently of frame rate: an `XRGPUSubImage` must be acquired
and consumed in its native `XRSession.requestAnimationFrame` callback. The
engine cannot leave its original RAF running and defer the imported target to
another callback.

Playwright validated the ownership transitions in the same experimental run:

```text
window RAF paused: tick 9 -> 9 over 250 ms
three XR-driven calls: tick 9 -> 12
window RAF resumed: tick 12 -> 16
```

The external-target test and lifecycle test still passed after the handoff.
The exports are deliberately not connected to IWER's session loop: IWER
cannot supply native WebGPU XR subimages, and treating its throwaway WebGL
layer as the production path would hide that distinction.

The next M4 slice is per-view rendering design: separate simulation from
render submission so one simulation tick can feed both eyes, pass each
`XRView` pose/projection and viewport into the renderer, and switch the color
attachment array layer between eyes without presenting or ticking twice.

### Regression matrix after Probes 1–7

- Clean Emscripten build: PASS. Known warnings remain the existing
  `-pthread`/memory-growth performance warning and the 629 MB preload-package
  size warning.
- Experimental Chrome + Playwright: PASS for XR-compatible device acquisition,
  JS/C++ texture import and readback, full UT frame in array layer 1, XR frame-
  loop ownership handoff, expected IWER/native-binding boundary detection, and
  IWER stereo lifecycle/teardown. Final run advanced XR frames `2 -> 183`.
- Ordinary Chrome + Playwright (no experimental flags): PASS. `XRGPUBinding`
  correctly remains unavailable while the IWER two-view lifecycle advances and
  ends cleanly.
- Ordinary WebGPU game harness: PASS on `DM-Deck16][` at approximately 59.8
  ticks/s, 95 draws/frame, 75 cached textures, 95 bind-group cache hits, zero
  new warm-frame bind groups, zero buffer rollovers, zero WebGPU errors,
  non-blank canvas, and clean quit.

## M5 frame/view refactor and browser audio checkpoint — PASS (2026-07-22)

The engine frame is now explicitly divided into `AdvanceGameFrame()`,
`RenderGameFrame()` and `FinishGameFrame()`. `RunOneFrame()` calls those three
phases in the original order, preserving the native and canvas behavior. The
XR diagnostic advances once, renders both views from that state, then performs
save/travel completion once.

`RenderSubsystem` now separates view-independent scene preparation from the
per-view geometry draw. Its M5 stereo-layer entry point updates BSP/light/
texture frame state once, renders two diagnostic eye transforms, and switches
the external attachment between array layers 0 and 1 without a second engine
tick. HUD/menu/flash/overlay handling is intentionally excluded from this path
until M9 chooses a world/quad-layer presentation.

`WebGPURenderDevice` now retains a browser-owned imported texture for a whole
external frame, supports layer/viewport selection both before and during a
lock, submits the first eye before reopening the pass on the second layer, and
releases the imported wrapper explicitly at frame end. The legacy one-frame
queue remains as a compatibility diagnostic. A compact state bitmask export
reports lock/external-frame ownership when browser automation fails.

The Emscripten target now links its existing OpenAL device to Emscripten's Web
Audio implementation. `BrowserAudioBridge` reports AudioContext lifecycle,
resumes it directly in the trusted Enter VR/Enable Audio click, and counts
completed resume promises. Browser-specific safeguards avoid the unsupported
meters-per-unit enum and Emscripten's effectively unbounded reported source
count.

Enabling real audio exposed a pre-existing browser lifetime bug rather than an
audio corruption: `commandline` pointed at a stack object after `callMain()`
returned and the RAF later queried `--debugstereo`. A symbols-enabled WASM
build traced the out-of-bounds access to `CommandLine::HasArg()` from
`RenderSubsystem::DrawGameInternal()`. The Emscripten command-line object now
has static lifetime, matching the already-static engine.

Final validation:

- Clean/incremental release Emscripten build: PASS; known pthread/memory-growth
  and 629 MB development-preload warnings remain.
- Experimental Chrome/IWER/WebGPU XR: PASS. Texture import/readback passed; a
  full UT frame rendered in array layer 1; RAF ownership paused/resumed; one
  stereo call advanced exactly one tick and produced 187 draw calls across two
  nonblank, varied layers with 1,159 differing sampled pixels; zero WebGPU
  errors; Web Audio was running; XR frames advanced `3 -> 182`; clean exit.
- Default Chrome/IWER: PASS with `XRGPUBinding` correctly absent; Web Audio
  running; XR frames advanced `5 -> 186`; clean exit.
- Ordinary WebGPU: PASS at approximately 60.1 ticks/s, 95 draws/frame, 75
  cached textures, 95 cache hits, zero WebGPU errors, nonblank canvas and clean
  quit.
- Native Windows Debug build: PASS for all shared engine/render/audio sources.

M5 is complete at the diagnostic boundary. The next implementation work is M6:
define the versioned packed JS/WASM XR frame ABI, feed native `XRView` matrices,
viewports and array layers into this proven consumer path, then replace the
IWER-only lifecycle with a real `XRGPUBinding` projection layer on a browser/
headset that supplies a native session. At this M5 checkpoint, M10 still lacked
tracked-head listener orientation, suspend/resume policy, legal data import,
persistence, deployment, and networking scope. Later checkpoints below close
the automated listener/lifecycle/importer/no-data seams; physical music/effect
testing, real user-data import, persistence, deployment, and networking remain.

## M6 packed XR frame ABI checkpoint — PASS (2026-07-22)

The first M6 slice now replaces ad-hoc bridge calls with a versioned single-
copy packet. ABI v1 is explicitly little-endian and packed:

- 36-byte header: version, total byte size, view count, flags, timestamp,
  reset generation, and color texture dimensions;
- 116-byte view: eye, array layer, viewport, position, orientation, and one
  4x4 float projection matrix;
- 268 bytes total for two views, with a maximum of two in v1.

`WebXRFrameBridge.h` uses fixed-width fields and compile-time size/offset
assertions. Native validation first copies the packed header/views into local
storage, rejects unknown flags, bad sizes/counts, non-finite values and out-of-
bounds viewports, then converts into aligned `WebXRSceneView` state. Exports
report ABI sizes, validate packets and expose a stable last-error code.

The browser writer copies only numbers from `XRFrame`/`XRView`/
`XRGPUSubImage`. It follows the current editor's draft by using
`getViewSubImage(layer, view)`, taking the array layer from
`getViewDescriptor().baseArrayLayer`, applying `subImage.viewport`, and
requiring one shared color texture for both views. That texture is visible to
Emdawnwebgpu only during one synchronous `Surreal_RenderWebXRFrame` call and is
cleared in `finally`.

The native entry imports the shared texture once, advances the game once,
renders each supplied projection/layer/viewport through the existing M5 seam,
finishes once and releases the wrapper before returning. Pose fields are
transported and validated but are intentionally not composed into the body
camera yet: coordinate conversion, recenter origin and calibrated
world-units-per-meter are M7, and guessing those would make headset scale
incorrect.

Validation evidence:

- JS layout diagnostic: 18/18 checks passed; native reports ABI version 1,
  header 36 and view 116.
- Experimental Chrome/IWER bridge diagnostic: packed render result 1, last
  error 0, tick `10 -> 11`, 187 accumulated draw calls, both layers nonblank
  and varied, 1,178 differing sampled pixels, zero WebGPU errors.
- The surrounding M4/M5 interop tests and session lifecycle still pass; the
  final XR run advanced frames `3 -> 184` and ended cleanly.
- Native Windows Debug and Emscripten release builds pass.

The current WebXR/WebGPU Binding editor's draft was rechecked during this
slice. It still labels the API unstable, requires an XR-compatible adapter and
the `webgpu` session feature, requires `layers` rather than `baseLayer`, uses
WebGPU `[0,1]` projection depth, returns one frame-scoped texture shared by the
projection views, and selects each view through `getViewDescriptor()`.

Remaining M6 blocker: IWER creates a JavaScript emulated session that Blink's
native `XRGPUBinding` constructor rejects. The production helper is ready, but
real projection-layer creation, compositor presentation, repeated entry/exit
and five-minute stability must run on a browser/headset with a native WebGPU-
compatible XR session. After that, M7 composes the transported pose.

## M6 opt-in native session lifecycle checkpoint — PASS WITH HEADSET GATE (2026-07-22)

Commit `1e6cc90e` wires the packed bridge into a production-shaped session
lifecycle without changing the default IWER route. Loading
`index_webxr.html?native-webgpu-xr=1` selects the native route explicitly; the
ordinary page still uses the emulated WebGL base layer and cannot be mistaken
for WebGPU compositor presentation.

The native Enter VR gesture now:

1. prevents duplicate pending/active entry;
2. requests `immersive-vr` with required `webgpu` and optional `local-floor`;
3. constructs `XRGPUBinding` from SurrealEngine's XR-compatible `GPUDevice`;
4. creates a color-only `bgra8unorm` projection layer and installs `layers`;
5. selects `local-floor`, falling back to `local`;
6. transfers engine RAF ownership only after all setup succeeds; and
7. drives the synchronous packed render bridge from the native XR RAF.

Every session has a monotonically increasing generation token. The next XR RAF
is queued before rendering, while callbacks from an ended generation are inert.
Setup rejection, native render failure and the session `end` event clear the
binding/layer/reference-space state and restore the normal canvas RAF. A null
viewer pose is instead a counted frame skip: simulation does not advance and
the session remains alive. Reference-space reset events increment the packet's
reset generation for M7.

Playwright validation:

- default IWER run: ABI 18/18, Web Audio resumed from the Enter VR click,
  frames `3 -> 173`, clean session end;
- experimental run: full external/packed stereo checks passed with zero WebGPU
  errors; the production entry point then reached the known Blink boundary,
  returned false with the native type-check error, never retained RAF
  ownership, and the canvas tick advanced `17 -> 27` after cleanup;
- after that expected failure, a fresh default IWER session advanced frames
  `3 -> 176` and ended cleanly.

This does not close M6. The test cannot construct a native Blink `XRSession`,
so the `XRGPUBinding` constructor, projection-layer creation, real subimages,
compositor output and headset timing remain unvalidated. The route also still
hardcodes `bgra8unorm`; preferred-format negotiation and alternate render
pipelines, visibility/device-loss/audio policy, repeated entry/exit, diagnostic
timings, and five-minute headset stability are missing. Position/orientation
composition remains deliberately assigned to M7.

## M10 redistributable no-data build checkpoint — PASS (2026-07-22)

Commit `194696bd` removes the unconditional commercial-data dependency from
the Emscripten link. When `SURREAL_GAMEDATA_DIR` is empty, CMake now omits
`--preload-file` entirely; a developer can still configure a local data tree
for the existing full-content smoke tests.

A clean temporary Emscripten configure with no game-data path produced link
commands containing no `--preload-file`. This is only the build seam: the
first-run browser importer, validation, quota reporting, OPFS/IndexedDB
persistence, config/save flushing, clear-data controls, launcher, and hosted
artifact audit remain M10 work. No commercial UT99 data was added or moved.

## M6 projection-format implementation checkpoint — PASS WITH HEADSET GATE (2026-07-22)

Commit `14451d9f` removes the `bgra8unorm` assumption. The production session
queries `XRGPUBinding.getPreferredColorFormat()` and accepts the current
draft's three supported color formats: `bgra8unorm`, `rgba8unorm`, and
`rgba16float`. `WebGPURenderDevice` reads the imported texture's actual format,
rejects unsupported formats, and selects a lazily-created pipeline family
compiled for that attachment. Pipeline families share the existing shader and
layouts; the family is selected once per render lock, leaving draw lookup O(1).

Direct browser diagnostics rendered a full external stereo frame in all three
formats. Each returned success, accumulated 187 draws, left external-target
state clear, and reported zero uncaptured WebGPU errors. The cache grew only as
new formats were exercised (`1 -> 2 -> 3`). The ordinary WebGPU smoke test also
passed at 95 draws with clean shutdown. Real projection-layer negotiation and
compositor acceptance remain headset-gated because IWER cannot supply the
native Blink `XRSession` required by `XRGPUBinding`.

## M7 tracked-pose implementation checkpoint — PASS WITH HEADSET GATE (2026-07-22)

Commit `b4b02dab` composes WebXR tracking into the UE1 render camera after the
same frame's `AdvanceGameFrame()`/`PlayerCalcView()` result. The conversion is
centralized as WebXR `(x,y,z) -> UE1 (-z,x,y)`. Body yaw remains authoritative;
tracked translation, yaw, pitch, and roll affect only per-eye render cameras
and do not mutate pawn physics or network state.

The bridge now:

- preserves the runtime's per-eye transforms and IPD;
- captures a viewer-center position/yaw recenter origin;
- responds to reference-space reset generations;
- exposes a configurable scale, defaulting to 39.3701 UU/m;
- rejects degenerate zero-length quaternions; and
- reflects the right-handed WebXR projection at the engine boundary exactly
  once while retaining the runtime's asymmetric projection.

The deterministic native self-test covers handedness, a 64 mm IPD, one metre
of translation, head yaw and pitch, body yaw, reset/recenter behavior, and
projection reflection. Browser automation returned self-test `1` and world
scale `39.370079`; native and Emscripten builds passed. Remaining M7 gates are
physical world-scale/eye-order validation, marker-scene parallax, collision
independence, recursive scene correctness, tracking-jump policy, seated versus
standing behavior, and a ten-minute comfort/recenter run.

## M6/M11 lifecycle-hardening checkpoint — PASS IN AUTOMATION (2026-07-22)

Commit `8f29634d` hardens both the default IWER lifecycle and the opt-in native
route. Monotonic session generations make stale RAF/end callbacks inert.
Cleanup now covers failure after `requestSession()` succeeds, explicit exit,
session end, render/setup exceptions, page shutdown, and WebGPU device loss.
Canvas RAF restoration is an idempotent ensure-running operation.

WebXR session visibility is handled independently from DOM visibility. A
hidden companion page does not silence an active immersive session; XR
`hidden` suspends lifecycle-owned audio, while `visible` and
`visible-blurred` resume it. `pagehide`/`beforeunload` share idempotent cleanup.
The engine device's `lost` promise ends an active session and permanently
rejects re-entry because this implementation does not recreate the renderer.

Extended Playwright/IWER validation passed in default and experimental Chrome:

- default session: XR frames `4 -> 185`, engine ticks `8 -> 189`, and canvas
  recovery `189 -> 211` after exit;
- experimental packed M6 render: result `1`, error `0`, 187 draws, zero GPU
  errors; pose and all three format diagnostics passed;
- expected native/IWER rejection restored the canvas tick (`21 -> 22`);
- three clean entry/exit generations completed with no stale callback damage;
- an injected failure after session acquisition called `end()` and cleared
  globals before a successful subsequent entry;
- DOM and XR visibility/audio policies passed independently; and
- destroying the real active `GPUDevice` tore down the session, rejected
  re-entry, and left repeated shutdown idempotent.

JavaScript syntax, Python parse, Emscripten build, native build, and diff checks
all passed. Physical headset work still owns compositor presentation, real
visibility transitions, sleep/wake, five-minute M6 stability, and the 60-minute
M11 soak.

## M8 packed controller/input checkpoint — PASS IN AUTOMATION (2026-07-22)

Commit `737916e5` upgrades the single-copy frame contract from ABI v1 to v2.
The header is now 44 bytes, each view remains 116 bytes, and up to two 128-byte
controller records follow the view array (532 bytes for stereo plus two hands).
Each record carries stable per-session source identity, handedness, connection/
mapping/pose flags, pressed and touched masks, four axes, eight analog button
values, and raw reference-space grip and aim poses.

The native decoder validates exact offsets/counts/size, known flags, finite
ranges, unique connected source IDs, and valid pose quaternions before changing
state. A valid zero-source packet publishes an empty new generation. The
aligned latest-state exchange normalizes handed sources into fixed slots,
replaces the whole prior snapshot, and is published before
`AdvanceGameFrame()` so `UpdateInput()` consumes it in the same simulation
tick. Pose/session reset also publishes an empty snapshot.

The browser collector tracks `session.inputSources` plus
`inputsourceschange`, assigns stable IDs with a per-session `WeakMap`, and
copies the live gamepad arrays every XR RAF. It applies a radial 0.15 dead zone
to each axis pair, preserves X, flips Web Gamepad Y to positive-forward/up,
clamps buttons/triggers, normalizes valid quaternions, and never retains XR
poses or mutable gamepad data after the callback. `xr-standard` keeps its
touchpad 0/1 and thumbstick 2/3 slots; generic sources have a deterministic
fallback.

Commit `b67c7083` consumes each input generation exactly once in the engine.
It keeps explicit per-controller analog and tracked-pose state while mapping
left buttons 0–5 to `Joy1`–`Joy6`, right buttons 0–5 to `Joy7`–`Joy12`, and
the two thumbsticks to `JoyX/Y/U/V`. Button edges and disconnect releases flow
through the existing `InputEvent`/keybinding system. A float axis path avoids
quantizing normalized stick input; flatscreen behavior is unchanged.

Validation evidence:

- JavaScript ABI diagnostic: 44/44 checks, 532-byte maximum packet;
- copied input collector: 37/37 checks, including in-place live-object
  mutation, stable IDs, dead-zone/sign normalization, source removal, and
  empty clearing;
- C++ state self-test: pass;
- packed stereo render: one published and processed generation, two sources,
  synthesized held mask `129`, axes `[0.25, 0.75, -0.5, 0.25]`, triggers
  `[0.6, 0.9]`, pose flags `[4, 7]`, and zero GPU errors;
- default and experimental Playwright/IWER suites: pass;
- ordinary WebGPU: 60.0 ticks/s, 95 draws, zero GPU errors, clean quit; and
- native Windows Debug and Emscripten builds: pass.

This checkpoint was not M8 exit. Later checkpoints below close the Quest
defaults, locomotion/turning, world-space hand composition, scoped weapon aim,
fire haptics, selectable movement reference/dominant hand, and controller
recenter/menu seams. Input-profile labels/models, settings UI/persistence,
safe-exit UX, two-hand policy, complete weapon fixtures, and physical headset
tests for disconnect/reconnect, handedness, focus loss, and re-entry remain.

## M8 locomotion/default-binding checkpoint — PASS IN AUTOMATION (2026-07-22)

Commit `c78b972f` installs Quest-style runtime defaults only where a `Joy*`
binding is blank. It borrows the user's existing mouse/keyboard commands when
available and never overwrites or persists an explicit Joy binding. Left-stick
input remains in the ordinary remappable `JoyX/JoyY` path but expands to UE1's
expected full-scale 7000 movement domain.

Right-stick turning now defaults to a configurable 30-degree snap with 0.75
activation, 0.35 rearm hysteresis, and one turn per deflection. Smooth,
Binding, and Disabled modes are available through `[Engine.WebXR]`. Snap and
smooth turns write only body/view yaw before M7 composes tracked head pose;
position, pitch, roll, collision, and physics are unchanged.

Native and Emscripten builds passed. The direct WASM diagnostic returned
`selfTest=1`, movement scale `7000`, default mode Snap, valid mode switching,
and rejected invalid settings. The complete default Playwright/IWER suite also
passed. At this checkpoint, head-/hand-relative movement, persisted settings/UI,
recenter/menu/exit actions, real input-profile verification, and headset tuning
remained. Commit `8cc2daf5`, recorded below, subsequently closes the engine-side
movement-reference, dominant-hand, recenter, and menu actions; browser
persistence/UI, safe exit, hardware verification, and tuning remain.

## M8 browser-haptics checkpoint — PASS IN AUTOMATION (2026-07-22)

Commit `ea10a97f` adds a scalar-only per-hand haptic queue to the WebXR session
owner. It resolves live sources/actuators only at queue and dispatch time,
supports `pulse()` and `playEffect()`, clamps intensity/duration, coalesces
queued pulses, rate-limits each hand to 50 ms, and exposes a disable switch.
Pending feedback is dropped on hidden/inactive sessions, source loss,
unsupported actuators, disable, and stale session generations.

The deterministic fake-actuator test passed 38/38 policy/routing checks. After
a clean Emscripten rebuild, both the default and experimental full Playwright
suites passed; the latter also retained the packed stereo/input/pose/format
results with zero WebGPU validation errors. Native and Emscripten builds,
JavaScript syntax, Python AST, and diff checks pass.

This is browser plumbing, not gameplay-complete or physical haptics. Engine
events for fire, pickup, damage, and UI confirmation still need to request
pulses; the setting must be persisted/exposed; and real Quest actuator latency,
focus loss, disconnect, and session re-entry must be tested.

## M8 weapon-aim audit checkpoint — IMPLEMENTATION PLAN RECORDED (2026-07-22)

The firing path is UnrealScript-driven. All relevant calls converge at
`Frame::Call`, so the minimum clean seam is a re-entrant RAII scope that saves
the local pawn's `ViewRotation`, substitutes a composed dominant-hand aim only
for weapon calls, and restores it on every exit. Controller poses must first
reuse M7's scale/recenter/body-yaw composition; the current raw reference-space
poses cannot drive gameplay directly.

`TraceFire` and `ProjectileFire` do not cover the full UT arsenal. The
classifier also needs Flak `Fire`/`AltFire`, Eightball `FireRockets.BeginState`
and `CheckTarget`, Translocator `ThrowTarget`, Chainsaw `Slash`, and Impact
Hammer `TraceAltFire`/firing `Tick`; guided-warhead steering needs its own
policy. Layered WebXR currently suppresses all overlays, so a weapon-only
`RenderOverlays` pass is required per eye before M9 restores the HUD/menu.

The first safe cut changes firing direction/rotation while leaving the stock
head-relative origin. A generic `CalcDrawOffset` override can double-apply the
different weapon `FireOffset` terms, so controller-origin firing requires
verified per-path handling. Stock `ServerMove` cannot replicate independent
body and hand rotations; multiplayer weapon aim remains an explicit M10
protocol/product decision rather than an M8 completion claim.

## M8 world-hand pose checkpoint — PASS IN AUTOMATION (2026-07-22)

Commit `102a4d71` shares M7's viewer-center recenter origin, coordinate
conversion, and world scale with grip/aim poses. Recenter capture now occurs
before the input snapshot and `AdvanceGameFrame`. The snapshot carries
recentered UE1-local position/forward, and `UpdateInput` composes both hands
into world position/forward/rotator after comfort turning but before console,
level, weapon, or state ticks. A first-frame actor-location fallback prevents
use of an uninitialized scripted camera; login/map changes reset that guard.

Right hand is the deterministic default dominant controller. Disconnect or an
invalid right pose clears the index instead of retaining stale transforms;
grip remains an explicit fallback when aim is absent. The experimental browser
diagnostic returned controller-pose self-test `1` and dominant index `1` after
the packed input frame. Native/Emscripten builds and the complete experimental
suite pass. Full orientation basis/roll for controller-attached viewmodels and
physical pose alignment remain open.

## M8 scoped weapon-direction checkpoint — PASS IN AUTOMATION (2026-07-22)

Commit `38e2ef47` adds a disabled-by-default, re-entrant RAII seam around
enabled `Frame::Call` dispatch. Cleanup executes on direct return, nesting, and
C++ exception unwinding. Commit `032a1a9f` installs one UT/WebXR Engine-owned
hook and clears it before engine destruction.

The hook requires the local pawn's exact current/owned weapon and a connected
dominant tracked aim pose. It temporarily replaces `Pawn.ViewRotation` only
for `TraceFire`, `ProjectileFire`, and the audited Botpack special paths (Flak,
Eightball, Translocator, Chainsaw, and Impact Hammer), then restores the exact
integer rotator. `CheckTarget` is counted separately. Generic `Fire`/`Tick`,
`GuidedWarShell`, `RenderOverlays`, and `CalcDrawOffset` fail closed.

Commit `616ad67e` restores only the current weapon's `RenderOverlays` once per
WebXR eye, with RAII restoration for canvas/device state and current weapon
location/rotation. HUD/menu/player overlays remain deferred. Automation proves
two expected and two completed weapon-overlay eye passes; the acceptance map
had no current weapon at the diagnostic instant (`weaponCalls=0`), so a visible
weapon/muzzle-flash fixture and headset validation are still required.

The classifier/restoration self-test returned `1`; native/Emscripten builds and
the full experimental packed stereo/input/pose/format/lifecycle suite pass with
zero WebGPU errors. Firing origin remains stock/head-relative, Translocator's
stock camera snap is intentionally restored away in VR, guided-warhead steering
has no controller policy, and multiplayer does not transport independent aim.

## M8 engine haptics and sibling-fix checkpoint — PASS IN AUTOMATION (2026-07-22)

Commit `ebbcdb19` adds a scalar-only Emscripten bridge to the browser haptic
queue. Commit `e4260c0c` multiplexes dominant-hand recoil requests through the
same classified firing scope, loads `[Engine.WebXR] HapticsEnabled`, exposes
diagnostics/setter/self-tests, and keeps browser coalescing/rate limits
authoritative. Experimental automation returned aim self-test `1`, haptic
bridge self-test `1`, and accepted disable/re-enable operations. Pickup,
confirmed damage, and UI feedback remain open, as does all physical tuning.

The read-only native VR worktree audit also found two correctness bugs. Commit
`fc85ed63` distinguishes byte-backed `bFire`/`bAltFire`/`bDuck` from packed
boolean properties in `GetBool`/`SetBool`, preventing adjacent-property
corruption. Commit `08afeb32` fixes in-place quaternion multiplication and adds
compile-time Hamilton-product checks. Both native and Emscripten builds pass.
The sibling's dirty screen-quad/tuning batch was not copied or cherry-picked.

## M8 full-basis weapon-presentation checkpoint — PASS IN DETERMINISTIC TESTS (2026-07-22)

Commit `b5917f10` preserves controller roll without changing the safer
direction-only ballistic policy. `WebXRFrameBridge` converts and recenters the
complete aim/grip forward/right/up basis, repairs handedness against the
expected up vector, and carries it into the engine. World composition
orthonormalizes that basis after body yaw. `WorldRotation` remains a zero-roll
direction for traces/projectiles; `WorldPresentationRotation` round-trips the
full basis, including the pitch singularity, for the viewmodel.

The weapon VM classifier accepts the exact global `RenderOverlays` function
for the local current weapon across Engine, Botpack, inheritance, and mod
packages. During that presentation call only, pawn and weapon rotation receive
the dominant aim basis, or tracked grip as a presentation-only fallback, and a
re-entrant cleanup restores both exact integer rotators. The already-existing
weapon-only per-eye renderer dispatches this scope; it does not call the weapon
again or restore player/HUD/menu overlays.

Native and Emscripten builds pass. Deterministic self-tests cover nonzero roll,
basis orthogonality/round-trip, singular orientation, package-independent
classification, nested presentation scopes, and byte-exact restoration. The
WASM diagnostic surface exposes right/up/full presentation values and the
presentation counter, but this commit did not add a new browser assertion for
them. Physical barrel alignment, position/scale, mirroring, tracking loss,
muzzle origin, automatic/special weapon fixtures, and Quest presentation are
still open.

## M9 initial capture-once stereo HUD checkpoint — BUILDS PASS; BROWSER GATE NOT REACHED IN THIS RUN (2026-07-22)

Commit `e387ec3f` adds a renderer-local display-list seam for essential UT99
HUD/crosshair output. With a local `myHUD`, `PlayerPawn.PostRender` runs once on
a stable 1280x960 logical canvas while tile/text/clipped-tile/2D-line device
submission is captured. The immutable commands are then replayed once per eye.
This prevents message queues, mutators, animation, and other stateful
UnrealScript work from advancing independently for left and right views.

One common head-locked 4:3 plane is configured by distance, horizontal FOV,
aspect, and safe-area fraction. Each eye's rectangle comes from projecting the
plane corners with its exact `Projection * WorldToView`; rectangles are
clamped and all canvas/device state restores through exception-safe scopes.
Diagnostics count state updates, command capture, eye presentations, clamps,
and unsupported draws. The self-test covers one update/two presentations,
asymmetric projection, forced clamp, and absent HUD.

Native and Emscripten builds plus diff checks pass. The experimental
Playwright/IWER run at this checkpoint passed ABI, copied-input, fake-haptics,
immersive support, XR-compatible WebGPU and `XRGPUBinding` probes, then timed
out waiting for the engine/Web Audio boot gate after importer integration. It
never executed HUD presentation, so that run was neither a browser HUD pass nor
a HUD failure. A later full experimental pass and the layer-reselection
correction are recorded below. Console,
UWindow/menu/cursor, `PreRender`, full player overlays, actor/clipped-actor/3D
canvas primitives, settings UI, and physical stereo/readability/comfort remain
open; unsupported 3D-style capture calls are suppressed and counted.

## M10 local UT99 importer checkpoint — 13/13 SYNTHETIC TESTS PASS (2026-07-22)

Commit `87f8324f` holds `Module.callMain()` behind a legal local-data gate.
Developer preloads bypass it; otherwise the user can select their own install
with `showDirectoryPicker()` or a directory file-input fallback. Paths are
canonicalized and traversal, absolute paths, NUL components, and case
collisions are rejected. Validation checks required directories, packages,
INI, executable, and representative asset extensions without reading or
uploading contents.

Schema `surrealengine-ut99-data` version 1 prefers OPFS and falls back to
IndexedDB. A new dataset is staged completely before metadata publication;
failure deletes staging, successful replacement retires the old dataset, and
re-import reloads instead of mutating the live filesystem. Storage quota and
persistence are reported, progress covers scan/store/restore, corruption and
eviction produce actionable errors, and saved blobs stream into `/gamedata`
before main starts. Clear-data is available. This persists imported game data,
not yet configs, bindings, VR settings, saves, or logs.

`python -u web/smoke_test_ut99_importer.py` passes all 13 deterministic cases:
schema/version; content-free layout validation; missing-file diagnostics;
path traversal/case collision; directory fallback; IndexedDB round-trip;
Emscripten-FS streaming; developer-preload bypass; no-data wait gate; later
saved-import boot; safe re-import; clear; and OPFS round-trip/missing-dataset
behavior when available.

All importer fixtures use fake data. A subsequent real `SURREAL_GAMEDATA_DIR`-
empty build and clean-profile wait-state audit closed the no-preload artifact
gate, as recorded below. Still required: import a complete user-owned
installation in clean desktop and Quest profiles, reach a playable map, measure
full-size quota/copy/thermal behavior, and verify restart persistence,
eviction/corruption recovery, and future schema migration. No test so far has
imported or redistributed real UT99 data.

## M8 configurable movement/action checkpoint — PASS IN AUTOMATION (2026-07-22)

Commit `8cc2daf5` completes the engine-side movement-reference, dominant-hand,
recenter, and menu slice. `[Engine.WebXR] MovementReference` accepts `Body`,
`Head`, or `DominantHand`; the latter two project the same-frame tracked forward
vector into the horizontal body-local plane and deterministically fall back to
body-forward when tracking is absent, non-finite, or vertical. Only the
left-stick axes are rotated: UE1's 7000 movement scale, pawn physics, collision,
and the existing input binding path stay authoritative.

`DominantHand` selects left or right for both movement and existing weapon/
haptic roles. `RecenterButton` defaults to right-stick click and rebuilds the
shared eye/head/hand recenter origin on its press edge. `MenuButton` defaults to
disabled so the existing Joy6/Escape mapping keeps precedence; when explicitly
assigned, its press/release edges synthesize Escape. Both actions reserve only
an otherwise-unbound normalized Joy slot, and a collision disables the menu
action so one edge cannot both recenter and toggle UI. The engine loads these
settings with the turn/haptics settings and writes them on its normal config-save
path. Browser-side persistence of that config remains M10 work.

The deterministic locomotion self-test covers head- and hand-relative axis
rotation, body fallback, non-finite/vertical rejection, action edges, binding
precedence, button parsing, and invalid setter rejection. Native and Emscripten
builds pass. The full experimental smoke reported:

```text
[harness] M8 world controller-pose diagnostics: {'selfTest': 1, 'dominantIndex': 1, 'aimBasis': [-0.026841429993510246, -0.9996397495269775, 0, 0, 0, 1, 0, 32488, 0], 'locomotionSelfTest': 1, 'movementReference': 0, 'dominantHand': 2, 'configuredRecenter': 10, 'effectiveRecenter': 0, 'configuredMenu': 0, 'effectiveMenu': 0}
```

Here `movementReference=0` is the configured body default and `dominantHand=2`
is right. The developer data already binds Joy10, so `effectiveRecenter=0` is
the intended do-not-steal-user-bindings behavior, not a failed action test.
Settings UI, browser persistence, physical profile/index verification, and
in-headset tuning remain open.

## M10 tracked-head audio listener checkpoint — PASS IN AUTOMATION (2026-07-22)

Commit `b6b74c00` adds the renderer-independent low-level listener contract.
`AudioListenerPose` carries UE1-world position, full forward/up basis, and
velocity. The OpenAL backend validates finite independent axes, normalizes and
orthogonalizes them, applies the existing `(x, y, -z)` reflection to position,
orientation, and velocity, and retains the normal `CameraActor` fallback for a
missing or invalid explicit pose. Its startup math self-test covers identity,
full yaw/pitch/roll, skew repair, reflection, and invalid vectors.

Commit `f4728617` supplies that explicit pose from the recentered center-head
sample after `PlayerCalcView`, recomposed against the exact camera anchor and
body yaw used by the XR render frame. Velocity is a finite difference only for
consecutive frame generations with an unchanged recenter count, finite elapsed
time in `(0.0001, 0.25]` seconds, and displacement at or below `157.4804` UE1
units. First samples, recenter, tracking loss/reacquisition, generation gaps,
long stalls, teleports, and new sessions reset velocity to zero instead of
creating a Doppler spike; missing tracking uses the ordinary camera listener.

## M9 active-eye HUD correction and full smoke — PASS IN AUTOMATION (2026-07-22)

Playwright exposed a WebGPU render-pass bug in the first HUD implementation:
capturing once was correct, but replaying after both eyes by re-selecting each
already-rendered array layer opened a clearing pass and erased the scene below
the HUD. Commit `46b5149f` still captures state once before the eye loop, but
replays each eye's immutable HUD commands while that eye's scene/weapon pass is
already active. It never reselects a completed layer.

The repaired full `--experimental-webgpu-xr` smoke passed the packed stereo,
input, pose, format, lifecycle, and device-loss gates with zero WebGPU errors.
Packed stereo rendered `189` draws: left `nonBlack=300434`, `distinct=865`;
right `nonBlack=300606`, `distinct=903`; `differentSamples=1175`. Its exact
combined presentation/audio diagnostic was:

```text
[harness] M8/M9/M10 presentation and audio diagnostics: {'aimSelfTest': 1, 'hapticsSelfTest': 1, 'disableAccepted': 1, 'enableAccepted': 1, 'expectedWeaponEyes': 2, 'weaponEyePasses': 2, 'weaponCalls': 0, 'hudSelfTest': 1, 'expectedHudEyes': 2, 'hudStateUpdates': 1, 'hudEyePresentations': 2, 'hudCapturedCommands': 65, 'hudUnsupportedDraws': 0, 'hudClampedViewports': 0, 'audioListenerActive': 1, 'audioListenerUpdates': 1, 'audioVelocityResets': 1, 'audioVelocity': [0, 0, 0]}
```

`weaponCalls=0` only means the sampled frame had no current weapon fixture; the
two weapon-eye dispatches still occurred. The single listener update/reset and
zero velocity are the expected safe first tracked sample. This desktop/IWER
automation is not physical Quest proof of stereo fusion, HUD readability,
tracked spatial audio, controller alignment, or browser audio gesture behavior.

## M10 real no-data artifact gate — PASS; REAL IMPORT OPEN (2026-07-22)

The real release-shape gate used a separate build directory and explicitly empty
cache value:

```powershell
C:\Devstuff\emsdk\upstream\emscripten\emcmake.exe cmake -S . -B build-emscripten-nodata -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DSURREAL_GAMEDATA_DIR:PATH=
cmake --build build-emscripten-nodata --target SurrealEngine -j 4
```

CMake emitted the expected no-game-data warning. The cache contains
`SURREAL_GAMEDATA_DIR:PATH=`; the emitted link command contains neither
`--preload-file`, `@/gamedata`, nor a commercial install path; there is no
`.data` artifact; and generated JS contains no `SurrealEngine.data` or package
loader. (`--preload-file` appears only in two stock Emscripten filesystem error
help strings, not link/package metadata.) Artifacts were:

- `SurrealEngine.js`: 564,900 bytes, SHA-256
  `F30171ADF2F2D8C8D73830EEB40B8C2EAE316BC4D07082533E1A31CC2D940C6F`;
- `SurrealEngine.wasm`: 6,257,417 bytes, SHA-256
  `6DB92E545D57CFAD4A3C24F4DEC5F9A4E5F164580F3A9B13F8E54336D142D319`.

A brand-new Playwright Chrome process/profile, launched headless with only
`--enable-unsafe-webgpu`, loaded that exact build over the COOP/COEP test server.
It was cross-origin isolated, acquired WebGPU, initialized WASM, made no `.data`
or `/gamedata/` request, showed the importer, and resolved
`{state: "waiting-for-import", backend: "opfs", error: null}` with
`surrealBooted=false`, `surrealCrashed=null`, and no request/page errors. The log
contained no `game data ready`, `calling main`, or `callMain threw`, proving main
remained gated. No real UT data was selected. A real full-size user import,
playable-map boot, restart/eviction/migration tests, and Quest storage behavior
remain open.

## M9 configurable HUD-plane checkpoint — PASS IN AUTOMATION (2026-07-22)

Commit `49c60be1` makes the active-eye HUD seam configurable through
`[Engine.WebXR]` and the Emscripten diagnostic/setter surface. `HudEnabled`,
`HudDistanceUU`, `HudHorizontalFovDegrees`, `HudAspectRatio`, and
`HudSafeAreaFraction` default respectively to `True`, `68.8976` UU (1.75 m at
39.3701 UU/m), `50`, `4/3`, and `0.90`. Strict setters accept finite values only
within distance `19.685..157.4804` UU, FOV `20..75` degrees, aspect
`0.75..2.0`, and safe area `0.50..1.0`; rejected values leave the configured
and effective renderer state unchanged. The existing config-save path writes
all five settings. Separate configured/effective getters make propagation to
the renderer directly observable.

Disabled means zero HUD work, not an invisible state update: it skips
`PlayerPawn.PostRender`, clears any stale captured display list, expects zero
eye presentations, and performs zero HUD state updates/captures/replays while
the world and weapon eye passes continue. The HUD self-test mask expands to
`31`, adding the disabled lifecycle to update/presentation, asymmetric
projection, viewport-clamp, and absent-HUD coverage; a separate range test
covers endpoints plus non-finite and out-of-range rejection.

The full experimental smoke rerun passed. HUD settings diagnostics returned
`selfTest=1`, mask `31`, configured/effective enabled `1`, distance
`68.8975983`, FOV `50`, aspect `1.3333330`, and safe area `0.89999998`. Its
runtime disable/restore assertion reported exactly:

```text
[harness] M9 disabled-HUD lifecycle: {'original': 1, 'disabled': 1, 'invalidDistance': 0, 'effective': 0, 'expectedEyes': 0, 'stateUpdates': 0, 'eyePresentations': 0, 'weaponEyes': 2, 'rendered': 1, 'restored': 1, 'restoredEffective': 1}
```

This proves the disabled path preserves stereo world/weapon rendering in
desktop/IWER automation. A user-facing settings panel, browser persistence of
the INI, in-headset distance/FOV/safe-area tuning, readability, fusion, and
comfort validation remain open.

## M10 redistributable PWA/deployment shell — 27/27 CHECKS PASS (2026-07-22)

Commit `28070633` adds the installable no-data entry point
`web/index_webxr.html?pwa=1&build=build-emscripten-nodata`, manifest, offline
guidance page, registration diagnostics, versioned service worker, deployment
instructions, and local-server MIME/header support. Registration is enabled
only for the known no-data build (or explicitly disabled); requesting PWA mode
with the developer preload route returns `refused-development-preload` and
registers no worker.

Service-worker version `2026.07.22-m10.1` uses a closed allowlist. It precaches
only the launcher, WebXR/importer/registration scripts, manifest, offline page,
and existing project icons. Only no-data/release `SurrealEngine.js` and
`SurrealEngine.wasm` may enter the lazy versioned runtime cache. `/gamedata`,
local-import database paths, `.data`, and UE1 package/map/music/sound/texture
extensions are rejected with policy `blocked-commercial-data` and HTTP 451;
the development runtime and every other URL remain network-only. Imported data
stays exclusively in OPFS/IndexedDB, outside Cache Storage.

Installation fills the complete shell cache before activation. Activation
refuses an incomplete shell, then removes only obsolete
`surrealengine-webxr-*` caches and preserves unrelated origin caches. Launcher
HTML is network-first and refreshes its canonical offline copy; versioned
no-data JS/Wasm is cache-first; an uncached offline navigation receives the
diagnostic offline page with status 503. Worker status/version, activation, and
fetch failures are observable. `APP_VERSION` must change whenever shell/runtime
compatibility changes.

`python web/smoke_test_pwa.py` passed 27/27 Playwright checks covering
registration/control, policy self-test, versioned caches, atomic old-cache
cleanup, exact shell allowlist, manifest/MIME, COOP/COEP/CORP headers,
service-worker revalidation, lazy no-data runtime caching, development-preload
non-caching and registration refusal, explicit commercial-data rejection,
network-first updates, cached offline launch, 503 offline guidance, and absence
of unexpected page errors.

Deployment still requires a real HTTPS host (localhost only satisfies
development), validation of Quest Browser installation/update/offline behavior,
immutable release hashes and version bump discipline, a complete artifact and
third-party license audit, rollback testing, and proof that no commercial or
local database export enters published files. This shell does not complete the
full map/settings/diagnostics launcher UX or networking decision.

## Native sibling audit addendum — provenance and reuse boundary (2026-07-22)

The read-only `SurrealEngine-vr-m2` audit separates committed evidence through
`4c504c7d` from about 2,618 dirty additions across 15 tracked files plus
untracked plans/tools. The committed controller series provides reusable
designs for dominant-hand resolution, foregrip hysteresis/two-hand blending,
left-hand mirroring, dual-Enforcer ownership, and synthetic
`--debugvrhands`/`--debugvrfire`/`--debugvrtwohand`/
`--debugvrdualenforcer`/`--debugvrgeometry` fixtures. The dirty `--vrtune`
workflow suggests release-before-capture arming, separate grip/aim markers,
raw one-hand calibration, gameplay suppression, package-qualified tables, and
persistent metadata-rich output; it contains no finished verified weapon table.

Do not copy the dirty `XrCompositionLayerQuad`/`vrQuad*` path, per-eye repeated
`RenderOverlaysVR`/`PostRenderVR`, mixed viewport/menu cursor coordinates,
direct `ShowMenu`/blind `bShowMenu` manipulation, generic `CalcDrawOffset`
origin override, or `PlayerViewOffset` fallback. The latest native quad/menu
test remained face-locked/unusable and its benchmark loaded inconsistent maps,
so provenance and branch execution were not proven. Native Quest evidence
does cover session/stereo fixes and basic movement/fire/yaw/pitch, but not
WebXR/WebGPU, Quest Browser profiles, full-basis weapon feel, two-hand tuning,
calibration values, importer/storage, current HUD fusion, audio lifecycle, or
release comfort/performance gates.
