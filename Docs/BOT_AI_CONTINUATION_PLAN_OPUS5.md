# Bot AI continuation plan

This plan sequences the next work on Unreal Tournament 436 and Unreal Gold
226b bot quality. It is a planning document only: it adds no runtime behavior,
relaxes no gate, and does not restate the iteration history already recorded in
`Docs/BOT_AI_QUALITY_EXECUTION_PLAN.md`.

## Objective, unchanged

1. Repair shared UE1 runtime fidelity until stock Botpack and `UnrealShare`
   bots stop failing for engine reasons.
2. Qualify read-only cross-game policy hooks against both games before any
   policy consumes them.
3. Eliminate avoidable suicides, wall running, and movement-intent stuck
   episodes, measured causally rather than by proxy.
4. Only then introduce opt-in live policy actions, per participant, against a
   repaired-stock baseline.

Maximum competence first. Difficulty tiers and human-like error remain a later
calibration layer.

## Current status: not ready

The branch is **not release-ready and not merge-ready as a bot behavior
change**, and nothing in this plan changes that judgment.

- Every live movement, hazard, wall-jump, seam, egress, and inventory-corridor
  candidate attempted so far has been rejected or retained as default-off
  experiment. The last live experiment (iteration 80, targetless `MoveTo`
  timeout) regressed combat while improving a stuck proxy.
- The checked-in campaign
  `Tools/BotBenchmark/QualificationCampaigns/UT436-Unreal226b-qualification-campaign-v1.json`
  is deliberately unpassable: its three `unrepresented_release_requirements`
  name metrics the engine does not emit. Recovery-time telemetry is now
  implemented but still needs fresh cross-game runtime evidence.
- Held-out maps (`DM-Fractal`, `DM-Phobos`, `DmElsinore`, `DmRadikus`) remain
  unopened and must stay unopened until tuning parameters freeze.
- The one shared correction with independent retail evidence — walking
  `HitWall` dispatch against `Pawn.MinHitWall` — is still blocked on a repaired
  native fixture and on boundary semantics that the oracle has bracketed but
  not proven.

### Fail-closed rules this plan preserves

These are constraints on every workstream below, not aspirations:

- Do not relax, rename, or delete any entry in the four gate files under
  `Tools/BotBenchmark/QualificationCampaigns/`. A metric is added by
  implementing honest telemetry, never by renaming a proxy.
- A missing, null, or unavailable metric fails the gate. `Evaluate-BotQualityGate.py`
  must keep failing closed on empty selectors and one-sided pairs.
- Default-off experiment flags (`failed_navigation_avoidance_enabled`,
  `harmful_zone_escape_enabled`, `hazard_swim_egress_*`,
  `falling_hazard_recovery_*`, `walking_preflight_positive_dps_veto_enabled`,
  `targetless_move_to_timeout_enabled`,
  `direct_actor_move_toward_timeout_enabled`) stay default-off and stay inside run
  identity, manifest, summary, matrix provenance, analyzer, and
  `Compare-BotBenchmarkRuns.py`. An enabled experiment is never compared as a
  stock-equivalent run.
- Observer work proves behavior neutrality by two-repetition byte equivalence
  and by baseline/candidate equivalence that ignores only the newly added
  `_exact` counters, `_diagnostics` payloads, and `output_directory`.
- Rendered or headless quality claims require owner-data runs written to the
  central QA tree under `qa/runs/<date>/<slice>/`. Repository-local build
  output remains inadmissible.
- Same-seed repeats are determinism checks, never independent samples.

## What the newest evidence actually says

The admissible iteration-80 Release matrix at
`qa/runs/2026-07-25/targetless-move-to-timeout-v1/ut-deck16-discovery-release-results`
ran 12 UT436 `DM-Deck16][` cases (four skill-7 bots, 1,800 ticks, seeds
104729/271828/314159, two repetitions, baseline `stock-timeout-disabled` versus
candidate `targetless-timeout-enabled`). All 12 completed and passed structural
analysis. Its six paired comparisons show, as candidate-minus-baseline means:

| Metric | Delta | Reading |
| --- | ---: | --- |
| `movement_intent_stuck_events_proxy` | −0.67 | 4 pair wins, 2 ties |
| `movement_intent_no_progress_seconds_proxy` | −0.71 s | 4 wins, 2 ties |
| `longest_movement_intent_no_progress_seconds_proxy` | −1.21 s | 4 wins, 2 ties |
| `kills_exact` | −0.67 | 0 wins, 2 losses |
| `match_score_delta` | −0.67 | 0 wins, 2 losses |
| `damage_dealt_to_other_participants_exact` | −71.0 | 179.3 → 108.3 per run |
| `hit_wall_events_exact` | +0.67 | 0 wins, 2 losses |
| `hazard_exposure_seconds` | +0.28 s | 6.52 → 6.81 |
| `hazard_entries` | +0.33 | 2.33 → 2.67 |
| `suicides_exact`, `environmental_deaths_exact`, `direct_self_kills`, `unassisted_environmental_deaths`, `ambiguous_deaths` | 0.00 | unchanged partition |

Four qualifying timeouts fired across the six candidate runs
(`move_stall_targetless_move_to_timeouts_exact` mean 0.67 versus 0 baseline).
So the flag did what it claimed, cost roughly 40% of measured
inter-participant damage, and left the death partition untouched. That is a
rejected live candidate, not a locomotion win.

Three further facts from the same artifacts shape the plan:

- `matrix-results.json` records `quality_gate_config: null` and
  `start_layout_id: null` with null layout fingerprints. This matrix is
  discovery evidence, not campaign evidence.
- `damage_efficiency_to_other_participants` is present for only four of six
  runs, so a paired gate with `min_pairs: 6` on that metric fails closed today.
- `confirmed_pickups_exact` is zero in all 12 runs and
  `navigation_coverage_union_fraction` is about 0.267 (roughly 67–70 of 251
  catalogued nodes). Resource acquisition and map coverage on Deck16-II are
  measurable but currently near-degenerate; they cannot yet carry a
  non-inferiority gate.

## Workstreams, in priority order

Each workstream states its dependencies, the likely code, test, and document
locations, and the evidence that closes it. Workstreams 1–6 are safe immediate
work: they are read-only, fixture-only, or measurement-only. Workstreams 7 and
8 are the two that eventually touch behavior, and both stay gated.

### 1. Repair the forced-corner walking `HitWall` fixture

**Objective.** Make `bot-walking-hitwall-corner-fixture` a deterministic,
two-contact native observation whose script dispatch is real, so the per-contact
notification contract can be qualified without owner-map luck.

**Why first.** It is cheap, it is the acknowledged blocker in front of the only
shared-fidelity correction with independent retail evidence, and it changes no
gameplay path.

**Depends on.** Nothing.

**Problem to fix.** `SurrealEngine/BotBenchmark/BotWalkingHitWallCornerFixture.cpp`
spawns a controlled bot, enables `EventName::HitWall`, forces a two-`BlockAll`
corner, and requires exactly two intercepted `HitWall` VM entries plus two
`PawnMovement::WalkingHitWallDispatchDiagnosticRecord` entries. Its first
owner-install run failed because the freshly spawned stock bot is not in a state
that defines a callable `HitWall` handler, and because the untouched native
walking loop recontacts the blocked geometry after the first pair. The
`VMCallHook` currently overrides the result of whatever it intercepts, which
proves interception, not dispatch to a real handler.

**Work items.**

- In `BotWalkingHitWallCornerFixture.cpp`, place the fixture pawn into a stock
  state that genuinely defines `HitWall` for both target games before the
  native tick. `FindAir` is the intended candidate because it exists in
  `Botpack.Bot` and in `UnrealShare.Bots` and is reachable without a live
  navigation target. Resolve the handler by reflection first and fail closed
  with an explicit reason if the selected state does not define `HitWall` for
  the loaded package set; never assume the state by name alone.
- Restore the entry state, physics, acceleration, velocity, collision flags,
  and event-enable flags on every exit path, as the current teardown already
  does for `AccelRate`, `DesiredSpeed`, blocking, `HitWall`, and `Bump`.
- Bound the observation to exactly the primary forward contact and its aligned
  slide. Either stop after the second `PawnMovement::WalkingHitWallContactPhase`
  record or reject the run explicitly when a third raw contact appears; do not
  silently truncate.
- Keep the fixture's `Bump` suppression and its dynamic `BlockAll` geometry,
  and keep the existing exact assertions on blocker identity, contact phase,
  and unchanged `PHYS_Walking`.
- Add a matching Unreal Gold 226b profile invocation so the same fixture runs
  on `DmDeck16` or `DmMorbias` with `UnrealShare.DeathMatchGame`. Do not reuse
  UT's `Botpack.DeathMatchPlus` assumptions; route it through
  `SurrealEngine/BotBenchmark/BotBenchmarkGameProfile.cpp` and
  `BotControlledMatch.cpp` like every other cross-game path.

