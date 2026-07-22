# WebXR provider skeleton handoff

Date: 2026-07-22

## Scope

This branch layers an optional WebXR/WebGPU provider on the flat Emscripten
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
  from `XRGPUBinding.getPreferredColorFormat()`; and
- provider-owned menu, loading, and cinematic capture textures, replayed as
  stable world-space quads into both projection eyes without scene depth; and
- exact tracked-controller menu contact routing plus a packed laser/hit
  feedback ABI; and
- a separate `web/index_webxr.html` harness. The existing flat
  `web/index_webgpu.html` remains unchanged and does not require WebXR.

The provider deliberately excludes locomotion, weapon behavior, dominant-hand
policy, controller models, haptics, PWA packaging, game-data import, data
persistence, and game-specific VM hooks.

## Projection-eye UI connector

The integration provider registers four non-zero WebGPU target slots for one
XR frame: projection/world is slot 1, cinematic is slot 2, loading is slot 3,
and menu is slot 4. The WebGPU backend can switch among registered targets
inside one locked engine frame. UI replay therefore uses the existing
`XRUISurfaceEngineBinding` and existing UE1 canvas callbacks; it does not add a
second `PostRender` path. The ordinary flat WebGPU path still registers no
external targets and retains its original slot-zero render flow.

Menu, loading, and cinematic textures are composited after world rendering by
opening a load pass on each projection-eye texture. The pass has no depth
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
directly on `pr/webxr-provider` at `89608ca7`. It upgrades the private frame ABI
to version 2, completes per-eye texture metadata handoff, and adds capability
and lifecycle diagnostics without changing the flat desktop-WASM entry point.

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
entry/exit/re-entry counts. These fields are observational: they do not change
session ownership, frame rendering, input submission, or error cleanup. The
release reporter does not export provider error text because browser/native
exceptions may contain environment-specific details.

## Frame ownership and failure behavior

The ordinary Emscripten main loop remains registered for the process lifetime.
It yields while an XR session owns frame scheduling. Ownership transfers only
after the session, `XRGPUBinding`, projection layer, and reference space are
ready. Session end or any frame exception restores canvas scheduling and resets
the tracked-pose origin.

Each callback requires one left and one right primary view, validates the ABI in native code,
advances simulation exactly once, builds one multi-view family, renders it, and
finishes deferred save/travel work once. A missing viewer pose skips the frame
without advancing simulation.

The browser acquires an `XRGPUSubImage` for each `XRView`. ABI v2 deduplicates
the common texture-array case, but does not assume both subimages share a
`GPUTexture`: each packed view identifies its texture, array layer, texture
extent, and viewport. Native code verifies those values against the imported
runtime-owned texture before binding it. All imports and releases remain inside
the owning `XRSession.requestAnimationFrame()` callback.

Session shutdown or frame failure cancels the queued XR animation callback,
releases frame-loop ownership only if it was acquired, clears the transient
texture handoff, and restores the ordinary canvas loop. Capability reporting
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
- all 23 registered native tests passing after the visual connector, including
  all eight WebXR/XR-UI focused tests;
- flat Chrome/WebGPU UT99 runtime after the provider changes: ticked from 61
  to 604, 95 draw calls, 75 cached textures, zero WebGPU errors, 100% nonblank
  screenshot pixels, and clean quit;
- `git diff --check`.

Commands:

```text
cmake -S . -B build "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" -DBUILD_TESTING=ON
cmake --build build --config Release --target SurrealEngine WebXRFrameBridgeTests PresentationTests --parallel 4
ctest --test-dir build -C Release --output-on-failure -R "WebXRFrameBridgeTests|PresentationTests"
node web/test_webxr_provider.mjs

& C:\Devstuff\emsdk\emsdk_env.ps1
emcmake cmake -S . -B build-emscripten -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" -DBUILD_TESTING=OFF
cmake --build build-emscripten --target SurrealEngine --parallel 8
node web/serve.mjs 8094
$env:SURREAL_WEB_BASE_URL="http://localhost:8094"; python web/smoke_test_webgpu.py
```

## Hardware gate and known limitations

No automated test proves physical headset presentation. The current
WebXR/WebGPU specification is explicitly an unstable editor's draft. Its current
interface name is `XRGPUBinding`; `XRWebGPUBinding` is an obsolete experimental
spelling and is reported but not used. Chrome first documented WebXR/WebGPU on
Android as an experimental developer-testing feature in Chrome 135, behind the
WebXR/WebGPU binding capability. Ordinary WebGPU availability is therefore not
evidence that WebGPU can present to WebXR.

Primary references: the [WebXR/WebGPU Binding editor's draft](https://immersive-web.github.io/webxr-webgpu-binding/)
and Chrome's [WebGPU 135 platform note](https://developer.chrome.com/blog/new-in-webgpu-135).

A real Quest browser and runtime must expose `XRGPUBinding`, accept an immersive
session with the required `webgpu` feature, accept a `GPUDevice` created from an
adapter requested with `xrCompatible: true`, import the runtime-owned
`GPUTexture` through Emscripten's WebGPU bridge, and present both views. This is
the release gate for the provider, not something the synthetic lifecycle test
can emulate. Until that passes on the target Quest Browser version, this branch
is an experimental/flag-required WebXR build, not a production WebXR release.

Quake's working browser path uses `XRWebGLLayer`, which is materially different.
SurrealEngine currently has no WebGL render backend. The minimum broadly
deployable fallback would be a separate WebGL2 render-device implementation
that can draw to the `XRWebGLLayer` framebuffer while reusing the same neutral
`ViewFamily`, frame ownership, input, and presentation-policy seams. That is a
sizable renderer project and must be an explicit product decision; it is not a
small fallback inside this provider.

The integration provider renders the world, procedural tracked-controller
proxies, exact-contact lasers/markers, and captured menu/loading/cinematic
surfaces. Weapon rendering remains disabled; the proxy is intentionally not a
game weapon model. Browser audio remains the flat platform's null backend.

The `integration/web-cinematic` topic replaces the Emscripten null decoder with
the existing IV50 SurrealVideo implementation and advances it from the outer
flat/WebXR frame owner instead of entering the legacy synchronous `PlayAVI`
loop. It routes decoded frames through this provider-neutral cinematic capture
path; see `WebCinematicPlayback.md` for tests and limitations. Owner-supplied
KHG data and a physical headset are still required to validate actual media,
and UT99/Unreal map intros remain a separate world/UI path. Loading has a
configured target but still needs an authoritative engine loading-visibility
signal before it can be shown. These are exact content/lifecycle blockers, not
quad-compositor blockers.

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
adapter activates WebXR only after the launcher has validated local game data,
selected a safe map, and started the native engine. Exiting, declining, or
failing the session leaves that same flat application running. The shared
launcher also propagates whether the actual device came from an adapter
requested with `xrCompatible: true`, so provider entry cannot mistake an
ordinary flat WebGPU device for an XR-compatible one.
