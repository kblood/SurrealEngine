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

The gate is now executable rather than only prose. `Evaluate-BotQualityGate.py`
checks required run counts, structural completion, required metrics, aggregate
thresholds, and per-run/map thresholds, and exits nonzero on a violation. It
fails closed when a selector matches nothing or a metric is absent or null; it
does not rename a proxy or synthesize an unavailable causal metric. The current
death counters still have a truth gap: a null/non-player `Killed` argument does
not distinguish an unassisted environmental death from a recent enemy
knockback or damage contribution. A pure five-way attribution model and a
nested-call coordinator now cover canonical `TakeDamage`, `AddVelocity`, known
environmental sources, `Killed`, abnormal unwinding, and per-life reset. They
are tested infrastructure, not live telemetry evidence, until the coordinator
is wired to verified UT436 and Unreal 226b script contracts and reconciled with
the final summary.

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

## Iterations 11 through 18: wall and stall recovery evidence

Cross-tick wall recovery was tested as a sequence of deliberately small
candidates. Destination-only success reporting was rejected because it could
claim progress without moving. Latent steering was starved by UE1 scheduling:
`UPawn::Tick` polls the latent action before the state VM can reassert the
escape. Synchronous callback steering reduced Deck wall callbacks but created
a repeatable steer/jump/return loop and worsened deaths. The retained
timeout-only wall safeguard is narrower: repeated contact within four units
times out the move and requests a normal script replan. In the fixed matrix it
cut Deck intent no-progress from 70.23 to 34.59 seconds and wall-adjust calls
from 7,043 to 1,047, but did not reduce the four environmental deaths. It is a
bounded safeguard, not a complete quality result.

A once-per-pawn-tick move-stall watchdog then localized a separate Deck seed
314159 failure: Cilia remained at one coordinate while repeatedly executing
`MoveToward LiftExit3`. The shadow observer was proven behavior-neutral by an
enabled/disabled same-layout A/B. A normal latent timeout was safe but
ineffective: the script selected the identical target eight times and the bot
remained stationary for 15.15 seconds. That live timeout candidate is rejected
until replanning retains bounded memory of the failed navigation segment.

| Iteration | Candidate | Evidence | Decision |
| --- | --- | --- | --- |
| 11 | destination-only wall completion | false success without displacement | reject |
| 12 | latent wall steering | scheduling starved the callback | reject |
| 14 | synchronous wall steering | fewer walls, worse deaths, repeat loop | reject |
| 15 | timeout-only wall replan | large wall/stall reduction; no survival gain | retain as bounded safeguard |
| 16 | shadow move-stall watchdog | one exact persistent `LiftExit3` episode; observer A/B neutral | retain telemetry |
| 17 | nav timeout on detection | eight same-target replans, zero movement | reject live action |
| 18 | empty-path filter plus failed-node memory | movement improved, but Deck suicides rose from 4 to 7 and the two-strike penalty was not exercised | reject combined candidate |

Iteration 18 was separated with a same-layout isolation build. Restoring the
old inventory scoring reproduced iteration 17 byte-for-byte on Deck, including
the eight failed LiftExit replans. Therefore the apparent movement improvement
and suicide regression came from rejecting empty inventory paths, while the
failed-node memory was being cleared by a nominal arrival before its second
strike. The next experiment must preserve a first strike until measured escape
and emit exact activation/application counters; no result may be attributed to
the penalty until those counters prove it ran.

## Iterations 19 through 22: durable failed-node memory and wall-slide prediction

Progress-aware failure memory was instrumented with exact activation,
safeguard-suppression, and route-penalty counters. Two-strike and immediate
two-entry variants moved Cilia away from `LiftExit3`, but cleared avoidance
after a 96-unit excursion and let her return to the same anchor in under two
seconds. Iteration 21 retained an activated entry for the full ten-second
window. That eliminated the permanent stall and reduced the seed-314159
longest intent stall from 9.37 to 1.78 seconds, but selected a riskier route:
the six-case matrix lost one kill, added one suicide/environmental death, and
applied the route penalty 12,441 times in that case. It remains an experiment,
not a release candidate.

Iteration 22 extended the bounded fall predictor through at most two
near-vertical wall collisions. It recognized both known Deck16-II walking
falls: Sarena seed 271828 was redirected and survived, while Necroth seed
104729 was vetoed but fell on the next tick and still died. The six cases and
their repeats were byte-identical, but Deck kills fell from six to five,
hazard-exposed deaths rose from five to six, longest intent stall rose from
1.78 to 6.58 seconds, and suicides remained tied with kills. The live extension
is rejected in that state. Attribution showed that rollback used the current
walking sub-iteration, which can already be unsupported; the next isolated
candidate must restore the tick's last supported start before changing the
prediction model again.

Iterations 23 through 26 isolated enforcement. Whole-tick rollback removed the
new 6.58-second stall but did not move Necroth back onto support. Post-rollback
support verification, a selected 64-unit retreat, exact walking-floor checks,
and finally four real collision-checked reverse steps of eight units all left
the reproduced tick-471 through tick-475 trajectory unchanged. The final two
candidates also exposed a 7.88-second targetless `MoveTo` stall. Because none
reduced deaths and the last candidate still produced K/D/S/E of 1/4/3/3 for
seed 104729, the wall-slide continuation and synchronous enforcement sequence
is rejected and removed from live runtime. The useful result is diagnostic:
the predictor can recognize the fall, but changing ledge physics is not yet a
safe way to enforce it.

Iteration 27 then exposed a watchdog attribution requirement. Resetting the
episode on every latent native call suppressed all Cilia detections because
Botpack reissues the same `MoveToward LiftExit3` command; the 9.37-second stall
returned. The watchdog now uses a stable command key: reissues for the same
actor target (or the same positional destination) preserve elapsed no-progress,
while a genuinely different target, destination, or latent mode starts a new
episode. Iteration 28 reproduced iteration 21 exactly, including two correctly
attributed activations and a 1.78-second maximum intent stall. A separately
typed targetless `MoveTo`/`StrafeTo` timeout cannot write failed-navigation
memory, although the clean fixed cases have not yet exercised that branch.
The remaining problem is the ten-second endpoint penalty: it removes the stall
but applies 12,441 times and retains the iteration-21 combat/survival regression.

Iteration 29 moved the failed-node cost out of internal reverse-graph relaxation
and onto unique final first-hop candidates, and reduced the hard lifetime from
ten to four seconds. Cilia's gameplay trajectory remained exact, while counted
applications fell from 12,441 to 4,261. The remaining count represents repeated
route searches during the active window rather than distorted internal graph
distances. Quality is still the iteration-21 result, so the live recovery
remains experimental.

Iterations 30 and 31 tested a higher-level hypothesis for Necroth's walk-off.
Telemetry correlated the failure with an already-selected direct
`MoveToward BulletBox4`, and bounded inventory-corridor variants changed the
target to `PathNode122` and eliminated three seed-104 deaths. They did not prove
that a fresh native `ActorReachable` approval at the veto was the causal
interception point. Iteration 38 tracing later showed that the inventory target
was already live before the recent ledge veto; a post-veto reachability check
could not cancel that in-flight latent move. The corridor variants also cut
kills from two to zero, score from zero to -1, and raised wall contacts from 291
to 568. Live integration is removed; the pure bounded corridor helper remains
research infrastructure, and the earlier direct-inventory causal claim is
withdrawn.

Iterations 32 and 33 proved another native-contract defect: `PickWallAdjust`
forced a jump even though Botpack Wandering had set `bCanJump=false`. The guard
kept Visse walking at the exact tick-55 oracle and removed her first slime
entry, but the isolated match regressed from K3/S2/score +1 to K2/S3/score -1
and wall-adjust calls changed substantially. The live guard is removed while
its pure contract test is retained. Correct local semantics alone are not
accepted when the match-level quality signal worsens.

The same iteration-22 binary was also run unattended on Unreal Gold 226b
`DmDeathFan`. All 1,802 shared gameplay records exactly matched the iteration-10
baseline: Dante still fell, entered pain 2.47 seconds later, and died. The
current 1.5-second predictor horizon therefore cannot see that hazard, and the
wall-slide extension did not affect Unreal. A longer horizon must be separately
bounded and qualified after rollback is proven on Deck.

| Iteration | Candidate | Evidence | Decision |
| --- | --- | --- | --- |
| 19 | progress-aware two-strike memory | penalty exercised, but escape cleared too early | revise |
| 20 | two remembered targets, immediate activation | moved 96 units, then returned to the same anchor | revise |
| 21 | durable ten-second avoidance | permanent stall removed; combat/survival regression and 12,441 applications | experimental only |
| 22 | two-collision wall-slide prediction | one walk-off saved, one veto failed; aggregate quality regressed | reject live behavior |
| 23-26 | supported rollback and bounded synchronous retreats | known fall remained trajectory-identical; targetless stall reached 7.88 s | reject and remove |
| 27 | reset watchdog on every latent call | Botpack reissues same target; detections fell to zero and 9.37 s stall returned | reject |
| 28 | stable command-key watchdog | correct two detections; exact iteration-21 escape and quality result | retain correctness, revise penalty |
| 29 | final-endpoint, four-second failed-node cost | exact escape; applications 12,441→4,261; quality regression remains | experimental |
| 30-31 | hazardous direct-inventory corridor | Necroth saved; K2→0 and walls 291→568 | remove live, retain helper |
| 32-33 | honor `bCanJump` in wall-jump branch | invalid jump prevented; K3→2, S2→3, score +1→-1 | remove live, retain helper |

### Failed-navigation policy safety boundary

The normal latent timeout remains the retained, script-owned recovery: it
sets `MoveTimer` negative and lets the existing bot state choose its next
route. The failed-navigation route penalty is different: the fixed Deck
evidence reduced the `LiftExit3` stall but caused broad endpoint-cost
applications and match-level regressions. It is therefore benchmark-opt-in and
default-off. The observer still records failed segments, activations, and
penalty applications; only an explicitly identified comparison run may apply
the cost. Manifest, summary, telemetry identity, matrix variant identity, and
quality comparison all carry `failed_navigation_avoidance_enabled`, preventing
an enabled experiment from being compared as a stock-equivalent run.

## Iterations 34 through 41: lift topology, determinism, and rejected wall-jump guards

Iterations 34 through 36 narrowed failed-navigation avoidance from actor
identity to a physical lift landing. Avoiding only another exit with the same
lift identity did not change Cilia's seed-314159 result: two activations still
caused 4,261 endpoint penalties. Temporarily penalizing every lift exit proved
that avoiding the alternate exit removed the permanent loop: one activation
and 85 applications moved Cilia to `PathNode116`. The retained narrow model
groups exits only when they share a non-lift landing node, zone, bounded XY
radius, and bounded height. Iteration 36 reproduced the first iteration-35
six-case result exactly while avoiding the global all-lifts rule.

The first and repeated iteration-35 matrices initially disagreed on UT Morbias
seed 104729 (K6/walls65/longest-intent-stall0.27 s versus
K7/walls100/2.83 s). The root cause was not bot randomness: overlap hits were
sorted by process pointer, so ASLR changed `Touch` callback order. Sorting world
first and actors by stable level index with name fallback removed that pointer
nondeterminism. This engine-level correction is required for trustworthy fixed
seeds; a same-seed repeat remains a reproducibility check, not an independent
quality sample.

| Iteration | Candidate and result | Decision |
| --- | --- | --- |
| 34 | same-lift-identity avoidance; seed 314159 unchanged at two activations/4,261 applications | revise topology |
| 35 | all-lift-exit avoidance; Cilia escaped with one activation/85 applications; six-case K22/D27/S5/E4/score +17 | behavioral proof only; rule too broad |
| 36 | shared-landing topology; exact iteration-35 first-matrix result after stable overlap ordering | retain narrow correctness, not a quality release |
| 37 | prospective wall-jump forecast used the pre-script velocity rather than the adjusted jump velocity; seed 314159 stayed K2/D3/S1/E1/score +1 | reject invalid experiment |
| 38 | recent-veto/VM handshake first spun and stopped at tick 358; corrected run regressed to K0/D3/S3/E3/score -3, walls 395, intent no-progress 13.13 s (longest 6.00 s) | reject; exact rollback |
| 39-40 | correct jump input plus first/same-wall diagnostics; admissible central-build run stayed byte-identical at K2/D3/S1/E1/score +1 | neutral; do not promote |
| 41 | bounded sustained same-BSP forecast changed motion, but hazard entries and hazard-exposed deaths rose 1→2; intent no-progress fell 10.62→4.45 s while K/D/S/E and score stayed 2/3/1/1 and +1 | reject safety regression; roll live forecast back |

Iteration 38's rollback events have the same SHA-256 as the iteration-35
seed-104729 events (`9fd10f192866...`), proving restoration rather than a
similar aggregate. Iteration 39 showed why the one-contact predictor was
neutral: after the first aligned slide, its reconstructed velocity retained a
small component into the same wall, and the next step failed open because the
single continuation was already consumed. Iteration 40 added same-surface and
actual-blocking diagnostics, but all clean seed-314159 outputs remained
byte-identical (`e4f17d562ea2...`). Diagnostic runs made from a repository-local
build directory violate the workspace output policy and are excluded from
qualification; only `iteration40-compliant-final-clean-deck314159-micro`, built
from the central output tree, is admissible, and it is neutral. Iteration 41
finally changed the trajectory but worsened the safety signal, so live
wall-jump forecasting must be removed while its pure fixtures remain.

The stable-overlap build also passed repeated Unreal Gold evidence. The fixed
four-case `DmDeck16`/`DmMorbias` regression matrix reproduced exactly at
aggregate K3/D4/S1/E0/score +2. The repeated tuning smoke also reproduced:
`DmHealPod` was K0/D0/S0/E0 with two wall callbacks, while `DmDeathFan` was
K1/D2/S1/E1 with 1,609 wall callbacks and a 2.27-second longest intent stall.
This confirms cross-game determinism and preserves the known DeathFan failure;
it is not a claim of Unreal bot competence.

The rendered bot-only lane is operational: a two-second UT436 `DM-Morbias][`
Vulkan run launched four stock bots at skills 7/5/3/1, logged in as
`Botpack.CHSpectator`, followed `TMale1Bot0`, owned no gameplay pawn, and exited
at the requested duration. The manifest-backed run proves unattended renderer,
camera, roster, and shutdown lifecycle. Because it ran minimized and retained
no subjective capture review, it does not prove visual or behavioral quality.

## Iteration 42: falling seam release rejected on causal safety evidence

Unreal `DmDeathFan` exposed a separate falling-physics defect. Dante and Ash
could enter a two-plane static-world seam in `PHYS_Falling`, make effectively
zero progress, and receive one `HitWall` callback per tick. The walking-only
stall watchdog cannot act in that state. A pure resolver reconstructed the
two-plane crease and iteration 42 tried exactly one additional crease sweep
after the existing aligned sweep. It was bot-only, bounded, deterministic, and
removed the long wall episode, but a geometrically valid vertical crease was
not a survivable route.

The first comparison was invalid because the candidate used
`0.016666667` while the established Unreal matrix invoked `0.0166667`. At the
historical timestep, two exact candidate repeats changed `DmDeathFan` seed
424242 from K1/D2, one unassisted environmental death, 1,609 wall callbacks,
and one 2.27-second intent-stuck episode to K1/D4, three unassisted
environmental deaths, 371 wall callbacks, and no intent-stuck episode. Ash and
Dante were released 1,171 and 928 units downward into the pain pool while
their requested destinations were hundreds of units above them. The candidate
therefore traded a locomotion failure for two additional avoidable deaths and
is rejected. Its repeated event files are byte-identical; this is a stable
regression, not noise.

The live crease sweep was removed. The rollback restored K1/D2 and the exact
historical shadow-decision SHA-256
`1542313e58c908f658d2c69faa7fc95ad0837137e322cd1a0d007fce5d05d8ef`.
Pure two-plane, falling-damage, target-progress, and supported-escape fixtures
remain research inputs. Future recovery must treat `Unknown` as no additional
movement, require positive target progress and proven walkable non-pain
support for any translation, and test both timestep spellings explicitly.

Iteration 43 then tested the lowest-risk research recommendation: perform no
extra sweep, expire only the active movement latent request, and project
acceleration out of the two inward wall half-spaces. It was byte-neutral on the
historical DeathFan case. At the nearby timestep it reduced DeathFan wall
callbacks from 182 to 32 without adding a death, but UT Deck seed 271828
regressed from K1/D2/score 0, one hazard entry/death, and longest intent stall
1.22 seconds to K1/D3/score -1, two hazard entries/deaths, and longest intent
stall 2.32 seconds. The two candidate hazard deaths had recent enemy momentum,
so they were not misreported as unassisted suicides; they still fail total
survival, hazard, combat, and locomotion non-regression gates. The live replan
is rejected and removed. Its pure projection fixtures remain available.

## Iteration 44: supported horizontal escape retained in shadow only

Iteration 44 observes the same bounded two-plane falling seam without changing
movement. It constructs a 24-unit outward horizontal bisector, dry-runs the
pawn sweep, and accepts support evidence only when the first downward
full-extent hit is static world, walkable, in a known non-pain zone, and the
hypothetical endpoint advances toward the active tactical destination. Unknown
evidence, dynamic support, pain support, and non-positive destination progress
remain rejections. The observer changes only attributed counters; it does not
move the pawn, alter acceleration or latent state, or issue callbacks.

