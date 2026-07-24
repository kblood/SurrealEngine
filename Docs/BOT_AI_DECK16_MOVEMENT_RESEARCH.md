# UT bot locomotion and hazard research

## Scope and confidence

This note narrows the Deck16-II investigation to native movement, route cost,
local hazard avoidance, and stuck recovery. It complements
`BOT_AI_QUALITY_EXECUTION_PLAN.md`; it does not replace the benchmark matrix.

The preserved fork's three-seed comparison is material evidence: mean Deck16
no-progress fell from about 36.33 seconds to 6.97 seconds and stuck proxies from
4.33 to 1.33. It is not evidence that the whole fork is merge-ready. Its same
Deck16 runs produced 15 deaths, 7 attributed kills, 7 environmental deaths, and
one self-fatal death. Traces repeatedly show `instigator=None` `Corroded` damage
while bots remain in slime. Movement correctness and hazard survival therefore
need separate gates.

No Deck16 map asset was added or inspected as part of this slice. Map-specific
causal claims below are benchmark-informed hypotheses until replayed against
the owner's retail data. The code-contract findings are directly reproducible.

## What unified already received from the earlier bot work

The following navigation commits are ancestors of the current unified branch,
not missing fork-only work:

| Commit | Present behavior | Remaining limitation |
| --- | --- | --- |
| `376abf05` | initial navigation search corrections | distance still dominates route choice |
| `c13cfb7b` | `FindPathToEndPoint` returns path and distance | no hazard cost in the path expansion |
| `6ce6c0ba` | further path extraction/selection correction | reach flags remain TODO |
| `cc4dd623` | calls a path point's `SpecialHandling` | depends on a correct route reaching that point |
| `0d1855d3` | bounds `SpecialHandling`/`FindPathToward` recursion | does not make unsafe paths expensive |

This matters for integration: bulk-porting these commits would duplicate code.
The preserved parity benchmark commit `a65ae787` contains later native fixes
that unified does not have, mixed together with extensive instrumentation.
Those fixes should be reconstructed one at a time with focused tests.

## Exact contract gaps

### 1. Arrival is declared far too early

Unified `UPawn::TickMoveTo` completes walking movement when
`distanceSquared < velocitySquared * 0.05`. At 300 uu/s that threshold is about
67 uu, regardless of frame duration, collision cylinder, or whether the target
is vertically touchable. A non-colliding NavigationPoint can consequently be
reported reached outside the distance at which script route-cache code consumes
it. The script can select the same node again, producing apparent completion,
reselection, and oscillation while the bot makes little progress.

The fork changes this to an elapsed-time envelope:

`threshold = max(target acceptance radius, horizontal speed * elapsed + 1)`

It also distinguishes colliding actors from point-like navigation actors and
rejects a walking arrival when the target's vertical separation is not
touchable. Of the fork-only candidates, this is the smallest fix most directly
supported by the measured no-progress improvement. It should be reconstructed
as an isolated patch and fixture before importing any broader path or policy
change.

### 2. Actor blockers can swallow the stock `HitWall` recovery

`Botpack.Bot` defines state-specific `HitWall` handlers that enter labels such
as `AdjustFromWall`; movers are also coordinated through that callback. In
unified `TickWalking`, a `UPlayerPawn` collision with a non-pushable colliding
actor enters a TODO branch and sends no `HitWall`. UT bots inherit through the
player-pawn hierarchy, so doors, movers, pawns, and similar actor blockers can
bypass the script recovery path. Fork fixture `f567c6a3` was built specifically
around this contract.

This is a narrow correctness candidate, but it is secondary to the arrival
envelope for the current stuck symptom: the benchmark evidence does not yet
attribute every no-progress span to a wall or mover contact.

#### Wall-adjust distance audit

The exported UE1 `Engine.Pawn` contract says `PickWallAdjust` should first test
whether a knee-height obstruction can be jumped, then try a destination 90
degrees right or left using outward traces and floor checks. Stock UT436 Bot
states set `Focus = Destination`, call `PickWallAdjust`, then execute
`StrafeTo(Destination, Focus)` followed by `MoveTo(Focus)`. A successful native
call must therefore leave a real temporary side destination while preserving
the blocked route goal in `Focus`.

The public script contract does not expose the retail native distance. Two
same-generation data points constrain a focused reconstruction: UT's
`TournamentPlayer` collision radius is 17 uu, and UE1 `Pawn.CheckWaterJump`
uses `2 * CollisionRadius` as its body-clearance trace distance above an
obstruction. The existing one-unit wall adjustment is neither body-scaled nor
executable under the corrected arrival envelope. The focused correction uses
one collision diameter for the lateral dry run and destination while retaining
the existing jump-first, right-first, then left ordering. This is an
evidence-based compatibility reconstruction, not a claim that the closed
retail native used the identical constant. Floor prediction and jump-selection
parity remain separate work.

### 3. Walking ledges bypass the script veto

Unified walking physics switches immediately to falling when the step-down
probe finds no floor. Stock Bot code has state-specific `MayFall` handlers and
temporarily enables `bAvoidLedges`/`bStopAtLedges` during tactical movement.
The fork calls `MayFall` before committing to a drop and restores the last
grounded position if script clears `bCanJump`.

This is valid parity work, but not a complete Deck16 fall fix. Current traces
often show falls being allowed, so a callback alone cannot decide whether a
landing is survivable. Route reach flags and bounded landing prediction remain
necessary.

### 4. Pain cost is calculated and then ignored by route selection

The local `Botpack.PainPath` source exposes both `SpecialCost` and
`SpecialHandling`. Its cost is very high unless the seeker has the matching
reduced-damage type. Unified `ClearPaths` calls `SpecialCost` and stores
`NavigationPoint.cost`, but `FindPathToEndPoint` accumulates only reachspec
distance. `FindBestInventoryPath` even retains a commented-out `cost +=
nav->cost()` line. The result is internally inconsistent: direct reachability
rejects some pain destinations, but graph search can still choose a route
through a pain path because the route never pays its cost.

