# UE1 movement/path source differential — 2026-07-26

## Scope and source status

This is a read-only source comparison.  No UCC invocation, build, telemetry
change, or behavior change is included.

The original UE1 native implementation is not distributed in the checked-in
UT469b script snapshot or the OldUnreal patch repository.  Accordingly, this
note separates **confirmed Surreal omissions** from an **oracle-required
candidate**; it does not claim byte-for-byte retail-native parity.

| Source | Authority used here |
| --- | --- |
| `research/bots/sources/ut99-unrealscript-469b/` | Versioned UT469b UnrealScript snapshot, [`Slipyx/UT99`](https://github.com/Slipyx/UT99/tree/769bd788cb2c06077a26ff7c3e4fced521a2169e) commit `769bd788cb2c06077a26ff7c3e4fced521a2169e`.  It establishes native declarations and Botpack/UnrealShare callers, not the original native implementation. |
| `tools/reference/deusex-scripts/Engine/Inc/APawn.h` and `Engine/Src/UnPath.h` | UE1 SDK headers: movement-native surface, reachability methods, path search signatures, capability flags, and `calcMoveFlags`. |
| Current source | `SurrealEngine/Native/NPawn.cpp` and `SurrealEngine/UObject/UActor.cpp`. |

## Retail contract and caller chain

`Engine/Pawn.uc:256-272` declares the shared native surface: latent
`MoveTo` (500) and `MoveToward` (502), plus `FindPathTo` (518) and
`FindPathToward` (517), both with `bSinglePath` and `bClearPaths` parameters.
The wrapper in `NPawn.cpp:118-132` preserves the default clear-path behavior:
it calls `ClearPaths()` unless `bClearPaths` is explicitly false.

The relevant stock bot chain is direct:

```text
Bot.FindBestPathToward(desired, bClearPaths)       Bot.uc:1325-1340
  -> FindPathToward(desired, , bClearPaths)        Bot.uc:1333
  -> MoveTarget / Destination = returned first hop Bot.uc:1337-1339
  -> MoveToward(MoveTarget)                        bot state movement labels
  -> HitWall -> PickWallAdjust() or MoveTimer=-1   Bot.uc:2757-2776
```

The shared UnrealShare script adapter has the same native calls and state-level
`HitWall` recovery pattern in `UnrealShare/Bots.uc` (for example
`1791-1810`).  This 469b source snapshot is a caller reference, not direct
proof of the Unreal Gold 226b native binary. `UnrealShare/ScriptedPawn.uc:592` also passes
`bSinglePath=true`, so that flag is live in the shared UE1 actor stack even if
the standard UT deathmatch bot path does not set it.

## Confirmed path-stack differentials

### 1. ReachSpec capability flags are read but not enforced

The UE1 SDK defines `R_WALK`, `R_FLY`, `R_SWIM`, `R_JUMP`, `R_DOOR`,
`R_SPECIAL`, and `R_PLAYERONLY` in `UnPath.h:35-45`.  `APawn.h:43-46` passes
reach flags into walk/jump/fly/swim reachability, and `APawn.h:57-67` passes
`calcMoveFlags()` into breadth path search.  `calcMoveFlags()` derives the
mask from the pawn's locomotion and door/special abilities.

Surreal deserializes and records `LevelReachSpec::reachFlags`
(`ULevel.cpp:99-106`; `UActor.cpp:5027-5029`), but its relevant searches carry
explicit `To do: check reachFlags` omissions:

| Current call site | Missing flag gate | Consequence if an incompatible edge is selected |
| --- | --- | --- |
| `UPawn::ActorReachable` (`UActor.cpp:4215`, `4239`) | direct reachability through navigation links | A candidate can look reachable despite requiring an unsupported movement class. |
| `UPawn::FindPathToEndPoint` (`5139`) | graph expansion and first-hop selection | A shortest route may include swim/jump/door/special traversal that this pawn cannot perform. |
| `UPawn::FindRandomDest` (`5338`) | reachable-node expansion | Random roaming can choose a capability-incompatible node. |

This is a confirmed implementation gap, and it plausibly explains navigation
loops or hazard exposure: a bot can be directed to a first hop whose map edge
requires movement the pawn cannot execute.  It is not yet proof that the
Deck16 `PathNode131` cluster uses such an edge; its reach-spec flags have not
been joined to a terminal adverse outcome.

**Deterministic test seam:** construct a fixed two-route navigation fixture
with equal collision dimensions: the cheaper first hop has only `R_SWIM` or
`R_JUMP`, while the alternate has `R_WALK`.  A walking non-swimming/non-
jumping pawn must reject the cheaper edge and select the walk edge.  Repeat
with capability enabled and with `R_DOOR`/`R_SPECIAL` separately.  Record
first hop, route cache, edge flags, latent terminal, and any `HitWall` call.
Use a retail oracle before enabling any correction on game maps.

### 2. `bSinglePath` is accepted but currently unused by `UPawn::FindPathToward`

`NPawn.cpp:126-132` forwards `bSinglePath` to
`UPawn::FindPathToward(anActor, value)`, but the implementation at
`UActor.cpp:5404-5423` does not read `singlePath`.  In contrast, the SDK
declares `findPathToward(..., bSinglePath, ...)` and passes that parameter to
`breadthPathFrom` (`APawn.h:57-61`).

This is a confirmed parameter-loss differential.  Its practical relevance to
the standard UT/Unreal deathmatch bot run is lower than reach flags, because
the observed Botpack `FindBestPathToward` call leaves the second argument at
its default.  It matters for shared `ScriptedPawn` callers and any mod/game
script that requests a single path.

**Deterministic test seam:** use a symmetric graph with two valid branches
and call `FindPathToward(goal, false)` and `FindPathToward(goal, true)` under
a fixed seed.  Capture returned first hop and full route cache, then compare
both modes with a retail executable/oracle.  Do not infer the intended
single-path tie-break rule from the header alone.

## Collision/`HitWall` candidate requiring a retail oracle

Retail `Pawn.uc:87` documents `MinHitWall` as the minimum
`HitNormal dot Velocity.Normal` needed for a physics `HitWall`; the Botpack
default is `-0.5` (`Bot.uc:771`) and its scripts deliberately adjust the value
in some states (`Bot.uc:3749-3754`, `5587-5601`).  The scripts react strongly
to an arriving callback: the standard handler either invokes `PickWallAdjust`
or sets `MoveTimer=-1` (`Bot.uc:2757-2776`).

At the current walking collision call site, Surreal dispatches the script
event on a vertical-normal band (`UActor.cpp:1379-1384`) rather than at an
explicit `MinHitWall` comparison.  The code also records the pre-callback
`MinHitWall` value for diagnostics.  This is a concrete source-level
difference in the dispatch predicate, but the available retail material does
not include `APawn::physWalking`; therefore it is a **candidate**, not a
licensed correction.

**Deterministic oracle test:** create fixed walking contacts with the same
wall normal/velocity but sweep `MinHitWall` above and below their dot product.
For each, record whether retail invokes `HitWall`, the callback's resulting
`MoveTimer`/state, and the matching Surreal result.  Include static world,
mover, and blocking actor contacts.  The existing walking-`HitWall` fixture
is suitable only after it exposes this parameterized result.

## Movement-latent result

The native IDs and high-level setup contract agree with the current code:
`MoveTo` clears `MoveTarget`; `MoveToward` stores the actor target; both set a
latent state and a move timer.  Surreal performs the timer decrement before
the latent poll (`UActor.cpp:5599-5647`), calculates its duration in
`SetMoveDuration` (`8999-9003`), and uses its own arrival/vertical-reach
logic in `TickMoveTo` (`6857-6920`).  The SDK exposes polling natives and
`setMoveTimer` but not their implementation, so there is no supported
source-level claim of a timer or arrival divergence yet.

The next deterministic parity test should therefore sample, for a fixed
start/target pair, initial `MoveTimer`, per-tick latent state, first arrival,
and timeout on both retail and Surreal.  It should cover a nav point, an
inventory actor, a pawn target, vertical separation, and a blocked route.

## Priority and non-goal

Prioritize the reach-flag fixture and retail-oracle comparison first: it is a
confirmed omitted capability predicate directly on the route-selection path.
Then test `MinHitWall` dispatch.  Neither finding authorizes a bot-policy
override or a broad movement rewrite; a source correction must be narrow,
retail-validated, and separately qualified on both UT and Unreal fixtures.
