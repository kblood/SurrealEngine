# Runtime release-blocker analysis

## Scope and method

This is a read-only second-opinion analysis of the three blockers found while
physically testing browser candidate `cef1e89b` on Quest 3 through Virtual
Desktop and VDXR. No runtime source was changed during this analysis.

Claude Code 2.1.218 was invoked with the exact model identifier
`claude-opus-4-8`. The CLI response confirmed that canonical first-party model.
One broad maximum-effort pass was stopped after its bounded ten-minute window
without a result. Three narrower high-effort, read-only passes then completed:

- WebXR negotiation and immediate immersive exit: 196 seconds;
- browser pointer lock without relative mouse-look: 277 seconds;
- WasmFS mutable-save omission: 132 seconds.

The findings below combine those completed reports with a manual consistency
check against the repository rules and the WebXR/WebGPU same-session
constraint. File line numbers refer to the inspected `cef1e89b`-era tree and
may move as fixes land.

## Executive assessment

The three failures are independent and should be fixed in separate commits.
None calls for a broad renderer or engine refactor.

1. **Automatic WebXR negotiation is the highest-risk release blocker.** It
   chooses direct WebGPU from weak pre-session signals, then requires the
   `webgpu` session feature. The provider never inspects
   `XRSession.enabledFeatures`. VDXR may therefore show consent and then reject
   the request, or accept the request and fail during WebGPU binding/layer
   activation. The exact stage remains a hypothesis until the privacy-safe
   headset report is captured.
2. **Flat browser mouse-look is most likely severed between browser relative
   motion and SDL's `xrel`/`yrel`.** The custom JavaScript path obtains pointer
   lock without enabling SDL relative mode. Buttons use a different path and
   work. The engine's raw-mouse consumer is intact. A small, Emscripten-only
   browser-to-native relative-delta bridge is more deterministic than relying
   on undocumented SDL/Emscripten lock-state synchronization.
3. **UT/Unreal save omission is a confirmed control-flow bug.**
   `FS.analyzePath` is treated as authoritative even when it throws or returns
   false, so the working `FS.stat` fallback is never tried. The save directory
   is then skipped before `readdir`. This has a minimal JavaScript fix and a
   direct regression test.

Recommended order is save persistence first, mouse delivery second, and WebXR
negotiation third. That order gives two deterministic fixes quick automated
closure while preserving WebXR as its own reviewable state-machine change.
Physical qualification should then test flat mouse/save behavior before the
headset session, followed by Automatic and forced bridge presentation.

## Blocker 1: Automatic WebXR exits immediately after consent

### Observed behavior

On Quest 3 through Virtual Desktop with VDXR as the PC OpenXR runtime, desktop
Chrome presents and grants the WebXR permission prompt. Automatic presentation
then leaves immersive mode immediately and the engine continues flat. Synthetic
Chrome tests pass both Automatic/direct WebGPU and forced WebGL bridge.

The physical v2 diagnostics report was not captured, so it is not yet known
whether failure occurred during session request or later binding/layer
activation. This distinction matters.

### Confirmed code path

Before a session exists, capability detection in
`web/webxr_provider.js:883-911` calls direct WebGPU available when:

- `XRGPUBinding` exists;
- a WebGPU device exists; and
- `surrealWebGPUDeviceXRCompatible` is true.

`web/browser_app.js:453-470` and the WebXR page bootstrap set that final flag
when `requestAdapter({ xrCompatible: true })` returns an adapter. This is a weak
signal: WebIDL implementations may ignore an unrecognized dictionary member,
so a normal flat WebGPU adapter can make the flag true.

`surrealXRRequestSession` in `web/webxr_provider.js:1018-1103` then:

1. computes direct and bridge availability;
2. chooses `direct-webgpu` before requesting a session whenever the weak direct
   signals are true;
3. requests `requiredFeatures: ["webgpu"]` for direct mode, or omits WebGPU for
   bridge mode; and
4. reserves the returned session for later activation.

The code does not inspect `session.enabledFeatures`.

During direct activation (`web/webxr_provider.js:1110-1146`) it constructs
`XRGPUBinding`, obtains a preferred format, creates a projection layer with
`COPY_DST`, and calls `updateRenderState({ layers: [...] })`. A throw becomes a
binding or projection-layer failure. The common failure path records an
allowlisted stage/code, tears down provider state, ends the session, and resumes
flat execution. That cleanup behavior matches the physical symptom.

