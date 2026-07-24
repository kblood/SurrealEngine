# Bot AI quality execution plan

## Objective

Deliver mergeable Unreal Tournament 436 and Unreal Gold 226b bot support whose
navigation, survival, combat, and resource behavior is demonstrably competent
across representative and held-out maps. Deck16-II is the primary UT regression
case: bots must stop accumulating avoidable environmental/self deaths, stop
running against walls, recover from failed movement, and remain productively
mobile without turning movement into blind jumping or constant strafing.

Maximum competence comes first. Difficulty tiers and human-like mistakes are a
later calibration layer.

## Separate the two causes

Bot quality has two independently releasable layers:

1. **Stock runtime fidelity.** Botpack and Unreal scripts already use UE1
   reachspecs, pain-zone tests, latent movement, route caches, `HitWall`, jump
   adjustment, fall physics, movers, inventory desire, death, and respawn.
   Surreal implementations of those contracts must match the games closely
   enough that stock bots do not fail for engine reasons.
2. **Enhanced policy behavior.** `utility-arena` and `tactical-state` may add
   better subjective memory, risk-aware route choice, survival decisions,
   weapon safety, and recovery. They remain opt-in until an action adapter and
   comparative evidence exist.

Runtime fixes are tested with stock bots before candidate policies use them.
Policy gains are then measured against the repaired stock baseline.

## Failure taxonomy and required evidence

Every observed failure is classified before tuning:

| Failure | Evidence | Intended response |
| --- | --- | --- |
| Wall running / oscillation | destination, movement intent, displacement, wall contacts, normals, route node, latent state | steering correction, local avoidance, replan, escalating recovery |
| Pain-zone entry | foot/head zone, damage rate/type, route edge and alternatives | route danger cost; reject avoidable entry; bounded escape if already inside |
| Drowning / trapped water | water/head region, breath/damage, exit route progress | prefer viable exit and suppress nonessential combat until safe |
| Bad jump / lethal fall | reachspec/traversal type, launch and landing state, predicted floor/drop | jump only for verified traversal or tactical need; reject unsafe drops |
| Self-damage / suicide weapon | instigator, causer, weapon/mode, splash geometry, health, kill attribution | withhold unsafe shot or choose another weapon/aim point |
| Intentional risk | target/action utility, route alternatives, expected damage | allow only when the measured tactical value exceeds configured risk |
| Idle but valid | lift wait, ambush, aiming, cover, match state | do not label as stuck without movement intent |

Kills, deaths, suicides, environmental deaths, damage attribution, score,
movement intent, route progress, wall contact, zone exposure, and recovery
transitions must be measured directly. Position-only proxies remain available
for old artifacts but cannot certify a bot.

## Implementation program

### Phase 1: trustworthy baselines

- Extend telemetry and analysis with stable attributed combat/death and
  movement-failure events.
- Scan each test map for navigation nodes, reachspec traversal, pain/water
  zones, KillZ, movers, and relevant inventory.
- Run repaired-stock baselines before enabling enhanced control.
- Add a rendered unattended spectator lane for qualitative inspection beside
  the fixed-step headless lane.

### Phase 2: stock UE1 locomotion fidelity

- Audit path cost and pain-zone rejection, route-cache ordering, latent
  `MoveTo`/`MoveToward`, `HitWall`, jump adjustment, falling/landing, mover
  handling, and movement timeout semantics against exact-version scripts and
  legal runtime observations.
- Reproduce each defect with the smallest deterministic unit or synthetic map
  fixture before changing shared native behavior.
- Keep corrections common where UE1 contracts are common and gate genuine
  Unreal/UT differences through game profiles.

### Phase 3: shared navigation safety services

- Track progress only while a bot has movement intent.
- Add deterministic wall-contact and oscillation detection.
- Use an escalating recovery ladder: steer around the obstruction, back/side
  step, retry the route node, replan with temporary edge penalty, then choose a
  new goal. Jumping is not the default unstuck action.
