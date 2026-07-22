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
| M8 — controller input/gameplay | In progress | ABI v2 input, Quest defaults, locomotion/turning, world-composed hands, scoped controller-direction firing, per-eye weapon overlays, and fire haptics are implemented; controller-relative viewmodel/origin, non-fire feedback, menu/recenter actions, and headset validation remain |
| M9 — UI/comfort/VR presentation | Not started | HUD, menus, weapon model, recenter and comfort controls |
| M10 — audio/data/network/deploy | In progress | Real OpenAL/Web Audio output, gesture/lifecycle policy, and redistributable no-data builds work; importer, persistence, deployment, networking scope, and head-pose listener remain |
| M11 — performance/robustness/release | In progress | Automated session-generation, visibility, setup-failure, shutdown, and device-loss coverage exists; Quest profiling, headset lifecycle, compatibility, and release gates remain |

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
- Decide locomotion orientation independently: head-relative and hand-relative
  options, with body yaw updated by turn controls.
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
- safely distinguishes byte-backed `bFire`/`bAltFire`/`bDuck` properties from
  packed boolean properties, avoiding adjacent-property corruption found by
  physical-controller testing in the sibling native branch.

Native/Emscripten builds, the complete default Playwright suite, and a direct
locomotion self-test pass. Remaining input/locomotion work is:

- add head-relative and hand-relative movement choices instead of only the
  current body-yaw-relative UE1 movement convention;
- persist settings through M10 and expose them through M9 UI;
- bind recenter, pause/menu, and safe exit without consuming a reserved system
  button;
- verify axis/button indices with real Quest input profiles and hardware; and
- tune speed, snap angle, smooth-turn rate, dominant hand, and accessibility
  fallbacks in-headset.

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

Offline/standalone is the M8 target. Stock UT networking sends body/view
rotation and cannot replicate independent hand aim without a protocol or
replicated-state extension. M10 must either declare browser multiplayer out of
scope or add that transport; M8 must not pretend restored local view rotation
is remotely authoritative.

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

Still missing: confirmed-health-loss damage feedback, successful-pickup
feedback, UI confirmation, user-setting persistence/UI, per-weapon effect
tuning, rejection/latency observation on real Quest actuators, and physical
confirmation that no stale pulse is replayed after focus loss, disconnect, or
session re-entry.

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

### 12.2 First-person weapon

- Place weapon meshes from the dominant grip/aim pose with configurable offsets.
- Correct clipping, handedness, animation origin, muzzle flash, lighting, and
  near plane.
- Offer dominant-hand selection and a head-aim fallback for accessibility.

### 12.3 Comfort options

- Snap turn default with configurable angle and debounce.
- Optional smooth turn with speed setting.
- Head-relative or hand-relative smooth locomotion.
- Optional movement/turn vignette if feasible in the WebGPU pipeline.
- Seated/standing mode, height calibration, recenter, weapon-hand choice,
  HUD distance/scale, and comfort presets.
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
- Update the listener from the tracked head pose, not only pawn/body rotation.
- **Complete in automation:** keep audio active when only the companion DOM is
  hidden, suspend while XR reports `hidden`, and resume for `visible` or
  `visible-blurred`. Confirm browser gesture and headset-runtime behavior
  physically.
- Test music streaming, positional effects, volume settings, map changes,
  session re-entry, underruns, and shutdown.

### 13.2 Legal game-data import and persistence

Replace the 629 MB `--preload-file` development artifact:

- **Complete build seam:** an empty `SURREAL_GAMEDATA_DIR` now emits no
  `--preload-file`, producing a redistributable engine build with no commercial
  data. The configured developer build may still preload a local data tree.

- Build a first-run importer using the File System Access API where supported
  and directory/file input fallback elsewhere.
- Validate required UT99 directories/packages, show progress and actionable
  missing-file errors, and never transmit file contents.
- Store imported data in OPFS/IndexedDB/IDBFS with an explicit schema/version.
- Request persistent storage where available; report quota before copying and
  handle eviction/corruption gracefully.
- Persist configs, key bindings, VR settings, saves, and logs; flush at safe
  checkpoints and session/page shutdown.
- Provide clear-data and re-import controls.

### 13.3 Launcher and loading UX

- Replace the native modal launcher with browser UI for import, game selection,
  map/URL arguments, graphics/VR settings, diagnostics, and launch.
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

### 13.5 Hosting/PWA

- Serve over HTTPS with `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp` for pthread/shared memory.
- Add correct MIME types, immutable hashes for application artifacts, and a
  service worker that caches only redistributable app code/assets.
- Add a web manifest, icons, install/update flow, version display, and safe
  rollback when WASM and JS bridge versions differ.
