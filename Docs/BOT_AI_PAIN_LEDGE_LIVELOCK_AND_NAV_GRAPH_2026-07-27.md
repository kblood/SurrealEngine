# Pain-ledge livelock and the DM-Deck16][ navigation graph

Measurements taken on the corrected-vision-cone default binary (`2d97f860`
onward). All bot benchmark figures are 7,200 ticks, 16 skill-3 bots, fixed
delta 0.016666667, `DM-Deck16][?Game=Botpack.DeathMatchPlus`, one run per cell.
Static graph figures come from `--headless-driver=map-catalog`.

## 1. The pain-ledge veto pins two to three bots per match

`UActor.cpp:1607` vetoes a walking step whose predicted fall enters a harmful
pain zone. It backtracks the move, zeroes velocity and acceleration, sets
`MoveTimer` to -1, and records the veto. The recovery that would steer the pawn
away, `ApplyPainLedgeRecovery` at `UActor.cpp:7727`, is reachable only under
`IsBotBenchmarkHarmfulZoneEscapeEnabled()`, which is default-off. The pawn
therefore re-requests the same direction and is vetoed again.

| seed | vetoes | repeat vetoes | repeat share | recovery attempts |
| --- | ---: | ---: | ---: | ---: |
| 104729 | 2955 | 2899 | 98.1% | 0 |
| 271828 | 1323 | 1276 | 96.4% | 0 |
| 314159 | 3354 | 3271 | 97.5% | 0 |

A repeat is not an artifact of replanning. `PawnPainLedgeRecovery.cpp:50`
requires the new veto to fall within `painLedgeRepeatRadius` of 96 units of the
previous one, to align at least `painLedgeDirectionAlignment` of 0.5 with the
previous unsafe direction, and to arrive inside the 0.5 second window. It means
the pawn is pushing the same way at the same place.

The vetoes concentrate rather than spread. The two worst bots absorb 48% to 71%
of a run's vetoes and the five worst absorb 77% to 99%. Those bots stop playing:

| seed | bot | vetoes | kills | HitWall |
| --- | --- | ---: | ---: | ---: |
| 104729 | Jared | 1480 | 2 | 72 |
| 104729 | Rhea | 621 | 0 | 68 |
| 314159 | Kira | 1132 | 0 | 59 |
| 314159 | Necroth | 877 | 1 | 36 |

The low HitWall counts are the diagnostic. A bot grinding along geometry records
300 to 400 HitWall events; these are pinned in place instead.

`ShouldVetoPainZoneLedge` is gated on `aiPlayerBot`, so it applies to bots only
and has no UE1 counterpart. This is a SurrealEngine addition whose recovery half
ships disabled, which is worse than shipping either both halves or neither.

## 2. Why the move-stall watchdog never sees it

`RecordFailedNavigation`, which teaches a pawn to stop selecting a target it
cannot reach, has exactly one feeder: the move-stall watchdog at
`UActor.cpp:7844`. That watchdog resets its episode whenever movement intent
drops (`PawnMoveStallWatchdog.cpp:187-195`). The veto sets `MoveTimer` to -1,
which ends the latent move and drops movement intent, so every veto erases the
evidence the watchdog was accumulating.

The counters show the consequence directly: 18 to 20 move-stall detections per
run against 1,323 to 3,354 pain-ledge vetoes. The veto is structurally invisible
to the only mechanism that could learn from it.

## 3. Enabling the existing recovery removes the livelock without improving play

Paired runs, `--botbench-harmful-zone-escape=0` against `=1`, same binary, three
seeds. The escape-arm runs reproduced the escape-off veto counts exactly, so the
arms are comparable.

The flag also gates a separate harmful-zone-escape subsystem at
`UActor.cpp:7585` and `7623`, which would normally confound the comparison. It
does not here: `harmful_zone_escape_episodes`, `recovery_attempts`,
`successful_escapes`, `forced_replans`, `no_safe_candidates`, and both
`hazard_residence` counters read zero in every run of both arms. On this map the
flag's only observable effect is `ApplyPainLedgeRecovery`.

Summed over the three seeds:

| signal | escape off | escape on | delta |
| --- | ---: | ---: | ---: |
| pain ledge vetoes | 7632 | 600 | -7032 |
| pain ledge repeat vetoes | 7446 | 0 | -7446 |
| pain ledge recovery attempts | 0 | 511 | +511 |
| pain ledge recovery escapes | 0 | 403 | +403 |
| kills | 116 | 118 | +2 |
| deaths | 155 | 165 | +10 |
| suicides | 39 | 47 | +8 |
| environmental deaths | 36 | 47 | +11 |
| damage dealt | 16433 | 17462 | +1029 |
| HitWall events | 21457 | 12454 | -9003 |
| move stall detections | 56 | 45 | -11 |

Only three outcome signals hold their sign across all three seeds: deaths,
suicides, and environmental deaths, all up. Kills (-1, +4, -1), damage (+182,
+1529, -682) and HitWall (+449, -3814, -5638) each flip sign, so with one run
per cell they are not usable. The livelock is genuinely removed and the
scoreboard does not reliably improve.

## 3a. The veto is roughly 94% false positive

Distinct vetoes across the three seeds are 7632 - 7446 = 186. If every one had
been a genuine imminent hazard entry, removing the block should have cost up to
186 additional environmental deaths. The measured cost was 11. That puts the
share of vetoes protecting against something real at roughly 6%.

This is an inference from the aggregate, not a per-edge verification. The
recovery steers around the drop rather than completing it, so an escape does not
prove the original step was survivable.

## 4. The DM-Deck16][ navigation graph

251 navigation points, 1,997 ReachSpecs. 1,051 specs (53%) carry `bPruned` and
are skipped at all four expansion sites; 946 remain, of which 925 are referenced
by some node's `paths[]`. The other 21 are orphans.

Edges are directed. Each node holds `paths[]`, `upstream_paths[]` and
`pruned_paths[]` as index lists; the observed maxima are 11, 9 and 13 against
UE1's fixed 16-slot arrays, so this map does not approach the format cap. 776 of
946 live edges (82%) have a reverse counterpart, leaving 170 one-way edges. No
node sets `bOneWayPath`; the asymmetry is in the edge set.

Connectivity is not complete:

```
reachable FROM a PlayerStart : 235/251   (16 unreachable)
can REACH a PlayerStart      : 251/251   (0 sinks)
strongly connected components: 12
  sizes: 220, 12, 7, 2, 2, 2, 1, 1, 1, 1, 1, 1
```

The disconnection is one-directional. Every node can walk out to a spawn; 16
cannot be walked into. Six have no incoming live edge at all and can never be
selected as a route destination: `InventorySpot142`, `InventorySpot170`,
`InventorySpot176`, `PathNode39`, `PlayerStart5`, `Teleporter1`.

