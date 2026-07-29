# UE1 route-graph semantic differential

Date: 2026-07-26

## Decision

The next source-level parity investigation is **ReachSpec eligibility at route
expansion**, not a new global route veto.  The unified engine currently
serializes, catalogs, observes, and can evaluate ReachSpec capability data, but
the live route search has explicit `reachFlags` omissions.  Authoritative
headers describe those flags as a required-capabilities subset.

That is a concrete semantic differential worth a deterministic test and a
read-only, same-life observation pass.  It is **not** evidence that the
missing check caused the Deck16 or DeathFan failures, and it does not authorize
a behavior change yet.  The two existing stock anchor runs recorded only
capability-compatible committed edges; see
`BOT_AI_CROSS_GAME_REACHSPEC_CAPABILITY_EVIDENCE_2026-07-26.md`.

The old claim that unified inventory routing completely ignores navigation
costs is no longer true.  `FindPathToEndPoint` currently adds the current
node's `cost()` (including an UnrealScript `SpecialCost` result) to its
accumulated distance, and `FindBestInventoryPath` ranks using the resulting
adjusted endpoint cost.  The stale commented loop in the inventory function is
not sufficient evidence for a cost fix.

No engine code, scripts, commercial game data, executable, or UCC invocation
was changed or run for this research note.

## Evidence boundary

| Evidence | What it establishes | What it does not establish |
| --- | --- | --- |
| `tools/reference/dx-headers/Engine/Inc/UnReach.h` and `APawn.h` | `FReachSpec::supports()` requires adequate collision dimensions and `(reachFlags & moveFlags) == reachFlags`; `APawn::calcMoveFlags()` defines the seven movement/capability bits.  The path API also propagates `bSinglePath` to `breadthPathFrom`. | Exact UT436 or Unreal Gold executable control flow or tie-breaking.  These reference headers are the semantic contract source, not a retail execution trace. |
| Owner-local retail UnrealScript export under `C:\Devstuff\QuestGames\SurrealEngine\qa\runs\2026-07-26\script-export-hitwall-certificate` | UT436 and Unreal 226b expose the same native 517/518 path API and native 540 inventory API; their standard bot scripts rely repeatedly on `ActorReachable`, `FindPathToward`, and `FindBestInventoryPath`. | Native implementation details.  The exported scripts remain outside Git because they are commercial game data. |
| Current unified source | The exact current behavior and omissions described below. | Retail parity merely from structural similarity. |
| Manifest-backed live anchor evidence | Both stock runs joined committed path edges to a live capability snapshot and found no capability mismatch. | Causation for a death, stall, or bad direct movement command. |

The flag values are stable across the reference header and the unified map
format: `walk=1`, `fly=2`, `swim=4`, `jump=8`, `door=16`, `special=32`, and
`player-only=64`.

## Current flow and caller contract

```text
UT Bot / Unreal Bots UnrealScript
  ├─ ActorReachable(target) ──> direct movement choice / combat decision
  ├─ FindPathToward(actor) ──> mark directly reachable nav endpoints
  │                              └─ reverse ReachSpec expansion
  │                                  └─ PathSpecialHandling / route cache
  └─ FindBestInventoryPath() ─> mark endpoints ─> per-marker route search
                                      └─ desire / adjusted route cost
```

The retail script declarations agree in UT436 `Engine.Classes.Pawn.uc`
lines 268–292 and Unreal 226b `Engine.Classes.Pawn.uc` lines 260–284:

- `FindPathTo(vector, optional bool bSinglePath, optional bool bClearPaths)`
  is native 518;
- `FindPathToward(actor, optional bool bSinglePath, optional bool bClearPaths)`
  is native 517; and
- `FindBestInventoryPath(out float MinWeight, bool bPredictRespawns)` is native
  540.

The standard UT436 `Botpack.Bot` and Unreal 226b `UnrealShare.Bots` callers
mostly use the default `bSinglePath=false`.  UT436 does pass the third,
`bClearPaths`, argument in its roaming logic, and the unified native wrapper
honors it before dispatching.  Both standard bots use route results to select
`MoveTarget` for orders, roaming, ambushes, and inventory; both also use
`ActorReachable` as an earlier direct-movement branch.  Therefore a false
positive direct reach result cannot be blamed on a historical route cache, and
a path-cache record alone cannot prove it caused the subsequent movement.

Unreal 226b `ScriptedPawn`—useful compatibility coverage but not the standard
player bot—does call `FindPathToward(..., true)` and `FindPathTo(..., true)`.

## Concrete differentials

### 1. ReachSpec flags are not consulted by the live graph search

This is the primary candidate.

The unified `UPawn::FindPathToEndPoint` reverse-expands `upstreamPaths` in
`SurrealEngine/UObject/UActor.cpp` (around lines 5107–5163).  It rejects
undersized or pruned links and non-player access to `bPlayerOnly` nodes, then
contains an explicit `// To do: check reachFlags` before admitting the edge.
`FindRandomDest` has the same omission.  Its direct-theoretical-navpoint gate
inside `ActorReachable(..., checkNavpoint=true)` repeats the omission for both
`Paths` and `PrunedPaths` (around lines 4120–4240).

This differs from `FReachSpec::supports()` in the reference header, which
requires all edge flags to occur in the pawn's movement mask.  The reference
mask includes walking, flying, swimming, jumping, door opening, special
handling, and player-only capability.  It is a meaningful omission even where
most stock bot edges happen to be compatible.

