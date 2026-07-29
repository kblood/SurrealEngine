# Upstream PR candidates, prepared for review

Date: 2026-07-28

Two branches are prepared and building against current `dpjudas/master`
(`2d47264c`). **Neither has been pushed and no pull request has been opened.**
This document is the review packet requested before either becomes a PR.

`Docs/UpstreamCoordination.md` records the contribution policy these were
prepared under.

## Where the branches are

| Branch | Worktree | Base | Commit |
| --- | --- | --- | --- |
| `pr/pawn-reachable-walk-simulation` | `repos/worktrees/review/pawn-reachable` | `origin/master` `2d47264c` | `983252d3` |
| `pr/statlogfile` | `repos/worktrees/review/statlogfile` | `origin/master` `2d47264c` | pending build |

Each is a single commit on top of upstream and touches a single file. They are
independent of each other and of every other branch in this fork.

They are not cherry-picks. `feature/bot-ai-quality-clean` has diverged far
enough that the same corrections were rewritten against upstream's own code and
idiom: upstream's `ActorReachable` has a two argument signature and none of this
fork's instrumentation, so the branch uses upstream's `TryMove(delta, true)` and
its literal `0.7071f` walkable convention rather than this fork's
`ProbeMoveCollision` and `walkingStepWalkableNormalZ`.

---

## Candidate 1: `pr/pawn-reachable-walk-simulation`

**File:** `SurrealEngine/Packages/Engine/Actors/Pawn/UPawn.cpp` (+36 / -11)

### The defect

`UPawn::ActorReachable` decides whether a pawn can walk somewhere by simulating
the walk. The walking branch sweeps the entire remaining distance to the goal in
a single `TryMove` and never checks whether there is floor underneath. Nothing
between the two endpoints is ever sampled, so a gap, a pit or a ledge in the
middle of the path is invisible and the goal is reported reachable.

