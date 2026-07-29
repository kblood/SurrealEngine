# BOT AI evidence review and continuation-plan revision, 2026-07-25

Read-only review of `feature/bot-ai-quality-clean` at `af9bf0f9`. No code, gate
file, flag default, or evidence artifact was changed. This replaces the earlier
same-day review at this path; findings that survived re-verification are kept,
the rest are corrected or added below.

## 0. What was verified, and what was not

Verified by reading this worktree: `CLAUDE.md`; `Docs/BOT_AI_PROJECT_GOAL.md`;
`Docs/BOT_AI_QUALITY_EXECUTION_PLAN.md` iterations 105–112 (lines 2625–2874);
`Docs/BOT_AI_CONTINUATION_PLAN_OPUS5.md`;
`Docs/BOT_AI_ROUTE_EXECUTION_FINDINGS.md`; `Docs/BOT_BENCHMARK_DRIVER.md`;
`Docs/BOT_AI_OWNER_DATA_EXTRACTION.md`; the native path-commit, inventory
direct-reach, and move-stall implementations in `SurrealEngine/UObject/`; the
flag and telemetry plumbing in `SurrealEngine/BotBenchmark/`;
`Tools/BotBenchmark/` analyzers, comparator, and tests; and the git history
including the gating commit `cdb9958e`.