**Likely locations.** `SurrealEngine/BotBenchmark/BotWalkingHitWallCornerFixture.{h,cpp}`;
`SurrealEngine/UObject/PawnWalkingHitWallDispatch.{h,cpp}`;
`SurrealEngine/BotBenchmark/BotControlledMatch.cpp`;
`SurrealEngine/BotBenchmark/BotBenchmarkGameProfile.cpp`;
`SurrealEngine/Runtime/HeadlessDriver.cpp`; tests in
`Tests/PawnWalkingHitWallDispatchTests.cpp` and `Tests/HeadlessDriverTests.cpp`.

**Acceptance evidence.**

- UT436 and Unreal 226b fixture runs each report `Passed`, exactly two
  intercepted VM dispatches into a real state handler, exactly two diagnostics
  with phases `primary_forward` then `aligned_slide`, preserved blocker
  identity, and unchanged physics.
- Two repetitions per game produce identical fixture result text, including
  candidate counters and the selected `PlayerStart`/heading.
- A negative control: a fixture invocation whose state has no `HitWall`
  handler fails with the explicit reason rather than passing on zero
  dispatches.

**Out of scope.** No change to the `TickWalking` dispatch predicate. That is
workstream 8.

### 2. A proper paired follow-up to the targetless `MoveTo` timeout

**Objective.** Either explain the iteration-80 combat regression well enough to
reopen the candidate, or close it permanently with cross-game evidence. The
flag stays default-off throughout.

**Why second.** It is the newest live experiment, the analysis above shows the
mechanism is real but harmful, and the discovery harness it needs is reused by
workstreams 4 and 6.

**Depends on.** Nothing for the fixture; the UT and Unreal paired matrices need
only existing tooling.

**Work items.**

- Reproduce the exact qualifying timeout in a pure fixture: a detected,
  targetless walking `MoveTo` with finite state and a positive `MoveTimer`,
  plus the disjoint negative cases (`MoveToward`, strafing, falling, swimming,
  pain-zone, mover context, non-finite state, non-positive timer). Assert both
  the acceleration clear and the latent timeout, and assert that no route,
  physics, or script state changes.
- Instrument the causal chain the current counters cannot show: for each fired
  timeout, record the elapsed no-progress at fire time, the script's next
  selected command key, whether the pawn moved within one second, and whether
  the same command key returned. A timeout that hands control back to a state
  which reissues the same target is not a recovery.
- Run paired discovery matrices for both games at three seeds and two
  repetitions: UT436 `DM-Deck16][` (repeat of the admissible configuration) and
  Unreal Gold 226b `DmDeathFan` plus `DmDeck16`. Unreal has never been paired
  for this flag at all; that alone blocks any promotion claim.
- Analyze the combat regression directly rather than by inference. The 179.3 →
  108.3 drop in `damage_dealt_to_other_participants_exact` and the +0.67
  `hit_wall_events_exact` are the falsifiable claims: did clearing acceleration
  break an in-progress engagement or aim, or did it reroute bots into
  additional wall contact?

**Likely locations.** `SurrealEngine/UObject/PawnMoveStallWatchdog.{h,cpp}`;
`SurrealEngine/BotBenchmark/BotBenchmarkDriver.cpp`;
`SurrealEngine/BotBenchmark/BotBenchmarkTelemetry.{h,cpp}` and
`BotBenchmarkProtocol.{h,cpp}`; `Tests/PawnMoveStallWatchdogTests.cpp` and
`Tests/BotBenchmarkTelemetryTests.cpp`; `Tools/BotBenchmark/Analyze-BotQuality.py`
with `Tools/BotBenchmark/tests/test_analyze_bot_quality.py`; artifacts under
`qa/runs/<date>/targetless-move-to-timeout-v2-*/`.

**Acceptance evidence.**

- The fixture reproduces the qualifying case and every disjoint rejection.
- Both games have complete paired matrices with two-repetition byte
  equivalence per variant.
- Promotion to the repaired-stock baseline requires, in both games: no
  regression in `kills_exact`, `match_score_delta`,
  `damage_dealt_to_other_participants_exact`, `hit_wall_events_exact`,
  `hazard_entries`, or `hazard_exposure_seconds`, and a measured reduction in
  movement-intent stuck episodes. Anything less closes the candidate.