This is the strongest code explanation for repeated slime exposure. Fix route
cost first, then add local escape steering for cases where combat, knockback,
spawn position, or an already-committed segment puts the pawn inside pain.

### 5. Jump-adjustment hypothesis was tested and rejected

Unified `EAdjustJump` simulates a jump starting from `JumpZ`, toward the current
height, and initially aims at `Focus`. Stock scripts can set boosted vertical
velocity immediately before calling it, and `Focus` may be an enemy while
`Destination` is the landing goal. A prototype instead solved flight time from
the current vertical velocity and gravity to `Destination.Z`, then derives the
horizontal velocity toward `Destination`.

This is relevant to bad jumps and splash/impact boosts. It is not a substitute
for proving that the landing corridor is supported and below a safe drop.

The exported Botpack script motivated that prototype: ordinary jump
setup assigns `Velocity.Z = JumpZ` immediately before `Velocity =
EAdjustJump()`, while impact-jump paths assign a computed boosted `velZ` or
`Default.JumpZ + velZ` first. The prototype covered level, uphill, downhill,
boosted, zero-gravity, no-solution, and invalid inputs in a pure fixture, but
the live benchmark did not establish a quality improvement. It has been
removed, and the stock `EAdjustJump` behavior remains in use. Any future jump
change must first add supported-landing and safe-drop evidence, then pass the
fixed UT and Unreal matrices independently.

## Design lessons from other FPS bot implementations

These projects converge on a useful separation between global routing and
bounded local movement:

- [Quake III Arena's GPL bot movement](https://github.com/id-Software/Quake-III-Arena/blob/master/code/botlib/be_aas_move.c)
  predicts movement with explicit stop events for slime, lava, gaps, and ground
  damage. Its higher-level movement code also keeps bounded avoid spots and
  avoids repeatedly failing reachabilities.
- [Recast/Detour's path corridor](https://github.com/recastnavigation/recastnavigation/blob/master/DetourCrowd/Source/DetourPathCorridor.cpp)
  continually feeds actual local movement back into a bounded corridor rather
  than assuming the original global path remains exact.
- [YaPB terrain handling](https://github.com/yapb/yapb/blob/master/src/navigate.cpp)
  confirms a suspected collision over time, then tries a bounded set of jump,
  lateral-strafe, and duck candidates while retaining short recovery history.
- [ReGameDLL CS bot navigation](https://github.com/rehlds/ReGameDLL_CS/blob/master/regamedll/dlls/bot/cs_bot_nav.cpp)
  measures average velocity over a window, records the stuck location, and does
  not clear the condition until meaningful displacement occurs.
- [UT99 `PainPath`](https://github.com/Slipyx/UT99/blob/769bd788cb2c06077a26ff7c3e4fced521a2169e/Botpack/PainPath.uc)
  confirms that hazardous navigation points are intended to participate in
  native route cost and special handling.

The implementation here is original, engine-independent code. The GPL Quake
sources inform behavior and test categories only; no code is copied.

Quake III's bounded `AAS_PredictClientMovement` pattern was also applied more
directly to unsupported UE1 walking transitions: fixed-frame cylinder sweeps
are paired with `UModel::FindRegion` foot samples, corresponding to Quake's
bbox trace and lava/slime point-content stop events. The UE1-specific danger
rule remains the existing `PointReachable` contract: reject a newly entered
`bPainZone` unless its damage type matches `ReducedDamageType`.

Public UT99 script documentation provides useful but incomplete jump evidence:

- [`Bot.uc`](https://project-archives.etc.cmu.edu/2001/fall/coyote210/Docs/undox/Botpack.Bot.html)
  sets velocity before `EAdjustJump`, uses `Destination` for its descending
  branch, and treats a low adjusted horizontal speed as meaningful there.
- [`Pawn.uc`](https://project-archives.etc.cmu.edu/2001/fall/coyote210/Docs/undox/Engine.Pawn.html)
  exposes `Destination` and `Focus` as distinct movement and look fields but
  contains only the native `EAdjustJump` declaration.
- [OldUnreal UT patches](https://github.com/OldUnreal/UnrealTournamentPatches)
  contain no public native `EAdjustJump` implementation. A reported
  [wall/retarget loop](https://github.com/OldUnreal/UnrealTournamentPatches/issues/1568)
  is behaviorally relevant but not an oracle for Surreal's engine code.

Consequently, `Destination` and caller-provided vertical intent are the best
supported semantics, but the exact horizontal adjustment remains unknown.
Three reconstructed variants were benchmarked and all regressed at least one
map. None is suitable as canonical runtime behavior yet.

## Measured wall-slide and rollback failure

The fixed-seed traces show that some apparent intentional slime entries are a
walking collision sequence, not simply a bad high-level goal. Necroth had an
already-selected `MoveToward BulletBox4`, but iteration-38 tracing corrected
the earlier causal claim: the target was live before the recent ledge veto, so
a new post-veto `ActorReachable` decision was not available to cancel the
in-flight move. Direct inventory selection is useful context, not a proven
single interception point. The pawn crosses the last supported edge, the
forward fall prediction immediately hits a vertical wall, and the original
fail-open collision rule never samples the pain zone below. Continuing a
bounded prediction through two near-vertical contacts recognized both
reproduced Deck16-II cases, but the first live implementation saved only one
bot.

The failed case explains why prediction and enforcement must be qualified
separately. Necroth seed 104729 received a veto at the correct tick, but the
walking loop restored only to its current inner-iteration start. An earlier
sub-iteration in that same tick had already moved the pawn beyond support, so
the bot began falling on the next tick and died. Sarena seed 271828 remained
grounded after the same class of veto and survived. Four follow-up enforcement
variants then tried whole-tick rollback, exact floor verification, a selected
supported retreat, and four real collision-checked reverse steps. None changed
Necroth's next-tick fall or death, and the last variant introduced no aggregate
survival gain. The wall-slide continuation and synchronous retreat are
therefore removed from live runtime. Increasing prediction breadth alone
cannot fix this failure.

Unreal Gold `DmDeathFan` exposes a different bound. Dante falls for 2.47
seconds before entering the harmful zone, beyond the current 1.5-second
prediction horizon. Iteration 22 was therefore exactly trajectory-identical
to the prior Unreal baseline and emitted no veto. A longer or height-adaptive
horizon is required for that map, but it should be tested only after Deck's
rollback contract is reliable so longer prediction does not amplify ineffective
vetoes.

### Bounded adaptive-horizon design

The DeathFan trace gives concrete limits: Dante leaves support at tick 546 at
`Z=1384` and enters pain at tick 694 at `Z=-300.85`. The trajectory lasts
2.4667 seconds, descends 1684.85 units, and travels 2174.6 units. Keep the
current 24 fixed `1/16`-second steps as a 1.5-second base phase. Only when that
phase remains a valid descending fall in the original non-pain zone, with no
walkable landing or dynamic mover contact, extend it to at most 44 total steps
(2.75 seconds). Retaining the existing step size avoids introducing a second,
coarser physics model.

Bound the whole prediction to 2048 units of downward displacement, 4096 units
of accumulated path, 64 collision sweeps including wall retries, and 256 zone
samples. Keep the existing two near-vertical wall responses per step. Stop
safely on a walkable floor and report danger immediately on a harmful pain-zone
sample. Invalid geometry, a non-walkable/nonvertical collision, a dynamic
mover, an unexpected non-pain zone transition, insufficient samples at the
required spacing, or any exhausted bound must fail open. A segment that crosses
a spatial bound should be traced and sampled only up to that bound before the
prediction stops.

This is adaptive in cost as well as distance. Deck's reproduced Necroth fall
reaches slime after only about 0.55 seconds and 158 units, so it stays inside
the unchanged base phase. Under DeathFan's measured `-1045` gravity, the model
misses the pain depth after 24 steps but reaches it by step 29; the expected
increment is therefore about five sweeps rather than all 20 optional steps.
The hard 64-sweep ceiling is also below the current theoretical 72-sweep worst
case of 24 steps with two collision responses on every step.

Focused tests should prove that the DeathFan start/gravity/depth misses at step
24 and is detected by step 29; a walkable floor found during the extension wins
over pain below it; mover, zone-change, invalid-collision, and each resource
cap fail open; and the reconstructed Deck wall/slime case is still detected
without entering the extension. The fixed DeathFan seed 424242 integration run
must then veto Dante's measured ledge transition without a hazard entry, while
the complete Deck/Morbias matrix must show no survival, combat, wall, or stuck
regression. Do not enable the longer live horizon until a different enforcement
contract can keep a vetoed pawn on demonstrably safe support and pass its Deck
fixtures; iterations 23 through 26 did not establish that contract.

### Later wall-jump forecast evidence

Iterations 37 through 41 covered the rejected inventory handshake and then
isolated Visse's seed-314159 Wandering wall jump:

| Iteration | Finding | Outcome |
| --- | --- | --- |
| 37 | forecast began with pre-script velocity, not the prospective adjusted jump velocity | invalid and neutral; reject |
| 38 | a recent-veto/native reachability handshake first spun the VM, then completed only after handshake repair | K0/D3/S3/E3/score -3; reject and exactly roll back |
| 39 | correct jump input reached the first static wall, slid, then retained inward velocity and failed open on the next same-wall contact | byte-identical neutral result |
| 40 | same-surface identity and actual `TryMove`-style actor blocking were diagnosed; compliant clean run remained byte-identical | no quality gain |
| 41 | bounded repeated contact with the same BSP surface finally changed the trajectory | intent stall improved, but hazard entries/deaths worsened; reject |

The pure Visse fixture makes the iteration-39 stop concrete: the first slide
ends with a small velocity component back into the wall. On the next fixed
prediction step that produces another contact, but the one-contact allowance
has already been consumed. Iteration 40 also exposed a separate modeling risk:
generic `TraceFirstHit` actor flags do not reproduce the pawn/other actor
blocking pairs used by `TryMove`, so nonblocking actors can create false
dynamic collisions unless the forecast applies the real blocking filter.

Iteration 41 bounded continued contact to the same BSP surface and reduced
seed-314159 movement-intent no-progress from 10.62 to 4.45 seconds. It did not
save Visse: hazard entries and hazard-exposed deaths increased from one to two,
while K2/D3/S1/E1 and score +1 were unchanged. That is a safety regression, so
the live wall-jump forecast is rolled back and only the pure diagnostics and
fixtures remain. Repository-local-build diagnostics are noncompliant with the
central output policy and do not qualify; the admissible iteration-40 clean run
was neutral.

The first iteration-38 run stopped at tick 358 without a summary. The repaired
handshake completed but regressed seed 104729 from K2/D4/S2/E2/score 0 to
K0/D3/S3/E3/score -3, increased wall callbacks from 291 to 395, and increased
intent no-progress from 5.88 seconds (longest 1.65) to 13.13 seconds (longest
6.00). Removing it restored the iteration-35 event stream exactly, including
SHA-256 `9fd10f192866...`; neither the VM spin nor the severe completed result is
part of the retained design.

## Deterministic lift recovery and cross-game evidence

The persistent `LiftExit3` loop required physical topology rather than exact
actor identity. A same-lift-only rule was neutral. Penalizing all lift exits
proved the alternate exit was part of the loop, but was too broad. The retained
pure relation groups only exits that share a nearby, same-zone, same-height
non-lift landing node. It reduced Cilia's seed-314159 recovery from two
activations and 4,261 applications to one activation and 85 applications while
preserving the first six-case iteration-35 result.

The first iteration-35 repeat then exposed an ASLR leak: overlap hits were
ordered by actor pointer, which changed `Touch` order and UT Morbias seed
104729. Stable world/level-index/name ordering removed the pointer dependency.
Repeated Unreal Gold `DmDeck16`/`DmMorbias` and `DmHealPod`/`DmDeathFan`
matrices then reproduced exactly. DeathFan still recorded one environmental
suicide, 1,609 wall callbacks, and a 2.27-second intent stall; determinism does
not make that behavior acceptable.

A rendered UT436 spectator smoke also proved that bot-only matches can use the
normal Vulkan path unattended: four stock bots ran on `DM-Morbias][` while a
`Botpack.CHSpectator` followed a bot and owned no combat pawn. The minimized
two-second lifecycle run proves setup, rendering, camera follow, and timed exit,
not subjective bot quality.

## Falling two-plane seam: iteration 42 rejection

DeathFan's long wall episode is a falling collision seam rather than the
walking stall already covered by the watchdog. A bounded pure model identified
two distinct non-walkable planes and iteration 42 applied one additional sweep
along their crease. At the established Unreal timestep (`0.0166667`), the
candidate reduced wall callbacks from 1,609 to 371 and removed the stuck proxy,
but increased unassisted environmental deaths from one to three. Ash fell
1,171 units and Dante 928 units from the released seam into the pain pool even
though both movement destinations were far above them.

This rejects an important assumption: a collision-valid crease is not a
survival-valid bot route. The live sweep is removed and rollback restores the
historical shadow stream exactly. A nearby timestep (`0.016666667`) happened
to produce a much better trajectory, but cannot be compared against the
historical run or used to hide the matching-timestep regression. Both values
are now explicit robustness cases.

The next safe contract is tri-state. `Safe` requires positive target progress
and a clear horizontal endpoint with nearby walkable, non-pain support;
`Harmful` rejects a proven dangerous landing; `Unknown` performs no extra
movement. A no-translation latent replan should be tested before any supported
outward nudge. Full multi-plane collision parity remains a separate engine
experiment because physical correctness alone cannot choose a safe route.

That no-translation experiment became iteration 43. It was exactly neutral on
the historical DeathFan stream and reduced wall contacts only on the nearby
timestep. On Deck seed 271828 it added one death, changed score 0 to -1, doubled
hazard entries/deaths from one to two, and introduced a 2.32-second intent
stall. Both new environmental outcomes carried recent enemy momentum, showing
why the complete causal partition must be combined with total-death, hazard,
combat, and movement gates. The live latent replan is removed. Only the pure
eligibility/projection and evidence-gated supported-escape models remain.

## Supported horizontal escape: iteration 44 shadow result

Iteration 44 wired the evidence-gated model as an observer only. For an
eligible static two-plane falling seam it builds a deterministic 24-unit
outward horizontal bisector, dry-runs the pawn sweep, and probes downward with
the full pawn extent. A candidate is merely classified as supportable when the
first support hit is static world rather than a pawn, mover, or other actor; the
normal is walkable; the supported foot region is known and non-pain; and the
endpoint makes positive progress toward the active tactical destination. The
observer never applies the candidate or changes movement state.

Rollback A/B comparisons were exact on `DmDeathFan` seed 424242 at historical
`0.0166667`, at nearby `0.016666667`, and on Deck seed 271828. DeathFan's
historical run produced 8 detections and 8 probes, all unknown or unsafe; its
nearby-timestep run produced 141/141 with the same classification. Deck
produced three probes, split into two target-progress rejections and one
unknown-or-unsafe-support result. All three comparisons authorized zero
escapes.

The tuning scan reinforced that result without opening held-outs. Deck seeds
104729 and 314159 recorded one and two unsafe probes. Morpheus recorded three
detections but only one valid, unsafe probe. No listed tuning case authorized a
candidate. The shadow instrumentation is retained as behavior-neutral causal
evidence, but it does not justify live movement: the known seams still lack a
proven supported escape, and held-out maps remain unopened.

Iteration 45 tested whether the bisector was simply the wrong escape direction.
The shadow observer now deterministically considers the bisector followed by
each individual horizontal wall normal, with duplicate removal and a strict
three-probe bound. It remains dry-run only. Historical DeathFan expanded from
8 to 24 unsafe probes and its persistent nearby-timestep episode expanded from
141 to 423 unsafe probes. Deck seed 271828 expanded from 3 to 9 probes; 8
regressed the active destination and 1 lacked safe support. Deck seeds 104729
and 314159 expanded to 3 and 6 probes, while Morpheus exposed 3 valid probes.
None were authorized.

Normalized gameplay events and summaries remained exact against iteration 44
for every comparison, and repeated shadow streams were exact. This rejects the
simple “try either wall normal” hypothesis on the measured failures without
risking live bot behavior. The 141 raw nearby-timestep detections are still one
long Ash seam episode rather than 141 independent navigation failures, so the
next observer should count deterministic episodes and separate blocked sweep,
missing support, pain support, true destination regression, and unknown intent
instead of treating all non-authorizations alike.

## Pre-commit walking support is the next intervention point

The Deck traces now point to a timing defect rather than another falling escape
direction. `TickWalking` mutates the step-up/forward/slide path before its final
floor-support decision. The bounded pain fall forecast also returns no hazard
when its first trace hits a wall. In seed 104729, Necroth becomes falling at
tick 471 while targeting `BulletBox4`, does not receive the first wall contact
until tick 481, and reaches pain at tick 503. By then every post-hoc rollback or
falling recovery starts from an unsupported position; iterations 23 through 26
could not reconstruct the lost safe origin.

The narrow hypothesis is therefore: preflight the walking subiteration while
the bot is still on known support, and identify only those unsupported
endpoints whose bounded continuation reaches a known pain region. This must
begin as an observer. It applies only to live autonomous stock UT/Unreal bots
in `PHYS_Walking`, ordinary downward gravity, a known non-pain/non-water foot
region, finite walking deltas, and static-world collision evidence. Upward jump
impulses, movers, dynamic blockers, unknown zones, and non-stock or human pawns
produce no authorization.

The current `TryMove(delta, true)` is insufficient for the multi-stage
simulation because it always starts from the pawn's actual `Location`. The
first implementation step is an explicit-origin read-only collision probe
shared with `TryMove`. At the actual current origin, synthetic clear/static/
dynamic collision fixtures must prove identical hit fraction, normal, and
actor selection before chained step endpoints are trusted. Temporary actor
translation is forbidden even in shadow mode.

After that equivalence gate, shadow telemetry should distinguish unsupported
preflights, wall-continuation hazard authorizations, and fail-open reasons, and
debounce by pawn life, supported origin, and semantic target. A future live
veto is allowed only if the known Deck trace authorizes before tick 471 and A/B
evidence shows that cancelling one step while still supported removes the
hazard without adding deaths, pain entries, wall/stall time, score loss, or
determinism changes. Deck seeds 271828 and 314159 must remain unattributed when
their falls are enemy momentum or an explicit wall jump. Morbias, Unreal Deck/
Morbias, HealPod, and both DeathFan timestep spellings are required tuning
non-regressions; held-outs remain closed until the behavior is frozen.

The explicit-origin foundation and death-time measurement repair were accepted
together as iteration 46. `TryMove` retains a single ordered collision hit list
for both blocking selection and later touch processing, while the shared probe
can start at a caller-provided point without moving the actor. Death-time
counter accumulation is idempotent and keeps recent enemy damage/momentum
evidence intact. A combined UT/Unreal A/B on the two DeathFan timestep cases
and Deck seed 271828 preserved gameplay, causal deaths, summaries, and repeated
shadow streams exactly.

Detailed episode telemetry confirms the raw-count interpretation. Historical
DeathFan has two physical episodes and the nearby-timestep 141-detection Ash
trace is one episode; every candidate in both cases lacks static walkable
support. Deck seed 271828 has two episodes: two candidates occurred without an
active movement target, six were finite true target regressions, and one lacked
support. Deck seeds 104729/314159 have one/two episodes whose old target-
rejection labels were actually Sleep/no-target outcomes. Morpheus has three
episodes, two with invalid geometry; its valid probes are one blocked sweep and
two missing-support outcomes. No measured episode is authorizable.

The accepted pure walking-preflight model consequently does not reuse a seam
escape. It requires known static support, axis-aligned step-up/down and
horizontal forward/slide deltas, a bounded unsupported endpoint, at most two
near-vertical static wall continuations, a walkable pain landing, and explicit
knowledge that the pawn is not immune. Every incomplete or dynamic observation
is a named no-decision. The remaining task is to populate those observations
from explicit-origin traces before the real walking move and prove that the
known Deck ledge is detected early.

## Iteration 48 walking-preflight result

The runtime observer is restricted to the opt-in `bot-benchmark` headless
driver and remains behavior-neutral. It probes before the real walking move,
lets stock movement and `MayFall` execute unchanged, and correlates any fresh
post-callback confirmation by pawn life, walking invocation/iteration,
unsupported endpoint, and semantic target. It never vetoes movement or
restores the pawn. The candidate executable SHA-256 was
`DEC5F63752639238D2CE1256CD8AB34BBFED95CC43FF8F1309B3AB7E4C974ADD`.

All three Deck baseline/candidate comparisons (seeds 104729, 271828, and
314159) passed exact 5/5 artifact equivalence after excluding only the new
observer payload, exact observer counters, and output-directory provenance.
All three candidate `r0`/`r1` comparisons passed exact 5/5 equivalence with
only output-directory provenance excluded. Aggregate observer counts were
13,086 observations, 669 unsupported endpoints, 12,643 no-decisions, 443
provisional harmful-pain predictions, 67 post-`MayFall` confirmations, 23
debounced authorizable episodes, and zero diagnostic overflows.

The known Necroth fall remains the decisive failure. At telemetry tick 471 of
seed 104729, the observer reconstructs the unsupported endpoint from the prior
supported origin while Necroth targets `BulletBox4`, but the fall forecast is
still incomplete after two wall continuations. It records
`walking_step_preflight_reason_incomplete_fall_forecast_exact`, not an
authorization, so it cannot justify a live veto. Conversely, seed 314159 tick
55 is a useful negative control: Visse's forward BSP collision is rejected as
`collision_callback_required_script_transition_unknown`; the real `HitWall`
then invokes `EAdjustJump` and changes the pawn to falling. A read-only
preflight must not guess through that callback.

The pure part of the minimum native-parity helper set is now implemented and
unit tested: exact bounded `TickPhysics` substep scheduling,
`BeginFallingParityStep`, `ResolveFallingParityDirectSweep`, and
`ResolveFallingParityAlignedSweep`. Tests cover clear integration, speed caps,
partial blocked slides, displacement-derived velocity, the native direct versus
aligned `0.7071` threshold asymmetry, two-contact bounds, and fail-open malformed,
mover, dynamic, water, bounce, and exhausted-horizon evidence. No runtime
adapter calls these helpers yet. That adapter must use explicit-origin
`ProbeMoveCollision` sweeps and stop at script-visible `HitWall`,
`Touch`/`UnTouch`/`Bump`, mover, dynamic-actor, or zone-transition boundaries.
Any future restoration also requires a proven reverse move and static walkable
non-pain support after the real callback. Iteration 48 enables no live veto and
is not release-ready or merge-ready as bot behavior.

The same observer was also exercised on Unreal Gold 226b `DmDeck16` with its
native `UnrealShare.DeathMatchGame` adapter. Seed 104729 passed baseline/candidate
and candidate-repeat equivalence for all five artifacts. It recorded 5,353
observations, 624 unsupported endpoints, 107 provisional predictions, zero
post-`MayFall` confirmations, and zero overflows. This proves deterministic,
behavior-neutral coverage of the shared path in Unreal; it is not evidence for
a live policy.

## Iteration 49 pawn subzone callback parity

An exact-package audit found that `UPawn::UpdateActorZone` dispatched
`FootZoneChange` and `HeadZoneChange` on the old `ZoneInfo` with the pawn as
the argument. Retail dispatches both events on the pawn with the new
`ZoneInfo`, while leaving `FootRegion` or `HeadRegion` at its old value until
the callback returns. Retail also compares the old and new zone pointers
directly, without an old-zone null guard. The corrected shared implementation
now follows that contract and removes native pain/drowning timer writes that
conflicted with the exact Pawn script handlers.

This is binary-proven for both target games. UT436 `ULevel::SetActorZone`
compares/calls/assigns the foot region at `0x1039bd16`,
`0x1039bd24-0x1039bd45`, and `0x1039bd4e`, and the head region at
`0x1039bdc7`, `0x1039bdd5`, and `0x1039bdde`. Unreal 226b has the same order at
`0x1037c72a`/`0x1037c738`/`0x1037c762` and
`0x1037c7db`/`0x1037c7e9`/`0x1037c7f2`. The audited package hashes were UT436
`Engine.u` `D587EEF8...964C11` and `BotPack.u` `9E21D633...A6C5AE`, plus Unreal
226b `Engine.u` `6CC42698...5E9201`, `UnrealShare.u`
`10C688AD...3C487`, and `UnrealI.u` `B64BAB7E...FAAA4`.

The pre-fix executable SHA-256 was
`D541794E2BBF3BB239AD3E7C7239917E190890D5BE3C5F2BC648A19B1446E80E`;
the corrected candidate was
`25ACADC3F4441226F032B5D85668D4BE92882410698434ED49F4E62117F9F7DD`.
Candidate repeats were exactly equivalent across all five artifacts for three
UT Deck seeds plus Unreal `DmDeck16` and `DmDeathFan`.

The quality result is deliberately not labeled a win. UT seed 104729 changed
from K2/D4 to K1/D4, unassisted environmental deaths 1 to 2, hazard deaths 2
to 3, and walls 291 to 319. Seed 271828 kept K1/D2 but walls rose 380 to 399.
Seed 314159 changed from K2/D3 to K1/D4, unassisted environmental deaths 1 to
2, enemy-contributed environmental deaths 0 to 1, hazard entries/deaths 1 to
3, walls 422 to 626, and movement-intent stuck episodes 0 to 2. The first UT
A/B mismatch on every seed is 16 points of pawn health, not movement: the
restored script callback applies immediate retail pain damage that the broken
dispatch delayed or masked.

Unreal `DmDeck16` remained artifact-equivalent at K0/D0. On `DmDeathFan`, kills
stayed 3 while deaths fell 8 to 6, unassisted environmental deaths 5 to 3,
hazard entries 14 to 5, walls 643 to 605, and stuck episodes 1 to 0. Restoring
the callback is required engine fidelity and makes hazard measurement honest,
but the exposed UT survival regressions block bot release. A conservative
pre-entry pain-column shadow and realized falling trace must now show how to
avoid those entries without suppressing useful drops.

## Iteration 50 realized falling evidence

The runtime now has a benchmark-only trace that starts only after a real
walking-to-falling transition and compares each actual falling move with the
pure forecast. It records exact matches, mismatches, unknowns, and callback
barriers, plus pain entry and terminal landing/death/continuity outcomes. The
observer is deliberately downstream of `MayFall` and `HitWall`: if script
changes physics or movement, it observes the committed post-callback state or
records the callback barrier instead of forecasting through script.

On Deck seed 104729, Necroth's fall starts at tick 471. Clear realized steps
472 through 480 match with zero velocity, requested-delta, and endpoint error.
Tick 481 is a partial static-world collision with `HitWall` callback mask 256;
pain follows at tick 503 and swimming ends the same-life falling correlation at
tick 507. On seed 314159, Visse's `EAdjustJump` case becomes a `HitWall` barrier
immediately after the transition, which confirms that the pre-callback state
must not be used as the safety decision.

Across repeat runs, seed 104729 produced 77 matches out of 90 steps with 13
callback barriers and 85.56% comparable coverage. Seed 314159 produced 142
matches out of 152 steps with 10 barriers and 93.42% coverage. Neither case
had a mismatch, unknown, or record overflow. Unreal `DmDeathFan` added 99
matches out of 105 steps with six barriers and 94.29% coverage; `DmDeck16`
offered only a single callback-barrier step and therefore remains a coverage
gap. All candidate repeats were exact, and baseline/candidate gameplay was
exact after explicitly ignoring only the added bounded diagnostics.

The companion pure vertical pain-column prototype samples the full vertical
center/foot/head column only across static-world evidence and requires an
independent walkable support result. It fails open on movers, dynamic actors,
callbacks, sample gaps, and bounded-horizon uncertainty. It is not yet wired
to the known Deck transitions, so it cannot authorize a live ledge veto.

## Retail two-plane falling correction target

Exact disassembly of UT436 `Engine.dll` and Unreal 226b `Engine.dll` shows the
same retail algorithm after two nonwalkable falling contacts: call
`AActor::TwoWallAdjust`, make a third collision move, test the special ditch or
strict `normal.z > 0.7` landing condition, reconstruct only horizontal velocity
from realized displacement, and restore the iteration-start falling Z
velocity. Retail charges the selected time slice before moving; the aligned and
third moves consume collision residuals inside that slice rather than adding
those fractions back to outer time. Only an independent backlog can continue,
under an eight-iteration cap.

Surreal currently uses `0.7071`, stops after its aligned second move, never
calls the retail two-wall adjustment or third move, reconstructs Z from zero
displacement, and sets remaining time to zero. Athena on UT Deck and Ash on
Unreal DeathFan both enter repeated static BSP two-plane contacts at a fixed
position with zeroed velocity, so this shared mismatch is a direct causal
candidate for their permanent falls. The correction must update the existing
tests that currently codify Surreal's `0.7071` boundary and must be evaluated
separately from the behavior-neutral observer. Its gates are: remove the
fixed-position seam episode, introduce no new pain/pool fall, retain bounded
physics, match repeat runs, and improve or preserve survival and movement on
both games.

## Iteration 51 retail correction result

The shared runtime now implements the audited path, including per-slice
integration, strict 0.7 boundary, direct and second nonwalkable `HitWall`
callbacks, `TwoWallAdjust`, third move, ditch landing, charged outer time, and
pre-gravity Z restoration. A direct walkable landing skips `HitWall`, matching
retail. The pure model has the same boundary and restoration behavior, with a
gravity fixture that proves an integrated -70 Z returns to the iteration-start
-50 Z after wall reconstruction.

The first candidate was rejected before commit because it confused collision
residual with outer remaining time. Its first physical divergence was about
1.96x movement across UT and Unreal; later wall responses generated velocities
up to roughly 19,600 units/second and new environmental suicides. Final retail
time accounting selects and subtracts the whole 0.02-second slice first, so a
clear aligned/third sequence schedules no fourth sweep.

The final executable is
`8F7BF38B8C4CDFD2B815E7876BE531E7E00FCCE01B07DF171060DC5637D09139`.
Eight configurations ran twice with exact five-artifact repeats: three UT Deck
seeds, UT Morpheus, Unreal DeathFan at two seeds, DmDeck16, and DmHealPod.
All comparable falling steps remain exact and no record overflows occur. Direct
walkable landings initially produced a conservative `unknown` step followed by
a `landed` terminal; iteration 52 adds a distinct `matched_landing` step rather
than mislabeling the collision as clear.

Deck survival improves in all three seeds while unassisted environmental death
counts do not increase. Seed 314159 changes K4/D5 to K1/D2, hazard deaths 2 to
1, seams 3 to 1, and walls 525 to 473. Seed 104729 changes K2/D3 to K0/D1 with
the same single environmental/hazard death, but hazard entries rise 1 to 3 and
seams 1 to 13. Seed 271828 changes K3/D4 to K0/D1 and hazard deaths 3 to 1.
The reduced deaths do not make these bots strong: kills collapse from 9 across
the three baselines to 1, and repeated harmful entries remain.

DeathFan seed 424242 retains one environmental/hazard death while walls fall
1,483 to 262 and deaths 2 to 1, but a long no-progress interval remains. Its
seed-123 control plus Unreal DmDeck16 and DmHealPod remain death-free. Morpheus
exposes a different failure: walls rise 330 to 2,283, seam detections 3 to 433,
and hazard deaths 2 to 3. Each tick stays within the exact two-callback bound
and pawns keep moving tangentially, so this is repeated low-gravity stock wall
behavior rather than a frozen seam or time-duplication bug.

Therefore the correction is retained as engine fidelity, not accepted as a
complete bot improvement. The next Deck work must use actual-trajectory pain
classification to veto or replan harmful entries. Morpheus needs episode-
debounced repeated-contact policy that distinguishes useful low-gravity motion
from nonproductive wall cycling. Combat engagement must be measured and
restored; lower deaths alone are not sufficient.

## Iteration 52 matched-landing qualification

The realized trace now counts direct partial static-world landings with strict
`normal.z > 0.7` and <=0.001 numerical error as `matched_landing`. Mismatched
landing geometry remains a mismatch, and `landed` remains the separate terminal
record. This restores the exact step partition without hiding landing evidence
inside `matched_clear`.

The final observer executable is
`72A3EE06650AC4D7F560E8BAB27C23FAC24AA3FED0329A901AA6F64AA07630F8`.
Two exact repeats on Deck104729, Deck314159, Morpheus424242, and Unreal
DeathFan424242 produce zero mismatches, unknowns, and overflows. Comparable
coverage is respectively 92.14%, 94.58%, 97.73%, and 96.34%, with a 100%
matched fraction in every case. Explicit baseline/candidate comparisons are
gameplay-equivalent after excluding only the new landing/unknown diagnostic
counters, bounded records, and output paths. The slice improves measurement,
not bot behavior.

## Wall callback and ledge-property parity audit

Retail UT436 and Unreal 226b `Engine.Pawn` both define `MinHitWall` as a
threshold based on the hit-normal dot normalized velocity, and expose
`bAvoidLedges` plus `bStopAtLedges`. Surreal reflects all three properties but
`TickWalking` reads none of them. Walking currently calls `HitWall` only for a
hard-coded `-0.2 < HitNormal.z < 0.2` band. That can emit callbacks for vertical
glancing contacts regardless of `MinHitWall`, suppress opposing steep faces,
and makes stock changes from `-0.5` to `-0.35` inert. Falling likewise ignores
the threshold, although stock falling-state handlers often mask the mismatch.

The live properties must be honored rather than replaced by a global bot rule.
UT enables both ledge flags in `Wandering` and `TacticalMove`, conditionally
uses avoidance in `Hunting`, and has five specialized `MayFall` states. Unreal
never sets `bStopAtLedges`; it conditionally enables `bAvoidLedges` in tactical
and hunting movement and has only two specialized `MayFall` states. An
unconditional UT-style ledge stop would therefore change Unreal policy.

The first parity slice remains observational: record normalized movement dot
normal, `MinHitWall`, the current Z-band decision, the inferred threshold
decision, blocker kind, state, and enabled event; separately record ledge flags
around the existing unsupported-step/`MayFall` decision. A pure boundary
fixture should cover head-on `-1`, glancing `-0.25`, an opposing steep face,
both stock thresholds, all ledge-flag combinations, and the existing script
outcomes (retain/clear `bCanJump`, change physics, or delete). A retail boundary
oracle must confirm the comparison sign before any callback predicate changes.
Restoring these properties is important wall/ledge parity, but most known Deck
slime entries occurred in `Roaming`, so it is not by itself the Deck fix.

## Quality measurement truth boundary

The analyzer and executable gate evaluator now fail closed for missing runs,
invalid completion, unmatched selectors, and absent/null metrics. Live hooks
track canonical damage, actual `AddVelocity` momentum, known environmental
call scopes, nested calls, abnormal returns, and per-life reset. Five primary
counters form an exact monotonic partition of `deaths_exact`: direct self,
direct enemy, unassisted environmental, recent-enemy-contributed environmental
proxy, and ambiguous. The sixth momentum field is a subset proxy, not another
disjoint class. `suicides_exact` remains explicitly labeled legacy scoreboard
semantics.

UT Deck's live smoke partitioned three deaths as one direct self, one direct
enemy, and one unassisted environmental death. Unreal DeathFan seed 424242
partitioned two deaths as one direct enemy and one unassisted environmental
death while preserving all 1,802 older telemetry payloads after removing only
the new fields, and preserving the shadow stream byte-for-byte. Measurement is
therefore trustworthy enough to reject iteration 42 causally. Until the bot
safety gates and unopened held-out maps pass, the work is still not release-
or merge-ready.

## Proposed runtime architecture

1. Keep macro policies (`stock-botpack`, `utility-arena`, and
   `tactical-state`) responsible for goals, not per-frame collision response.
2. Repair the stock native contracts independently: elapsed-time arrival,
   actor-blocker `HitWall`, `MayFall`, correct jump adjustment, reach flags, and
   route costs.
3. Add a bounded engine adapter that samples at most 16 local candidate
   directions. Each probe reports clearance, supported landing/drop, predicted
   jump reachability, pain entry, and expected damage.
4. Run `MovementSafetyAdvisor` in shadow. It rejects pain entry, missing or
   excessive-drop landings, blocked corridors, and unreachable jumps; when
   already in pain it strongly prefers an exit. After one stuck second it
   prefers lateral candidates and deterministically rotates away from the last
   attempted recovery.
5. Only after shadow telemetry and fixtures agree with live traces should a
   command adapter apply the selected direction.
6. Keep weapon fire authorization separate. Add muzzle-to-impact clearance,
   blast radius, predicted self damage, friendly exposure, and escape-space
   inputs before allowing splash fire. The current macro policy observation has
   none of these, while stock `ClearShot` protection is partial and
   state-specific.

The first shadow slice is implemented in `BotMovementSafety.h/.cpp`, with a
strict standalone test in `BotMovementSafetyTests.cpp`. It is deliberately not
wired to live bot control.

## Focused execution plan

### Phase A: reconstruct native correctness

1. Add a synthetic moving-pawn fixture proving the old arrival expression
   completes early and reselects a non-colliding nav point.
2. Implement only the elapsed-time and target-envelope `TickMoveTo` change.
3. Re-run the six fixed Deck16/Morbias cases. Require no regression in match
   completion and compare no-progress/stuck distributions, not just means.
4. Independently add actor-blocker `HitWall`, ledge-veto, and `EAdjustJump`
   fixtures before considering those patches.

### Phase B: make routes hazard-aware

1. Add a synthetic graph with a short `PainPath` route and longer safe route.
2. Honor `SpecialCost` and legal reach flags with saturating path arithmetic.
3. Add telemetry for chosen route cost, pain-zone entry/exit duration, and
   environmental damage.
4. Require the safe route when affordable and rapid escape when already in
   pain; do not treat eventual death as the first indication of failure.

### Phase C: shadow local steering

1. Build deterministic probe adapters for a wall corridor, ledge, safe jump,
   excessive drop, and pain-zone edge.
2. Record every candidate status and score through the existing shadow path.
3. Compare `continue`, `steer`, `escape-hazard`, and `recover-from-stuck`
   recommendations with actual stock outcomes on Deck16.
4. Enable live steering only behind an explicit experimental selector after it
   reduces exposure and stuck time without increasing falls.

### Phase D: weapon safety

1. Create hitscan and splash-fire synthetic fixtures, including a close wall,
   close enemy, teammate, and no-escape landing.
2. Gate fire on predicted self/friendly damage independently of action policy.
3. Require zero avoidable self-fatal splash events in the fixed-seed smoke
   matrix before discussing skill-tier tuning.

## Merge recommendation

Do not merge the preserved parity fork wholesale. Unified already contains its
older path-search series, while the later benchmark commit combines valuable
native fixes, telemetry, and behavior changes. The current evidence supports a
sequence of narrow merges, starting with the arrival-envelope fixture and fix,
then hazard-aware route cost. Local steering remains shadow-only until the
Deck16 environmental-death signal improves. Difficulty levels should be built
only after competent survival and locomotion are stable.

Current status remains **not ready to merge**. Keep held-out maps unopened,
remove the rejected live wall-jump forecast, and qualify the retained narrow
runtime corrections against trustworthy causal telemetry before promotion.
