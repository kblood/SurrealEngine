# WebXR provider handoff

Date: 2026-07-24

Integration status: implemented and automated through
`integration/unified-engine` commit `078a6d2f`. Both presentation modes remain
experimental. The first physical Quest 3/Virtual Desktop/VDXR Automatic attempt
failed after consent and before confirmed presentation. A later candidate
`173bf623` test explicitly forced the WebGL bridge and entered immersive VR,
with its temporary blocking-timing QA option unchecked. Presentation and input
reached the headset, but rotational distortion and a short black world cutoff
make this a hardware-observed visual failure rather than a qualification.
The later ABI-v4 current-pose branch is native-, Emscripten-, and deterministic-
test validated, but has not yet been run through an actively connected headset;
it is not a physical qualification or release candidate.

The production browser integration now starts the game flat and exposes WebXR
as a post-launch session controller. Enter, exit, headset/system end, denial,
setup failure, and re-entry operate on the same live WASM module; the former
pre-launch reservation adapter remains only as a compatibility surface for old
harnesses. This controller integration is deterministic-test validated, not a
new physical qualification.

## Scope

The provider layers optional WebXR presentation on the flat Emscripten/WebGPU
platform. It provides:

- generation-safe immersive session enter, exit, failure cleanup, and re-entry;
- explicit transfer between the canvas requestAnimationFrame loop and the
  XRSession requestAnimationFrame loop;
- a packed, versioned JavaScript/WASM frame ABI;
- runtime asymmetric projection and tracked per-eye pose conversion into the
  shared `ViewFamily` abstraction;
- opaque presentation-target binding to WebGPU texture views;
- per-view selection of WebGPU projection texture array slices or distinct
  per-eye textures, using each `XRGPUSubImage`'s descriptor and viewport;
- `bgra8unorm`, `rgba8unorm`, and `rgba16float` projection pipelines selected
  from `XRGPUBinding.getPreferredColorFormat()`;
- provider-owned HUD, menu, loading, and cinematic capture textures, replayed
  as stable world-space quads into both projection eyes without scene depth;
- exact tracked-controller menu contact routing plus a packed laser/hit
  feedback ABI;
- XRCommon-routed, per-hand haptic transport with live browser actuator
  capability detection and lifecycle-safe rejection;
- direct `XRGPUBinding` presentation plus an automatic `XRWebGLLayer`
  compatibility mode that copies a WebGPU-rendered stereo atlas through
  WebGL 2; and
- a separate `web/index_webxr.html` harness. The existing flat
  `web/index_webgpu.html` remains unchanged and does not require WebXR.

After the flat game reaches `FlatRunning`, the packaged launcher exposes
**Automatic** and **Force WebGL compatibility bridge** in its separate VR
session panel. The trusted Enter action applies the current panel settings
through `surrealXRSetPresentationPreference("auto" | "webgl-bridge")` and calls
`surrealXRRequestSession()` synchronously before awaiting activation. The
former `surrealXRForceWebGLBridge` global is only a compatibility fallback for
older harnesses that never call the API.
A separate default-off **Temporary QA: blocking bridge timing (slower)**
checkbox is available only with forced bridge selection. The controller applies
it through `surrealXRSetBridgeBlockingTiming(boolean)` before reservation.

The low-level provider deliberately excludes game-specific policy, PWA
packaging, game-data import, and persistence. Product integration composes it
with the shared semantic XR input adapter and provider-neutral one-hand weapon
pose/runtime. Procedural controller proxies remain UI feedback rather than game
weapon models. Dominant-hand settings, comfort locomotion, additional weapon or
damage haptic profiles, muzzle calibration, and two-hand behavior remain
separate profile work.

## Projection-eye UI connector

The integration provider registers five non-zero WebGPU target slots for one
XR frame: projection/world is slot 1, cinematic is slot 2, loading is slot 3,
menu is slot 4, and startup HUD is slot 5. The WebGPU backend can switch among
registered targets inside one locked engine frame. UI replay therefore uses the
existing `XRUISurfaceEngineBinding` and existing UE1 canvas callbacks; it does
not add a second `PostRender` path. The ordinary flat WebGPU path still
registers no external targets and retains its original slot-zero render flow.