The observer was exactly behavior-neutral against its rollback on Unreal
`DmDeathFan` seed 424242 at both timestep spellings and on UT Deck seed 271828.
After removing only the five new counter fields, gameplay telemetry and shadow
records matched byte-for-byte. At historical `0.0166667`, DeathFan produced
eight detections and eight candidate probes, all classified unknown or unsafe
support. At `0.016666667`, it produced 141 detections and 141 probes, again all
unknown or unsafe. Deck seed 271828 produced three probes: two destination-
progress rejections and one unknown-or-unsafe-support result. No candidate was
authorized in any of these comparisons.

The tuning scan remained diagnostic rather than a promotion gate. Deck seeds
104729 and 314159 registered one and two unknown-or-unsafe probes respectively;
UT Morpheus registered three seam detections but only one valid probe, also
unknown or unsafe. The other listed tuning cases authorized no escape, and the
entire scan recorded zero authorized candidates. The shadow instrumentation is
retained because it safely disproves candidate availability on the known
failures. No live horizontal translation is enabled, and held-out maps remain
unopened.

## Iteration 45: individual seam directions also fail safe authorization

Iteration 45 expands only the behavior-neutral observer. For each eligible
two-plane falling seam it probes, in deterministic order, the normalized
outward bisector and then the two individual horizontal wall normals. Duplicate
directions collapse, a near-opposed pair omits the unstable bisector, and at
most three bounded 24-unit dry-run sweeps are classified. The observer stops
after the first authorization but never applies a translation or changes
movement state. Analyzer version 12 requires no more than three probes per
detection, no more than one authorization per detection, and an exact
partition of every probe into authorized, target-progress-rejected, or
unknown/unsafe support.

The candidate executable SHA-256 was
`0ECF15DEF64692D55E4A8815B7E8A0682E7C8E5043E66E070D8CA3DC399643CB`.
Two candidate repeats for each of historical DeathFan, nearby-timestep
DeathFan, and Deck seed 271828 produced exact repeated shadow streams. After
removing only the five seam observer counters, their gameplay events and
summaries exactly matched the accepted iteration-44 baseline. Historical
DeathFan produced 8 detections and 24 unsafe probes; nearby-timestep DeathFan
produced 141 detections and 423 unsafe probes. Deck produced 3 detections and 9
probes, split into 8 target-progress rejections and 1 unsafe-support result.
All three scenarios authorized zero candidates.

The additional tuning scan remained behavior-neutral on Deck seeds 104729 and
314159 and Morpheus seed 424242. Those cases respectively produced 3, 6, and 3
probes, with zero authorizations. The individual normals therefore broaden the
diagnostic evidence but do not establish a safe live recovery on any known
fixture. No seam movement is promoted. The next instrumentation slice must
debounce repeated ticks into physical seam episodes and partition rejection
causes more precisely before another live intervention is considered.

## Iteration 46: measurement and collision-preflight foundations

Two behavior-neutral foundations were accepted before attempting the walking
preflight. First, the benchmark now accumulates every pawn-native exact counter
at the start of the victim's outermost `Killed` call. A death after the last
tick sample, repeated flush, subsequent same-pawn capture, death before first
sample, and replacement pawn sharing the roster identity are covered without
double counting. Death flushing never clears life attribution, and `Killed`
depth is tracked per victim so synchronously nested deaths of different bots
are both recorded and classified.

Second, `TryMove` now shares its blocking-hit selection with an explicit-origin
read-only probe. The current-location path retains one ordered collision trace
for blocking, touch, and encroachment processing; it does not retrace or mutate
the actor during a probe. Pure fixtures preserve the chosen hit fraction,
normal, and stable fake actor identity across world, pawn, mover/other, player-
blocking, and base-exclusion cases. The new API is infrastructure only: it does
not yet simulate a walking step or alter bot behavior.

The combined Release candidate SHA-256 was
`E3DC775E4CA509817D71BD623CF565E5CC18531C1B09F9E7FEB45434D84257A3`.
Two repeats on historical DeathFan, nearby-timestep DeathFan, and Deck seed
271828 produced exact repeated shadow streams. After removing only the native
diagnostic counters whose polling implementation changed, gameplay, causal
death attribution, and summaries exactly matched iteration 45. The diagnostic
counters were also unchanged on these runs; focused fixtures provide the
positive death-between-samples case that the maps did not happen to trigger.

## Iteration 47: physical seam episodes and exact rejection causes

Iteration 47 retains the raw seam rollups but adds a per-life episode identity
from the unordered wall-normal pair and an 8-unit horizontal anchor. Vertical
motion and elapsed gaps do not split an episode while the pawn remains falling
in a known safe foot region. Detailed per-candidate outcomes distinguish
blocked sweep, missing static walkable support, pain support, no active
movement intent/target, known target regression, and unknown evidence. Invalid
geometry remains per detection; only episode count and the first authorization
within an episode are debounced.

The Release candidate SHA-256 was
`F7057226AF9C2CFF6362325E01A722563AF3F506F14BB98BC694BE8E8C01BB0C`.
Two repeats for historical DeathFan, nearby-timestep DeathFan, and Deck seed
271828 preserved gameplay and summaries exactly after removing only the new
detailed fields, and repeated shadow streams remained exact. Historical
DeathFan's 8 detections are 2 physical episodes (one each for Dante and Ash),
with all 24 candidates lacking static walkable support. The nearby-timestep
141-detection Ash jam is 1 episode with 423 missing-support candidates. Deck's
3 detections are 2 episodes: 2 candidates had no active movement target, 6
were true target regressions, and 1 lacked support.

The tuning scan found Deck seeds 104729 and 314159 to contain 1 and 2 episodes;
their former target-rejection rollups were Sleep/no-target cases, not true
regressions. Morpheus's 3 detections are 3 episodes, including 2 invalid-
geometry detections; its 3 valid candidates split into 1 blocked sweep and 2
missing-support results. No known case produced an authorizable episode. This
strengthens the decision to leave falling-seam translation disabled and move
the next intervention to pre-commit walking support.

## Next behavior slice: pre-commit unsupported-step hazard shadow

The next Deck intervention moves earlier in the walking transaction. Current
walking physics applies step-up, forward/slide, and step-down movement before
the final support check. The existing fall predictor also stops at its first
collision. In the accepted Deck seed 104729 trace, Necroth leaves support at
tick 471 while moving toward `BulletBox4`, first hits the wall at tick 481, and
enters pain at tick 503. The rejected post-move rollback iterations therefore
began after the recoverable supported position had already been lost.

Implementation is split into independently qualified stages:

1. Extract the read-only blocking-hit selection behind `TryMove` into an
   explicit-origin probe. At the pawn's current origin it must select exactly
   the same fraction, normal, and actor as the existing dry run. It must not
   mutate `Location`, callbacks, latent state, or RNG.
2. From a known supported walking origin, shadow-simulate the bounded step-up,
   forward/one-slide, and step-down endpoint. Only an unsupported endpoint may
   enter the existing bounded fall forecast, extended across at most two
   near-vertical static-BSP contacts. Movers, dynamic actors, unusual gravity,
   unknown or water/pain origin zones, jumps, humans, and non-stock pawns fail
   open with an explicit rejection reason.
3. Debounce observations by pawn life, supported origin, and semantic movement
   target. Prove the observer gameplay-neutral on the implicated Deck trace and
   on the UT/Unreal tuning matrix.
4. A live candidate may cancel at most one authorized step per episode while
   the pawn is still on proven support, expire the latent move, and arm the
   existing pain-ledge recovery. Promotion requires removing the implicated
   hazard/death, productive movement or a semantic replan within one second,
   and no survival, hazard, combat, wall, stall, or determinism regression.

This is shared UE1 locomotion timing, not a UT-only tactical policy. Stock UT
and Unreal bots both rely on native `MayFall`/`HitWall` timing, although the
exact DeathFan failure is already falling and remains a separate seam case.
After this slice, `MinHitWall` dispatch parity, native `bAvoidLedges` and
`bStopAtLedges`, and direct-reach disagreement telemetry remain separate
measured audits rather than bundled behavior changes.

The pure preflight decision model is now implemented. It authorizes only a
fully known stock-bot walking chain from static safe support through bounded
step-up, forward, at most one slide, unsupported step-down, at most two
near-vertical static-BSP fall continuations, and a walkable harmful pain-zone
landing with known non-immunity. Zero, non-finite, misdirected, dynamic, mover,
unknown, safe, human, ScriptedPawn, jumping, unusual-gravity, and out-of-bound
observations remain disjoint no-decisions. Runtime integration remains shadow-
only until it reproduces the implicated Deck trace.

## Iteration 48: benchmark-only walking preflight observer

Iteration 48 connects the pure model to `TickWalking` only while the opt-in
`bot-benchmark` headless driver is active. Before a real walking subiteration,
the observer probes the supported origin, step-up, forward, actual
step-down, and support endpoint without moving the pawn. If the endpoint is
unsupported, it records a provisional fall forecast. The real stock movement
and `MayFall` callback then run unchanged; a post-callback record is confirmed
only when the same life, invocation, iteration, semantic target, endpoint, and
fresh evidence still agree. It never vetoes a step, restores a position,
changes acceleration or latent state, or calls script. Normal rendered UT and
Unreal play do not run this observer.

The Release candidate SHA-256 was
`DEC5F63752639238D2CE1256CD8AB34BBFED95CC43FF8F1309B3AB7E4C974ADD`.
For Deck seeds 104729, 271828, and 314159, each baseline/candidate comparison
passed exact equivalence for all five artifacts after excluding only the new
preflight exact fields, the preflight diagnostic payload, and output-directory
provenance. Candidate `r0`/`r1` repeats for all three seeds also passed exact
five-artifact equivalence while excluding only output-directory provenance.
The observer is therefore behavior-neutral and deterministic on this matrix;
that result does not establish that its forecast is correct enough to act.

Across the three candidate seeds, the observer recorded 13,086 observations,
669 unsupported endpoints, 12,643 no-decisions, 443 provisional harmful-pain
predictions, 67 post-`MayFall` confirmations, 23 debounced authorizable
episodes, and zero diagnostic overflows. The exact reason partition was 10,021
supported endpoints, 1,671 callback-required/unknown-script-transition
rejections, 524 non-static start supports, 166 invalid step deltas, 41 invalid
step sequences, 101 incomplete fall forecasts, 119 safe fall landings, and 443
harmful pain falls. These are observer classifications, not prevented falls or
quality improvements.

The motivating positive trace did not pass. At telemetry tick 471 in seed
104729, Necroth left support while moving toward `BulletBox4`; the precommit
record correctly starts from the prior supported position, but its forecast is
incomplete after the maximum two wall continuations and therefore produces no
authorization. It is not promotable. The negative trace at tick 55 in seed
314159 is handled correctly: Visse's forward BSP contact requires the real
`HitWall` callback, which performs `EAdjustJump` and changes script/physics
state, so the observer returns the explicit callback-required unknown reason
without forecasting or acting.

The current runtime fall forecast is not `TickFalling` parity. It uses different
substep timing and residual-motion arithmetic, predicts velocity rather than
deriving it from actual displacement/elapsed where native physics does so, and
cannot reproduce script-visible `HitWall`, `Touch`/`UnTouch`/`Bump`, mover,
dynamic-actor, or zone-transition effects. The minimum next helper design is
to factor `BeginFallingParityStep`, `ResolveFallingParityDirectSweep`, and
`ResolveFallingParityAlignedSweep`, then drive them with the exact bounded
`TickPhysics` substeps of at most 0.02 seconds and explicit-origin
`ProbeMoveCollision` sweeps. The pure numerical helpers and their focused tests
are now implemented, including blocked aligned sweeps, displacement-derived
velocity, threshold asymmetry, and conservative evidence bounds. They are not
connected to runtime forecasting. Any script-visible callback or mover,
dynamic, or zone evidence must fail open. Before any later live restore is
considered, a reverse move and static walkable non-pain support must also be
proven from the post-callback state.

The unchanged benchmark-only observer also passed a cross-game check on Unreal
Gold 226b `DmDeck16`: baseline/candidate and candidate-repeat comparisons were
exact for all five artifacts. The run recorded 5,353 observations, 624
unsupported endpoints, 107 provisional predictions, zero post-`MayFall`
confirmations, zero episodes, and zero overflows. It demonstrates shared-path
coverage and determinism, not a quality improvement.

No live walking veto is enabled. Iteration 48 is useful instrumentation, but
it neither prevents the known Deck fall nor improves bot results, and is
therefore **not release-ready or merge-ready as a bot behavior change**.

## Iteration 49: retail pawn subzone callbacks

Exact UT436 and Unreal 226b package/disassembly evidence showed that Surreal
sent `FootZoneChange` and `HeadZoneChange` to the wrong receiver with the wrong
argument. Retail calls the pawn with the new `ZoneInfo`, retains the old
`FootRegion`/`HeadRegion` during the callback, then publishes the new region.
The shared fix also removes native pain/drowning timer compensation because
the exact Pawn script handlers already implement entry and exit timing.

The pre-fix and candidate executable SHA-256 values were respectively
`D541794E2BBF3BB239AD3E7C7239917E190890D5BE3C5F2BC648A19B1446E80E` and
`25ACADC3F4441226F032B5D85668D4BE92882410698434ED49F4E62117F9F7DD`.
Three UT Deck cases and Unreal `DmDeck16`/`DmDeathFan` all passed exact
five-artifact candidate repeat comparison. All ten candidate matches completed
and passed structural analysis.

The A/B quality result is mixed. UT seed 104729 changed K2/D4 to K1/D4,
unassisted environmental deaths 1 to 2, hazard deaths 2 to 3, and walls 291 to
319. Seed 271828 stayed K1/D2 while walls rose 380 to 399. Seed 314159 changed
K2/D3 to K1/D4, unassisted environmental deaths 1 to 2, enemy-contributed
environmental deaths 0 to 1, hazard entries/deaths 1 to 3, walls 422 to 626,
and movement-intent stuck episodes 0 to 2. In every UT case, the first A/B
divergence is 16 health points at pain entry rather than movement. Correct
scripted pain timing exposes suicides that the broken callback had delayed or
masked; it does not create evidence that the bot decision was safe.

Unreal `DmDeck16` remained artifact-equivalent at K0/D0. Unreal `DmDeathFan`
kept 3 kills while deaths improved 8 to 6, unassisted environmental deaths 5
to 3, hazard entries 14 to 5, walls 643 to 605, and stuck episodes 1 to 0.
The callback change is required cross-game engine fidelity and restores bot
overrides such as `FindAir.HeadZoneChange`, but it is not bot-release-ready by
itself. The UT regressions make conservative pre-entry hazard classification
and realized falling correlation the next blocking work.

## Iteration 50: realized falling parity and pain-column groundwork

The benchmark now correlates each real walking-to-falling transition with the
actual bounded `TickFalling` moves. It compares the pure forecast with the
real requested delta, collision, endpoint, and displacement-derived velocity.
Script-visible `BasedActor`, encroachment, `Bump`, `Touch`, `UnTouch`, region,
foot-region, head-region, and `HitWall` callbacks are explicit barriers rather
than guessed-through observations. Pain entry, landing, death, continuity
loss, life replacement, and the 96-step record budget have exact terminal
accounting. This observer performs no extra move, callback, random draw, or
gameplay mutation.

The analyzer requires the complete counter and record group, a strict
step-outcome partition, monotonic correlations, finite evidence, canonical
integers, legal callback masks, exact record/counter reconciliation, and the
runtime's 96-step bound. It derives completion, comparable coverage, match,
mismatch, and unknown fractions. The comparator can ignore the bounded record
group only by explicit audited field names. A future vertical pain-column
classifier has the same fail-closed quality vocabulary: precision, recall,
false-positive rate, labeled fraction, and exclusive TP/FP/FN/TN/ambiguous/
unknown accounting.

The final executable SHA-256 is
`1C9DDE3EA29639EA07EC0A2E7CB0CAC33ACD1FE53DA698C893F107D78ADB52CE`.
It is byte-identical to the executable used by the QA matrix. Candidate
repeats were exactly equivalent across all five artifacts for UT Deck seeds
104729 and 314159 and Unreal `DmDeck16` and `DmDeathFan`. Baseline/candidate
comparisons were also exact after ignoring only `output_directory` and the
new realized-parity counters/records.

UT seed 104729 recorded 13 episodes and 90 steps: 77 comparable matches, 13
callback barriers, no mismatch or unknown, 12 landings, one continuity loss,
and 85.56% comparable coverage. Seed 314159 recorded 10 episodes and 152
steps: 142 matches, 10 barriers, no mismatch or unknown, eight landings, one
continuity loss, and 93.42% coverage. Its 90% completion is one live episode
at the deliberately short run boundary. The known Necroth fall matches for
nine clear steps before a real `HitWall` barrier, then enters pain and loses
continuity in swimming. Visse's wall-adjusted jump is correctly barred at the
first script callback rather than being misclassified as a predictable clear
fall.

Unreal `DmDeathFan` recorded six episodes and 105 steps: 99 matches, six
barriers, no mismatch or unknown, six landings, 94.29% coverage, and complete
terminal accounting. `DmDeck16` supplied only one callback-barrier step, so it
proves deterministic lifecycle handling but not the per-fixture comparable
coverage gate. More Unreal fixtures must supply non-callback clear falls before
the observer itself is fully qualified.

The pure vertical pain-column prototype requires a known unsupported endpoint,
bounded static-world vertical collision, independent walkable support, and
continuous center/foot/head zone evidence. Movers, dynamic actors, callbacks,
sample gaps, horizon exhaustion, or evidence caps return unknown. Its negative
result means only "no harmful pain observed," never "safe." It has focused
unit coverage but is not connected to runtime telemetry or control.

