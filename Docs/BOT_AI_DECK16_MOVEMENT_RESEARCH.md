# UT bot locomotion and hazard research

## Scope and confidence

This note narrows the Deck16-II investigation to native movement, route cost,
local hazard avoidance, and stuck recovery. It complements
`BOT_AI_QUALITY_EXECUTION_PLAN.md`; it does not replace the benchmark matrix.

The preserved fork's three-seed comparison is material evidence: mean Deck16
no-progress fell from about 36.33 seconds to 6.97 seconds and stuck proxies from
4.33 to 1.33. It is not evidence that the whole fork is merge-ready. Its same
Deck16 runs produced 15 deaths, 7 attributed kills, 7 environmental deaths, and
one self-fatal death. Traces repeatedly show `instigator=None` `Corroded` damage
while bots remain in slime. Movement correctness and hazard survival therefore
need separate gates.

No Deck16 map asset was added or inspected as part of this slice. Map-specific
causal claims below are benchmark-informed hypotheses until replayed against
the owner's retail data. The code-contract findings are directly reproducible.

## What unified already received from the earlier bot work

The following navigation commits are ancestors of the current unified branch,
not missing fork-only work:

| Commit | Present behavior | Remaining limitation |
| --- | --- | --- |
| `376abf05` | initial navigation search corrections | distance still dominates route choice |
| `c13cfb7b` | `FindPathToEndPoint` returns path and distance | no hazard cost in the path expansion |
| `6ce6c0ba` | further path extraction/selection correction | reach flags remain TODO |
| `cc4dd623` | calls a path point's `SpecialHandling` | depends on a correct route reaching that point |
| `0d1855d3` | bounds `SpecialHandling`/`FindPathToward` recursion | does not make unsafe paths expensive |

This matters for integration: bulk-porting these commits would duplicate code.
The preserved parity benchmark commit `a65ae787` contains later native fixes
that unified does not have, mixed together with extensive instrumentation.
Those fixes should be reconstructed one at a time with focused tests.

## Exact contract gaps

### 1. Arrival is declared far too early

Unified `UPawn::TickMoveTo` completes walking movement when
`distanceSquared < velocitySquared * 0.05`. At 300 uu/s that threshold is about
67 uu, regardless of frame duration, collision cylinder, or whether the target
is vertically touchable. A non-colliding NavigationPoint can consequently be
reported reached outside the distance at which script route-cache code consumes
it. The script can select the same node again, producing apparent completion,
reselection, and oscillation while the bot makes little progress.

The fork changes this to an elapsed-time envelope:

`threshold = max(target acceptance radius, horizontal speed * elapsed + 1)`

It also distinguishes colliding actors from point-like navigation actors and
rejects a walking arrival when the target's vertical separation is not
touchable. Of the fork-only candidates, this is the smallest fix most directly
supported by the measured no-progress improvement. It should be reconstructed
as an isolated patch and fixture before importing any broader path or policy
change.

### 2. Actor blockers can swallow the stock `HitWall` recovery

`Botpack.Bot` defines state-specific `HitWall` handlers that enter labels such
as `AdjustFromWall`; movers are also coordinated through that callback. In
unified `TickWalking`, a `UPlayerPawn` collision with a non-pushable colliding
actor enters a TODO branch and sends no `HitWall`. UT bots inherit through the
player-pawn hierarchy, so doors, movers, pawns, and similar actor blockers can
bypass the script recovery path. Fork fixture `f567c6a3` was built specifically
around this contract.

This is a narrow correctness candidate, but it is secondary to the arrival
envelope for the current stuck symptom: the benchmark evidence does not yet
attribute every no-progress span to a wall or mover contact.

#### Wall-adjust distance audit

The exported UE1 `Engine.Pawn` contract says `PickWallAdjust` should first test
whether a knee-height obstruction can be jumped, then try a destination 90
degrees right or left using outward traces and floor checks. Stock UT436 Bot
states set `Focus = Destination`, call `PickWallAdjust`, then execute
`StrafeTo(Destination, Focus)` followed by `MoveTo(Focus)`. A successful native
call must therefore leave a real temporary side destination while preserving
the blocked route goal in `Focus`.

The public script contract does not expose the retail native distance. Two
same-generation data points constrain a focused reconstruction: UT's
`TournamentPlayer` collision radius is 17 uu, and UE1 `Pawn.CheckWaterJump`
uses `2 * CollisionRadius` as its body-clearance trace distance above an
obstruction. The existing one-unit wall adjustment is neither body-scaled nor
executable under the corrected arrival envelope. The focused correction uses
one collision diameter for the lateral dry run and destination while retaining
the existing jump-first, right-first, then left ordering. This is an
evidence-based compatibility reconstruction, not a claim that the closed
retail native used the identical constant. Floor prediction and jump-selection
parity remain separate work.

