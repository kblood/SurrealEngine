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

The fallback flat canvas successfully acquired pointer lock and mouse buttons
reached the game: primary fire worked. Relative mouse motion did not rotate the
view. This is not a pointer-permission or capture-overlay failure. It is a
separate browser-relative-motion-to-SDL/native delivery defect, or a Virtual
Desktop input-injection incompatibility, and remains a flat desktop release
blocker until instrumented and corrected.

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
- Reload the preserved game library, select **Force WebGL compatibility
  bridge**, leave blocking timing disabled, and test entry.
- Re-run the deterministic provider tests in the final package; the implemented
  matrix covers optional-feature negotiation, both enabled-feature outcomes,
  bridge-only, forced bridge, direct-only, unobservable feature state, and
  direct failure without an illegal same-session fallback.
- Confirm the new allowlisted `provider_error_code` appears in a physical
  privacy-safe report without raw exception text.
- Run a real Chrome pointer-lock test from nonzero browser
  `movementX`/`movementY` through the `28906717` atomic bridge to
  `Engine::OnWindowRawMouseMove`; retain the existing post-XR capture-prompt
  and requested-but-unlocked SDL fallback checks.
- Only after bridge entry works, qualify stereo output, head/controller
  tracking, menu and intro quads, exact pointer contact, exit/re-entry, timing,
  and loaded UT99/Unreal Gold behavior.

This attempt is useful hardware evidence, but it is a failed qualification and
the candidate must not be promoted to stable.

Post-candidate fixes are implemented but not yet physically qualified:

- `46d13173` restores WasmFS save-directory discovery through a `stat` fallback;
- `f3643b78` negotiates the WebXR backend from enabled session features; and
- `28906717` bridges exact-canvas browser relative motion into the engine while
  retaining requested-but-unlocked SDL fallback and de-duplicating active-lock
  delivery.

The independent Claude Code Opus 4.8 code-path review, ranked hypotheses,
candidate fixes, tests, risks, and commit decomposition are recorded in
`CLAUDE_OPUS_RUNTIME_BLOCKER_ANALYSIS.md`.