This slice is deterministic and behavior-neutral, and its numerical forecast
agrees with every comparable sampled step. It does not yet prevent a suicide,
repair a stall, or pass the vertical classifier's held-out gates. The bot
release remains **not release-ready and not merge-ready**.

Exact UT436 and Unreal 226b disassembly has now identified the next shared
runtime correction. Both retail `physFalling` implementations use a strict
`normal.z > 0.7` landing threshold, project the remainder after the first wall,
perform a second move, call `TwoWallAdjust` and perform a third move after a
second nonwalkable contact, and reconstruct horizontal velocity while restoring
the iteration-start Z velocity. Retail selects and charges the whole current
time slice before the sweeps; hit fractions scale spatial residuals but never
become outer remaining time. Only an independently pre-existing backlog can
continue under the eight-iteration bound. Surreal currently stops after the
second move and rebuilds all three velocity components. That defect exactly
fits the frozen Athena and Ash BSP-crease traces. It will be implemented and
A/B-qualified as a separate live iteration.

## Iteration 51: retail two-plane falling physics

Both target retail binaries independently prove the same reachable path.
Each iteration snapshots location and velocity, integrates gravity/fluid/air
control for the selected slice, charges that whole slice from outer remaining
time, and makes the direct move. A nonwalkable direct contact calls `HitWall`
and moves the projected residual. A second nonwalkable contact calls `HitWall`
again, applies `TwoWallAdjust`, and makes a third move. A walkable direct or
second contact and a walkable/ditch third contact land under strict
`normal.z > 0.7`. Nonbounce wall resolution reconstructs XY from realized
iteration displacement divided by slice time while restoring the pre-gravity
iteration-start Z velocity. The loop is capped at eight; normal `TickPhysics`
inputs of at most 0.02 seconds consume one complete slice.

The implementation and pure parity model now share those thresholds, slice
selection, two-wall adjustment, ditch predicate, and velocity reconstruction.
Focused tests cover exact 0.7, a higher walkable normal, normal and independently
split backlogs, cross-plane and same-side adjustment, gravity-integrated Z
restoration, third-move geometry, malformed evidence, and the existing
two-plane safety/contact fixtures. Direct walkable landing no longer emits the
pre-existing nonretail `HitWall`; a static nonwalkable two-plane sequence emits
exactly two callbacks before adjustment.

An intermediate executable (`D506DA30...3AE269C`) was explicitly rejected.
It reused the first-hit time residual after aligned/third sweeps had already
consumed the same spatial residual, causing about 1.96x first-divergence motion
and later velocities up to roughly 19,600 units/second. It deterministically
increased hazard deaths in both games and is not committed. A second review
also corrected integrated-Z preservation, multi-slice sources, and walkable
callback order before final qualification.

The final candidate SHA-256 is
`8F7BF38B8C4CDFD2B815E7876BE531E7E00FCCE01B07DF171060DC5637D09139`.
The primary observer baseline is `1C9DDE3E...ADB52CE`; extra behavior-only
controls use `A7982728...B345E4` from the same zone-callback gameplay state but
without iteration-50 instrumentation. Eight UT/Unreal configurations each ran
twice for 30 simulated seconds. Every pair completed and was exactly equivalent
across all five artifacts after normalizing only `output_directory`.

Every comparable realized-falling step in the iteration-51 evidence matches
with zero numerical mismatches and zero record overflows. Direct walkable
landings initially exposed a record-vocabulary gap: a zero-error landing became
`unknown` immediately before its separate `landed` terminal because only
`matched_clear` existed. Iteration 52 closes that gap with a distinct
`matched_landing` step; it does not relabel a collision as clear.

The final quality result is mixed rather than releasable. UT Deck seed 314159
improves from K4/D5 to K1/D2 while retaining one unassisted environmental death;
hazard deaths fall 2 to 1, seams 3 to 1, and walls 525 to 473, although hazard
entries rise 2 to 4. Seed 104729 changes K2/D3 to K0/D1 with the same one
unassisted environmental death and one hazard death, but hazard entries rise
1 to 3, seams 1 to 13, and walls 333 to 352. Seed 271828 changes K3/D4 to K0/D1,
keeps one unassisted environmental death, reduces hazard deaths 3 to 1, and
keeps walls approximately flat at 342 versus 340. Survival improves, but combat
engagement and kills regress sharply.

Unreal DeathFan seed 424242 changes K1/D2 to K0/D1 while retaining the same one
unassisted hazard death; walls fall 1,483 to 262, but one bot still has a long
no-progress interval. DeathFan seed 123, DmDeck16, and DmHealPod remain
death-free and nearly or exactly neutral. UT Morpheus is a decisive policy
regression: K2/D4 becomes K0/D3, hazard deaths rise 2 to 3, walls 330 to 2,283,
and seam detections 3 to 433. No tick exceeds the exact two-callback retail
bound and affected pawns continue tangential motion, so this is not the
rejected duplicated-time defect; it exposes repeated stock low-gravity wall
behavior that still needs policy control.

This is a binary-proven shared engine-fidelity correction and a useful
foundation for safety work, but it is **not a bot release**. It does not meet
combat, hazard-entry, Morpheus wall-contact, or cross-map quality gates. The
next live policy must prevent harmful pain-column entry and replan repeated
wall/seam trajectories without undoing retail physics. Held-out maps remain
unopened, and the branch remains **not release-ready and not merge-ready as a
complete bot improvement**.

## Iteration 52: matched direct-landing evidence

Realized falling now has an exclusive `matched_landing` step outcome and
`falling_parity_realized_matched_landing_steps_exact` counter. A direct partial
static-world hit qualifies only when `normal.z > 0.7` and velocity,
requested-delta, and endpoint errors are all at most 0.001. A larger error is a
real mismatch. The matched-landing step stops numerical forecasting; the
separate `landed` record still terminates the episode lifecycle. Analyzer
version 16 validates the static collision evidence and includes both matched
clear and matched landing in comparable/matched fractions while retaining an
exact step partition.

The executable SHA-256 is
`72A3EE06650AC4D7F560E8BAB27C23FAC24AA3FED0329A901AA6F64AA07630F8`.
UT Deck seeds 314159 and 104729, UT Morpheus seed 424242, and Unreal DeathFan
seed 424242 each ran twice for 1,800 ticks. All four repeat pairs are exactly
equivalent across all five artifacts after normalizing only `output_directory`.
Each run is also equivalent to its iteration-51 candidate after explicitly
excluding only the new matched-landing counter, the replaced unknown counter,
the bounded realized records, and output path. Gameplay counters and the older
shadow stream are unchanged.

Deck314159 records 512 matched-clear steps, 11 matched landings, 30 callback
barriers, zero mismatch/unknown/overflow, and 94.58% comparable coverage.
Deck104729 records 253 clear matches, five landing matches, 22 barriers, zero
mismatch/unknown/overflow, and 92.14% coverage. Morpheus has 344 clear matches,
eight barriers, zero mismatch/unknown/overflow, and 97.73% coverage. DeathFan
has 182 clear matches, two landing matches, seven barriers, zero mismatch/
unknown/overflow, and 96.34% coverage. Every comparable fraction is 100%
matched.

This fixes a release-measurement blocker and remains behavior-neutral. It does
not improve the candidate's kills, pain entries, or Morpheus wall cycles, so the
bot-release judgment remains unchanged.

## Iteration 53: falling-hazard causal audit and observer scope

An exact repeat audit of iteration-51 candidate-3 traces shows that a fixed
vertical pain column cannot be the sole hazard classifier. Deck seed 104729 has
an ordinary support-loss fall whose forecast becomes unknown at a callback and
then reaches pain after a short post-wall leg. Deck seed 314159 contains both a
combat-momentum fall that never arms the walking observer and a later ordinary
support-loss suicide redirected by repeated wall callbacks. Seed 271828 enters
pain after a walking `HitWall` adjustment launches the pawn upward. Unreal
DeathFan reaches pain after a long redirected fall, including 823 units of
horizontal travel after its last callback. These are distinct causal classes,
not one vertical-drop failure.

The next behavior-neutral observer therefore arms on every committed stock-bot
falling episode, records a typed transition source, and creates a new forecast
generation after a script callback or external velocity/physics discontinuity.
Each generation samples the predicted swept path at bounded spacing and
correlates it with the same pawn life's actual segments until callback, pain
entry, landing, death, or continuity loss. Static walkable support uses the
retail strict `normal.z > 0.7` rule. Movers, dynamic actors, unknown zones,
unreturned callbacks, water/physics-zone transitions that have not been
integrated, and exhausted horizons remain explicit unknowns. A bounded
`NoHarmfulPainObserved` result is evidence only for that generation, never a
claim that the whole route is safe.

Four same-life safe landings are frozen as negative controls, including a
568-unit Deck fall with a late `HitWall` that still lands without pain. This
rules out blanket callback, high-drop, and falling vetoes. The current telemetry
also cannot distinguish an actual `MayFall` dispatch from a support-loss path
where no authorization was produced; iteration 53 must add a transition-source
mask and pre/post-callback state rather than infer that source after the fact.
Live control remains disabled until the shadow demonstrates useful recall on
the audited positives, preserves all safe negatives, and repeats exactly.

Damage truth and bot policy are separate. Outcome correlation uses
`bPainZone && DamagePerSec > 0`; `DamageType != ReducedDamageType` only marks
whether stock Bot/Bots avoidance logic considers that damage when choosing a
retreat state. A different harmful-zone identity or a harmful entry simultaneous
with a non-zone callback is ambiguous. A known harmful entry after a negative
forecast is a false negative, including harmful water; unrelated combat death
before a forecast terminal is censored unknown, not a false positive or true
negative.

The initial per-generation bounds are 256 actual swept subsegments and four
seconds. Aligned and two-wall-adjusted legs may consume zero additional time
because retail already charged their outer falling slice. A four-second horizon
that expires while the pawn is still falling closes unknown and starts a typed
`horizon_continuation_commit` generation. This covers the audited Deck and
DeathFan intervals without assigning Morpheus's 9.5-to-15.7-second wall cycles
to one causal forecast. The runtime retains only the active generation and
emits completed summaries immediately, avoiding a large per-pawn history array.

## Iteration 54: clean default-off Unreal repeat

The clean bot branch was rerun on owner-installed Unreal Gold 226b using
`DmDeathFan?Game=UnrealShare.DeathMatchGame`, seed `424242`, four skill-3 bots,
and 1,800 fixed 1/60-second ticks. The failed-navigation route-cost experiment
was explicitly disabled in the matrix identity. Both repetitions completed and
were byte-equivalent across `events.jsonl`, `shadow-decisions.jsonl`,
`shadow-manifest.json`, and the normalized manifest/summary (only each output
directory differed). The generated matrix, analyzer, repeat-equivalence report,
and provenance are retained locally under
`qa/runs/2026-07-25/failed-navigation-default-off/`.

This is a reproducibility and safety-boundary check, not a quality win. Each
repeat records five kills, eight deaths, three suicides, three unassisted
environmental deaths, 409 `HitWall` calls, and one 2.1-second
movement-intent-no-progress event. The falling observer has five true positives
and no labelled false positives or negatives, but 1,757 of 1,794 completed
generations are `unknown` (97.94%). It therefore cannot authorize a general
falling or callback intervention. The next policy must target a repeatedly
observed, causally attributable hazard/stall segment, retain the existing
script-owned timeout handoff, and qualify separately in UT436 and Unreal Gold.

## Iteration 55: DeathFan water-egress live candidate rejection

The swim-egress observer was then enabled for the same DeathFan configuration.
It authorizes the falling pre-move anchor for all observed harmful-water
episodes, including the three unassisted deaths. The existing live candidate
was run as a paired, two-repeat observer-versus-live matrix. Both observer
repeats and both live repeats are exactly deterministic after normalizing their
output directory. The matrix, analysis, repeat-equivalence reports, and
provenance are retained locally under
`qa/runs/2026-07-25/hazard-swim-egress-deathfan-live/`.

The live acceleration overlay is rejected. It applies three times for 286
ticks but records zero successful exits. Relative to the observer baseline,
kills fall by three, suicides and environmental deaths each rise by one, and
no-progress time rises by 17.62 seconds. Reduced total deaths and wall calls
do not offset those deterministic combat, suicide, and permanent-stall
regressions. The experiment remains benchmark-only and default-off.

Trace review identifies the next safe measurement slice. Rhiannon and Dante
enter the same harmful water at ticks 778, 978, and 1438 respectively and die
2.75 seconds later; each has a known harmful forecast 28 ticks (about 0.467
seconds) beforehand. Callback and impulse generation boundaries fragment that
single physical fall into mostly `unknown` individual generations. A new
observer must therefore latch only after two consecutive same-life,
same-fall, same-known-zone harmful forecasts across such a boundary, record
lead time to actual entry, and reset on a safe landing, zone change, life
boundary, or contradictory forecast. It remains read-only until this
episode-level signal repeats on UT436 and Unreal Gold while preserving safe
controls.

## Iteration 56: persistent harmful-fall latch (observer only)

The shared falling observer now has a default-off-policy, read-only persistent
harmful-fall latch. It starts only from a valid known harmful forecast and
promotes only when a second forecast has the same life, physical fall, harmful
foot/physics zone identities, and water classification after an exact callback
or external-impulse boundary. A changed key, safe forecast, non-boundary
terminal, landing, death, abandonment, or life boundary clears it. A promoted
latch becomes a confirmed entry only when the realized terminal is the same-key
`confirmed_harmful_forecast`; it records a bounded observed-sweep lead time in
milliseconds. No acceleration, path query, target, timer, physics, or script
state is changed.

The counter group is emitted as complete exact telemetry and fail-closed in the
quality analyzer: candidates, promotions, resets, confirmed entries, lead
samples, and summed lead milliseconds. Focused runtime lifecycle tests cover
callback promotion/confirmation and a safe contradictory continuation. The
benchmark protocol test, full executable build, and all 106 Python benchmark
tests pass.

On DeathFan seed 424242, the observer records six candidates, five promotions,
five confirmed harmful entries, and five lead samples totaling 3,005 ms (601 ms
mean). This proves the callback-split Unreal signal is measurable without
changing play. The UT Deck16-II seed 271828 target instead records one candidate
followed by a reset and zero promotions/entries; two runs are byte-equivalent.
The latch is therefore deliberately **not** a shared live gate yet. The next
observer iteration must explain the single-generation UT water entry or derive
a separate, equally causal UT-safe signal before any recovery action is tested.

## Iteration 57: single-generation harmful-fall prefix (observer only)

The shared observer now separately measures the UT-style case that has one
harmful aligned-continuation forecast followed by a long, unbroken realized
prefix. It starts only for an avoidance-relevant harmful water endpoint with
known foot and physics zones. A single exact, clear zero-time aligned setup
sweep is allowed but does not count; promotion then requires three exact,
positive-time, direct, clear, dry, callback-free sweeps in the same life, fall,
generation, and starting physics zone. Any mismatch, unknown evidence, water or
pain entry, callback, impulse, zone change, terminal, life boundary, or
replacement generation clears the candidate. A promotion confirms only on that
same generation's `confirmed_harmful_forecast` terminal. This is telemetry only:
it changes no bot movement, timing, target, physics, or script state.

The new six-field exact telemetry group (candidates, promotions, resets,
confirmed entries, lead samples, summed lead milliseconds) is serialized,
validated as a complete group, and exposed with derived fractions/mean lead in
the analyzer. Focused observer and telemetry tests pass, as does the full
executable build and the 106-test Python benchmark suite.

Two UT436 `DM-Deck16][` seed-271828, four skill-7 bot runs are exactly
equivalent after normalizing only `output_directory`. Each records the same
single candidate, promotion, confirmed entry, and 1,027 ms observed lead for
Alys, while gameplay remains K0/D1/S1. Unreal Gold `DmDeathFan` seed 424242,
four skill-3 bots, remains K5/D8/S3 and records zero candidates: its relevant
falls are interrupted rather than an unbroken single generation. This is the
intended separation. The earlier two-forecast persistent latch remains
observer-only as well; external-impulse repetition is not causal evidence for a
future live recovery policy. No live bot experiment is authorized by this
measurement slice.

## Iteration 58: exact combat-damage telemetry (measurement only)

The benchmark now records canonical `TakeDamage` health loss for each tracked
participant without changing play. Every positive health reduction is counted
once in `damage_taken_exact` and assigned to exactly one source: another
benchmark participant, the victim itself, or a nonparticipant/environmental
instigator. Only damage dealt to a different benchmark participant contributes
to `damage_dealt_to_other_participants_exact`; self-inflicted splash is kept
separate and cannot inflate combat effectiveness. Counters are monotonic and
fail closed on overflow or a canonical damage frame that loses its tracked
victim.

The serializer emits the five-field group atomically. The analyzer requires
the complete group for every v2 snapshot, checks each bot's source partition
against total damage, checks that aggregate inter-participant dealt equals
inter-participant taken at every sample, and exposes the resulting exact totals
plus `damage_efficiency_to_other_participants`. This is an accounting ratio,
not an aim/accuracy score and not a quality gate by itself.

The full 107-test Python suite, telemetry serialization test, and the three
death-attribution contract tests pass. Fresh default-policy repeats were run
with the current schema: UT436 `DM-Deck16][`, seed 271828, four skill-7 bots,
and Unreal Gold 226b `DmDeathFan`, seed 424242, four skill-3 bots. Each pair
is byte-equivalent in its bot snapshots after excluding the output-directory
path. Deck records 128 total damage, all nonparticipant/environmental, with no
inter-participant damage. DeathFan records 1,182 total damage: 542 from other
participants, 640 nonparticipant/environmental, zero self damage, and 542
dealt to other participants (the accounting ratio is 1.0 by construction).
The artifacts and analyzer reports remain local under
`qa/runs/2026-07-25/damage-telemetry-smoke/`.