### 3. Walking ledges bypass the script veto

Unified walking physics switches immediately to falling when the step-down
probe finds no floor. Stock Bot code has state-specific `MayFall` handlers and
temporarily enables `bAvoidLedges`/`bStopAtLedges` during tactical movement.
The fork calls `MayFall` before committing to a drop and restores the last
grounded position if script clears `bCanJump`.

This is valid parity work, but not a complete Deck16 fall fix. Current traces
often show falls being allowed, so a callback alone cannot decide whether a
landing is survivable. Route reach flags and bounded landing prediction remain
necessary.

### 4. Pain cost is calculated and then ignored by route selection

The local `Botpack.PainPath` source exposes both `SpecialCost` and
`SpecialHandling`. Its cost is very high unless the seeker has the matching
reduced-damage type. Unified `ClearPaths` calls `SpecialCost` and stores
`NavigationPoint.cost`, but `FindPathToEndPoint` accumulates only reachspec
distance. `FindBestInventoryPath` even retains a commented-out `cost +=
nav->cost()` line. The result is internally inconsistent: direct reachability
rejects some pain destinations, but graph search can still choose a route
through a pain path because the route never pays its cost.

This is the strongest code explanation for repeated slime exposure. Fix route
cost first, then add local escape steering for cases where combat, knockback,
spawn position, or an already-committed segment puts the pawn inside pain.

### 5. Jump adjustment uses the wrong flight model and target

Unified `EAdjustJump` simulates a jump starting from `JumpZ`, toward the current
height, and initially aims at `Focus`. Stock scripts can set boosted vertical
velocity immediately before calling it, and `Focus` may be an enemy while
`Destination` is the landing goal. The fork instead solves flight time from
the current vertical velocity and gravity to `Destination.Z`, then derives the
horizontal velocity toward `Destination`.

This is relevant to bad jumps and splash/impact boosts. It is not a substitute
for proving that the landing corridor is supported and below a safe drop.

The exported Botpack script confirms the native call contract: ordinary jump
setup assigns `Velocity.Z = JumpZ` immediately before `Velocity =
EAdjustJump()`, while impact-jump paths assign a computed boosted `velZ` or
`Default.JumpZ + velZ` first. The focused reconstruction therefore solves the
later positive gravity root from current `Velocity.Z` to `Destination.Z`, aims
the horizontal component at `Destination`, and bounds it by `GroundSpeed`.
Level, uphill, downhill, boosted, zero-gravity, no-solution, and invalid inputs
are covered by a pure deterministic fixture. This improves landing intent; it
does not authorize a jump or establish that a Deck16 landing is safe.

## Design lessons from other FPS bot implementations

These projects converge on a useful separation between global routing and
bounded local movement:

- [Quake III Arena's GPL bot movement](https://github.com/id-Software/Quake-III-Arena/blob/master/code/botlib/be_aas_move.c)
  predicts movement with explicit stop events for slime, lava, gaps, and ground
  damage. Its higher-level movement code also keeps bounded avoid spots and
  avoids repeatedly failing reachabilities.
- [Recast/Detour's path corridor](https://github.com/recastnavigation/recastnavigation/blob/master/DetourCrowd/Source/DetourPathCorridor.cpp)
  continually feeds actual local movement back into a bounded corridor rather
  than assuming the original global path remains exact.
- [YaPB terrain handling](https://github.com/yapb/yapb/blob/master/src/navigate.cpp)
  confirms a suspected collision over time, then tries a bounded set of jump,
  lateral-strafe, and duck candidates while retaining short recovery history.
- [ReGameDLL CS bot navigation](https://github.com/rehlds/ReGameDLL_CS/blob/master/regamedll/dlls/bot/cs_bot_nav.cpp)
  measures average velocity over a window, records the stuck location, and does
  not clear the condition until meaningful displacement occurs.
- [UT99 `PainPath`](https://github.com/Slipyx/UT99/blob/769bd788cb2c06077a26ff7c3e4fced521a2169e/Botpack/PainPath.uc)
  confirms that hazardous navigation points are intended to participate in
  native route cost and special handling.

The implementation here is original, engine-independent code. The GPL Quake
sources inform behavior and test categories only; no code is copied.

Quake III's bounded `AAS_PredictClientMovement` pattern was also applied more
directly to unsupported UE1 walking transitions: fixed-frame cylinder sweeps
are paired with `UModel::FindRegion` foot samples, corresponding to Quake's
bbox trace and lava/slime point-content stop events. The UE1-specific danger
rule remains the existing `PointReachable` contract: reject a newly entered
`bPainZone` unless its damage type matches `ReducedDamageType`.

Public UT99 script documentation provides useful but incomplete jump evidence:

- [`Bot.uc`](https://project-archives.etc.cmu.edu/2001/fall/coyote210/Docs/undox/Botpack.Bot.html)
  sets velocity before `EAdjustJump`, uses `Destination` for its descending
  branch, and treats a low adjusted horizontal speed as meaningful there.
- [`Pawn.uc`](https://project-archives.etc.cmu.edu/2001/fall/coyote210/Docs/undox/Engine.Pawn.html)
  exposes `Destination` and `Focus` as distinct movement and look fields but
  contains only the native `EAdjustJump` declaration.
- [OldUnreal UT patches](https://github.com/OldUnreal/UnrealTournamentPatches)
  contain no public native `EAdjustJump` implementation. A reported
  [wall/retarget loop](https://github.com/OldUnreal/UnrealTournamentPatches/issues/1568)
  is behaviorally relevant but not an oracle for Surreal's engine code.

Consequently, `Destination` and caller-provided vertical intent are the best
supported semantics, but the exact horizontal adjustment remains unknown.
Three reconstructed variants were benchmarked and all regressed at least one
map. None is suitable as canonical runtime behavior yet.

## Proposed runtime architecture

1. Keep macro policies (`stock-botpack`, `utility-arena`, and
   `tactical-state`) responsible for goals, not per-frame collision response.
2. Repair the stock native contracts independently: elapsed-time arrival,
   actor-blocker `HitWall`, `MayFall`, correct jump adjustment, reach flags, and
   route costs.
3. Add a bounded engine adapter that samples at most 16 local candidate
   directions. Each probe reports clearance, supported landing/drop, predicted
   jump reachability, pain entry, and expected damage.
4. Run `MovementSafetyAdvisor` in shadow. It rejects pain entry, missing or
   excessive-drop landings, blocked corridors, and unreachable jumps; when
   already in pain it strongly prefers an exit. After one stuck second it
   prefers lateral candidates and deterministically rotates away from the last
   attempted recovery.
5. Only after shadow telemetry and fixtures agree with live traces should a
   command adapter apply the selected direction.
6. Keep weapon fire authorization separate. Add muzzle-to-impact clearance,
   blast radius, predicted self damage, friendly exposure, and escape-space
   inputs before allowing splash fire. The current macro policy observation has
   none of these, while stock `ClearShot` protection is partial and
   state-specific.

The first shadow slice is implemented in `BotMovementSafety.h/.cpp`, with a
strict standalone test in `BotMovementSafetyTests.cpp`. It is deliberately not
wired to live bot control.

## Focused execution plan

### Phase A: reconstruct native correctness

1. Add a synthetic moving-pawn fixture proving the old arrival expression
   completes early and reselects a non-colliding nav point.
2. Implement only the elapsed-time and target-envelope `TickMoveTo` change.
3. Re-run the six fixed Deck16/Morbias cases. Require no regression in match
   completion and compare no-progress/stuck distributions, not just means.
4. Independently add actor-blocker `HitWall`, ledge-veto, and `EAdjustJump`
   fixtures before considering those patches.

### Phase B: make routes hazard-aware

1. Add a synthetic graph with a short `PainPath` route and longer safe route.
2. Honor `SpecialCost` and legal reach flags with saturating path arithmetic.
3. Add telemetry for chosen route cost, pain-zone entry/exit duration, and
   environmental damage.
4. Require the safe route when affordable and rapid escape when already in
   pain; do not treat eventual death as the first indication of failure.

### Phase C: shadow local steering

1. Build deterministic probe adapters for a wall corridor, ledge, safe jump,
   excessive drop, and pain-zone edge.
2. Record every candidate status and score through the existing shadow path.
3. Compare `continue`, `steer`, `escape-hazard`, and `recover-from-stuck`
   recommendations with actual stock outcomes on Deck16.
4. Enable live steering only behind an explicit experimental selector after it
   reduces exposure and stuck time without increasing falls.

### Phase D: weapon safety

1. Create hitscan and splash-fire synthetic fixtures, including a close wall,
   close enemy, teammate, and no-escape landing.
2. Gate fire on predicted self/friendly damage independently of action policy.
3. Require zero avoidable self-fatal splash events in the fixed-seed smoke
   matrix before discussing skill-tier tuning.

## Merge recommendation

Do not merge the preserved parity fork wholesale. Unified already contains its
older path-search series, while the later benchmark commit combines valuable
native fixes, telemetry, and behavior changes. The current evidence supports a
sequence of narrow merges, starting with the arrival-envelope fixture and fix,
then hazard-aware route cost. Local steering remains shadow-only until the
Deck16 environmental-death signal improves. Difficulty levels should be built
only after competent survival and locomotion are stable.
