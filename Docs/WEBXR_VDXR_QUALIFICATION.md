# WebXR Virtual Desktop / VDXR qualification

Date: 2026-07-23

Candidate: `cef1e89b3bbccba4d041ce5d00cc09bd99358c06`

Public URL:
`https://dionysus.dk/webxr/Ports/SurrealEngine-Candidate-cef1e89b/`

## Observed result

The first physical Quest 3 test used desktop Chrome through Virtual Desktop
with VDXR selected as the PC OpenXR runtime. **Automatic** reached the browser's
WebXR consent prompt, but immersive presentation ended immediately after the
permission was granted. The same engine instance continued in flat desktop
mode. This proves that the browser/runtime exposes the entry surface; it does
not prove that a projection layer was created or that a headset frame was
submitted.

The privacy-safe v2 report was not captured during the failed attempt, so the
exact provider failure stage remains unknown. The next reproduction must copy
the report before reloading. Its `provider_error_stage` distinguishes session
request, binding, format, projection-layer, and render-state failures. The
post-candidate `f3643b78` diagnostics now include the exact `lastErrorCode` only
as an allowlisted `provider_error_code`; raw exception text remains excluded.
Current source also preserves two formerly lost runtime-end outcomes:
`session-ended-before-activation` for an `end` event during reservation/setup,
and `session-ended-before-first-frame` after activation but before a completed
native presentation. Explicit application exit is not classified as either.

The fallback flat canvas successfully acquired pointer lock and mouse buttons
reached the game: primary fire worked. Relative mouse motion did not rotate the
view. This is not a pointer-permission or capture-overlay failure. It is a
separate browser-relative-motion-to-SDL/native delivery defect, or a Virtual
Desktop input-injection incompatibility, and remains a flat desktop release
blocker until instrumented and corrected.

## Candidate 173bf623 flat retest

A later human retest of immutable candidate `173bf623` confirmed that the exact
canvas captured the pointer, fire still worked, and relative mouse motion now
rotated the view. This physically qualifies the `28906717` delivery correction
for that Chrome/Virtual Desktop flat path. Current source additionally records
per-capture aggregate event/lock/nonzero/forward/gate counters and has a real
Chrome Pointer Lock regression, without retaining movement values.

Audio was initially audible, but disappeared later and did not recover until
the game was restarted. Inspection found a deterministic lifecycle gap: hidden
documents suspended WebAudio, while return-to-visible and presentation changes
only refreshed the UI. Commit `4dfceb0f` now best-effort resumes a started game
after either transition. The explicit Enable audio button remains available if
browser policy rejects that attempt, and a trusted active-session controller
`selectstart` supplies an additional data-free retry. Physical recovery still
needs retesting.

An apparent WebXR fallback during this round was not a valid headset result:
the headset was not actively connected as a WebXR device. The copied report
correctly remained entirely idle (`enter_attempts: 0`, no transitions). It does
not contradict or qualify either presentation backend.

For every desktop Chrome/VDXR retest, wake the Quest and controllers, select
VDXR, connect Virtual Desktop, and confirm the headset remains actively
connected as a WebXR device before loading the page and pressing Play. If the
pre-entry report still shows `capability_available: no`, do not treat a later
flat run as a WebXR failure. If it shows `enter_attempts: 0`, no immersive
request was recorded and the report is not headset-session evidence.

## Candidate 173bf623 forced-bridge headset retest

A subsequent physical test actively connected Quest 3 through Virtual Desktop
with VDXR selected, opened the deployed candidate in desktop Chrome, and chose
**Force WebGL compatibility bridge**. **Temporary QA: blocking bridge timing
(slower)** remained unchecked. The browser entered immersive VR instead of
falling back to the flat canvas.

The headset displayed native content and visibly tracked the blue left and
orange-red right pistol-like controller proxies. Those objects are the WebXR
UI provider's procedural controller boxes, not UT weapon models. Their presence
proves session reservation and activation, `XRWebGLLayer` presentation, native
frame production, UI visual composition, and tracked-controller input reached
the headset.