This closes the prior missing *damage attribution* measurement, but not the
match-competence gate. Accuracy, pickup/resource acquisition, useful map
coverage, controlled role/start-layout swaps, a multi-map baseline, and a
causally justified movement recovery candidate still need evidence before a
bot behavior change can be considered for merge.

## Iteration 59: pre-entry falling-hazard recovery trial (rejected live policy)

An experimental, default-off falling-hazard recovery now measures the precise
single-generation prefix from iteration 57 and can, only when its separate
live flag is explicitly selected, apply bounded horizontal air control toward
the first verified dry anchor of the same life and fall episode. It requires a
promoted prefix, known finite safe anchor 8–256 units away, clear horizontal
probe, autonomous authority pawn, falling physics, and a short duration bound.
It leaves vertical motion unchanged and aborts on invalid context, unsafe
zone, blocked probe, landing, water/pain entry, death, timeout, or flag
withdrawal. Observer-only selection changes no acceleration.

The telemetry group records calls, rejected context, missing active fall,
missing prefix, promotions, eligibility, anchor/probe rejections, live starts,
active ticks, and each terminal result. Native-counter accumulation is covered
by the death-attribution coordinator fixture; pure recovery-gate, falling
observer, telemetry serialization, and full executable tests pass. The
analyzer requires the complete monotonic group and rejects impossible counter
relationships.

Two UT436 `DM-Deck16][` seed-271828, four skill-7 observer-only runs are
event-identical. They show one promoted and eligible Alys episode, no live
application, and therefore preserve the stock run. The two corresponding live
runs are also event-identical but are rejected: recovery applies once for 24
ticks, while total deaths and suicides rise deterministically from 1/1 to 2/2.
The gate-local terminal label did not establish avoidance of the later generic
harmful entry. The live flag remains experimental and
default-off; it is not a merge candidate. Unreal Gold 226b `DmDeathFan` seed
424242 observer smoke records zero recovery promotions/applications and retains
K5/D8/S3, as expected for its callback-interrupted fall pattern.

## Iteration 60: observed start-layout coverage

The matrix runner can now use `start_layouts` instead of bare seeds. Each
layout provides an ID, seed, and required SHA-256 fingerprint of the observed
tick-zero participant state. The analyzer joins the immutable actual roster to
the run-start telemetry and hashes ordered roster index, requested skill,
actual class/name, exact position/velocity, physics mode, and state. A declared
layout must match its expected fingerprint; paired baseline/candidate cases
must match each other; and an asserted distinct-layout set fails if it collapses
to one observed layout. This is truthful coverage of the engine's observed
initial pawn state, not a claim that the runner can force a particular
`PlayerStart`.

Seed-only matrices remain compatible. True role swapping remains unavailable:
the current enhanced-policy selection is process-wide and the verified stock
rosters do not provide stable per-slot policy binding. A role-swap gate will be
implemented only after a per-participant policy adapter can bind immutable
roster indexes and prove balanced slot assignments in both UT436 and Unreal
Gold 226b.

## Iteration 61: resource and navigation coverage observation

The benchmark now distinguishes an observed inventory transition from a useful
but unproven script side effect. Only an unowned world `Inventory` source whose
owner becomes the controlled pawn after its outermost `Touch` dispatch counts
as `confirmed_pickups_exact`. Weapon, ammo, health, armor, and other categories
are an exact partition of that total. A source destroyed during the same
dispatch without this transfer is retained only as the separate
`pickup_source_consumed_unconfirmed_exact` diagnostic; it is never added to
resource acquisition. The implementation is shared UE1 `UInventory` logic, not
package-specific item names, and it remains read-only.

The same slice snapshots the shared `NavigationPointList` after controlled
roster setup, rejects invalid/cyclic/oversized catalogs, and tracks a bot only
when its collision cylinder overlaps a catalogued node. Per-bot visited and
catalog counts plus the shared roster-union count are monotonic exact
observations. The analyzer validates category partitioning, immutable catalog
denominators, bounded counts, and a common union value before reporting
fractions. These are coverage/resource measurements, not a quality score or
release gate.

Fresh 30-second observer smokes completed and structurally validated on both
profiles. UT436 `DM-Deck16][` seed 271828 retained K0/D1/S1, had a 251-node
catalog with 70 union nodes (27.89%), and observed no confirmed transfer.
Unreal Gold 226b `DmDeathFan` seed 424242 retained K5/D8/S3, had a 116-node
catalog with 72 union nodes (62.07%), and likewise observed no confirmed
transfer. UT436 `DM-Morbias][` seed 104729 supplied the first positive witness:
Nikita made one confirmed weapon transfer and one separate consumed-source
diagnostic. Two 30-second runs had byte-identical event and shadow-decision
streams; their only full-artifact differences were the configured output paths.

The Deck causal audit also corrects the earlier live-recovery interpretation:
the gate-local recovery counter did not establish avoidance of Alys' later
water entry. Its 24-tick action had already stopped before the generic harmful
entry, and Alys still died after failed egress; a later extra direct-self death
made the live variant K0/D2/S2. The experiment remains rejected and no live
behavior policy is promoted from this measurement slice.

## Iteration 62: bounded harmful-water egress witness

The remaining reproducible Deck16 failure now has a terminal, per-episode
observer witness. It starts only after the existing exact static harmful-water
classification, snapshots the pre-entry anchor and immediate movement target,
records the nearest collision-probed safe navigation candidate as a candidate
rather than a route, and finishes only on primary-zone clearance, death, or
an explicit life/context reset. It is benchmark-only and does not alter
acceleration, movement intent, path selection, or the default-off controls.

The candidate's entry, minimum, and terminal distances are measured separately
from the entry movement target, with strict progress/regression sample counts.
This matters because the Deck witness had no movement target at the harmful
entry: treating a later `MoveTarget` as if it caused the entry would be false.
The bounded pure observer has focused lifecycle/overflow tests, and the
telemetry fixture covers its JSON encoding. The quality analyzer validates the
bounded terminal stream's exact schema, actor/sequence/episode uniqueness,
candidate/target distance consistency, and the final record-plus-overflow
bound against observed egress episodes. These diagnostics remain neutral
evidence rather than a score or release gate.

Fresh paired observer/control matrices (two repetitions each, recovery and
live controls off) passed on UT436 `DM-Deck16][` seed 271828 and Unreal Gold
226b `DmDeathFan` seed 424242 at
`qa/runs/2026-07-25/hazard-swim-egress-v2-observer-pair/`. Both variants had
zero deltas across the checked quality metrics. The Deck failure remained
Alys's tick-644 `death_before_exit` episode from a `falling_direct_sweep`:
the selected collision-safe `PathNode144` candidate was 619.46 units away at
entry, reached a 488.18-unit minimum, and still had not been reached at death
(52 progress versus 43 regression samples). Thus the candidate is evidence of
some stock movement toward a safe node, not evidence that the direct probe is
an egress route or that it can be reached before drowning. No live policy is
authorized from this result.

## Iteration 63: multi-seed Deck egress falsification sweep

A six-seed, 60-second UT436 `DM-Deck16][` observer sweep ran two identical
observer variants for each seed (`271828`, `104729`, `424242`, `314159`,
`161803`, and `8675309`) at
`qa/runs/2026-07-25/hazard-water-egress-observer-sweep/`. The revalidated
v21 analyzer accepted all twelve immutable run artifacts and their six paired
comparisons. The baseline stream produced 22 terminal harmful-water episodes:
16 `primary_zone_cleared` and 6 `death_before_exit`.

Eight episodes selected a collision-safe navigation candidate. Five of those
still cleared the primary water zone, while three died; importantly, several
of the successful clearances retained candidate distances of 544–768 units at
their terminal sample. The three candidate-backed deaths likewise retained
373–575 units. The candidate is therefore neither an observed destination nor
a causal explanation for either exit or death. This falsifies using the
existing `TryMove(..., true)` candidate as a route certificate or direct live
target. The next prerequisite for a candidate policy is a separate,
read-only first-hop/route certification that does not mutate stock path state.

## Iteration 64: read-only static-walk certificate

The next observer is deliberately narrower than a pathfinding replacement. At
harmful-water entry it starts from the captured pre-water anchor, snapshots a
bounded navigation graph, collision-probes a direct anchor-to-node first hop,
and then accepts only an unpruned UE1 `R_WALK` continuation with adequate pawn
cylinder dimensions. Water, harmful, dynamic, player-only, lift, teleport,
jump, swim, fly, door, and special-path nodes or reachspecs are rejected. The
snapshot traversal does not call `ActorReachable`, `FindPath*`,
`MarkReachableNavEndPoints`, `ClearPaths`, `PathSpecialHandling`, or write a
route cache, endpoint flag, pawn position, target, or acceleration.

The resulting telemetry says only `certified_static_walk_continuation`,
`no_eligible_direct_first_hop`, `no_static_walk_continuation`,
`search_budget_exhausted`, or `not_attempted_missing_anchor`; it includes the
selected first-hop and continuation names, static cost, hop count, and exact
visited-node count. A certificate proves a collision-clear static first hop
and one static graph continuation, not runtime reachability, traversal time,
stock intent, or survival. Pure fixtures cover a valid route plus blocked
first hop, pruned/undersized/jump edges, unsafe endpoints, and search budget
exhaustion. The v23 analyzer validates all certificate availability and
rejection invariants within the terminal egress record.

Fresh two-repetition paired observer/control runs passed on UT436
`DM-Deck16][` seed 271828 and Unreal Gold 226b `DmDeathFan` seed 424242 at
`qa/runs/2026-07-25/hazard-static-walk-certificate-pair/`. Every checked
quality delta was exactly zero. Deck's Alys death had a certified
`InventorySpot192 -> PathNode29` continuation with static cost 274. DeathFan
had a certified `PathNode65 -> PathNode19` continuation with static cost 180,
yet four observed terminal episodes died and one cleared across each
repetition. Certification therefore does not establish a causal escape route
or authorize a live target. The next safety experiment must measure the
pre-entry and post-entry causal prefix against these witnesses; direct egress
steering remains rejected.

## Iteration 65: static-certificate first-hop progress falsification

The static certificate now records its own first-hop position and exact
entry/minimum/terminal distances, plus progress and regression samples. These
are intentionally separate from the old in-water direct-nav candidate: the
certificate starts from the safe falling anchor and can choose a different
node. The observer owns the distances from immutable entry and terminal
positions; the analyzer rejects missing or mismatched availability, impossible
minimum distances, and impossible certificate/anchor combinations.

Fresh two-repetition paired observer/control matrices again passed unchanged
on Deck16 and DeathFan. Every checked quality delta remained zero. The added
trace falsifies emergency use of the certificate: Deck's certified
`InventorySpot192` first hop began 1206.71 units away, reached only 1202.34,
and regressed to 1348.18 at death (43 progress / 52 regression samples).
DeathFan's certified `PathNode65` first hops began 1740.70–1814.08 units away;
the four terminal deaths never improved on their entry distance, while the one
clearance ended essentially unchanged at 1741.13. A collision-clear long line
and static continuation are therefore not a time-bounded swim escape. No
candidate-target or generic acceleration policy is authorized. The next
causal slice remains the separate pre-entry partition for the non-egress
environmental deaths.

## Iteration 66: exact same-death hazard co-observation

The benchmark driver now stages terminal movement witnesses at the outermost
`GameInfo.Killed` entry, binds that stage to the accepted attribution-scope
token, and finalizes it only when the outer `Killed` result yields an
attribution decision. The emitted `hazard_death_partition_records` stream has
one ordered record per classified death, including killer relation, the exact
five-way attribution, environmental callback source, movement intent/target,
and optional terminal water-egress, falling-hazard, and falling-parity
references. Abnormal or unfinalized outer scopes fail closed rather than
leaking a staged witness into a later death.

This is explicitly a *same-death co-observation*, not a causal label. Every
death can finish an active water or falling observer: an enemy kill while a
pawn is swimming must not be described as a water suicide. The analyzer
therefore reconciles every emitted record exactly against the attribution
partition and requires each claimed witness to match a `death_before_exit` or
`died` terminal diagnostic from the same telemetry event. It rejects
fabricated links, regressing sequences/times, partial or unavailable IDs,
and unknown physics, attribution, source, or correlation vocabulary. Any
future intervention hypothesis must filter to environmentally attributed
deaths and must separately establish a controllable pre-entry cause.

Fresh two-repetition observer/control matrices with the v24 analyzer completed
on UT436 `DM-Deck16][` (seed 271828) and Unreal Gold 226b `DmDeathFan` (seed
424242) at `qa/runs/2026-07-25/hazard-death-partition-pair/`. The Deck
observer recorded the reproducible unassisted `PainTimer` death as a
same-death water-egress terminal with movement intent and `LiftExit6` as its
target. The observer-disabled control correctly has no water witness; this is
observer availability, not evidence of a different death. On DeathFan, the
new records distinguish two unassisted `PainTimer` deaths per run from direct
enemy kills that happened while swimming: the latter do carry water terminals
in the observer variant, proving why terminal water presence alone cannot
authorize a policy. One unassisted witness has no active movement intent.
All eight artifacts completed and passed strict schema/cross-reference
validation. No behavior policy changed and no release gate is claimed.

The next experiment is pre-entry only: use the existing falling transition,
anchor, navigation target, and physics evidence to identify a bounded,
environmentally-attributed harmful-water prefix before acceleration or routing
is changed. It must reject direct-enemy deaths, targetless/no-intent states,
unreachable candidates, and all unproven generic egress steering.

## Iteration 67: direct pre-entry certificate falsification

A new pure `DirectHarmfulWaterEntryCertificate` now accepts only a completed
full-step falling forecast with known dry center/foot/head/physics start zones,
an avoidance-relevant harmful-water foot endpoint, no reduced-damage exception,
no transient harm, and an all-direct, clear, finite, contiguous predicted
segment chain. It has no pawn, navigation target, death, attribution, or live
movement dependency. The falling observer records only prospective outcomes:
candidate, same-generation harmful-water confirmation, safe landing, or
unresolved boundary; the metrics remain neutral counters and do not claim
causality or prevention.

Fresh two-repetition paired observer/control matrices at
`qa/runs/2026-07-25/direct-harmful-water-prediction/` passed the v24 analyzer
on Deck16 and DeathFan with zero checked quality deltas. The result rejects
this certificate as the current policy predicate: all eight artifacts had zero
candidates, including the repeatable Deck16 unassisted `PainTimer` water death
and DeathFan's three unassisted environmental deaths per run. The strict
certificate is therefore not an observed prefix for the actual failures; no
opt-in steering policy is authorized. The next observer must count the
certificate's fail-closed rejection reasons against the same pre-entry falling
generations before broadening any predicate.

## Iteration 68: certificate source-gate audit

The direct-entry observer now emits one exact result for every accepted falling
generation: either `source_not_eligible` (the certificate was deliberately not
evaluated) or its single fail-closed certificate result. The result stream is
complete and non-negative, and the analyzer requires the `certified` result to
equal the prospective candidate count. This keeps a source-gated bypass
separate from a rejected forecast; neither outcome is a death-causality claim
or a live control authorization.

Fresh two-repetition control/observer matrices passed the v25 analyzer at
`qa/runs/2026-07-25/certificate-rejection-audit/` on Deck16 and DeathFan.
Each Deck artifact reports 1,091 source-gate bypasses, four eligible forecasts
rejected solely because their endpoint was not harmful water, and zero
certifications. Each DeathFan artifact reports 1,787 source-gate bypasses,
nine non-harmful-water endpoint rejections, and zero certifications. Quality
and death counts remain exactly unchanged between control and observer because
this is measurement only.

The exact terminal records identify the repeatable Deck harmful-water forecast
as `aligned_continuation_commit`, while the repeatable DeathFan harmful-water
forecasts are `external_impulse_commit`; both are intentionally bypassed by
the current `existing_falling_commit` certificate. Thus the prior zero result
was not evidence that the endpoint checks rejected the observed harmful
forecasts. It instead falsifies this certificate's source domain as a useful
policy predicate. The next work must model source-specific, prospective
evidence: aligned continuations may be explored only as a separate
movement-controlled hypothesis, while external impulses require attribution
and controllability evidence before any steering or avoidance intervention.
No source gate is broadened and no live policy is authorized by this audit.

## Iteration 69: aligned-continuation command-provenance audit

Aligned continuations now carry source-specific command-provenance evidence in
their paired start/terminal falling diagnostics. A monotonic native command
token is created at the four movement latent commands. Immediately before the
retail `HitWall` callback, the observer snapshots the live bot command, latent
state, target, destination, acceleration, and whether the collision is static
world geometry. The aligned continuation then reports one fail-closed result:
no witness, non-static contact, no live command, a changed command token/state/
target/destination/acceleration, or `intact_command_but_no_action_lead`.
The latter is deliberately not an authorization: the aligned `TryMove` is
inside the current falling physics slice after collision and callback handling,
so the next bot tick cannot honestly prevent it.

Fresh two-repetition control/observer matrices passed unchanged with the v25
analyzer at `qa/runs/2026-07-25/command-provenance-audit/`. On every Deck
artifact, 277 aligned continuations retained an intact command but no action
lead and 110 had no command witness. Crucially, the repeatable harmful-water
continuation remained in the latter group (`no_command_witness`), so it is not
a bot-command-proven candidate. Every DeathFan harmful-water continuation
remained an `external_impulse_commit` and therefore
`not_aligned_continuation`; its external boundary has no live policy domain.
All quality/death results remained identical between control and observer.