**Out of scope.** Steering, route selection, and failed-node memory. The
retained recovery remains the script-owned latent timeout.

### 3. Integrate the portable `SetEnemy` observer

**Objective.** Turn `BotTargetSelectionProbeTracker` from unused infrastructure
into a qualified, read-only, cross-game target-acquisition observer.

**Why third.** `Docs/BOT_AI_ARCHITECTURE.md` makes this the gate for any future
target-selection policy, and the tracker and contract validator already exist
and are tested. Only the runtime integration and its equivalence evidence are
missing.

**Depends on.** Nothing. It must not depend on any live policy.

**Current state.** `SurrealEngine/BotBenchmark/BotTargetSelectionHookContract.{h,cpp}`
validates the reflected `SetEnemy(Pawn NewEnemy) -> bool` shape, and
`BotTargetSelectionProbeTracker.{h,cpp}` implements bounded outermost-call
scope tracking with `AcceptedTargetChange`, `AcceptedSameTarget`, and
`RejectedOrUnchanged` outcomes plus capacity and ordering statuses. Neither is
referenced by `BotBenchmarkDriver.cpp`; the only consumers are
`Tests/BotTargetSelectionHookContractTests.cpp` and
`Tests/BotTargetSelectionProbeTrackerTests.cpp`.

**Work items.**

- Validate the reflected signature for every dispatched `SetEnemy`
  implementation in the loaded package set before installing the hook, for
  `Botpack.Bot` and for `UnrealShare.Bots` including state overrides that
  recursively call the global implementation. Any unexpected shape disables the
  observer for the run and records why; it never guesses.
- Bind the tracker at the outermost call per bot only, recording candidate,
  return value, and before/after `Enemy` identity. Never replace arguments,
  write `Enemy`, synthesize perception events, or select a policy.
- Emit a complete, monotonic exact counter group plus a bounded record stream
  with explicit overflow accounting, following the pattern already used by the
  hazard and falling observers. Reconcile counters against records in the
  analyzer and reject partial groups.
- Add an explicit default-off selection flag carried in the command line, run
  identity, manifest, summary, matrix provenance, analyzer, and
  `Compare-BotBenchmarkRuns.py`, so an observer run is never compared as
  stock-equivalent by accident.

**Likely locations.** `SurrealEngine/BotBenchmark/BotTargetSelectionHookContract.{h,cpp}`;
`BotTargetSelectionProbeTracker.{h,cpp}`; `BotBenchmarkDriver.{h,cpp}`;
`BotBenchmarkTelemetry.{h,cpp}`; `BotBenchmarkProtocol.{h,cpp}`;
`Tests/BotTargetSelectionProbeTrackerTests.cpp`,
`Tests/BotBenchmarkTelemetryTests.cpp`, `Tests/BotBenchmarkProtocolTests.cpp`;
`Tools/BotBenchmark/Analyze-BotQuality.py`,
`Tools/BotBenchmark/Compare-BotBenchmarkRuns.py` and their tests;
`Docs/BOT_AI_ARCHITECTURE.md` and `Docs/BOT_BENCHMARK_DRIVER.md` for the
contract description.

**Acceptance evidence.**

- Observer-on versus observer-off equivalence for both games: UT436
  `DM-Deck16][` and `DM-Morbias][`, Unreal Gold 226b `DmDeathFan` and
  `DmDeck16`, two repetitions each, exact across all artifacts after ignoring
  only the new counters, the new record stream, and `output_directory`.
- Non-trivial positive coverage: at least one run per game records both an
  accepted target change and a rejected-or-unchanged outcome, with nested
  state-override calls attributed to a single outermost scope.
- A rejected-signature negative control disables the observer and records the
  reason rather than emitting partial data.

**Out of scope.** Any live target selection, aim, or fire authorization.

### 4. Recoverable-movement clearance and recovery timing

**Objective.** Emit the two metrics the campaign already requires:
`recoverable_movement_episode_clear_within_2s_fraction` and
`recoverable_movement_episode_clear_or_replanned_within_5s_fraction`.

**Implementation status.** The native episode model, exact terminal counters,
bounded terminal records, benchmark lifecycle flushing, telemetry serialization,
fail-closed analyzer validation, and a native cross-game forced-stall fixture
are implemented. The fixture passed twice on both UT436 `DM-Deck16][` and
Unreal Gold 226b `DmDeathFan`, producing one detection and one
`ClearedWithin2Seconds` terminal record per run. The campaign continues to
require the two derived metrics; a run with no qualifying episode, incomplete
telemetry, or a record overflow reports null and fails its gate.

