# Bot AI implementation plan

## Strategy

Develop measurement and candidate policies together. The first phase maximizes
competence with one unrestricted configuration per policy. Skill levels are a
later calibration layer, after the strongest architecture and its failure modes
are known.

Keep stock Botpack available throughout. Enhanced behavior is selected by an
explicit policy ID and can be removed without changing ordinary UT behavior.

## Workstreams

### A. Quality and tournament system

1. Extract structural validation and useful metric definitions from the
   preserved bot fork.
2. Add policy ID/version, roster role, spawn identity, and scenario identity to
   immutable benchmark configuration.
3. Extend telemetry in focused slices: movement/progress, perception, decision,
   weapon/fire, damage/death, resource, then performance.
4. Support two or more controlled bots and role-swapped candidate-versus-baseline
   matches.
5. Build a matrix runner and analyzer that emits validated per-run summaries,
   paired comparisons, intervals, Pareto data, and machine-readable verdicts.
6. Add controlled fixtures before relying on free-play statistics.

### B. Stock Botpack fidelity

1. Reproduce sight acquisition and loss against exported Botpack scripts and
   retail behavior.
2. Restore missing latent movement, reachability, route ordering, wall contact,
   mover, jump/fall, focus, and strafe contracts one at a time.
3. Keep each engine correction independently testable and suitable for human
   review.

This baseline distinguishes true policy gains from improvements caused merely
by repairing the runtime.

### C. `utility-arena` candidate

1. Implement deterministic utility inputs and an interruptible action stack.
2. Start with recover, attack, retreat, hunt, resupply, pickup, and explore.
3. Add route danger, exposure, equipment value, and local numerical advantage.
4. Trace every selected action, competing score, interruption, and failure.

### D. `tactical-state` candidate

1. Implement explicit idle, acquire, attack, hunt-last-known, investigate-sound,
   resupply, retreat, and unstuck states.
2. Use subjective known-actor records and recognition transitions.
3. Add encounter-direction look targets, cover/escape queries, and local team
   reports.
4. Trace state transitions and their causes.

### E. Shared native services

1. Perception and known-actor memory.
2. Reachspec cache, deterministic tactical queries, and path follower.
3. Aim/weapon controller.
4. Local team blackboard and reservations.
5. Per-bot operation budgets and diagnostic snapshots.

Candidate policies may not bypass these services to obtain omniscient or
unbounded information.

## Parallel ownership

The initial parallel wave assigns disjoint areas:

| Owner | Deliverable | File boundary |
| --- | --- | --- |
| Evaluation agent | unified-compatible quality analyzer, schema fixtures, tests, and extraction notes | `Tools/BotBenchmark/` only |
| Utility-policy agent | deterministic `utility-arena` policy prototype and focused tests | new `SurrealEngine/BotAI/Utility*` and matching test file |
| Tactical-policy agent | subjective memory plus `tactical-state` prototype and focused tests | new `SurrealEngine/BotAI/Tactical*` and matching test file |
| Integrator | research docs, shared contracts, CMake registration, conflict review, builds, and final evidence | shared/build files |

Agents must not edit the current unrelated package, game-database, property,
or VM changes. Agent output is experimental evidence requiring line-by-line
human review, not an upstream-ready PR.

## Milestones and gates

### M0: contracts and offline tests

- Documentation and license/provenance rules exist.
- Shared observations, actions, policy identity, reset/tick, and decision trace
  contracts compile.
- Two experimental policies pass deterministic synthetic scenarios.
- The analyzer rejects malformed or incomparable runs.

### M1: benchmark comparison

- Driver selects a policy explicitly and records it immutably.
- At least two controlled bots can run candidate-versus-stock matches.
- Role-swapped, multi-seed summaries are produced automatically.
- Navigation, damage, death, score, and performance metrics reconcile.

### M2: native service integration

- Controlled perception, reachability, stuck, pickup, combat, and death fixtures
  pass.
- Stock behavior does not regress on the fixture matrix.
- Candidate policies use the same bounded native service APIs.

### M3: best-play tournament