Visual correctness failed. Turning the head produced severe rotational
distortion, and recognizable world geometry ended in black after a short
distance. The test therefore does **not** qualify stereo/FOV, world scale,
tracking registration, controller/laser alignment, UI placement, latency, or
gameplay comfort. The unchecked blocking-timing option also rules out the
intentional `gl.finish()` QA mode as the cause.

The audit identified two independent source defects behind the result:

1. WebXR projection near/far values are metres, but native view-space positions
   are Unreal Units. The candidate flipped Z but did not apply the configured
   `WorldUnitsPerMeter`. At 39.3701 UU/m, a default 1000 m far plane was treated
   as roughly 1000 UU, or 25.4 m, matching the short black cutoff. `0331ef21`
   now scales the full homogeneous column while preserving the supplied
   asymmetric/sheared projection. Bridge packets convert WebGL depth to WebGPU
   `[0,1]` before setting the depth flag; `0218d5b7` also flags direct
   `XRGPUBinding` packets because Chromium already supplies their projection in
   `[0,1]` form. Native metre-to-UU scaling applies to both without double
   converting direct mode. Automated regressions pass; hardware requalification
   remains required.
2. Every XR callback presents the previously completed front atlas before it
   captures and schedules rendering for the current pose. Even without an
   Asyncify delay the visible atlas is therefore one XR callback old; with a
   delay it can be older. The color-only `XRWebGLLayer` submission carries no
   source-pose/depth information, so the compositor cannot make that image
   current and `gl.finish()` cannot repair the mismatch.

The landed projection-unit correction is the smallest first retest because it
directly explains the cutoff and is covered mathematically. Forced bridge is not
shippable as stable until current-pose presentation is also solved. A
rotation-only image warp may reduce turning artifacts, but full correctness for
translation and nearby world/UI/controller geometry requires same-XR-callback
render/present or depth/motion-aware reprojection.

Commit `078a6d2f` makes the remaining atlas defect measurable without exposing
pose or game data. In a running bridge report,
`bridge_present_age_frames`/`bridge_present_age_ms` describe the currently
presented target's source age, their `bridge_max_*` counterparts retain the
session maxima, and `bridge_reused_presents` increments when the same completed
target is submitted again. Values are bounded to 65,535 frames, 60,000 ms, and
4,294,967,295 reuses and reset on entry/re-entry. `unknown` age before the first
completed presentation is normal. Positive age is evidence of stale-pose
submission; a rising maximum or reuse count shows worse delay. The fields are
diagnostics, not a release waiver.

## Automatic backend correction

The current Automatic preflight selects direct presentation when
`XRGPUBinding` exists and `requestAdapter({xrCompatible: true})` returns an
adapter. A WebIDL implementation may ignore an unrecognized dictionary member,
so adapter creation alone can overstate XR compatibility.

Implemented at `f3643b78`, when direct WebGPU and the WebGL bridge are both
candidates, Automatic now:

1. requests one immersive session with `webgpu` in `optionalFeatures`;
2. inspects `session.enabledFeatures` after consent;
3. uses direct `XRGPUBinding` only when `webgpu` was enabled; and
4. otherwise creates the normal `XRWebGLLayer` compatibility bridge.