**Why now.** These were two of the four fail-closed release requirements, they
correspond directly to the plan's stated navigation-safety gate, and the stall
watchdog already owns the episode identity they need.

**Depends on.** Independent of workstreams 1 and 3; shares telemetry plumbing
with workstream 2 and should be built alongside it.

**Work items.**

- Define a recoverable movement episode from the existing stable command key:
  same actor target, or same positional destination, and same latent mode.
  Start on measured no-progress under movement intent; end on measured
  displacement, on a semantically different command key, on death, or on life
  boundary.
- Record per-episode elapsed time to clearance and, separately, elapsed time to
  a genuinely different replan. A reissue of the same command key is neither
  clearance nor replan; iteration 27 already proved that treating it as one
  hides the failure.
- Classify intentional stops out of the denominator with evidence, not
  heuristics: lift waits, aiming, ambush, and match state. An episode whose
  classification is unknown stays unknown and is reported, never silently
  excluded.
- Derive both fractions in the analyzer with an exact partition of episodes
  into cleared-within-2s, cleared-or-replanned-within-5s, still-open at run
  end, censored, and unknown. Report null when the group is incomplete.

**Likely locations.** `SurrealEngine/UObject/PawnMoveStallWatchdog.{h,cpp}`;
`SurrealEngine/BotBenchmark/BotBenchmarkQualityObservation.{h,cpp}`;
`BotBenchmarkTelemetry.{h,cpp}`; `Tests/PawnMoveStallWatchdogTests.cpp`,
`Tests/BotBenchmarkQualityObservationTests.cpp`;
`Tools/BotBenchmark/Analyze-BotQuality.py`,
`Tools/BotBenchmark/Evaluate-BotQualityGate.py` and their tests.

**Acceptance evidence.**

- Pure fixtures for clearance, same-key reissue, genuine replan, permanent
  stall, censored run boundary, and each intentional-stop classification.
- The native forced-stall fixture passes twice in each game with one detection,
  one clearance, a reconciled terminal record, and no overflow. This is now
  satisfied on Deck16-II and DeathFan.
- Both metrics present and reconciled in fresh natural-opportunity UT436 and
  Unreal 226b runs, with the known Deck16-II `LiftExit3`-style stall and the
  known DeathFan stall each landing in the expected bucket.
- Observer neutrality proven by paired equivalence ignoring only the new
  fields.

### 5. In-engine AI frame timing

**Objective.** Emit `ai_frame_p95_ms` from inside the engine so the 2 ms p95
target for 16 bots can be evaluated rather than approximated by launcher wall
time.

**Why now.** It is a fail-closed release requirement, it is independent of every
behavioral question, and it must exist before a 16-bot campaign matrix is worth
running.

**Depends on.** Nothing. It should be measured on the repaired-stock baseline
first so later policy work has a reference distribution.

**Work items.**

- Instrument the bot-relevant per-frame work only: policy/observation building,
  navigation and tactical queries, hazard and falling observers, and the
  benchmark driver's own per-tick sampling. Do not fold renderer, audio, or
  package loading into the number.
- Accumulate per-frame totals with a bounded, deterministic reservoir or exact
  histogram; a fixed-step headless run must produce the same distribution on a
  repeat. Emit p50, p95, p99, max, and sample count, not p95 alone.
- Report per-run and per-bot-count context so a 4-bot discovery run is never
  compared against the 16-bot target.

**Likely locations.** `SurrealEngine/BotBenchmark/BotBenchmarkDriver.cpp`;
`SurrealEngine/Runtime/DeterministicRuntime.{h,cpp}`;
`SurrealEngine/BotAI/BotPolicyObservationBuilder.cpp`;
`BotBenchmarkTelemetry.{h,cpp}`; `Tests/DeterministicRuntimeTests.cpp`,
`Tests/BotBenchmarkTelemetryTests.cpp`;
`Tools/BotBenchmark/Analyze-BotQuality.py` and its tests;
`Docs/BOT_BENCHMARK_DRIVER.md`.

**Acceptance evidence.**

- Repeat runs produce identical sample counts and identical or documented
  bounded-variance percentiles; timing values are excluded from byte-equivalence
  comparisons by an explicit, audited ignore rather than by weakening the
  comparator's rules.