There is already a deliberately disconnected, tested implementation of the
same header rule in `PawnReachSpecEligibility.*`, and the route-commit observer
records its result.  Keep it disconnected until the proof gates below pass.
The existing 7,200-tick UT436 Deck16 and Unreal 226b DeathFan observer runs
found zero incompatible *committed* edges, so adding an unconditional filter
would currently be a speculative regression risk.

### 2. `bSinglePath` is accepted but ignored in unified routing

This is a real API-consumption gap, but a lower-priority bot-quality candidate.

The native wrappers pass `bSinglePath` to `UPawn::FindPathTo` and
`UPawn::FindPathToward`.  Those functions forward it through calls, but the
final `FindPathToward` route branch always calls `FindPathToEndPoint(..., 1000)`
without a single-path parameter; no current branch reads `singlePath`.
The reference `APawn.h` keeps `bSinglePath` in `findPathTo`,
`findPathToward`, and `breadthPathFrom`, establishing that it is an intended
native routing input rather than dead script syntax.

The standard UT436 and Unreal player bots do not exercise `true` in the
inspected callers, so this should not be folded into the current Deck16 bot
fix.  It should be covered by a compact compatibility fixture before a future
ScriptedPawn/mod parity change.

### 3. Direct reach simulation has an acknowledged zone-transition blind spot

`ActorReachable` performs bounded walking, flying, or swimming simulation, but
the walking path explicitly says `// To do: take zone changes into account?`.
It checks the target's pain/water zone before simulating; it does not model all
intermediate zone transitions.  This is relevant to harmful corridors and
water/pain exits, but it is a **separate direct-command hypothesis**, not a
reason to change graph eligibility.  Existing direct-reach and inventory-marker
observers are the correct causal instrumentation for it.

### 4. Cost is present, but its parity semantics remain unproven

`ClearPaths` evaluates `SpecialCost` or `ExtraCost`.  Reverse expansion then
uses `PawnPath::AccumulateCost(previous, reachSpec.distance, current->cost())`;
the helper clamps negative components and saturates at `int32` maximum.
`FindBestInventoryPath` divides desire by `AdjustedEndpointCost`.

The current code therefore has a coherent cost path.  What remains unproven is
the exact retail placement and ordering of special-node cost, endpoint choice,
and equal-cost tie breaks.  Do not change cost math from the old comment.  A
two-branch fixture plus a retail oracle comparison is required first.

## Deterministic proof plan

| Priority | Seam | Required deterministic test | Required live observation | Promotion gate |
| --- | --- | --- | --- | --- |
| P0 | ReachSpec eligibility | Extend the existing `PawnReachSpecEligibilityTests` with the exact header subset rule, every flag, mixed flags, unknown bits, and changing `bCanJump`.  Add a small route fixture with two otherwise identical branches: one capability-ineligible, one eligible. | Default-off route record must include candidate edge index, raw flags, collision rejection, capability snapshot, rejection reason, selected chain, caller API, and same-life command/progress outcome. | On both game lanes, show a same-life ineligible candidate selected by baseline and tie it to an unsafe/stalled command; then demonstrate the eligible alternative and no quality regression. |
| P1 | `bSinglePath` | Fixture calls both `FindPathTo` and `FindPathToward` with false/true on a branching graph; it must first characterize retail output, then encode that output. | Record the input value and path length/first hop only when explicitly enabled. | Keep separate from standard-bot quality unless a player-bot caller is found using `true`. |
| P2 | Cost placement/tie break | Two equal-geometry branches with `ExtraCost`/`SpecialCost` at controlled nodes; verify accumulated cost, first hop, endpoint, saturation, and deterministic equal-cost order. | Route record adds candidate accumulated cost and stable order, never changes selection. | Compare an isolated retail result and qualify UT436 + Unreal after a proposed change. |
| P3 | Direct zone transitions | Reuse the existing direct-reach observer on a controlled harmful/water crossing; assert only observed reason/progress fields. | Join target, caller origin, zone sequence, support/landing result, and harmful outcome in one life. | A causal witness and a counterfactual are mandatory; do not infer it from route cache history. |

The existing `BotInventoryRouteHandoffFixture` and route-commit capability
observer are suitable starting seams.  They avoid map edits and UCC: fixture
graphs exercise native routing in-process, while the observer preserves retail
map data as read-only input.

## Live-observation sequence

1. Freeze a clean, attested baseline for UT436 `DM-Deck16][` and Unreal 226b
   `DmDeathFan`; retain the catalog hash, bot roster, seed, tick budget, and
   observer configuration in each manifest.
2. Enable observations only.  For every route result, record the API origin,
   `bSinglePath`, target/anchor, endpoint candidates, all considered edge
   rejections, cost, selected edge chain, capability snapshot, and the
   first ensuing movement-command/progress window.  Join by pawn and life ID.
3. Analyze whether a candidate which the header rule calls unsupported was
   actually selected, then whether it was the active command immediately
   preceding a reproducible stall, harmful-zone entry, or suicide.
4. If and only if a witness exists, implement the smallest candidate behind a
   default-off experiment, rerun the deterministic fixture, and compare
   multiple matched bot-only lanes in both games.
5. Promote only after manifest-backed quality metrics show no regression and
   the observation demonstrates that the affected same-life failure decreased.

This sequencing separates three distinct questions: whether a serialized edge
is formally unsupported, whether the native search selected it, and whether
that selection caused a harmful action.  Treating any one as proof of the next
would conflate parity data with bot-quality causation.

## Scope boundary

This note recommends no route filter, direct-reach override, path-cache clear,
cost rewrite, or bot-script rewrite.  It records the next differential and
the evidence necessary to decide it safely across UT436 and Unreal Gold.