- Verify all third-party assets and dependencies have compatible licenses.

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
| M8 locomotion | Head-/hand-relative movement, settings persistence/UI, recenter/menu/exit mapping, hardware tuning | M9 settings UI, M10 persistence, real Quest input sources |
| M8 weapon | Controller-relative full-basis/roll viewmodel, verified controller origin, guided-warhead policy, automatic/special-weapon fixtures | Loaded Botpack function table, deterministic firing fixtures, headset/barrel alignment tests |
| M8 haptics | Damage/pickup/UI events, per-weapon tuning, persisted settings UI, physical latency/source-loss tests | Gameplay outcome hooks and real actuator hardware |
| M8 networking | Independent hand-aim replication or an explicit offline-only product decision | M10 networking scope; stock `ServerMove` is insufficient |
| M9 UI/comfort | Stereo HUD/menu plane, controller cursor, readable scale, weapon placement, vignette/comfort policies, loading/pause presentation | UX choices, per-eye overlay work, headset comfort sessions |
| M10 audio | Tracked-head listener and physical gesture/focus/music/map-change tests | Shared world pose and real browser audio lifecycle |
| M10 data | Legal first-run importer, validation, quota/progress, OPFS/IndexedDB schema, persistence migration, clear/re-import | User-owned UT99 installation; storage/browser support matrix |
| M10 product | Browser launcher, diagnostics, networking declaration/relay design, HTTPS/COOP/COEP hosting, PWA/update/rollback, license audit | Hosting target and explicit multiplayer decision |
| M11 performance | 72 Hz minimum target qualification, CPU/GPU/memory/GC traces, render-scale/foveation decisions, pthread memory strategy | Quest hardware, acceptance/stress map set, repeatable profiling harness |
| M11 release | Compatibility matrix, sleep/wake and failure recovery, three entry cycles, 60-minute soak, reproducible artifact audit | Release browser/runtime versions, physical test reports, clean profile/import path |

### 15.1 Read-only reuse audit of the native VR worktree

The sibling `SurrealEngine-vr-m2` worktree was audited on 2026-07-22. Its
committed HEAD was `4c504c7d`, but it also contained roughly 2,580 uncommitted
lines of screen-quad/tuning experiments. Do not cherry-pick or copy that dirty
batch wholesale.

Adapted immediately:

- sibling `50cb3027` exposed and fixed byte-backed input booleans. Current
  commit `fc85ed63` applies the isolated, type-safe read/write fix;
- the same sibling commit exposed a broken in-place quaternion Hamilton
  product. Current commit `08afeb32` fixes it with compile-time identity,
  noncommutativity, assignment, and associativity checks; and
- sibling `89831fae` supplied real-Quest evidence for the 7000 UE1 movement
  scale, negative right-stick yaw sign, `ViewRotation` involvement, and routing
  fire through normal input events. Current M8 input already follows those
  facts.

Reuse as design evidence, not direct code:

- `b64a995f`'s full controller basis/roll is the next viewmodel-orientation
  reference; current gameplay aim deliberately retains a zero-roll rotator;
- `458899c7`/`5c12bf88` confirm the weapon-instance `RenderOverlays` seam and
  automatic-fire routing, but the current RAII VM scope is safer on exceptions;
- the sibling's generic `CalcDrawOffset` can double-apply stock weapon
  `FireOffset`, so it remains explicitly rejected until per-weapon origin tests;
- Translocator's permanent stock `ViewRotation` write is intentionally undone
  by the VR scope; test and document that VR policy. Guided Redeemer steering
  remains a separate missing policy;
- native HUD tangent/convergence findings inform M9, but WebXR must derive its
  virtual plane from the exact runtime projection rather than native Vulkan
  viewport assumptions; and
- two-hand aiming, handedness/mirroring, dual Enforcer handling, and the dirty
  quad UI experiment were not headset-qualified. Reuse their failure notes and
  acceptance tests only.

The sibling contains no implementation to port for browser haptics,
tracked-head audio, legal browser data import, persistence, or deployment.
Current WebXR lifecycle automation is already more relevant than its native
OpenXR state machine.

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
   locomotion, snap/smooth turn seams, world hand composition, scoped weapon
   direction, per-eye weapon dispatch, and fire haptics are complete. Next add
   controller-relative viewmodel orientation, head-/hand-relative movement,
   damage/pickup/UI feedback, and recenter/menu/exit actions.
8. Implement M9 HUD/menus, weapon presentation, recenter UX, and comfort options.
9. Finish M10 importer/persistence/launcher/deploy and make the explicit
   networking product decision; wire the listener to tracked head pose.
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