### Ranked causes

**Confirmed facts**

1. Automatic hard-requires `webgpu` when the pre-session signals prefer direct.
2. Those signals do not prove that the immersive session supports WebGPU.
3. The provider never uses `enabledFeatures` to negotiate after consent.
4. The synthetic session mock accepts requested features unconditionally, so it
   cannot reproduce a runtime that declines WebGPU.
5. Direct activation failures deliberately end the session and return flat.

**Hypotheses requiring the physical report**

1. Most likely, VDXR exposes enough surface API to pass preflight but does not
   enable the required WebGPU session feature. `requestSession` rejects after
   consent, producing `session-request-failed` at the requesting-session stage.
2. Also plausible, the session enables WebGPU but `XRGPUBinding` construction,
   projection-layer creation, or `updateRenderState` fails. This should report
   creating-binding or creating-projection-layer.

### Candidate fix

Make Automatic negotiate from the created session instead of finalizing the
backend from preflight:

- **Forced WebGL bridge:** request `local-floor` only. Do not request WebGPU as
  required or optional.
- **Automatic with direct and bridge candidates:** request `local-floor` and
  `webgpu` as optional features. After the session is returned, inspect
  `session.enabledFeatures` before reserving it.
  - If `webgpu` is enabled, select `direct-webgpu`.
  - If it is not enabled, select `webgl-bridge`.
- **Automatic with only direct available:** keep `webgpu` required.
- **Automatic with only bridge available:** omit WebGPU and select the bridge.
- **Future explicit forced-direct mode:** require WebGPU.

Presentation mode should remain unresolved in provider status until the session
feature set resolves it. The adapter can still reserve the session during the
trusted Play gesture and activate later.

If `enabledFeatures` is absent or cannot be inspected in the auto/both path,
the strict safe behavior is to end that session with an allowlisted
`feature-negotiation-unobservable` error and invite a new trusted gesture using
forced bridge. Selecting WebGL merely because `enabledFeatures` is missing
could be illegal if WebGPU was silently enabled. This is a deliberately
conservative refinement to the generated analysis.

Add the allowlisted exact `lastErrorCode` to the privacy-safe diagnostics report
as a token. Continue representing arbitrary exception text only as
present/absent.

### Non-negotiable spec constraint

When a session has the `webgpu` feature enabled, it must stay on the WebGPU XR
layer path. Do not create an `XRWebGLLayer`, set a WebGL `baseLayer`, or attempt
a bridge fallback in that same session. If direct binding or projection setup
fails after WebGPU was enabled, end the session. Recovery requires a new trusted
gesture and a newly requested forced-bridge session that does not request
WebGPU.

The existing `COPY_DST` projection-texture usage remains necessary for the
renderer copy path and should not be removed as a compatibility workaround.

### Focused tests

Extend the provider and bridge tests with sessions that expose realistic
`enabledFeatures` and record request options:

1. Auto/both with WebGPU enabled requests it optionally, chooses direct,
   constructs `XRGPUBinding`, and never creates a WebGL base layer.
2. Auto/both without WebGPU enabled chooses the bridge, never constructs
   `XRGPUBinding`, and never submits a projection-layer array.
3. Auto/both with missing `enabledFeatures` ends cleanly without creating either
   layer type and reports the allowlisted negotiation code.
4. Direct-only requires WebGPU.
5. Forced bridge contains WebGPU in neither required nor optional features.
6. Direct activation failure ends the session and never constructs
   `XRWebGLLayer` in that session.
7. A shared invariant asserts that one session never observes both WebGPU
   binding construction and WebGL base-layer assignment.
8. Diagnostics include the allowlisted provider code and exclude raw exception
   text or user/game data.

### Physical gate and risks

Capture the v2 report before reload on the currently failing candidate. After
the fix, Automatic should select the bridge on VDXR when WebGPU is not enabled
and produce nonzero submitted bridge frames. Separately test forced bridge,
stereo, tracking, menu quads, exit, and re-entry.

Residual risk remains that a runtime reports WebGPU enabled but fails binding or
projection later. The correct response is still clean session termination plus
an actionable forced-bridge retry, never same-session fallback.

