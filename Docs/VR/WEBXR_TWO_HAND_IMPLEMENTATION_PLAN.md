# WebXR two-hand weapon implementation plan

## Implementation status (2026-07-22)

The fail-closed code foundation and conservative engine seam are implemented.
`SurrealEngine/WebXR/WebXRTwoHandWeapon.{h,cpp}` is a portable deterministic
module with no engine-object dependency. The Emscripten engine copies normalized
squeeze from standard button-value slot 1, resolves dominant/off-hand sources by
handedness, and updates two-hand state once per newly published input frame after
both grips have been composed into UE1 world space.

Implemented and verified:

- Immutable metadata schema version 1 with exact package, class, package hash,
  controller profile, dominant hand, world scale, headset, and runtime
  qualification. Missing, duplicate, malformed, out-of-range, or mismatched
  rows fail closed. The production table has zero rows.
- Strict `> 0.60`/`< 0.40` analog and `< 0.12 m`/`> 0.25 m` spatial hysteresis,
  including exact-boundary fixtures. Squeeze remains ordinary engine input and
  is neither synthesized nor consumed by this feature.
- A frame-rate-independent 100 ms grab/release blend for 60, 72, 80, and 90 Hz,
  plus smooth short-baseline fade below 15 cm. Negative/zero deltas do not
  advance the blend; non-finite input resets it.
- A full-basis construction using the main grip's presentation up axis, with a
  projected main-right fallback, explicit handedness, and quaternion
  shortest-arc blending. Zero effective weight is an exact structural
  passthrough of the pre-existing one-hand basis.
- State binding to monotonic input frame, conservative session generation,
  reference-space reset generation, dominant/off-hand source IDs, handedness,
  pawn, exact current weapon, and metadata row. Session/reset/source/hand/pawn/
  weapon/death/menu/tracking/non-finite/stale-frame/exception transitions clear
  active state, blend, and cached identities. Recovery requires a neutral frame
  followed by a fresh qualifying grip.
- Conservative engine integration: a qualified nonzero blended basis can drive
  the existing per-eye presentation rotation and its zero-roll forward can drive
  only the already-scoped local-current-weapon aim override. Main-grip visual
  position, pawn transform, movement, view state outside the scope, muzzle
  origin, spread, autoaim, inventory, fire state, and remote actors are not
  changed.
- Persisted, default-off `Engine.WebXR.TwoHandAimEnabled` setting. Enabling is
  an explicit opt-in; disabling synchronously resets state and makes the
  existing one-hand paths authoritative.
- Versioned native/Wasm diagnostics for settings/eligibility/activity, identity
  generations and source IDs, reset/rejection/grab/release counts and reasons,
  analog/distances/weights/filter latency, all one/two/blended basis components,
  orthonormal error, package/class, metadata schema, and production row count.
- Deterministic module tests cover package collisions and qualification,
  ownership/death/menu failures, all hysteresis boundaries, four frame rates,
  reset/fresh-grip behavior, coincident/non-finite/short-baseline cases,
  full-roll handed bases, yaw-wrap shortest arc, exact one-hand passthrough, and
  exception unwind. Browser smoke additionally proves grip/profile transport,
  loaded `Botpack.ShockRifle` identity, zero production eligibility, exact
  zero-weight fallback, two weapon eye passes, tracking loss, and setting reset.

Verification evidence for this implementation pass:

```text
portable MinGW self-test driver: pass
cmake --build build --config Debug -j 4: pass
cmake --build build-emscripten --target SurrealEngine -j 2: pass
cmake --build build-emscripten-nodata --target SurrealEngine -j 2: pass
python -B -u web/smoke_test_webxr.py --experimental-webgpu-xr: pass
packed frame: 201 draws, 1181 differing stereo samples, zero GPU errors
```

The implementation is intentionally not a physically usable two-hand grip yet.
No browser/headset identity strings or package-content hash are transported into
the engine qualification query, no calibration-candidate writer exists, and no
weapon row has been measured or approved. Those facts keep the table empty and
all real weapons one-handed. The remaining gates are the staged-delivery items
6-8 below: add an explicit non-production calibration flow, carry exact runtime/
headset/controller-profile and package-hash provenance, measure a rifle for both
dominant hands, review the candidate into immutable code, and pass Quest 3/VDXR
tracking-loss, occlusion, jitter, comfort, and 30-minute tests. Optional grab/
release haptics and low-pass filtering also remain disabled; neither should be
enabled before that physical tuning.

## Scope and default policy

Two-hand aiming is an opt-in weapon interaction layered on the existing
controller aim, visual-only grip placement, and scoped firing direction. It does
not implement physical reloads, change projectile origins, move the pawn, or
invent foregrips for uncalibrated weapons.

Eligibility is immutable and package/class-qualified. The production metadata
table starts empty, so every current weapon remains exactly one-handed until a
foregrip point has been measured, inspected in WebGPU, and qualified on a
headset. A class-name-only or inferred `PlayerViewOffset`/`FireOffset` fallback
is forbidden.

## Required state and inputs

Extend the normalized controller state with the standard squeeze/grip analog
from button-value slot 1. The two-hand state is bound to all of:

- input frame/session generation;
- dominant and off-hand source IDs;
- local pawn and exact current weapon identity;
- weapon package/class metadata row;
- main grip and off-hand grip pose validity.

Reset active grip, blend, filtered orientation, and cached identities on session
end, pose/source loss, reference-space reset, dominant-hand change, weapon or
pawn change, death/travel, menu ownership, non-finite input, and exception
unwind. A stale cached foregrip must never survive any of those transitions.