The 16 unreachable nodes cluster around y 1379 to 1993 and include **both**
Teleporters and seven InventorySpots. Teleport transitions are not ReachSpecs,
so the region can be walked out of but entered only by teleporting, which route
search cannot plan. Whether retail builds specs through teleporters and
SurrealEngine drops them on load is not yet established; if it does, this is a
routing defect affecting every map with a teleporter rather than a Deck16 quirk.

Thirteen nodes have a single outgoing edge, which makes them one veto away from
being dead ends.

## 5. Capability mask restriction, measured

Reach flag values present: walk 1528, walk|jump 448, special 21. No spec has
zero flags and none carries a bit outside the known mask.

| mask | live edges kept | nodes reachable from spawns |
| --- | ---: | ---: |
| 125 (current) | 946 | 235 |
| 61 (drop PlayerOnly) | 946 | 235 |
| 117 (drop Jump) | 752 | 220 |

Mask 61 is identical to 125 in both columns. Zero of 251 nodes set
`bPlayerOnly` and zero specs carry the PlayerOnly flag, so removing that bit
cannot change routing on this map.

Mask 117 removes 194 edges and costs 15 nodes, of which 6 are InventorySpots
clustered at x 827 to 1036, y -257, z -975 to -1038. `ut_shieldbelt0` sits at
(895, -257, -1021) and its pickup node `InventorySpot147` at (895, -257, -975)
is in that set. The shield belt is reachable only over Jump edges, so denying
bots the Jump capability removes the map's most contested powerup from the AI
while leaving it available to human players.

## 6. The trap geometry

Stuck positions of the pinned bots cluster tightly. The densest cell,
(768, -1280, -640) with 1,592 near-zero-displacement ticks, sits 56 to 122 units
from `PathNode68`, `LiftExit5`, `InventorySpot152` and `JumpSpot0`, all at
z of about -590. `InventorySpot152` was the top stall target for that bot.

Every jump edge leaving that cluster is a downward drop:

| property | value |
| --- | --- |
| edges | 26 |
| dz range | -128 to -141 |
| distance range | 213 to 993 |

These are walk-off-a-ledge transitions, which is exactly what
`PredictedFallEntersHarmfulPainZone` responds to. Three SlimeZones are present
with `pain` true and `DamagePerSec` 40. No navigation point resolves inside a
pain zone, so the graph never routes a bot into slime directly; the exposure is
in the fall.

Detour analysis on the live graph, using the engine's own cost rule
(`AccumulateCost`: accumulated + edge distance + node cost, where node cost is
`ExtraCost` or the `SpecialCost` script result):

| edge set | count | unreachable if avoided | detour excess |
| --- | ---: | ---: | --- |
| the 26 cluster drops | 26 | 0 | max +648 |
| all downward jumps, map-wide | 92 | 32 (35%) | median +1277, max +7011 |

A targeted penalty on the hazard-adjacent drops reroutes every one of them at
modest cost, well inside the existing `failedNavigationFirstHopCost` of 4096. A
blanket policy against downward jumps strands a third of its destinations.

Note that `SelectFailedNavigationEndpoint` at `UActor.cpp:5611` penalises
candidate **endpoints**, not edges. Feeding the failure memory therefore makes a
pawn abandon the goal rather than approach it by a safer route. The detour table
above describes what an edge-level penalty would achieve, which is not what the
current mechanism implements.

## 7. Cost model

```
total = sum(edge.distance) + sum(node.cost)
node.cost = SpecialCost()  if bSpecialCost
          = ExtraCost      otherwise
```

Distance is the only geometric term. There is no weighting for vertical drop,
hazard proximity, or execution reliability, so a 213 unit drop past slime and
213 units of flat corridor cost the same. The only authored risk dial is
`ExtraCost`, and it is used: all three JumpSpots carry 400. The 26 dangerous
drop edges hang off ordinary PathNodes carrying 0.

`FindPathToEndPoint` expands in FIFO insertion order with relaxation rather than
by a cost-ordered priority queue, re-pushing a node when a cheaper route is
found. It converges within the `maxNodes` budget of 1000 against 251 nodes, but
it is not Dijkstra and nodes are not expanded in cost order.

## 8. Static analysis is far cheaper than simulation

The map catalog is 716 KB and extracts in seconds. A benchmark run is 1.7 GB and
takes minutes. Every figure in sections 4 through 7 came from the catalog with
no engine run. Connectivity, reachability, mask comparison and detour cost are
all answerable statically; hazard geometry is not, because zones are BSP volumes
and the catalog records their properties rather than their bounds.

## 9. Bounding the veto by repeat count removes the livelock and costs kills

Section 3a inferred that roughly 94% of vetoes were false positives, from the
aggregate that unblocking 186 distinct vetoes produced only 11 extra
environmental deaths. That inference does not survive a direct test.

