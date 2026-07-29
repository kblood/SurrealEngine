# How UE1 bots work in SurrealEngine

This is a practical reference for the bot systems currently loaded from
Unreal Tournament 436 and Unreal Gold 226b. It distinguishes three things
that are easy to conflate:

1. The **stock UnrealScript brain** that chooses a state, target, item, or
   route goal.
2. The **native UE1 services** that make those script calls work: tracing,
   path search, latent movement, physics, collision callbacks, and map actor
   interactions.
3. The **map-authored navigation data** that constrains a route: navigation
   nodes, directed reachspecs, zones, movers, inventory positions, starts, and
   teleporter/warp links.

The current compatibility target is to make the stock brain competent. New
policies remain opt-in experiments; this document is not a claim that every
future policy exists today.

The diagram companion is [BOT_AI_MECHANICS_DIAGRAMS.html](BOT_AI_MECHANICS_DIAGRAMS.html).

## 1. Bot classes and script authority

| Game | Stock bot base used by the benchmark | Game mode used by the anchor benchmark |
| --- | --- | --- |
| UT436 | `Botpack.Bot` | `Botpack.DeathMatchPlus` |
| Unreal Gold 226b | `UnrealShare.Bots` (with concrete UnrealShare/UnrealI subclasses) | `UnrealShare.DeathMatchGame` |

The game packages supply the behavior states: roaming, combat, hunting,
inventory seeking, lift/door handling, falling, swimming, and recovery. They
invoke UE1 native functions through the VM; they do not perform collision
resolution or graph search in script.

Useful engine boundaries:

- `SurrealEngine/Native/NPawn.cpp` binds script-visible Pawn natives such as
  `FindPathTo`, `FindPathToward`, `FindBestInventoryPath`, `ActorReachable`,
  `MoveTo`, `MoveToward`, `StrafeTo`, `TurnTo`, and `PickWallAdjust`.
- `SurrealEngine/UObject/UActor.cpp` contains the shared `UPawn` pathfinding,
  movement, collision, and latent-action implementation.
- The VM `StateFrame` stores the active UnrealScript state and latent action;
  the benchmark samples this as `MoveTo`, `MoveToward`, `StrafeTo`, and related
  movement intent rather than guessing from velocity alone.

## 2. The normal bot loop

At a high level, a stock bot repeatedly selects a goal, asks native code for a
path or direct-reach answer, starts a latent move, and lets normal pawn physics
advance that move until arrival, a collision callback, a route transition, or
script state change. Combat and inventory decisions interrupt the same loop.

```text
UnrealScript state
  -> choose enemy / pickup / random destination / objective
  -> direct check (ActorReachable or PointReachable), or FindPath*
  -> MoveToward / MoveTo / StrafeTo latent command
  -> TickWalking, TickFalling, TickSwimming and collision
  -> HitWall, Bump, Touch, ZoneChange, timer/arrival callbacks
  -> script resumes and chooses the next command
```

The important ownership rule is that script chooses *intent*, while native code
owns physical progression. A proposed bot improvement must not silently change
`Destination`, `MoveTarget`, `RouteCache`, acceleration, physics, or latent
state unless it is a declared, benchmarked experiment.

## 3. How a map tells a bot where it can go

### Navigation graph

Each loaded `ULevel` supplies a linked `NavigationPointList` and a level-wide
`ReachSpecs` array. A NavigationPoint owns indexed outgoing, incoming, and
pruned path references. A ReachSpec is a directed traversal edge with:

- start and end node references;
- travel distance and collision dimensions;
- reach flags (for example walking, jumping, swimming, special traversal);
- pruning/availability information; and
- map-authored meaning derived from its endpoints and actors.

`FindPathToEndPoint` searches this graph and `SetRouteCache` writes a bounded
prefix of selected NavigationPoints into the Pawn's `RouteCache`. The first
cache entry becomes the next navigation target; script normally issues
`MoveToward` against it and revisits planning as the cache changes.

The map catalog records this real data rather than recreating it. The current
anchors demonstrate the scale: UT Deck16-II has 251 navigation points and
1,997 reachspecs; Unreal DeathFan has 116 points and 1,431 reachspecs.

### Node and traversal actors

These authored actors give native/script code additional traversal semantics:

| Actor family | What it contributes to bot navigation |
| --- | --- |
| `PathNode`, `NavigationPoint` | ordinary graph waypoints and reachspec endpoints |
| `InventorySpot` | a node associated with an item/pickup location |
| `PlayerStart` | spawn/initial placement and a navigation position |
| `LiftCenter`, `LiftExit` | lift boarding/exiting graph structure |
| `ButtonMarker`, `TriggerMarker` | scripted mover/button traversal hints |
| `Teleporter`, `WarpZoneMarker` | non-local transitions represented in the map graph |
| `Mover` / doors / lifts | runtime blockers and scripted traversal interactions |
| zones | water, pain, kill, gravity, friction, velocity, and terminal-speed context |
| inventory/weapon/ammo/health actors | direct-reach and inventory-routing goals |

Nodes are not a collision-free guarantee. A graph edge still has to survive
current collision, mover state, zone physics, callbacks, and script
`SpecialHandling` at runtime.

## 4. Path search, direct reach, and route ownership