**Not verified.** Admissible evidence lives in the owner-local QA tree at
`C:\Devstuff\QuestGames\SurrealEngine\qa\`
(`Tools/BotBenchmark/QualificationCampaigns/README.md:15`), outside this
worktree and outside this session's filesystem access.
`qa/reports/bot-ai/native-path-commit-observer-v2-full.json`, the per-run
`native-path-commit-observer-v2-*-r[12].json` reports, and
`qa/runs/2026-07-25/native-path-commit-observer-v2/` could not be read. The
counts and SHA-256 equivalence values recorded in
`BOT_AI_QUALITY_EXECUTION_PLAN.md:2843-2874` (UT `E40CC9D6…6696969F`, Unreal
`802B9935…C2A0F047D`) are therefore repeated here as claims, not as confirmed
measurements. No build, benchmark run, or test suite was executed for this
review; test invocation was not permitted in this session.

## 1. Merge readiness: no, on two independent grounds

**As a bot behavior change.** Nothing on the branch improves measured bot
quality. Every live candidate is rejected or retained default-off: the
targetless `MoveTo` timeout (−0.67 kills, −71.0 damage, death partition
unchanged — `BOT_AI_CONTINUATION_PLAN_OPUS5.md:78-94`); the direct-actor
`MoveToward` timeout (a qualified test hook with a single activation on one
map/seed — `BOT_AI_ROUTE_EXECUTION_FINDINGS.md:205-237`, iteration 106);
hazard-swim egress, harmful-water egress steering, air-control steering, the
route-edge veto, and every `ActorReachable` return change. The project goal
states the same conclusion (`BOT_AI_PROJECT_GOAL.md:69-76`). Iterations 108–112
add provenance and refutations only; iteration 112 says so in its own closing
line.

**As a focused topic.** `git diff --shortstat master...HEAD` is 626 files,
114,325 insertions, across 463 commits, and includes the entire WebXR/WebGPU
`web/` workstream. `CLAUDE.md`'s upstream policy item 4 requires one correction
per PR with unrelated work removed. Independently of BOT AI quality, this
branch cannot be an upstream candidate; a merge would have to be reconstructed
as a focused topic.

The campaign remains deliberately unpassable by design:
`UT436-Unreal226b-qualification-campaign-v1.json` names three release
requirements the engine does not emit (`role_swapped_participant_policy_coverage`,
`avoidable_suicide_rate`, `ai_frame_p95_ms`), and the tuning gate file requires
those plus the two recovery fractions. That is intended behavior of the gate,
not a defect, but it means no run on this branch can currently pass.

What is defensible as an integration-lane slice is the observer and analyzer
plumbing — one telemetry slice plus analyzer compatibility — after the defects
in section 2 are closed. It is not an upstream candidate: it reproduces no
upstream bug and corrects no upstream behavior.

## 2. Is the observer-only baseline sound?

Structurally yes. Three real defects and two smaller ones qualify that.

Sound, and verified in source:

- **Capture boundary is what iteration 108 asked for.** `CommitRoutePathCache`
  calls `SetRouteCache` first (`SurrealEngine/UObject/UActor.cpp:4610`), gates
  on the observer flag at `:4611`, and `SpecialHandling` is not reached until
  `:4689` via `PathSpecialHandling` (`:4673-4703`). The record is genuinely
  pre-redirection.
- **Fail-closed is double-locked.** A node/edge count mismatch, a deleted or
  classless node, an out-of-range reachspec index, an endpoint disagreement, or
  a queue above 1024 drops the record and increments an overflow counter
  (`UActor.cpp:4624-4662`). The analyzer rejects any participant with non-zero
  overflow on any tick (`Analyze-NativePathCommits.py:188-190`). Because the
  sequence is consumed *before* validation (`UActor.cpp:4615`), a dropped record
  also breaks the analyzer's contiguity check (`:105-107`). A silent gap is not
  representable — this is good design and should be the pattern for the next
  slice.
- **Stock runs cannot be misread as observer evidence.** The analyzer requires
  `native_path_commit_observer_enabled` to be explicitly `true`
  (`Analyze-NativePathCommits.py:80-81`); the flag is part of config identity
  and comparator protection (`Compare-BotBenchmarkRuns.py:44-45,250-254,292-295`)
  and of the analyzer's canonical identity
  (`Analyze-BotQuality.py:2910-2912`). Every committed edge is rebound to the
  immutable catalog by index, including `pruned` rejection
  (`Analyze-NativePathCommits.py:135-152`).
- **Iteration 111's named blocker is closed.** `PickWallAdjust` now requires
  `IsBotBenchmarkFailedNavigationAvoidanceEnabled` (`UActor.cpp:4228`);
  pain-ledge and wall-adjust steering are gated in `TickMoveTo`
  (`:6473-6479`); the navigation replan requires an explicit selector input
  (`PawnMoveStallWatchdog.cpp:71`, `PawnMoveStallWatchdog.h:33`).

### Defects

1. **`cdb9958e` changed experiment semantics, not only defaults.** The previous
   `!ApplyPainLedgeRecovery(delta) && !ApplyWallAdjustRecovery(delta)`
   short-circuited: a successful pain-ledge escape suppressed the wall-adjust
   call. The replacement evaluates the two operands independently
   (`UActor.cpp:6473-6479`), and both write `Acceleration()`
   (`:4385` and `:8305-8322`). With `harmful_zone_escape_enabled` and
   `failed_navigation_avoidance_enabled` both on, wall-adjust steering can now
   overwrite a pain-ledge escape that previously won. Default-off runs are
   unaffected, but prior evidence taken with both flags enabled is no longer
   reproducible by this build. Restore the short-circuit, or record the change
   as an explicit experiment-semantics revision with fresh paired evidence.

2. **Observation was folded into the behavior gate.**
   `WallAdjustCallCountValue++` now sits inside the experiment gate
   (`UActor.cpp:4228-4231`), and `ApplyPainLedgeRecovery` /
   `ApplyWallAdjustRecovery` are short-circuited away when their flags are off
   (`:6473-6478`), so a stock run measures none of these paths. Meanwhile
   `AdvanceWallAdjustRecovery` still runs ungated for autonomous bots and keeps
   writing `WallAdjustRecovery` and `WallAdjustRecoverySuccessCountValue`
   (`:6637-6647`). The stock baseline therefore advances a state machine whose
   arming site is disabled, and reports counters that no longer mean what their
   names say. The branch's own convention — a separate default-off *observer*
   flag, distinct from the *experiment* flag — should be applied here.

3. **`Analyze-NativePathCommits.py:231` hard-codes `"qualified": True`.** An
   enabled run with zero committed paths yields an empty `counts` object and
   still reports qualified. The manifest check at `:80-81` prevents a *stock*
   run being read as observer evidence but not a *null* observer run. There is
   no minimum-coverage assertion anywhere in the analyzer. This is the same
   failure mode as iteration 107's zero-witness tranche and the Unreal residence
   set's zero certified candidates (`BOT_AI_PROJECT_GOAL.md:128-136`): an
   observer that sees nothing currently reads as an observer that passed.

Smaller, non-fatal:

4. **`PickWallAdjust` with the flag off takes neither branch** (`:4228` vs
   `:4311`), so `WallAdjustRecovery` is never reset for autonomous bots. Inert
   today because flags are immutable per run; it needs a unit assertion rather
   than an argument.

5. **Iteration 111's audit scope was narrower than the code.** It named four
   paths. `UActor.cpp` has roughly two dozen
   `IsAutonomousPlayerBot(this)` / `IsStockAutonomousPlayerBot(this)` sites
   between `:4037` and `:8006`. Until each is individually classified as
   observe-only or flag-gated, "this branch is an observation-only stock
   baseline" is an assertion, not an audited fact. This is the cheapest
   remaining merge blocker.

6. **Contract drift.** `Docs/BOT_BENCHMARK_DRIVER.md:66-70` documents the
   driver's configuration flags but never mentions
   `--botbench-native-path-commit-observer` or
   `--botbench-inventory-direct-reach-support-observer`; the contract exists
   only in the execution plan. Separately, `CommitRoutePathCache` derives
   `cacheCapacity` from `RouteCache()` only when `ue1Version > 219`
   (`:4621`, mirroring `SetRouteCache` at `:4598`); on an older title every
   commit would serialize zero nodes with `truncated_by_route_cache` true and
   `cache_clear` false — a shape the analyzer rejects
   (`Analyze-NativePathCommits.py:125-126`). Harmless for UT436 and Unreal Gold
   226b, but it should be an explicit unsupported-version guard before the
   observer is pointed at any other title.

## 3. Is the native-path evidence sound?

As **observer-only provenance**, yes — conditional on the QA artifacts matching
their recorded description, which this review could not check. As **causal
evidence about bot failure**, it is sound precisely because it is negative, and
it must not be cited as a behavior improvement.

The strongest supported reading is iteration 110's: on UT436 Deck16-II the
repeated `PathNode121 → PathNode123` commit precedes the harmful entry by
114–116 ticks, but by entry time the live command is direct `BulletBox4`
movement and the route cache is stale context; on DeathFan only one of 31
hazard entries follows a non-empty commit within two seconds and none within 12
ticks (`BOT_AI_ROUTE_EXECUTION_FINDINGS.md:146-162`). That refutes the
route-edge hypothesis in both families. It authorizes nothing.

Three limits on how far the evidence reaches:

- **Coverage is structurally partial.** Records are emitted only from
  `CommitRoutePathCache`, reached from `FindPathToward` and
  `FindBestInventoryPath` (`UActor.cpp:5033-5137`). Direct `MoveToward` against
  an actor — the command actually live at the observed UT hazard entry — never
  passes through it. The observer cannot, by construction, see the decision the
  evidence points at. That is the argument for the next slice, and it should be
  stated as a coverage limitation rather than left implicit.
- **Death and hazard context are inferred, not recorded.** The analyzer detects
  a death from a `deaths_exact` increase and a hazard entry from a rising edge
  on `in_hazard_zone` (`Analyze-NativePathCommits.py:202-218`). Neither the
  route rows (`BotBenchmarkDriver.cpp:2204-2249`) nor the tick events carry a
  life ordinal, so "same life" is currently an inference from a counter edge.
  Iteration 111 forbids exactly that for the next slice
  (`BOT_AI_QUALITY_EXECUTION_PLAN.md:2816-2817`).
- **Test coverage does not match the claim.** Iteration 111 required the
  path-commit analyzer tests to exercise death context, hazard rising edge,
  respawn sequence, overflow, cache clear, and a base-rate/null comparison
  (`BOT_AI_QUALITY_EXECUTION_PLAN.md:2812-2814`).
  `Tools/BotBenchmark/tests/test_analyze_native_path_commits.py` contains three
  cases: exact catalog edge acceptance, unknown edge index rejection, and
  missing terminal-life provenance fields. The death-context and
  hazard-entry code paths that iteration 110's conclusion rests on are the
  least-tested part of the pipeline.

The iteration-107 inventory direct-reach observer is weaker still, and its
"zero harmful witnesses in both anchors" should not be read as a statement about
`ActorReachable`'s safety. It records only when `reached && resolvedWallSlide`
(`UActor.cpp:4116`), only for inventory/`Ammo` targets (`:4034-4040`,
`PawnInventoryReachability.h:65-66`), and its record
(`PawnInventoryReachability.h:45-59`) carries no tick, no life, no caller
origin, and identifies actors by name. Negative verdicts never reach the record
site at all: `ActorReachable` returns early at `:3924-3929` (distance),
`:4023` (`FastTrace`), and `:4027` (`CheckLocation`). It is a filtered tranche
with an unmeasured denominator.

## 4. Next highest-value slice

**Same-life `ActorReachable`-to-active-direct-command provenance, observer
only.** This is the slice iterations 110–112 name, and the source review
confirms it is the right one: the path-commit observer proved the route cache is
not the dangerous command, and the only remaining candidate producer of that
command is the direct reachability verdict. It cannot be reached by widening the
iteration-107 records, for the three reasons in section 3.

**One new default-off flag**,
`--botbench-direct-reach-command-provenance-observer=0|1`, plumbed exactly like
`native_path_commit_observer_enabled`: engine state (`Engine.h:205-212`
pattern), config identity, `manifest.json`, `summary.json.config`, the event
envelope, `Compare-BotBenchmarkRuns.py` config fields
(`:44-45,250-254,292-295`), and `Analyze-BotQuality.py` canonical identity
(`:2910-2912`).

**Capture A — reach observation.** At the return of `UPawn::ActorReachable` for
a stock autonomous authority pawn in `PHYS_Walking` whose target is neither a
`UNavigationPoint` nor a `UPawn`. Emit for **both** true and false results; the
false results are the base rate and are not optional. `resolved_wall_slide`
becomes a recorded field, not an eligibility condition — this replaces the
filter at `UActor.cpp:4116`.

**Capture B — command activation.** At the existing per-tick route-execution
sample, when `MoveTarget` is an actor with an open reach observation for the
same participant identity, the same `life_id`, and the same level actor slot
index, and the latent action is `MoveToward` or `MoveTo`. Emit exactly one
activation per reach observation, then close it.

**Not in this slice.** No write to the reachability result, target, route cache,
acceleration, `MoveTimer`, physics, or latent state. No widening of eligibility
to pawns or navigation points. No policy. `ActorReachable` must return a
bit-identical value with the flag on and off for identical input.

## 5. Required fail-closed fields, counters, and tests

### Shared life identity — the prerequisite

Promote a single per-pawn monotonic `life_id` and emit it on the reach record,
the activation record, and every per-tick participant row in
`route-execution.jsonl`. The primitive already exists as
`MoveStallRecoveryLifeId` (`UActor.cpp:6776-6782`,
`PawnMoveStallWatchdog.h:131,144`); unify it rather than adding a fourth private
counter beside `HazardSwimEgressLifeId` and `HarmfulZoneEscapeLifeId`
(`UActor.h:2555,2568,2632`). If unification cannot fit in this slice, both
counters must be carried and the analyzer must reconcile them. Same-life binding
must be a recorded fact, never a `deaths_exact` inference.

### Reach observation record (`direct_reach_observations`)

`sequence` (per participant, contiguous, consumed before validation so a drop is
detectable) · `life_id` · `tick` (recorded natively, not derived from the
emitting row) · participant identity (roster/PRI, never actor name) ·
`caller_origin` ∈ {`script_actor_reachable`, `path_special_handling`
(`UActor.cpp:4698`), `find_path_to_end_point` (`:4930`, `:4999`),
`find_best_inventory_path`, `find_random_dest`, `unknown`} — exhaustive, with
`unknown` fail-closed rather than a default · `result` ·
`reject_reason` ∈ {`null_actor`, `distance`, `navpoint_reachspec`, `trace`,
`check_location`, `walk_simulation`, `unsupported_physics`, `reached`} — no
`other` bucket · `target_actor_index` (level slot, catalog-bindable),
`target_actor_name`, `target_class`, `target_kind` · `physics_mode`,
`pawn_location`, `pawn_zone`, `target_location` · `resolved_wall_slide`,
`walking_simulation_iterations` · the existing post-resolution support fields
(`PawnInventoryReachability.h:36-59`) when the simulation ran, `unavailable`
when it did not · `status` of `complete` or `unavailable:<reason>`, never a
silent skip.

### Activation record (`direct_reach_command_activations`)

`sequence` · `life_id` · `tick` · `reach_sequence` · `ticks_since_reach` ·
`latent_mode` and the stall-watchdog command key
(`PawnMoveStallWatchdog.h:146-153`) · `move_target_actor_index`,
`move_target_name`, `move_target_class`, liveness · `route_head_present`,
`route_head_node` — this separates a direct command from a route-headed one,
the distinction iteration 110 turned on ·
`link_status` ∈ {`same_life_exact`, `unavailable_life_boundary`,
`unavailable_target_replaced`, `unavailable_no_reach_record`,
`unavailable_overflow`} · exclusive terminal disposition ∈ {`target_reached`,
`command_changed`, `cleared`, `death`, `life_boundary_censor`,
`run_end_censor`}.

### Exact counters

Monotone, complete, reconciled: `direct_reach_observations_exact` =
`..._true_exact` + `..._false_exact`; the `reject_reason` subcounters partition
`..._false_exact`; `direct_reach_command_activations_exact` = the sum of the
terminal-disposition subcounters; the `link_status` subcounters sum to the
observations that had a candidate. Overflow must be **split**:
`integrity_rejections_exact` and `queue_overflows_exact`, reported separately
for each stream. The current design increments one
`RoutePathCommitOverflowCountValue` for both conditions
(`UActor.cpp:4626,4634,4645,4651,4662`) — safe but uninformative; back-port the
split to the path-commit observer.

### Fail-closed analyzer rules

Reject unless the manifest flag is explicitly `true` (mirror
`Analyze-NativePathCommits.py:80-81`). Reject any non-zero overflow of either
kind. Reject a non-contiguous per-participant sequence. Reject any partition
that does not reconcile. Reject an activation whose `life_id` differs from its
reach record's — never link across a life boundary. Reject positive counters
with an empty stream, and an empty stream with positive counters. Report
`null`/`unavailable`, never `0`, when the observer is off or a group is
incomplete. **Emit an explicit coverage verdict**: a zero-observation enabled
run reports `qualified: false` with a reason. Do not copy the `qualified: True`
literal at `Analyze-NativePathCommits.py:231`; fix it in the same slice.

### Mandatory tests

Native (`Tests/`):

1. Reach true → same-life `MoveToward` on the exact target slot → exactly one
   `same_life_exact` activation with `target_reached` or `command_changed`.
2. Reach true → death before any command → `unavailable_life_boundary`, no
   activation, disposition `life_boundary_censor`.
3. Reach true → next command targets a different actor → no activation.
4. Reach true → same target *name*, different actor slot (respawned pickup) →
   `unavailable_target_replaced`. Name matching must never link.
5. Reach **false** → recorded, and never linked even if that actor later becomes
   the move target.
6. Reach true → run ends before any command → `run_end_censor`.
7. One case per `reject_reason`, including the three pre-simulation early exits
   at `UActor.cpp:3924-3929`, `:4023`, `:4027`.
8. A nested-call case: `SpecialHandling`'s re-check (`UActor.cpp:4698`)
   attributed to `path_special_handling`, not conflated with the
   script-originated call.
9. Disjoint ineligibility, one case each: non-authority role, non-stock
   autonomous bot, non-walking physics, navigation-point target, pawn target,
   mover context.
10. Queue exhaustion and integrity rejection move their two counters
    independently, and the sequence gap is observable.
11. Observer disabled → zero records, zero counters, no sequence advance, no
    write to reachability result, location, route, target, acceleration, latent
    state, or physics, and a bit-identical `ActorReachable` return.
12. Flag-off `PickWallAdjust` leaves `WallAdjustRecovery` untouched (guards the
    `:4228`/`:4311` gap from defect 4).

Analyzer (`Tools/BotBenchmark/tests/`):

13. Rejects an absent or false manifest flag.
14. Rejects non-contiguous sequence, non-zero overflow, unreconciled partitions,
    `life_id` mismatch, and empty-stream/positive-counter disagreement.
15. Base-rate/null control: an enabled run with zero eligible calls is
    `qualified: false`.
16. Respawn: `life_id` increments and the prior life's open observations close as
    `life_boundary_censor` rather than linking forward.
17. The six cases still owed to `Analyze-NativePathCommits.py` — death context,
    hazard rising edge, respawn sequence, overflow, cache clear, base rate —
    written **first**, because the new analyzer reuses that correlation code
    (`BOT_AI_QUALITY_EXECUTION_PLAN.md:2812-2814`).

## 6. Cross-game acceptance and stop conditions

Anchors unchanged from iteration 112 so the evidence is comparable: UT436
`DM-Deck16][`, seed `104729`, skill 7, `Botpack.Bot`; Unreal Gold 226b
`DmDeathFan`, seed `271828`, skill 3, `UnrealShare.Bots`. 7,200 ticks, four
bots, Release preset, observer-on and observer-off, two repetitions each —
eight runs.

**Neutrality, mandatory in both games.**

- The two repetitions of each variant are byte-identical to each other.
  Same-seed repeats are determinism checks, never independent samples.
- Observer-on and observer-off `events.jsonl` are byte-identical after excluding
  only `config_id` and the declared observer envelope. Record the SHA-256 in the
  iteration entry, as iteration 112 did.
- Executable SHA-256, `game.manifest`, `build_preset`, environment allowlist,
  and dirty-tree state recorded. Artifacts under
  `qa/runs/<date>/direct-reach-command-provenance-v1/`, reports under
  `qa/reports/bot-ai/`. Repository-local `botbench-output/` is inadmissible.
- Zero overflow of either kind in all four enabled runs; analyzer schema version
  bumped; `Compare-BotBenchmarkRuns.py` rejects an observer-on run as
  stock-equivalent.
- Kills, deaths, suicides, damage, and hazard counts are **unchanged**. Any
  movement in them is a neutrality failure, not a result.

**Coverage, per game independently — a UT-only positive set is not a cross-game
witness.**

- At least one `same_life_exact` activation and at least one non-linked outcome,
  with the exclusive disposition partition reconciled against exact counters.
- The linked harmful-outcome rate reported **with its denominator**, against the
  base rate over all reach observations in the same run. A raw count of
  harmful-outcome links is not a finding.
- Every `target_actor_index` resolves against the anchor's map catalog
  (`Tools/BotBenchmark/Validate-MapCatalog.py`) and every participant against
  `Validate-RealizedBotCapabilities.py`.
- The known UT direct-command cases must appear: `enforcer13` near tick 5363 and
  `PAmmo1` near tick 7060 (`BOT_AI_ROUTE_EXECUTION_FINDINGS.md:192-198`), and
  the `BulletBox4` command that iteration 110 showed follows the historical
  `PathNode121 → PathNode123` commit (`:154-162`). An observer that misses these
  has not qualified.
- The existing quality analyzer passes on all eight runs.

**Stop conditions.** Any one of these closes the slice as observer-only evidence
and forbids a behavior candidate derived from it:

- Unreal Gold produces zero `same_life_exact` activations, or its
  harmful-outcome link rate does not exceed its base rate. A one-sided witness
  cannot support a shared correction; this already sank the
  route-pin/target-override proposal (`BOT_AI_PROJECT_GOAL.md:127-136`). Given
  iteration 107's zero harmful witnesses in both anchors and the residence
  observer's zero certified Unreal candidates, a second consecutive
  zero-coverage Unreal result should **close** the direct-reach hypothesis, not
  defer it again.
- The linked harmful-outcome rate falls within the run-to-run spread of the base
  rate in either game.
- The observed direct commands are dominated by combat or latent-command
  ownership rather than by the reachability verdict — the competing explanation
  already flagged at `BOT_AI_PROJECT_GOAL.md:99-106`.
- Fewer than three independent map/seed instances identify one mechanism, the
  standing bar at `BOT_AI_ROUTE_EXECUTION_FINDINGS.md:82-86`.

Held-out maps (`DM-Fractal`, `DM-Phobos`, `DmElsinore`, `DmRadikus`) stay
unopened throughout.

## 7. Behavior changes that remain explicitly unauthorized

Until the slice above yields a non-zero, reconciled, same-life witness in **both**
games and a causal counterfactual on top of it:

- Any change to `ActorReachable`'s return value or to its walking simulation —
  support probe, `stepDownDelta`, wall-slide ordering, or return-to-walk-height
  order. The early local probe regressed DeathFan from K7/D29/S22 to K12/D38/S26
  (`BOT_AI_PROJECT_GOAL.md:115-124`); iteration 107 rejects a return-value change
  on its own evidence.
- Reachspec veto, route pin, route-cache clear policy, and route penalty
  (iterations 110–111, `BOT_AI_ROUTE_EXECUTION_FINDINGS.md:146-162`).
- Target override or redirection, direct-candidate acceleration, and
  harmful-water egress steering (iterations 63–65).
- Air-control steering on the falling path — 2,080 bounded counterfactuals
  certified zero alternatives (`BOT_AI_PROJECT_GOAL.md:88-97`) — and the
  pre-launch pure forecast as a rollback or replan gate, which marked zero of
  eleven fatal launches harmful (`:108-113`).
- Promotion of `direct_actor_move_toward_timeout_enabled` or
  `targetless_move_to_timeout_enabled` out of default-off benchmark experiments.
- Falling-seam translation, crease sweep, horizontal escape, wall-jump
  forecasting, inventory-corridor enforcement, and the `bCanJump` wall-jump guard
  (`BOT_AI_CONTINUATION_PLAN_OPUS5.md:576-598`).
- Live `utility-arena` or `tactical-state` control, which additionally needs
  workstream 7.
- The shared walking `HitWall` dispatch correction, still blocked on the
  unidentified dispatch-time physics operand (iteration 104,
  `BOT_AI_CONTINUATION_PLAN_OPUS5.md:527-537`).
- Widening direct-reach eligibility as a *behavior* change. Widening the
  *observed* population is authorized by this review; widening what the engine
  acts on is not.
- Any upstream PR, external post, or maintainer contact. `CLAUDE.md` requires an
  explicit request, and no candidate meets the reproduction, understanding, and
  validation gates.

## 8. Revision to the continuation plan

`Docs/BOT_AI_CONTINUATION_PLAN_OPUS5.md` remains correct in its verdict, its
fail-closed rules, and its cross-game obligations table. It is stale in one
respect only: it predates iterations 108–112 and has no entry for the provenance
chain they built. Proposed amendments, for the plan owner to apply:

1. Add **workstream 0, baseline integrity**, ahead of everything else: complete
   the write-audit of all `IsAutonomousPlayerBot` /
   `IsStockAutonomousPlayerBot` sites in `UActor.cpp` (defect 5); separate
   observer counters from experiment flags (defect 2); resolve the
   pain-ledge/wall-adjust ordering change (defect 1); replace the hard-coded
   `qualified: True` with an explicit coverage verdict (defect 3); land the six
   missing path-commit analyzer tests; and document the two observer flags in
   `Docs/BOT_BENCHMARK_DRIVER.md` (defect 6). None of this is optional before
   another observer is stacked on this baseline.
2. Insert the same-life reachability-to-command provenance slice (sections 4–6)
   as **workstream 4a**, between recovery timing and AI frame timing. It is the
   only workstream with a live, unrefuted causal hypothesis behind it.
3. Record in "What the newest evidence actually says" that the route-edge
   hypothesis is refuted in both games, so it is not revisited, and that the
   iteration-107 zero-witness result is a filtered tranche with an unmeasured
   denominator rather than a safety finding.
4. Restate workstream 2 as a *closure* task. The honest cheap outcome is to
   close the targetless-timeout candidate on cross-game evidence, not to reopen
   it.
5. Leave workstreams 1, 3, 5, 6, 7, and 8 as written. Workstream 1 (the `HitWall`
   corner fixture) remains the cheapest independent win, but note that it
   unblocks only workstream 8, which is itself blocked on iteration 104's
   unidentified dispatch operand — so it should not hold up workstream 0 or 4a.

No promotion, no flag default change, and no gate relaxation follows from this
review.
