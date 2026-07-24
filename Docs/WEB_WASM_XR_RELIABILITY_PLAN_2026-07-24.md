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

The prior audio retry listened to `selectstart`. Move it to a trusted `select`
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

Progress, 2026-07-24 (immutable hosting transaction): commit
`fa4f61456aa08e520a5e10b93fb2e387004c3450` adds exact v2-manifest
verification, durable scoped locks/journals/completion records, immutable public
generations keyed by the full manifest SHA-256, and an atomic stable `.htaccess`
redirect with pointer-only rollback. The first initialization retains the old
unversioned payload for in-flight compatibility. Synthetic HTTP boundary tests
prove that old A resources remain available while fresh navigation selects only
complete A or B targets; Windows and native-Linux crash/recovery suites pass.
The exact `ff89e9d1` candidate also passed the flat browser smoke through its
expected manifest-hash redirect and was rolled back to the exact `8b93cf60`
generation while the candidate remained addressable. Evidence is in
`SurrealEngine/qa/runs/2026-07-24/fa4f6145/wp1-versioned-release/manifest.json`.
The remaining WP1 gate is actual Apache 2.4 staging/remote validation of rewrite,
headers, MIME, compression, permissions, and pointer rollback; the Node host is
not accepted as a substitute.

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

Progress, 2026-07-24 (flat performance/comparison increment): the first direct
comparison found WebGL2 stable but limited to 29.2 ticks/s while WebGPU reached
59.6 on the same 640x480 Turbine scene. The WebGL2 path was reallocating and
uploading vertex and index buffers for every ordered draw. It now preserves
draw order, depth, blend, texture, viewport, and matrix boundaries while
aggregating CPU geometry; a representative 345-draw frame requires four buffer
submission batches rather than roughly 690 per-draw buffer uploads. The
post-change comparison measured 60.1 WebGL2 ticks/s and 59.8 WebGPU ticks/s,
with coherent frames and zero backend errors on both. Resize, pointer-lock,
audio, and forced context-restoration tests still pass through the aggregated
path.

WP2 closed, 2026-07-24: clean commit `2723fe44972a0edecc407c77c2373071ea92f618`
passed 10/10 full browser/WASM reloads, exact mutable-settings persistence, a
30.165-second sustained run at 60.0 frames/ticks per second, stable 256 MiB heap
and texture counts, two rendered fixture maps, and explicit pre-render
classification of four maps excluded by the demo's missing `UnrealI.u`. The
clean WebGL2/WebGPU comparison measured a 1.002 tick-rate ratio and 1.3247/255
mean absolute channel difference with zero backend errors. The combined,
hash-verified evidence manifest is
`SurrealEngine/qa/runs/2026-07-24/2723fe44/wp2-flat-complete/manifest.json`.

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

Progress, 2026-07-24: production launch now registers only the flat presentation
and reveals a separate WebXR session panel after native startup. A controller
owns the five explicit states above, calls `requestSession()` synchronously from
the Enter action, activates the reserved session against the existing module,
waits for provider/GPU cleanup on exit, and returns denial or setup failure to a
retryable flat state. Provider readiness events include cleanup and blocked-end
admission state. Deterministic tests cover success, denial, activation failure,
system end, repeated entry on one module, and a superseded async completion;
the browser launcher and immutable package tests also pass. Mock sentinels now
assert heap, map/player, renderer, audio graph, and mount identity and prove no
second `main()` call across transitions. Remaining WP3 work is the in-headset
exit action and equivalent evidence from a live runtime transition harness.

WP3 gate closed, 2026-07-24: the headset-rendered UE1 menu's existing
Quit/Exit action now emits a browser session-exit request while WebXR owns the
frame loop, returning to the same flat game instead of terminating WASM; flat
Quit semantics are unchanged. A real WebGL2 UE1/WASM continuity harness, with
only the unavailable headless browser XR-session boundary mocked, exercised
flat, entry, exit, re-entry, and the game-menu exit route. Engine, loaded
level, player pawn, renderer, audio device, module, data controller, and audio
controller identities remained equal; heap size stayed 256 MiB; ticks advanced
7 to 70 to 131; WebGL remained generation 1 with 164 textures and zero errors;
and no second `main()` call occurred. This closes state-machine continuity, not
physical headset presentation qualification.

### WP4 — Transition lifecycle correctness

- Make `requestSession()` the first platform request of an enabled Enter click;
  never await pending cleanup while consuming transient activation.
- Move audio retry to trusted XR `select` and preserve one audio graph.
- Add a native frame-clock reset at scheduler handoff and visibility recovery.
- Neutralize controller state/haptics, release/reacquire pointer lock correctly,
  restore flat viewport/canvas state, and clamp transition deltas.

Gate: transition tests detect no duplicate tick, large time step, stuck input,
audio restart, unwanted pointer capture, or stale XR render target.

Progress, 2026-07-24: the provider now forwards audio retry only from a trusted
completed XR `select`, removing its prior `selectstart` listener on every exit.
Frame-loop ownership changes reset the native elapsed-time clock before either
scheduler resumes. XR focus recovery requests the same reset; when Asyncify
native work is suspended, the reset is deferred until the native-call gate
reopens. Deterministic tests cover immediate and deferred recovery without
native re-entry.

WP4 closed, 2026-07-24: clean commit `e752ac8cb347bab6ef90797bcdecc548076863e1`
passed the live scheduler handoff gate. Flat RAF remained paused for one second
while XR owned the loop (tick 133 stayed 133), then resumed at tick 148 with a
maximum 17,655 microsecond delta rather than consuming teardown time as a game
step. Runtime, level, player, renderer, audio, module, heap, WebGL generation,
and texture identities remained stable. The hash-verified evidence manifest is
`SurrealEngine/qa/runs/2026-07-24/e752ac8c/wp4-lifecycle-handoff/manifest.json`.

