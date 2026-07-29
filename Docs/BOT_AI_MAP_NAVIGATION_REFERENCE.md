# UE1 map navigation reference for bots

This companion to [BOT_AI_MECHANICS_REFERENCE.md](BOT_AI_MECHANICS_REFERENCE.md)
describes the map-side data that a stock bot and SurrealEngine's native Pawn
services consume. It is based on the loaded `ULevel` representation, not on a
replacement graph.

## Core graph objects

| Loaded object | Role in bot navigation | Important runtime detail |
| --- | --- | --- |
| `NavigationPoint` / `PathNode` | authored waypoint and reachspec endpoint | linked through `NavigationPointList`; arrays reference global reachspec indexes |
| `ReachSpec` | directed feasible traversal relation | stores start/end, distance, collision dimensions, reach flags, and pruning state |
| `RouteCache` | bounded Pawn-local selected path prefix | written by native search; first entry is usually the current route target |
| `InventorySpot` | graph location associated with a pickup | supports inventory routing but live item availability remains runtime state |
| `PlayerStart` | spawn placement and graph-adjacent location | starts a new pawn life; it is not a route guarantee |

Direction matters. A map can contain a traversal that is possible one way but
not the other, so a route inspection must bind each selected edge to its exact
directed ReachSpec rather than treating nodes as an undirected graph.

## Special traversal actors

| Map feature | Navigation objects | Why native and script handling are both needed |
| --- | --- | --- |
| lifts | `LiftCenter`, `LiftExit`, mover | route intent reaches the lift, but timing, boarding, waiting, collision, and script state decide traversal |
| doors/buttons | `ButtonMarker`, `TriggerMarker`, mover | `SpecialHandling`, `HandleDoor`, triggers, and `HitWall` can redirect the immediate command |
| teleporters/warp zones | `Teleporter`, `WarpZoneMarker` | a non-local transition has authored graph meaning and live destination/zone behavior |
| water and pain areas | zone membership plus navigation geometry | swimming, damage, gravity, currents, and safe egress are live physics/zone questions |
| inventory | InventorySpot and inventory actor | desired item state, respawn, ownership, and direct reachability are live questions |

## From a script goal to a map traversal

1. Script chooses an actor, point, objective, inventory item, or random
   destination.
2. Native `ActorReachable`/`PointReachable` may allow direct movement if
   immediate collision/physics simulation accepts it.
3. Otherwise `FindPathTo`, `FindPathToward`, or inventory path search expands
   directed ReachSpecs whose endpoints and flags are valid for the Pawn.
4. The chosen prefix is written to `RouteCache`; `SpecialHandling` may then
   alter script's immediate target for a traversal actor.
5. Script starts a latent `MoveToward`/`MoveTo` command.
6. Native walking, falling, swimming, collision, and mover handling execute
   the command. The bot can return to script through arrival, `HitWall`, Bump,
   Touch, zone transition, death, or a latent timeout.

The direct and graph routes must remain distinguishable in analysis. A direct
pickup command does not prove a ReachSpec was selected; an old RouteCache entry
does not prove that it caused a later direct move.

## What the map catalog captures

`--headless-driver=map-catalog` serializes the owner-local evidence required to
reason about a map without committing retail assets:

- stable actor/index identity, class, position, and collision data for every
  NavigationPoint;
- outgoing/incoming/pruned path indexes and fully resolved ReachSpec endpoints;
- item spots, player starts, lift/mover/trigger/teleporter relationships;
- zone topology and properties relevant to water, pain, gravity, friction,
  velocity, and terminal speed; and
- package/map identity and hashes so same-named maps are not assumed identical.

The current anchor catalog results are documented in
[BOT_AI_OWNER_DATA_EXTRACTION.md](BOT_AI_OWNER_DATA_EXTRACTION.md): UT436
Deck16-II has 251 navigation points / 1,997 ReachSpecs; Unreal Gold DeathFan
has 116 / 1,431. The catalog is validation evidence, not a second authority
that a bot follows instead of the loaded map.

## Safe use in bot work

- Use a map catalog to select fixtures, validate endpoint references, and
  identify map features before proposing a change.
- Use native path-commit provenance to prove what current path search chose.
- Use per-tick route samples to describe current execution, not to rewrite
  historical path-search decisions.
- Treat movers, zone changes, collision callbacks, and item respawns as live
  state: static graph data cannot by itself prove a safe action.
- Require a deterministic, cross-game opportunity and exact terminal evidence
  before making a default-off behavior experiment.
