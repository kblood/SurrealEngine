# WebXR provider handoff

Date: 2026-07-22

Integration status: implemented and automated at `integration/unified-engine`
commit `a0fb4f93`; both browser presentation modes remain experimental and
hardware-unverified on Quest.

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
- direct `XRGPUBinding` presentation plus an automatic `XRWebGLLayer`
  compatibility mode that copies a WebGPU-rendered stereo atlas through
  WebGL 2; and
- a separate `web/index_webxr.html` harness. The existing flat
  `web/index_webgpu.html` remains unchanged and does not require WebXR.

The provider deliberately excludes locomotion, weapon behavior, dominant-hand
policy, game-specific controller models, haptics, PWA packaging, game-data
import, data persistence, and game-specific VM hooks. Product integration adds
procedural controller proxies through the provider-neutral UI compositor; they
are not weapon models or provider policy.

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
desktop-WASM entry point. The Window-owned Asyncify integration now uses ABI
version 3. Its binary layout is unchanged, but v3 makes persistent host-owned
textures (never XR compositor or canvas-current textures) part of the contract.

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

## Frame ownership and failure behavior

The ordinary Emscripten main loop remains registered for the process lifetime,
but `Surreal_SetXRFrameLoopActive(1)` pauses it rather than allowing its callback
to re-enter Wasm and return early. It resumes only after the last Asyncify
render and session cleanup have drained. Ownership transfers only
after the selected mode has a session, projection layer, and reference space:
direct mode additionally requires `XRGPUBinding`, while compatibility mode
requires its WebGL 2 context, `XRWebGLLayer`, and stereo-atlas bridge. Session
end or any frame exception restores canvas scheduling and resets the
tracked-pose origin.

Each XR animation callback requires one left and one right primary view, but it
never calls Wasm. It copies pose, projection, controller input, and layout
metadata into JavaScript-owned packets; presents the last completed front
target; discards all current-frame XR objects; and returns. A later browser task
drains the ordered input queue and starts at most one Asyncify-aware native
producer render into the back target. Completion atomically publishes that back
target. A missing viewer pose skips capture without advancing simulation, and
headset frames may repeat the immutable front image while the producer waits on
OPFS. This is intentional frame dropping, not a second simulation tick.

In direct mode, ABI v3 gives native code two ordinary persistent 2D eye
textures. The projection layer requests `COPY_DST`; the XR callback acquires
each current `XRGPUSubImage` only during its late presentation section and
copies the immutable front eyes into the returned viewport/array slices before
returning. In compatibility mode, native code receives one persistent stereo
atlas. A separate hidden WebGPU transfer canvas is acquired late, receives the
atlas copy, and is synchronously uploaded and drawn into the `XRWebGLLayer` in
that same callback. The SDL/flat canvas is not resized or borrowed by the
bridge.

Session shutdown or frame failure immediately invalidates the generation and
cancels the queued XR animation callback. Native neutral-input/pose cleanup,
target destruction, flat-loop resume, and a new session request wait for any
in-flight producer and submitted GPU work to finish. An old-generation
completion can therefore neither publish nor present. Capability reporting
distinguishes secure-context, immersive-session, current `XRGPUBinding`,
obsolete `XRWebGPUBinding`, WebGPU-device readiness, and whether that device was
created from an adapter requested with `xrCompatible: true`. Failures expose a
stable code and stage in `surrealXRGetState()`.

Unsupported projection formats fail closed during entry. A rejected imported
texture or presentation target fails the active session rather than silently
rendering to the flat canvas.

## Validation

Completed locally:

- native Windows RelWithDebInfo compile/link of the complete default target
  set, including `SurrealEngine`, editor, debugger, and tests;
- `PresentationTests` and `WebXRFrameBridgeTests`, both passing;
- synthetic Node lifecycle test covering capability, preferred RGBA format,
  duplicate-entry rejection, distinct per-eye textures, shared texture-array
  slices, packed two-view metadata, callback cancellation, controlled frame
  failure, exit, and re-entry;
- Emscripten compilation and final JavaScript/WASM link;
- focused `WebXRUIProviderTests` coverage for shared pose conversion, stable
  descriptor targets/scaling, center-pixel contact, one-shot replay/click,
  both-hand visual construction, exact beam endpoint/radius, marker alignment,
  draw-order invariants, and held-select edge behavior;
- all 25 registered native tests passing at integrated commit `a0fb4f93`;
- direct and fallback provider tests passing controller input, cleanup,
  exit/re-entry, and flat-loop restoration through the shared native runtime;
- deterministic ABI-v3 tests proving persistent double buffering, no Wasm
  calls from XR callbacks, no provider/audio re-entry while an Asyncify render
  is unresolved, bounded ordered input queueing, frame replacement, deferred
  exit cleanup, and re-entry after cleanup;
- fixed-256 MiB Window-owned Asyncify Emscripten compile and final link with
  the paused flat loop and two-phase provider;
- ordinary non-Asyncify Emscripten compile and final link, preserving the
  shared flat/browser build configuration;
- a real desktop Chrome WebGPU-canvas to WebGL 2 upload/readback probe passing;
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

& C:\Devstuff\emsdk\emsdk_env.ps1
emcmake cmake -S . -B build-emscripten -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" -DBUILD_TESTING=OFF
cmake --build build-emscripten --target SurrealEngine --parallel 8
node web/serve.mjs 8094
$env:SURREAL_WEB_BASE_URL="http://localhost:8094"
python web/probes/webgpu_webgl_bridge_probe_test.py
python web/smoke_test_webgpu.py
```

## Hardware gate and known limitations

No automated test proves physical headset presentation, and no repository
evidence yet proves either mode on a target Quest/browser combination. The
current WebXR/WebGPU specification is explicitly an unstable editor's draft.
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

Both modes remain **experimental**. Automated tests prove selection, ABI,
projection conversion, shared engine behavior, cleanup, and desktop cross-API
upload/readback. They cannot prove that a target Quest browser exposes direct
binding, that its opaque WebGL XR framebuffer presents the atlas correctly, or
that the cross-API copy meets the headset frame budget. The release gates and
timing thresholds are recorded in `WEBXR_WEBGL_BRIDGE_HANDOFF.md`. A real WebGL
2 render device remains the contingency if the atlas bridge fails those gates.

The integration provider renders the world, procedural tracked-controller
proxies, exact-contact lasers/markers, and captured HUD/menu/loading/cinematic
surfaces. Weapon rendering remains disabled; the proxy is intentionally not a
game weapon model. Browser audio remains the flat platform's null backend.

The `integration/web-cinematic` topic replaces the Emscripten null decoder with
the existing IV50 SurrealVideo implementation and advances it from the outer
flat/WebXR frame owner instead of entering the legacy synchronous `PlayAVI`
loop. It routes decoded frames through this provider-neutral cinematic capture
path; see `WebCinematicPlayback.md` for tests and limitations. Owner-supplied
KHG data and a physical headset are still required to validate actual media,
and UT99/Unreal map intros use a separate implemented `URL.LocalMap`, startup
HUD, menu-handoff, and intro-trigger path. That map path has synthetic coverage
but remains owner-data/headset-unverified. Loading has a configured target but
still needs an authoritative engine loading-visibility signal before it can be
shown. These are exact content/lifecycle blockers, not quad-compositor blockers.

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