- Annotate route choices with pain-zone, water-exit, fall/drop, mover, recent
  failure, and exposure costs.
- Preserve continuous productive movement, while allowing intentional stops
  for aiming, lift waits, ambushes, and other explicit actions.

### Phase 4: survival and combat safety

- Expose usable health, armor, ammunition, weapon mode, self-splash risk,
  nearby threat, reachable escape, and useful pickups through bounded
  subjective observations.
- Prevent projectile/splash fire when predicted self-damage is unacceptable,
  especially at walls and close targets.
- Prioritize leaving damaging zones and recovering viable movement over
  low-value attacks.
- Add weapon selection, aim, retreat, and resource behavior only after
  navigation safety gates pass.

### Phase 5: live candidates and tournament

- Add a narrow per-participant action adapter; never replace all bots through a
  global switch.
- Run stock, `utility-arena`, and `tactical-state` concurrently in role-swapped
  matches where supported. A hybrid may be added only when results identify a
  complementary strength.
- Use the same perception, path follower, hazard service, aim controller, and
  operation budgets for every enhanced policy.
- Iterate on the largest measured failure category, rerunning lower test tiers
  before each matrix.

### Phase 6: Unreal support

- Implement a distinct Unreal 226b deathmatch lifecycle: `UnrealShare.Bots`,
  `UnrealShare.UnrealSpectator`, 0-through-3 skill plus `ReSetSkill`, and its
  exact automatic-roster contract. Do not reuse UT's singular `Bot`,
  `CHSpectator`, 0-through-7 novice mapping, or `InitializeSkill` assumptions.
- Prove spawn, exact roster, skill, death, and respawn before policy control.
- Share only verified engine-level navigation and policy services.
- Keep other Unreal patches and modes fail-closed until independently probed.

## Test pyramid

1. Pure deterministic policy, risk, path-cost, progress, attribution, and
   recovery tests.
2. Synthetic engine fixtures: wall/corridor, safe route beside damage floor,
   lethal drop, jump edge, water exit, door, lift, pickup, splash wall, duel,
   death, and respawn.
3. Short owner-data smoke on one map for each supported game/profile.
4. Rendered spectator review for camera-visible path and combat defects.
5. Paired role-swapped map/seed matrices against repaired stock.
6. Held-out maps used only after tuning is frozen.

Initial owner-data integration maps:

- UT436 tuning: `DM-Morbias][`, `DM-Deck16][`/Deck16-II,
  `DM-Pressure`, `DM-Fractal`, and `DM-Morpheus` or `DM-Phobos`.
- UT436 held-out: at least two additional deathmatch maps chosen after feature
  scanning.
- Unreal 226b tuning after adapter qualification: `DmMorbias`, `DmDeck16`, and
  `DmHealPod` or another verified deathmatch map.
- Unreal held-out: at least one additional verified deathmatch map.

## Qualification and release gates

A candidate cannot be called good or merge-ready unless all applicable gates
pass:

### Correctness and safety

- No crashes, hangs, invalid targets, non-finite state, unbounded traces, or
  uncontrolled roster changes.
- Deterministic fixture outputs and structurally valid bounded telemetry.
- Synthetic safe-route, damage-floor escape, wall recovery, jump/fall, and
  splash-safety fixtures pass 100%.
- No permanent movement-intent stuck episode; 95% of recoverable episodes
  clear within two seconds and all clear or select a new reachable goal within
  five seconds.

### Match competence

- Across the Deck16-II qualification matrix, avoidable suicides are lower than
  kills and no worse than repaired stock in every role-swapped aggregate.
- Candidate suicide rate and movement-intent stuck time are each at least 50%
  lower than the pre-fix Surreal baseline, unless repaired stock already meets
  the absolute fixture gates.
- Candidate score margin, damage efficiency, survival, useful map coverage,
  and resource acquisition are non-inferior to repaired stock across the full
  tuning matrix; an aggregate gain may not conceal a map safety regression.
- Held-out maps complete without a new severe failure category or more than a
  10% regression in suicide rate, stuck time, or score margin versus repaired
  stock.