- A 16-bot UT436 and a 16-bot Unreal 226b run each report the full percentile
  group with no unbounded per-bot actor or navigation scan observed.

### 6. Causal avoidable-suicide rate

**Objective.** Emit `avoidable_suicide_rate` as an honest causal metric, or
prove it cannot yet be emitted and keep the gate failing closed.

**Why sixth.** It is the hardest truth problem on the list and depends on
observers that already exist. Rushing it produces exactly the relabeled proxy
the campaign authority forbids.

**Depends on.** The hazard death partition (iteration 66) and the falling,
water-egress, and parity observers, all of which already emit terminal records.

**Work items.**

- Define avoidability as a property of the pre-entry decision, not of the
  death. A death is avoidable only when the pawn had movement intent, a known
  safe alternative existed at decision time, the harmful outcome was
  predictable from evidence the bot could have had, and no external impulse or
  enemy contribution intervened. Everything else is unknown or excluded.
- Build the metric on `hazard_death_partition_records` plus the existing
  five-way attribution. Direct enemy kills, recent-enemy-contributed and
  momentum-contributed environmental deaths, and ambiguous deaths are excluded
  by construction; iterations 68 and 69 already showed the observed Deck
  `aligned_continuation_commit` and DeathFan `external_impulse_commit` sources
  have no proven bot-command lead.
- Emit an exact partition — avoidable, unavoidable, excluded, unknown — with the
  rate computed only over avoidable plus unavoidable. Publish the unknown
  fraction alongside the rate; a rate derived from a mostly unknown population
  is not evidence. Iteration 54's 97.94% unknown generations is the cautionary
  precedent.
- If the classifier cannot label the known reproducible Deck16-II and DeathFan
  deaths without guessing, report the metric as unavailable with its evidence
  requirement and leave the gate failing. That is a successful outcome for this
  workstream, not a failure.

**Likely locations.** `SurrealEngine/BotBenchmark/BotBenchmarkHazardDeathPartition.{h,cpp}`;
`BotBenchmarkDeathAttribution*.{h,cpp}`;
`SurrealEngine/UObject/PawnHazardWaterEgressObserver.cpp`;
`SurrealEngine/UObject/PawnFallingHazardRuntimeObserver.cpp`;
`Tests/BotBenchmarkHazardDeathPartitionTests.cpp`,
`Tests/BotBenchmarkDeathAttributionTests.cpp`;
`Tools/BotBenchmark/Analyze-BotQuality.py` and its tests.

**Acceptance evidence.**

- Frozen positive and negative controls: the reproducible Deck16-II unassisted
  `PainTimer` water death, the DeathFan unassisted environmental deaths, the
  DeathFan enemy kills that happen while swimming, and the four frozen safe
  landings from iteration 53. The enemy kills and safe landings must never be
  labelled avoidable.
- Analyzer rejection of any partition that does not reconcile with
  `deaths_exact`.

### 7. Per-participant policy binding and role swap

**Objective.** Make `role_swapped_participant_policy_coverage` implementable by
binding policy to immutable roster indexes inside one executable, in both
games.

**Why seventh.** It is the largest remaining architectural gap and the true
precondition for any opt-in live policy. Iteration 60 established observed
start-layout coverage but stated plainly that policy selection is process-wide
and no stable per-slot binding exists.

**Depends on.** Workstream 3 for the read-only cross-game hook pattern, and on
workstreams 4–6 for the metrics that would judge a swapped match.

**Work items.**

- Add a narrow per-participant action adapter keyed by immutable roster index.
  Never a global switch, and never selection by build order or global mutable
  state; `Docs/BOT_AI_ARCHITECTURE.md` already requires immutable policy
  identity and version as benchmark inputs.
- Prove balanced slot assignment: the same policy set occupies mirrored slots
  across paired runs, verified against the actual roster rather than the
  requested one, in UT436 (`Botpack.Bot`, skills 0–7) and Unreal Gold 226b
  (`UnrealShare.Bots`, skills 0–3, `ReSetSkill`, automatic-roster contract).
- Emit coverage as an exact assertion, not a boolean claim: which slot ran
  which policy, in which repetition, and whether the assignment set is
  balanced. A collapsed or ambiguous assignment fails closed, matching the
  existing distinct-start-layout rule.
- Keep every enhanced policy on the same perception, path follower, hazard
  service, aim controller, and operation budgets.