## Blocker 2: pointer lock works but relative mouse-look does not

### Observed behavior

The exact game canvas acquires pointer lock. The cursor is captured and mouse
buttons reach gameplay, including fire. Moving the mouse does not rotate the
view. This narrows the defect to relative motion rather than gesture permission,
the event pump as a whole, or gameplay input generally.

### Confirmed code path

On Emscripten, `SDL2DisplayWindow::LockCursor` in
`SurrealWidgets/src/window/sdl2/sdl2_display_window.cpp:186-208` does not call
`SDL_SetRelativeMouseMode`. It sets the window's own `CursorLocked` flag and
uses `MAIN_THREAD_EM_ASM` to call
`SurrealBrowserPointerLock.setRequested(true)`. Only the non-Emscripten branch
calls `SDL_SetRelativeMouseMode(SDL_TRUE)`; the same split exists on unlock.

`web/pointer_lock_gesture.js:130-164` acquires the actual browser lock by calling
`canvas.requestPointerLock()` from a trusted gesture. SDL did not request that
lock and is not explicitly told that its relative mode became active.

SDL motion dispatch reaches `OnMouseMotion` in
`sdl2_display_window.cpp:644-654`. When the window's independent `CursorLocked`
flag is true, it forwards `event.xrel`/`event.yrel` to
`WindowHost->OnWindowRawMouseMove`.

`Engine::OnWindowRawMouseMove` in `SurrealEngine/Engine.cpp:2234-2244`
accumulates the deltas. The frame then drains them into `IK_MouseX` and
`IK_MouseY` axis events (`Engine.cpp:2160-2169`). UWindow only consumes raw
motion first when a visible/modal cursor requires it. The in-game consumer path
is therefore intact.

The browser native-call gate explicitly snapshots `movementX` and `movementY`
on `mousemove` events (`web/browser_native_call_gate_library.js:17-25`), proving
that the browser-facing machinery recognizes relative fields. It does not prove
that SDL converts them to nonzero `xrel`/`yrel` after an out-of-band lock.

### Ranked causes

**Confirmed facts**

1. Emscripten obtains pointer lock outside SDL and never enables SDL relative
   mode in repository code.
2. Raw gameplay motion uses SDL `xrel`/`yrel`, not browser `movementX`/`movementY`
   directly.
3. Fire working proves button dispatch and the gameplay loop are alive.
4. The engine motion accumulator is straightforward and platform-neutral.

**Hypotheses**

1. Leading: SDL's Emscripten backend only maps browser relative deltas into
   `xrel`/`yrel` when SDL believes relative mode/pointer lock is active. Because
   the custom path bypasses that state, absolute coordinates remain stationary
   under browser lock and SDL reports zero relative motion.
2. `PROXY_TO_PTHREAD` may deepen the state split: pointer-lock/document state is
   on the browser main thread while SDL/game processing is on the application
   pthread.
3. Virtual Desktop could inject absolute movement with zero browser
   `movementX`/`movementY`. A plain desktop Chrome control run is required to
   distinguish this runtime behavior from the engine bridge defect.

The SDL/Emscripten implementation is not vendored here, so the first two are
strongly supported but not proven from repository source.

### Candidate fix

Use the same small browser-to-engine pattern already used for forwarded Escape
intent, scoped only to Emscripten:

1. Add a capture-phase `mousemove` listener beside the pointer-lock gesture
   handlers. Forward `event.movementX` and `event.movementY` only when the exact
   canvas is locked, capture is requested, XR is inactive, and native calls are
   not blocked.
2. Export `Surreal_ForwardBrowserMouseMotion(int dx, int dy)`. Its body only
   atomically adds the two deltas, making the main-thread call safe under the
   existing pthread architecture in the same manner as forwarded Escape.
3. Once per engine frame, exchange both accumulators to zero and pass a nonzero
   sum through the existing `OnWindowRawMouseMove` path.
4. When that bridge is installed, suppress the Emscripten SDL raw-motion forward
   while locked so a future SDL backend change cannot double-count the same
   browser event. Keep all native SDL behavior unchanged.

The direct exported call should not use the JSEvents coalescing queue, which
replaces coalesced events rather than summing relative deltas. Each browser
delta must be atomically accumulated. Dropping movement while native calls are
blocked is acceptable during XR/presentation transitions; input resumes after
recapture.