HUD, menu, loading, and cinematic textures are composited after world rendering
by opening a load pass on each projection-eye texture. The pass has no depth
attachment and visits `BuildReplayFrame()` in its existing deterministic
back-to-front order. Menu remains last and cannot be hidden behind a world or
decorative quad. This intentionally does not use `createQuadLayer`; non-
projection WebGPU composition-layer support is not mature enough to be the
required path.

The provider derives the viewer anchor from the same `ViewFamily` used to draw
the eyes. Controller target-ray poses use the same canonical-WebXR-to-engine
transform, recenter state, body yaw, and world scale. Both tracked hands are
routed as independent pointer IDs and the semantic `select` button is the UI
primary button. No locomotion, weapon, or preferred-hand decision is embedded
in this connector.

`Surreal_GetWebXRPointerFeedback` exposes the exact ray and the exact contact
returned by `XRUISurfaceEngineBinding`, including surface, UV, pixel, distance,
world hit point, and current select-button state. The projection compositor
consumes this result directly instead of repeating hit testing.

While any XR UI surface is visible, both connected hands are represented by a
small procedural pistol-like controller proxy and an eight-sided laser beam.
Left and right use cyan/blue and orange colors respectively. A hand holding
select becomes brighter and its beam becomes 1.6 times thicker; this is derived
per hand and does not select a dominant controller. The solid draw order is:

1. controller proxies and laser beams (`100`);
2. captured HUD/cinematic/loading/menu surfaces (`200` through `500`); and
3. opaque twelve-sided contact markers (`600`).

The beam begins at the exact hit-test ray origin and ends at the feedback hit
point. A contact marker is centred on that same point using the hit surface's
right/up axes. It is the only feedback primitive drawn over the menu, so the
menu remains topmost relative to controllers and beams while its active contact
is still unambiguous.

Default visual settings are intentionally centralized in `UIVisualSettings`:

| Setting | Default |
| --- | ---: |
| Controller body length | 0.14 m |
| Controller body width / height | 0.045 m / 0.04 m |
| Grip length | 0.10 m |
| Grip forward / up offset from aim origin | -0.075 m / -0.065 m |
| Laser radius | 0.0025 m |
| Selecting beam scale | 1.6x |
| Hit marker radius | 0.014 m |

These metre values are converted with the same `WorldUnitsPerMeter` used for
eyes, controller rays, and UI surface placement. The proxy intentionally uses
procedural geometry rather than a UT weapon mesh: it does not acquire game
assets, alter weapon gameplay, or add provider handles to shared contracts.
The physical forced-bridge run visibly confirmed the blue left and orange-red
right pistol-like proxies. That proves tracked input and visual composition
reached the headset, not that proxy scale, aim, or stereo registration is
correct while the projection path remains distorted.

## Dependency and commit order

Branch: `pr/webxr-provider`

Base: `pr/web-platform-foundation` at `88980d6234d0d897e5c01f78cfc4b70f618c90c3`.

The provider branch preserves its prerequisites as distinguishable commits:

1. `f2959604` — provider-neutral view families (cherry-picked from `9a88d556`)
2. `141b6d1e` — view-family legacy source-list entry (from `c0e81509`)
3. `99a8e56a` — provider-neutral presentation layers (from `2c9c452b`;
   CMake test placement reconciled with the Emscripten block)
4. `5082bc63` — frame-flash presentation correction (from `9d96e62f`)
5. `0ca84163` — opaque target binding and per-view selection seam
   (cherry-picked from OpenXR lane commit `398dbfd8`)

The WebXR implementation follows those prerequisites and should not be used as
the source for upstreaming the shared seams.