`SURREAL_PAIN_LEDGE_VETO_REPEAT_CAP=4` fails the veto open once the same veto
has repeated four times at the same origin and direction, then clears the
memory so the count restarts. Six runs, three seeds, DM-Deck16][, 16 skill-3
bots, 7,200 ticks at a fixed 0.016666667 delta.

The cap-off arm reproduces the pre-existing baseline exactly on all three seeds
(2955/2899, 1323/1276, 3354/3271), confirming the change is inert when the
variable is unset.

| Metric (sum of 3 seeds) | cap-off | cap 4 | Delta |
| --- | --- | --- | --- |
| Pain-ledge vetoes | 7632 | 299 | -96% |
| Repeat vetoes | 7446 | 178 | -98% |
| Move-stall detections | 56 | 34 | -22 |
| Kills | 116 | 90 | -26 |
| Damage dealt | 16433 | 13218 | -3215 |
| Environmental deaths | 36 | 60 | +24 |
| Suicides | 39 | 60 | +21 |
| Deaths | 155 | 150 | -5 |

Per seed, kills go 47->36, 33->29, 36->25 and environmental deaths go 9->17,
17->21, 10->22. Kills, damage dealt and environmental deaths each move the same
direction on all three seeds, which is the sign stability the section 3 escape
arm never reached. Total deaths are flat, so this is not bots dying more; it is
bots dying in slime instead of to enemies, and scoring 22% less while doing it.

The livelock is real and the cap removes it, but walking the pawn through the
ledge is worse than pinning it there. The veto is doing real work. What the
default build lacks is the recovery path that steers around the ledge, not a
bypass that steps off it. The section 3 escape arm, which steers, held kills
flat while adding 11 environmental deaths; this arm, which does not steer, gives
up 26 kills for 24. Between the two available shapes of fix, steering is
strictly better, and neither yet earns promotion to default.

Note that the fail-open counter added alongside this gate is never read by the
benchmark driver, so the mechanism evidence here is the veto collapse rather
than a direct count.

## 10. How bots actually die, and where they enter the slime

Every environmental death in the six section 9 runs reports
`environmental_source = pain_timer`; there are no other environmental sources.
The physics mode at death is dominated by `Swimming`, not `Falling`: 29 against
10 with the cap off, 47 against 13 with it on. Bots are not dying from the fall.
They are dying in the slime afterwards, and at 40 damage per second a full-health
bot has about 2.5 seconds.

Reconstructing zone transitions from `route-execution.jsonl`, which records each
bot's zone, move target and route cache every tick, the three baseline seeds
contain 77 slime entries and 76 exits. Median residence is 56 ticks (0.93 s),
p90 is 139 ticks (2.3 s), the maximum is 163. The p90 sits just under the
survival budget. Against 36 environmental deaths, close to half of all entries
are fatal, taking respawn to be the recorded exit.

86% of entries are into `SlimeZone0`. The move target on the tick before entry:

| Move target | n | Route cache head | n |
| --- | --- | --- | --- |
| (none) | 24 | (empty) | 23 |
| `ut_shieldbelt0` | 18 | `InventorySpot147` | 14 |
| `PathNode149` | 5 | `PathNode110` -> `PathNode108` | 4 |
| `PathNode110` | 4 | `JumpSpot1` -> `LiftExit7` | 3 |
| `ut_jumpboots0` | 4 | `PathNode122` -> `PathNode123` | 3 |

The shieldbelt funnel identified in section 6 is the largest named attractor,
and 31% of entries happen with no move target and no route at all.

That last figure has a structural explanation. Navigation points carry a zone
through `resolved_zone_actor_index`, but all 251 nodes on DM-Deck16][ resolve to
`LevelInfo0` and none is inside a SlimeZone. `LevelReachSpec` has no zone field
at all. So the graph cannot express that an edge crosses a hazard, and the
interior of a hazard is off-graph: a pawn in the slime has no node at its
position to plan from. The only component that knows the slime exists is the
per-step veto, which has neither a map nor a memory.

## 11. Hazard swim egress improves scoring

`--botbench-hazard-swim-egress` and `--botbench-hazard-swim-egress-live` gate an
already-implemented egress behaviour that was off in every run above. Three arms,
same three seeds, same binary: control is the section 9 cap-off trio with both
flags off, observe sets the base flag only, live sets both.

The observe arm is identical to control on every behavioural counter, including
kills per seed and 7632 pain-ledge vetoes, so the observer is inert and the
comparison is clean.

| Metric (sum of 3 seeds) | control | observe | live |
| --- | --- | --- | --- |
| Kills | 116 | 116 | 131 |
| Damage dealt | 16433 | 16433 | 17707 |
| Environmental deaths | 36 | 36 | 43 |
| Deaths | 155 | 155 | 175 |
| Pain-ledge vetoes | 7632 | 7632 | 4115 |
| Move-stall detections | 56 | 56 | 66 |

Per seed, kills go 47->46, 33->40, 36->45 and environmental deaths go 9->13,
17->15, 10->15. Deaths balance against kills plus environmental deaths in both
arms, so the extra deaths are extra combat rather than unexplained attrition.

The live arm authorized 48 egress episodes and recorded 27 exits and 13 forced
replans, against 77 slime entries in the control. Authorization requires the
anchor to come from `FallingPreMove`, so it does not cover bots that walk or
swim in. It also halves the pain-ledge livelock as a side effect, 7632 to 4115,
without touching the veto.

On these three seeds this is the first candidate in the investigation to move
kills upward, but one seed is marginally negative and the environmental-death
deltas change sign. Section 12 extends the same comparison to eight seeds and
supersedes this reading.

## 12. At eight seeds the swim-egress gain disappears

Five further seeds were declared before any of their results were seen: 161803,
577215, 141421, 173205, 223606. Each was run as a control/live pair under the
same configuration as section 11.

| Seed | Kills | Damage | Env deaths | Pain-ledge vetoes |
| --- | --- | --- | --- | --- |
| 104729 | 47 -> 46 | 6393 -> 6258 | 9 -> 13 | 2955 -> 1592 |
| 271828 | 33 -> 40 | 4525 -> 5602 | 17 -> 15 | 1323 -> 1458 |
| 314159 | 36 -> 45 | 5515 -> 5847 | 10 -> 15 | 3354 -> 1065 |
| 161803 | 40 -> 33 | 5381 -> 5247 | 10 -> 13 | 1526 -> 2378 |
| 577215 | 39 -> 39 | 6287 -> 6287 | 19 -> 19 | 3551 -> 3551 |
| 141421 | 30 -> 38 | 4872 -> 5132 | 16 -> 13 | 928 -> 4217 |
| 173205 | 37 -> 39 | 5086 -> 5678 | 14 -> 8 | 2567 -> 1837 |
| 223606 | 36 -> 31 | 5147 -> 4923 | 17 -> 8 | 1308 -> 4651 |

| Metric | control | live | Delta | Seeds up/down/tie |
| --- | --- | --- | --- | --- |
| Kills | 298 | 311 | +13 (+4.4%) | 4/3/1 |
| Damage dealt | 43206 | 44974 | +1768 (+4.1%) | 4/3/1 |
| Environmental deaths | 112 | 104 | -8 (-7.1%) | 3/4/1 |
| Deaths | 417 | 419 | +2 (+0.5%) | 4/3/1 |
| Pain-ledge vetoes | 17512 | 20749 | +3237 (+18.5%) | 4/3/1 |

The kill deltas are -1, +7, +9, -7, 0, +8, +2, -5: mean +1.6 per seed against a
standard deviation of 6.0, so the standard error is 2.1 and the effect is well
inside noise. The three seeds in section 11 happened to be three of the four
positive ones. Nothing survives the extension: environmental deaths move the
opposite way to section 11, and the claim that egress halves the pain-ledge
livelock inverts outright, with vetoes 18.5% higher across eight seeds.

Seed 577215 is diagnostic. Its live run is identical to its control on every
counter, yet it authorized 15 egress episodes and recorded 8 exits. It issued
zero forced replans. Forced replans are the only output of this mechanism that
changes pawn behaviour, and there were 34 across the eight live runs. The
episode, exit and authorization counters measure observation, not action.

Hazard swim egress is therefore not qualified for promotion on DM-Deck16][. It
is not a regression either, unlike the section 9 repeat cap; it simply does not
do enough to measure. Three candidates have now been tested against this
map -- harmful-zone escape, the veto repeat cap, and swim egress -- and none
improves scoring. All three operate at the level of a single pawn reacting to a
hazard it discovers by walking into it. The finding in section 10 that no
navigation node lies inside a hazard, and that no ReachSpec records whether it
crosses one, remains the untested explanation for why per-pawn reactions cannot
recover the loss: the planner routes bots into the slime and only the pawn, too
late and without a map, ever objects.

## 13. Hazard-aware edge costs change routing without changing outcomes

`SURREAL_REACHSPEC_HAZARD_COST` annotates every ReachSpec with the hazard its
span passes over and charges that as extra edge length in `FindPathToEndPoint`.
It is a cost and never a filter, so unlike the mask restriction in section 5 it
cannot strand a node.

The first implementation sampled the straight line between the two navigation
points and flagged zero edges out of 1,997. Navigation points sit on walkable
floor and the slime is underneath it, so the segment never enters a pain zone.
Sampling downward from each point along the edge, stopping at the first solid
sample so an intervening floor does not count, flags 122 specs of which 56 are
live, about 6% of the 946 live edges.

The flagged set contains confirmed entry sites from section 10: `PathNode149`,
`PathNode110` -> `PathNode108`, `PathNode122`, `PlayerStart5` and `LiftExit6`.
It does not contain `InventorySpot147` or `ut_shieldbelt0`, the largest single
funnel, because that approach is a direct inventory reach rather than a graph
edge. No ReachSpec annotation can cover it.

Sixteen runs, eight seeds, control against hazard. All eight control runs on the
new binary reproduce the previous build exactly on kills, deaths, environmental
deaths, damage and pain-ledge vetoes, so the change is inert when disabled.

| Metric | control | hazard | Delta | up/down/tie | t |
| --- | --- | --- | --- | --- | --- |
| Kills | 298 | 296 | -2 (-0.7%) | 3/4/1 | -0.11 |
| Damage dealt | 43206 | 41576 | -1630 (-3.8%) | 5/3/0 | -0.53 |
| Environmental deaths | 112 | 107 | -5 (-4.5%) | 4/4/0 | -0.35 |
| Deaths | 417 | 411 | -6 (-1.4%) | 6/2/0 | -0.28 |
| Move-stall detections | 151 | 128 | -23 (-15.2%) | 3/5/0 | -1.14 |
| Pain-ledge vetoes | 17512 | 21205 | +3693 (+21.1%) | 4/4/0 | +0.83 |

Routing plainly changed: per-seed pain-ledge veto counts swing by up to 3,323 in
either direction. No outcome metric moves. Environmental deaths trend down 4.5%
at a 4/4 seed split and t of -0.35.

The size of the achievable effect explains part of this. Of the 77 slime entries
measured in section 10, 24 happened with no move target and 23 with an empty
route cache, so roughly a third involve no route decision that any edge cost
could influence. A further 18 targeted `ut_shieldbelt0` through the uncovered
direct-reach path. The share of entries this mechanism can reach at all is
around a third, and at the observed per-seed standard deviation of 5.0
environmental deaths the experiment would have resolved an effect several times
larger than the one measured.

## 14. Where this leaves bot quality

Four interventions have now been measured against DM-Deck16][ at eight seeds or
with sign-stable three-seed evidence:

| Candidate | Layer | Kills | Verdict |
| --- | --- | --- | --- |
| Harmful-zone escape | pawn reaction | flat | no effect |
| Pain-ledge veto repeat cap | pawn reaction | -22% | regression |
| Hazard swim egress | pawn reaction | +4.4%, 4/3/1 | noise |
| ReachSpec hazard cost | planner | -0.7%, 3/4/1 | no effect |

The first three share a failure mode and the fourth was built specifically to
avoid it, by moving the decision from the pawn to the planner. It did not help
either. Environmental deaths are 112 of 417 total deaths, so even eliminating
them entirely leaves most of the scoreboard untouched, and every arm that moved
environmental deaths substantially left kills where they were.

The reasonable conclusion is that hazard handling is not what limits bot
scoring on this map. The remaining documented and untested structural defect is
in section 4: 16 of 251 navigation points cannot be reached from any PlayerStart
even though all 251 can reach one, six have no incoming live edge at all, and
the unreachable set contains both Teleporters and seven InventorySpots. Bots
cannot route to those items at all. Whether retail builds ReachSpecs through
teleporters and SurrealEngine drops them on load is still unestablished, and if
it does the defect is not specific to this map.

## 15. Bots walk into the slime, which is why none of the fixes worked

Section 10 recorded that 24 of 77 slime entries happened with no move target and
23 with an empty route cache, which invited the reading that bots lose their
routes. Sampling `latent_action` across a control run does not support it. Of
890 live targetless bot states, 29.3% are `StrafeFacing`, 20.0% `TurnTo` and
6.5% `TurnToward`, so 56% is combat; a further 34% is `Sleep`, `FinishAnim` or
`WaitForLanding`; only 5.2% is `MoveTo` or `StrafeTo`. `MoveTarget` is cleared
by `MoveTo`, `StrafeTo` and `TurnTo`, so a null target means the pawn is not
running an actor-directed move, not that it has no goal. Dropping a navigation
target at 400 units to engage an enemy is correct bot behaviour.

Correlating the 77 slime entries against per-tick state gives the useful figure:

| Physics mode at entry | n | Latent action at entry | n |
| --- | ---: | --- | ---: |
| Walking | 44 | Sleep | 39 |
| None | 21 | MoveToward | 21 |
| Falling | 7 | Continue | 7 |
| Swimming | 5 | TurnTo / TurnToward | 4 |
| | | StrafeFacing | 2 |
| | | WaitForLanding / FinishAnim | 4 |

57% of entries are `Walking`. 9% are `Falling`. Only two of seventy-seven are
combat strafing. Bots walk into the slime on the level, and section 10 shows
they then die swimming in it, 47 of 60 deaths against 13 falling.

Every hazard mechanism measured in this document targets falling. The pain-ledge
veto predicts whether a walking step's *fall* enters a pain zone. The swim
egress live steer authorizes only when the anchor source is `FallingPreMove`.
The section 13 edge annotation probes *downward* from each edge for a hazard
underneath, because the straight segment between nodes never enters a pain zone.
All three address the entry mode responsible for roughly one entry in eleven.

That is a coherent explanation for four null results in a row, and it is a
prediction rather than a conclusion: it says a mechanism that addresses walking
entry at floor level should behave differently from the four that did not. It
has not been built or measured, and nothing here establishes that fixing it
would move kills, given that environmental deaths are 112 of 417 and every arm
that moved them left kills flat.

## 16. Retail has a hazard-avoidance mechanism and DM-Deck16][ does not use it

The retail UnrealScript is embedded in the shipped packages and the map catalog
driver already exports it: `--catalog-export-scripts=1` writes
`<output>/scripts/<Package>/Classes/*.uc` and refuses an output path inside the
game root. Botpack yields 505 script classes, Engine 88, UnrealI 146,
UnrealShare 360.

Reading it answers what six benchmark campaigns could not.

`Botpack.PainPath` is a NavigationPoint that exists precisely for this problem:

```
event int SpecialCost(Pawn Seeker)
{
	if ( Seeker.ReducedDamageType == DamageType )
		return 0;
	return 1000000;
}
```

A node cost of one million, against edge distances in the hundreds. UE1's answer
to routing bots around a hazard is not a nudge, it is a prohibition. SurrealEngine
supports it: `ClearPaths` calls `SpecialCost` through the VM for any node with
`bSpecialCost`, and three JumpSpots on this map exercise that path.

DM-Deck16][ contains no PainPath. Its 251 navigation points are 148 PathNode,
69 InventorySpot, 15 PlayerStart, 10 LiftExit, 3 JumpSpot, 2 LiftCenter,
2 Teleporter and 2 DefensePoint. Only the three JumpSpots set `bSpecialCost`,
and the only authored `ExtraCost` values are 400 on those and on the two
LiftCenters. The slime is unmarked.

Nor is there a general runtime escape. `Engine.Pawn.PainTimer` only applies
damage. `Bot.PainTimer` delegates to it. The single escape in the bot script is
an override inside `state TacticalMove` that calls `GotoState('Retreating')`,
and `Retreating` is a combat retreat that reasons about `Enemy` and `MoveTarget`,
not a hazard egress.

So retail bots have no slime avoidance on this map, by content and by design.
Bots walking into the Deck16 slime and dying there is faithful UT99 behaviour,
not a SurrealEngine defect. That explains four null results at once: sections 3,
9, 11 and 13 each tried to improve behaviour that already matches retail, on a
map whose designer left the hazard unmarked.

It also recontextualises section 1. The pain-ledge veto is a SurrealEngine
addition with no UE1 counterpart, inventing an avoidance retail does not have.
Section 9 showed that removing it costs 26 kills, so it is doing something
useful, but what it is doing is not what retail does.

The immediate consequence for section 13 is a number. That candidate charges a
flat 4096 for entering a hazardous edge, chosen to match
`failedNavigationFirstHopCost`. PainPath charges 1000000, roughly 244 times
more. Retail treats a pain path as forbidden unless the pawn is immune; the
candidate treats it as mildly discouraged. The gate already accepts the cost as
its value, so `SURREAL_REACHSPEC_HAZARD_COST=1000000` tests retail semantics
with no code change.

## 17. Bots jump into the slime, and the pain ledge veto never sees it

Section 16 concluded that Deck16 slime deaths were faithful retail behaviour because
the map carries no PainPath and `Engine.Pawn.PainTimer` has no escape logic. A retail
measurement qualifies that conclusion without overturning it.

UT GOTY patched to OldUnreal v469e was configured to match the benchmark: DM-Deck16][,
DeathMatchPlus, 16 bots, `ChallengeBotInfo.Difficulty` 3, five minute time limit. Bot
count is governed by `InitialBots` and `MinPlayers` with an `[Engine.GameInfo]
MaxPlayers` ceiling, not by the `NumBots` URL option, which is an internal counter.
`ChallengeBotInfo.Difficulty` is a 0 to 7 scale that encodes novice mode as well as
skill: `DeathMatchPlus.SpawnBot` subtracts 4 and clears `bNoviceMode` above 3, so 3
means novice mode at skill 3. `BotControlledMatch.cpp` sets the same property, so the
benchmark and this retail run resolve to the same skill and the same novice mode.

Environmental deaths are recoverable exactly from the scoreboard. Displayed frags are
`Score`, which is kills minus self and environmental deaths, and the deaths column is
the raw count, so `S = (deaths_total - frags_total) / 2`. The matched retail run gave
163 frags against 189 deaths, which is 13 environmental deaths over 16 bots and five
minutes, or about 0.16 per bot minute. The three control runs of section 13 give 36
environmental deaths over 16 bots and two minutes each, which is 0.375 per bot minute.

Retail bots do die in the Deck16 slime. This engine does so roughly 2.3 times as often.
An earlier reading of this section claimed retail produced zero environmental deaths;
that came from a 5 player match at the default `InitialBots` of 4, and did not survive
a correctly sized run. The divergence is real but moderate, and closing it is worth
roughly halving slime deaths rather than eliminating them.

Reconstructing the 49 hazard zone entries in those three runs from per tick telemetry,
by walking backwards from each entry to the last tick the pawn was not falling:

| How the pawn became airborne | Count | Share |
| --- | --- | --- |
| Jumped | 34 | 69% |
| Stepped off an edge | 14 | 29% |
| Indeterminate | 1 | 2% |

At the launch tick 47 of 49 pawns were Walking, and the most common latent action was
`MoveToward` at 28. Takeoff vertical velocity is 357.5 at both median and maximum, a
full power jump against a fixed `JumpZ`. Airborne duration is a median of 58 ticks, and
the jumps carry a median of 277 horizontal units while descending a median of only 17.
These are deliberate route jumps, not falls.

The decisive number concerns the pain ledge veto. Across those same three runs the veto
fires 7632 times, of which 7446 are repeat vetoes against the same proposal. Between the
launch tick and the hazard entry it fires as follows:

| Vetoes between launch and entry | Entries |
| --- | --- |
| 0 | 46 |
| 1 | 2 |
| -1 | 1 |

The veto is a pre move check on walking. Once a pawn commits to a jump it is airborne
and the veto has no say in where the arc ends. It therefore fires roughly 2544 times per
run in situations that do not kill anyone, and not at all in the 49 that do.

This explains the four null results together. The harmful zone escape and the hazard
swim egress act after entry, when the pawn is already taking damage. The repeat cap
loosens a walking check that was never consulted for these events. The ReachSpec hazard
cost of section 13 probes downward from each edge to find ledges with a hazard beneath
them, which prices falling risk; the pawns jump over those edges. No candidate built in
this investigation could observe a jump.

Three of the hazard observers crash the benchmark when enabled. With
`--botbench-movement-command-provenance-observer=0` a 600 tick run exits 0 in 5.6
seconds and writes a summary; with the same flag set to 1 it exits 3 after 1.6 seconds
and writes nothing to stdout or stderr. The command transition ledger observer and the
pre entry causal slice observer fail identically. The crash is inside the run rather than
at argument parsing. This is why every summary in this investigation reports those
observers as disabled, including the pre entry causal slice, which is the instrument
built for exactly this question. The analysis above reconstructs that slice from the
per tick telemetry instead.

## 18. Candidates reverted, and upstream commits worth taking

Every candidate this investigation produced has been removed and the tree returned to
upstream for the affected files.

| Candidate | Files | Measured result |
| --- | --- | --- |
| ReachSpec hazard cost | `PawnReachSpecHazard.{h,cpp}`, `PawnReachSpecHazardCostCandidate.h`, `Tests/PawnReachSpecHazardTests.cpp`, `ULevel.h`, `CMakeLists.txt` | no effect, and section 17 shows it priced falling risk while pawns jump |
| Pain ledge veto repeat cap | `PawnPainLedgeVetoRepeatCapCandidate.h`, `PawnPainLedgeRecovery.{h,cpp}` | kills down 22 percent, sign stable on three seeds |
| Walking hit wall conjunction | `PawnWalkingHitWallConjunctionCandidate.h`, `PawnWalkingHitWallDispatch.{h,cpp}` | previously rejected |

`PawnPainLedgeRecovery.{h,cpp}`, `PawnWalkingHitWallDispatch.{h,cpp}`, `ULevel.h` and
`CMakeLists.txt` are byte identical to HEAD again. Re-running seed 104729 after the
revert reproduces the pre-revert control exactly: 47 kills, 57 deaths, 9 environmental
deaths, 2955 pain ledge vetoes, 2899 repeat vetoes, 2493 hit wall events. The candidates
were default off and their removal is behaviourally inert.

Upstream is 24 commits ahead. Three are relevant:

`6986107b` fixes cylinder hit normals. Its own comment records a flat top reporting a
normal of z=0.69 against the 0.7071 walkable threshold, which flips a standing pawn
between walking and falling every frame. That matches the failure class in section 17,
where pawns are falling at hazard entry and a walking-only pre move veto never fires.
It touches only `TraceTest.cpp`, which this branch has not modified, so it applies
cleanly. It corrects actor cylinder traces rather than BSP world geometry, so it may not
reach the Deck16 pit edges at all.

`d8eb2151` makes `Object.Rand` max exclusive again. Every script level random draw,
including bot target selection and roaming, was off by one.

`12a7ade8` propagates globalconfig changes to sibling classes.

A full merge is not advisable yet. `ea8fad80` splits the UObject files and touches
`UActor.h`, `NActor.cpp`, `OverlapTest.cpp` and `Iterator.cpp`, all of which carry
uncommitted work on this branch. Cherry picking the individual fixes avoids that.

## 19. Retail instrumentation via a UT99 mutator, and the first matched A/B

A telemetry mutator was built for the retail v469e install at
`C:\Devstuff\QuestGames\UT99-469e` so retail bot behaviour could be measured
rather than inferred from scoreboard screenshots.

`BotTelemetry\Classes\BotTelemetryMutator.uc` samples every pawn at a fixed rate
and logs position, velocity, physics mode, health, zone flags for all three body
regions, `MoveTarget`, `RouteCache[0..1]`, state and orders. It also hooks
`MutatorTakeDamage`, `PreventDeath` and `ScoreKill`, so every damage pulse and
death is recorded with its exact `DamageType`. `BotTelemetryLog` extends
`StatLogFile` but writes through `FileLog()` and flushes once per sample instead
of once per line, which `StatLogFile.LogEventString()` would otherwise do.

The mutator's own death log and the scoreboard it dumps agree exactly: 34 deaths
against 32 frags, and `(34 - 32) / 2 = 1` environmental death, matching the single
logged `Corroded` death. The scoreboard arithmetic used in sections 16 and 17 is
therefore sound.

### Matched comparison

Retail: DM-Deck16][, 16 bots, Difficulty 3, 2.1 minutes, 35.8 bot-minutes.
SurrealEngine: two control seeds, 16 bots, 64.0 bot-minutes.

| Measure                          | Retail | SurrealEngine |
| -------------------------------- | ------ | ------------- |
| Jumps per bot-minute             | 1.76   | 6.50          |
| Jumps landing in a hazard zone   | 4.8%   | 6.2%          |
| Hazard episodes per bot-minute   | 0.11   | 0.64          |
| Residence, median                | 0.77s  | 7.15s         |
| Residence, min                   | 0.29s  | 1.90s         |
| Residence, max                   | 0.99s  | 14.78s        |
| Episodes ending in escape        | 75%    | 78%           |
| Pain pulse interval              | 1008ms | 917ms         |

Three findings follow.

**Residence distributions do not overlap.** The shortest of our 41 episodes
(1.90s) is longer than the longest of retail's (0.99s). No retail bot was ever in
the slime for a full second; no bot of ours ever got out in under two. A gap with
no overlap at all is a mechanism difference, not a tuning difference.

**Escape rates are equivalent.** 78% against 75%. Bots on both sides get out of
the slime at the same rate; ours simply take about nine times as long to do it.
The defect is in egress speed, not in the decision to leave or the ability to
leave. This contradicts the working assumption in sections 12 through 15 that
the problem was preventing entry.

**Bots jump 3.7x too often.** 6.50 jumps per bot-minute against retail's 1.76,
over 416 and 63 samples respectively. Conditional on jumping, the odds of landing
in a hazard zone are close (6.2% against 4.8%), so individual jumps are not much
more dangerous. The excess entry rate is driven by jump volume, and jump volume
is a general behavioural defect rather than something specific to the shield belt
ledges of section 5.

Pain pulses arrive every 917ms against retail's 1008ms, so the pain timer runs
about 9% fast. Pulse sizes match retail's depth scaled 16/32/40, so damage per
pulse is correct. At 9% this is real but not a significant contributor next to a
9x residence gap.

### Reproduction

    C:\Devstuff\QuestGames\UT99-469e\run-telemetry-match.ps1

Defaults to 16 bots, Difficulty 3, DM-Deck16][, 5 minutes, 30Hz. The log is
renamed from `.tmp` to `.log` when the match ends, so the timelimit must be
allowed to expire. Output is UTF-16LE tab separated at `UT99-469e\Logs\`.

## 19. Retail instrumentation via a UT99 mutator

Sections 12 through 15 assumed the defect was bots entering the slime. Retail
telemetry shows that assumption was wrong, and identifies a specific mechanism we
never execute.

### The instrument

`C:\Devstuff\QuestGames\UT99-469e\BotTelemetry\Classes\` adds a mutator that
samples every pawn at 30Hz (position, velocity, physics, health, zone flags for
all three body regions, `MoveTarget`, `RouteCache`, state, orders) and hooks
`MutatorTakeDamage`, `PreventDeath` and `ScoreKill` so every damage pulse and
death carries its exact `DamageType`. `BotTelemetryLog` extends `StatLogFile` but
writes through `FileLog()` and flushes once per sample rather than once per line.

Attach with `?Mutator=BotTelemetry.BotTelemetryMutator`, or use
`UT99-469e\run-telemetry-match.ps1`. Output is UTF-16LE TSV in `UT99-469e\Logs\`.

Validation: the logged death count and the dumped scoreboard agree exactly on
both runs. The 5 minute run logged 19 environmental deaths against a scoreboard
of 136 frags and 174 deaths, and `(174 - 136) / 2 = 19`.

### Measurement error corrected

An earlier pass reported a 9x hazard residence gap and a 78% escape rate for
SurrealEngine. Both were artifacts. Episodes were closed when `in_hazard_zone`
went false and classified as deaths only if `health <= 0` was observed, but a bot
that dies in a hazard respawns outside it at full health, so every hazard death
was recorded as an escape and its residence was extended to the respawn.

`environmental_deaths_exact` reports 26 across the two control runs where that
reconstruction found 9. Episodes are now terminated on the tick `deaths_exact`
increments, and the retail parser terminates on the logged death for symmetry.
`harmful_zone_escape_foot_entries_exact` independently confirms the episode count
of 41.

### Matched comparison

Retail: DM-Deck16][, 16 bots, Difficulty 3, 5 minutes, 96 bot-minutes.
SurrealEngine: control seeds 141421 and 161803, 64 bot-minutes.

| Measure                        | Retail | SurrealEngine |
| ------------------------------ | ------ | ------------- |
| Jumps per bot-minute           | 3.36   | 6.50          |
| Jumps landing in a hazard zone | 4.6%   | 6.2%          |
| Hazard episodes per bot-minute | 0.167  | 0.64          |
| Fatality per episode           | 43.8%  | 92.3%         |
| Residence, median              | 1.43s  | 1.83s         |
| Escape time, median            | 1.43s  | 11.37s        |
| Pain pulse interval            | 999ms  | 917ms         |

Residence is comparable because our episodes are dominated by deaths that arrive
quickly. Splitting by outcome is what separates the engines: our bots die in a
median of 1.82s and escape in a median of 11.37s, with the fastest escape in 39
episodes taking 8.52s. Retail escapes in 1.43s, inside its own time to death.
Egress loses the race by roughly 6x, which is what produces 92.3% fatality.

Retail bots do die in Deck16 slime, at 43.8% per episode. The premise that they
essentially never do is wrong; they die about a third as often as ours.

### JumpOutOfWater never fires

`Bot.uc` `ZoneChange()` calls `JumpOutOfWater()` when a swimming bot crosses out
of a water zone, which sets `velocity.Z = 380` and launches it over the lip.
That value is a usable signature.

| Engine        | Swimming to Falling exits | vz in [360,400] |
| ------------- | ------------------------- | --------------- |
| Retail        | 30                        | 7               |
| SurrealEngine | 53                        | 0               |

Two retail samples sit at exactly 380. Our exits cluster at 50 to 125, which is
falling and drifting, never launching. Retail's 7 launches match its 9 escapes.

The guards on that call are `bCanWalk`, non-zero horizontal acceleration,
`Destination.Z >= Location.Z`, and `CheckWaterJump()`. Evaluating the middle two
against telemetry at each of the 53 transitions gives 42 where both pass and no
jump fired. `bCanWalk` is set true for bots in `PreSetMovement()`. By elimination
`CheckWaterJump()` is returning false in every case where the jump was reachable.

`CheckWaterJump` is UnrealScript, not native, so it executes from the original
package; its absence from our C++ means nothing. It traces forward from
`Location` by `CollisionRadius` with a box extent and requires a hit, then traces
again from `1.1 * MaxStepHeight` higher with zero extent and requires a miss.
Either half can fail: a box trace against BSP that does not register the wall, or
the zero extent trace spuriously hitting. `NActor::Trace` forwards `Extent`
correctly, so the fault is inside `UActor::Trace` or `Collision.TraceFirstHit`.

This is not yet confirmed by direct instrumentation of `CheckWaterJump` itself.

### Two independent defects

1. `JumpOutOfWater` never triggers, giving 92.3% fatality per hazard entry.
2. Bots jump about 1.9x too often, giving 3.8x the hazard entry rate.

Neither is addressed by the pain-ledge veto, repeat cap, reachspec hazard cost or
hit-wall conjunction experiments in sections 12 through 15, which all targeted
entry prevention at walking speed and all returned null.

The pain timer fires every 917ms against retail's 999ms measured over 58 pulses,
so it runs about 9% fast. Pulse sizes match retail's depth scaled 16/32/40.

## 20. Trace is not the defect

Section 19 named `CheckWaterJump()` by elimination and pointed at `UActor::Trace`
or `Collision.TraceFirstHit`. The trace half of that is now falsified.

`--headless-driver=trace-corpus` replays the retail mutator's corpus through our
own collision system. Both engines answer the same 4016 queries, anchored to the
same NavigationPoints, across four combinations: box extent and zero extent, each
with `bTraceActors` false and true.

| Outcome                   | Probes |
| ------------------------- | ------ |
| Start coordinates differ  | 0      |
| Both miss                 | 3490   |
| Both hit                  | 512    |
| Retail hit, ours miss     | 6      |
| Ours hit, retail miss     | 8      |
| Normal differs on a hit   | 2      |

99.65% agreement. Box hit counts are 176 retail against 178 ours. The `boxact`
and `zeroact` variants reproduce the `box` and `zero` disagreements exactly, so
the residual is geometric edge cases, not actor filtering. Three of the six
retail-hit cases are `Teleporter1` probes where retail reports a hit at the start
point itself, which is a degenerate result rather than a wall.

`IsOwnedBy` walks from `this`, so `pawn->IsOwnedBy(pawn)` is true and a pawn's own
cylinder is already excluded from its own traces. That was also a candidate and is
also not the defect.

So `CheckWaterJump`'s two traces should behave the same in both engines. What has
not been ruled out is the direction they are fired in. `CheckWaterJump` traces
along `vector(Rotation)`, and the corpus only probes the four axis directions. A
bot facing away from the ledge produces a correct miss and a correct `false`, and
that would make this an orientation or destination defect rather than a collision
one.

Settling it needs `CheckWaterJump`'s inputs and outputs for our own pawns while
they are in the slime, with their real rotation, which the corpus cannot supply.

### StatLogFile

`OpenLog`, `FileLog`, `FileFlush` and `CloseLog` were registered natives with
empty bodies. They now perform real file I/O, including the `.tmp` to final
rename on close. Streams are held in a map keyed by object because retail's
`LogAr` int property cannot hold a pointer safely. This exists so instrumentation
packages written for retail can run unchanged here.

## 21. ActorReachable is over-permissive, and the nav graph is not to blame

Magnus Norddahl described this bug on Discord before it was measured here: SE's
`ActorReachable` returns true for nav points that are not actually reachable, so
bots try to run to places they cannot get to. Both halves are now quantified.

### The graph is identical

`NavigationPoint.Paths[16]`, `upstreamPaths[16]` and `PrunedPaths[16]` are
script-visible `const int` arrays indexing the ReachSpec table serialised in the
`.unr`. Both engines load the same file, so the indices are directly comparable.
The mutator dumps them as `G` records; `--trace-corpus-reach` does the same.

| Property     | Nodes agreeing |
| ------------ | -------------- |
| location     | 251 / 251      |
| Paths        | 251 / 251      |
| upstreamPaths| 251 / 251      |
| PrunedPaths  | 251 / 251      |
| ExtraCost    | 251 / 251      |
| bEndPoint    | 251 / 251      |
| bPlayerOnly  | 251 / 251      |

925 outgoing edges on each side. The stored connectivity is byte identical, so
nothing here is a map loading or reachspec deserialisation defect. Whatever
differs is in the runtime query.

### The query is not

Every ordered pair of nav points within 1000 units, asked from a `TMale1` probe
standing on the source node. The probe reports `r=17 h=39 step=25 player=True
walk=True swim=True fly=False` on both engines.

| Outcome                  | Pairs |
| ------------------------ | ----- |
| Both false               | 7590  |
| Both true                | 1974  |
| **Ours true, retail false** | **1536** |
| Retail true, ours false  | 21    |

Retail calls 1995 of 11121 pairs reachable (17.9%). We call 3510 reachable
(31.6%). The error is 73 to 1 in the permissive direction. `PointReachable`
leans the same way: 1778 pairs true only for us, 0 true only for retail.

### The signature points at the walk simulation

|                  | distance median | \|dz\| median | drop over 100 units |
| ---------------- | --------------- | ------------- | ------------------- |
| False positives  | 689             | 89            | 26%                 |
| Agreed true      | 364             | 2             | 9%                  |

The pairs we get wrong are far apart and at different heights. That matches what
`UActor.cpp:4510-4555` does: it steps up `MaxStepHeight`, sweeps the whole
remaining distance toward the goal in one `TryMove`, then steps back down
`MaxStepHeight`. Nothing in the loop tests whether the pawn is standing on
anything, so when there is no floor within a step the probe simply carries on at
the same altitude. A gap or a ledge is invisible to it. After the loop a single
unbounded downward `TryMove` falls to the goal and the result is accepted if it
lands within `CollisionHeight`, which also admits targets reached only by
dropping. Retail steps a real `walkMove` and cannot cross a gap it would fall
into.

This connects to the entry rate in section 19: we measured 0.64 hazard episodes
per bot minute against retail's 0.167, with 40 of 41 entries occurring while
Falling. Bots committing to destinations across the Deck16 slime pit and running
off the ledge produces exactly that.

Two clauses in the same function are separately wrong but are not this bug. The
reachspec pre-filter at `:4375-4431` loops over every nav point in the level
asking whether any traversable edge anywhere ends at the target, which is a
global property of the target rather than a question about reaching it from here,
so it passes nearly always and prevents little. The `PrunedPaths` loop at
`:4405-4427` rejects on `reachSpec.bPruned`, which is true for every entry it
iterates, so it can never contribute. Both can only reject, never falsely accept.

### Artifacts

`qa/runs/2026-07-27/reach-corpus/retail-reach-corpus.tsv` (11121 `R` rows, 251
`G` rows) and `ours-reach-corpus.tsv`. Reproduce ours with
`--autoplay --headless-driver=trace-corpus --trace-corpus-reach=1`; `--autoplay`
is required or the launcher GUI blocks the run.

## 22. Fixing the walk simulation

The walking branch of `UPawn::ActorReachable` swept the entire remaining distance
to the goal in a single `TryMove` and never asked whether the pawn was standing on
anything. Three changes, each measured against the retail corpus from section 21.

1. Advance one step at a time. The step is `2 * CollisionRadius`, capped at
   `walkingSimulationMaxIterations` (32) iterations instead of the previous 5.
   A single full length sweep samples nothing between the endpoints, so a gap in
   the middle of the path was invisible no matter what test followed it.
2. Settle onto the floor after each step, and refuse to continue when there is no
   walkable surface. This is what stops the probe from crossing a pit at constant
   altitude.
3. Allow the settle to fall further than a step down, up to
   `walkingSimulationFallDepth` (1024), because bots do drop off ledges to reach
   things. Whether the drop actually arrived is left to the existing height check
   after the loop, so falling into a pit fails there rather than being admitted.

Step 3 matters: with only steps 1 and 2 the false positives fell but false
negatives rose almost as much, because legitimate descents were being rejected.

| Version              | False pos | False neg | Agreement | Ours reachable |
| -------------------- | --------- | --------- | --------- | -------------- |
| original             | 1536      | 21        | 86.0%     | 31.6%          |
| + support check      | 1137      | 411       | 86.1%     | 24.5%          |
| + incremental sweep  | 271       | 456       | 93.5%     | 16.3%          |
| + falling allowed    | 369       | 204       | **94.9%** | 19.4%          |

Retail answers 17.9% on Deck16. DM-Curse][ was never used while tuning and comes
out at 96.4% agreement, 13.1% retail against 13.7% ours, so this is not fitted to
one map. Its nav graph is also identical across engines (165 nodes, 554 edges).
A full corpus run takes 17 seconds.

### Behaviour

Five seeds, 16 bots, 7200 fixed ticks, difficulty 3, DM-Deck16][, same
`config_id` as the section 19 controls.

| Seed    | 141421 | 161803 | 173205 | 223606 | 577215 |
| ------- | ------ | ------ | ------ | ------ | ------ |
| control | 16     | 10     | 14     | 17     | 19     |
| fixed   | 6      | 7      | 7      | 4      | 5      |

| Counter (5 seeds)          | Control | Fixed | Delta |
| -------------------------- | ------- | ----- | ----- |
| environmental deaths       | 76      | 29    | -62%  |
| suicides                   | 80      | 35    | -56%  |
| damage taken from level    | 7457    | 3216  | -57%  |
| hit wall events            | 26684   | 16114 | -40%  |
| deaths                     | 262     | 229   | -13%  |
| kills                      | 182     | 194   | +7%   |
| nav nodes visited          | 2996    | 3435  | +15%  |
| weapon pickups             | 55      | 63    | +15%  |

Improved on 5 of 5 seeds. The bots are not being made passive to buy this: they
visit more of the map, pick up more, and kill more while dying to the level less.

`pain_ledge_vetoes_exact` fell from 928 to 2 on seed 141421. The veto machinery
from sections 12 to 15 was catching bots about to walk into the slime; there is
now almost nothing left for it to catch, which is the expected signature of
having moved the fix upstream of the symptom.

Remaining: 369 false positives and 204 false negatives on Deck16, and
`PointReachable` is untouched and still answers true for 1778 pairs retail
rejects. The retail rate for the section 19 hazard entry measurement has not been
re-run against the fixed build.
