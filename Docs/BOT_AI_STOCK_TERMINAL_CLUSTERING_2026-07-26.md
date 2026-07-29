# Stock environmental-terminal clustering (2026-07-26)

## Scope and method

This is a read-only clustering pass over the current 16-bot, stock-predicate
anchors already used by the provenance discovery lane:

| Adapter | Run | Map / seed / difficulty / ticks |
| --- | --- | --- |
| UT436 | `qa/runs/2026-07-26/ut436-deck16-instrumented-stock-s104729-7200-r1/` | `DM-Deck16][`, `104729`, 3, 7,200 |
| Unreal Gold 226b | `qa/runs/2026-07-26/unreal226b-deathfan-instrumented-stock-s104729-7200-r1/` | `DmDeathFan`, `104729`, 3, 7,200 |

For every exact death partition with `environmental_source="pain_timer"`, the
pass joined the terminal sample to the preceding 121 telemetry samples.  With
the configured fixed delta this is a 2.03-second inclusive terminal window.
It grouped terminal position, physics/state, positive-DPS-hazard membership,
retained target/latent command history, and monotonic callback/command
counters.  It also checked the exact hazard-residence/direct-reach ownership
witness already emitted by the driver.

These are one stock capture per adapter (`r1` only).  Counts show strong
within-run recurrence; they are **not** claims of an exact same-configuration
repetition.  No output below authorizes a behavior or telemetry change.

## Terminal population

The common exact terminal is `PainTimer` while a harmful water residence is
active: 70 cases total.  It is an engine damage/death boundary, not evidence
that the immediately preceding bot command selected the water route.

| Measure | UT Deck16 | Unreal DeathFan | Combined |
| --- | ---: | ---: | ---: |
| `PainTimer` harmful-residence terminals | 17 | 53 | 70 |
| Unassisted environmental death | 12 | 38 | 50 |
| Recent-enemy-contributed environmental proxy | 5 | 15 | 20 |
| Terminal physics: `Swimming` / `Falling` | 12 / 5 | 47 / 6 | 59 / 11 |
| Window wholly in hazard (at least 120 of 122 samples) | 11 | 26 | 37 |
| Window contains both safe/falling and hazard samples | 6 | 27 | 33 |
| Exact residence command ownership | 0 | 0 | 0 |
| Hazardous direct-reach terminal | 0 | 0 | 0 |

The 20 enemy-contribution cases are combat-confounded and cannot establish a
bot-only navigation failure.  The remaining 50 unassisted deaths establish
that survival is poor in the two maps' harmful water, but not that one common
bot action produced the entry or that a common corrective command is safe.

## Map and zone clusters

Both catalogues identify static, positive-DPS water, but they are not the same
map geometry.

| Map | Catalogued positive-DPS zones | Terminal-location clustering | Interpretation |
| --- | --- | --- | --- |
| Deck16 | `SlimeZone0`, `SlimeZone1`, `SlimeZone2`; each is water/pain at 40 DPS | 5 terminals form a tight upper basin: x `1937..2016`, y `1169..1374`, z `-745..-718`; 12 form a lower band: x `511..1246`, y `-906..487`, z `-1397..-1241`. | At least two spatially distinct local water hazards are involved. Current tick telemetry reports only `in_hazard_zone`, not the zone actor/index, so it cannot assign a terminal to one of the three `SlimeZone` objects. |
| DeathFan | one positive-DPS zone, `SlimeZone0` (water/pain, 40 DPS; catalogue actor 241) | all 53 terminals lie in its shared lower volume: x `-847..806`, y `-848..905`, z `-457..-283`. | One map-specific slime volume accounts for the complete DeathFan terminal population. |

Thus the matching factor is UE1 water-pain semantics (`PainTimer`), while the
geometries and location distributions are map-specific.  The evidence does
not support calling the two location clusters one shared bot navigation bug.

## Final two seconds: state and command provenance