Presentation-runtime follow-up branch: `pr/webxr-presentation-runtime`, based
directly on `pr/webxr-provider` at `89608ca7`. It originally upgraded the
private frame ABI to version 2, completed per-eye texture metadata handoff, and
added capability and lifecycle diagnostics without changing the flat
desktop-WASM entry point. Window-owned Asyncify first used ABI version 3 for
persistent host-owned textures. The current-pose branch uses ABI version 4;
its packet layout remains unchanged, while the native lifecycle is explicitly
split into asynchronous preparation, synchronous current-pose rendering, and
asynchronous completion.

## Checkpoint provenance

The implementation was extracted and adapted from `webxr-m1`:

- `288d027c` — initial WebXR session harness
- `5be186f1` — WebGPU presentation groundwork
- `264ffe23` — two-view browser frame path
- `e58a6f43` — packed frame ABI
- `1e6cc90e` — opt-in XRSession-owned frame loop
- `14451d9f` — projection color-format support
- `b4b02dab` — tracked head pose composition and recentering
- `8f29634d` — generation-safe session lifecycle

Adaptations made during extraction:

- replaced checkpoint-specific `WebXRSceneView` calls with shared
  `ViewFamily` construction;
- replaced direct external-target calls with `PresentationTargetBinding` and
  `BeginPresentationView`;
- kept the external native handles opaque to engine code;
- restored the flat canvas pipeline after every XR target unbind;
- retained the browser-preferred XR projection format instead of assuming the
  canvas format; and
- cached one WebGPU pipeline family per encountered color format so XR frame
  bind/unbind does not rebuild pipelines, while still restoring the flat canvas
  family after each frame; and
- removed every controller, gameplay, UI, persistence, and PWA dependency.

Provider C++ lives under `SurrealEngine/Platform/WebXR`. Browser ownership and
packet packing live in `web/webxr_provider.js`.

The release-package diagnostics layer may read the provider's copied status
snapshot. That snapshot also exposes a bounded, sequence-numbered lifecycle
transition list, the current setup stage, last failure stage, and cumulative
entry/exit/re-entry counts. It allowlists presentation mode, known layer/atlas
dimensions, and bounded bridge timing summaries; raw timing samples do not leave
the bridge. These fields are observational: they do not change session
ownership, frame rendering, input submission, or error cleanup. The release
reporter does not export provider error text because browser/native exceptions
may contain environment-specific details.

A runtime-driven `end` event during session reservation or layer/reference-space
activation is retained as `session-ended-before-activation`. If activation
completed but the runtime ends before the first native render is published, the
code is `session-ended-before-first-frame`. Both are fixed allowlisted tokens;
their messages are static and raw browser/runtime exceptions remain private.
An explicit `surrealXRExit()` before the first frame and a runtime end after at
least one published frame remain normal clean ends.

## Frame ownership and failure behavior

The ordinary Emscripten main loop remains registered for the process lifetime,
but `Surreal_SetXRFrameLoopActive(1)` pauses it rather than allowing its callback
to re-enter Wasm and return early. It resumes only after the last Asyncify
preparation/completion and session cleanup have drained. Ownership transfers only
after the selected mode has a session, projection layer, and reference space:
direct mode additionally requires `XRGPUBinding`, while compatibility mode
requires its WebGL 2 context, `XRWebGLLayer`, and stereo-atlas bridge. Session
end or any frame exception restores canvas scheduling and resets the
tracked-pose origin.

ABI v4 separates frame work at a hard browser boundary. Outside XR animation
callbacks, one Asyncify-aware task applies at most one retained input state and
calls `Surreal_PrepareWebXRFrame` to advance simulation. Once preparation has
completed, the next XR animation callback obtains its current `XRViewerPose`,
calls `Surreal_RenderWebXRFrame` synchronously with that callback's view packet,
and immediately copies/uploads and presents the result before returning. There
is no Promise or microtask boundary from pose sampling through native render
and presentation. `Surreal_CompleteWebXRFrame` then runs outside the callback
to perform deferred finish/travel/save work and allow the next preparation.

