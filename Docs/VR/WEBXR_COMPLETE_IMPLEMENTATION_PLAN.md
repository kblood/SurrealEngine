# SurrealEngine UT99 WebXR — Complete Implementation Plan

**Canonical roadmap:** 2026-07-22  
**Branch/worktree:** `webxr-m1` in `ut99-vr/SurrealEngine`  
**Target:** Unreal Tournament 1999 running as the existing SurrealEngine C++
game compiled to WebAssembly, rendered with WebGPU, and presented through
WebXR on Quest-class headsets.

This is the standalone execution plan. `WEBXR_IMPLEMENTATION_PLAN.md` remains
the chronological engineering journal with the detailed M1–M4 investigation,
failed probes, measurements, and implementation notes. `WEBXR_PORT_PLAN.md`
records the earlier architecture decision that selected the Emscripten +
WebGPU port over a ground-up Three.js rewrite.

## 1. Product definition and non-negotiable constraints

The finished product must:

1. Boot the real SurrealEngine/UnrealScript runtime in a browser from legally
   user-supplied UT99 data.
2. Enter and leave an immersive WebXR session without reloading the game.
3. Render two correct, head-tracked views from one game simulation tick.
4. Support Quest-style tracked controllers for movement, aiming, firing,
   interaction, menus, and recentering.
5. Provide comfortable defaults, usable HUD/menu presentation, working audio,
   persistent settings/saves, and an ordinary non-XR canvas fallback.
6. Run from HTTPS with the isolation headers required by the pthread-enabled
   WebAssembly build and without redistributing commercial UT99 assets.

Constraints:

- UT99 game data is commercial. It must never be committed, embedded in a
  public build, uploaded to a project server, or placed in a service-worker
  cache. The user imports their own local installation.
- The direct presentation design depends on the unstable WebXR/WebGPU Binding
  Module. In Chrome 150 it is available only behind
  `WebXRWebGPUBinding,WebXRLayers`; it is not a default shipping API yet.
- A WebGPU-compatible session uses `layers`, not `XRWebGLLayer`/`baseLayer`,
  and its projection matrices use a `[0,1]` depth range.
- The browser owns projection-layer textures. The engine may import their
  JavaScript `GPUTexture` objects into Emdawnwebgpu, but must render while the
  corresponding native XR animation frame is active.
- The existing native/OpenXR work in the `SurrealEngine-vr-m2` worktree is a
  separate branch. Reuse proven math and design findings deliberately; do not
  mix worktrees or make commits on the other branch.

## 2. Current architecture and status

```text
XRSession.requestAnimationFrame
        │
        ├─ XRFrame.getViewerPose(referenceSpace)
        ├─ XRGPUBinding.getViewSubImage(layer, XRView)
        │      ├─ browser-owned color texture array
        │      ├─ array layer / view descriptor
        │      └─ viewport
        │
        ├─ JS writes a compact per-frame/per-view state block to WASM
        └─ synchronous C export
               ├─ advance simulation once
               ├─ render left eye
               ├─ switch attachment layer
               ├─ render right eye
               └─ submit before returning to the browser
```

| Milestone | State | Result |
|---|---|---|
| M0 — architecture/platform gate | Complete, gate ongoing | Emscripten + WebGPU selected; direct WebXR/WebGPU path verified behind Chromium flags |
| M1 — browser/WASM engine | Complete | Real engine boots, loads UT99 data, ticks, and quits under browser RAF |
| M2 — WebGPU renderer | Complete | UT99 world/HUD renders through Emdawnwebgpu |
| M3 — WebGPU hot path | Complete | Bind-group caching and buffer diagnostics measured on small and large maps |
| M4 — presentation groundwork | Complete | XR-compatible device, texture import/readback, full UT frame in array layer 1, frame-loop ownership handoff |
| M5 — frame/view refactor | Complete (diagnostic projections) | One simulation tick now renders two independently selected texture-array layers; real `XRView` data starts M6 |
| M6 — native WebGPU XR session | Implementation complete; headset validation gated | Packed ABI, preferred-format pipeline families, synchronous renderer, and hardened production session/RAF lifecycle are implemented; real `XRGPUBinding` compositor presentation still requires a supported runtime |
| M7 — tracking/camera/world scale | Deterministic implementation complete; headset validation gated | 6DoF pose conversion, body/head composition, recentering, world scale, and exact per-eye projection are implemented; physical scale and scene correctness remain to validate |
| M8 — controller input/gameplay | In progress | ABI v2 input, Quest defaults, body/head/dominant-hand locomotion, turning, selectable dominant hand, controller recenter/menu actions, world-composed full-basis hands, scoped controller-direction firing, roll-preserving per-eye weapon presentation, confirmed fire/damage/pickup/menu-confirm haptics, and browser-persisted VR controls are implemented; controller-relative position/origin, per-weapon tuning, two-hand UX, fixtures, safe-exit UX, and headset validation remain |
| M9 — UI/comfort/VR presentation | In progress | HUD plus console/menu 2D output is captured once and replayed per eye on a finite-depth plane, including UI-only frames. Dominant-hand aim drives UT's absolute cursor, and a menu-gated trigger safely selects without firing behind the menu. Strict plane settings have a validated persistent browser panel; stick/D-pad navigation, actor-style canvas draws, pointer-loss UX, recenter UX, comfort policies, complete loading/pause presentation, and headset readability remain |
| M10 — audio/data/network/deploy | In progress | Real Web Audio output, tracked-head listener, no-data builds, local OPFS/IndexedDB UT99 import, allowlisted mutable settings/save/log persistence, an installable PWA, an offline-only browser MVP scope, and fail-closed release staging/auditing are implemented; real full-install import, abrupt-termination/storage tests, fuller launcher UX, production HTTPS/Quest installation, and physical audio/storage validation remain |
| M11 — performance/robustness/release | In progress | Automated session/lifecycle/device-loss coverage and a reproducible 83-check no-commercial-data release gate exist; Quest profiling, headset lifecycle, compatibility, soak, and physical release gates remain |

## 3. M0 — architecture and platform gate

### Completed

- Chose the existing C++ engine compiled with Emscripten rather than replacing
  the UnrealScript VM/gameplay with a new JavaScript engine.
- Confirmed there is no practical automatic Vulkan-to-WebGPU translation.
- Replaced the Vulkan bindless assumption with a working fixed-slot WebGPU
  renderer.
- Verified the current WebXR/WebGPU application sequence against the editor's
  draft: XR-compatible adapter, session feature `webgpu`, `XRGPUBinding`,
  projection layer, `layers` render state, and per-view subimages.
- Added capability reporting for default and experimental Chrome launches.

### Ongoing platform gate

Before claiming general availability, verify all of the following on the
actual target browser and headset:

- `navigator.gpu.requestAdapter({xrCompatible:true})` returns an adapter.
- `navigator.xr.isSessionSupported("immersive-vr")` succeeds.
- `XRGPUBinding` is exposed without developer-only flags.
- `requestSession(..., {requiredFeatures:["webgpu"]})` succeeds from a user
  activation.
- A projection layer can be created with the binding's preferred color format
  and installed via `updateRenderState({layers:[layer]})`.
- `getViewSubImage()` produces usable color textures for every view.

If this remains unavailable in the shipping Quest browser, keep the 2D WebGPU
build usable and label VR experimental. Do not silently substitute IWER's
throwaway WebGL layer. A WebGL XR fallback would require a real renderer
backend or a separately approved and measured copy architecture; it is not a
small compatibility shim.

### Exit criterion

M0's engineering work is complete. The release gate remains open until the
target browser exposes the direct API or the product explicitly accepts an
experimental-browser requirement.

## 4. M1 — Emscripten engine foundation

### Completed

- Emscripten CMake graph excludes native Vulkan/OpenXR/editor targets.
- SDL2 browser window path and `emscripten_set_main_loop` drive the real engine.
- Synchronous MEMFS package loading boots real UT99 content.
- Null render/audio backends allow isolated boot tests.
- Wasm-specific alignment/empty-struct bugs were fixed.
- Playwright proves boot, tick progression, map load, quit, and no late crash.

### Remaining maintenance

- Keep a no-render M1 smoke test so later graphics/session failures can be
  separated from VM/filesystem failures.
- Preserve native build behavior whenever shared engine files change.
- Replace the giant development preload before public deployment in M10.

### Exit criterion

Complete and regression protected.

## 5. M2 — WebGPU renderer

### Completed

- Browser-created `GPUDevice` is passed to Emdawnwebgpu before `callMain`.
- Canvas surface, WGSL shaders, pipelines, fixed texture slots, samplers,
  uploads, depth, batching, and supported UE1 texture formats are implemented.
- World geometry and 2D tile orientation are corrected and screenshot tested.
- Rendering diagnostics expose draw calls, textures, errors, and buffer usage.

### Known renderer gaps to retain in the backlog

- Confirm `DrawGouraudPolygon` orientation with a deterministic visible actor
  or first-person weapon; earlier native/WebGPU screenshots were inconclusive.
- Browser `ReadPixels` remains asynchronous; keep screenshots/readback in the
  JS harness unless the engine gains an async screenshot API.
- HDR, bloom, MSAA, and advanced post-processing are intentionally absent.
- Device-loss recovery after an irrecoverable `GPUDevice` loss remains M11
  work. Projection-layer format variation is implemented for `bgra8unorm`,
  `rgba8unorm`, and `rgba16float`.

### Exit criterion

Complete for the minimum XR renderer; visual parity gaps remain tracked.

## 6. M3 — WebGPU resource hot path

### Completed

- Cached bind groups use the four texture identities plus sampler modes.
- Cache entries are invalidated before dependent texture views are destroyed.
- Warm-frame creation/hit counters and true geometry-buffer rollovers are
  exported to both browser harnesses.
- `DM-Deck16][` and `CTF-Darji16` measurements showed no need to enlarge the
  existing vertex/index buffers.

### Remaining performance decision

Measure the fixed-slot model on Quest hardware before redesigning it. Only
consider texture arrays, atlases, larger batches, or buffer growth when a
headset trace demonstrates a real bottleneck.

### Exit criterion

Complete; Quest measurements move to M11.

