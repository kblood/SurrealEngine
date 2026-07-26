# UT99 pre-entry route/trajectory observer evidence — 2026-07-26

## Scope

This is a default-off, read-only extension of the hazard-residence pre-entry
causal slice. It adds the exact preceding native path-commit identity, retained
route head or an explicit cache-clear result, current command target, entry
velocity/direction, and a bounded falling trajectory/zone observation. It does
not change movement, pathfinding, collision, physics, or UnrealScript.

Artifact:

`D:\SurrealEngineQA\2026-07-26\ut436-deck16-hazard-preentry-route-trajectory-s104729-r3\runs\000000-hazard-preentry-route-trajectory-dm-deck16-game-botpack-deathmatc-s104729-r0-367992e2b0f6`

The run is UT436 Deck16, 16 skill-3 bots, seed `104729`, and 7,200 fixed ticks.
The generic matrix validation passes, as does
`Analyze-HazardResidencePreentryCausalSlices.py` schema v2.

## Exact result

The strict observer sealed 16 causal slices with no overflow: 15 harmful-water
deaths and one run-end censor. Every slice has a finite, complete bounded
trajectory result and an exact entry-zone identity.

| Route state at entry | Episodes |
| --- | ---: |
| Retained route head | 13 |
| Exact native cache-clear commit | 3 |

The retained heads are heterogeneous (`PathNode122`, `InventorySpot152`,
`LiftExit6`, and several single occurrences). The three missing heads are not
unknown data: their immediate native commits explicitly cleared RouteCache, so
the observer records that there was no route-head target to attribute. Fifteen
of sixteen slices retain a concrete command target; all are joined to their
entry command and native path-commit sequence by the fail-closed analyzer.

## Decision

This evidence does not authorize a movement intervention. The terminal cluster
still contains mixed retained routes, targets, and explicit cache clears. The
next causal decision must use repeated/held-out route and target concentration,
not infer a generic water or ledge rule from the dominant SlimeZone0 terminal.