If preparation is not complete when an XR callback begins, that callback
clears/skips; it never resubmits the previous pose's target. Discrete input
states remain queued for later simulation frames, while a lone continuous
pose/axis state is replaced by its newest sample. A missing viewer pose skips
capture without advancing simulation. This removes guaranteed stale-pose
presentation at the cost of dropped headset frames when preparation or OPFS
work misses the callback deadline. Simulation state may be older than the
render pose, but the rendered headset view uses the presenting callback's pose.

The same native-call gate covers browser-side services. Mutable-data interval,
visibility, and pagehide checkpoints never inspect `Module.FS` while an
Asyncify preparation/completion is unresolved. Repeated triggers coalesce into one
deferred lifecycle reason and flush once the gate reopens; disposal removes
the listener. Browser audio uses the same defer-and-flush rule. Once the engine
has created its OpenAL/WebAudio context, startup, return-to-visible, and
presentation transitions make a best-effort resume; hidden pages still
suspend. A trusted `selectstart` on the currently active XR session forwards
only a detail-free audio-unlock event, never controller or pose data. Central
session cleanup removes that listener before allowing re-entry, while the DOM
Enable audio button remains the explicit policy fallback.

Asyncify builds also link an Emscripten user JavaScript library that installs a
native-callback gate before `SDL_Init`. It wraps the live
`JSEvents.registerOrRemoveHandler` registration seam, so keyboard, mouse,
wheel, touch, focus, blur, visibility, gamepad, and other Emscripten HTML5
callbacks cannot use `dynCall` to re-enter Wasm while
`surrealXRNativeCallsBlocked` is true. The same library overrides
`emscripten_set_timeout` and `emscripten_clear_timeout` with stable logical
timer IDs, preventing SDL timers from entering Wasm during the unresolved
preparation/completion and retaining correct cancellation behavior. Ordinary non-Asyncify
browser builds do not link or install this library.

Blocked DOM events are snapshotted in gate-owned JavaScript and replayed
after the gate reopens. Only continuous events with the same registration
owner and type are coalesced; discrete transitions retain capture order.
Registrations removed before replay are treated as stale and skipped. The
queue is bounded: expendable continuous samples are discarded first, while a
discrete-only overflow latches instead of silently dropping an input edge. One
`surrealnativecallgateoverflow` event then fails the WebXR provider closed, and
normal cleanup still waits for the in-flight async phase before releasing native
state. The latch and deferred queue are cleared during that cleanup so a later
session can start cleanly.

This is intentionally a scoped Emscripten integration rather than a patch to
`EventTarget` or `safeSetTimeout`. Browser-only tasks and Asyncify's own sleep
mechanism remain available while native entry is blocked. The generated SDL2
path uses the current `JSEvents` handlers; audited debug, release, and packaged
outputs contain no legacy `SDL.receiveEvent` path.

The input queue preserves discrete focus, connection, pressed, and touched
transitions in capture order even when more than 16 changes arrive during one
suspended render. Pose and axis-only samples with the same discrete state are
coalesced to the latest sample. A 256-entry safety limit fails the session
closed with `input-transition-overflow`; it never silently discards a button
edge. Only one retained discrete state is submitted before each native
simulation preparation, ensuring a quick press and release are observable
on separate engine frames instead of both being applied before one tick.
Session focus loss and controller removal are cancellation barriers: older
queued gameplay edges are discarded and the neutral/latest connection state
is delivered next, preventing delayed firing after a system overlay or lost
controller. A safety packet can be applied without inventing a simulation or
render frame when focus/controller events arrive and the browser supplies no
new viewer pose.

In direct mode, ABI v4 gives native code two ordinary persistent 2D eye
textures. The projection layer requests `COPY_DST`; after synchronous native
rendering, that same XR callback copies the just-rendered eyes into its current
`XRGPUSubImage` viewport/array slices before returning. Before copying, it validates integer viewport bounds, one valid
array layer, exact projection format, and `COPY_DST` usage. Any mismatch fails
closed as `invalid-projection-subimage`. In compatibility mode, native code
receives one persistent stereo
atlas. A separate hidden WebGPU transfer canvas is acquired late, receives the
atlas copy, and is synchronously uploaded and drawn into the `XRWebGLLayer` in
that same callback. The SDL/flat canvas is not resized or borrowed by the
bridge.