There are two distinct ways a bot begins movement.

1. **Direct movement.** Script asks `ActorReachable` or `PointReachable` and,
   if accepted, uses a target actor or position directly. This is common for
   nearby pickups and weapons. It may have no RouteCache head.
2. **Graph movement.** Script calls `FindPathTo`, `FindPathToward`,
   `FindBestInventoryPath`, or a related helper. Native search selects directed
   reachspecs, writes the route prefix, and script moves toward the first node.

`SpecialHandling` is deliberately after the native route-cache write: a node
or actor can redirect script behavior after graph search. This is why a later
`MoveTarget` alone does not prove which reachspec path search originally chose.
The native path-commit observer and route-execution stream preserve that
boundary for benchmark investigation.

## 5. Movement and collision execution

The latent movement functions set script-visible movement state and are then
advanced every frame by Pawn movement code.

| Command | Typical input | Native responsibility while it is active |
| --- | --- | --- |
| `MoveTo` | world-space destination | turn, accelerate, walk/fall/swim, detect arrival and timeouts |
| `MoveToward` | actor target, usually a node or pickup | update destination/target relationship, arrive, replan when script asks |
| `StrafeTo` / `StrafeFacing` | destination and focus | preserve facing while moving under normal collision/physics |
| `TurnTo` / `TurnToward` | vector or actor | rotate within pawn turn constraints |
| `WaitForLanding` | no destination | yield script until landing/physics transition |

`TickWalking` performs collision sweeps, step-up/down handling, sliding, wall
contacts, and calls script `HitWall` where the UE1 contract permits it. Script
handlers may adjust a jump, choose a temporary wall-avoidance destination,
open a mover, or switch state. Falling and swimming use their own physics
paths; zone transitions can trigger pain, water, gravity, damage, and terminal
effects.

The engine must preserve callback ordering. A seemingly small change to direct
reach, `HitWall`, path cache ownership, or movement timers can alter stock
script decisions and therefore changes actual bot behavior.

## 6. Inventory, combat, and survival goals

The same graph serves several script goals:

- inventory search evaluates reachable pickups and graph paths to desired
  weapons, ammunition, health, armor, and other items;
- combat states choose enemies using sight, team and game-state rules, then
  select a pursuit, retreat, firing, or navigation action;
- roaming/random-destination states choose navigation goals when there is no
  stronger combat or inventory need;
- traversal states handle lifts, doors, movers, water, falling, and temporary
  collision conditions; and
- death/respawn ends a pawn life and starts a new placement/state cycle.

SurrealEngine exposes `LineOfSightTo`, hearing/noise helpers, targeting
functions, native route search, and movement/collision callbacks to make these
stock script decisions meaningful. Some higher-level perception features remain
future policy work and are not silently substituted for stock logic.

## 7. What SurrealEngine measures before changing a bot

The deterministic `BotBenchmark` driver creates controlled bot-only matches
and writes a per-tick event stream. It records persistent score/death counters,
movement intent, positions, latent action, target identity, route samples,
damage, pickups, hazard exposure, and optional observer records. Read-only
observers currently include:

- selected native route-cache commits and their reachspec provenance;
- direct `ActorReachable` command provenance;
- navigation coverage and map-catalog identity;
- move-stall recovery, wall contacts, falling/pain/water diagnostics; and
- exact death and hazard-residence partitions.

The required analysis stance is fail-closed: missing roster, counter,
sequence, endpoint, or overflow evidence makes a run unqualified. A lower
proxy is not enough; a behavior candidate must demonstrate cross-game,
deterministic improvement in survival, navigation, or combat without causing a
regression elsewhere.

## 8. Reading a bot run

Use this checklist to diagnose a failure without mistaking symptoms for cause.

1. Identify the bot's game, class, map, seed, skill, and actual roster.
2. Check whether it had an active latent command and whether `MoveTarget` was
   direct or route-backed.
3. For graph movement, inspect the native path-commit and RouteCache first-hop
   evidence; do not infer an edge from a later cache sample.
4. For direct movement, require exact same-life target linkage and an active
   command window before associating a later event with the reachability check.
5. Inspect collision, `HitWall`, physics mode, zone/hazard state, mover, and
   life boundary before calling a bot stuck or unsafe.
6. Compare at least paired deterministic runs. Treat default-off observers as
   neutral only when observer-on/off streams are equivalent after removing
   their declared telemetry envelope.

## Related references

- [BOT_AI_ARCHITECTURE.md](BOT_AI_ARCHITECTURE.md) — target layered design.
- [BOT_AI_OWNER_DATA_EXTRACTION.md](BOT_AI_OWNER_DATA_EXTRACTION.md) — map,
  bot-class, reachspec, zone, and traversal extraction contract.
- [BOT_AI_CROSS_GAME_AND_MAPS.md](BOT_AI_CROSS_GAME_AND_MAPS.md) — adapter and
  map-feature matrix.
- [BOT_BENCHMARK_DRIVER.md](BOT_BENCHMARK_DRIVER.md) — deterministic match
  driver and telemetry artifacts.
- [BOT_AI_QUALITY_EXECUTION_PLAN.md](BOT_AI_QUALITY_EXECUTION_PLAN.md) —
  evidence-backed iteration history and current behavior gates.