## 7. M4 — WebXR/WebGPU presentation groundwork

### Completed

- The engine's actual adapter is requested with `xrCompatible:true`.
- Experimental Playwright launches installed Chrome with the two Chromium
  WebXR/WebGPU feature flags.
- `WebGPU.importJsTexture()` imports a browser-created `GPUTexture` into C++.
- C++ GPU output is read back through the original JavaScript texture.
- A complete 95-draw UT frame renders into layer 1 of a browser-owned
  two-layer `bgra8unorm` texture with zero uncaptured WebGPU errors.
- The renderer can bypass the canvas for an external target, select an array
  layer, resize depth state, submit, release the imported wrapper, and restore
  canvas rendering.
- Emscripten's window RAF can pause, advance exactly one engine frame per
  explicit XR-driven call, and resume.
- IWER covers session request, two-view lifecycle, frame progression, and
  teardown. It cannot construct a native Blink `XRSession`, so it cannot test
  `XRGPUBinding` itself.

### Exit criterion

Complete as groundwork. It proves the renderer and scheduling seams, not
headset presentation.

## 8. M5 — split simulation from stereo view rendering

Completed on 2026-07-22 for the engine/render seam and deterministic two-layer
browser diagnostic. The seam uses fake diagnostic eye transforms until M6/M7
provide native `XRView` data.

### 8.1 Frame responsibilities

Refactor `Engine::RunOneFrame()` into explicit phases without changing native
behavior:

1. `AdvanceGameFrame()` — elapsed time, input, UnrealScript/level tick,
   `PlayerCalcView`, and audio.
2. `RenderGameFrame(float)` — render the already-advanced state.
3. `FinishGameFrame(float)` — save/travel operations that happen once after
   rendering.

The native/window loop calls all phases once. The XR loop advances once and
renders both eyes from the same immutable camera/game state. Never call the
whole simulation loop once per eye.

Implemented classification in `RenderSubsystem`: view-independent BSP actor
updates and light/texture counters run once before the two views; scene
geometry is submitted per eye. The M5 diagnostic deliberately defers HUD,
menus, overlays, flash and `PreRender`/`PostRender` until M9 defines their VR
presentation. Continue auditing the following as real view data is integrated:

- BSP actor `UpdateBspInfo`, light/texture frame counters, canvas reset,
  `PreRender`, `PostRender`, flash, and overlays must be classified as once per
  simulation frame, once per view, or once per submitted target.
- Portal, mirror, sky, corona, fog, mesh, decal, and weapon rendering must
  inherit the eye's viewport and projection override.
- UI and flash effects must not be applied twice to game state.

### 8.2 Renderer target lifecycle

The one-shot external-target diagnostic now has a frame-scoped API:

- `BeginExternalRenderTargetFrame(texture, width, height, layer)`
  imports/retains one
  browser texture wrapper.
- `SelectExternalRenderTargetView(arrayLayer, viewport)` creates/selects the
  2D view. The current diagnostic uses a full-attachment depth buffer; eye-
  sized/subimage depth policy remains part of production integration.
- Draw one eye, end the pass, then select the next array layer.
- `EndExternalRenderTargetFrame()` releases every imported
  wrapper before the synchronous JS call returns.
- Keep the canvas `Lock`/`Unlock` path behaviorally unchanged.

Initially use an engine-owned depth texture per eye and create the projection
layer without compositor depth. Browser-owned depth can be added only after
color presentation is correct.

### 8.3 Per-frame bridge data

The versioned POD structure shared by JS and C++ was introduced as M6 ABI v1
and is now M8 ABI v2:

- frame timestamp and view count;
- reference-space/reset generation;
- per view: eye/index, position, orientation, 4x4 projection matrix, viewport,
  texture base-array-layer, color dimensions;
- per controller: stable source ID, handedness/validity flags, button masks,
  analog axes/values, and grip/aim poses.

The current packed layout is a 44-byte header, 116 bytes per view, and 128
bytes per input source (532 bytes for stereo plus two controllers). C++
`static_assert`s every size/critical offset; JS performs 44 deterministic
offset/value checks and Playwright compares all native-reported strides before
using the bridge.

Write it into WASM memory in one operation per XR frame. Avoid dozens of
`ccall`s and avoid retaining JavaScript XR objects beyond the callback. Keep
the current color texture in a JS global only for the duration of the one
synchronous import/render call, clear it in `finally`, and assert that C++ has
released its wrapper.

Implemented: the JS helper copies pose, orientation, projection, viewport and
`getViewDescriptor().baseArrayLayer`, requires both views to share one
`colorTexture`, performs one synchronous `ccall`, and clears the temporary
texture global in `finally`. Native code copies and validates the packed data
before rendering; packed structs never leak into aligned renderer state.

### 8.4 Tests

- Browser automation proves one synchronous stereo call increments the
  simulation counter once and renders both eye layers.
- Both layers are read back and required to contain nonblank, varied, distinct
  UT scene output from two diagnostic projections.
- Canvas RAF pause/manual-frame/resume is verified around the stereo call.
- Normal WebGPU, default IWER, experimental interop, and native Debug build
  tests pass.

### Exit criterion

Met on 2026-07-22: one simulation state rendered into two independently
selected array layers in one synchronous call with different diagnostic
projections, exactly one tick, 187 accumulated draw calls, distinct readback,
canvas recovery, and zero uncaptured WebGPU errors. Real `XRView` matrices and
subimages remain the M6/M7 production path, not an M5 claim.

## 9. M6 — real native WebGPU WebXR session and presentation

### Completed bridge slice (2026-07-22)

- Added ABI version/header/view/max-view introspection, native packet
  validation and last-error diagnostics.
- Added a synchronous `Surreal_RenderWebXRFrame` entry that imports the current
  browser texture once, selects each supplied array layer and viewport, uses
  each supplied WebGPU `[0,1]` projection matrix, advances simulation once,
  finishes once and releases the imported wrapper before returning.
- Added production-facing JS helpers around `getViewerPose()`,
  `getViewSubImage()` and `getViewDescriptor()` without retaining XR objects.
- Experimental Playwright passed the complete packed call: exactly one tick,
  error code 0, 187 draw calls, two nonblank/varied layers, 1,178 differing
  sampled pixels, canvas RAF recovery and zero uncaptured WebGPU errors.
- Native Windows Debug and Emscripten release builds pass.

### Completed production-session scaffold (2026-07-22)

- Added an explicitly selected `?native-webgpu-xr=1` route. The default page
  continues to use the IWER-compatible WebGL lifecycle harness and cannot
  accidentally claim native WebGPU presentation.
- The Enter VR gesture now requests `immersive-vr` with required `webgpu`,
  constructs `XRGPUBinding`, creates and installs a color-only projection
  layer, and requests `local-floor` with a `local` fallback.
- Canvas RAF ownership transfers only after the session, binding, layer, render
  state and reference space are ready. Each native XR RAF schedules its
  successor first and synchronously packs/renders the frame before returning.
- Session generations make stale callbacks inert. Setup rejection, render
  failure, the session `end` event and explicit exit all clear native state and
  restore the normal Emscripten canvas loop.
- The native bridge error code distinguishes a legitimate no-pose skip from a
  renderer rejection; the latter ends the session instead of silently losing
  frames.
- Playwright now invokes the production entry point under experimental IWER,
  observes the expected Blink type-check rejection, verifies error diagnostics
  and RAF recovery, then starts and ends a fresh default IWER session. Both the
  default and experimental suites pass.

M6 is complete at the implementation and automated-test boundary. IWER's
JavaScript session still cannot satisfy Blink's native `XRSession` type check,
so projection-layer creation, the actual XR callback/subimage route and
compositor presentation must be validated on a supported real runtime/headset.
The code now negotiates the binding's preferred color format, accepts the
three formats required by the current draft (`bgra8unorm`, `rgba8unorm`, and
`rgba16float`), and lazily caches matching pipeline families. Generation-safe
cleanup, WebXR/DOM visibility policy, post-acquisition setup failure, repeated
entry/exit, page shutdown, and device loss are automated. The remaining M6
exit work is physical compositor validation and the five-minute headset gate.

### 9.1 Session creation

The production path is separate from the IWER lifecycle path. Current status:

1. **Implemented:** require a user gesture from the Enter VR button.
2. **Implemented:** request `immersive-vr` with
   `requiredFeatures:["webgpu"]` and optional `local-floor`. Add
   `bounded-floor` and later input/UI features only when their milestone needs
   them.
3. **Implemented, headset-unvalidated:** construct
   `XRGPUBinding(session, engineDevice)`.
4. **Implemented, headset-unvalidated:** query
   `XRGPUBinding.getPreferredColorFormat()` and reject formats outside the
   draft's supported set.
5. **Implemented:** select the imported texture's actual format and lazily
   create a matching pipeline family. Deterministic browser tests render
   `bgra8unorm`, `rgba8unorm`, and `rgba16float` with zero GPU errors.
6. **Implemented, headset-unvalidated:** create a color-only projection layer
   at `scaleFactor:1.0` using the binding's preferred format.
7. **Implemented, headset-unvalidated:** install it with
   `session.updateRenderState({layers:[projectionLayer]})`.
8. **Implemented:** acquire `local-floor` when available, otherwise `local`,
   and expose the chosen mode in diagnostics.

### 9.2 XR animation frame

For each native XR callback:

- **Implemented:** schedule the next callback first.
- **Implemented:** return without simulation/render when no viewer pose is
  available, while counting the skipped frame.
- **Implemented, headset-unvalidated:** for each `XRView`, call
  `getViewSubImage`, use
  `XRGPUSubImage.getViewDescriptor()` and its viewport, and collect the exact
  browser-provided projection matrix.
- **Implemented, headset-unvalidated:** import the color texture once even if
  both views share the same array.
- **Implemented, headset-unvalidated:** run the M5 synchronous engine frame
  and submit before returning.
- **Partial:** record frame count, skipped frames, last render result,
  reference-space choice, configured format, lifecycle phase and errors.
  Still record CPU/GPU frame time, GPU errors, view count, dimensions and layer
  indices in a compact diagnostic overlay/log.