Session shutdown or frame failure immediately invalidates the generation and
cancels the queued XR animation callback. Native neutral-input/pose cleanup,
target destruction, flat-loop resume, and a new session request wait for any
in-flight async phase and submitted GPU work to finish. An old-generation
completion can therefore neither publish nor present. Capability reporting
distinguishes secure-context, immersive-session, current `XRGPUBinding`,
obsolete `XRWebGPUBinding`, WebGPU-device readiness, and whether that device was
created from an adapter requested with `xrCompatible: true`. Failures expose a
stable code and stage in `surrealXRGetState()`.

Unsupported projection formats fail closed during entry. A rejected imported
texture or presentation target fails the active session rather than silently
rendering to the flat canvas.

## Validation

The full-engine entries below include the native-callback-gate follow-up.
Focused debug and release harnesses pass, as do complete optimized default and
Window-owned Asyncify `SurrealEngine` rebuilds.

Completed locally:

- native Windows RelWithDebInfo compile/link of the complete default target
  set, including `SurrealEngine`, editor, debugger, and tests;
- `PresentationTests`, `WebXRFrameBridgeTests`, and the ABI-v4
  `WebXRFramePhaseTests`, all passing;
- synthetic Node lifecycle test covering capability, preferred RGBA format,
  duplicate-entry rejection, distinct per-eye textures, shared texture-array
  slices, packed two-view metadata, callback cancellation, controlled frame
  failure, exit, and re-entry;
- Emscripten compilation and final JavaScript/WASM link;
- focused `WebXRUIProviderTests` coverage for shared pose conversion, stable
  descriptor targets/scaling, center-pixel contact, one-shot replay/click,
  both-hand visual construction, exact beam endpoint/radius, marker alignment,
  draw-order invariants, and held-select edge behavior;
- all 30 registered native tests passing in both ordinary and OpenXR-enabled
  Release builds at integrated commit `359aaa30`;
- direct and fallback provider tests passing controller input, cleanup,
  exit/re-entry, and flat-loop restoration through the shared native runtime;
- deterministic ABI-v4 tests proving async prepare/synchronous render/async
  complete ordering, no provider/audio re-entry while Asyncify is unresolved,
  bounded ordered input queueing, deferred exit cleanup, and re-entry after
  cleanup. The direct-provider test opens a microtask-bounded window at
  `getViewerPose` and requires the exact uninterrupted order pose, synchronous
  native render, left-eye copy, right-eye copy inside one XR callback;
- deterministic mutable-persistence gate coverage proving multiple interval
  and lifecycle triggers make zero filesystem calls while blocked, then create
  exactly one checkpoint after unblock and none after teardown;
- fixed-256 MiB Window-owned Asyncify Emscripten compile and final link with
  the paused flat loop and phased provider;
- ordinary non-Asyncify Emscripten compile and final link, preserving the
  shared flat/browser build configuration;
- a real desktop Chrome WebGPU-canvas to WebGL 2 upload/readback probe passing;
- historical ABI-v3 real desktop Chrome two-phase WebGPU coverage proving an artificially
  suspended Wasm producer left the red front immutable across three browser
  frames, never exposed the partial blue back, then published green, with one
  Wasm entry, zero rAF-time Wasm re-entries, and zero validation/uncaptured
  errors;
- a real desktop Chrome compatibility-shape probe completing 60 exact-pixel
  persistent-atlas transfers after an awaited boundary, acquiring the transfer
  canvas late and performing submit plus immediate WebGL `texSubImage2D` in
  one callback (1.390 ms median, 2.115 ms p95), with zero WebGPU/WebGL errors;
- that same probe's expired-canvas negative control produced the expected
  destroyed-current-texture submission validation error;
- a standalone Emscripten/Asyncify real-Chrome probe observed the JavaScript-
  published `EngineMainLoopCallback` count remain exactly 3 through eight
  samples during the unresolved suspension and advance to 4 only after resume;