Bots ask this constantly while routing, so they walk into things they cannot
walk across. On DM-Deck16][ that is the slime.

The wall slide in the same loop has two further errors: it recomputes its
remaining distance from the goal rather than from the move it just made, and it
then assigns `actuallyMoved` a distance the pawn did not travel, which corrupts
the loop's own progress test.

### The fix

Advance one collision diameter at a time and settle onto the floor after each
step. A drop beyond `MaxStepHeight` is still allowed — bots deliberately fall
off ledges to reach things — but the landing has to be on walkable ground.
Whether the landing actually got anywhere is still decided by the existing
height check after the loop, so falling into a pit fails there, not in the loop.

The iteration cap rises from 5 to 32 because each iteration now covers a bounded
step rather than the whole distance.

### Evidence

Both engines were asked the identical question. A `Botpack.TMale1` probe with
identical collision size (`r=17 h=39 step=25`) is placed on each navigation
point and `ActorReachable` is called for every other navigation point within
1000 units. On retail UT99 this runs as a mutator through `StatLogFile`; on
SurrealEngine it runs as a headless driver. Both engines execute the same
UnrealScript, so a disagreement here is a native difference by construction.

| Map | Pairs | Agreement before | Agreement after | Retail reachable | Ours before | Ours after |
| --- | --- | --- | --- | --- | --- | --- |
| DM-Deck16][ | 11121 | 86.0% | **94.9%** | 17.9% | 31.6% | **19.4%** |
| DM-Curse][ | 10078 | — | **96.4%** | 13.1% | — | **13.7%** |

DM-Curse][ was never used while developing the fix.

The false positives that remain are consistent with the defect being the right
one: before the fix, pairs where the engines disagreed had a median separation
of 689 units against 364 for pairs they agreed on, and a median absolute height
difference of 89 units against 2.

The navigation graph itself was ruled out first. Both engines were made to dump
every node's `Paths`, `upstreamPaths`, `PrunedPaths`, `ExtraCost`, `bEndPoint`
and `bPlayerOnly`. They agree byte for byte on both maps (251 nodes / 925 edges
on Deck16, 165 / 554 on Curse), so map loading and reachspec deserialisation are
not involved.

Gameplay, 5 seeds × 16 skill 3 bots × 120 s on DM-Deck16][, fixed tick:

| Counter | Before | After | |
| --- | --- | --- | --- |
| environmental deaths | 76 | 29 | −62% |
| damage taken from the level | 7457 | 3216 | −57% |
| hit wall events | 26684 | 16114 | −40% |
| suicides | 80 | 35 | −56% |
| kills | 182 | 194 | +7% |
| navigation nodes visited | 2996 | 3435 | +15% |
| confirmed pickups | 55 | 63 | +15% |

Every one of the 5 seeds improved.

### Risks and non-goals

- **Cost.** Each call now runs up to 32 bounded iterations instead of 5
  unbounded ones, with one extra dry-run trace per iteration for the floor
  settle. `ActorReachable` is called frequently by bot routing. Measured in
  the corpus run, the full 11121-pair sweep takes 17 s including map load.
- **Still not exact.** 369 false positives and 204 false negatives remain
  against retail on Deck16. This narrows the gap; it does not close it.
- **`PointReachable` is untouched** and still disagrees with retail on 1778
  pairs. It is a separate correction and is deliberately not in this PR.
- **Non-walking physics untouched.** The `PHYS_Flying` / `PHYS_Swimming` branch
  has the same single-sweep shape but no floor to check against, so it is left
  alone.
- No regression test is included: there is no test harness in the repository to
  add one to. The evidence above is the differential corpus and the benchmark.

---

## Candidate 2: `pr/statlogfile`

**File:** `SurrealEngine/Native/NStatLogFile.cpp`

### The defect

All six `StatLogFile` natives are empty stubs, so any script writing through
`StatLogFile` silently produces nothing. UT99 uses it for match statistics.

### The fix

Implement `OpenLog`, `FileLog`, `FileFlush` and `CloseLog` against
`std::ofstream`, and have `CloseLog` rename `StatLogFile` to `StatLogFinal` the
way retail does. `GetChecksum` keeps returning a constant and `Watermark` stays
a no-op, both now with a comment saying why rather than a commented-out log
line.

Retail stores the archive in the `LogAr` int property, which cannot hold a
pointer on a 64 bit build, so open streams are tracked in a side table keyed by
object.

### Why it is worth having

Beyond match stats, it is the only file writer UnrealScript has in UT99. That
makes it the mechanism for running one instrumentation package unchanged on both
this engine and retail and comparing the results — which is how the evidence for
candidate 1 was produced.

### Risks

- **Script-controlled file paths.** The path comes from the `StatLogFile`
  property and is used as given, including `create_directories` on its parent.
  Retail behaves the same way, but this does let a package write where it asks
  to. If upstream wants it confined to a log directory, say so and it can be.
- Streams are keyed by `UObject*` in a process-lifetime map. An object destroyed
  without `CloseLog` leaks its entry until the process exits.
- `GetChecksum` returning `"0"` means a real UT99 stats server would reject the
  log as unsigned. That is unchanged from today's behaviour.

---

## Gate status

From `CLAUDE.md`:

| Gate | Candidate 1 | Candidate 2 |
| --- | --- | --- |
| 1. Problem exists on current `dpjudas/master` | Confirmed, code read at `2d47264c` | Confirmed, stubs read at `2d47264c` |
| 2. Reproduction and before/after | Corpus diff on 2 maps + 5-seed benchmark | Trivially observable: nothing is written |
| 3. Every changed line understood | Yes | Yes |
| 4. One correction, nothing unrelated | One file, one function | One file |
| 5. Regression test | Not practical, no harness upstream | Not practical, no harness upstream |
| 6. Risks documented | Above | Above |
| 7. Commits human-curated | **Awaiting your review** | **Awaiting your review** |
| Builds against upstream | Yes, `SurrealEngine` target | Pending |

## What is deliberately not proposed upstream

The rest of the work committed on `feature/bot-ai-quality-clean` is fork
tooling or unconfirmed, and none of it should be offered:

- **`SURREAL_MOVER_ACTOR_COLLISION_FIDELITY_CANDIDATE`** — five real collision
  corrections, but bundled behind an environment variable for A/B measurement.
  Upstream should be offered the individual corrections unconditionally, once
  each has its own evidence. Not yet.
- **`SURREAL_VISIBLE_COLLIDING_ACTORS_OCCLUSION_CANDIDATE`** — no retail oracle
  has confirmed what trace retail performs. Speculative.
- **Trace corpus driver, BotTelemetry mutator, benchmark and oracle tooling,
  and all `Docs/`** — fork infrastructure.