**Likely locations.** `SurrealEngine/BotAI/BotPolicyRegistry.{h,cpp}`;
`BotPolicyShadow.{h,cpp}`; `SurrealEngine/BotBenchmark/BotBenchmarkRoster.{h,cpp}`;
`BotControlledMatch.cpp`; `BotBenchmarkGameProfile.cpp`;
`Tests/BotPolicyRegistryTests.cpp`, `Tests/BotBenchmarkRosterTests.cpp`,
`Tests/BotBenchmarkGameProfileTests.cpp`;
`Tools/BotBenchmark/Run-BotBenchmarkMatrix.py`,
`Tools/BotBenchmark/Analyze-BotQuality.py` and their tests;
`Docs/BOT_AI_ARCHITECTURE.md`, `Docs/BOT_AI_CROSS_GAME_AND_MAPS.md`.

**Acceptance evidence.**

- A single executable runs a mirrored-slot pair in each game with deterministic
  repeats and a balanced assignment assertion.
- Shadow-only binding first: with every policy in observe mode, a bound run is
  artifact-equivalent to an unbound run.
- Held-out maps stay closed; role-swap proof is a tuning-lane result.

### 8. Shared walking `HitWall` dispatch correction

**Objective.** Decide the `Pawn.MinHitWall` dispatch question with a repaired
fixture and calibrated boundary evidence, then either implement the shared
correction under the iteration-71 constraints or record it as unresolved.

**Why last.** It is the only shared-fidelity change with independent retail
evidence in both games, and it is also the one most likely to move gameplay. It
must land after the fixture is trustworthy and after the measurement gaps close,
so a regression is visible rather than inferred.

**Depends on.** Workstream 1 (fixture), workstreams 4–6 (metrics that can judge
the result), and the remaining oracle calibration.

**Work items.**

- Close the calibration gap. Iteration 79 pinned `PlayerStart`, direction, and
  contact signature and bracketed suppression at `-0.397` against dispatch at
  `-0.350` in both games; iteration 75 narrowed a flat contact to `-0.397`
  versus `-0.395`. An adjacent, mixed-outcome microthreshold bracket is still
  required, and the runner must keep reporting the observed interval rather
  than asserting `<` versus `<=`.
- Implement the correction as notification-only. The generic walking path keeps
  its legacy Z-band aligned slide, second `TryMove`, time accounting, and
  no-backwards check even when a glancing contact is rejected for notification.
  A threshold-accepted non-legacy contact must not acquire a new slide path.
  Accepted callbacks still run before the slide. The `UPlayerPawn`
  pushable-decoration path is untouched.
- Preserve mover ordering exactly as the iteration-76 oracle proved:
  `hitwall_pre` → `handle_door_pre` → mover `HandleDoor` → `handle_door_post` →
  `pick_wall_adjust_skipped`.

**Likely locations.** `SurrealEngine/UObject/UActor.cpp` (`TickWalking`);
`SurrealEngine/UObject/PawnWalkingHitWallDispatch.{h,cpp}`;
`Tests/PawnWalkingHitWallDispatchTests.cpp`, `Tests/ActorMovementTests.cpp`,
`Tests/ActorMoveCollisionProbeTests.cpp`;
`Tools/BotBenchmark/RetailHitWallOracle/{UT,Unreal}/Classes/*.uc` and
`Tools/BotBenchmark/Run-RetailMinHitWallOracle.ps1`.

**Acceptance evidence.**

- The six pure fixtures required by iteration 71: head-on vertical contact
  notifying and sliding; glancing vertical contact not notifying with unchanged
  slide; threshold-accepted non-legacy contact notifying without a new slide;
  exact threshold boundary; state-adjusted threshold; invalid geometry.
- The runtime fixture from workstream 1, proving a rejected glancing collision
  preserves location, remaining time, and second-slide outcome, and that
  first and second contacts emit independent telemetry.
- Paired UT436 `DM-Deck16][` and Unreal 226b `DmDeathFan` matrices with
  deterministic repeats and no regression in the death partition, hazard
  entries, wall contacts, stuck episodes, or combat metrics.
- Retail installs byte-identical before and after every oracle run.

## Safe now versus blocked

**Safe to start immediately** — read-only, fixture-only, or measurement-only,
with no gameplay path affected: workstreams 1, 2 (fixture and paired
measurement portions), 3, 4, 5, and 6, plus the shadow-only binding stage of
workstream 7.

**Blocked or premature, and to remain so:**