### 9.3 Lifecycle

- **Implemented:** on successful session start, transfer RAF ownership only
  after the layer and reference space are ready.
- **Implemented:** on `end`, setup/render exception, explicit exit, or device
  loss, invalidate XR RAF, clear state and restore window RAF/canvas when the
  device remains usable. Imported handles are frame-scoped and synchronously
  released; a missing pose skips one frame without ending the session.
- **Implemented:** handle WebXR `visibilitychange` (`visible`,
  `visible-blurred`, `hidden`) separately from DOM visibility, with an explicit
  Web Audio suspend/resume policy.
- **Partial:** count reference-space `reset` generations and reset native pose
  state. Repeated entry/exit, setup failure after session acquisition, browser
  back/escape through session end, and page shutdown are covered. Input-source
  changes and full mouse/keyboard restoration move with M8.
- **Implemented:** prevent simultaneous enter requests and stale callbacks
  from an old session using a session generation/token.

### 9.4 Validation boundary

IWER remains the lifecycle test. Investigate Chromium's WebXR Test API for a
real native-session automation path, but do not make CI depend on private Mojo
layout-test bindings. Projection creation and compositor presentation require
a real supported browser/runtime if no public native mock is available.

### Exit criterion

Implementation is complete at the automation boundary. On a real
headset/browser, the user enters VR, sees distinct content in both
eyes, receives continuous frames for five minutes, exits cleanly to the canvas,
and can repeat the cycle three times with no device loss, leaked wrapper,
uncaptured error, crash, or double-speed simulation.

## 10. M7 — head tracking, per-eye camera, and world scale

The deterministic M7 implementation landed on 2026-07-22. The packed pose is
validated (including nonzero finite quaternions), converted once at the engine
boundary, and composed after `PlayerCalcView`. Physical headset validation is
still required before declaring scale, comfort, and all scene rendering
correct.

### 10.1 Coordinate conversion

Document and test the conversion from WebXR's meters/right-handed convention
(`+X` right, `+Y` up, `-Z` forward) to UE1/SurrealEngine coordinates. Do not
reuse the debug-stereo IPD constant or assume its unit comment is calibrated.

- **Implemented:** configurable `worldUnitsPerMeter`, defaulting to 39.3701
  UU/m from UE1's approximate one-inch unit. Validate that default against
  known UT geometry and player eye height in-headset before freezing it.
- **Implemented:** explicit packed matrix storage and engine-boundary
  conversion from WebXR right-handed axes to UE1 left-handed axes.
- **Implemented:** preserve the runtime's exact asymmetric WebGPU `[0,1]`
  projection and reflect view Z exactly once when required by engine convention.
- **Implemented:** use each `XRView` transform directly; runtime IPD is
  preserved and no toe-in or synthetic eye offset is introduced.

### 10.2 Body, head, and recenter model

- **Implemented:** keep UnrealScript `PlayerCalcView` as the body/base camera.
- **Implemented:** compose the tracked viewer pose relative to a viewer-center
  application recenter origin,
  rotated by body/pawn yaw and scaled into world units.
- **Implemented:** apply headset translation and pitch/roll to render cameras without directly
  mutating pawn physics or network state.
- **Implemented in commit `8cc2daf5`:** choose body-, head-, or
  dominant-hand-relative locomotion independently; unavailable or invalid
  tracking falls back to body-forward, while turn controls continue to update
  body yaw.
- **Implemented:** recenter captures current viewer-center position/yaw;
  reference-space reset generations force a fresh capture while preserving IPD.
- Decide whether seated/standing modes should optionally retain or neutralize
  tracked standing height; the current recenter neutralizes the initial height.
- Define seated and standing modes and clamp/handle implausible tracking jumps.

### 10.3 Rendering correctness

- **Implemented:** feed exact per-eye world-to-view and projection matrices through the existing
  `ViewportOverride`/`ProjectionOverride` path.
- Verify recursive portals, mirrors, sky zones, coronas, fog, decals, actors,
  particles, and first-person meshes in both eyes.
- Ensure culling uses each eye or a conservative stereo frustum; do not let one
  eye incorrectly remove geometry visible to the other.

### Tests and exit criterion

- **Passed:** deterministic matrix tests for identity, yaw, pitch, one-metre
  translation, 64 mm IPD, body yaw, recenter/reset, projection reflection,
  left/right eye ordering, and handedness.
- A known near/mid/far marker scene must show correct parallax and no vertical
  disparity, world rotation inversion, swapped eyes, or head-translation sign
  error.
- Head translation must not move the gameplay collision capsule.
- Ten-minute headset test with room movement and repeated recentering is stable
  and comfortable.

The remaining M7 exit criteria are physical: verify world scale and eye order,
the near/mid/far marker scene, collision independence, recursive scene features,
tracking jumps, and ten-minute comfort on the target headset.

## 11. M8 — controllers, locomotion, weapon interaction, and haptics

The first M8 slice landed on 2026-07-22. ABI v2 appends up to two fixed-size
controller records to the same per-frame packet as the views, so the browser
still performs one synchronous WASM handoff. Browser and native deterministic
tests cover copied live state, stable source IDs, normalization, poses,
disconnect clearing, packet validation, and exactly-once engine consumption.

### 11.1 Browser input collection

- **Implemented:** track `session.inputSources` and `inputsourceschange`; key
  sources by
  handedness plus stable per-session identity.
- **Implemented:** read aim pose from `targetRaySpace`, grip pose from
  `gripSpace`, and controller buttons/axes from the source's live `gamepad`
  each XR frame.
- **Partial:** understand and flag `xr-standard`; use its fixed touchpad/stick
  axis slots with a deterministic generic fallback. WebXR Input Profiles are
  still needed for controller-specific labels/models.
- **Implemented and tested:** copy button/axis values each frame because WebXR
  gamepad objects update live in place; edge detection cannot compare the same
  retained object.
- **Implemented:** normalize a radial 0.15 dead zone, axis ranges/signs,
  button/trigger ranges, and handedness in JS,
  then send one POD input block with the view state.

### 11.2 Engine input mapping

Provide a remappable default Quest layout:

- left stick: movement;
- right stick: snap turn by default, optional smooth turn;
- primary trigger: fire/select;
- secondary trigger or grip: alternate fire/context action;
- face buttons: jump, use, weapon next/previous, menu;
- controller pose: dominant-hand weapon/aim, off-hand future interaction;
- recenter and emergency menu/exit action without stealing the browser's
  reserved system button.

Synthesize existing `EInputKey`/axis events where semantics match, but add an
explicit VR input state for tracked poses and analog trigger values. Do not
force spatial data through integer keyboard events.

Current implementation:

- publishes a full replacement snapshot before `AdvanceGameFrame()` so the
  same simulation tick consumes the controller state;
- normalizes left/right sources into stable engine slots and retains raw
  reference-space grip/aim poses plus eight analog button values per hand;
- maps left buttons 0–5 to `Joy1`–`Joy6`, right buttons 0–5 to
  `Joy7`–`Joy12`, and thumbsticks to `JoyX/Y/U/V` with correct edge/release
  behavior through the existing remappable keybinding layer; and
- publishes an empty new generation on disconnect/session reset so no held
  button or axis can stick;
- installs runtime-only Quest defaults only for blank `Joy*` bindings, copying
  the user's existing mouse/keyboard commands where possible while preserving
  every explicit Joy binding;
- expands normalized left-stick input through `JoyX/JoyY` to UE1's expected
  full-scale 7000 movement domain; and
- defaults the right stick to a 30-degree snap turn with threshold/rearm
  hysteresis, while exposing configurable Smooth, Binding, and Disabled modes.
  Turning changes body/view yaw only before tracked head composition; it never
  writes pitch, roll, position, physics, or collision; and
- commit `8cc2daf5` adds configurable `Body`, `Head`, and `DominantHand`
  movement references. Head/hand forward is flattened into the horizontal
  body-local plane, with deterministic body-forward fallback for lost,
  non-finite, or vertical tracking; the existing 7000-scale input path remains
  authoritative;
- selects left or right `DominantHand` consistently for movement, weapon aim,
  presentation, and haptics;
- maps a configurable, otherwise-unbound normalized controller button to
  recenter on its press edge (right-stick click by default), rebuilding the
  shared eye/head/hand origin, and optionally maps another button's press/release
  edges to Escape for menu control. Existing Joy bindings win, and a same-button
  conflict disables the menu action; and
- loads movement reference, dominant hand, recenter/menu buttons, turn policy,
  and haptics from `[Engine.WebXR]`, exposes validated runtime setters, and
  writes them through the normal engine config-save path; and
- safely distinguishes byte-backed `bFire`/`bAltFire`/`bDuck` properties from
  packed boolean properties, avoiding adjacent-property corruption found by
  physical-controller testing in the sibling native branch.

Native/Emscripten builds, the complete default Playwright suite, and direct
locomotion/action self-tests pass. The self-tests cover reference-axis rotation,
fallbacks, action edges, binding precedence, button parsing, and invalid
settings. The full experimental smoke reported `selfTest=1`,
`locomotionSelfTest=1`, body reference `0`, right dominant hand `2`, configured
right-stick recenter `10`, and menu disabled `0`. Its developer config already
bound Joy10, so effective recenter `0` correctly proved that VR actions do not
steal user bindings. Remaining input/locomotion work is:

- persist the written settings in browser storage and expose them through M9 UI;
- add a dedicated safe-exit UX without consuming a reserved system button;
- verify axis/button indices with real Quest input profiles and hardware; and
- tune speed, snap angle, smooth-turn rate, movement reference, dominant hand,
  recenter/menu choices, and accessibility fallbacks in-headset.

### 11.3 Weapon aiming

- Separate view/head orientation from weapon aim.
- Audit firing traces, projectile spawn transforms, weapon mesh placement,
  recoil, muzzle flashes, crosshair, and replication expectations.
- Add the smallest clean engine hook that lets VR use controller aim while
  flatscreen keeps pawn/view rotation unchanged.
- Define two-handed weapons and physical reload as post-MVP features unless
  specifically approved.

The 2026-07-22 call-path audit established this implementation sequence. Steps
1–4 are now implemented and deterministically tested; step 5 remains open:

1. Refactor M7's WebXR-to-UE1 conversion, recenter origin, scale, and body-yaw
   composition into a shared pose helper. Compose controller aim/grip poses
   before simulation input and publish world position, forward vector, and
   UE1 rotator for the dominant hand. Recenter capture occurs before
   `AdvanceGameFrame`, then world composition is refreshed after comfort yaw
   and before gameplay ticks. Raw reference-space poses never drive gameplay.
2. Add an XR-only, re-entrant RAII scope seam around `Frame::Call`. When a
   valid dominant aim pose exists for the local player's current weapon, save
   `Pawn.ViewRotation`, substitute hand aim only for classified weapon calls,
   then restore the byte-identical value on every exit path.
3. Classify more than `TraceFire` and `ProjectileFire`. UT99 special paths
   include Flak `Fire`/`AltFire`, Eightball `FireRockets.BeginState` and
   `CheckTarget`, Translocator `ThrowTarget`, Chainsaw `Slash`, and Impact
   Hammer `TraceAltFire`/firing `Tick`. Guided-warhead steering needs a
   separate explicit policy.
4. Add a weapon-only `RenderOverlays` pass for each XR eye. Layered stereo
   now restores the current weapon without invoking pawn/HUD/menu overlays and
   uses RAII to restore canvas, device-node, and weapon transforms. The full
   HUD/menu overlay and controller-relative viewmodel alignment remain M9.
5. Treat a controller-origin firing ray as a later verified hook. Stock
   weapons add different `FireOffset` terms, so a generic `CalcDrawOffset`
   override can double-apply offsets. Use per-path evidence before changing
   origin; direction-only controller aim is the safe first cut.

Commit `b5917f10` completes the deterministic full-basis/presentation slice
without changing that conservative origin policy. The frame bridge now carries
orthonormal forward/right/up axes for grip and aim through WebXR-to-UE1
handedness, recenter, scale, and body-yaw composition. Gameplay ballistics keep
the existing zero-roll `WorldRotation`; a separate
`WorldPresentationRotation` round-trips the full basis, including controller
roll and the pitch singularity, for viewmodel presentation.

The VM classifier now recognizes the current weapon's exact global
`RenderOverlays` call across Engine, Botpack, inherited, and mod weapon
packages. For that presentation scope only, it temporarily assigns the
dominant aim basis to both pawn and weapon rotation, falls back to tracked grip
orientation if aim is absent, and restores the byte-exact integer rotations in
re-entrant LIFO order. The existing once-per-eye weapon-only renderer remains
the presentation dispatcher; this does not invoke player/HUD/menu overlays and
does not add a second weapon call. Ballistic and target-acquisition scopes
retain their prior direction-only behavior.

Native and Emscripten builds pass, and deterministic controller-pose/weapon
self-tests cover nonzero roll, orthogonality, singular-basis round-trip,
package-independent presentation classification, nesting, and exact restore.
The browser diagnostic surface exposes all 18 pose values and the presentation
scope counter. This is automation evidence only: real barrel alignment,
handedness/mirroring, weapon scale/position, muzzle origin, special/automatic
weapon fixtures, tracking loss, and physical Quest presentation remain open.

Offline/standalone is the M8 target. Stock UT networking sends body/view
rotation and cannot replicate independent hand aim without a protocol or
replicated-state extension. M10 now explicitly declares browser multiplayer
out of scope for the MVP; M8 must not pretend restored local view rotation is
remotely authoritative. See `WEBXR_NETWORKING_SCOPE.md` for the source audit
and requirements that must be met before that decision can be reopened.

### 11.4 Haptics and optional hands

- Use available gamepad haptic actuators for fire, pickup, damage, and UI
  confirmation with rate limiting and a disable option.
- Articulated hand tracking is optional after controller MVP. Request it only
  as an optional feature and keep controller/gamepad fallback complete.

Implemented browser foundation (2026-07-22): per-hand scalar pulse queues,
`pulse()`/`playEffect()` actuator support, intensity/duration clamps, 50 ms
rate limiting and coalescing, a disable switch, generation-safe dispatch, and
clean drops for hidden/inactive sessions, source loss, unsupported actuators,
and stale generations. The deterministic fake-actuator policy test passes all
38 checks in both default and experimental Playwright runs.

Implemented engine bridge (2026-07-22): a scalar-only native/Emscripten API
calls the browser queue synchronously without retaining strings, XR sources, or
actuators. Classified real firing calls request a dominant-hand recoil pulse in
the same shared VM scope as controller aiming; nested projectile paths coalesce
under the browser rate policy. `[Engine.WebXR] HapticsEnabled` initializes the
disable setting and deterministic bridge/setter tests pass.

Commit `1737b6f9` adds outcome-based gameplay feedback instead of pulsing on
attempts. Damage pulses both hands only after `TakeDamage` produces a net health
loss. Pickup pulses the dominant hand only after `Inventory.Touch(Pawn)` causes
a confirmed ownership, inventory, health, destruction, visibility, collision,
or sleep-state transition. Re-entrant `Super` chains collapse to one decision.
Commit `e5e7c9f6` routes an accepted menu primary-trigger press through
`Console.KeyEvent` and emits the centralized UI-confirm pulse only when the
console accepts the click. Current policy is fire `0.55/35 ms`, pickup
`0.32/45 ms`, UI `0.25/30 ms`, and damage `0.35..0.80/45..110 ms`.

Commit `e21a338d` exposes the haptics enable option in the strict browser
settings profile, while `e5e7c9f6` fixes engine config saving for haptics and
all implemented turn settings. The full experimental browser smoke validates
the native outcome self-test and browser actuator policy in addition to the
existing 38 fake-actuator checks.

Still missing: per-weapon effect tuning, direct-health-mutation/custom-pickup
hooks that bypass the audited calls, rejection/latency measurements on real
Quest actuators, and physical confirmation that no stale pulse is replayed
after focus loss, disconnect, or session re-entry.

### Exit criterion

The player can start a match, walk, turn, aim independently of the head, fire
both modes, jump/use, change weapon, operate menus, recenter, and exit using
Quest controllers. Inputs remain correct after source disconnect/reconnect and
session re-entry.

## 12. M9 — HUD, menus, first-person presentation, and comfort

### 12.1 HUD and menu strategy

Do not depend on WebGPU non-projection composition layers for MVP; the binding
draft notes those layer types are still in development. Render UI inside the
projection layer:

- Convert the existing 2D canvas/HUD to a head-locked or world-locked virtual
  plane at a comfortable configurable distance.
- Render the plane separately for each eye with correct stereo depth and no
  depth test where appropriate.
- Scale text for headset readability and keep critical UI inside a conservative
  field-of-view safe area.
- Drive menu cursor/raycast from the dominant controller, with trigger select
  and stick/D-pad navigation fallback.
- Use DOM Overlay only as an optional diagnostics/import convenience when the
  target runtime reports it enabled, never as the sole in-headset menu.

First safe HUD seam, commit `e387ec3f` (2026-07-22):

- gate on a local player with `myHUD`; no HUD means zero state updates, zero
  captured commands, and zero eye presentations;
- call UT99's `PlayerPawn.PostRender` exactly once on a stable 1280x960
  logical canvas, suppress actual device submission during that call, and
  capture view-independent tiles, glyphs, clipped tiles, and 2D lines;
- replay the same immutable command list once into each active eye after the
  existing weapon-only pass, so message queues, mutators, animation, and other
  legacy script side effects do not advance twice;
- construct one common head-locked 4:3 plane at configurable distance/FOV,
  project its world corners with each eye's exact
  `Projection * WorldToView`, and clamp the resulting rectangle into a
  configurable conservative safe area; and
- restore canvas dimensions, cursor/clip state, scene node, and device state
  with exception-safe scopes. Non-XR canvas submission remains unchanged.

Diagnostics count frames, state updates, captured commands, eye
presentations, clamped viewports, and unsupported draws. A deterministic
self-test covers the one-update/two-presentation policy, asymmetric
projections, forced viewport clamping, and the absent-HUD path. Native and
Emscripten builds and diff checks pass.

The first browser attempt did not reach HUD presentation because it stopped at
the engine/Web Audio boot gate; that run was neither a HUD pass nor failure. A
later Playwright run exposed a real WebGPU bug: replaying after both eyes by
re-selecting each completed array layer opened a clearing pass and erased its
scene beneath the HUD. Commit `46b5149f` keeps capture once before the eye loop,
but replays each immutable eye HUD while that eye's scene/weapon pass is already
active. No completed layer is reselected.

The repaired full experimental smoke passed packed stereo, input, pose, format,
lifecycle, and device-loss gates with zero WebGPU errors. It rendered 189 draws;
left `nonBlack=300434`, `distinct=865`; right `nonBlack=300606`,
`distinct=903`; and `differentSamples=1175`. HUD diagnostics were
`hudSelfTest=1`, `expectedHudEyes=2`, `hudStateUpdates=1`,
`hudEyePresentations=2`, `hudCapturedCommands=65`, `hudUnsupportedDraws=0`, and
`hudClampedViewports=0`. This is desktop/IWER automation, not a physical Quest
stereo-fusion or readability result.

The exact combined presentation/audio line was:

```text
[harness] M8/M9/M10 presentation and audio diagnostics: {'aimSelfTest': 1, 'hapticsSelfTest': 1, 'disableAccepted': 1, 'enableAccepted': 1, 'expectedWeaponEyes': 2, 'weaponEyePasses': 2, 'weaponCalls': 0, 'hudSelfTest': 1, 'expectedHudEyes': 2, 'hudStateUpdates': 1, 'hudEyePresentations': 2, 'hudCapturedCommands': 65, 'hudUnsupportedDraws': 0, 'hudClampedViewports': 0, 'audioListenerActive': 1, 'audioListenerUpdates': 1, 'audioVelocityResets': 1, 'audioVelocity': [0, 0, 0]}
```

`weaponCalls=0` means only that the sampled frame lacked a current weapon
fixture; the expected two weapon-eye dispatches still completed.