### WP5 — Production immersive renderer

- Use the long-lived WebGL2 context with `makeXRCompatible()` and render directly
  to `XRWebGLLayer` inside XR animation callbacks.
- Run one simulation update followed by both eye views; never tick once per eye.
- Label the WebGPU-to-WebGL copy bridge diagnostic-only.
- Keep direct `XRGPUBinding` behind a named experimental runtime flag and a
  separate qualification result.

Implementation gate: deterministic and live-engine tests prove that the shared
context renders both current-pose eyes directly into one compositor framebuffer
inside XR RAF, performs no cross-API copy, advances simulation only in the
asynchronous prepare phase, restores the prior framebuffer, and resumes the
same flat runtime after exit/failure. Physical presentation remains a separate
WP7 promotion gate.

WP5 closed, 2026-07-24, at clean commit
`5db4b8159b64d168ed907af312ae9db629acd4c8`: production `auto` now selects
`direct-webgl2` only when
the running engine owns a live WebGL 2 context. Entry calls
`makeXRCompatible()`, creates a fresh session-owned `XRWebGLLayer`, and uses its
actual two eye viewports as a shared ABI-v4 atlas. The async phase performs the
single game update; the current XR callback binds the opaque browser
framebuffer, synchronously renders both views through the existing WebGL 2
device, and restores the prior read/draw bindings before returning. Flash and
direct HUD/menu passes are applied while each eye viewport is selected. WebGPU
XR requires the explicit `surrealXRExperimentalWebGPU` flag; the
WebGPU-to-WebGL copy bridge is available only when forced for diagnostics. A
live UT99 demo test with only the headless browser
session/layer/compatibility boundary mocked rendered a real 1024x512 stereo
target: 846 native draws, eight submissions, 2,078,802 nonzero bytes split
across both eyes (1,039,396 left and 1,039,406 right), zero WebGL errors, one
update before the render boundary, no update during synchronous stereo
rendering, stable engine/renderer identities, and successful return to
advancing flat frames. This is strong implementation evidence, not
physical-headset qualification.

The clean live evidence is
`SurrealEngine/qa/runs/2026-07-24/5db4b815/wp5-direct-webgl2/manifest.json`.

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

Electron diagnostic-package progress, 2026-07-24: wrapper commit
`8fcefa4d5a0ca928bdebc7b8db377c7bbcaf11cf` packages the clean, data-free
WebGL2 browser build from `af790cceabbd0aa40cde91016f83c2096c4d978c`
with Electron 43.2.0. The package pins and verifies every browser-release file,
uses a stable single-instance loopback origin, applies restrictive response
headers and renderer permissions, enforces ASAR integrity and hardened Electron
fuses, and records a complete package inventory plus ZIP hash. Packaged-runtime
automatic, forced-OpenXR, and experimental-WebGPU-XR diagnostics pass their
non-immersive gates; strict immersive capability correctly fails because no
headset/runtime was attached. The evidence manifest is
`SurrealEngine/qa/runs/2026-07-24/8fcefa4d/wp6-electron-diagnostic/manifest.json`.
This package remains unsigned and internal, and it does not close any named
physical WebXR matrix row.

Production-browser candidate progress, 2026-07-24: clean production build
`ff89e9d1aec186a929545a7b571954c6c4a3125e` is frozen as build
`ff89e9d1aec1-34c387709c89fff4` with direct WebGL2, Asyncify/OPFS, zero
assertions, 52 manifest payloads, and no bundled game data. The staged Chrome
gate passed immutable hashes and compression, launcher/import readiness,
WebGL2 flat launch, fullscreen, input, responsive layout, and zero page errors.
The live stable site remains the older `8b93cf60dfb2-3a67ac2ff50e7191`
generation; it was intentionally not promoted without hardware. Current
artifact identities and the required Q1/D1/E1/E2 rows are in
`Docs/WEBXR_PHYSICAL_QUALIFICATION_FF89E9D1.md`; automated evidence is in
`SurrealEngine/qa/runs/2026-07-24/ff89e9d1/wp7-production-candidate/manifest.json`.

Direct-UI candidate progress, 2026-07-24: clean commit
`0cb8a55bd2fb71d56c2034603e9c29e91452f10e` supersedes the automated
candidate above as build `0cb8a55bd2fb-2c33259ba0d3e82d`. Direct WebGL2 now
uses the same compact, UI-scale-aligned eye rectangles for rendering and hit
testing; presents the UE1 menu in both eyes; draws the controller ray/hit
feedback in both eyes; delivers a successful direct-menu trigger exactly once;
and clears pointer/visual diagnostics on exit before re-entry. The live UE1
smoke preserved one engine and renderer through flat -> VR -> flat -> VR while
proving nonzero menu pixels in both eyes, a menu hit, trigger selection, and
stereo view mask `3`. The frozen data-free browser candidate, matching source
archive, and hardened Electron 43.2.0 diagnostic ZIP pass their automated
gates. Exact identities and remaining physical rows are in
`Docs/WEBXR_PHYSICAL_QUALIFICATION_0CB8A55B.md`; evidence is in
`SurrealEngine/qa/runs/2026-07-24/0cb8a55b/wp7-production-candidate/manifest.json`.
Actual Apache staging and named Quest/OpenXR rows remain open, so the live
stable pointer has not been changed.

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