This is the adaptive path described by the
[WebXR/WebGPU Binding editor's draft](https://immersive-web.github.io/webxr-webgpu-binding/).
A session with `webgpu` enabled must not create an `XRWebGLLayer` or set a
WebGL `baseLayer`. Consequently, a later direct binding or projection-layer
failure cannot fall back inside the same session. The provider must end that
session, report a safe actionable failure, and require another trusted user
gesture with **Force WebGL compatibility bridge** selected.

Direct-only operation may continue to require `webgpu`. Forced bridge must not
request it. Direct presentation also retains `COPY_DST` texture usage because
the renderer copies its persistent eye targets into runtime projection
textures; removing that usage is not a valid compatibility workaround.

## Required next evidence

- Reproduce Automatic and save the v2 report before reloading.
- Retain the proved forced-bridge entry with blocking timing disabled; test the
  landed projection correction, then repeat after correcting atlas pose age and require stable
  stereo/FOV while yawing, pitching, and translating the head.
- Re-run the deterministic provider tests in the final package; the implemented
  matrix covers optional-feature negotiation, both enabled-feature outcomes,
  bridge-only, forced bridge, direct-only, unobservable feature state, and
  direct failure without an illegal same-session fallback.
- Confirm the new allowlisted `provider_error_code` appears in a physical
  privacy-safe report without raw exception text.
- Interpret `session-ended-before-activation` as a browser/runtime shutdown
  during the reserved session, layer, or reference-space setup. Interpret
  `session-ended-before-first-frame` as successful activation without one
  completed native presentation. In either case, preserve the report and VDXR
  OpenXR log before trying forced bridge; do not infer a projection or game
  rendering failure from the consent prompt alone.
- Retain the passed human mouse-look result plus the real Chrome Pointer Lock,
  post-XR capture-prompt, requested-but-unlocked SDL fallback, and aggregate
  counter checks in the superseding package.
- Once bridge visuals are stable, qualify head/controller tracking, menu and
  intro quads, exact pointer contact, exit/re-entry, timing, and loaded
  UT99/Unreal Gold behavior.

This attempt is useful hardware evidence, but it is a failed qualification and
the candidate must not be promoted to stable.

Post-candidate fixes are implemented but not yet physically qualified:

- `46d13173` restores WasmFS save-directory discovery through a `stat` fallback;
- `f3643b78` negotiates the WebXR backend from enabled session features; and
- `28906717` bridges exact-canvas browser relative motion into the engine while
  retaining requested-but-unlocked SDL fallback and de-duplicating active-lock
  delivery;
- `0331ef21` scales runtime projection depth from metres to Unreal Units while
  preserving its asymmetric matrix;
- `0218d5b7` marks direct Chromium WebGPU projections as already zero-to-one;
  and
- `078a6d2f` reports bounded bridge pose age and target reuse without exposing
  source pose/projection data.

All three are available for retest in immutable candidate `173bf623` at
`https://dionysus.dk/webxr/Ports/SurrealEngine-Candidate-173bf623/`. Its clean
package, live flat HTTPS smoke, source offer, headers, and public hashes pass;
those checks do not substitute for the physical VDXR matrix above.

Live synthetic trusted-Play checks additionally prove both Automatic outcomes
and forced bridge against the deployed provider assets. The granted feature set
selected exactly one layer API in every session and cleanup returned the engine
loop. The native engine and immersive compositor were necessarily synthetic,
so the next action remains a real VDXR session rather than promotion.

The same live candidate also passed an isolated same-origin owner-data upgrade
test. It restored and flat-launched owned GOG Unreal Gold (335 files) and UT99
(496 files), with advancing ticks, nonuniform rendered canvases, and no page or
WebGPU errors. The run reproduced throwing WasmFS `analyzePath`, proved the
`stat` fallback included a synthetic allowlisted `Save99.usa`, explicitly
flushed it to OPFS, and restored its exact 32-byte content after closing and
reopening Chrome. A disallowed sibling remained absent. This closes the
candidate's real owner-save gate. The later human flat retest qualified
mouse-look and established initial audible output. Forced bridge subsequently
entered immersive VR and displayed tracked procedural controllers, but failed
visual qualification through projection cutoff and rotational distortion.
Audio recovery after a lifecycle transition, corrected headset visuals, and
the remaining physical matrix are still unqualified.

The independent Claude Code Opus 4.8 code-path review, ranked hypotheses,
candidate fixes, tests, risks, and commit decomposition are recorded in
`CLAUDE_OPUS_RUNTIME_BLOCKER_ANALYSIS.md`.