An even narrower diagnostic experiment could call `SDL_SetRelativeMouseMode`
after the custom browser lock becomes active. It should not be the release fix
without verifying the exact Emscripten SDL behavior: it may issue another
asynchronous lock request, proxy unexpectedly, or still leave worker/main-thread
state split. The explicit delta bridge has deterministic ownership and tests.

### Focused tests

1. With the exact canvas locked, dispatch two synthetic mouse moves and verify
   forwarded deltas are summed, including negative Y.
2. Verify no forwarding without lock, when capture is not requested, while XR
   is active, and while native calls are blocked.
3. Verify forwarded deltas drain exactly once per frame into the existing raw
   consumer and reset to zero.
4. Verify the SDL Emscripten path does not also deliver the same delta after the
   browser bridge is active.
5. Retain pointer-lock gesture, Escape forwarding, release, and post-XR recapture
   tests.
6. Run normal native input tests to prove the non-Emscripten relative-mode path
   is unchanged.

Physical validation needs both plain desktop Chrome and Quest/Virtual Desktop.
Capture, move, rotate, press Escape, recapture, and rotate again. If plain Chrome
works but Virtual Desktop still reports zero browser deltas, the remaining issue
is Virtual Desktop input injection rather than SDL translation. Add only
allowlisted counters for received browser motion events/nonzero deltas if
diagnostics are needed; never record user movement streams.

### Risks and non-goals

- Double counting is the main implementation risk; the Emscripten SDL source
  must be disabled or explicitly de-duplicated when the bridge owns motion.
- Atomic integer overflow is practically bounded by per-frame exchange, but
  tests should cover multiple events per frame.
- Sensitivity, acceleration, DPI scaling, and Virtual Desktop's own injection
  policy are not changed. Raw deltas preserve current desktop semantics.
- Native desktop, gameplay bindings, and XR controller aiming are non-goals and
  must remain untouched.

## Blocker 3: WasmFS omits real `.usa` saves from checkpoints

### Observed behavior and confirmed root cause

In the shipping WasmFS owner-data test,
`FS.analyzePath('/gamedata/Save')` throws while `FS.stat`, `FS.readdir`, and file
reads work. INI, settings, and log files persist, but real `Save<N>.usa` files do
not.

`fsExists` in `web/mutable_persistence.js:499-505` currently returns immediately
from `FS.analyzePath(path).exists` whenever `analyzePath` exists. Its `FS.stat`
fallback is reachable only when `analyzePath` is absent. A thrown exception is
caught by the outer catch and converted to false. An `exists: false` result also
returns false without trying stat.

`snapshotFS` at `web/mutable_persistence.js:520-536` calls that helper before
`FS.readdir('/gamedata/Save')`. False short-circuits enumeration, so no save path
can reach the existing filename allowlist, stat-based file check, or read.
Exact mutable INI/settings/log paths are seeded independently, explaining the
partial success.

`web/ut99_importer.js:922-930` contains a separate helper with the same
analyzePath-first control flow. It is not involved in this exact checkpoint
failure, but it carries the same latent WasmFS false-negative risk.

The current fake filesystem always makes `analyzePath` succeed. The native-call
gate test models a genuinely empty filesystem instead of throwing
`analyzePath` plus working stat/readdir. No existing test represents the owner
failure.

### Candidate fix

Make `analyzePath` a positive fast path, never a negative veto:

```js
function fsExists(FS, path) {
    if (typeof FS.analyzePath === "function") {
        try {
            if (FS.analyzePath(path).exists) return true;
        } catch (_) {
            // WasmFS can throw here; stat remains authoritative.
        }
    }
    try {
        FS.stat(path);
        return true;
    } catch (_) {
        return false;
    }
}
```

Thus both an `analyzePath` throw and an `exists: false` result fall through to
stat. A genuinely absent optional save directory still returns false when stat
throws. Do not add catches around `readdir` or `readFile`: unexpected failures
after the directory is known to exist must fail the checkpoint visibly rather
than silently omit data.

Apply the same shape to the importer helper in a separate commit only if its
public behavior is covered with a focused test. Keeping that latent correction
separate makes the release-blocker patch minimal and upstream-reviewable.

