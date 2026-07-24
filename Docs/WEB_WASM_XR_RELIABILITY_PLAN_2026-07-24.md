# Web, WASM, and WebXR reliability plan

Date: 2026-07-24

## Decision

Ship one long-lived browser game that always starts flat. WebXR is an optional,
hot-swappable presentation mode entered from a real **Enter VR** button after the
game is running. Ending or losing the XR session returns to the same flat game;
it must not reload the page, call `main()` again, remount game data, or recreate
the game world.

```text
FlatRunning
  -> RequestingSession -> ActivatingXR -> ImmersiveRunning
  <- request/setup failure <- EndingXR <- exit, disconnect, or runtime end
```

Only one scheduler may advance simulation. The Emscripten/window animation loop
owns flat mode and `XRSession.requestAnimationFrame` owns immersive mode. This is
the lifecycle described by the [WebXR application flow](https://www.w3.org/TR/webxr/#application-flow)
and supported by [Emscripten's main-loop pause/resume API](https://emscripten.org/docs/api_reference/emscripten.h.html).

## What already exists

SurrealEngine is closer to this design than the current launcher suggests:

- `Surreal_SetXRFrameLoopActive()` pauses and resumes the existing Emscripten
  loop instead of creating another game runtime.
- `webxr_provider.js` has generation-safe session setup, XR frame ownership,
  teardown, re-entry, `surrealXREnter()`, and `surrealXRExit()`.
- Session failure and normal exit restore flat scheduling.
- The missing product seam is a post-launch Enter/Exit VR controller. The
  release currently makes presentation a pre-launch choice.

QuakeQuest supports the same important invariant—one game update with
presentation-specific eye rendering—but it enters native OpenXR before Quake
starts and stays in that session. Its big-screen mode changes presentation
inside XR; it is not a browser-style flat-to-XR lifecycle. See the
[QuakeQuest integration](https://github.com/Team-Beef-Studios/QuakeQuest/blob/master/Projects/Android/jni/QuakeQuestSrc/QuakeQuest_OpenXR.c)
and [Team Beef XR lifecycle](https://github.com/Team-Beef-Studios/QuakeQuest/blob/master/Projects/Android/jni/QuakeQuestSrc/TBXR_Common.c).

## Why previous releases were fragile

A read-only live-site audit found that the current deployment has correct HTTPS,
WASM MIME, byte ranges, COOP, COEP, and CORP. It also found four immediate
problems:

1. The 11 MB WASM is sent uncompressed even when the client accepts Brotli or
   gzip. Emscripten recommends precompressed transfer and reports substantial
   size reductions for WASM. See its [deployment guide](https://emscripten.org/docs/compiling/Deploying-Pages.html).
2. Engine JS and WASM use mutable, unversioned URLs. A partial upload can combine
   files from different Emscripten builds.
3. The browser launcher rejects the whole application when `navigator.gpu` is
   missing, so a future flat WebGL fallback cannot start.
4. The generated engine has runtime debug/assertion checks. That is useful for a
   canary build, but it is not a separately optimized production build.

There is no registered service worker on the current site, so a stale service
worker is not the present cause. The prior “worked once” behavior is more
consistent with non-atomic/mixed deployments, persistent-storage version skew,
startup races, or graphics/session failures that were not observable enough.

The current WebGPU-to-WebGL XR bridge is also a likely source of the poor VR
state. Each eye frame copies WebGPU output into a canvas and then uploads the
canvas through `gl.texSubImage2D()` to an `XRWebGLLayer`. That introduces a
full-frame cross-API synchronization/copy on the latency-critical path.

## P0: make flat web repeatable

Before another headset qualification:

- Keep the build single-threaded and omit a service worker.
- Produce separate diagnostic and optimized production builds from a pinned
  emsdk version.
- Give JS, WASM, workers, and data bundles content-hashed names. Cache hashed
  assets for one year with `immutable`; keep only HTML and the release manifest
  on `no-cache`. See [MDN's immutable caching guidance](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Cache-Control).
- Precompress JS/WASM with Brotli and gzip, preserve `application/wasm`, add
  `Vary: Accept-Encoding`, and verify streaming compilation.
- Upload a complete version directory, verify remote hashes, smoke-test that
  exact directory, then atomically switch the live pointer. Never overwrite the
  active JS/WASM pair in place.
- Retain a known-good release and make rollback a pointer swap.
- Run fresh-profile, warm-cache, and ten consecutive start/reload tests before
  promotion.
- Add a downloadable startup report containing the release ID, asset hashes,
  fetch/MIME/encoding status, WASM compile and initialization times, storage
  mount/migration results, renderer choice, first frame, and bounded errors.

Do not add pthreads until this baseline is repeatable. Emscripten requires
separate threaded and unthreaded builds rather than one binary that falls back;
see [Emscripten pthreads](https://emscripten.org/docs/porting/pthreads.html).

## P1: add the runtime Enter/Exit VR controller

1. Always start the selected game flat.
2. After native startup and first flat frame, probe `immersive-vr` and the
   presentation backend, then show a DOM **Enter VR** button.
3. Keep Enter disabled while prior session cleanup is pending. Once enabled,
   `requestSession("immersive-vr")` must be the first asynchronous platform
   request made by the trusted click; awaiting cleanup first can consume the
   transient user activation.
4. Release pointer lock on entry. Require a fresh click to recapture the mouse
   after exit.
5. During XR, change the mirrored desktop control to **Exit VR** and add an
   in-headset menu/controller exit action.
6. On the session `end` event, drain the pending native/GPU frame, clear XR
   input and haptics, retire session-owned targets, restore the flat framebuffer
   and canvas size, reset the native frame clock, then resume flat animation.
7. Preserve the WASM module, heap, game world, map, player, audio graph, storage
   mounts, and renderer device/context across the transition.

Use a player base pose plus a presentation pose. Head lean, pitch, roll, and
floor offset should not silently modify the gameplay actor. Neutralize XR input
on source loss, blur, hide, failure, and exit. Hidden XR sessions receive no
animation frames; this is normal, not a hang. See the [WebXR visibility rules](https://www.w3.org/TR/webxr/#dom-xrsession-visibilitystate).

The current audio retry listens to `selectstart`. Move it to a trusted `select`
handler and resume audio synchronously there; the [WebXR input explainer](https://immersive-web.github.io/webxr/input-explainer.html#input-events)
identifies completed `select` as the user-activating event.

## P1: separate production and experimental XR renderers

The production target should be a long-lived WebGL2 context used for both flat
rendering and direct `XRWebGLLayer` rendering. Call `makeXRCompatible()` at XR
entry, render directly to the compositor framebuffer inside XR animation
callbacks, and avoid the WebGPU-canvas-WebGL copy. This follows the standard
[WebXR WebGL compatibility path](https://www.w3.org/TR/webxr/#webgl-context-compatibility).

Keep both existing alternatives explicitly experimental:

- the WebGPU-to-WebGL bridge is a diagnostic compatibility path;
- direct `XRGPUBinding` is gated by runtime probes and named browser/headset
  qualification because the [WebXR/WebGPU binding](https://immersive-web.github.io/webxr-webgpu-binding/)
  remains an unstable draft.

Handle `webglcontextlost`/restoration and WebGPU `device.lost` as first-class
states. Old WebGL resources are invalid after restoration; see the
[WebGL context-loss rules](https://registry.khronos.org/webgl/specs/latest/1.0/).

## Electron's role

Electron is a pinned diagnostic shell and a valid flat package, not proof of
desktop-headset support. Its default mode should use core WebXR and a named
OpenXR runtime. Experimental WebGPU-WebXR feature gates belong behind a separate
flag and must be recorded in diagnostics. Chromium's desktop OpenXR support has
runtime and sandbox compatibility requirements documented in its
[XR architecture notes](https://chromium.googlesource.com/chromium/src/+/HEAD/device/vr/README.md).

Before debugging Surreal's immersive renderer in Electron, run an official
Immersive Web sample in the same Electron, Chromium, headset, and OpenXR runtime.
If that sample cannot obtain `immersive-vr`, the wrapper/runtime boundary is the
blocker.

## Implementation work packages

Execute these in order. A package does not start until the previous package's
acceptance gate and evidence manifest are preserved.

### WP0 — Reproducible baseline and build identities

- Pin the emsdk/Emscripten version and record all CMake/compiler/linker flags.
- Produce a diagnostic build with assertions and an optimized production build
  from the same source commit.
- Record build ID, source tree, dependency commits, tool versions, sizes, and
  SHA-256 values in the release manifest.
- Add startup milestones and the downloadable privacy-bounded diagnostic report.

Gate: both profiles build from a clean tree; the flat diagnostic build completes
ten fresh/warm starts with identical asset identities and no proprietary data.

### WP1 — Immutable package and atomic hosting

- Content-hash engine JS, WASM, workers, and future data bundles.
- Generate and test Brotli/gzip variants while retaining correct MIME headers.
- Teach the release packager and smoke tests to reject missing, mixed, unhashed,
  or mismatched artifacts.
- Upload only to a new version directory, verify remote hashes, then promote by
  an atomic pointer swap with one-command rollback.

Gate: interrupted staging cannot affect live; cold and warm remote tests load one
manifest generation; rollback restores the prior tested generation.

### WP2 — Production flat renderer baseline

- Keep existing flat WebGPU available, but remove `navigator.gpu` as a gate for
  the whole application.
- Implement and qualify a WebGL2 render device/context that can remain alive for
  both ordinary canvas rendering and later `XRWebGLLayer` presentation.
- Make backend selection explicit and diagnostic; never silently reinterpret a
  failed WebGPU initialization as success.
- Add WebGL context-loss and WebGPU device-loss tests before XR depends on them.

Gate: the same game data reaches a playable flat first frame in the supported
WebGL2 baseline and WebGPU profile, including reload, resize, pointer lock, audio,
save, and restore tests.

Progress, 2026-07-24: the opt-in WebGL2 backend now renders the UT99 demo's
DM-Turbine scene through the engine's real complex-surface, Gouraud, tile, line,
texture, blend, depth, and lightmap paths. Automated Chrome evidence records
345+ draws per frame, 150+ restored textures, zero unsupported draws, zero
unexpected GL errors, and nonblank frames before and after forced context loss;
the same WASM tick stream continued across generation 1 to 2. This satisfies the
representative first-frame and resource-rebuild milestone, not the complete WP2
gate: reload, resize, pointer-lock, audio-activation, save/restore, broader map,
and WebGPU comparison qualification remain pending before WebGL2 is advertised
or shipped as the production default.

Progress, 2026-07-24 (flat lifecycle increment): the running UT99 demo now
passes a single-browser test covering trusted-gesture audio activation, native
relative mouse motion, pointer-lock release and reacquisition across an XR-style
transition, a 640x480 to 960x540 live resize, and forced WebGL context loss and
restoration. SDL window geometry, the Emscripten canvas backing store, and the
UE1 viewport resize through one native transaction, so the resized and restored
frames fill the complete buffer rather than retaining black bands around a stale
640x480 scene. The shared production browser launcher also observes CSS,
device-pixel-ratio, window, and fullscreen geometry and calls that transaction
without restarting the module. Automated evidence records uninterrupted ticks,
zero unsupported draws or unexpected GL errors, generation 1 to 2 resource
recovery, and 99.8% nonblank frames before and after restoration. Reload,
save/restore, broader-map, sustained-performance, and WebGPU comparison gates
remain pending.

Progress, 2026-07-24 (flat matrix increment): a repeatable Chrome matrix now
creates a fresh WASM runtime across consecutive reloads, checkpoints an
allowlisted mutable settings sentinel and verifies its exact bytes after a full
page/runtime reload, captures multiple maps, and samples live frame/tick, WASM
heap, texture, unsupported-draw, and GL-error counters. The exploratory UT demo
run rendered both DM-TurbineDEMO and DOM-SesmarDEMO coherently (345 and 736
draws per sampled frame), restored the sentinel, held the 256 MiB heap and
texture count constant, and sustained roughly 31 frames/ticks per second in
headless Chrome. Four other maps are explicitly classified as fixture
exclusions because this standalone demo directory lacks `UnrealI.u`; they fail
before renderer creation with `Could not find package UnrealI`, rather than
being counted as renderer passes. A clean 10-reload/30-second run and the
WebGPU comparison remain pending.

### WP3 — Post-launch Enter/Exit VR state machine

- Change WebXR from a pre-launch presentation choice to a post-launch controller.
- Implement the explicit `FlatRunning`, `RequestingSession`, `ActivatingXR`,
  `ImmersiveRunning`, and `EndingXR` UI/runtime states.
- Add DOM Enter/Exit controls, an in-headset exit action, bounded errors, and
  provider readiness signaling.
- Assert that module, heap, map, player, renderer identity, audio graph, and
  storage mounts survive every transition.

Gate: mocks cover success, denial, setup failure, runtime end, repeated re-entry,
and stale async completion without a reload or second `main()` call.

### WP4 — Transition lifecycle correctness

- Make `requestSession()` the first platform request of an enabled Enter click;
  never await pending cleanup while consuming transient activation.
- Move audio retry to trusted XR `select` and preserve one audio graph.
- Add a native frame-clock reset at scheduler handoff and visibility recovery.
- Neutralize controller state/haptics, release/reacquire pointer lock correctly,
  restore flat viewport/canvas state, and clamp transition deltas.

Gate: transition tests detect no duplicate tick, large time step, stuck input,
audio restart, unwanted pointer capture, or stale XR render target.

### WP5 — Production immersive renderer

- Use the long-lived WebGL2 context with `makeXRCompatible()` and render directly
  to `XRWebGLLayer` inside XR animation callbacks.
- Run one simulation update followed by both eye views; never tick once per eye.
- Label the WebGPU-to-WebGL copy bridge diagnostic-only.
- Keep direct `XRGPUBinding` behind a named experimental runtime flag and a
  separate qualification result.

Gate: physical hardware presents current-pose stereo frames without the
cross-API full-frame copy; failed setup returns to uninterrupted flat play.

### WP6 — Storage, recovery, and upgrade behavior

- Version persistent schemas and validate imported immutable content separately
  from saves/settings.
- Preserve saves while discarding only corrupt derived caches.
- Handle WebGL context restoration and WebGPU device loss with complete resource
  rebuilding or an explicit controlled restart.
- Test previous-release upgrades, quota failures, interrupted imports, and
  origin/storage changes.

Gate: no upgrade or recovery path reports success with missing/corrupt data, and
the user can clear derived data without deleting saves.

### WP7 — Browser, Quest, OpenXR, and Electron qualification

- Automate flat, lifecycle, failure-injection, repeat-entry, and remote artifact
  checks with dated manifests.
- Run the physical matrix on named Quest Browser and desktop OpenXR combinations.
- Run an official WebXR sample first in every Electron/headset/runtime tuple.
- Keep Electron core WebXR as default and WebGPU-WebXR incubation opt-in.
- Publish only the modes that pass their own matrix; keep all other modes marked
  experimental in both UI and release notes.

Gate: the full matrix below passes, evidence is preserved, the production pointer
is promoted atomically, and rollback is tested after promotion.

## Release gates for VR

A candidate is not qualified until physical hardware passes all of these:

- play flat for at least 60 seconds, then enter without changing game state;
- exit through both game UI and headset/system UI and resume the same flat game;
- repeat enter/exit ten times without resource growth or duplicate simulation;
- deny entry, obscure/sleep the headset, disconnect a controller, and force
  session end without interrupting flat play;
- verify no large simulation delta on any handoff;
- verify audio position, neutral input, pointer-lock recovery, and saved data;
- compare the official WebXR sample, the hosted build in Quest Browser, and the
  pinned Electron build separately;
- record renderer path, browser/runtime versions, first submitted XR frame,
  skipped frames, visibility transitions, and frame-time percentiles.

Mock sessions prove the state machine. Only a named physical browser, headset,
and runtime can qualify presentation.