- a real-Chrome Emscripten/SDL2 callback-gate harness passed in debug and
  release configurations: synthetic keyboard, mouse, wheel, touch, focus,
  blur, visibility, and gamepad events plus repeating and cancelled
  `SDL_AddTimer` callbacks produced zero Wasm entries while the Asyncify
  async native phase was unresolved, then resumed after unblock; an `SDL_AddEventWatch`
  canary also remained silent during suspension and observed replayed SDL
  events afterward;
- that harness's overflow control proved the bounded queue emits one provider
  failure, makes no further Wasm entry before the async phase resolves, drains
  cleanup, and permits a fresh session; its generated-output audit also found
  no `SDL.receiveEvent` path;
- bridge-provider Node coverage proving distinct persistent atlas targets,
  no native re-entry across multiple rAF callbacks while preparation is
  unresolved, clear/skip rather than old-target reuse, current-pose render and
  presentation in one callback, immediate exit invalidation, deferred
  destruction, and no stale publication after drain;
- flat Chrome/WebGPU UT99 runtime after the provider changes: ticked from 61
  to 604, 95 draw calls, 75 cached textures, zero WebGPU errors, 100% nonblank
  screenshot pixels, and clean quit;
- `git diff --check`.

Commands:

```text
cmake -S . -B build "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
node web/test_webxr_provider.mjs
node web/test_webxr_webgl_bridge.mjs
node web/test_webxr_webgl_fallback_provider.mjs

cmake -S . -B build-webxr-mainloop-pause-em -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-webxr-mainloop-pause-em --target WebXRMainLoopPauseProbe --parallel 4
python web/smoke_test_webxr_main_loop_pause.py

cmake -S . -B build-webxr-native-callback-gate-em -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DSURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS_ASYNCIFY=ON
cmake --build build-webxr-native-callback-gate-em --target WebXRNativeCallbackGateProbe --parallel 4
python web/smoke_test_webxr_native_callback_gate.py

& C:\Devstuff\emsdk\emsdk_env.ps1
emcmake cmake -S . -B build-emscripten -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" -DBUILD_TESTING=OFF
cmake --build build-emscripten --target SurrealEngine --parallel 8
node web/serve.mjs 8094
$env:SURREAL_WEB_BASE_URL="http://localhost:8094"
python web/probes/webgpu_webgl_bridge_probe_test.py
python web/probes/webgpu_two_phase_present_probe_test.py
python web/smoke_test_webgpu.py
```

## Hardware gate and known limitations

Automated tests cannot prove physical headset presentation. For desktop Chrome
through Virtual Desktop/VDXR, the Quest must be awake and actively connected as
a WebXR headset before the page loads and requests `immersive-vr`. A report
with `enter_attempts: 0` records no immersive request and is not headset-session
evidence. A 2026-07-23 Quest
3 test through desktop Chrome, Virtual Desktop, and VDXR reached WebXR consent
in Automatic mode and immediately returned to flat presentation. The v2 report
was not saved, so the precise session/binding/projection stage is unknown. A
later run on immutable candidate `173bf623` selected **Force WebGL compatibility
bridge**, left **Temporary QA: blocking bridge timing** unchecked, and entered
immersive VR. Head rotation produced severe distortion, recognizable geometry
ended in black at a short distance, and the blue/red procedural controller
proxies were visible and tracked. This proves reservation, activation,
`XRWebGLLayer` presentation, native rendering, UI composition, and XR input; it
does not qualify stereo, FOV, world scale, latency, or visual correctness. See
`WEBXR_VDXR_QUALIFICATION.md`. The current WebXR/WebGPU
specification is explicitly an unstable editor's draft.
Its current interface name is `XRGPUBinding`; `XRWebGPUBinding` is an obsolete
experimental spelling and is reported but not used. Chrome first documented
WebXR/WebGPU on Android as an experimental developer-testing feature in Chrome
135, behind the WebXR/WebGPU binding capability. Ordinary WebGPU availability
is therefore not evidence that WebGPU can present to WebXR.