The recent histories do not converge on a common movement command or recovery
callback.

| Signal in the inclusive 2.03-second window | UT Deck16 | Unreal DeathFan |
| --- | ---: | ---: |
| Terminal retained no `MoveTarget` | 8 / 17 | 51 / 53 |
| Terminal retained movement intent | 13 / 17 | 46 / 53 |
| Distinct residence command changes, per terminal | 0–6 (mean 2.29) | 0–17 (mean 3.57) |
| Windows with raw `HitWall` increments | 6 / 17 (11 total) | 12 / 53 (69 total) |
| Windows with a dispatched walking `HitWall` callback | 1 / 17 | 0 / 53 |
| `WallAdjust` calls / recovery attempts | 0 / 0 | 0 / 0 |
| Move-stall detections / forced replans | 0 / 0 | 0 / 0 |
| Direct-reach observation, success, failure, or same-life terminal increment | 0 | 0 |

The retained latent actions are correspondingly heterogeneous: `MoveTo`,
`MoveToward`, `StrafeFacing`, `StrafeTo`, `WaitForLanding`, `Continue`,
`TurnTo`, `Sleep`, and targetless transitions all occur in the two-second
windows.  Deck16 has nine terminals with a known target, spread across
`PlayerStart16`, `PathNode122`, `PathNode149`, `UT_Eightball1`, `enforcer49`,
and `PulseGun1`; DeathFan has only two known targets (`MaleOneBot1` and
`InventorySpot30`).  This is command churn, not a repeated target or callback
chain.

Two Deck16 residences observed a direct-safe candidate, but both were
superseded before death.  DeathFan observed none.  More importantly, every
terminal has `hazard_residence_command_ownership_exact=false`.  The existing
direct-reach observer therefore gives no same-life, live-target owned command
to replace, cancel, or replan.

Raw collision counts are also not a candidate.  They lack the exact walking
callback and `WallAdjust` recovery path in the same terminal window, are
present in a minority of terminals, and are substantially more numerous but
less specific on DeathFan.  Treating a raw `HitWall` counter as cause would
reintroduce the already rejected wall-contact inference.

## Engine-hook assessment

`UPawn::Tick` already calls `AdvanceHazardResidence(elapsed)` and, on the
authority path, dispatches the VM `PainTimer` event when the pawn pain timer
expires (`UActor.cpp`).  The benchmark validates that hook contract and joins
it to `Killed`; `RecordHazardResidenceDeath` and the driver then create the
exact terminal/ownership witness.

`PainTimer` is therefore the highest-frequency exact shared hook, but it is
already an observed and rejected *action predicate*: it is the final damage
timer, not a pre-entry bot choice, and it has zero exact command-owned
terminals across all 70 cases.  A policy at that point would be reactive after
known damage, with no certified exit or stock command to modify.

There is **no highest-frequency common mechanism with a new exact engine hook
that is not already rejected** in these anchors.  The other candidate hooks
are weaker:

- `HitWall` has no repeatable dispatched-callback/`WallAdjust` chain.
- move-stall and direct-reach terminals have no overlap with the 70 deaths;
- falls are a minority terminal physics state and occur at different
  map-specific water volumes;
- a route-edge explanation remains unavailable here (the native path-commit
  observer was not enabled) and was already rejected by the earlier
  cross-game route-correlation evidence.

## Conclusion and evidence boundary

The defensible conclusion is "shared UE1 water-pain death terminal; separate
map-local slime geometry; no exact bot-command cause."  It is useful
measurement evidence, but not a cross-game BOT AI correction candidate.

Any future observer-only discovery slice must add the missing live zone
actor/index and retain a same-life command identity from issuance through the
`PainTimer`/death terminal.  A future behavior proposal would additionally
need a certified safe exit and exact-repeat evidence in the same stock
configuration.  Until then, keep egress, route, timeout, and collision
interventions default-off.