- Any live falling-seam translation, crease sweep, or horizontal escape. Every
  variant was rejected on causal safety evidence; the iteration-45 and
  iteration-47 observers authorized zero candidates on every known failure.
- Harmful-water egress steering and candidate-target acceleration. Iterations
  63 through 65 falsified both the direct navigation candidate and the static
  walk certificate as escape routes; entry distances did not improve before
  death.
- Wall-jump forecasting, inventory-corridor enforcement, and the `bCanJump`
  wall-jump guard. All regressed match-level quality; their pure helpers stay,
  their live paths stay out.
- Promotion of the targetless `MoveTo` timeout to the repaired-stock baseline.
  It stays default-off until workstream 2 closes.
- The failed-navigation route penalty. It stays benchmark-opt-in and
  default-off, with `failed_navigation_avoidance_enabled` carried through run
  identity.
- Live `utility-arena` and `tactical-state` control. Blocked on workstream 7
  and on the full metric set; the shadow stream is still not gameplay evidence.
- Held-out maps. Closed until tuning freezes.
- Any upstream PR, external post, or maintainer contact. `CLAUDE.md` requires an
  explicit request, and no candidate currently meets the reproduction,
  understanding, and validation gates.

## Cross-game obligations

No workstream is complete on UT436 alone.

| Workstream | UT436 obligation | Unreal Gold 226b obligation |
| --- | --- | --- |
| 1 fixture repair | `DM-Deck16][` forced corner, `Botpack.Bot` state handler | `DmDeck16`/`DmMorbias` forced corner, `UnrealShare.Bots` state handler |
| 2 targetless timeout | repeat the three-seed Deck16-II paired matrix | first-ever paired matrix on `DmDeathFan` and `DmDeck16` |
| 3 `SetEnemy` observer | `Botpack.Bot` and its state overrides | `UnrealShare.Bots` and its state overrides |
| 4 recovery timing | Deck16-II lift-exit stall control | DeathFan stall control |
| 5 AI p95 | 16-bot tuning-map run | 16-bot tuning-map run |
| 6 avoidable suicide | Deck16-II `PainTimer` water death | DeathFan unassisted environmental deaths and swimming enemy kills |
| 7 role swap | skills 0–7, `Botpack.Bot`, `CHSpectator` lifecycle | skills 0–3, `UnrealShare.Bots`, `UnrealSpectator`, `ReSetSkill`, exact automatic roster |
| 8 dispatch correction | pinned microthreshold bracket plus mover ordering | independent 226b bracket plus mover ordering |

Shared corrections must pass both game matrices; game-specific adapters must not
leak class or skill assumptions across the boundary.

## Sequencing to a live policy

1. Workstreams 1 and 3 land as read-only correctness and observation.
2. Workstream 2 closes or reopens the timeout candidate with cross-game
   evidence.
3. Workstreams 4, 5, and 6 populate the campaign's four fail-closed metrics, or
   document precisely why one cannot yet be honest.
4. Workstream 7 makes per-participant, role-swapped comparison real.
5. Workstream 8 lands or shelves the shared dispatch correction, with the full
   metric set available to judge it.
6. Only then does the first opt-in live policy action get proposed, against a
   repaired-stock baseline, on tuning maps, with held-out maps still closed.

A candidate reaches merge consideration only as a focused topic: one reproduced
runtime correction plus its test, one game-profile lifecycle adapter plus
owner-data evidence, one telemetry slice plus analyzer compatibility, or one
opt-in policy adapter plus qualification evidence. Ordinary gameplay stays
unchanged until that focused change passes its gates.

## Evidence conventions

- Artifacts under `qa/runs/<date>/<slice>/`, never inside the repository.
- Release provenance requires `game.manifest`, an explicit environment
  allowlist, `build_preset` on every variant, and a recorded dirty-tree state.
- Two repetitions per case, compared with `Compare-BotBenchmarkRuns.py`,
  ignoring only audited `_exact` counters, `_diagnostics` payloads, and
  `output_directory`.
- Executable SHA-256 recorded for every candidate, and rollback proven by
  matching hashes rather than by similar aggregates.
- Analyzer schema version bumped whenever a counter group or record stream is
  added, with the group validated as complete and monotonic.
- Every new claim recorded as an iteration entry in
  `Docs/BOT_AI_QUALITY_EXECUTION_PLAN.md`, with its decision stated as reject,
  revise, experimental, release-candidate, or merge-ready.