This closes both currently observed failure sources as immediate steering
targets. A future Deck intervention must predict a harmful collision
continuation *before* the initial bot command is committed, with static-world
geometry and a measured, nonzero pre-move action lead. A future DeathFan
intervention needs a separate causal/controllability model for external
impulses. Neither is approximated by the current observer, and no live policy
is enabled.

## Iteration 70: walking `MinHitWall` parity research

The repeated Deck and DeathFan deaths do not currently supply a defensible
bot-controlled pre-move intervention. A separate UE1 parity audit instead
identified an unmeasured native dispatch contract that can materially change
stock bot recovery: `Pawn.MinHitWall` is the minimum
`HitNormal dot Velocity.Normal` value for physics to issue `HitWall`. The
public Unreal-era Pawn declaration documents that contract, and recovered
UT436 Botpack source sets it to `-0.5` in `PreSetMovement` and temporarily
adjusts it by `+0.15` in `Wandering` and `TacticalMove`. Its `HitWall`
handlers own the mover, `PickWallAdjust`, fall-state, and `MoveTimer = -1`
replan behavior.

The unified `TickWalking` currently gates the callback solely on the fixed
`-0.2 < HitNormal.z < 0.2` band and never reads `MinHitWall`. This is therefore
a plausible shared UT436/Unreal226b fidelity hypothesis, distinct from a new
AI policy. The nearby unfinished non-movable-actor branch is not a bot cause:
it is nested under `UPlayerPawn`, whereas UT `Bot` and Unreal `Bots` are Pawn
subclasses and use the existing generic wall path.

The next slice remains observer-only. It must record each walking collision's
normalized movement dot normal, live `MinHitWall`, legacy Z-band decision,
threshold decision, blocker kind, and script callback outcome; then compare
the distributions and quality deltas in paired UT Deck16-II and Unreal
DeathFan runs. The exact proprietary UT436 native comparison operator has not
been independently observed, so no callback predicate or bot behavior changes
in this iteration. Any later correction must first pin that boundary in pure
fixtures and preserve the callback-before-stock-script-recovery ordering.

The behavior-neutral observer now emits bounded per-collision records and
monotonic exact counters. Fresh paired two-repetition artifacts at
`qa/runs/2026-07-25/minhitwall-dispatch-audit/` are byte-identical between
control and the existing non-live observer variant. Each Deck16-II artifact
has 33 walking collisions: 27 satisfy both the old Z band and the inferred
`MinHitWall` predicate, two satisfy neither, and four are legacy-only
callbacks. All four disagreements are static-world contacts. Each DeathFan
artifact has 12 collisions: five satisfy both predicates and seven are
legacy-only callbacks; five of those disagreements are movers and two are
static-world contacts. This establishes that the ignored property is exercised
on both target games, but it does not prove the proprietary exact boundary or
authorize a dispatch change. In particular, mover suppression needs a separate
retail ordering oracle before a shared correction is considered.

## Iteration 71: walking collision resolution and notification separation

If a retail oracle validates the `MinHitWall` comparison, the correction must
separate physical walking collision resolution from `HitWall` notification.
The generic walking path must retain its current legacy-Z-band aligned slide,
second `TryMove`, time accounting, and no-backwards check even when a glancing
contact is rejected for script notification. In particular, nesting the slide
inside the new threshold gate would make legacy-only glancing contacts stop
sliding, which is a movement regression rather than a callback correction.

For every raw generic contact, the eventual implementation should evaluate the
live threshold solely to decide whether to call `HitWall`; accepted callbacks
must still run before the pre-existing slide/recovery work. A threshold-accepted
non-legacy contact must not acquire a new slide path merely because it now gets
a callback. The distinct `UPlayerPawn` pushable-decoration path retains its
mass, velocity, and teleport behavior unchanged pending a separate oracle.

Telemetry must also become contact-accurate before measuring a live change.
The current single initial-contact diagnostic plus an aggregate
`callback_dispatched` flag can attribute a second-slide callback to an initial
contact that did not satisfy the threshold. Each initial and second-slide
collision therefore needs its own decision, callback outcome, callback-induced
physics/deletion result, and monotonically ordered diagnostic record. No
counter-to-record reconciliation is inferred until the driver can prove that
the drain boundary preserves every raw contact or reports an overflow.

Required gates for a future correction are:

1. Pure fixtures for head-on vertical contact (notify and slide), glancing
   vertical contact (no notify but unchanged slide), a threshold-accepted
   non-legacy contact (notify but no newly added slide), exact threshold
   boundary, state-adjusted threshold, and invalid geometry.
2. A runtime fixture proving a rejected glancing collision preserves location,
   remaining time, and second-slide outcome; accepted script callbacks occur
   before slide and safely handle physics changes or deletion; and first/second
   contacts emit independent telemetry.
3. Paired UT436 Deck16-II and Unreal226b DeathFan matrices with the existing
   deterministic, telemetry, and quality-delta checks, plus a retail ordering
   oracle for movers before changing their callback eligibility.

Until those gates pass, this remains a design constraint only: no live
`MinHitWall` callback predicate or bot policy is enabled.

## Iteration 72: isolated retail UT436 `MinHitWall` oracle

The first disposable retail UT436 oracle is now source-controlled under
`Tools/BotBenchmark/RetailHitWallOracle/UT/` and is run only through
`Run-RetailMinHitWallOracle.ps1`. It preserves the stock
`DeathMatchPlus` spawn path by selecting a concrete probe bot through a
`ChallengeBotInfo` subclass; the bot then enters one dedicated probe state.
That state is intentionally a physics-notification oracle rather than a claim
about stock bot decision quality. It logs the live threshold, pre-handler
velocity, normal, normalized dot, wall identity, and the local
`HandleDoor`/`PickWallAdjust` ordering when a mover is actually contacted.

Each invocation creates a new disposable runtime below the requested QA
directory, copies only `System`, uses asset-directory junctions, compiles its
package locally, records full before/after SHA-256 inventories of the installed
retail root, and bounds the dedicated server process before terminating that
local child. The runner explicitly recognizes the UTF-16 local stat log and
rejects a callback-required case that emits no `hitwall_pre` record. The
installed-file guard passed for the successful UT436 smoke.

The first valid static-world observation is retained at
`qa/runs/2026-07-25/retail-minhitwall-ut436-v12/`: the fixed Deck16-II
corridor contact recorded `MinHitWall=-0.500000`, normal/velocity dot
`-1.000000`, and one `HitWall` callback. This is positive evidence for the
documented native gate, but it does not identify its exact comparison operator
or boundary behavior. Attempts to reuse the adjacent lift as the glancing or
mover oracle either fell through without a contact or struck a different
static corridor face. Those are rejected setup results, not filtered-callback
evidence and not mover ordering evidence.

The next oracle slice must place a controlled collision object or a pinned
closed brush mover where the same measured contact can be repeated on both
sides of `-0.5` and `-0.35`. Unreal Gold remains separately required: its
candidate is the DmHealPod north/east Main Hall lift, but the local owner map
is not presently available to pin its actor identity and transform. No shared
dispatch predicate changes on the basis of this one head-on UT observation.

## Iteration 73: controlled dynamic-contact UT436 threshold evidence

The UT436 oracle has now replaced the rejected map-face/lift setup with a
dynamic `BlockAll` contact. For every attempt it selects a `PlayerStart` path
with clear world geometry and walkable floor samples, spawns the blocker,
requires an expanded actor trace to return that exact blocker, and records a
shared monotonic sequence number. The isolated runner rewrites only its copied
`Server.ini` stat-log destination, snapshots the local UTF-16 logs before and
after each process, accepts exactly one changed local log, copies it into the
case directory, and parses only the unique run ID. Its manifest preserves the
raw-log hash, structured sequence, child PID, timeout, and termination method.
Installed retail before/after inventories remained byte-identical.

Three fresh valid runs are retained outside version control under
`qa/runs/2026-07-25/retail-minhitwall-ut436-v23/` through `v25/`:

| Case | Live `MinHitWall` | Contact evidence | Native callback result |
| --- | ---: | --- | --- |
| Head-on | `-0.500000` | preflight identity plus bilateral `Bump` | exactly one walking (`Physics=1`) callback, dot `-1.000000` |
| Nominal glancing | `-0.500000` | preflight identity, bilateral `Bump`, and postflight identity | zero callbacks; `MoveTo` returned |
| Nominal glancing | `-0.350000` | preflight identity plus bilateral `Bump` | exactly one walking callback, observed dot `-0.397676` |

This is the first controlled evidence that retail UT436 changes walking
`HitWall` dispatch across the interval containing the observed glancing dot.
It is consistent with the documented `dot < MinHitWall` interpretation; it
does **not** prove `<` versus `<=` at equality, authorize a generic dispatch
change, or cover mover ordering. A suppressed callback is only accepted when
direct contact is separately observed, never merely because `HitWall` is
absent. The next required oracle work is an equivalent Unreal Gold 226b
dynamic-contact package, repeated boundary-neighbor measurements, and an
independent mover ordering profile. The runtime bot policy remains unchanged.

## Iteration 74: controlled dynamic-contact Unreal Gold 226b evidence

The required 226b counterpart now exists under
`Tools/BotBenchmark/RetailHitWallOracle/Unreal/`. It uses the retail-specific
`UnrealShare.DeathMatchGame` / `UnrealShare.BotInfo.GetBotClass(int)` /
`UnrealI.MaleOneBot` lifecycle rather than reusing UT's `DeathMatchPlus` or
`ChallengeBotInfo` assumptions. The runner's `Unreal226b` profile copies only
the installed `System` directory, creates isolated `Logs`, `Save`, and `Cache`
directories, junctions the necessary asset roots, and patches only copied
`Default.ini` and `Unreal.ini`. It records full installed-root inventories.

Retail Unreal Gold 226b does not instantiate the old local stat logger for
this dedicated UCC mode. The oracle therefore mirrors each tagged record into
the one redirected standard-server stream for that isolated child, and the
manifest labels the retained source `system-log`. This is a deliberate,
per-run fallback—not a scan of ambient logs—and its terminal path emits a
bounded flush filler so records are durable before process shutdown.

Fresh `DmMorbias` results at
`qa/runs/2026-07-25/retail-minhitwall-unreal226b-v7/` through `v9/` have
byte-identical installed before/after inventories:

| Case | Live `MinHitWall` | Contact evidence | Native callback result |
| --- | ---: | --- | --- |
| Head-on | `-0.500000` | preflight identity plus bilateral `Bump` | exactly one walking (`Physics=1`) callback, dot `-1.000000` |
| Nominal glancing | `-0.500000` | preflight identity, bilateral `Bump`, and postflight identity | zero callbacks; `MoveTo` returned |
| Nominal glancing | `-0.350000` | preflight identity plus bilateral `Bump` | exactly one walking callback, observed dot `-0.397680` |

The independently controlled 226b result agrees with UT436 across this
glancing interval, materially strengthening the shared dispatch hypothesis.
It remains insufficient to claim equality behavior, change shared runtime
dispatch, or infer mover ordering. Next: repeated calibrated neighboring
thresholds on both games, a separately verified mover oracle, then the
per-contact slide/callback separation gates from Iteration 71.

## Iteration 75: repeated neighbor thresholds and flat-path admission

Near-threshold repetitions exposed one invalid dynamic attempt: although the
old probe required a walkable floor at five points, it allowed a small
slope/drop and received a falling callback. That artifact is rejected; it is
not walking-gate evidence. The path admission now requires each sampled floor
normal to be at least `0.95` upward and every sampled floor height to stay
within 16 units of the first sample. The first head-on UT re-smoke under that
tighter admission remained a valid walking callback.

With a stable flat UT contact (same preflight normal and observed callback dot
`-0.397676`), `MinHitWall=-0.397000` produced bilateral contact and
`MoveTo` return but no callback; `-0.395000` produced exactly one walking
callback. The corresponding independent 226b DmMorbias repetitions showed the
same no-callback / callback transition at `-0.397000` / `-0.395000` and the
same observed callback dot. All retained runs have byte-identical installed
before/after inventories.

This narrows the retail transition interval on both targets to two
millithresholds, but it does not identify the cause of the remaining gap
(native precision, internal quantization, or a non-observed instantaneous
contact value). Therefore it still cannot prove equality semantics. The oracle
must report this interval honestly and keep the shared C++ predicate unchanged
until calibrated contact-state capture or a boundary-specific retail proof is
available. In parallel, mover ordering needs a separate dynamic mover target;
the previously attempted Deck/HealPod map lifts remain rejected, unpinned
profiles.

## Iteration 76: dynamic retail mover ordering oracle

The map-lift limitation is now removed without fabricating a map profile. Both
retail packages can spawn a hidden `TriggerOpenTimed` mover with collision,
`bUseTriggered=True`, and a 60-second delay. Every case requires the expanded
trace to return that exact spawned mover before moving; the mover overrides
only `HandleDoor` to log around the inherited behavior. The runner's case `2`
requires one walking callback, the expected bot/mover call chain, a handled
return, `WaitingPawn` equal to the probe, a nonzero `SpecialPause`, an explicit
`PickWallAdjust` skip, and no `PickWallAdjust` result.

The fresh controlled UT436 run
`retail-minhitwall-ut436-v35/` and Unreal Gold 226b run
`retail-minhitwall-unreal226b-v13/` both passed with an exact sequence:

`preflight_mover` → `move_begin` → walking `hitwall_pre` →
`handle_door_pre` → `mover_handle_door_enter` →
`mover_handle_door_return(handled=True)` → `handle_door_post` →
`pick_wall_adjust_skipped` → `oracle_complete(mover_handled)`.

The installs' before/after inventories were byte-identical. This closes the
mover-ordering evidence gate for the controlled shared contract, while the
old Deck/HealPod lifts remain unsuitable as map-specific evidence. The only
remaining blocker to a shared walking dispatch correction is calibrated
boundary semantics and the per-contact slide/notification implementation and
fixture gates in Iteration 71.

## Iteration 77: per-contact walking notification telemetry

The shared runtime still preserves the historical `z`-band callback predicate;
this iteration changes observation only. Each walking collision now records
its own pre-callback velocity, `MinHitWall`, blocker classification, and
callback outcome. Records name their contact phase as `primary_forward`,
`aligned_slide`, or the reserved `forward_retry_result`, so a secondary
aligned-slide callback cannot be attributed to the initial forward impact.
Blocker classification is captured before UnrealScript executes, preventing a
callback mutation from rewriting the observed contact provenance.

Callback and movement ordering are unchanged: the primary `HitWall` remains
before aligned slide movement, and a second callback remains after its blocked
slide contact. The telemetry schema and analyzer require a recognized phase;
the C++ serializer fixture and Python schema fixtures cover it. This fixes
measurement attribution, not native dispatch behavior. Boundary calibration
and a controlled forced-corner runtime fixture remain required before any
predicate correction or merge decision.

## Iteration 78: bilateral retail contact witnesses

The retail oracle now records an ordered bilateral `Bump` stream for every
static-blocker contact. Each pair carries the live threshold, velocity,
acceleration, walking state, both collision extents, and an expanded trace
witness. The runner verifies paired ordinal order, exact preflight blocker
identity, live threshold retention, and that a delivered callback immediately
follows the final probe-side witness. A missing retail `Server.ini` on current
GOG UT436 installs is seeded from `UnrealTournament.ini` only in the disposable
runtime; before/after installed inventories remain byte-identical.

Fresh UT436 `v46` head-on evidence and Unreal Gold 226b `v14` head-on evidence
both compile the disposable package and pass these stronger witness checks. In
both games the expanded Bump-time trace normal matches the later head-on
`HitWall` normal, while the center-derived normal has a nonzero vertical
component and is different. This validates the trace as useful raw state but
proves that the center reconstruction is not the native physics operand.

The retained UT glancing observations remain intentionally unmerged as a
boundary proof: `-0.397` suppressed the callback after six paired contacts,
whereas `-0.395` dispatched after two; their dynamically selected corridors
differed. The next oracle extension must pin PlayerStart and direction and
repeat adjacent microthresholds before it can make any equality claim.