Commit `49c60be1` exposes strict `[Engine.WebXR]` configuration and matching
Emscripten setters/getters for HUD enable, distance, horizontal FOV, aspect, and
safe area. Defaults are `True`, `68.8976` UU, `50` degrees, `4/3`, and `0.90`.
Finite accepted ranges are `19.685..157.4804` UU, `20..75` degrees,
`0.75..2.0` aspect, and `0.50..1.0` safe area. Invalid setters reject without
mutating state. The normal config-save path writes all five values, while
separate configured/effective diagnostics prove that renderer state tracks the
engine setting.

Disabling the HUD skips `PlayerPawn.PostRender`, clears stale captured
commands, and performs zero HUD state updates or eye presentations without
hiding the world or weapon passes. Self-test mask `31` adds this disabled-state
contract to the existing lifecycle/projection/clamp/absent-HUD checks; a range
self-test covers endpoints, non-finite values, and out-of-range rejection. The
full experimental smoke rerun passed with configured/effective enabled `1`,
distance `68.8975983`, FOV `50`, aspect `1.3333330`, and safe area `0.89999998`.
Its exact disabled lifecycle result was:

```text
[harness] M9 disabled-HUD lifecycle: {'original': 1, 'disabled': 1, 'invalidDistance': 0, 'effective': 0, 'expectedEyes': 0, 'stateUpdates': 0, 'eyePresentations': 0, 'weaponEyes': 2, 'rendered': 1, 'restored': 1, 'restoredEffective': 1}
```

Commit `2ca7effb` extends the same capture-once contract through
`Console.PostRender`, after `PlayerPawn.PostRender`, matching the desktop
ordering. Console text, loading messages, and tile/text/2D-line based UT99
menu output now join one immutable command stream and are still replayed only
inside each currently active eye pass. Separate renderer diagnostics count the
player and console calls, which are each bounded to at most one per XR frame.

The same commit fixes full-screen menus that set `Console.bNoDrawWorld`.
WebXR now executes an explicit UI-only eye path instead of skipping the whole
frame: world and weapon passes remain zero, while the captured UI is presented
to every active view. The deterministic HUD self-test now also requires this
`0` weapon / `2` UI-eye plan. The native Debug build passes; Emscripten and
browser results are recorded after the integration build below.

This slice still excludes stick/D-pad navigation input, `PreRender`, full
player `RenderOverlays`, timedemo/debug overlays, and DOM Overlay.
`Canvas.DrawActor`, `DrawClippedActor`, and 3D-line calls cannot yet be recorded
as view-independent 2D commands; capture suppresses and counts them instead of
leaking them into whichever eye was selected. Headset work still owns
distance/FOV/scale/readability, stereo fusion, safe-area tuning, occlusion
policy, complete menu operability, and the 30-minute comfort gate.

Commit `64c519fe` implements the geometry half of controller menu input. It
factors the head-locked plane basis shared by stereo presentation, intersects
the dominant controller's world-composed aim ray against the finite plane, and
writes a successful in-bounds hit to UT's existing absolute
`WindowsMouseX/Y` cursor state on the same frame before console capture.
Parallel, behind-plane, untracked, disconnected, and outside-plane rays leave
the cursor untouched. A deterministic center-hit/parallel-rejection check is
part of the existing HUD self-test mask, and native Debug builds pass.

Commit `e5e7c9f6` completes the click half with a menu-gated trigger route. A
dominant primary press becomes UT's `LeftMouse` only when the previous captured
frame has a valid controller-plane hit and the console reports both a visible
and available cursor. This bypasses gameplay binding fallback while the menu is
eligible, so a rejected click cannot fire the weapon behind it. Release is
forced on menu loss or a dominant-hand switch, and an accepted press emits the
UI-confirm haptic. Outside that exact state, the existing gameplay trigger/fire
route is unchanged. Pure deterministic routing/release tests are included in
the locomotion self-test.

Commit `e21a338d` adds a versioned, strict browser settings panel for turn mode,
movement reference, dominant hand, recenter/menu actions, snap/smooth tuning,
haptics, and all five HUD-plane controls. It loads after engine initialization,
applies settings transactionally with native rollback on rejection, and stores
only a complete schema-v1 profile in `localStorage`. Eight settings tests pass;
the full browser smoke also validates the ready status and native defaults.

Stick/D-pad focus navigation, pointer-loss visuals, cursor hotspot calibration,
unsupported actor draws, and physical Quest ray/selection validation remain
open.

### 12.2 First-person weapon

- **Partial:** preserve full dominant aim/grip orientation, including roll,
  during each eye's stock weapon `RenderOverlays`; position still uses stock
  overlay placement and needs controller-relative offsets/calibration.
- Place weapon meshes from the dominant grip/aim pose with configurable offsets.
- Correct clipping, handedness, animation origin, muzzle flash, lighting, and
  near plane.
- Offer dominant-hand selection and a head-aim fallback for accessibility.

### 12.3 Comfort options

- Snap turn default with configurable angle and debounce.
- Optional smooth turn with speed setting.
- Head-relative or hand-relative smooth locomotion.
- Optional movement/turn vignette if feasible in the WebGPU pipeline.
- Seated/standing mode, height calibration, recenter, weapon-hand choice, and
  comfort presets. HUD distance/FOV/aspect/safe-area configuration and browser
  persistence exist, but still need headset tuning and higher-level presets.
- No camera shake, forced roll, artificial head bob, or cutscene camera motion
  in the comfort preset; audit existing UE1 effects and gate them in VR.
- Define pause/map-transition/loading presentation so the compositor never
  shows stale or violently moving frames.

### Exit criterion

All essential gameplay information and menus are legible and controller
operable in-headset; the default profile passes a 30-minute comfort session
without incorrect weapon scale, stereo UI disparity, forced camera motion, or
unrecoverable menu state.

## 13. M10 — audio, user data, networking scope, and deployment

### 13.1 Browser audio

The Emscripten build now uses the existing `AudioDevice.cpp` OpenAL backend,
linked to Emscripten OpenAL/Web Audio. It no longer uses `NullAudioDevice`.

- **Complete:** reuse the existing decoded sources, music path and spatial
  OpenAL calls through Emscripten's Web Audio implementation.
- **Complete:** expose AudioContext state/resume diagnostics and invoke resume
  directly from the trusted Enter VR/Enable Audio click.
- **Complete:** avoid unsupported `AL_METERS_PER_UNIT`, unbounded Emscripten
  source-count reporting, and context-destruction ordering hazards.
- **Complete in commits `b6b74c00` and `f4728617`:** update the listener from
  the recentered center-head pose after `PlayerCalcView`, recomposed against the
  exact camera anchor/body yaw used by the XR render frame. The low-level
  `AudioListenerPose` contract preserves position, velocity, and the full
  forward/up basis; OpenAL validates/orthonormalizes it and applies the existing
  `(x, y, -z)` coordinate reflection. A missing/invalid tracked pose retains the
  normal `CameraActor` fallback.
- **Complete in deterministic policy:** derive Doppler velocity only from
  consecutive frame generations with unchanged recenter count, finite elapsed
  time in `(0.0001, 0.25]` seconds, and displacement no greater than `157.4804`
  UE1 units. First samples, recenter, tracking loss/reacquisition, generation
  gaps, stalls, teleports, and session changes reset velocity to zero.
- **Complete in automation:** keep audio active when only the companion DOM is
  hidden, suspend while XR reports `hidden`, and resume for `visible` or
  `visible-blurred`. Confirm browser gesture and headset-runtime behavior
  physically.
- **Automated smoke evidence:** the full experimental run reported
  `audioListenerActive=1`, `audioListenerUpdates=1`,
  `audioVelocityResets=1`, and `audioVelocity=[0, 0, 0]`, the expected safe
  first tracked sample.
- Still test on physical Quest: user-gesture unlock, head-relative positional
  direction and roll, measured Doppler during continuous motion, reset behavior
  across recenter/tracking loss/focus/session re-entry, music streaming,
  positional effects, volume settings, map changes, underruns, and shutdown.

### 13.2 Legal game-data import and persistence

Replace the 629 MB `--preload-file` development artifact:

- **Complete build seam:** an empty `SURREAL_GAMEDATA_DIR` now emits no
  `--preload-file`, producing a redistributable engine build with no commercial
  data. The configured developer build may still preload a local data tree.

- **Implemented in commit `87f8324f`:** a first-run importer uses
  `showDirectoryPicker()` when available and a `webkitdirectory` file-input
  fallback elsewhere. Developer-preloaded builds retain a direct bypass.
- **Implemented:** canonicalize local relative paths, reject traversal,
  absolute paths and case-collisions, and validate the required `System`,
  `Maps`, `Textures`, `Sounds`, and `Music` layout, core packages, INI,
  executable, and representative asset extensions without reading contents.
  File contents are never uploaded or logged.
- **Implemented:** explicit schema `surrealengine-ut99-data` version 1, with
  OPFS preferred and IndexedDB as compatibility fallback. Imports stage under
  a new dataset ID, publish metadata only after all blobs succeed, remove
  failed staging data, and retire the prior dataset after replacement.
- **Implemented:** storage estimate/persistence diagnostics, pre-copy quota
  failure, scan/store/restore progress, actionable corruption/eviction errors,
  clear-data control, safe re-import by save-then-reload, and streamed
  materialization into `/gamedata` before `Module.callMain()` opens the boot
  gate.
- **Implemented in commit `ca262715`:** restore and flush a separate mutable
  overlay with the strict allowlist `SE-UnrealTournament.ini`, `SE-User.ini`,
  `Save/Save<N>.usa`, `Settings.json`, and `SE-Log-LastRun.txt`. Immutable
  imported commercial packages are never walked or copied by this subsystem.
  OPFS uses staged snapshots plus a current pointer; IndexedDB is an atomic
  fallback. Restore runs after imported-data materialization and before main.
- **Implemented:** explicit flush/status/clear APIs plus periodic, hidden, and
  `pagehide` checkpoints. Corrupt restore is nonfatal but pauses future flushes
  until the user clears the mutable store. Clearing mutable records never
  removes imported game data. Nine mutable-persistence tests pass.
