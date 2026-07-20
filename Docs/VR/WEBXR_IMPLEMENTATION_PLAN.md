# SurrealEngine WebXR Port (Option A) — Detailed Implementation Plan

Companion to `PLAN.md` (high-level decision history/status log) and
`WEBXR_PORT_PLAN.md` (the Option A vs Option B comparison that led here).
This file is the granular, execute-without-supervision technical plan for the
WebXR/Option A build, mirroring `VR_IMPLEMENTATION_PLAN.md`'s role for the
native VR effort. Written 2026-07-18, the day the user said "Go with option
A, do the WebXR port" — superseding the earlier "native VR first, WebXR is
M5/deferred" decision (see memory `ut99-vr-native-port.md`).

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
3. **M3: Bindless-texture-model redesign.** `DescriptorSetManager`'s existing
   overflow-safety-valve *policy* (flush-and-clear when the array fills,
   `DescriptorSetManager.cpp`/`VulkanRenderDevice.h:206-213`) is already the
   right shape for a fixed-size WebGPU budget — this milestone retargets the
   *mechanism* (Vulkan descriptor-set update → WebGPU bind-group recreation),
   not the policy.
4. **M4: WebXR session integration.** `XRGPUBinding` (Immersive Web Editor's
   Draft, see `WEBXR_PORT_PLAN.md`), stereo rendering reusing the
   asymmetric-frustum/coordinate-convention math already verified for native
   VR (`RenderScene.cpp`, this doc's — er, `VR_IMPLEMENTATION_PLAN.md`'s — M2
   step 6 convergence-sign addendum).
5. **M5: Input, comfort, deploy.** WebXR controller input, comfort options,
   PWA/hosting pipeline — leaning directly on `webxr-port/`'s proven harness
   (IDBFS persistence, headless verification, PWA packaging).

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

## M3: re-scoped (2026-07-19 audit)

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

What actually remains in the code as of this audit:

- **Per-draw bind-group creation**: `DrawEntry` (`WebGPURenderDevice.cpp:500-546`)
  calls `wgpuDeviceCreateBindGroup` + `wgpuBindGroupRelease` on every draw
  call, every frame — a deliberate M2 simplification (comment at line 513).
  This is the real remaining work: cache bind groups keyed by the 4
  `WebGPUCachedTexture*` + 3 sampler-mode ints already stored per
  `WebGPUDrawBatchEntry` (`WebGPURenderDevice.h:15-28`), invalidating on
  texture destruction/update (`WebGPUTextureManager::Flush`,
  `UpdateTextureRect`).
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

Re-scoped M3 plan: (1) measure frame time + draw/buffer stats on a heavier
map than `DM-Deck16][` in desktop Chrome; (2) implement the bind-group
cache; (3) raise buffer sizes if step 1 shows multiple submits per frame.
Full task breakdown in `Docs/VR/NEXT_STEPS_PLAN.md` (Task 5). Whether steps
1-3 are *sufficient* for Quest 3 browser performance can't be answered until
M4 runs on the headset — that's an M4/M5 finding, not an M3 blocker. If this
re-scope holds, M3 is small (days, not weeks), and **M4 (WebXR stereo
session) becomes the next substantial milestone.**

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