### Cross-game and performance

- UT436 and Unreal 226b lifecycle/roster/skill tests pass independently.
- Common runtime changes pass both game matrices; game-specific adapters do not
  leak class or skill assumptions.
- AI work is bounded and reported. The initial target is p95 under 2 ms per
  frame for 16 bots on the qualification machine, with no single-bot unbounded
  actor or navigation scan.

Numeric gates are versioned in quality metadata. They may be tightened after
the first trustworthy baseline, but they cannot be relaxed merely to pass a
candidate.

## Initial pre-fix baseline

The pushed `ed93e9c2` binary was run for three seeds each on UT436
`DM-Deck16][` and `DM-Morbias][`, with four external-skill-7 stock Botpack bots
for 1,800 fixed 60 Hz ticks (30 simulated seconds). All six runs completed and
passed structural validation. The position-only v1 analyzer reported:

| Map | Mean active movement fraction | Mean aggregate no-progress proxy | Mean stuck proxies | Worst single no-progress span |
| --- | ---: | ---: | ---: | ---: |
| `DM-Deck16][` | 0.67 | 36.33 s | 4.33 | 20.57 s |
| `DM-Morbias][` | 0.79 | 22.46 s | 1.67 | 26.78 s |

One Morbias bot remained in the `Roaming` state while almost stationary for
about 27 seconds. This supports treating locomotion as a runtime correctness
problem rather than only a policy-selection problem. The baseline cannot yet
count kills, deaths, suicides, wall contacts, movement intent, or zone exposure;
it must not be used to infer those outcomes. Artifacts are retained under the
central QA tree at `SurrealEngine/qa/runs/2026-07-24/ed93e9c2/`.

The preserved `bot-ai-parity` fork binary was run over the same six cases. Its
richer trace reports mean no-progress proxies of about 6.97 seconds on Deck16
and 5.56 seconds on Morbias, with 1.33 and 0.67 stuck proxies respectively.
This is strong evidence that its arrival-envelope, wall, ledge, and pathing work
contains useful locomotion corrections. It is not a complete bot solution:
across the three Deck16 runs it recorded 15 deaths but only 7 attributed kills,
including 7 environmental deaths and one self-fatal death, for aggregate score
−1. Candidate patches from that fork must therefore be reconstructed and
qualified separately; motion improvement cannot waive hazard-survival gates.
The comparison artifacts are under
`SurrealEngine/qa/runs/2026-07-24/bot-ai-parity-fork/`.

## Iteration 1: attributed telemetry and arrival correction

Telemetry v2, the Unreal Gold 226b deathmatch adapter, the deterministic local
movement-safety advisor, and the elapsed-step/target-aware `TickMoveTo` arrival
correction were integrated and rebuilt together. Seven focused native tests and
all 19 analyzer/matrix-runner tests passed. Unreal 226b completed controlled
combat, death, and respawn runs on `DmMorbias` and `DmDeck16`; other Unreal
versions and modes remain fail-closed.

The rebuilt UT candidate then ran the same three-seed, four-bot, 30-second
Deck16-II/Morbias matrix. All six cases completed with telemetry v2:

| Map | Pre-fix no-progress total | Candidate no-progress total | Pre-fix stuck events | Candidate stuck events | Exact kills | Exact suicides | Environmental deaths |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `DM-Deck16][` | 108.98 s | 115.48 s | 13 | 7 | 1 | 10 | 10 |
| `DM-Morbias][` | 67.37 s | 30.75 s | 5 | 5 | 16 | 3 | 0 |

The arrival correction is a useful but insufficient locomotion fix: it reduced
Deck stuck-event count and sharply improved Morbias no-progress, but it did not
improve aggregate Deck no-progress and cannot pass the survival gate. Every
Deck environmental death occurred while hazard-exposed. Two Deck bots also
spent most of a run at the same location in `Roaming`; one received 14,277
`HitWall` callbacks (up to ten in one fixed tick). Iteration 2 therefore targets
route `SpecialCost`/`PainPath` correctness and the separate repeated-wall
failure before any live enhanced policy is enabled. Candidate artifacts are at
`SurrealEngine/qa/runs/2026-07-24/arrival-v2-deck16-morbias/`.