- **Implemented in commit `e21a338d`:** the browser WebXR profile persists a
  separately validated schema-v1 UI representation and applies it
  transactionally to native settings after engine initialization.

Deterministic importer validation passes 13/13 checks: explicit schema;
content-free layout validation; actionable missing-file errors; traversal and
case-collision rejection; directory-input root stripping; IndexedDB
save/restore; streamed Emscripten-FS materialization; developer-preload bypass;
no-data boot gating; saved-import boot on a later load; safe replacement
without mutating the live FS; clear; and OPFS round-trip/corruption behavior
when available.

The real no-data artifact half of the acceptance gate now passes. It was built
in a separate directory with:

```powershell
C:\Devstuff\emsdk\upstream\emscripten\emcmake.exe cmake -S . -B build-emscripten-nodata -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DSURREAL_GAMEDATA_DIR:PATH=
cmake --build build-emscripten-nodata --target SurrealEngine -j 4
```

The cache contains `SURREAL_GAMEDATA_DIR:PATH=`. The emitted link command has no
`--preload-file`, `@/gamedata`, or commercial install path; no `.data` artifact
exists; and generated JS has no `SurrealEngine.data`/package loader. The latest
integrated no-data output was `SurrealEngine.js` 583,952 bytes, SHA-256
`b3eaf7e914a7de73699e3727037d812f078272b2f74853442be624d1496559eb`, and
`SurrealEngine.wasm` 6,282,208 bytes, SHA-256
`8af3653f5d6483f42f072ce8b1608525fd558f654feb2e175cc3f103f1a9f4d5`.

A brand-new Playwright Chrome process/profile loaded that exact build with no
`.data` or `/gamedata/` request. It acquired WebGPU, initialized WASM, displayed
the importer, and resolved `{state: "waiting-for-import", backend: "opfs",
error: null}` with `surrealBooted=false`, `surrealCrashed=null`, and no request/
page errors. No real UT data was selected, so `Module.callMain()` correctly
remained gated.

The importer and mutable-store tests still use synthetic fake files. They and the no-data wait
gate do not prove a complete user-owned UT99 import, real Quest Browser folder
permissions, full-size quota/copy/thermal behavior, storage survival after
browser/OS restart or eviction, recovery from an abrupt process kill, custom
save paths/names, migration from a future schema, or clean-profile launch into
a playable map. Those remain explicit M10 acceptance gates; no commercial
content was imported, committed, embedded, or requested here.

### 13.3 Launcher and loading UX

- Replace the native modal launcher with browser UI for import, game selection,
  map/URL arguments, graphics/VR settings, diagnostics, and launch.
- **Partial:** import/clear/progress/error UX, map query arguments, a complete
  WebXR settings panel, runtime diagnostics, and the visible
  multiplayer-unsupported decision exist. A browsable map/game selector,
  richer crash report, safe-exit flow, and stable in-headset loading scene do
  not.
- Keep expensive package scanning/loading off critical XR presentation where
  possible; show a stable loading environment or leave XR during major loads.
- Add crash/error reporting that contains engine/browser diagnostics but no
  proprietary asset data.

### 13.4 Networking scope

Browser sandboxes do not provide UT99's raw UDP/TCP socket model. Make an
explicit product decision:

- MVP option: offline/single-player/local bot play only, with multiplayer
  clearly labeled unsupported.
- Full option: design and operate a WebSocket/WebTransport relay/proxy plus
  protocol adaptation, authentication, server discovery, latency testing, and
  abuse/security controls.

Do not mark multiplayer complete merely because Emscripten socket code links.
Inventory `UInternetLink`, `UTcpLink`, `UUdpLink`, master-server browsing, and
game protocol traffic before estimating the relay.

**Decision for the browser MVP (2026-07-22): offline/single-player/local-bot
play only. Browser multiplayer is explicitly unsupported.** This is a scoped
release decision, not a claim that networking is impossible and not a silent
fallback to partially working sockets.

The source audit found that the transport problem begins below WebXR and below
the browser sandbox:

- `UNetDriver` is an empty subsystem base and `USurrealNetworkDevice` only
  persists/query-dispatches configuration properties; it does not implement a
  connection, channel, bunch, package-map, actor replication, relevancy, or
  prediction layer;
- `UnrealURL` still marks fully qualified network URLs as TODO, while the map
  load/client-travel path resolves local packages and logs in a local player;
- `UTcpLink` creates a native socket, but bind, listen, open, close,
  connection-state, read, and send operations are stubs;
- `UUdpLink` can bind and send text on native platforms, but receive, binary
  read/send, and event dispatch are absent; and
- even a future browser socket shim would not carry independent head and hand
  poses. Stock body/view rotation is insufficient for remote VR weapon aim.

The full multiplayer option therefore requires two separately testable
projects before it can re-enter WebXR release scope:

1. complete and qualify SurrealEngine's native UE1 networking/replication
   model against compatible servers, including travel, package compatibility,
   master/server discovery, joins, disconnects, relevancy, prediction, and
   abuse limits; and
2. design a browser-safe WebSocket or WebTransport gateway and an explicit VR
   pose/aim protocol extension, with authentication, origin policy, relay
   operation, latency/loss tests, backwards compatibility, privacy, and
   security review.

Until both exist, the launcher and release notes must say **Multiplayer:
unsupported in the browser MVP**, must not expose a nonfunctional join flow,
and must never describe controller-local aim as replicated or authoritative.

### 13.5 Hosting/PWA

- **Implemented in commits `28070633`, `ca262715`, and `e21a338d`:** installable entry point
  `web/index_webxr.html?pwa=1&build=build-emscripten-nodata`, standalone web
  manifest/project icons, offline guidance, registration/version/update/error
  diagnostics, deployment instructions, and versioned service worker
  `2026.07.22-m10.3`. The allowlist includes the mutable-persistence and
  WebXR-settings modules without caching any user data.
- **Implemented policy:** the worker precaches exactly the launcher,
  WebXR/importer/registration scripts, manifest, offline page, and project
  icons. It lazily cache-firsts only no-data/release `SurrealEngine.js` and
  `.wasm`. All other resources are network-only; `/gamedata`, import database
  paths, `.data`, and UE1 package/map/media extensions return HTTP 451 and can
  never enter Cache Storage. Imported content stays solely in OPFS/IndexedDB.
- **Implemented safety:** PWA registration refuses the development-preload
  build, shell installation must complete before activation, activation deletes
  only obsolete `surrealengine-webxr-*` caches, HTML is network-first for
  updates, the versioned no-data runtime is cache-first, and uncached offline
  navigation receives an explicit 503 diagnostic page.
- **Complete in local automation:** `python web/smoke_test_pwa.py` passes 27/27
  Playwright checks for registration/control, allowlisting and commercial-data
  blocking, cache version/cleanup isolation, manifest/MIME, COOP/COEP/CORP,
  worker revalidation, online update, offline launch/fallback, no-data runtime
  caching, development-preload refusal, and browser error absence.
- **Implemented in commits `5add1e67` and `8b909457`:**
  `web/stage_web_release.py` copies an exact redistribution allowlist into a
  new empty directory, writes deterministic SHA-256 metadata, then invokes
  `web/audit_web_release.py` fail-closed. The auditor rejects symlinks,
  commercial extensions/paths, `.data`, local import databases, preload
  markers, malformed runtime signatures, duplicate/incomplete runtimes, and
  missing shell/MIME references. Unit coverage passes 9/9 staging and 8/8
  audit cases.
- **Complete for the current integrated artifact:** a fresh real stage scanned
  15 files and passed 83 checks with zero errors and zero warnings. It contains
  exactly the current no-data JS/Wasm pair whose hashes are recorded in
  section 13.2.
- **Still required:** serve the production artifact over real HTTPS with
  `Cross-Origin-Opener-Policy: same-origin`,
  `Cross-Origin-Embedder-Policy: require-corp`, and
  `Cross-Origin-Resource-Policy: same-origin`; validate Quest Browser
  install/update/offline/eviction behavior; bump
  `APP_VERSION` for every shell/runtime compatibility change; test rollback;
  independently review the final staged manifest and verify all third-party
  assets and dependencies have compatible licenses.

### Exit criterion

A clean browser profile can install/open the site, import a legitimate local
UT99 installation, persist it and settings across reloads, start through a user
gesture with audio, play the declared networking scope, update safely, and
remove all stored data. No commercial game file appears in hosted artifacts or
network requests.

## 14. M11 — Quest performance, robustness, compatibility, and release

### 14.1 Performance budgets

Profile on the actual standalone Quest target, not desktop Chrome only:

- Target the runtime's supported 72/80/90 Hz modes deliberately; first release
  must sustain at least 72 Hz on the acceptance map set or document a smaller
  supported content profile.
- Measure JS, WASM simulation, culling, two-eye draw encoding, queue submission,
  GPU time, texture uploads, memory, and garbage collection separately.
- Move per-frame JS/WASM exchange to a single packed block and eliminate avoidable
  allocations/import wrappers.
- Reuse view-independent BSP/actor work across eyes while preserving conservative
  visibility.
- Measure bind-group cache behavior, geometry-buffer rollovers, texture memory,
  upload spikes, and map-load peaks on headset.
- Test projection-layer `scaleFactor`, fixed foveation when supported, LOD/detail
  settings, and resolution presets. Prefer measured quality controls over hidden
  dynamic changes.
- Investigate the existing pthread + memory-growth warning; choose a bounded
  initial/maximum memory strategy if growth causes unacceptable stalls.

### 14.2 Robustness matrix

Automated browser coverage now includes three clean session generations,
post-request setup failure cleanup, independent DOM/XR visibility and audio
policy, native-route rejection recovery, page-shutdown idempotence, and active-
session `GPUDevice.destroy()` teardown with permanent re-entry rejection.
Physical-runtime coverage remains required for sleep/wake, controller loss,
compositor behavior, and long play.

Automate where possible and manually cover:

- enter/exit VR repeatedly;
- headset sleep/wake and browser tab background/foreground;
- `visible`, `visible-blurred`, and `hidden` session states;
- lost/reconnected controllers and changed handedness;
- reference-space reset/recenter;
- WebGPU validation error and device loss;
- map travel, death/respawn, menu, pause, save/load, and long play;
- permission denial, unsupported WebGPU XR, insufficient storage, missing data,
  and stale service worker;