Primary references: the [WebXR/WebGPU Binding editor's draft](https://immersive-web.github.io/webxr-webgpu-binding/)
and Chrome's [WebGPU 135 platform note](https://developer.chrome.com/blog/new-in-webgpu-135).

A real Quest browser may use either implemented presentation mode. Direct mode
requires `XRGPUBinding`, an immersive session with the `webgpu` feature, and an
XR-compatible WebGPU adapter/device. Compatibility mode instead creates a
normal immersive session with `XRWebGLLayer`, renders both eyes into the
existing WebGPU canvas atlas, and copies that atlas through WebGL 2. The second
mode was informed by the working Quake presentation shape but does not add a
second SurrealEngine render device.

Automatic capability selection no longer treats successful
`requestAdapter({xrCompatible: true})` as conclusive: a WebIDL implementation
may ignore an unrecognized dictionary member. At `f3643b78`, when both backends
are available, the provider makes one session request with `webgpu` optional,
then selects from `session.enabledFeatures`. If `webgpu` is enabled, WebGL
`baseLayer` fallback is forbidden for that session; a later direct failure must
end it and require a fresh trusted gesture for forced bridge. Missing or
uninspectable enabled features fail closed with an allowlisted exact code and
forced-bridge guidance. The automated direct/bridge exclusivity matrix passes;
physical VDXR requalification remains.

### Projection metre/Unreal-Unit correction and retest gate

`XRView.projectionMatrix` encodes `depthNear` and `depthFar` in metres, while
the native `WorldToView` input is in Unreal Units. Candidate `173bf623` retained
the runtime matrix and flipped forward Z but did not apply
`WorldUnitsPerMeter`. With the default 39.3701 UU/m, the default 1000 m far
plane acted like approximately 1000 UU, or 25.4 m, matching the observed short
black cutoff.

Commit `0331ef21` retains the runtime matrix, applies the existing handedness
conversion, and scales its entire fourth homogeneous column by
`WorldUnitsPerMeter`. This preserves asymmetric FOV and runtime shear rather
than rebuilding a symmetric frustum. The native correction applies to both
presentation modes. Bridge packets first convert the runtime WebGL `[-1,1]`
depth projection to WebGPU `[0,1]` and set
`FrameProjectionDepthZeroToOne`. At `0218d5b7`, direct `XRGPUBinding` packets
also set that flag because Chromium already supplies their projections in
WebGPU `[0,1]` form; native code therefore does not convert them again. Near,
far, asymmetric-eye, rotated-pose, and mode-specific depth tests pass. Physical
Quest/VDXR requalification is still required before the cutoff or stereo/FOV
defect can be considered closed.

### Current-pose render boundary and pose age

ABI v4 implements the minimum same-callback boundary: simulation preparation
finishes before the XR callback; the callback samples its current pose, performs
one synchronous native render, and copies/uploads that result immediately.
Deferred `FinishGameFrame` work and the next preparation run afterward. No
`await`, Promise continuation, or Asyncify-capable call is permitted between
`getViewerPose` and presentation. If preparation misses the callback, the
provider clears/skips instead of reusing a completed atlas from an older pose.

The compatibility bridge's optional rotation-only reprojection remains
default-off. It is no longer required to compensate for a deliberately stale
source pose; if enabled for QA, source and current metadata describe the same
callback and therefore should produce an identity correction. A color-only
warp still cannot correct translation or nearby geometry and is not a release
qualification mechanism.

Commit `078a6d2f` adds privacy-bounded measurements to provider state and the
v2 report:

- `bridge_present_age_frames` / `bridge_max_present_age_frames` are the current
  and session-maximum differences between the presenting callback sequence and
  the source capture sequence, bounded to 65,535;
- `bridge_present_age_ms` / `bridge_max_present_age_ms` are the corresponding
  callback-time differences, bounded to 60,000 ms; and
- `bridge_reused_presents` counts repeated submissions of a completed target,
  bounded to 4,294,967,295 and reset with each session/re-entry.

Before the first completed atlas is presented, age fields are `unknown` and
reuse is zero. ABI-v4 presentations should report zero current/maximum age and
zero reuse because each rendered target is submitted once from the same
callback. Any positive age or reuse is now a regression signal. Only
allowlisted numeric aggregates leave the provider: source poses, view matrices,
projections, game data, paths, and logs remain excluded.

The physical report can now distinguish an ordinary session-request or layer
failure from a runtime that grants consent and then ends the session before
activation/first presentation. This is diagnostic state only; it neither keeps
an ended session alive nor attempts an illegal same-session backend fallback.
If an end arrives while asynchronous WebGL `makeXRCompatible()` setup is still
pending, a later-created bridge is destroyed rather than being attached to a
dead generation; reference-space and engine-loop ownership cannot continue.

Both modes remain **experimental**. Automated tests prove selection, ABI,
projection conversion, shared engine behavior, cleanup, and desktop cross-API
upload/readback. Hardware proves that the older VDXR `XRWebGLLayer` path could
enter and display native content, but contradicted visual correctness.
Automated ordering tests prove the ABI-v4 software boundary, not physical
current-pose presentation: that requires an actively connected headset and a
successful immersive session (`enter_attempts`, `successful_entries`, and
`frames` must be nonzero). Tests still cannot prove direct binding, stereo/FOV
correctness, or that synchronous rendering plus cross-API copy meets the
headset frame budget. The release gates and
timing thresholds are recorded in `WEBXR_WEBGL_BRIDGE_HANDOFF.md`. A real WebGL
2 render device remains the contingency if the atlas bridge fails those gates.

The integration provider renders the world, a controller-aimed first-person
game weapon in each eye, procedural tracked-controller proxies,
exact-contact lasers/markers, and captured HUD/menu/loading/cinematic surfaces.
The weapon path is experimental and still needs loaded-game and physical Quest
qualification. Browser audio uses the bounded OpenAL browser backend rather
than the former null backend.

The `integration/web-cinematic` topic replaces the Emscripten null decoder with
the existing IV50 SurrealVideo implementation and advances it from the outer
flat/WebXR frame owner instead of entering the legacy synchronous `PlayAVI`
loop. It routes decoded frames through this provider-neutral cinematic capture
path; see `WebCinematicPlayback.md` for tests and limitations. Owner-supplied
KHG data and a physical headset are still required to validate actual media,
and UT99/Unreal map intros use a separate implemented `URL.LocalMap`, startup
HUD, menu-handoff, and intro-trigger path. That map path has synthetic coverage
but remains owner-data/headset-unverified. Loading now has both a configured
target and an authoritative, exception-safe engine visibility scope around
real map and save loads. Its actual presentation during yielded browser I/O
and long travel remains an owner-data/headset validation gate. These are exact
content/lifecycle blockers, not quad-compositor blockers.

The procedural proxy uses target-ray orientation with a stable world-up roll;
the current feedback contract does not carry grip-pose roll into the compositor.
A headset pass must validate perceived proxy size, near-field comfort, beam
thickness, stereo marker convergence, controller disconnect, both-hands-held
selection, and menu readability on Quest before these defaults are considered
release tuned.

## Shared browser launcher composition

The integration branch composes this provider with the XR-neutral browser app
through `web/webxr_browser_app_adapter.js`. `web/surreal_app.html` is one shared
game library for both targets. The release shell probes WebXR independently,
requests an XR-compatible WebGPU adapter only when the provider can otherwise
run, and retries an ordinary adapter when XR compatibility is unavailable. The
adapter activates WebXR only after the launcher has validated local game data
and launch policy and started the native engine. Skip mode also requires a
validated safe map; normal-intro mode deliberately omits `--url` and leaves the
game's `URL.LocalMap` authoritative. Exiting, declining, or failing the session
leaves that same flat application running. The shared launcher also propagates
whether the actual device came from an adapter requested with
`xrCompatible: true`, so provider entry cannot mistake an ordinary flat WebGPU
device for an XR-compatible one.