A native forced-corner fixture is compiled into the headless driver. Its first
owner-install run exposed an iterator invalidation bug in the fixture itself:
it spawned and destroyed `BlockAll` actors while iterating the live level actor
array. The fixture now snapshots `PlayerStart` locations before any mutation;
it reaches the actual Deck16][ geometry and records a dynamic primary-forward
contact followed by an aligned-slide contact. This is still not a release
gate: the newly spawned stock bot has no callable `HitWall` handler in that
state, and the untouched native walking loop recontacts the blocked geometry
after the first pair. The result remains fail-closed while the fixture is
reduced to a two-contact native observation without inventing script dispatch.

## Iteration 79: pinned microthreshold retail calibration

The retail oracle now has a separate `-PinnedMicrothreshold` mode for the
static glancing blocker. It does not change the default dynamic PlayerStart /
direction search used by the existing milli-threshold matrix. Instead, each
profile supplies a requested PlayerStart transform and candidate direction;
the retail package must resolve exactly one map `PlayerStart`, validate the
same flat path and trace-identified spawned blocker, and use only that one
direction. An absent, ambiguous, blocked, or falling setup is rejected rather
than searching for a different corridor.

Thresholds in this mode are integer millionths (`OracleMinHitWallMicro`) and
are logged alongside the existing float representation. At least three unique
thresholds and two fresh server repetitions are required. The runner compares
the actor identity, PlayerStart transform, preflight path, blocker transform,
normal, direction, and lateral offset across all repetitions; it then requires
one deterministic result per threshold, monotonic callback admission, and at
least one suppressed and one dispatched threshold. Its
`pinned-boundary-summary.json` reports only the observed bracket. It must not
be treated as a proof of `<` versus `<=` or of the unprinted retail native
floating-point operand.

Fresh UT436 and Unreal Gold 226b pinned runs resolved exact map PlayerStarts
and fixed their contact signatures to `+Y`; both owner-install inventories
were byte-identical before and after. In each game, `-0.397` suppressed and
`-0.350` dispatched across two bounded repetitions. This is a reproducible
broad bracket, not a predicate change; an adjacent, mixed-outcome
microthreshold bracket remains required.

## Iteration 80: bounded targetless `MoveTo` timeout experiment

The existing targetless `MoveTo` watchdog timeout had been live without an
explicit experiment selection, making a repaired-stock baseline ambiguous. It
is now default-off and is part of the benchmark command line, run identity,
manifest, summary, matrix provenance, analyzer, and run-comparison contract.
When enabled, it remains deliberately narrow: only a detected, targetless
walking `MoveTo` with finite state and a positive timer may clear acceleration
and set the existing latent `MoveTimer` timeout. It does not steer, choose a
route, alter physics, or run for `MoveToward`, strafing, falling, swimming,
pain-zone, or mover contexts.

The first valid Release pair used four UT436 Deck16][ bots for 30 seconds,
three seeds, and two repetitions per seed. All twelve runs completed. The
enabled variant executed four targetless timeouts across six paired cases and
reduced movement-intent stuck events from 0.67 to 0.00 per run (four pair wins,
two ties); movement-intent no-progress time fell by 0.71 seconds per pair on
average. It did not reduce the one suicide or one unassisted environmental
death per run. It also regressed combat/route indicators: kills and score each
fell by 0.67 per pair on average, while `HitWall` rose by 0.67 and hazard
exposure rose by 0.28 seconds. This is insufficient and mixed evidence, so
the flag stays default-off and is not promoted to the repaired-stock baseline.

The first Debug matrix is retained only as invalid-harness evidence: it wrote
startup telemetry but exited 255 before summaries in both variants. The
Release matrix is the admissible result at
`qa/runs/2026-07-25/targetless-move-to-timeout-v1/ut-deck16-discovery-release-results`.
Before reopening the candidate, reproduce the exact qualifying timeout in a
small fixture and show non-regression in both UT and Unreal paired discovery
runs; do not infer a quality gain from the lower stuck proxy alone.

## Iteration 81: cross-game forced-corner walking `HitWall` fixture

The `bot-walking-hitwall-corner-fixture` previously proved only that native
walking attempted `CallEvent`: a valid UT436 corner produced six native
diagnostics but no callable `HitWall` VM body because the freshly spawned bot
was in `startup`; its hook also suppressed any body it might have reached. The
fixture now reflects a state-local handler before changing state, requires a
non-native `HitWall(vector, actor)` shape, enters the selected state, and then
uses an observer-only VM hook. The portable default is `FindAir`, which the
loaded UT436 `Botpack.Bot` and Unreal Gold 226b `UnrealShare.Bots` packages
both resolve to a real script body. `Bump` is disabled only during the
fixture's native tick; `HitWall` arguments and normal returns are never
replaced or suppressed.

The fixture-only contact budget is default-disabled and is armed only by this
driver. It returns from that one native `TickWalking` invocation immediately
after recording its second contact, after the second script return has
destroyed the temporary `BlockAll` actors. This prevents a third recontact
without changing any ordinary pawn tick. State, physics, velocity,
acceleration, collision flags, event flags, the VM hook, and temporary actors
are restored on every post-setup failure path.

Release evidence at
`qa/runs/2026-07-25/corner-fixture-v2` contains byte-identical repeats for
UT436 `DM-Deck16][` and Unreal Gold 226b `DmDeck16`. Each successful run
records a reflected `FindAir.HitWall` handler, exactly two VM entries and two
normal script returns, exactly two diagnostics in `primary_forward` then
`aligned_slide` order, exact first/second dynamic-blocker identities and
normals, no physics change, and deterministic start/heading data. The
cross-game `Attacking` negative control fails before ticking with zero entries,
returns, and diagnostics because that state has no state-local `HitWall`
handler. `PawnWalkingHitWallDispatchTests`, `ActorMovementTests`,
`HeadlessDriverTests`, and `VMCallHookTests` also passed from the same Release
build.

This makes the fixture admissible evidence for the notification-contract work;
it does not authorize a `TickWalking` predicate change. The exact
`MinHitWall` boundary and glancing mover behavior remain unproven, and the
required calibrated retail microthreshold bracket and quality telemetry work
must still close before a shared runtime correction is proposed.

## Iteration 82: fail-closed move-stall recovery timing evidence

The watchdog now treats each one-shot persistent-movement detection as a
separate recovery episode, starting its clock at detection rather than at the
original no-progress anchor. It records exactly one terminal outcome: cleared
within two seconds, cleared after two but within five seconds, qualified new
navigation replan within five seconds, missed five-second deadline, intentional
stop exclusion, life-boundary censor, run-end censor, or unknown. Reissuing an
equivalent latent command never qualifies as a replan; only a changed live
`MoveToward` navigation target may do so. The observer does not call native
reachability probes, because those probes simulate movement and would change
the behavior under measurement.

Each terminal record has a stable source pawn actor, sequence, life ID,
episode ID, elapsed seconds, and outcome. The benchmark accumulates the exact
counter partition across pawn respawns, drains death records after the life
boundary has been recorded, and explicitly flushes live episodes as run-end
censors before final telemetry. Bounded queues expose overflow counters; an
analyzer must reject an incomplete field group, overflow, malformed records,
or a disagreement between counters and records. These metrics are evidence
only: no live recovery policy has been promoted, and the targetless `MoveTo`
candidate remains default-off and rejected pending a fresh cross-game fixture.

Fresh Release observer smokes from the current build completed and passed the
structural analyzer on UT436 `DM-Deck16][` and Unreal Gold 226b `DmDeathFan`
(four bots, 900 and 1,800 ticks each). All four runs emitted the complete
zeroed counter/record group with no overflow; neither short deterministic
sample contained a qualifying detection, so both derived fractions are null.
That is the intended fail-closed result, not a pass.

## Iteration 83: cross-game forced move-stall recovery fixture

`bot-move-stall-recovery-fixture` now drives the production watchdog directly
without changing a game script or enabling the rejected targetless-timeout
experiment. It creates one ordinary controlled bot at a safe walking
`PlayerStart`, disables only that fixture bot's script tick/timer/tactics,
arms a finite targetless native `MoveTo` with zero acceleration, and ticks it
for eight 0.25-second slices. The fixture then makes one verified safe
horizontal relocation and ticks once more. It requires exactly one detection,
one episode start, one reset, one `ClearedWithin2Seconds` terminal record, no
other outcome, no queue overflow, and a record whose actor/IDs/timing reconcile
with the counters. A fixture fails rather than silently enabling the default-off
targetless `MoveTo` timeout experiment.

Release runs passed twice per game: UT436 `DM-Deck16][` artifacts
`qa/runs/2026-07-25/move-stall-fixture-v1/ut436-deck16-r3` and `-r4`, and
Unreal Gold 226b `DmDeathFan` artifacts
`qa/runs/2026-07-25/move-stall-fixture-v1/unreal226b-deathfan-r1` and `-r2`.
Every run has one terminal record with a 0.25-second clearance and no overflow.
This qualifies cross-game measurement and lifecycle accounting, not bot
behavior: it does not show that natural play produces enough recovery
opportunities or that any candidate bot policy improves them. Natural,
complete campaign opportunities remain required before recovery fractions can
pass a quality gate.

## Iteration 84: causal avoidable-suicide classifier contract

The existing five-way death attribution and hazard-death partition are retained
as attribution evidence only. In particular, an unassisted environmental death
is not silently relabeled "avoidable": the current live records contain only
death-time state, terminal observer correlation, and recent-enemy proxies, not
the decision that led into the hazard.

`BotBenchmarkAvoidableSuicide` is a pure, cross-game, fail-closed classifier
that establishes the required contract before live wiring. It permits an
`avoidable` outcome only for an unassisted environmental death with a matching
same-life actor/command witness, movement intent, a harmful forecast available
before commitment, terminal/forecast agreement, a known safe alternative at
that same commitment, and proof that no external intervention occurred. Enemy,
ambiguous, direct-self, and external-impulse cases are excluded from the
navigation avoidability denominator. Missing, mismatched, or overflowed witness
data is unknown; a proven absent safe alternative or non-predictable outcome is
unavoidable. The rate denominator is only `avoidable + unavoidable`, with an
explicit unknown fraction.

The pure fixture covers the positive control and each fail-closed branch. It
does not make `avoidable_suicide_rate` available to campaigns yet: next, a
read-only pre-commit observer must capture the command/life token, forecast,
bounded safe alternative, and external-intervention evidence before movement
commits, then reconcile the witness with the existing terminal record. The
known Deck16-II and DeathFan deaths remain excluded or unknown until that
evidence exists.

## Iteration 85: bounded pre-commit command witness

The existing walking-step preflight diagnostic now snapshots the pawn's
monotonic native movement-command token at its already read-only pre-commit
boundary, before the first walking `TryMove`. The token is carried alongside
the existing actor, life generation, invocation, iteration, semantic target,
destination, and forecast evidence. This adds no probe, callback, movement,
or policy action. Zero explicitly means that no native movement command was
available; older telemetry-v2 artifacts normalize a missing field to zero and
remain analyzable.

A fresh Release UT436 `DM-Deck16][` smoke emitted 379 serialized command-token
fields and passed the structural analyzer at
`qa/runs/2026-07-25/precommit-command-token-smoke/ut436-deck16-r1`. This proves
the witness transport only. It deliberately does not connect a token to a
death, prove a safe alternative, or expose `avoidable_suicide_rate`; those are
the next required terminal-reconciliation and safe-alternative work items.

## Iteration 86: command-stable confirmation

Post-`MayFall` confirmation now requires the same non-zero movement-command
token captured at pre-commit, in addition to the existing endpoint, life,
latent-state, target, and destination checks. A callback that silently issues
a replacement movement command therefore changes the diagnostic outcome to
`post_callback_evidence_changed`; it cannot be used as causal evidence merely
because the replacement happens to reuse the same target or destination. The
paired diagnostic validator applies the same invariant and rejects a changed
token. This is observer bookkeeping only and has no movement, probe, callback,
or policy action.

## Iteration 87: DeathFan swim-egress discovery

A fresh 120-second, four-bot stock run reproduces the visible survival failure:
UT436 `DM-Deck16][` records five suicides and four kills, while Unreal Gold
226b `DmDeathFan` records 25 suicides, nine kills, and 22 unassisted
environmental `PainTimer` deaths (21 while swimming). The DeathFan artifacts
are in `qa/runs/2026-07-25/behavior-discovery-v1`.

The default-off hazard-swim-egress experiment was then evaluated with the same
DeathFan seed. Observer-only mode found 24 eligible/authorized egress episodes
without changing the stock result. Live mode produced 27 eligible/authorized
episodes, reduced suicides from 25 to 20, reduced unassisted environmental
deaths from 22 to 16, and increased kills from nine to 12. It also increased
hazard exposure from 141.88 to 144.47 seconds and 14 authorized episodes died
before exit. Therefore this is a discovery signal, not a promotion: stock stays
default, and repeats, UT cross-game qualification, causal attribution, and hazard
exposure non-regression must all pass before the experiment can be reconsidered.

The exact same experiment also changes UT436 Deck16-II, reducing suicides from
five to one and hazard exposure from 33.65 to 7.58 seconds, but reducing kills
from four to one. Both live runs were byte-identical per game, so this is a
deterministic cross-game tradeoff rather than sample noise. It fails the combat
non-regression requirement and remains rejected/default-off; a follow-up must
find why egress steering suppresses engagement before another live-policy run.

During that run, a valid authorization could steer and later hit a blocked
re-probe. The analyzer previously treated those sequential observations as a
disjoint authorization partition and rejected the artifact. It now bounds each
counter independently by authorizations, while retaining all other exact
counter checks; this makes the live telemetry structurally valid without
weakening its quality gates.

## Iteration 88: command-owned swim-egress correction

The rejected live experiment was found to replace `Acceleration()` for every
active swim tick (78 ticks in the UT run and 286 in the DeathFan run), with no
arbitration against the bot's latent `MoveTo`, `MoveToward`, or strafe command
and no restoration after swimming physics. That is a direct mechanism for the
observed UT combat regression, but not proof that it is the only cause.

The experiment now declines before collision probing whenever an active stock
movement latent owns the pawn. Where the egress vector is permitted, it is a
single-physics-step acceleration overlay: the pre-overlay acceleration is
restored after swimming integration only if the overlay still owns it, so a
callback-issued replacement command wins. Falling also terminates the existing
live authorization, preventing a stale vector from resuming after a later water
transition. `HazardSwimEgressLiveSteerTests` covers the fail-closed
command-ownership decision; the Release engine and the full 122-test Python
benchmark suite pass.

This is a lifecycle correction to the rejected, default-off experiment, not a
promotion. It requires fresh paired, fixed-seed UT436 Deck16-II and Unreal
226b DeathFan owner-data runs, including repeat identity and the existing
combat/survival/exposure gates, before it can be credited with any bot-quality
improvement.

## Iteration 89: command-stable harmful-fall terminal reconciliation

The analyzer now derives three observer-only, nullable metrics from complete
record streams: command-stable post-MayFall harmful `begin_falling` witnesses;
the subset that has the same actor/life/invocation/iteration parity `died`
record and exactly one matching death partition; and the unassisted
environmental subset. This is deliberately not named “avoidable”: it does not
yet prove a safe alternative or exclude all external intervention.

Any absent witness/parity/death stream, a preflight or parity record overflow,
or an inconsistent parity/partition claim produces null evidence or a
fail-closed analyzer error. Enemy-attributed terminals remain counted as a
terminal correlation but never enter the unassisted subset. Focused positive,
enemy, landed, legacy-token, overflow, missing-start, and duplicate-claim
tests pass together with the full 123-test Python suite. Fresh owner-data
campaigns must supply a non-zero complete opportunity set before these metrics
can inform an avoidable-suicide gate.

## Iteration 90: egress overlay ownership fixture (superseded)

The live egress unit test covered the acceleration transaction itself, not only
its eligibility policy. It proved that the one-physics-step overlay restored the
saved stock acceleration only when the swimming integrator still owned the
vector. That ownership result remains historical evidence, but the overlay
itself is now removed: even its command-safe form is not an acceptable way to
own stock bot movement and it caused the earlier combat regression.

## Iteration 91: one-shot stock planner handoff

The egress experiment has a replan-only candidate for the case that the stock
bot owns an active movement command. It never replaces acceleration. Instead,
it clears acceleration and issues the engine’s established `MoveTimer = -1`
completion signal exactly once, allowing the stock UnrealScript state to choose
its next route. The handoff requires the existing authorized positive-DPS water
episode and an already collision-probed safe navigation candidate; otherwise it
does nothing. The pre-existing exact forced-replan counter records every such
action.

This is an opt-in experiment under the same default-off live flag, not a
promotion. Two UT436 Deck16-II 7,200-tick, seed-271828 runs are byte-identical;
relative to repaired stock, exposure falls from 33.6501 to 27.5334 seconds and
entries from six to five, while kills remain four, deaths remain nine, and
suicides remain five. It is therefore retained only as a disabled measurement
experiment: the proxy improvement is not a survival improvement. It still lacks
Unreal 226b evidence and seed diversity.

## Iteration 92: independent Opus review and reissue-witness next slice

The configured Claude Opus workflow independently reviewed the goal, execution
plan, continuation plan, live egress code, and causal analyzer. It agreed that
the replan-only candidate remains default-off: preserving combat while reducing
exposure is useful evidence, but unchanged deaths and suicides prohibit
promotion. It also confirmed that historical artifacts with an absent parity
lifecycle should yield null causal metrics rather than a false zero or a
structural rejection; a mixed-identity fixture now proves that one incomplete
bot nulls only its own causal metrics and cannot erase another bot's complete
evidence.

Before any further movement policy, add observer-only witnesses for each forced
replan: whether stock code reissues the same hazardous target or clears without
reentry. Only if that evidence proves unsafe target reissue may a new,
one-shot destination handoff be considered. It must run only in a navigation,
disengaged state, use the already collision-probed candidate target through the
stock latent command, respect a script replacement target, and never write
acceleration. Promotion still requires deterministic paired UT436 and Unreal
226b multi-seed evidence of fewer suicides or unassisted environmental deaths,
with no combat, wall, hazard-entry, or determinism regression.

## Iteration 93: exact planner-handoff outcome witness

The default-off planner handoff now records one terminal, mutually exclusive
outcome for every forced `MoveTimer = -1` handoff: the same stock command was
reissued, a different stock command was issued, the primary hazard cleared
before a command, the pawn fell, the pawn died, or the observation was censored
at a life boundary, run end, or abandoned episode. The new observer snapshots
only the pre-handoff semantic command and observes the next explicit native
movement command; it changes neither target, route, latent state, acceleration,
nor physics. The pre-existing default-off handoff remains the only live action:
it clears acceleration and sets `MoveTimer = -1` once. The outcome counters
must partition forced replans in the final run sample. Earlier in-flight
samples may have unresolved handoffs, but can never claim more resolved
outcomes than replans.

`HazardSwimEgressLiveSteerTests`, `BotBenchmarkTelemetryTests`, the Release
engine build, and the full 123-test Python analyzer suite pass. The new analyzer
group is optional as a complete group, so historical artifacts remain valid;
new handoff artifacts fail closed on a partial group, an outcome overflow, or a
final non-partition.

Two 7,200-tick UT436 Deck16-II seed-271828 runs are byte-identical (events
SHA-256 `481F17DC9E25D364EFE50E671CD921ED493CAE9E6ED8679E06F057A46AC4DCC2`).
They retain the known K4/D9/S5 result, 27.5334 seconds exposure, five entries,
and two forced replans. One was followed by the same command and one by a
different command; neither cleared, fell, or died before the replacement.
Therefore there is no evidence yet that handoff failure is simply the stock
script reselecting the same target, and no destination override is justified.

The retained Unreal 226b DeathFan fixture also repeats byte-identically (events
SHA-256 `6FFDAAEDA03230E6646ECEB592CB0D83474020DCFEA397498B3D0E7503F6DBF2`) at
seed 271828, but has zero forced replans. A one-run Unreal `DmDeck16` discovery
has zero hazard entries and zero handoffs. These are valid determinism and
schema checks, not cross-game behavioral qualification: the opportunity set is
zero. The next work is a bounded multi-seed owner-data discovery matrix to find
an Unreal harmful-water/falling opportunity or to reject this shared candidate
for lack of cross-game relevance.

## Iteration 94: planner-handoff multi-seed discovery result

The UT436 Deck16-II seed set is now complete at one deterministic discovery
repetition per seed. Across seeds 104729, 271828, and 314159, six handoffs
occurred: two reissued the same semantic stock command and four received a
different command. No handoff cleared, fell, or died before that replacement.
The `same` outcome is consequently neither dominant nor causally connected to
a death; it does not justify overriding the script target. The seed metrics are
respectively K6/D11/S5, K4/D9/S5, and K5/D7/S2, with 23.13, 27.53, and 22.48
seconds of hazard exposure. These are discovery observations, not a quality
claim or a replacement for paired repaired-stock comparison.

The retained Unreal 226b DeathFan fixture was also run at all three seeds. It
has substantial hazard exposure and entries (106.72/40, 116.95/33, and
105.02/27 seconds/entries) but **zero forced handoffs in every run**. The
candidate's required falling-pre-move authorization therefore has no observed
Unreal opportunity here. The shared planner handoff remains default-off and is
not eligible for destination-redirection follow-up or promotion. Continue from
the independently attributable death trajectories rather than relaxing its
authorization or using zero-opportunity Unreal evidence as support.

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
held-out results remain unopened until parameters freeze. No iteration through
45 is merge-ready: Deck still has avoidable hazard deaths, DeathFan remains an
Unreal safety/stall failure, and the required held-out evidence has deliberately
not been opened. Live causal death attribution is available, but measurement
truth does not make the observed behavior safe.

## Iteration 95: harmful-residence observer follow-up

1. Run the optional exact residence group across UT436 and Unreal Gold
   multi-seed discovery. Require final terminal partitions, no counter
   overflow, and same-build repeats before interpreting command arbitration.
2. For UT436, inspect only episodes with a collision-probed candidate followed
   by another movement command. Establish whether the command is a combat
   interruption, target replacement, or unsafe navigation reissue; totals
   alone never authorize target redirection.
3. For Unreal Gold, add a read-only external-impulse fall controllability
   witness before considering any action. It must certify a collision-clear dry
   support alternative inside the same forecast horizon and reconcile to a
   same-life terminal outcome.
4. Promote no behavior until it has a non-zero opportunity set in both games,
   paired multi-seed safety improvement, unchanged-or-better combat and
   navigation metrics, deterministic repeats, and held-out-map evidence.

## Iteration 96: external-impulse air-control rejection

The existing falling parity forecast is now used as a read-only counterfactual
witness. It only evaluates `external_impulse_commit` falls whose original
forecast is complete, harmful, and bot-avoidance relevant. Eight bounded
air-control directions are independently collision- and zone-forecasted. A
certificate requires `NoHarmfulPainAtStaticLanding` inside the same configured
horizon; dynamic collision, water, missing support, unknown state, and every
other result fail closed. The exact decisions partition harmful witnesses.

On Unreal Gold 226b DeathFan, seed 271828 has 260 qualifying witnesses and
2,080 alternatives, with zero certificates in two byte-identical 7,200-tick
runs. Reject air-control steering for that discovered external-impulse class.
The next discovery slice should identify non-air-control mechanisms with a
separate witness—such as an earlier navigation commitment, lift/edge route
choice, or damage-residency arbitration—rather than weakening this certificate.

## Iteration 97: launch-navigation provenance and route-veto rejection

Claude Opus independently recommended this as the final diagnostic necessary
before a route-edge policy: capture whether a route head exists, distinguish a
navigation-point target from another target, and require a dominant fatal
provenance class before authorizing a launch-tick veto. The new observer-only
fields are attached to the bounded existing water-egress record. They are
optional as one complete group for legacy artifact compatibility; partial new
groups fail closed, and unavailable provenance is prohibited from claiming
names, route state, or vectors.

On Unreal Gold 226b DeathFan, two byte-identical 7,200-tick seed-271828 runs
have events SHA-256
`8A3EDE441B38BA85BD4EB2AFCA8448B59BC144598E753F17928E573AD53E77F3` and
retain 27 egress episodes: 16 deaths before exit and 11 primary-zone clears.
All 16 fatal records have a launch snapshot; 11 have a route head, five have a
navigation-point move target, and 11 begin above z=1000. The remaining fatal
records include no route head or no navigation target, so route commitment is
not a dominant sufficient cause. Reject the proposed shared route-hop veto at
this point. This remains a diagnosis result, not a behavior-quality change.

## Iteration 98: launch forecast rejection

Following the independent Opus review, the existing pure bounded falling
forecast is captured read-only at the first tick of each continuous fall and
stored beside the bounded water-egress diagnostic. It runs only with the
existing default-off hazard-egress benchmark instrumentation, performs no
actor-state write, and distinguishes an unavailable forecast from a completed
harmful forecast. Historical egress artifacts without the optional pair remain
valid; a partial pair or a harmful unknown result fails closed.

On Unreal Gold 226b DeathFan, two byte-identical 7,200-tick seed-271828 runs
have events SHA-256
`BDB1446059183AC2C657C0B3ED0B2B40A326326C453392204D30EA79DC583DC2`.
They retain 27 egress episodes, 16 deaths before exit, and 11 fatal launches
above z=1000. The forecast completes for all 11 but classifies zero as harmful.
This fails the required pre-commit recall gate (at least 9 of 11) and rejects
any forecast-driven rollback/replan candidate. The remaining investigation
must explain why the forecast becomes harmful only after the falling sequence
has progressed; do not weaken its fail-closed classification merely to create
an intervention opportunity.

## Iteration 99: ActorReachable support hypothesis and early-probe rejection

The mixed launch provenance pointed upstream of route ownership. A read-only
Opus review of `UPawn::ActorReachable` confirmed that its walking dry-run can
advance horizontally across an unsupported drop without ever checking support
beneath the resulting position. UnrealScript bot code uses this predicate to
choose direct movement instead of `FindPathToward`, so this is a plausible
common cause for DeathFan's mixed inventory, weapon, navigation-point, and
targetless launch commands.

The first local experiment was deliberately not retained. It probed support
immediately after the forward dry move, while the simulated pawn was still
raised by `stepUpDelta` and before wall-slide resolution. That leaves only
`0.3 * MaxStepHeight` of downward tolerance instead of the walking physics
path's `1.3 * MaxStepHeight`, and rejects valid downsteps. The deterministic
DeathFan seed-271828 run regressed from K7/D29/S22 to K12/D38/S26; reject this
placement and keep it out of commits.

The next slice is observer-only and default-off: after wall-slide resolution
and the return to walk height, dry-probe the existing `stepDownDelta` and
record only the would-flip ActorReachable verdict. It must demonstrate
unchanged stock behavior in shadow mode, identify the fatal high-platform
launches as old-reachable/new-unreachable, and preserve a control set of
ordinary downsteps. Only then may a separately gated live experiment return
unreachable; no route rewrite, command override, random motion, or air steering
is authorized.

## Iteration 100: exact harmful-residence arbitration result

`Analyze-HazardResidence.py` is a separate, fail-closed analysis of the exact
benchmark event stream. It requires the current complete residence counter
group, positive-DPS samples, non-regressing time/death counters, and a
successful run result; missing legacy fields are an error rather than a zero.
It reconstructs positive-DPS episodes with a 0.25-second clear grace period,
then records entry physics/state/target, target and latent-action churn,
candidate-observed and candidate-superseded deltas, re-entry, and a death,
clear, or run-end-censored terminal.

The current observer-only three-seed rerun deliberately disables the rejected
live egress overlay. UT436 Deck16-II has 42 episodes: 19 deaths, 23 clears,
six observed direct-navigation candidates, and six supersessions. Five of
those six supersessions terminate in death (`PathNode144` or `PathNode145` in
five fatal episodes; `PathNode30` in the one cleared episode). Unreal Gold
DeathFan has 160 episodes: 70 deaths, 87 clears, three run-end censors, and no
observed direct-navigation candidate or supersession. Repeated analysis of
the matched seed-271828 UT and Unreal streams is byte-identical.

This rejects a shared route pin or target override. UT has a real arbitration
signal, but Unreal has no authorization opportunity, so a common behavior
would be speculative. Preserve the observer and seek a separate controllable
Unreal witness; do not relax candidate certification, infer a safe escape from
a clear terminal, or use the existing UT signal as a cross-game quality pass.

### Cross-certification correction (2026-07-25)

The residence analyzer now certifies its independently reconstructed episode
partition against the engine's exact per-participant counters: episodes,
clears, deaths, run-end censors, command changes, observed candidates, and
candidate supersessions. Native life-boundary and unknown terminals remain
fail-closed because the sample stream cannot reconstruct them. This exposed
and corrected two lifecycle defects before any bot behavior was considered:

- the analyzer could begin a new residence from a dead pawn's final harmful
  sample after the engine had already recorded its death terminal; and
- the post-physics water-only sample could transiently clear a residence while
  another pawn region remained in a positive-DPS zone, inflating native
  re-entry counts.

Fresh 7,200-tick observer-only runs now pass both the normal quality gate and
the cross-certificate in UT436 `DM-Deck16][` (seed `271828`, difficulty 7) and
Unreal Gold 226b `DmDeathFan` (same seed, difficulty 3). The UT stream reports
six residences, six deaths, two observed candidates, and two supersessions;
the Unreal stream reports 28 residences, 25 deaths, three clears, and no
candidate/supersession. For both games, enabling the observer with the live
egress overlay disabled produced a behaviorally identical event stream after
removing only configuration and `hazard_*` telemetry fields. Evidence is in
`qa/runs/2026-07-25/hazard-residence-cross-certification-v3/` and
`qa/reports/bot-ai/hazard-residence-cross-certification-v3/`.

This certifies the diagnostic witness, not a route pin, target override, or
movement intervention. The cross-game evidence still rejects a shared live
policy: UT has witnessed supersession, while Unreal has no certified candidate.

## Iteration 101: current-position static-first-hop probe rejection

The static-walk certificate is computed from a pre-fall anchor, so its first
hop is not an authorization for a later water-residence action. A new
observer-only field pair records whether that retained first hop is within the
bounded current-position probe radius and whether the identical dry collision
sweep is clear from the pawn's *current* position. The pair is optional for
historical artifacts but complete-or-failing when present. It makes no actor,
route, target, acceleration, latent-action, or physics write.

Two valid 7,200-tick seed-271828 repetitions are byte-identical on each game.
UT436 `DM-Deck16][` at its verified external skill 7 has seven egress
diagnostics and three fatal terminals: all three have a certified static
continuation, but one is outside the current probe range and the other two
are collision-blocked. Unreal Gold 226b `DmDeathFan` at its verified native
external skill 3 has 27 diagnostics and 16 fatal terminals: ten have a
certified static continuation but are outside the current probe range, while
six have no eligible direct first hop. No fatal witness has a current-position
collision-clear first hop.

Therefore reject a first-hop steer, route pin, or target override. The result
is diagnostic evidence only: a clear capsule sweep would still require support,
capability, hazard-exit, and command-ownership certification before a live
candidate could be considered. The next safe work is the read-only catalog
tranche: decoded reach flags, node-zone joins, reverse/pruned paths, traversal
actors, bot configuration, and validators. It will explain whether excluded
assisted traversal or capability constraints account for the missing direct
option without changing stock bot behavior.

## Iteration 102: cross-game walking HitWall corner fixture qualification

The `bot-walking-hitwall-corner-fixture` now has owner-data qualification in
both adapters. Its invocation requires the normal headless `--autoplay` launch
contract, creates a controlled one-bot match, reflects a real state-local
`FindAir.HitWall` body, and uses two temporary `BlockAll` actors to force one
primary-forward contact followed by one aligned-slide contact. The fixture
removes the blockers only after the second normal script return, so it rejects
both a missing callback and an uncontrolled third contact.

Two repetitions pass in each game with byte-identical result text:

- UT436 `DM-Deck16][`, external skill 7: SHA-256
  `2f45dac3a962367a5315ffb904b6b29797f48776723c6c85a28b3b03ec4fea8e`.
- Unreal Gold 226b `DmDeathFan`, external skill 3: SHA-256
  `eab62f18ccee25427a95107c3d5b777750c266066bb524faaf0c15a7369c82e5`.

Every passing result has exactly two intercepted VM entries, two normal script
returns, no diagnostic overflow, `primary_forward` then `aligned_slide`
diagnostics, preserved first/second blocker identity, and unchanged walking
physics. A UT negative control selecting `Startup` fails with the explicit
`fixture state does not define a state-local HitWall handler: Startup` reason;
it cannot pass by observing zero callbacks. Evidence is in
`qa/runs/2026-07-25/walking-hitwall-corner-v3-crossgame/`.

This qualifies the native/script fixture only. The `MinHitWall` dispatch
predicate still needs the separately bracketed retail boundary oracle before
any shared walking behavior correction can be promoted.

## Iteration 103: direct-actor timeout record reconciliation

The benchmark-only direct-actor `MoveToward` timeout experiment is recorded in
the authoritative execution plan as **experimental**, not as a promoted bot
behavior change. Its default-off configuration key,
`direct_actor_move_toward_timeout_enabled`, is part of run identity, manifests,
matrix provenance, the exact forced-replan partition, and
`Compare-BotBenchmarkRuns.py`.

The initial UT436 Deck16-II seed `104729` result is a single deterministic
activation only: enabled changed aggregate K/D/S from `7/17/10` to `6/14/8`;
Unreal Gold 226b DeathFan seed `271828` had zero activations and identical
outcomes. Those results are insufficient to claim a survival or competence
improvement. Promotion requires a reproducible activating fixture, a paired
multi-seed matrix on Deck16-II and a second tuning map, the complete
combat/resource/hazard/wall/stall panel, and a cross-game non-zero opportunity
set or an explicit UT-only scope decision. The experiment remains disabled
outside benchmark runs until that evidence exists.

Repository-local `botbench-output/` and BotBenchmark Python bytecode are
ignored so they cannot be mistaken for admissible central-QA evidence or be
staged accidentally.

## Iteration 104: bilateral retail `MinHitWall` adjacent-boundary result

Fresh pinned retail calibration closes the adjacent-threshold observation, but
does not authorize a shared runtime predicate change. Every threshold had two
matching repetitions, retained its own pinned start, direction, blocker, and
logged contact signature, and each retail batch's installed inventory was
byte-identical before and after execution.

- UT436 `DM-Deck16][`:
  `retail-minhitwall-ut436-pinned-micro-v8-boundary/` suppresses at
  `-395000` and dispatches at `-394000` and `-393000`.
- Unreal Gold 226b `DmMorbias`:
  `retail-minhitwall-unreal226b-pinned-micro-v2-adjacent/` suppresses at
  `-397000` and dispatches at `-396000` and `-395000`.

Each game independently contradicts the current observation-only comparison:
the logged Bump/trace normal and pre-handler velocity imply a dot near
`-0.397676`, which is already less than a suppressing `-0.395000` threshold
in UT436 and a suppressing `-0.397000` threshold in Unreal Gold. The two
retail brackets also differ by 2,000 microthreshold units (`0.002`), with
UT436 suppressing at `-0.395000` where Unreal Gold dispatches. The samples do
not establish that this difference belongs to the adapters: the maps, pawn
speeds, floor geometry, and reconstructed center normals differ too. Instead,
they prove that the logged Bump/trace witness is not the exact native operand
of the retail callback decision. The current C++ comparison must therefore
not be promoted as a reconstruction of retail semantics, and no shared
`MinHitWall` behavior correction is enabled.

The next permitted fidelity slice is finer bisection on each identical pinned
contact followed by geometry-controlled cross-runs, using the existing trace,
center-normal, and velocity witnesses to identify or bound the instantaneous
physics operand. If that operand cannot be observed, the result remains an
empirical bracket rather than a `<` versus `<=` rule. Any later correction
must still be notification-only and satisfy the pure fixtures, runtime
collision fixture, and paired cross-game match matrix specified above.

The first finer half-step batches retained their inventory guards and narrowed
the intervals to UT436 `[-395000, -394500]` and Unreal Gold
`[-396500, -396000]`, where each interval is `[highest suppressed, lowest
dispatched]`. Further quarter-step batches did not run: both disposable retail
compilers independently faulted during UCC driver-cache startup before package
compilation or a server case. Those failed attempts are not oracle evidence;
the already-completed brackets remain valid and the runtime correction stays
blocked.

The retail runner now bounds its compile phase, requires the compiler's clean
success record, and writes `compile-attempt-1.json` with process outcome,
stdout/stderr hashes, and isolated INI hashes before admitting any server
case. It makes no retry attempt: a timed out, faulted, or incomplete compile
is fail-closed and cannot be confused with oracle evidence. A UT436 smoke
compile and head-on case passed that guard with an unchanged retail inventory
at `qa/runs/2026-07-25/retail-minhitwall-ut436-compile-guard-v1/`.

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
The current branch is **not ready to merge**; retain only independently proven
helpers/corrections and keep rejected live wall-jump and inventory enforcement
out of the release candidate.

## Iteration 105: read-only target-selection coverage

The benchmark now has an opt-in `--botbench-target-selection-observer=0|1`
observer for reflected `SetEnemy(Pawn NewEnemy) -> bool` dispatch. It discovers
the global and state-local implementations in every controlled bot class
hierarchy, validates every reflected signature before registering a VM hook,
and never replaces arguments, overrides returns, writes `Enemy`, or makes a
movement decision. Unsupported declarations disable the observer with a stable
contract reason instead of changing the match.

When active, the telemetry emits bounded, one-shot target-selection records
and exact per-bot counters for outermost/nested calls, accepted target changes,
accepted same-target calls, rejected-or-unchanged calls, missing results,
identifier rejects, capacities, and integrity failures. The analyzer requires
the observer state to remain constant, record sequences to be contiguous,
records to reconcile exactly with outcome counters, and active evidence to
have zero missing-result, overflow, or integrity-failure counters.

Fresh observer runs passed this strict validation:

- UT436 `DM-Deck16][`, seed `104729`, difficulty 7, four bots:
  `qa/runs/2026-07-25/target-selection-observer-ut436-deck16-s104729-v2/`.
  It includes accepted target changes, accepted same-target calls,
  rejected-or-unchanged calls, and nested state/global dispatches.
- Unreal Gold 226b `DmDeathFan`, seed `271828`, native difficulty 3, four
  bots: `qa/runs/2026-07-25/target-selection-observer-unreal226b-dmdeathfan-s271828-v2/`.
  It likewise has positive accepted/rejected/nested coverage with no observer
  integrity failure.

`DmDeck16` on Unreal Gold had a healthy active observer but zero `SetEnemy`
dispatches for this deterministic setup, so it is recorded as zero coverage,
not as target-selection evidence. This tranche improves measurement only; it
does not promote a target-selection behavior change or make the branch
merge-ready.

## Iteration 106: direct-actor timeout reachability scan

The experimental direct-actor `MoveToward` timeout remains too sparsely
reachable for a behavior promotion. Its known UT436 Deck16-II seed `104729`
activation occurs only at telemetry tick 5,363; the candidate retains the
earlier mixed K/D/S result of `6/14/8` versus stock `7/17/10`. Three new
full-duration candidate-only UT436 scouts at seed `424242` had no activation:
Deck16-II (`9/15/6` K/D/S), Morpheus (`2/12/10`), and Turbine (`6/7/1`).
Their structurally valid aggregate report is
`qa/reports/bot-ai/direct-actor-timeout-v4-scout.json`.

The current Deck16-II stock seed `104729` terminal state also shows why a
single latent timeout is not the required fix: 8 of 17 deaths are classified
as unassisted environmental deaths and 8 as hazard-exposed deaths, distributed
across all four bots. The timeout reaches only one direct-actor stall in the
known candidate. Do not widen its eligibility or promote it from these results.
The next candidate must instead be derived from a causally attributable
hazard/route witness with enough activation coverage for paired UT and Unreal
qualification.

## Iteration 107: post-resolution inventory direct-reach observer

The previously rejected early `ActorReachable` support probe is now replaced
with a separately gated, read-only observer. It is enabled only by
`--botbench-inventory-direct-reach-support-observer=1`, is part of the run
identity, manifests, summaries, matrix provenance, strict analyzer, and
comparison configuration, and performs no write to the reachability result,
location, route, target, acceleration, latent state, or physics.

The observer is deliberately narrower than a behavior change: it observes a
stock autonomous walking bot's direct ammo-inventory reach only after the
native dry run has resolved a wall slide, returned to walking height, and
already concluded the target is reachable. It dry-probes the existing
`stepDownDelta` from that final simulated position and records a bounded,
per-pawn exact outcome: safe supported, safe unsupported with no observed
hazard, harmful foot zone, unsupported over a harmful zone, or unavailable.
The analyzer requires the active status on every event, all seven counters,
contiguous records, exact outcome reconciliation, and no record overflow.

Fresh 7,200-tick anchor runs passed the structural analyzer with no overflow:

- UT436 `DM-Deck16][`, seed `104729`, difficulty 7:
  `qa/runs/2026-07-25/inventory-direct-reach-support-observer-v1/ut436-deck16-s104729-r2-t7200/`.
  It recorded three post-slide eligible calls, all unsupported but with no
  observed harmful zone below.
- Unreal Gold 226b `DmDeathFan`, seed `271828`, difficulty 3:
  `qa/runs/2026-07-25/inventory-direct-reach-support-observer-v1/unreal226b-dmdeathfan-s271828-r3-t7200/`.
  It recorded four calls with the same safe-unsupported/no-observed-hazard
  outcome.

There are zero harmful-foot and unsupported-over-harmful witnesses in both
anchors. This qualifies the observer implementation and rejects any
`ActorReachable` return-value change from this evidence. The next direct-reach
investigation must find a causal unsafe witness outside this sparse ammo-only
tranche before widening eligibility or changing stock navigation.

Observer-on and observer-off 7,200-tick runs are behaviorally identical after
removing only `config_id`, the observer envelope, and the declared
`inventory_direct_reach_support_*` counters/diagnostics. The canonical
gameplay-stream SHA-256 is
`6d0c0ea4358bce2c056f790d4e723932ae19faa41aba5b58ff676fc5de6e8f10` for
UT436 and
`4f8dd2465a5768f7ce7988932a66cf07764c6aa61e6fd7855b0dc0b2038823ca` for
Unreal Gold. The observer-off controls are the sibling
`*-baseline-t7200/` directories in the same QA tranche.

## Iteration 108: route-cache context correlation boundary

The static map catalog, realized capability witness, and existing
`route-execution.jsonl` can now be joined by the fail-closed offline
`Analyze-RouteExecutionContext.py` utility. It validates catalog/manifest map
identity, benchmark configuration identity, complete roster identity, one
route record per tick telemetry record, contiguous route sequence, navigation
node class/name references, and directed reachspec endpoints. Its output names
the result an **observed route-cache first hop** deliberately: it does not
mistake a post-tick cache sample for the native path search's selected
reachspec.

The fresh UT436 Deck16-II and Unreal Gold DeathFan anchor reports resolve
10,783 and 5,170 active first-hop samples respectively. Death ticks are not
an activating route-edge witness: the UT death and all 25 Unreal DeathFan
death-counter increments were outside active `MoveToward` first-hop context at
the terminal tick. No route policy is promoted from that correlation.

The next permitted implementation is a default-off, behavior-neutral native
path-commit provenance observer. It must preserve the actual selected
reachspec index while `FindPathToEndPoint` unwinds the selected endpoint,
record only the bounded prefix committed by `SetRouteCache` before
`SpecialHandling`, distinguish cache clears from successful commits, and fail
closed on any overflow or invalid edge reference. It must first demonstrate
positive, repeatable coverage in UT436 and Unreal Gold; it cannot claim to
cover direct movement, arbitrary script route writes, or later script target
redirection.

## Iteration 109: native path-commit provenance observer

The native observer is now captured at the correct boundary: after the shared
`SetRouteCache` write and before `SpecialHandling` can mutate the script goal.
`FindPathToEndPoint` retains the exact reachspec index for each unwound edge,
and the bounded committed route prefix serializes its nodes, exact raw
reachspec fields, raw/adjusted endpoint cost, and failed-navigation penalty
applications. A cache-clear is a distinct record, while an invalid reference
or bounded-queue overflow is fail-closed. `Analyze-NativePathCommits.py`
rebinds every record to the immutable map catalog by index and rejects pruned,
altered, missing, or out-of-order evidence.

Fresh 7,200-tick UT436 and Unreal Gold controls both pass the analyzer with
positive committed paths and zero overflow. Their complete authoritative
gameplay streams exactly match the pre-observer controls, proving this
observer does not change measured bot behavior. The death hook drains records
into a participant-owned pending queue so terminal-life commits are retained
even while the next route sample is unavailable; a fresh 25-death DeathFan
run passes that strict path. This qualifies only observer plumbing; repeated
qualification and a causal hazard/route witness are still required before it
can support a behavior candidate.

## Iteration 110: native commit versus active direct-command boundary

Native provenance now streams a death and hazard-entry context. The repeated
UT Deck16-II `PathNode121 -> PathNode123` lead is not a route-selection
authorization: by the harmful entry the bot has switched to direct `BulletBox4`
movement and the route cache merely retains the earlier path. DeathFan likewise
has no short-latency native-route pattern across its 31 hazard entries. Reject
a reachspec veto or cache-clear policy. The next permitted direct-reach work
is an observer-only pickup/actor provenance witness that ties a successful
native reachability result to the subsequently active direct command; it must
remain behavior-neutral and fail closed on any missing life or command link.

## Iteration 111: independent source audit and stock-baseline restoration

An independent read-only source audit confirms that the native path-commit
observer is captured after `SetRouteCache`, before `SpecialHandling`, and its
offline catalog binding is a valid integrity check. It also confirms the
negative causal conclusion: the observed Deck16-II route cache is historical
when direct `BulletBox4` movement begins, and DeathFan has no short-latency
route-commit pattern. A reachspec veto, cache clear, route pin, or
`ActorReachable` return-value change remains rejected.

The audit found a separate merge blocker: several earlier BOT AI recovery
paths write pawn state for ordinary autonomous player bots without a declared,
default-off benchmark experiment flag. This means the current branch is not
yet an observation-only stock baseline, irrespective of the soundness of the
new provenance observer. The first corrective slice is therefore to either
remove those writes or place each behind an explicit default-off flag that is
part of run identity, manifests, matrix provenance, and comparison rejection.
The affected paths are the `PickWallAdjust` prepend, move-stall navigation
replan, pain-ledge recovery, and wall-adjust recovery. Mutating helpers must
not retain an `Observe*` name.

Before expanding direct-reach provenance, also give native path-commit capture
its own default-off switch instead of inheriting the general walking-preflight
benchmark mode. Re-qualify it with observer-on/off byte-equivalence on both
anchors, ignoring only its declared telemetry envelope. Extend the path-commit
analyzer tests to exercise death context, hazard rising edge, respawn sequence,
overflow, cache clear, and a base-rate/null comparison. Only after the stock
baseline is restored may the next permitted observer tie a successful native
reachability result to a same-life, same-target active direct command. Missing
life or command linkage is a fail-closed unavailable result, never inference.

The immediate baseline restoration is complete. `PickWallAdjust`, wall-adjust
steering, and the move-stall navigation replan now require the existing
default-off `failed_navigation_avoidance_enabled` experiment; pain-ledge
steering requires the existing default-off `harmful_zone_escape_enabled`
experiment. The move-stall selector has an explicit navigation-replan enable
input and a focused unit assertion that a detected navigation move remains
inert until it is enabled. Fresh 120-tick UT436 Deck16-II (skill 7) and Unreal
Gold DeathFan (skill 3) smokes completed and passed structural analysis with
all of those flags false at
`qa/runs/2026-07-25/stock-baseline-restoration-v1/` and
`qa/reports/bot-ai/stock-baseline-restoration-v1-smoke.json`. This verifies
launch and telemetry structure only; full observer-on/off qualification remains
required before the branch can be called merge-ready.

## Iteration 112: independently gated native path-commit observer

Native path-commit provenance no longer inherits the broad walking-preflight
benchmark mode. It now requires the explicit default-off
`--botbench-native-path-commit-observer=0|1` flag, which is bound into engine
state, config identity, manifests, summaries, event telemetry, structural
comparison protection, and the path-commit analyzer. The analyzer rejects a
run unless that manifest flag is explicitly true, so an empty path-commit
stream from a stock run cannot be interpreted as observer evidence.

Fresh 120-tick paired controls on UT436 Deck16-II (skill 7, seed `104729`) and
Unreal Gold DeathFan (skill 3, seed `271828`) pass the quality analyzer; their
enabled runs also pass strict map-catalog provenance analysis with positive
commits. After removing only `config_id` and the declared
`native_path_commit_observer` event envelope, the full event streams match:

- UT436: `99192C2DB6046C8B9ADAACE94A794B4C572790BF99CCC443AA0188895EB8E44E`
- Unreal Gold: `C89070BFE2B6C568FFE5CA59F76F6DE563635DC41FB73456464DF34CA040E256`

The runs and reports are under
`qa/runs/2026-07-25/native-path-commit-observer-v2/` and
`qa/reports/bot-ai/native-path-commit-observer-v2-*.json`. This is a smoke
qualification only; retain full-duration two-repetition anchors before using
the observer to choose the next direct-command provenance slice.

The full-duration qualification is complete. Two 7,200-tick observer-on/off
repetitions per anchor pass structural quality analysis and all four enabled
runs pass strict map-catalog path-commit analysis. Canonical event streams are
byte-identical after excluding only `config_id` and the declared
`native_path_commit_observer` envelope:

- UT436 Deck16-II, both repetitions:
  `E40CC9D6A1818CC515ABA303F188C0E88BC0CB4FCF9B6DBCCF6791AC6696969F`
- Unreal Gold DeathFan, both repetitions:
  `802B9935A307984F6F3446F0AC104D66789965C53BD43937B6819A5C2A0F047D`

The full aggregate is
`qa/reports/bot-ai/native-path-commit-observer-v2-full.json`; per-run strict
provenance reports are `native-path-commit-observer-v2-*-r[12].json`. Native
path commits are therefore qualified as observer-only evidence. They still do
not establish a causal direct-movement defect; proceed only with the separate
same-life pickup/actor reachability-to-command witness.

## Iteration 113: direct-reach command provenance observer

The separate default-off `--botbench-direct-reach-command-observer=0|1`
observer now captures eligible native walking `ActorReachable` simulation
outcomes and links a successful result only when the next active stock command
is the same live actor slot, in the same move-stall life, with `MoveTo` or
`MoveToward` and an empty route-cache head. Every other outcome is explicit:
not reached, life boundary, no active direct command, retained route head, or
replaced target. The dedicated analyzer rejects an inactive manifest/envelope,
counter or record discontinuity, partition mismatch, and any queue overflow;
coverage requires at least one exact same-life link.

Two full 7,200-tick observer-on/off repetitions per anchor are complete and
their normalized gameplay streams are identical after excluding only
`config_id`, the declared direct-reach observer envelope, and its declared
per-bot counters/records:

- UT436 Deck16-II (seed `104729`, skill 7):
  `E40CC9D6A1818CC515ABA303F188C0E88BC0CB4FCF9B6DBCCF6791AC6696969F`.
  Each enabled repetition reports 55 observations, 54 successful simulations,
  12 exact same-life links, 43 explicit unlinked records, and zero overflow.
- Unreal Gold DeathFan (seed `271828`, skill 3):
  `802B9935A307984F6F3446F0AC104D66789965C53BD43937B6819A5C2A0F047D`.
  Each enabled repetition reports 85 observations, 83 successful simulations,
  22 exact same-life links, 63 explicit unlinked records, and zero overflow.

The exact linked targets are ordinary pickup and weapon actors; this run does
not establish that any direct command caused a harmful-zone entry or terminal
death. It therefore qualifies the provenance plumbing but authorizes no
`ActorReachable` return-value change. The next discovery slice must attach an
exact terminal disposition to a same-life direct command (hazard clear, harmful
entry/death, life boundary, target change, or run-end censor) before considering
any behavior gate.

## Iteration 114: bounded direct-command terminal correlation

The direct-command terminal observer now closes a same-life activation as soon
as stock replaces its exact active command: a different/deleted target, a
non-direct latent action, or a route-cache head is an explicit
`command_replaced` terminal. This prevents a pickup check early in a long life
from being mislabeled as the cause of a later hazard death. The analyzer
requires one terminal for every exact activation and validates its complete
partition, terminal ordering, exact-hazard proof, sequence continuity, and
zero overflow.

On the new full Unreal Gold DeathFan anchor (seed `271828`, skill 3), all 22
same-life activations resolve as 21 `command_replaced` and one run-end censor;
none remains active through a hazardous death. UT436 Deck16-II's 120-tick
smoke records five exact activations, all `command_replaced`, with zero
overflow. The prior broad 15-death DeathFan correlation was therefore a
life-long association, not command ownership. Reject an `ActorReachable`
behavior change for this failure class.

The next independent locomotion hypothesis is shared walking `HitWall`
notification fidelity. It remains observer-only: retail microthreshold
brackets prove the current logged normal/velocity is not the native
dispatch-time operand, so replacing the legacy vertical-wall predicate with
the reconstructed `MinHitWall` predicate would be unsafe. First repair the
controlled retail-oracle operand witness and the cross-game corner fixture;
only then may a default-off notification-only experiment be considered.