- canvas fallback after every failed or ended XR session.

No error path may leave Emscripten RAF paused, retain a browser-owned texture,
double-submit a frame, tick twice, or require a page reload to recover unless
the GPU device itself is irrecoverably lost and the UI says so.

### 14.3 Test layers

1. Native/unit: coordinate conversions, projections, phase ordering, input edge
   detection, config migration.
2. Browser 2D Playwright: M1 boot, WebGPU screenshots, maps, counters, quit.
3. IWER Playwright: support checks, two-view pose/lifecycle, controllers where
   emulation is representative, teardown.
4. Experimental native-binding probe: Chrome flags, XR-compatible device,
   external texture/readback, frame-loop ownership.
5. Physical headset: projection creation, compositor presentation, tracking,
   input, audio, comfort, performance, sleep/wake, and long-run stability.

Keep the exact browser version, headset OS/runtime version, enabled flags,
map, frame rate, render scale, and error counters in every headset report.

### 14.4 Compatibility/release gate

The reproducible software-artifact half is implemented. The stager accepts only
an explicit source root, one no-data runtime directory, and a nonexistent or
empty destination; it never overwrites or cleans a destination. It stages the
closed shell/runtime allowlist, emits deterministic hashes, and immediately
runs the independent auditor. The current integrated stage passed 83 checks
over 15 files with no errors or warnings. This gate does not replace the
physical compatibility, performance, legal-source, or soak gates below.

Release candidates require:

- target Quest browser exposes the required WebXR/WebGPU API under the declared
  support policy;
- five representative maps plus a larger stress map are playable;
- no uncaptured WebGPU errors or leaked imported textures;
- correct stereo/pose/controller behavior and usable comfort/UI/audio;
- legal first-run import and persistent-data workflow;
- documented networking scope;
- three clean enter/exit cycles and a 60-minute headset soak;
- fallback 2D mode and actionable unsupported-browser messaging;
- reproducible build and deployment instructions with no local absolute paths
  or proprietary assets.

## 15. Consolidated remaining-work and dependency register

This register is the short answer to "what is still missing". The milestone
sections above remain authoritative for implementation detail and acceptance
tests.

| Area | Missing deliverable or decision | Dependency / evidence needed |
|---|---|---|
| M0/M6 platform | Native Quest `XRGPUBinding` session, projection layer, real subimages, compositor output, and five-minute stability | Supported Quest Browser/Chromium build, declared flag policy, physical headset |
| M7 tracking | Physical eye order, scale, parallax, recursive-scene, tracking-jump, seated/standing, collision-independence, and ten-minute comfort gates | Marker map, representative maps, headset report with browser/runtime versions |
| M8 locomotion | Dedicated safe-exit UX, controller-profile verification, and hardware tuning for implemented body/head/dominant-hand movement plus recenter/menu actions; the strict browser settings profile is implemented | Real Quest input sources and headset tuning |
| M8 weapon | Controller-relative viewmodel position/scale/offsets, verified muzzle/fire origin, dominant-hand UI, two-hand policy, guided-warhead policy, automatic/special-weapon fixtures | Full-basis/roll presentation is implemented; remaining work needs package-qualified calibration data, loaded Botpack function table, deterministic firing fixtures, and headset/barrel alignment tests |
| M8 haptics | Per-weapon tuning, hooks for direct health/custom pickup paths that bypass audited calls, and physical latency/source-loss tests; confirmed fire/damage/pickup/UI outcomes and persisted enable UI are implemented | Representative mods/weapons and real actuator hardware |
| M8 networking | **MVP decision complete:** offline/single-player/local-bot browser release; multiplayer and independent hand-aim replication are explicitly unsupported | Reopen only after a qualified native replication layer plus browser relay/protocol project; stock body/view rotation is insufficient |
| M9 UI/comfort | Physically validate capture-once console/menu, UI-only frames, dominant-hand cursor ray, and safe menu-gated trigger; add stick/D-pad navigation, pointer-loss UX, and unsupported actor draws; finish readable scale, weapon position, vignette/comfort policies, recenter, and loading/pause presentation | Navigation contract, actor-draw strategy, per-eye headset inspection, 30-minute comfort session |
| M10 audio | Physically validate the implemented tracked-head listener: gesture unlock, head-relative direction/roll, Doppler and reset policy, focus/session re-entry, music, effects, volume, map changes, underruns, and shutdown | Real Quest Browser audio lifecycle and representative maps/sounds |
| M10 data | Import a complete user-owned install into the audited no-preload artifact; verify playable clean-profile boot, large-copy quota/progress, restart/eviction/corruption and abrupt-kill recovery, custom save-path policy, and schema migration. Strict config/VR/save/log persistence is implemented | User-owned UT99 installation, clean desktop/Quest browser profiles, Quest storage/browser support matrix |
| M10 product | Extend the tested installable shell/settings UI into a full map/game/crash-diagnostics launcher; validate HTTPS/COOP/COEP deployment, Quest install/update/offline behavior, rollback, and license audit. Deterministic staging and the proprietary-content scan are implemented | Production hosting target, Quest Browser, independent manifest/license review |
| M11 performance | 72 Hz minimum target qualification, CPU/GPU/memory/GC traces, render-scale/foveation decisions, pthread memory strategy | Quest hardware, acceptance/stress map set, repeatable profiling harness |
| M11 release | Compatibility matrix, sleep/wake and failure recovery, three entry cycles, 60-minute soak, and independent final manifest/license review; reproducible staging/auditing is implemented | Release browser/runtime versions, physical test reports, clean profile/import path |

### 15.1 Read-only reuse audit of the native VR worktree

The sibling `SurrealEngine-vr-m2` worktree was audited read-only on 2026-07-22.
Provenance is critical because two materially different bodies of work coexist
there:

- **Committed history through clean checkpoint `4c504c7d`:** controller work
  `b64a995f`, `458899c7`, `5c12bf88`, `401a42bf`, `f1ad7643`, and `673b89be`,
  plus earlier headset-tested stereo/input commits `50cb3027` and `89831fae`.
  The controller handoff states that all six weapon milestones built and
  passed synthetic/log/screenshot fixtures, but none of those six had been
  tried in a headset at that checkpoint.
- **Dirty worktree after `4c504c7d`:** 15 tracked files with approximately
  2,618 additions and 124 deletions, plus untracked analysis/plans/tools. This
  includes live `--vrtune` calibration, weapon tables/markers/diagnostics,
  native-resolution changes, menu cursor work, and
  `XrCompositionLayerQuad` intro/menu experiments. It is explicitly
  uncommitted, known-broken WIP: the latest physical test still reported a
  face-locked menu and unusable controller cursor, while the launch harness
  loaded varying benchmark maps and could not prove that the intended path was
  exercised. Do not cherry-pick, copy, or cite this batch as completed code.

Isolated findings already adapted safely:

- sibling `50cb3027` exposed byte-backed input booleans; current commit
  `fc85ed63` applies the isolated type-safe `GetBool`/`SetBool` fix;
- the same sibling commit exposed broken in-place quaternion multiplication;
  current commit `08afeb32` fixes it with compile-time Hamilton-product checks;
- sibling `89831fae` supplied real-Quest evidence for 7000-unit UE1 movement,
  negative right-stick yaw sign, `ViewRotation` involvement, and normal input
  routing for fire; current M8 follows those facts; and
- committed `b64a995f`'s full pose basis informed `b5917f10`, but WebXR keeps a
  separate zero-roll ballistic rotator and roll-preserving presentation basis.

Reusable concepts that require fresh WebXR-native implementations and tests:

- **dominant hand/profile:** centralize hand-role resolution instead of
  scattering left/right indices; support dominant-hand selection, grip/aim
  fallback, package-qualified weapon metadata, left-hand visual mirroring, and
  dual-Enforcer master/slave ownership without changing ballistic ownership;
- **two-hand aim:** use a foregrip point, grab/release hysteresis, minimum hand
  baseline, blend and optional smoothing around a between-hands vector. Treat
  this as opt-in post-MVP until real-hand tracking loss and comfort are tuned;
- **calibration:** a `--vrtune`-style guarded mode can park a weapon, show grip
  and aim axes separately, require grip release before capture arms, suppress
  crouch/gameplay input, solve `gripOffset` plus `rotationTrim` against the raw
  one-hand aim basis, and emit package/class/hand/profile/world-scale metadata.
  Persist generated values instead of relying on a rotating log. The sibling
  contains no completed, headset-qualified per-weapon table; zero offset is the
  safe fallback;
- **fixtures:** port the concepts behind `--debugvrhands`, `--debugvrfire`,
  `--debugvrtwohand`, `--debugvrdualenforcer`, and `--debugvrgeometry` into
  deterministic WebXR/WASM fixtures. Use real `Touch()` semantics for stock
  pickup/pairing where required, exercise loaded Botpack functions, and assert
  transforms, call scopes, restore counts, automatic fire, and source loss;
  logs alone do not prove barrel alignment; and
- **HUD/menu:** finite-depth tangent convergence is useful design evidence.
  Commit `e387ec3f` improves on the sibling by deriving rectangles from each
  exact WebXR projection and capturing stateful HUD script once before stereo
  replay. Menus still need a single surface/cursor coordinate contract.

Unsafe symbols/approaches that must not be transplanted as-is:

- a generic `CalcDrawOffset`/`vrCalcDrawOffsetConfirmedOwnerRelative`
  replacement can combine controller placement with stock `FireOffset` twice;
  controller muzzle origin stays open until per-weapon fixtures prove it;
- `PlayerViewOffset` is hundreds of flatscreen units and is not a safe default
  controller-relative `gripOffset`; use zero, then calibrated package-qualified
  values;
- permanent writes to `Pawn.ViewRotation`, `AdjustedAim`, or weapon rotation
  couple movement/head view to aim or are overwritten by UnrealScript. Keep
  the current re-entrant fire/presentation scopes and exact restoration;
