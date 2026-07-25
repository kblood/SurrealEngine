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