## Iterations 2 and 3: route cost, collision amplification, and tactical offset

Iteration 2 made route search honor nonnegative, saturating
`NavigationPoint.cost`, including script `SpecialCost`. The synthetic short
PainPath route costs 1,000,100 versus 240 for its longer safe alternative, and
the safe route wins deterministically. The live Deck matrix was bit-for-bit
unchanged. An owner-map export explains why: `DM-Deck16][` contains 251
navigation actors but no `PainPath` and no navigation actor inside any of its
three slime zones. Route-cost parity is useful shared correctness, but it is
not the Deck hazard fix.

All 12 observed Deck slime entries crossed into a hazard with negative vertical
velocity; seven were directed `Roaming` moves, three were already in
`FallingState`, one was `wandering`, and one was `TacticalMove`. Eleven
completed exposures ended in a hazard-exposed death and none escaped alive.
The next direct hazard fixture is therefore a supported ledge beside a pain
zone, exercising `bAvoidLedges`, `bStopAtLedges`, and `MayFall` before a walking
step commits to falling.

Separately, the repeated Deck wall count was proven to be real physics
amplification: five zero-time walking iterations each emitted a forward and
slide `HitWall`, producing exactly ten callbacks per fixed tick. A zero-progress
guard now stops after the first unchanged forward/slide attempt. The two short
Deck reproductions reduced the maximum from ten to two callbacks per tick,
while intentionally leaving the cross-tick route/recovery failure visible.

Restoring UT436's advanced `MoveToward`/`AlterDestination` callback improved
aggregate Morbias combat from 16 kills, 3 suicides, and score +13 to 18 kills,
0 suicides, and score +18. It also exposed an unsafe fixed-side tactical offset:
seed 104729 drove one bot into a two-wall corner for 26.02 seconds. This slice
is not independently releasable. The preferred 100-unit lateral candidate must
be collision-checked, with a clear opposite side and then the unaltered target
as bounded fallbacks. Stock `PickWallAdjust` also used an ineffective one-unit
lateral probe/destination; its reconstruction now uses one pawn collision
diameter and must pass the full matrix with the tactical candidate guard.

## Iterations 4 through 10: ledge prediction, recovery, and rejected jump variants

The integrated arrival, route-cost, walking-loop guard, guarded
`AlterDestination`, wall-adjustment, and `MayFall` stack reduced Morbias
movement-intent no-progress from 21.37 seconds to 8.17 seconds and eliminated
its stuck proxies. On Deck16-II it reduced intent no-progress from 73.37 to
57.63 seconds and raw `HitWall` callbacks from 31,347 to 6,431, but all 12
slime entries still ended in hazard-exposed deaths. `MayFall` alone was not a
hazard solution because stock Botpack states frequently leave `bCanJump` true.

A bounded unsupported-step predictor now performs cylinder sweeps and adaptive
foot-region samples for at most 24 fixed steps. It applies only to autonomous
`bIsPlayer` pawns moving from walking to falling, after the script `MayFall`
hook. Humans, ordinary `ScriptedPawn` actors, bots already in pain, immune
pawns, unusual gravity, and already-airborne jumps remain unchanged. A positive
veto uses short per-pawn forbidden-direction memory and collision-, support-,
and pain-checked reverse/side steering instead of repeatedly selecting the same
ledge.

The fixed Deck/Morbias matrix established the following candidate outcomes:

| Candidate | Deck hazard entries | Deck environmental deaths | Deck intent no-progress | Morbias intent no-progress | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| Integrated stack before prediction | 12 | 10 | 57.63 s | 8.17 s | survival failure |
| Simple fall prediction, stock jump | 4 | 3 | 121.62 s | 8.17 s | safe but stalls |
| Exact ballistic jump replacement | 4 | 2 | 57.38 s | 26.27 s | Morbias regression |
| Progress-floor jump replacement | 0 | 0 | 121.64 s | 19.63 s | safety by stalling |
| Conditional descending jump | 5 | 4 | 102.27 s | 13.78 s | cross-map regression |
| Conditional jump plus ledge recovery | 5 | 3 | 25.20 s | 13.78 s | promising Deck, Morbias regression |
| Stock jump plus ledge recovery | 5 | 4 | 70.23 s | 8.17 s | current conservative base |

The jump replacements remain rejected experiments. Public `Bot.uc` evidence
supports using `Destination` and respecting script-selected vertical velocity,
but no public UE1 native implementation was found, and every tested replacement
regressed Morbias movement or suicides. The worktree therefore retains stock
`EAdjustJump` behavior while wall and hazard changes are qualified separately.

Recovery telemetry records exact veto, repeated-veto, recovery-attempt, and
radius-escape counters. In the current conservative matrix, Deck produced 71
vetoes, 66 recovery attempts, and 51 measured escapes, while Morbias produced
none and stayed bit-for-bit equal to the pre-recovery repaired baseline. Five
remaining Deck hazard entries comprise two walking falls that hit a wall before
sliding into slime, two combat-knockback falls, and one explicit stock jump.
The largest remaining movement defect is independent of pain handling: two
bots across different seeds spent 50.72 seconds at the exact coordinate near
`(654.7, 1007.1, -788)` and generated 6,078 wall callbacks with zero pain-veto
activity. Bounded cross-tick wall recovery is the next release blocker.

Artifacts are retained in the central QA tree under the dated
`iteration4-safety-v2-deck16-morbias` through
`iteration10-recovery-stock-jump-v2` directories. Stale-binary iterations are
not evidence and are explicitly discarded.

## Frozen tuning and held-out maps

Installed owner-data packages were verified before expanding the matrix. Exact
URLs omit the package extension:

| Game | Tuning maps | Held-out maps |
| --- | --- | --- |
| UT436 | `DM-Pressure?Game=Botpack.DeathMatchPlus`; `DM-Morpheus?Game=Botpack.DeathMatchPlus` | `DM-Fractal?Game=Botpack.DeathMatchPlus`; `DM-Phobos?Game=Botpack.DeathMatchPlus` |
| Unreal Gold 226b | `DmHealPod?Game=UnrealShare.DeathMatchGame`; `DmDeathFan?Game=UnrealShare.DeathMatchGame` | `DmElsinore?Game=UnrealShare.DeathMatchGame`; `DmRadikus?Game=UnrealShare.DeathMatchGame` |

The first tuning smoke completed on all four maps. UT Pressure had no hazard
entry, UT Morpheus exposed two, Unreal HealPod had none, and Unreal DeathFan
exposed one slime/environmental death. These are tuning observations only;
held-out results remain unopened until parameters freeze.

## Iteration and parallel ownership

Each iteration has four lanes:

1. research exact engine/script contracts and relevant open-source bot designs;
2. implement one measured runtime or policy slice;
3. extend fixtures, telemetry, analyzer, and matrices independently; and
4. integration review: inspect diffs, run the pyramid, compare evidence, and
   decide reject, revise, release-candidate, or merge-ready.

Agents work in disjoint files where possible. Shared engine and benchmark
changes are integrated only after review. Commercial game assets and scripts
remain local test inputs; committed artifacts contain code, schemas, synthetic
fixtures, summaries, and provenance only.

## Merge strategy

The experimental branch may contain the full comparison system. Main-repo
merges remain focused:

- one reproduced stock runtime correction plus its test;
- one game-profile lifecycle adapter plus owner-data evidence;
- one telemetry/schema slice plus analyzer compatibility;
- one opt-in enhanced service or policy adapter plus qualification evidence.

Ordinary gameplay remains unchanged until a focused change passes its gates.
Enhanced AI stays explicitly selectable until its full matrix is release-ready.