- Stock, utility, and tactical candidates complete the same map/seed matrix.
- Determinism and safety gates pass.
- Strength, competence, and cost results identify a Pareto frontier.
- Failed scenarios, not aggregate impressions, select the next behavior work.

### M4: skill and personality

After selecting the strongest policy or hybrid, derive skill levels by changing
recognition delay, aim dynamics, prediction, query budget, uncertainty, and
risk. Keep personality parameters such as aggression, caution, teamwork, route
risk, and weapon preference orthogonal to mechanical skill.

## Immediate task order

1. Land M0 documentation, policy contracts, two prototypes, and analyzer tests.
2. Add multi-bot/policy configuration without behavior changes.
3. Add movement and participant-score summaries.
4. Port controlled damage/death and sight fixtures from the bot fork in small
   topics.
5. Run the first short candidate tournament.
6. Use the measured largest failure class to choose perception, navigation,
   combat, or tactical follow-up work.

## Initial parallel-wave result

The M0 experimental foundations now exist in this worktree:

- `BotAI/BotPolicy.h` defines the shared observation, action, decision trace,
  reset, and tick contract.
- `utility-arena` v1 implements deterministic utility scoring, target tie
  ordering, hysteresis, and stuck-recovery suspension/resumption.
- `tactical-state` v1 implements explicit combat, remembered-hunt,
  uncertain-investigation, resupply/retreat, and recovery states with bounded
  subjective enemy memory.
- `Analyze-BotQuality.py` validates unified telemetry and reports only the
  movement, no-progress, health, survival, and completion measurements that
  schema v1 can support. Optional metadata enables fail-closed exact pairing.

These policy cores are compiled and covered by synthetic tests, but they are
not yet connected to live Botpack pawns. They cannot be ranked in gameplay
until M1 adds explicit policy selection, controlled multi-bot rosters, richer
event attribution, and candidate-versus-stock matches. This distinction is a
gate against mistaking policy-unit behavior for improved game bots.

The second parallel wave adds:

- a deterministic policy registry and bounded shadow evaluator, allowing every
  experimental policy to consume the same live observation later without
  controlling or destabilizing the stock bot;
- a concurrent map/seed/repetition matrix runner that compares separate binary
  variants today and writes exact pairing metadata for quality analysis; and
- a fail-closed game-profile resolver that records the verified UT99 436
  benchmark contract while recognizing—but not falsely enabling—the incomplete
  Unreal Gold 226b contract.

Cross-game details and the proposed representative/held-out map matrix are in
`BOT_AI_CROSS_GAME_AND_MAPS.md`.

The third parallel wave adds two more integration contracts without changing
the existing telemetry schema or live bot behavior:

- a strict 1–16 participant roster parser with per-bot skills, optional stable
  requested names, and canonical identity fragments; and
- a deterministic runtime-snapshot builder that filters invalid, dead,
  deleted, friendly, and self actors before producing identical sorted policy
  observations for shadow evaluation.

The fourth parallel wave binds the roster contract to the benchmark while
preserving stock Botpack control:

- manifest and summary schema v2 describe the requested and actual ordered
  roster, while telemetry v1 bot identities are checked against it;
- the driver can create 1–16 unnamed bots and assign independently requested
  stock skill values without weakening its exact-roster assertions;
- the matrix runner and analyzer understand both legacy single-bot artifacts
  and roster-aware v2 artifacts; and
- bounded shadow-decision JSON can record deterministic policy outputs without
  granting those policies control of a live pawn.

The fifth integration slice connects the runtime-snapshot builder to live
controlled Botpack pawns without changing their behavior. Each roster identity
owns independent `tactical-state` and `utility-arena` evaluators. A separate
bounded manifest/JSONL stream records current weapon/ammunition usability,
health and per-tick health loss, subjective line-of-sight enemies, policy
decisions, evaluation counts, and action transitions. It explicitly marks item,
armor, and stuck-time observations as unavailable and never issues movement or
fire commands.

The next code step is to validate this stream in a short owner-data smoke, then
add evidence-backed item/reachability and route-progress observations. Only
after shadow decisions are stable across repeated map/seed pairs should one
experimental policy receive a narrowly scoped action adapter.
