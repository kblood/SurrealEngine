# Direct-reach differential and stock outcome evidence (2026-07-26)

## Scope

This is a read-only comparison of the UE1 script contract, the current native
implementation, and retained stock-bot provenance.  It did not invoke UCC,
build the engine, change runtime flags, or alter bot behaviour.  Its question
is deliberately narrow: does a successful script `ActorReachable` decision
identify one shared direct-movement action that subsequently stalls, enters a
hazard, or dies in both UT99 and Unreal Gold?

The answer from the currently retained evidence is **no**.  There is no
cross-game direct-reach correction candidate to enable or merge from this
analysis.

## Contract and current implementation

Both extracted retail script sets expose the same native API:

```unrealscript
native(520) final function bool actorReachable(actor anActor);
native(540) final function actor FindBestInventoryPath(out float MinWeight,
    bool bPredictRespawns);
```

`Pawn.uc` describes `PointReachable` as a test of what portion of a direct
path is traversable with the pawn's current locomotion.  The sibling
`ActorReachable` declaration is native-only, so the scripts establish the
caller's decision contract but not the original native algorithm.

The two stock player-bot implementations use the result as a binary choice:

```text
candidate actor/inventory
  -> ActorReachable(candidate)
  -> true:  MoveTarget = candidate; later MoveToward(candidate)
  -> false: FindPathToward(candidate), or FindBestInventoryPath(...)
```

Examples in the exact exports are `Botpack/Bot.uc` (`PickLocalInventory`,
roaming, retreat, following, hunting) and `UnrealShare/Bots.uc` (local and
long-range inventory, ambush, retreat, following, and combat movement).
`FindBestInventoryPath` is not itself a direct inventory movement command: it
selects an `InventorySpot` route and returns the first route result for the
script to assign as `MoveTarget`.

The current engine implements that division as follows:

- `NPawn::actorReachable` calls `UPawn::ActorReachable(..., true,
  ScriptActorReachable)`; the script API supplies the `checkNavpoint` path.
- `UPawn::ActorReachable` rejects null, distant non-pawn, unreachable
  navigation-marker, pain-zone, non-swimmable-water, blocked-trace, and
  unstandable targets.  For walking pawns it runs up to five temporary
  step/slide movement probes, restores the original location, and returns the
  simulated result.
- `UPawn::FindBestInventoryPath` first marks nearby directly reachable
  navigation endpoints, scores reachable `InventorySpot` items by desire over
  route cost, then commits the selected graph route through
  `PathSpecialHandling`.

The direct-reach command observer is intentionally narrower than the method:
with the observer enabled, it records authority-side, stock autonomous player
bots in walking physics and excludes pawn and navigation-point targets.  Thus
it sees the script's direct actor/inventory decisions that could become a
walking command, while avoiding the internal navigation-endpoint probes used
to construct a graph route.  It records target/marker identity, result,
caller, reject reason, wall-slide use, and later same-life command terminal.
It is observation-only unless an unrelated, separately enabled experiment is
selected.

## Exact stock evidence

The two retained 16-bot anchors below completed successfully.  Their live
corrective settings are off: direct timeout, failed-navigation avoidance,
falling recovery, harmful-zone escape, inventory-marker safety, and walking
positive-DPS veto are all disabled.  `hazard_swim_egress_enabled` is the
observer envelope, while `hazard_swim_egress_live_enabled=false`; it does not
change movement.  Direct-reach, target-selection, pick-target, and warning
telemetry are enabled for evidence only.

| Adapter | Retained run | Map | Direct observations | Success / failure | Exact same-life terminals |
| --- | --- | --- | ---: | ---: | ---: |
| UT436 | `qa/runs/2026-07-26/ut436-deck16-16bot-s104729-full-provenance-r2/` | `DM-Deck16][` | 168 | 102 / 66 | 18 |
| Unreal Gold 226b | `qa/runs/2026-07-26/unreal226b-deathfan-16bot-s104729-provenance-envelope-1200-r1/` | `DmDeathFan` | 38 | 31 / 7 | 19 |

`Tools/BotBenchmark/Analyze-DirectReachCommands.py` validates sequence
continuity, counter reconciliation, target/life linkage, and terminal
partitioning.  Its result is the same in both anchors:

| Same-life terminal | UT Deck16 | Unreal DeathFan |
| --- | ---: | ---: |
| Command replaced | 18 | 19 |
| Cleared | 0 | 0 |
| Hazardous death | 0 | 0 |
| Non-hazard death | 0 | 0 |
| Life-boundary or run-end censored | 0 | 0 |

Every same-life record is a successful `script_actor_reachable` inventory
decision (`reject_reason=reached`), later replaced by another command.  The
UT records include weapons, ammo, health vials, and thigh pads; the Unreal
records include Razorjack, Rifle/RifleAmmo, and ASMD ammo.  Some probes use a
wall slide, but none terminates in a stall, hazardous death, or non-hazard
death.  Failed reachability probes are not movement commands; all 66 UT and
7 Unreal failures are unlinked, as expected.

The larger stock terminal-clustering pass independently found 17 Deck16 and
53 DeathFan harmful-water `PainTimer` terminals, but zero exact
direct-reach-owned terminals in either map.  Direct-reach observation/success/
failure/same-life counters did not increment in any two-second terminal
window.  That excludes using the high death count as evidence that the direct
inventory commands above caused water entry.

## Retry-chain check

The one retained UT run that also enables both the direct-reach and native
route-commit observers is:

`qa/runs/2026-07-26/ut436-deck16-4bot-s104729-t2400-direct-inventory-retry-observer-r1/`.

`Analyze-DirectInventoryRetryChains.py` found three exact direct-inventory
verdicts, zero full watchdog stalls, zero immediate same-target reissues, zero
marker-route commits, and zero qualifying chains.  This auxiliary run is not
a cross-game qualification (it is UT-only), but it also supplies no evidence
for a direct-inventory retry/replan remedy.

## Decision

Reject a shared live direct-reach change in the current state.  The direct
path is real and heavily used, especially for nearby inventory, but the two
stock adapters show its linked outcomes are command replacement rather than
stall or death.  A rejection, cooldown, forced graph route, or safety policy
would therefore be an unproven behaviour change and could suppress legitimate
short pickup routes.

This does not negate the separate source-level reach-spec capability gap
recorded in `BOT_AI_UE1_MOVEMENT_PATH_DIFFERENTIAL_2026-07-26.md`.  That gap
is on graph-route eligibility and needs its own deterministic fixture and
retail oracle; it is not licensed by these direct-command traces.

Any future candidate must first retain, in both adapters, an exact chain:

```text
successful same-life direct actor/inventory command
  -> live target/marker remains selected
  -> full no-progress watchdog stall or exact hazard/death ownership
  -> repeatable result with all live remedies off
  -> certified safe alternative route or cancellation behaviour
```

Until that evidence exists, retain the observer and keep direct-reach policy
changes default-off.