- calling `RenderOverlaysVR`/`PostRenderVR` separately per eye repeats
  `PlayerPawn.PostRender` and `console.PostRender` state changes. Do not copy
  that lifecycle; capture or render state once and present twice;
- direct `ExecCommand({"ShowMenu"})`, blind `bShowMenu=false`, and cursor math
  mixing desktop viewport, raw render-target, `Canvas.uiscale`, and menu logical
  pixels leave compiled console/pause/mouse state inconsistent. Follow the
  stock Escape/`Console.KeyEvent` path and define one surface coordinate space;
- the dirty `vrQuadActive`/`vrQuadPose*`/
  `XrCompositionLayerQuad` experiment is native OpenXR, not WebXR/WebGPU
  evidence. Its latest physical result was negative/ambiguous, and current
  WebXR MVP deliberately renders inside the projection layer; and
- do not infer success from `--autoplay --url=...` logs until the resolved map,
  benchmark/attract branch, and actual UI branch are explicit. The dirty
  sibling harness produced DM-Morbias, DM-Zeto, and DM-StalwartXL instead of a
  stable requested acceptance map.

Headset evidence boundaries:

- the native commits do provide dated Quest 3/Virtual Desktop evidence for
  session/swapchain/frame-loop, later stereo HUD/frustum correction, controller
  fire, movement/turn sign/scale, and yaw/pitch aim;
- they do **not** prove WebXR `XRGPUBinding`, Quest Browser controller indices,
  WebGPU projection textures, Web Audio, OPFS/IndexedDB, or browser lifecycle;
- full-basis viewmodel roll, two-hand feel, left-hand mirroring, dual Enforcer
  feel, calibration constants, the dirty quad/menu/cursor path, and the current
  WebXR HUD plane were not positively headset-qualified; and
- desktop, IWER, synthetic action spaces, screenshots, and logs can close
  deterministic math/lifecycle fixtures only. They cannot close compositor,
  stereo fusion, physical alignment, controller profile, audio, storage,
  thermal, comfort, sleep/wake, or long-soak rows.

The sibling contains no implementation to port for browser haptics,
tracked-head audio, legal browser data import, persistence, or deployment.
The tracked listener and legal local importer now implemented in this WebXR
worktree were developed independently; the sibling still supplies no validation
for them. Current WebXR lifecycle automation is already more relevant than its
native OpenXR state machine.

### 15.2 Targeted sibling follow-up audit (2026-07-22)

A second read-only subagent pass compared the active implementation directly
against the sibling's committed controller series and current dirty worktree.
The branches diverged at `bf8e76d5`; no sibling edit or cherry-pick was made.
The useful donor surface is narrower than the first audit suggested:

- **Direct clean-room adaptation:** the small menu-navigation state machine in
  sibling `Engine.cpp` (`IsVRScreenUIActive` and `UpdateVRMenuNavigation`) maps
  stick directions to arrow-key pulses, A to Enter, B/Menu to Escape, applies
  initial/repeat delays, and clears held gameplay movement/fire on menu entry.
  Reimplement this on top of `UpdateWebXRInput` and the existing menu/pointer
  predicates; do not copy its direct `ShowMenu` state manipulation.
- **Conceptual adaptation:** `VRWeaponGripInfo`, `GetWeaponGripInfo`, and the
  `RenderOverlays`/`CalcDrawOffset` experiments establish that controller-
  relative position needs a package-qualified grip table, zero-offset untuned
  fallback, and separate proof of visual placement and muzzle origin.
  `PlayerViewOffset` is demonstrably unsafe, and the sibling's under-500-UU
  origin heuristic is diagnostic only. Extend the active re-entrant
  `Frame::CallScopeHook` seam narrowly; do not transplant its global pre/post
  interception.
- **Direct math, new integration:** `ShortestAngleDelta`,
  `LerpRotatorShortest`, `SolveRollForUpAxis`, `WorldForegripPoint`, and
  `UpdateVRTwoHandGrip` provide useful UE1-space two-hand math after WebXR pose
  composition. Preserve grab/release hysteresis, coincident-hand rejection,
  shortest-arc blending, primary-grip roll, and fade toward one-hand aim below
  a 15 cm controller baseline.
- **Fixture concepts:** adapt the sibling's fake-hand, repeated-fire,
  scripted-foregrip, and all-weapon geometry modes into deterministic WebXR
  packets/Playwright checks before accepting weapon offsets or origins.
- **Later UT-specific work:** left-hand viewmodel mirroring needs independent
  WebGPU winding/culling proof; dual-Enforcer ownership/hand resolution follows
  only after the single-weapon position/origin contract is stable.

The recommended implementation sequence is therefore: menu arrow/Enter/Escape
navigation and suppression; safe controller-relative weapon position/table;
narrow muzzle-origin seam; deterministic weapon fixtures; two-hand state and
blend; live headset calibration/persistence; left-hand/stock-weapon validation;
then dual Enforcers. Native-only `VulkanXRSession`, OpenXR action/swapchain/
composition-layer code, permanent head/body synchronization, hard-coded Touch
paths, the dirty quad experiment, placeholder grip values, and its blanket
weapon scale remain incompatible or unverified.

No desktop/IWER test can close a row that explicitly requires native compositor,
controller, audio, storage, thermal, or comfort evidence. Those rows stay open
until a dated headset report records the exact runtime, flags, map, render
scale, frame rate, and error counters.

## 16. Cross-cutting implementation rules

- Feature-detect every unstable WebXR module. Never infer support from browser
  version alone.
- Keep JS responsible for browser/XR object lifetimes and C++ responsible for
  engine simulation/rendering. Cross the boundary with compact data and explicit
  ownership.
- Never retain `XRFrame`, `XRView`, `XRGPUSubImage`, or its frame-scoped texture
  beyond the active callback.
- Advance gameplay exactly once per displayed XR frame regardless of view count.
- Use browser-provided per-eye transforms, projection matrices, view descriptors,
  and viewports; do not manufacture stereo values when real values exist.
- Preserve the normal canvas and native build paths after every milestone.
- Record failed probes as well as successful results in
  `WEBXR_IMPLEMENTATION_PLAN.md` so future work does not repeat them.
- Do not optimize from desktop-only numbers; measure on Quest before a renderer
  redesign.
- Keep experimental flags and IWER limitations visible in logs and UI.

## 17. Immediate execution order

1. **Complete:** implement M5's simulation/render split with identical native
   and canvas behavior.
2. **Complete:** upgrade the external-target API to render two independently
   projected array layers in one synchronous frame and read back both.
3. **Complete:** add and validate the packed view-state ABI. Matrix storage is
   proven end-to-end; coordinate/unit correctness remains M7 headset work.
4. **Complete in automation, real-runtime validation pending:** implement the
   production session/layer/RAF lifecycle, preferred color formats, strict
   generation-safe cleanup, visibility/audio policy, and no IWER fallback.
5. **Complete in deterministic tests, headset validation pending:** compose
   tracked head pose after `PlayerCalcView`, preserve runtime IPD, expose world
   scale/recenter controls, and test handedness/projection conversion.
6. Run the real projection-layer and tracked-pose path on a supported physical headset/browser;
   in parallel, determine whether the public Chromium WebXR Test API can provide
   a native automated session.
7. **In progress:** ABI v2 input, remappable Quest defaults, full-scale
   body/head/dominant-hand locomotion, snap/smooth turning, configurable
   dominant hand and conflict-safe recenter/menu actions, world full-basis hand
   composition, scoped weapon direction/presentation, per-eye weapon dispatch,
   confirmed fire/damage/pickup/menu haptics, and persistent browser controls
   are complete. Next add controller-relative viewmodel position/offsets,
   two-hand policy, verified muzzle origin, loaded-weapon fixtures, per-weapon
   haptic tuning, safe-exit UX, hardware profile verification, and headset
   tuning.
8. **In progress:** essential HUD/crosshair state is captured once and replayed
   inside each active eye pass on an exact-projection finite-depth plane; the
   repaired full experimental smoke proves one state update/two eye
   presentations without layer clears. Enable/distance/FOV/aspect/safe-area
   settings, persistent browser UI, a shared-plane controller cursor, safe
   menu-gated trigger selection, and a zero-work disabled lifecycle also pass
   automation. Next add stick/D-pad navigation, pointer-loss UX, unsupported
   actor-draw handling, recenter UX, comfort/vignette, loading/pause, and
   physical tuning/readability/fusion tests.
9. **In progress:** the local schema-v1 OPFS/IndexedDB importer passes 13/13
   synthetic tests, and a real no-preload artifact in a clean profile reaches
   the OPFS `waiting-for-import` state without calling main or requesting game
   data. The tracked-head listener and velocity-reset policy also pass desktop
   automation. Next import a complete user-owned installation in clean desktop
   and Quest profiles, prove playable boot and full-size persistence/recovery,
   physically qualify audio, and validate the implemented allowlisted mutable
   store against restart, eviction, corruption, and abrupt termination. The
   no-data PWA passes 27/27 checks; settings pass 8/8, mutable persistence 9/9,
   release staging 9/9, and auditing 8/8. A real 15-file stage passed 83 checks
   with zero errors/warnings. Next validate production HTTPS and Quest
   install/update/offline paths, finish the map/game/crash launcher, and perform
   the independent license/manifest review.
10. Finish M6/M7 headset gates and M11 profiling/soak/compatibility gates from
    physical Quest traces, then qualify a release.

## 18. Authoritative browser references

- WebXR/WebGPU Binding Module:
  <https://immersive-web.github.io/webxr-webgpu-binding/>
- WebXR Device API:
  <https://www.w3.org/TR/webxr/>
- WebXR Layers API:
  <https://immersive-web.github.io/layers/>
- WebXR Gamepads Module:
  <https://immersive-web.github.io/webxr-gamepads-module/>
- WebXR Input Profiles:
  <https://immersive-web.github.io/webxr-input-profiles/>
- WebXR Hand Input Module:
  <https://immersive-web.github.io/webxr-hand-input/>
- WebXR DOM Overlays Module:
  <https://immersive-web.github.io/dom-overlays/>

These are evolving specifications. Recheck them at the start of M6, M8, and
release qualification rather than treating this 2026-07-22 snapshot as frozen.