The existing per-file `fsIsFile` still converts all stat exceptions to false.
That is a residual silent-omission risk after `readdir` has listed a save, but it
was not observed in the owner matrix. Do not broaden this release fix without a
reproduction and errno/error-classification design.

### Focused tests

Add a WasmFS-like fake whose `analyzePath` always throws but whose stat,
readdir, and readFile work. Seed four exact mutable files, a real
`/gamedata/Save/Save99.usa`, and a disallowed package in the Save directory.
Assert:

1. snapshot enumeration includes `Save99.usa`;
2. the commercial package remains excluded;
3. the expected file count is exact;
4. flush and restore round-trip the save contents; and
5. `analyzePath` returning false while stat succeeds also cannot veto the save.

Retain a true-missing-directory case where both mechanisms indicate absence.
Retain the native-call gate. If the importer duplicate is fixed, add its own
throwing-analyzePath game-detection test.

Run the mutable browser suite, native-call gate, persistence smoke test, and the
real owner-data matrix. The manual gate is a UT99 and Unreal `.usa` write,
checkpoint, reload, and restore under the shipping Asyncify/WasmFS artifact.

### Risks and non-goals

- The fallback adds one stat call only when `analyzePath` is negative or throws;
  checkpoint cost is negligible.
- Missing optional directories remain harmless.
- Nested Deus Ex `SaveNNNN/*.dxs` layout is not solved by this flat UT/Unreal
  allowlist correction.
- Abrupt browser termination guarantees and arbitrary game-data persistence are
  out of scope.

## Commit decomposition

Keep implementation and proof in small, independently reviewable commits:

1. **Fix WasmFS mutable directory fallback** — change only the persistence
   helper and its throwing/false `analyzePath` regressions.
2. **Harden importer WasmFS existence fallback** — optional separate commit with
   a direct importer regression.
3. **Bridge browser relative mouse deltas** — Emscripten-only JS/native bridge,
   explicit SDL de-duplication, focused tests, and no native behavior changes.
4. **Negotiate the WebXR backend from enabled features** — provider state
   machine plus direct/bridge invariant tests.
5. **Expose the allowlisted provider error code** — may accompany WebXR
   negotiation if inseparable from qualification, otherwise a tiny diagnostics
   commit.
6. **Record qualification evidence** — documentation-only after automated and
   physical results exist; do not claim success in advance.

These product-integration commits should not be submitted upstream wholesale.
Any upstream candidate must be reconstructed as a focused current-upstream fix
with a reproduction, line-by-line human understanding, and its own manual
evidence as required by `CLAUDE.md`.

## Combined validation gate

Before a new immutable candidate is published:

1. Run the narrow JavaScript/provider/persistence/pointer-lock suites.
2. Run conventional and shipping Asyncify/WasmFS Emscripten builds and browser
   smoke tests.
3. Run native desktop input/build gates because shared C++ input code changes.
4. Repeat real UT99 and Unreal save round trips.
5. In plain desktop Chrome, prove pointer capture, relative look, Escape, and
   recapture.
6. On Quest through Virtual Desktop/VDXR, repeat flat mouse behavior and capture
   privacy-safe counters if movement is still zero.
7. Capture the WebXR v2 report on the old failure before reload if possible.
8. On the new candidate, test Automatic negotiation, forced bridge, actual
   headset frame submission, tracking, UI quads, exit, and re-entry.
9. Preserve the immutable candidate until the physical evidence and artifact
   hashes are documented.

Until those gates pass, `cef1e89b` remains useful diagnostic evidence rather
than a promotable WebXR release.

## Implementation follow-up

The three focused product corrections were subsequently implemented without a
broad engine refactor:

- `46d13173` — WasmFS mutable Save-directory `stat` fallback and regressions;
- `f3643b78` — optional-WebGPU session negotiation, backend exclusivity matrix,
  and allowlisted provider error code; and
- `28906717` — actual-lock browser relative-motion ownership, atomic per-frame
  delivery, SDL de-duplication, blocked-call reset handoff, and regressions.

Their automated gates pass. This does not change the report's physical release
requirements: a newly built immutable package still needs real owner-save,
plain Chrome mouse-look, Quest/VDXR mouse-look, Automatic WebXR, forced bridge,
stereo/tracking/UI, exit, and re-entry qualification.
