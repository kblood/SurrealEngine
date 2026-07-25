# BOT AI architecture and evidence review, 2026-07-25

Read-only review of `feature/bot-ai-quality-clean` at `af9bf0f9`. No code, test,
or evidence file was changed.

## 0. Inspection scope and what could not be verified

Inspected: `CLAUDE.md`; `Docs/BOT_AI_PROJECT_GOAL.md`;
`Docs/BOT_AI_QUALITY_EXECUTION_PLAN.md` iterations 107–112 (lines 2680–2874);
`Docs/BOT_AI_CONTINUATION_PLAN_OPUS5.md`;
`Docs/BOT_AI_ROUTE_EXECUTION_FINDINGS.md`; `Docs/BOT_BENCHMARK_DRIVER.md`;
`Docs/BOT_AI_OWNER_DATA_EXTRACTION.md`; the native path-commit, inventory
direct-reach, and move-stall implementations in `SurrealEngine/UObject/`; the
telemetry/protocol/driver flag plumbing in `SurrealEngine/BotBenchmark/`; and
`Tools/BotBenchmark/` analyzers, comparators, gate files, and tests.

**Not verified.** The owner-local QA tree lives at
`C:\Devstuff\QuestGames\SurrealEngine\qa\` (see
`Tools/BotBenchmark/QualificationCampaigns/README.md:14`), outside this
worktree. `qa/reports/bot-ai/native-path-commit-observer-v2-full.json` and
`qa/runs/2026-07-25/native-path-commit-observer-v2/` were therefore not
readable in this session. Every statement below about those artifacts is a
statement about what the documents claim, not an independent check of the
JSON. The claimed SHA-256 values in iteration 112 (UT
`E40CC9D6…6696969F`, Unreal `802B9935…C2A0F047D`) remain unconfirmed by this
review.

## 1. Merge-readiness: no

The branch is not merge-ready as a BOT AI behavior improvement, and it is not
mergeable as a focused topic at all.

- **No behavior improvement exists to merge.** Every live candidate is
  rejected or retained default-off: targetless `MoveTo` timeout (rejected on a
  −71.0 damage and −0.67 kills regression,
  `BOT_AI_CONTINUATION_PLAN_OPUS5.md:78-94`), direct-actor `MoveToward` timeout
  (test hook only, `BOT_AI_ROUTE_EXECUTION_FINDINGS.md:206-237`), hazard swim
  egress, harmful-water egress steering, air-control steering, route-edge veto,
  and any `ActorReachable` return change. The project goal file states the same
  conclusion directly (`BOT_AI_PROJECT_GOAL.md:69-76`).
- **The branch is not a focused topic.** `git diff master...HEAD` is 625 files
  and 113,947 insertions across 462 commits, and includes the entire
  WebXR/WebGPU/`web/` workstream. `CLAUDE.md`'s upstream policy item 4 requires
  one correction per PR with unrelated work removed; this branch violates that
  by two orders of magnitude regardless of the BOT AI content's quality.
- **The campaign is deliberately unpassable.**
  `Tools/BotBenchmark/QualificationCampaigns/UT436-Unreal226b-qualification-campaign-v1.json:58-77`
  names three fail-closed release requirements
  (`role_swapped_participant_policy_coverage`, `avoidable_suicide_rate`,
  `ai_frame_p95_ms`) that the engine does not emit, and
  `UT436-tuning-quality-gates-v1.json:12-16` requires all three plus the two
  recovery fractions. Nothing on this branch can pass it today.

What *is* defensible on this branch is observer plumbing. Two things verified
in source support that:

- Native path-commit capture is at the claimed boundary: `SetRouteCache` at
  `SurrealEngine/UObject/UActor.cpp:4610`, the observer gate at 4611, and
  `SpecialHandling` not reached until 4689. It fails closed by dropping the
  record and incrementing an overflow counter on a node/edge mismatch, an
  out-of-range reachspec index, an endpoint disagreement, or a queue above 1024
  (4624–4662). The sequence counter only advances when the flag is on, so the
  off path is inert.
- Iteration 111's stock-baseline blocker is genuinely closed:
  `PickWallAdjust` now requires `IsBotBenchmarkFailedNavigationAvoidanceEnabled`
  (`UActor.cpp:4228`), pain-ledge and wall-adjust recovery are gated in
  `TickMoveTo` (`UActor.cpp:6473-6479`), and the navigation replan requires an
  explicit selector input (`PawnMoveStallWatchdog.cpp:72`).

### Defects found in the current slice

1. **`Analyze-NativePathCommits.py:231` hard-codes `"qualified": True`.** An
   observer-on run with zero committed paths produces an empty `counts` object
   and still reports qualified. The manifest-flag check at line 80 prevents a
   *stock* run being read as observer evidence, but not a *null* observer run.
   There is no minimum-coverage assertion anywhere in the analyzer.
2. **The path-commit analyzer test matrix iteration 111 required was not
   written.** `Tools/BotBenchmark/tests/test_analyze_native_path_commits.py`
   has three tests (accept, bad reachspec index, missing terminal-life field).
   Death context, hazard rising edge, respawn sequence, overflow, cache clear,
   and the base-rate/null comparison named at
   `BOT_AI_QUALITY_EXECUTION_PLAN.md:2812-2814` are all uncovered, while the
   death-context and hazard-entry code paths (`Analyze-NativePathCommits.py:202-218`)
   are the ones iteration 110's negative conclusion rests on.
3. **Path-commit records carry no life identity.** The record built at
   `UActor.cpp:4614-4623` has sequence, origin, costs, nodes, and edges but no
   `life_id`, while the water-egress, falling-hazard, and vertical-pain
   observers already serialize one
   (`SurrealEngine/BotBenchmark/BotBenchmarkTelemetry.cpp:327,368,507,639`).
   The path-commit stream therefore cannot be same-life bound today, which is
   exactly the property the next slice needs.
4. **The inventory direct-reach observer discards its own base rate.** It
   records only when `reached && resolvedWallSlide` (`UActor.cpp:4116`), only
   for inventory/ammo targets (4034–4040), and its record
   (`SurrealEngine/UObject/PawnInventoryReachability.h:45-59`) has no tick, no
   life, no caller origin, and identifies actors by name only. Iteration 107's
   "zero harmful witnesses in both anchors" is therefore a statement about a
   filtered tranche with an unmeasured denominator, not about
   `ActorReachable`'s safety.
5. **`WallAdjustCallCountValue` moved inside the experiment gate**
   (`UActor.cpp:4231`), so a stock run now reports zero wall-adjust calls. That
   is a silent change to a counter's meaning; it should be documented in the
   telemetry contract or the counter moved outside the gate.
6. **`PickWallAdjust` with the flag off takes neither branch** (4228 vs 4311),
   so `WallAdjustRecovery` is never reset for autonomous bots. Inert today
   because flags are immutable per run, but it needs a unit assertion.

## 2. Is the plan sound: yes in method, wrong in order

The method is sound and should not be relaxed. Specifically: exclusive terminal
partitions, exact counters reconciled against records, explicit overflow
accounting, two-repetition byte equivalence, default-off flags carried through
run identity and rejected by the comparator, and reporting `null` rather than
zero on incomplete evidence. The record of rejections
(`BOT_AI_PROJECT_GOAL.md:88-147`) is the strongest asset here — each one closes
a hypothesis with a negative witness rather than a hunch.

Three corrections to the plan as written:

- **`BOT_AI_CONTINUATION_PLAN_OPUS5.md` is stale relative to iterations
  107–112.** It sequences workstream 1 (the `HitWall` corner fixture) first,
  but that fixture is a prerequisite only for workstream 8, which the same plan
  puts last and which iteration 104 has blocked on an unidentified dispatch-time
  physics operand (`BOT_AI_CONTINUATION_PLAN_OPUS5.md:527-537`). Meanwhile the
  actual evidence trail has moved to direct-reach provenance, and iterations
  110–112 name a specific next slice the plan does not mention.
- **The plan does not carry iteration 111's audit findings as workstream
  entries.** Baseline restoration landed, but the path-commit analyzer test
  backfill and the base-rate assertion it required did not.
- **The plan under-weights coverage-of-zero as a failure mode.** Iteration 107's
  zero-witness result, the Unreal residence set's zero certified candidates
  (`BOT_AI_PROJECT_GOAL.md:128-136`), and the `qualified: True` default above
  are the same problem: an observer that sees nothing currently reads as an
  observer that passed.

### Revised sequence

| Step | Work | Rationale |
| --- | --- | --- |
| 1 | Direct-reach → command provenance observer (section 3) | The single "next permitted" step named by iterations 110, 111, and 112 and by `BOT_AI_ROUTE_EXECUTION_FINDINGS.md:161-162` |
| 1b | Backfill path-commit analyzer tests; replace `qualified: True` with an explicit coverage verdict | Closes iteration 111's outstanding requirement; zero behavior risk; runs in parallel with 1 |
| 2 | Workstream 5, in-engine `ai_frame_p95_ms` | Independent of every behavioral question; one of three fail-closed campaign requirements |
| 3 | Workstream 4, recovery clearance/replan fractions on natural opportunities | Two more required metrics; the forced fixture already passes both games |
| 4 | Workstream 1 (`HitWall` fixture) and workstream 3 (`SetEnemy` observer) | Read-only; both feed later work but neither unblocks a campaign metric |
| 5 | Workstreams 6, 7, 8 unchanged | As written |

Workstream 2 (the targetless-timeout follow-up) stays where the plan puts it,
but note it is a *closure* task: the honest cheap outcome is to close the
candidate on cross-game evidence, not to reopen it.

## 3. Smallest next observer-only slice

**Goal.** Establish, without inference, that a specific successful native
`ActorReachable` result and a specific later active direct movement command
belong to the same pawn life and the same target actor.

**Scope.** One new default-off flag, two record streams, one exact counter
group, one analyzer, one set of fixtures. Nothing else.

New flag `--botbench-direct-reach-command-provenance-observer=0|1`, default
false, plumbed exactly like `native_path_commit_observer_enabled`: engine state,
config identity, `manifest.json`, `summary.json.config`, the event envelope,
`Compare-BotBenchmarkRuns.py` config fields
(`Compare-BotBenchmarkRuns.py:45,250-254,292-295`), and `Analyze-BotQuality.py`
canonical identity (`Analyze-BotQuality.py:2910-2912,4211-4214`).

**Capture A — reach observation.** At the return of `UPawn::ActorReachable` for
a stock autonomous authority pawn in `PHYS_Walking` whose target is neither a
`UNavigationPoint` nor a `UPawn`. Emit for **both** true and false results; the
false results are the base rate and are not optional. This replaces the
`reached && resolvedWallSlide` filter at `UActor.cpp:4116` — `resolved_wall_slide`
becomes a recorded field, not an eligibility condition.

**Capture B — command activation.** At the existing per-tick route-execution
sample, when the pawn's `MoveTarget` is an actor with an open reach observation
for the **same** participant identity, **same** `life_id`, and **same** level
actor slot index, and the latent action is `MoveToward` or `MoveTo`. Emit
exactly one activation record per reach observation, then close it.

**Explicitly not in this slice.** No write to the reachability result, target,
route cache, acceleration, `MoveTimer`, physics, or latent state. No widening of
eligibility to pawns or navigation points. No policy. `ActorReachable` must
return the identical value with the flag on and off for identical input.

## 4. Mandatory default-off / fail-closed fields and test cases

### Reach observation record (`direct_reach_observations`)

`sequence` (contiguous per participant, ≥1) · `life_id` (the shared pawn-life
counter, ≥1; if the existing per-observer counters cannot be unified in this
slice, the record must carry both and the analyzer must reconcile them) ·
`tick` · participant identity (roster/PRI, never actor name) · `caller_origin`
enum (`script_actor_reachable`, `find_path_toward`, `path_special_handling`,
`find_random_dest`, `mark_reachable_nav_end_points`, `find_best_inventory_path`,
`other`) · `target_actor_index` (level slot, catalog-bindable),
`target_actor_name`, `target_class`, `target_kind` · `physics_mode` · `result` ·
`pawn_location`, `target_location` · `resolved_wall_slide`,
`walking_simulation_iterations` · the existing post-resolution support fields
(`walkable_support`, `support_fraction`, `support_normal_z`,
`harmful_foot_zone`, `harmful_below`, `outcome`) · `status` of `complete` or
`unavailable:<reason>` — never a silent skip.

### Activation record (`direct_reach_command_activations`)

`sequence` · `life_id` · `tick` · `reach_sequence` · `ticks_since_reach` ·
`latent_action` · `move_target_actor_index`, `move_target_name`,
`move_target_class` · `route_head_present`, `route_head_node` (separates a
direct command from a route-headed one — the distinction iteration 110 turned
on) · `link_status` ∈ {`same_life_exact`, `unavailable_life_boundary`,
`unavailable_target_replaced`, `unavailable_no_reach_record`,
`unavailable_overflow`} · exclusive terminal disposition ∈ {`target_reached`,
`command_changed`, `cleared`, `death`, `life_boundary_censor`,
`run_end_censor`}.

### Exact counters (monotone, complete group, reconciled)

`direct_reach_observations_exact` = `..._true_exact` + `..._false_exact`;
`direct_reach_command_activations_exact` = the sum of the terminal-disposition
subcounters; the `link_status` subcounters sum to the observations that had a
candidate; `direct_reach_observation_overflows_exact` and
`direct_reach_activation_overflows_exact` reported separately.

### Fail-closed analyzer rules

Reject unless the manifest flag is explicitly `true` (mirror
`Analyze-NativePathCommits.py:80-81`). Reject any non-zero overflow. Reject a
non-contiguous per-participant sequence. Reject any partition that does not
reconcile. Reject an activation whose `life_id` differs from its reach record's
— never link across a life boundary. Reject positive counters with an empty
record stream, and an empty stream with positive counters. Report `null`/
`unavailable`, never `0`, when the observer is off or the group is incomplete.
**Emit an explicit coverage verdict**: a zero-observation enabled run reports
`qualified: false` with a reason. Do not copy the `qualified: True` literal at
`Analyze-NativePathCommits.py:231`; fix it in the same slice.

### Mandatory test cases

Native/unit (`Tests/`):

1. Reach true → same-life `MoveToward` on the exact target slot → exactly one
   `same_life_exact` activation with `target_reached` or `command_changed`.
2. Reach true → death before any command → `unavailable_life_boundary`, no
   activation, disposition `life_boundary_censor`.
3. Reach true → next command targets a different actor → no activation, no link.
4. Reach true → same target *name*, different actor slot index (respawned
   pickup) → `unavailable_target_replaced`. Name matching must never link.
5. Reach **false** → recorded, and never linked even if that actor later becomes
   the move target.
6. Reach true → run ends before any command → `run_end_censor`.
7. Disjoint ineligibility, one case each: non-authority role, non-stock
   autonomous bot, non-walking physics, navigation-point target, pawn (combat)
   target, mover context.
8. Bounded-queue overflow increments the exact counter and truncates nothing
   silently.
9. Observer disabled → zero records, zero counters, no sequence advance, and
   `ActorReachable` returns a bit-identical result for the same input.
10. Flag-off `PickWallAdjust` leaves `WallAdjustRecovery` untouched (guards the
    `UActor.cpp:4228`/`4311` gap).

Analyzer (`Tools/BotBenchmark/tests/`):

11. Rejects an absent or false manifest flag.
12. Rejects non-contiguous sequence, non-zero overflow, unreconciled partitions,
    `life_id` mismatch, and empty-stream/positive-counter disagreement.
13. Base-rate/null control: an enabled run with zero eligible calls is
    `qualified: false`.
14. Respawn: `life_id` increments and the prior life's open reach observations
    close as `life_boundary_censor` rather than linking forward.
15. The same six cases retro-fitted onto `Analyze-NativePathCommits.py` (death
    context, hazard rising edge, respawn sequence, overflow, cache clear,
    base rate), closing `BOT_AI_QUALITY_EXECUTION_PLAN.md:2812-2814`.

## 5. UT436 and Unreal 226b qualification gates for this slice

Anchors, unchanged from iteration 112 so the evidence is comparable: UT436
`DM-Deck16][`, seed `104729`, skill 7; Unreal Gold 226b `DmDeathFan`, seed
`271828`, skill 3. 7,200 ticks, four bots, Release preset, observer-on and
observer-off, two repetitions each — eight runs total.

**Neutrality (both games, mandatory).**

- The two repetitions of each variant are byte-identical to each other.
- Observer-on and observer-off `events.jsonl` are byte-identical after excluding
  only `config_id` and the declared `direct_reach_command_provenance` envelope.
  Record the SHA-256 in the iteration entry, as iteration 112 did.
- Executable SHA-256, `game.manifest`, `build_preset`, environment allowlist,
  and dirty-tree state recorded. Artifacts under
  `qa/runs/<date>/direct-reach-command-provenance-v1/`, reports under
  `qa/reports/bot-ai/`. Repository-local `botbench-output/` is inadmissible.
- Zero overflow in all four enabled runs.
- Analyzer schema version bumped; `Compare-BotBenchmarkRuns.py` rejects an
  observer-on run as stock-equivalent.

**Coverage (per game, independently — a UT-only positive set is not a
cross-game witness).**

- At least one `same_life_exact` activation **and** at least one non-linked
  outcome, with the exclusive disposition partition reconciled against the exact
  counters. Unreal must clear this on its own evidence: the residence observer
  already produced zero certified candidates there
  (`BOT_AI_PROJECT_GOAL.md:128-136`), and the earlier inventory observer found
  zero harmful witnesses in both anchors. A second consecutive zero-coverage
  Unreal result closes the direct-reach hypothesis rather than deferring it.
- Every `target_actor_index` resolves against the v3 map catalog for that anchor
  (`Tools/BotBenchmark/Validate-MapCatalog.py`; anchor catalog hashes at
  `BOT_AI_OWNER_DATA_EXTRACTION.md:216-222`) and every participant against
  `Validate-RealizedBotCapabilities.py`.
- The known UT direct-command cases must appear: `enforcer13` at tick ≈5363 and
  `PAmmo1` at tick ≈7060 (`BOT_AI_ROUTE_EXECUTION_FINDINGS.md:195-198`), and the
  `BulletBox4` command that iteration 110 showed follows the historical
  `PathNode121 → PathNode123` commit (lines 155-162). An observer that misses
  these has not qualified.
- The existing quality analyzer passes on all eight runs.

**Explicitly not a gate.** Kills, deaths, suicides, damage, and hazard counts
must be *unchanged*. Any movement in them is a neutrality failure, not a result.
Held-out maps (`DM-Fractal`, `DM-Phobos`, `DmElsinore`, `DmRadikus`) stay
unopened.

## 6. Behavior changes that remain forbidden until that evidence exists

Until the slice above produces a non-zero, reconciled, same-life witness in
**both** games, and a causal counterfactual on top of it, all of the following
stay out:

- Any change to `ActorReachable`'s return value or to its walking simulation —
  support probe, `stepDownDelta`, wall-slide ordering, or return-to-walk-height
  order. The early local probe already regressed DeathFan from K7/D29/S22 to
  K12/D38/S26 (`BOT_AI_PROJECT_GOAL.md:118-124`).
- Route-edge veto, reachspec pin, route-cache clear policy, and route penalty
  (iteration 110; `BOT_AI_ROUTE_EXECUTION_FINDINGS.md:145-153`).
- Target override or redirection, direct-candidate acceleration, and
  harmful-water egress steering (iterations 63–65).
- Promotion of `direct_actor_move_toward_timeout_enabled` or
  `targetless_move_to_timeout_enabled` out of default-off.
- Air-control steering on the falling path — 2,080 bounded counterfactuals
  certified zero alternatives (`BOT_AI_PROJECT_GOAL.md:88-97`).
- The pre-launch pure forecast as a rollback or replan gate — it marked zero of
  eleven fatal launches harmful (lines 108-113).
- Any reachspec capability filter —
  `Tools/BotBenchmark/Analyze-ReachspecCapabilities.py` reports
  `selection_safe: false` and no live participant lacks a capability named by a
  reachspec on either anchor (`BOT_AI_OWNER_DATA_EXTRACTION.md:277-285`).
- Wall-jump forecasting, inventory-corridor enforcement, and the `bCanJump`
  wall-jump guard.
- Live `utility-arena` or `tactical-state` control, which additionally needs
  workstream 7.
- The shared walking `HitWall` dispatch correction, still blocked on the
  unidentified dispatch-time physics operand (iteration 104).
- Opening held-out maps, and any upstream PR, external post, or maintainer
  contact — `CLAUDE.md` requires an explicit request, and no candidate meets the
  reproduction, understanding, and validation gates.
