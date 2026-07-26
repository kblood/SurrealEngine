# Retail movement-latent path evidence — 2026-07-26

## Scope

This read-only trace combines the retail UT436 `BotPack.u` and Unreal Gold
226b `UnrealShare.u` scripts with the shared native pawn implementation.  It
also classifies the one-tick near-expiry samples in the current stock anchors:

- UT436 `DM-Deck16][`, 16 bots, seed `104729`, 7,200 ticks.
- Unreal Gold 226b `DmDeathFan`, the same bot count, seed, and tick budget.

No UCC invocation, engine behavior change, or telemetry change is included.

## Shared native contract

Both retail script packages use the same UE1 native numbering:

| Native | Latent state | Native setup | Natural completion |
| --- | --- | --- | --- |
| `Pawn.MoveTo(vector, optional float)` (`500`) | `MoveTo` (`501`) | Clears `MoveTarget`; sets destination/focus and a distance-derived `MoveTimer`. | Arrival or timer below zero. |
| `Pawn.MoveToward(Actor, optional float)` (`502`) | `MoveToward` (`502`) | Sets `MoveTarget`, destination, and focus. Pawn targets receive a one-second timer; other actors, including navigation points, receive a distance-derived timer. | Arrival, deleted target, or timer below zero. |
| `StrafeTo` (`504`) / `StrafeFacing` (`506`) | `StrafeTo` (`505`) / `StrafeFacing` (`507`) | Both use a distance-derived timer; `StrafeTo` clears `MoveTarget`. | The same timer-or-arrival rule. |

`UPawn::Tick` subtracts the fixed delta from `MoveTimer` before evaluating the
active latent.  A positive timer within one 60-Hz tick of zero is therefore an
ordinary end-of-command sample, not a timeout, stall, or failed route by
itself.

## Retail caller paths

The following paths are shared in structure by UT `Bot` and Unreal `Bots`:

| State/action | Script issue | Intent |
| --- | --- | --- |
| `Roaming` | `PickDestination` then `MoveToward(MoveTarget)` | Inventory or navigation progression. The route helper `FindBestPathToward` sets `MoveTarget` to the path result and `Destination` to its location. |
| `Hunting` | `PickDestination`; `MoveTo(Destination)` when no actor target exists, otherwise `MoveToward(MoveTarget)` | Search/follow progression. |
| `Charging` | `MoveToward(Enemy)` when directly reachable; otherwise `FindBestPathToward(Enemy)` and transition to route/tactical movement. | Direct pursuit or graph approach to an enemy. |
| `Retreating` | `MoveToward(MoveTarget)` when facing the route target; otherwise `StrafeFacing(MoveTarget.Location, Enemy)`. | Retreat route versus combat movement. |
| `TacticalMove` | `MoveTo(Destination)` for direct movement or `StrafeFacing(Destination, Enemy)` for a strafing combat move. | Combat positioning, not necessarily navigation. |
| `wandering` | `MoveTo(Destination, WalkingSpeed)`. | Targetless patrol movement. |

`AdjustFromWall` is important: in both packages it issues `StrafeTo(Destination,
Focus)` in `Hunting`, and in Unreal `Bots.Roaming` it also issues `StrafeTo`
after the wall-adjust decision.  These are positional recovery moves and clear
the actor target.

The significant adapter differences are script policy, rather than the native
movement contract: UT `Charging` calls `FindBestPathToward(Enemy, true)` and
has an advanced-tactics branch; Unreal uses its own `ValidRecovery` and
skill/visibility choices.  Both still reach the same `500`/`502` native
latents for positional and actor-directed movement.

## Near-expiry classification

Using `0 < MoveTimer <= 0.018334` (one fixed tick plus rounding) on retained
stock telemetry:

| Adapter | Reported samples | Script-level classification | Consequence |
| --- | ---: | --- | --- |
| UT436 Deck16 | 21 | All are `StrafeFacing` in `TacticalMove` (the `DoStrafeMove` branch), not `MoveTo` or route `MoveToward`. | Natural combat-strafe completion; no route-expiry claim. |
| Unreal DeathFan | 10 | Six `StrafeTo` samples in `Hunting` and four in `Roaming`, each from the `AdjustFromWall` path. | Targetless positional wall-adjust completion; no target-owned route expiry. |

For context, the same one-tick band also contains ordinary `MoveToward` and
other latent completions (UT: 27 `MoveToward`, 1 `MoveTo`, 1 `StrafeTo`; Unreal:
8 `MoveToward`, 2 `MoveTo`, 65 `StrafeFacing`).  The 21/10 figures therefore
cannot be used as a proxy for an expiring route command.

## Decision boundary

An actionable timeout candidate needs more than a near-zero timer: retain the
same life, latent kind, live target identity, route/direct-command provenance,
no-progress interval, and a non-benign terminal (or an independently certified
hazard/death).  The current 21/10 samples fail at the first semantic step: they
are strafe commands rather than route ownership witnesses.
