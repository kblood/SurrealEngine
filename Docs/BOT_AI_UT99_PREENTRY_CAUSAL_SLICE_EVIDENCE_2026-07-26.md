# UT99 pre-entry causal-slice evidence — 2026-07-26

## Scope

This note records the first successful run of the default-off pre-entry hazard
causal-slice observer. It joins a harmful-residence terminal ledger to exact
entry-zone identity, position, physics/support transition, retained command
lineage, and same-tick `MayFall`/walking `HitWall` boundaries. It is read-only.

Artifact:

`D:\SurrealEngineQA\2026-07-26\ut436-deck16-hazard-preentry-causal-slice-s104729-r1\runs\000000-hazard-preentry-causal-slice-dm-deck16-game-botpack-deathmatc-s104729-r0-0525bec64473`

The 16-bot, skill-3, seed `104729`, 7,200-tick UT436 Deck16 run passes the
generic quality validator and the dedicated causal-slice analyzer.

## Exact result

The observer sealed 16 integrity-valid episodes with no overflow: 15 death
terminals and one run-end censor. All 15 deaths are harmful-water deaths.

| Entry-zone identity | Deaths |
| --- | ---: |
| `134:SlimeZone0` | 12 |
| `137:SlimeZone2` | 2 |
| `136:SlimeZone1` | 1 |

Fourteen deaths transition from `falling` to `falling`, with no static support,
no same-tick `MayFall`, and no walking `HitWall` boundary. The sole exception
enters `SlimeZone2` as `walking` to `falling`, with a MayFall boundary at tick
883 and no HitWall. It is one episode, not a repeated causal callback pattern.

Entry commands are also heterogeneous: 13 `MoveToward` and two `MoveTo`, across
Roaming (9), Hunting (3), Charging/Retreating (3) contexts and one additional
mixed state. Complete same-life lineages range from one to nineteen commands.
This records real command churn, but it does not identify one route target,
direction, or script issue as the cause of a water entry.

## Decision

No movement, route, water-egress, wall, or ledge behavior change is authorized.
The repeated `SlimeZone0` outcome is a spatial terminal cluster, while the
causal evidence shows that most entries were already falling before the
residence began and lacked a common same-tick callback boundary.

The next read-only evidence requirement is a pre-entry route/trajectory join:

1. retain the immediately preceding path-commit target and route head;
2. retain the entry-direction and a bounded predicted fall/zone crossing;
3. join that evidence to the existing causal slice and terminal; and
4. require repetition on Deck16 plus held-out confirmation before proposing a
   zone-specific veto, replan, or other movement intervention.

Until then, treat the `SlimeZone0` prevalence as a reason to investigate a
specific pre-fall route/trajectory, not as permission to override bot movement.