## Package-qualified metadata

Each immutable row contains:

- schema version;
- package and class;
- `twoHandEligible`;
- weapon-local foregrip XYZ relative to the same calibrated visual origin and
  orientation used by the viewmodel seam;
- optional grab/release radii and analog thresholds, bounded to safe ranges;
- calibration provenance: build/package hash, controller profile, dominant
  hand, world scale, and measured headset/runtime.

Missing, duplicate, non-finite, wrong-package, wrong-hash, or unqualified rows
fail to one-hand behavior. Metadata must not silently modify the authoritative
muzzle schema.

## Grab/release state machine

Compute the world foregrip from the exact current visual transform. With both
grip poses valid and a qualified weapon:

1. Inactive -> active only when off-hand squeeze is above the grab threshold
   and off-hand grip position is inside the grab radius.
2. Active -> inactive when squeeze drops below a lower release threshold or the
   off hand moves beyond a larger release radius.
3. Ramp a blend weight toward the active state over approximately 100 ms.
4. Do not generate a gameplay button/key, consume the squeeze binding, or
   change inventory/fire state.

Initial conservative defaults, subject to physical tuning, are 0.60/0.40
analog hysteresis and 0.12 m/0.25 m spatial hysteresis, converted through the
configured world-units-per-meter. Invalid time deltas cannot advance the blend.

## Orientation composition

The main grip remains the visual weapon origin. The raw two-hand forward vector
is `offGripPosition - mainGripPosition`. Reject coincident/non-finite hands.

Use the main grip's full presentation up axis as the roll hint, project it onto
the plane orthogonal to the new forward vector, and build a finite orthonormal
forward/right/up basis with explicit handedness tests. If the roll hint is
degenerate, fall back to the main grip's right axis; if that is also degenerate,
remain one-handed for the frame.

Fade the two-hand contribution toward zero as hand separation falls below
15 cm because short baselines amplify positional jitter. Combine this baseline
weight with the 100 ms grab weight. Blend the full orientation along the
shortest rotation arc; do not independently lerp wrapped Euler angles. Optional
low-pass filtering must be user-configurable, default off, reset on every state
identity change, and report its added latency.

The resulting full basis drives per-eye visual presentation. Its zero-roll
direction drives only the already scoped local-current-weapon aim path. The
authoritative firing origin remains governed separately by
`WEBXR_MUZZLE_ORIGIN_PLAN.md`.

## Diagnostics and settings

Expose versioned diagnostics for eligibility, active state, blend and baseline
weights, source IDs/generation, weapon package/class, grip distance/analogs,
reset/rejection reasons, raw one/two-hand and blended bases, and finite/
orthonormal errors. Persist a strict `twoHandAimEnabled` setting; disabling it
must synchronously clear state and reproduce one-hand output exactly.

Optional grab/release haptics must route through the existing browser outcome
policy and remain disabled until physical tuning. They must never fire merely
because the off hand is near an ineligible weapon.

## Deterministic fixture matrix

- Empty metadata table and wrong-package same-class rejection.
- Exact local/current weapon eligibility; remote pawn, bot, stale weapon, and
  inventory transition rejection.
- Analog and distance hysteresis at, below, and above every boundary.
- Frame-rate-independent 100 ms blend for 60/72/80/90 Hz steps.
- Source/session/dominant-hand/weapon/menu/tracking-loss resets with no stuck
  active state; automatic recovery requires a fresh grip condition.
- Coincident, near-baseline, non-finite, extreme-world-scale, and degenerate
  roll-hint handling.
- Orthonormal handed basis and shortest-arc behavior across yaw/pitch/roll wrap.
- Exact one-hand passthrough at weight zero and setting disabled.
- Main-hand visual origin invariance while orientation blends.
- Scoped ballistic direction follows blended forward while gameplay muzzle
  origin, spread, autoaim, view rotation, movement, and remote actors remain
  unchanged.
- Loaded stock eligible-weapon `RenderOverlays` fixture with two eye passes,
  exact nested restoration, source loss, menu entry, weapon switch, and session
  re-entry.
- Browser fixture proving grip analog/profile slots and physical Quest test for
  grab feel, jitter, release, occlusion/loss, handedness, and comfort.

## Staged delivery

1. Add pure state-transition, baseline weighting, and full-basis composition
   helpers with native self-tests.
2. Copy normalized squeeze analogs and identity generations into engine state.
3. Add state reset/diagnostics with an empty production metadata table.
4. Integrate the two-hand orientation into visual and scoped direction paths;
   prove exact one-hand behavior for all unqualified weapons.
5. Add deterministic fake-hand and loaded-weapon browser fixtures.
6. Build an explicit headset calibration flow that writes candidate metadata
   without changing the immutable production table.
7. Inspect and qualify one rifle row end-to-end, including left/right dominant
   hand, tracking loss, and 30-minute comfort testing.
8. Expand weapon coverage only through individual reviewed rows and reports.

## Reuse boundary

The sibling native worktree contains useful clean-room math concepts:
foregrip-world composition, dual analog/spatial hysteresis, a 100 ms transition,
minimum-baseline weighting, roll derived from the main hand, and exact release
on invalid tracking. Its class-only table and Sniper foregrip are explicitly
placeholder data and must not be copied. Its Euler interpolation/filtering and
global weapon hooks also require fresh WebXR-native full-basis integration and
tests.
