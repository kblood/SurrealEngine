# Controller-Aimed Weapons — Design & Implementation Plan (M3 weapon-aim track)

Companion to `VR_IMPLEMENTATION_PLAN.md` (whose M3 section 3 — "6DoF weapon aim
decoupled from view — the hardest, most UT99-specific part" — this document
expands into a real, phased plan). Written 2026-07-21 as a design pass only:
no engine source was modified for this document. Architecture facts below were
re-verified against the live `vr-m2` worktree this session (file:line
citations throughout), not carried over blind from earlier recon.

## Feature summary (user requirements)

1. Weapon aim comes from the motion controllers, not head-look.
2. Left-handed / right-handed mode: which physical hand is the "main hand."
3. Two-handed weapons: held in the main hand; when the off-hand grips the
   foregrip, aim direction comes from the **vector between the two hand
   positions**, not either hand's orientation.
4. Dual-wielding: two one-handed guns, each independently aimed by its own
   hand's pose, each independently fireable.
5. Hand models on weapons track the physical controller poses (grip-socket
   attachment near an authored grip point — not full IK).
6. Left-handed mode mirrors the weapon model.

## Prior-art research (concise)

**Two-handed aim from hand positions.** Pavlov VR's two-hand long-gun model is
position-only: the aim ray runs from the trigger hand's grip point through the
foregrip hand's position — controller *orientations* are ignored while both
hands grip, because with a rigid object the between-hands line and the barrel
line are physically the same thing (community discussion of the exact model:
[Pavlov two-handed mechanics thread](https://steamcommunity.com/app/555160/discussions/0/1489992080524926052/)).
The known downside they discuss: moving the off-hand sideways shifts aim even
though the trigger hand "feels" pointed at the target — accepted as the
standard trade-off because the position-vector is far more stable than either
controller's rotation. Roll is the missing third degree of freedom and is
conventionally taken from the primary hand's up vector (Unity convention:
`LookRotation(offHand.pos - mainHand.pos, mainHand.up)`).

**Stabilization / jitter.** Angular noise of the between-hands vector scales
inversely with hand separation (same positional tracking error over a shorter
baseline = bigger angle error), so implementations (a) require a minimum grab
distance / blend back to single-hand aim when hands come too close, (b) apply
hysteresis on grab/release (grab radius smaller than release radius, plus a
short slerp blend on transition so aim doesn't snap), and (c) offer an
optional low-pass filter on the aim direction — H3VR ships exactly this as
"Hand Filtering & Smoothing," plus proximity-based stabilization and a
**virtual stock** (gun pivots about a shoulder anchor when raised to the
shoulder) as further optional stability layers
([H3VR Gun Stabilization wiki](https://h3vr.fandom.com/wiki/Gun_Stabilization)).
Virtual stock is noted here as a possible later comfort option, not scoped.

**Grip-socket attachment (not IK).** The standard pattern (Unity XR
Interaction Toolkit's "Attach Transform" / "Secondary Attach Transform",
[two-handed grab setup](https://medium.com/@Brian_David/how-to-create-two-handed-vr-interactions-using-unitys-xr-grab-interactable-component-f62c3bd7f56e))
is: the weapon authors one primary grip transform and one foregrip transform
in weapon-local space; the weapon is placed so its primary grip transform
coincides with the controller's grip pose; hand meshes are parented to those
authored transforms (riding the controller pose), with no inverse kinematics.
UT99 makes this easier than a modern game: first-person viewmodels have the
arms/hands **baked into the weapon mesh**, so "hands that match the
controller" mostly falls out of moving the whole viewmodel with the
controller (see M-B).

**Muzzle vs grip offset.** The fire ray should originate at the weapon's
muzzle (an authored weapon-local offset from the grip point), not at the
controller origin — otherwise near-cover shots clip through walls the visible
barrel is behind. Grip pose vs aim pose also matters: OpenXR defines the
**grip pose** (fist orientation, for holding objects) and the **aim pose**
(pointing ray) separately, and Touch controllers report them with different
orientations. Design choice below: viewmodel placement and the two-hand
vector use grip poses; single-hand aim direction uses the aim pose plus a
per-weapon rotational trim.

**Dual-wielding.** Games with independent dual-wield (Pavlov, Contractors,
etc.) simply run the one-hand pipeline twice — each weapon parented to its
own hand's pose with its own fire input. The hard part here is not VR math
but UT99's single-`Pawn.Weapon` data model (see M-E).

## Ground truth (verified in this worktree, 2026-07-21)

- **No pose actions exist.** `VRControllerState`
  (`SurrealEngine/RenderDevice/Vulkan/VulkanXRSession.h:33-56`) is
  sticks/triggers/buttons only. `VulkanXRSession::CreateActions()`
  (`VulkanXRSession.cpp:692-823`) creates zero `XR_ACTION_TYPE_POSE_INPUT`
  actions; the Touch profile bindings (`:784-798`) bind no `.../grip/pose` or
  `.../aim/pose` paths. **Important constraint:** `xrAttachSessionActionSets`
  is called once at the end of `CreateActions()` (`:810-819`) and the OpenXR
  spec forbids attaching twice — pose actions must be added *inside*
  `CreateActions()` before the attach.
- **Head-pose composition to reuse for controllers**: `Engine::Run()`'s XR
  frame block converts OpenXR LOCAL-space poses to UE1 world space via
  `XRVecToUE1` (`Engine.cpp:97`), `UUPerMeter = 1/0.0254` (`:111`, 1 UU = 1
  inch), a yaw-only recenter `Coords::YawRotation(xrYawOffsetUE)` (`:437`),
  and `CameraLocation` as the play-space anchor (`:442`). The verified sign
  conventions (Rotator-space yaw = −(Coords::YawRotation-space yaw); pitch is
  `atan2(fwd.z, horizLen)` with no flip) are documented at `Engine.cpp:400-434`
  and `:1868-1897` — controller poses must go through the exact same helper,
  not a reimplementation. `xrLocateSpace` calls must use the same
  `lastPredictedDisplayTime` already used by `LocateViews`
  (`VulkanXRSession.cpp:522,544`).
- **`Pawn.ViewRotation` is written from head pose every frame**
  (`Engine.cpp:1897-1932`: yaw at `:1897/1919`, pitch at `:1931`) and is ALSO
  what UT99's script `PlayerMove` uses for movement direction (hard-won fix,
  see the doc comments there). It must **not** be persistently redirected to
  controller aim.
- **Fire is fully script-driven; the VM has a single choke point.** There is
  no native `Fire`/`TraceFire`/`ProjectileFire` in this codebase. Every
  UnrealScript function invocation — virtual, final, and global alike — routes
  through `Frame::Call` (`SurrealEngine/VM/Frame.cpp:201-240`; the only
  bytecode dispatcher, `ExpressionEvaluator::Call`, funnels into it at
  `ExpressionEvaluator.cpp:687-705`, short-circuit `&&`/`||` excepted). This
  is the interception seam the whole plan is built on.
- **Fire input paths**: trigger edges synthesize `IK_LeftMouse`/`IK_RightMouse`
  through `Engine::InputEvent` (`Engine.cpp:1963-1968`); automatic weapons
  additionally re-fire from weapon *state code* during the script tick — so
  any aim fix keyed to the InputEvent edge alone would miss automatic re-fire.
  The `Frame::Call` seam catches both (both end in a `TraceFire`/
  `ProjectileFire` script call).
- **Viewmodel rendering**: `RenderSubsystem::RenderOverlaysVR()`
  (`Render/RenderCanvas.cpp:157-201`) sets `MainFrame.Frame = VREyeFrame[eye]`
  (`:172`) then dispatches script `RenderOverlays` (`:178`, ue1Version > 219)
  or `InvCalcView` + `DrawActor` (`:184-187`). The weapon actor's
  `Location()`/`Rotation()` (script-set from `Inventory.PlayerViewOffset`,
  `UObject/UActor.h:901`, and `ViewRotation`) drive a normal mesh draw whose
  transform is built at `Render/VisibleMesh.cpp:21`
  (`translate(Location+PrePivot) * Rotation * scale(DrawScale)` — uniform
  scale only, relevant to mirroring in M-F).
- **Single-weapon data model**: `UPawn::Weapon()` (`UActor.h:2002`) and
  `PendingWeapon()` (`:1970`) are single pointers. `Weapon.FireOffset`
  (`:951`), `AdjustedAim` (`:941`), `MuzzleScale` (`:968`) exist as property
  accessors only — nothing native reads them today.
- **Per-hand analog grip already exists** (`VRControllerState::leftGrip/
  rightGrip`, `VulkanXRSession.h:38`; right grip currently bound to crouch,
  `Engine.cpp:1938`) — M-D's foregrip grab can use it without new actions,
  but crouch needs rebinding (move crouch to left stick click or B).

## The `ViewRotation` / `AdjustedAim` question — resolution

**What UT99's fire path actually does** (per public community documentation —
[Unreal Wiki, Legacy:Hitscan Weapons](https://unrealarchive.org/wikis/unreal-wiki/Legacy:Hitscan_Weapons.html);
[UnCodeX Engine.PlayerPawn reference](http://uncodex.ut-files.com/Unreal/216/engine/playerpawn.html);
[Weapon (UT) property reference](https://beyondunrealwiki.github.io/pages/weapon-ut.html)):

- `TraceFire`: start = `Owner.Location + CalcDrawOffset() + FireOffset·(X,Y,Z)`
  where `(X,Y,Z) = GetAxes(Pawn.ViewRotation)`; end = start + random spread
  along Y/Z + `10000 * vector(AdjustedAim)`, where
  `AdjustedAim = Pawn.AdjustAim(...)` is called **at fire time**. The trace
  direction therefore comes entirely from `AdjustAim`'s return value; only
  the *origin offset* and *spread axes* come from `ViewRotation` directly.
- `ProjectileFire`: same start computation; the spawned projectile's rotation
  is `AdjustedAim` from the same fire-time `AdjustAim` call. Lobbed
  projectiles use the sibling `AdjustToss`.
- `AdjustAim`
  (`function rotator AdjustAim(float projSpeed, vector projStart, int aimerror, bool bLeadTarget, bool bWarnTarget)`)
  is a **script** function on PlayerPawn (for humans it applies optional
  autoaim assistance around the player's view; for bots it computes bot aim).
  It is not native in this engine — grep confirms no `AdjustAim`,
  `CalcDrawOffset`, or `TraceShot` native exists anywhere in
  `SurrealEngine/Native/`.

**Verdict on the original hypothesis** ("write a controller-vs-head delta
into the `AdjustedAim` property each frame"): **does not hold.**
`AdjustedAim` is not a persistent input the fire path reads — it is an output
variable overwritten by the `AdjustAim()` call inside every `TraceFire`/
`ProjectileFire`. A per-frame property write would be clobbered at the only
moment it matters.

**Recommended approach — fire-scoped `ViewRotation` override via a VM
interception seam.** The insight that *does* survive: everything the fire
path derives (origin axes, spread axes, and — because player `AdjustAim`
builds on the view — the fire direction itself) flows from `Pawn.ViewRotation`
**as read during the `TraceFire`/`ProjectileFire` call**. `Frame::Call`
(`Frame.cpp:201`) wraps every such call synchronously. Therefore:

1. Add a narrow, engine-owned interception hook consulted at the top of
   `Frame::Call` (installed by `Engine`, so the VM module doesn't depend on
   Engine headers — a `std::function` slot, null in non-VR runs, zero
   behavior change when unset).
2. When the called function is named `TraceFire` or `ProjectileFire` **and**
   the instance is the local player's current weapon
   (`engine->viewport->Actor()->Weapon()`) **and** VR controller aim is
   active with a valid pose: save `Pawn.ViewRotation`, write the controller
   aim Rotator for the owning hand, run the original script call unchanged,
   restore `ViewRotation` (re-entrancy-safe via a save stack).
3. `PlayerMove`, camera, swimming, HUD — all separate script calls — never
   see the swapped value. Movement stays exactly as it works today.
4. Stock autoaim keeps working *relative to controller aim* (its assistance
   cone is computed around the swapped `ViewRotation`) — a free, period-
   correct VR aim assist. If it proves annoying, a second name-keyed
   intercept of `AdjustAim` can hard-return the controller rotator instead.

Why this beats the fallbacks:
- It catches **all** fire entry points — the synchronous `InputEvent` edge
  *and* automatic re-fire from weapon state code — because both terminate in
  a `TraceFire`/`ProjectileFire` script call.
- It is per-call-scoped: no persistent `ViewRotation` corruption, no
  movement regression window at all (the swap-around-`InputEvent` fallback
  would have missed automatic re-fire and charge-up weapons entirely).
- It needs zero UnrealScript changes, no custom `.u` package, and no
  decompiled code — the intercepted names and their documented contract come
  from public references above.

**Fire origin (phase 2 of the same mechanism):** with only the rotation swap,
shots originate near the head (`CalcDrawOffset` is EyeHeight/PlayerViewOffset
based) but travel along the controller ray — acceptable for a first
verifiable cut, with the classic near-cover parallax artifact. The fix is a
second name-keyed intercept: `CalcDrawOffset` (an `Inventory` script
function returning an Owner-relative offset) returns
`(handGripWorldPos + gripToMuzzleOffset) − Owner.Location` for the local
player's weapon. That single intercept moves **both** the fire origin and the
script-computed viewmodel anchor (`InvCalcView` uses the same function),
which M-B wants anyway.

**Fallback plan if some weapon doesn't route through these names:** UT99
weapon subclasses conventionally override `TraceFire`/`ProjectileFire` but
keep the names (the `Frame::Call` seam dispatches on the *name*, so overrides
are caught automatically). If a specific Botpack weapon computes aim some
other way (candidates to audit: guided Redeemer alt-fire, Ripper blade
bounce aiming, Translocator toss, sniper zoom), the per-weapon fix is to
widen the intercepted-name set (e.g. also wrap that weapon's `Fire`/`AltFire`
with the same scoped swap — strictly more of the same mechanism, not a new
one). If public docs can't pin a given weapon's behavior down, that specific
weapon goes through the project's two-agent clean-room decompile-spec process
(behavioral spec only, no code copied) — flagged per-weapon in M-C/M-D DoD,
not a blanket requirement.

## Aim-source model (used by all milestones)

Per hand, per frame (computed in `Engine`, next to the existing head-pose
composition):

- `handGripPos/Coords` — grip pose, world space (viewmodel anchor, two-hand
  vector endpoints, foregrip grab tests).
- `handAimRotator` — aim pose forward converted to a Rotator
  (`yaw = atan2(fwd.y, fwd.x)`, `pitch = atan2(fwd.z, √(fwd.x²+fwd.y²))`,
  scaled by `32768/π` — these are the already-verified sign conventions from
  `Engine.cpp:400-434`/`:1868-1897`).
- `weaponAimRotator(hand)` — what M-C's intercept writes:
  - one-handed: `handAimRotator` + per-weapon trim;
  - two-handed grip active (M-D): rotator of
    `normalize(offHandGripPos − mainHandGripPos)` with roll from the main
    hand's up axis, blended/filtered as specified in M-D.

## Milestones

Dependency chain: **M-A → M-B → M-C → M-D → M-F → M-E.** M-F (handedness) is
deliberately ahead of M-E (dual-wield): it's cheap if every earlier milestone
uses a main/off-hand indirection from day one, and dual-wield is the most
speculative item so it goes last.

### M-A — Per-hand grip + aim pose tracking (OpenXR actions/spaces)

**What changes**
- `VulkanXRSession.h`: new `VRHandPose` struct (`bool valid; float posX..;
  float qx..qw;` — same opaque-POD style as `VREyePose`, `:21-26`), two per
  hand (grip + aim). New method `bool LocateHandPoses(VRHandPose outGrip[2],
  VRHandPose outAim[2])`.
- `VulkanXRSession.cpp CreateActions()` (`:692-823`): four
  `XR_ACTION_TYPE_POSE_INPUT` actions (`left_grip_pose`, `right_grip_pose`,
  `left_aim_pose`, `right_aim_pose`) via the existing `makeAction` lambda
  (`:716`); bind `/user/hand/{left,right}/input/{grip,aim}/pose` in the Touch
  profile block (`:784-798`) and both pose paths in the `khr/simple_controller`
  block (`:804-808` — simple_controller does define grip and aim poses).
  **Must precede `xrAttachSessionActionSets` (`:813`)** — attach is
  once-per-session.
- After session-space creation: `xrCreateActionSpace` per pose action
  (identity pose-in-action-space), four stored `XrSpace` handles + cleanup in
  `DestroySession()`.
- `LocateHandPoses()`: `xrLocateSpace(handSpace, appSpace,
  (XrTime)lastPredictedDisplayTime)` — same time source as `LocateViews`
  (`:522,544`); `valid` = both `XR_SPACE_LOCATION_POSITION_VALID` and
  `ORIENTATION_VALID` bits set.
- `Engine.h`: new per-hand world-space state block next to `xrHeadYawUE`
  (`:203-213`) — grip position (vec3), grip Coords, aim Rotator, valid flag,
  plus a `mainHand` index (0/1) consulted through `MainHand()`/`OffHand()`
  accessors from day one (M-F flips one variable).
- `Engine.cpp Run()` XR block: after `LocateViews` (`:367`), call
  `LocateHandPoses`; factor the existing eye composition math (`:385-448`)
  into a shared `ComposeXRPoseToWorld` helper (quaternion → UE1 axes via
  `XRVecToUE1` `:97`, `UUPerMeter` `:111`, recenter `:437`, `CameraLocation`
  anchor `:442`) and use it for both eyes and hands.
- Extend the throttled "VR input diag" log (`Engine.cpp:1814-1856`) with both
  hands' world positions and aim yaw/pitch.

**Definition of done**: clean build; `--autoplay --vr` run logs
`xrCreateAction`/`xrCreateActionSpace` results = 0 and per-hand diag lines;
with the Quest 3 connected via Virtual Desktop, diag shows plausible values
(hands ~0.3-0.7 m below/ahead of the head position, moving when controllers
move). No regression in the no-`--vr` log (zero OpenXR lines).

**Verification mode**: non-interactive for build/plumbing (poses read
`valid=false` without a headset — graceful path exercised); real pose *data*
needs the headset connected, but judging it is log-based, not perceptual —
the user only needs to wave controllers during one timed run.

### M-B — Viewmodel (weapon + baked hands) rides the controller

**What changes**
- New interception seam: a single `std::function`-based hook slot consulted
  at the top of `Frame::Call` (`VM/Frame.cpp:201`, before the dispatch at
  `:232-239`), installed by `Engine` only when a VR session is active. Null
  hook = today's behavior byte-for-byte. Keyed on function name + instance
  filter inside the Engine-side handler, not in the VM.
- Intercept `InvCalcView` when `instance == viewport->Actor()->Weapon()`:
  natively write `weapon->Location() = mainHandGripPos + gripOffset(weapon)`
  and `weapon->Rotation() = weaponAimRotator(mainHand)`, skip the script
  body (returns nothing). Both `RenderOverlaysVR` dispatch paths
  (`RenderCanvas.cpp:178` script-internal call, `:185` direct `CallEvent`)
  route through `Frame::Call`, so one intercept covers both engine versions.
- Per-weapon grip table: weapon-class-name → weapon-local grip offset +
  rotational trim + muzzle offset + foregrip point (M-D) — a small native
  table with a sane default (derived initially from `PlayerViewOffset`
  `UActor.h:901` / `FireOffset` `:951`), overridable via ini for tuning.
- Hand models: UT99 viewmodels bake the arms/hands into the weapon mesh, so
  the main hand is covered by the viewmodel itself. Off-hand: render a small
  placeholder marker (reuse `DrawActor` on a spawned decorative actor or a
  simple translucent sprite at `offHandGripPos`) so the player can see where
  their empty hand is for M-D grabbing. Custom hand meshes = stretch,
  requires an asset source, explicitly out of scope here.
- New `--debugvrhands` flag: injects two fake, slowly-orbiting hand poses
  without any XR session, so the intercept + rendering is exercisable on the
  build machine.

**Definition of done**: with `--debugvrhands`, `PrintWindow` screenshots show
the weapon viewmodel displaced/rotated away from screen-center over time (vs
today's fixed anchoring); log line confirms the `InvCalcView` intercept fired.
Flatscreen no-flag run unchanged.

**Verification mode**: non-interactive via `--debugvrhands` + screenshot;
"does it feel attached to my hand" needs the user in-headset (tuning the
per-weapon grip offsets is headset work by nature).

### M-C — Single-hand fire redirect (the ViewRotation resolution)

**What changes**
- Same seam: intercept `TraceFire` and `ProjectileFire` for the local
  player's weapon → scoped `ViewRotation` swap to `weaponAimRotator(mainHand)`
  as specified in the resolution section (save/swap/run-original/restore,
  re-entrancy stack). Log one throttled line per fire with the swapped
  rotator for verification.
- Phase 2 (same milestone): intercept `CalcDrawOffset` to return
  `(mainHandGripPos + muzzleOffset(weapon)) − Owner->Location()` — moves the
  fire origin to the hand. Confirm the documented Owner-relative return
  convention empirically via a log comparison on first run (community docs
  say relative; verify before trusting).
- Rebind crouch off right-grip (`Engine.cpp:1938`) to free grip analog for
  M-D (move to left stick click; drop the current recenter binding to
  B-button, `:1988`).
- Weapon audit list (public-docs first, per-weapon clean-room spec pass only
  if docs are insufficient): Sniper zoom, Redeemer guided alt, Ripper,
  Translocator (`AdjustToss`), Bio Rifle lob. Each gets a one-line "aims
  correctly / needs wider intercept / needs spec pass" disposition.

**Definition of done**: with `--debugvrhands` + a synthesized fire press
(`InputEvent(IK_LeftMouse, ...)` from a debug path or timed script), the log
shows intercepted fires whose direction matches the fake hand rotator, while
the movement-diag lines (`Engine.cpp:1849-1855`) still show velocity heading
tracking *head* yaw — i.e. aim and movement demonstrably decoupled in one
log. In-headset: shots land where the controller points for hitscan and
projectile weapons; automatic fire (minigun/pulse) tracks a moving controller
*while held* — this last point is the explicit regression test for the
"automatic re-fire misses the edge-trigger" trap.

**Verification mode**: log-verifiable non-interactively to a high degree
(direction math, decoupling); final accuracy/feel needs the user in-headset.

### M-D — Two-handed aim from the between-hands vector

**What changes**
- Foregrip grab state machine (in `UpdateVRControllerInput`,
  `Engine.cpp:1789`): grab when off-hand grip analog > 0.6 **and**
  `|offHandGripPos − foregripWorldPos| < R_grab` (~12 cm · `UUPerMeter`);
  release when analog < 0.4 **or** distance > R_release (~25 cm) — dual
  hysteresis. Foregrip point = authored per-weapon offset (M-B table)
  transformed by the current viewmodel pose.
- While gripped, `weaponAimRotator(mainHand)` switches to the two-hand model:
  direction `normalize(offGrip − mainGrip)` (grip poses), roll from the main
  hand's up axis; viewmodel `Rotation()` (M-B intercept) and fire rotation
  (M-C intercept) both consume it automatically — no new seams.
- Stability: if hand separation < ~15 cm, blend toward single-hand aim
  (angular noise ∝ error/baseline); ~100 ms slerp on grab/release
  transitions; optional EMA low-pass with an ini-tunable constant (default
  off or very light — H3VR-style "hand smoothing").
- Only weapons flagged two-handed in the per-weapon table participate
  (rifles yes, Enforcer no).

**Definition of done**: `--debugvrhands` extended with a scripted
grab-release sequence; log shows the aim rotator switching models with
hysteresis and no discontinuity > a few degrees at transition. In-headset:
holding a rifle two-handed visibly steadies aim vs one hand, and releasing
the foregrip hands aim back smoothly.

**Verification mode**: state machine + math non-interactive; stability
*tuning* (R_grab/R_release, blend times, filter constant, minimum baseline)
genuinely needs the user in-headset — flagged as an explicit tuning session.

### M-F — Left/right-handed mode (hand-role swap + mirroring)

**What changes**
- Setting: `--vr-lefthand` + persisted ini entry; sets the `mainHand` index
  from M-A. Because every milestone consumes hands only via
  `MainHand()`/`OffHand()`, the functional swap is one variable. Trigger
  mapping swaps with it (fire = main-hand trigger, alt = off-hand;
  `Engine.cpp:1936-1937`); locomotion stays on the left stick (VR
  convention), revisit only on user feedback.
- Mesh mirroring: UT99 viewmodels are authored right-handed (baked right
  arms). Mirror by injecting a weapon-local `scale(1,−1,1)` into the
  transform at `Render/VisibleMesh.cpp:21` for the local player's weapon
  draw only, when left-handed mode is on. **Open technical check**: negative
  determinant flips triangle winding — verify whether the Vulkan mesh path
  culls back-faces for viewmodels; if it does, the draw needs a
  front-face/cull flip for this actor. Fallback (ships fine, several VR
  games do this): don't mirror the mesh at all — mirror only the grip/
  foregrip/muzzle offset math about weapon-local Y and accept a
  right-handed-looking model in the left hand.

**Definition of done**: `--debugvrhands --vr-lefthand` screenshot shows the
viewmodel mirrored (or the documented fallback) and anchored to the other
fake hand; fire log shows fire following the left hand's rotator.

**Verification mode**: mirroring *correctness* is screenshot-verifiable;
whether the mirrored model "reads" correctly to the eye needs the user.

### M-E — Dual-wield (independent per-hand aim + fire)

Most speculative milestone; scoped in two phases to de-risk.

**E1 — Dual Enforcers via the stock master/slave pair.** UT99's stock "double
enforcer" is (per community knowledge) implemented in Botpack as a
master/slave weapon-actor *pair* — i.e. two live weapon actors already exist
in exactly the stock case the user cares most about. Plan: identify the slave
actor natively (property lookup on the Enforcer class — name and semantics
**need verification, likely the first real candidate for a clean-room
decompile-spec pass**, since public docs on the slave mechanism are thin);
route the M-B/M-C intercepts per-actor — master follows `MainHand()`, slave
follows `OffHand()`; drive the slave's fire from the off-hand trigger
(today's AltFire mapping, `Engine.cpp:1937/1968` — dual-pistol mode has no
separate alt-fire, matching flat UT99 where double-enforcer alt does nothing
different). Risk: stock script may alternate fire timing between the pair;
if its state machine fights independent triggers, E1 degrades to
"alternating fire but per-hand aim," which is still a large win.

**E2 — Generic dual-wield of arbitrary one-handed weapons.** Requires new
native per-hand weapon bookkeeping alongside the single `Pawn.Weapon()` slot
(`UActor.h:2002`) — spawning/holding a second weapon actor the HUD/ammo/
selection script never expects. Explicitly **out of committed scope**;
documented here as a future design problem (per-hand inventory slots, HUD
ammo for two weapons, weapon-switch UX with a gun in each hand), to be
revisited after E1 ships and real usage shows whether anyone wants
Shock+Ripper akimbo.

**Definition of done (E1)**: log shows two distinct weapon actors receiving
distinct aim rotators and firing on their own triggers (or documented
alternating-fire degradation); in-headset, each pistol tracks its own hand.

**Verification mode**: actor bookkeeping + per-actor aim log non-interactive
via `--debugvrhands`; everything about how it *feels* needs the headset.

## Open risks / unknowns

1. **Per-weapon fire-path conformance** (biggest risk): the whole M-C design
   assumes stock weapons fire through script functions *named*
   `TraceFire`/`ProjectileFire` that read `ViewRotation`/`AdjustAim` in the
   documented way. Conventional and documented for the base class; individual
   Botpack weapons (Sniper zoom, guided Redeemer, Ripper, Translocator, Bio
   lob) need a per-weapon audit — public docs first, two-agent clean-room
   decompile-spec pass for any weapon docs can't settle. The seam itself
   makes per-weapon patches cheap (widen the name set), so this is contained
   risk, not architectural risk.
2. **Enforcer master/slave mechanism** (M-E1): slave-actor discovery and its
   fire-alternation state machine are not well documented publicly — likely
   needs the clean-room spec process before E1 is implementable.
3. **`CalcDrawOffset` return convention**: documented as Owner-relative;
   verify empirically (one log line) before the M-C phase-2 origin intercept
   trusts it.
4. **Mirroring vs backface culling** (M-F): negative-determinant transform
   needs a winding/cull flip if the Vulkan mesh path culls; needs a quick
   renderer check at implementation time, with a no-mirror fallback that
   ships regardless.
5. **Headset-tuning constants**: grip offsets per weapon, grab/release radii,
   min two-hand baseline, blend/filter times — all need the user in-headset;
   plan budgets a dedicated tuning session after M-D rather than per-
   milestone headset trips.
6. **`xrSyncActions`/focus dependency**: pose actions read invalid whenever
   the session isn't `FOCUSED` (same failure mode already diagnosed for
   buttons, `VulkanXRSession.h:44-55`) — all intercepts must degrade to stock
   behavior on `valid=false` so a dashboard grab mid-fight never leaves the
   gun frozen or the ViewRotation swap stuck (the save/restore scoping
   guarantees the latter structurally).
7. **Aim pose vs grip pose trim**: Touch's aim pose is defined by the runtime,
   not the game — the per-weapon rotational trim must be tuned so barrels
   visually align with where shots go (one shared trim may suffice; verify
   in-headset).

## Sources

- [Unreal Wiki — Legacy:Hitscan Weapons (unrealarchive mirror)](https://unrealarchive.org/wikis/unreal-wiki/Legacy:Hitscan_Weapons.html) — TraceFire start/end/AdjustedAim math.
- [UnCodeX — Engine.PlayerPawn](http://uncodex.ut-files.com/Unreal/216/engine/playerpawn.html) — AdjustAim signature/placement.
- [BeyondUnreal Wiki — Weapon (UT)](https://beyondunrealwiki.github.io/pages/weapon-ut.html) — Weapon property reference (FireOffset, AdjustedAim).
- [Pavlov VR — two-handed aiming mechanics discussion](https://steamcommunity.com/app/555160/discussions/0/1489992080524926052/) — position-only two-hand model and its trade-offs.
- [H3VR Wiki — Gun Stabilization](https://h3vr.fandom.com/wiki/Gun_Stabilization) — virtual stock, proximity stabilization, hand filtering/smoothing.
- [Unity XR Interaction Toolkit two-handed grab (attach transforms)](https://medium.com/@Brian_David/how-to-create-two-handed-vr-interactions-using-unitys-xr-grab-interactable-component-f62c3bd7f56e) — grip-socket/attach-point pattern.

## M-C per-weapon fire-path audit

Per Open Risk #1 above, this audits whether the M-C `TraceFire`/
`ProjectileFire` intercept (`Engine::HandleFrameCallIntercept` in
`SurrealEngine/Engine.cpp`) actually covers each named weapon's real fire
path. Method: read decompiled UT99 469b UnrealScript source
(`Slipyx/UT99` on GitHub — a public, pre-existing community decompile,
used here for research only; no UT99 script or game data has been copied
into this repository) for `Botpack/SniperRifle.uc`, `Botpack/ripper.uc`,
`Botpack/Translocator.uc`, and `Botpack/ut_biorifle.uc`. The Redeemer's
weapon/missile classes are not present in that decompile tree (searched
for `Redeemer`/`missile`/`nuke`/`screamer` across the full repo tree —
only `NoRedeemer.uc`, a server-option mutator stub, and `Redeemertrail.uc`,
a cosmetic trail effect, exist), so its disposition rests on secondary
docs (Unreal Wiki/Fandom, UT99.org) plus general UE1 architecture
knowledge rather than a direct source read.

- **Sniper zoom (`Botpack/SniperRifle.uc`)** — aims correctly via this
  intercept. `AltFire`/`ClientAltFire` (lines 159–168) only does
  `GotoState('Zooming')` — a FOV change, not a separate fire/aim path. The
  actual shot is `TraceFire(float Accuracy)` (line 253), which builds
  `StartTrace` from `Owner.Location`/`PawnOwner.Eyeheight` and calls
  `PawnOwner.AdjustAim(...)` — reading `PawnOwner.ViewRotation` at the
  moment of the call, which is exactly the value our pre-hook swaps to
  `WeaponAimRotator(MainHand())` before the script body runs. Zoom being
  active doesn't change which function fires or what it reads.
- **Ripper (`Botpack/ripper.uc`)** — aims correctly via this intercept.
  Both primary fire and `AltFire` (line 89, the bounce-blade mode) route
  through the same overridden `ProjectileFire` (line 46): `AltFire` calls
  `ProjectileFire(AltProjectileClass, AltProjectileSpeed, bAltWarnTarget)`
  directly (line 103). `ProjectileFire` reads `Pawn(owner).ViewRotation`
  via `GetAxes(...)` and `AdjustAim(...)` (lines 51–53), so both fire
  modes are covered by the single intercepted function name.
- **Translocator `AdjustToss` (`Botpack/Translocator.uc`)** — needs a
  wider intercepted-name set: `Fire`/`ThrowTarget` (Translocator-specific
  names, not `TraceFire`/`ProjectileFire`). The disc toss is
  `Fire()` (line 185) → `ThrowTarget()` (line 379), which is a distinct
  function name our current intercept does not match at all — so in VR
  today, a translocator throw is aimed by whatever `ViewRotation` already
  holds (head yaw/pitch), not by the hand rotator. Worse, `ThrowTarget`
  line 391 does `Pawn(Owner).ViewRotation =
  Pawn(Owner).AdjustToss(TossForce, Start, 0, true, true);` — a
  **permanent** write to the pawn's `ViewRotation` (a vanilla flatscreen
  UX quirk: the camera snaps to the throw arc), not a transient read like
  `TraceFire`/`ProjectileFire`. A naive save/restore wrap around
  `ThrowTarget` would compute `AdjustToss` from the swapped hand rotator
  (correct aim), but then our post-hook's restore-to-baseline would
  immediately undo the intentional permanent camera-snap the stock game
  performs — fighting the vanilla behavior instead of just redirecting
  aim. This needs a dedicated M-C-follow-up pass that decides whether to
  (a) keep the permanent write but seed it from the hand rotator instead
  of restoring afterward, or (b) suppress the permanent write entirely in
  VR (arguably correct, since head-tracked camera should never be
  puppeted by a throw). `AltFire` (line 346) is the detonate/recall
  action (`Translocate()`) and is not aim-related.
- **Bio Rifle lob (`Botpack/ut_biorifle.uc`)** — aims correctly via this
  intercept. `UT_BioRifle` doesn't override primary `Fire`, so it uses the
  base `Weapon`/`TournamentWeapon` fire state machine, which calls the
  overridden `ProjectileFire` (line 144) for the instant glob. The charged
  alt-fire's release-throw also goes through the same `ProjectileFire`
  override, via `state ShootLoad`'s `BeginState()` (line 276:
  `Gel = ProjectileFire(AltProjectileClass, AltProjectileSpeed,
  bAltWarnTarget);`). Both read `Pawn(owner).ViewRotation` (line 149) and
  call `AdjustToss` (line 151) at call time, so the swap covers them. Note:
  `state ShootLoad`'s `Timer()` (lines 248–265) spawns extra
  `AltProjectileClass` splash globs using `Owner.Rotation` with random
  yaw/pitch jitter, not `ViewRotation`/`AdjustAim` — but this is the
  glob's own proximity-triggered secondary burst effect, not a
  player-aimed action, so it's correctly out of scope for this intercept.
- **Redeemer guided alt-fire** — needs a clean-room decompile-spec pass;
  docs insufficient (and this decompile tree doesn't contain the
  Redeemer/missile classes to check directly). Public docs (Unreal
  Wiki/Fandom, UT99.org) describe the *behavior* (primary fire launches a
  nuclear missile; alt-fire launches a slower missile that the firing
  player then steers in first person until impact) but not the exact
  function/property names. By general UE1 architecture, the initial
  launch is very likely a normal `ProjectileFire`-based call (same
  pattern as every other rocket-type weapon above) and so probably aims
  correctly via this intercept already; the guided-flight *steering*
  phase, however, is almost certainly implemented as direct actor
  possession (the player's view/controls are handed to the missile actor
  itself, similar to a vehicle), which is a fundamentally different
  control path that never calls `TraceFire`/`ProjectileFire` at all and
  that this milestone's intercept structurally cannot address. Confirming
  the exact mechanism (and whether/how to redirect the missile's steering
  input to hand orientation) needs the two-agent clean-room decompile-spec
  process called out in Open Risk #1, using the actual Redeemer/
  `SeekingRedeemer`-equivalent class once sourced.

**Summary**: 3 of 5 (Sniper zoom, Ripper, Bio Rifle lob) aim correctly
through the M-C intercept as built. 1 of 5 (Translocator) needs a wider
intercepted-name set (`Fire`/`ThrowTarget`) plus a design decision about
the permanent `ViewRotation` write. 1 of 5 (Redeemer) needs a clean-room
decompile-spec pass — both because the guided-steering phase is
architecturally outside this intercept's reach and because source for
that weapon wasn't available in the decompile used for this audit.
