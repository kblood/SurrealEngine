# Bot AI evaluation and quality measurement

## Existing foundation

Unified already provides a seeded fixed-step runtime, a bounded headless bot
driver, an immutable manifest, and deterministic JSONL snapshots. It currently
proves setup and state observation only. It does not attribute navigation
progress, perception, shots, damage, kills, deaths, pickups, or decisions.

The preserved bot fork has a broader trace validator and matrix analyzer. That
work should be extracted against unified's smaller protocol in focused slices.

## Evaluation principles

- Compare candidates under identical map, spawn/role, seed, tick rate, roster,
  and match duration.
- Swap roles/spawns and repeat seeds to prevent positional bias.
- Keep a stock Botpack baseline in every qualification batch.
- Report metric vectors and Pareto tradeoffs, not only one opaque score.
- Use paired results and confidence intervals; never rank bots from one match.
- Treat crashes, invalid targets, omniscient reads, non-finite telemetry,
  nondeterminism, and failure to complete as automatic qualification failures.
- Separate playing strength from human likeness and later difficulty scaling.

## Quality dimensions

### Correctness and safety gates

- deterministic replay digest;
- exact participant roster and policy identity;
- no nonparticipant or dead target selection;
- no impossible line-of-sight or knowledge transition;
- no NaN, infinite, out-of-order, or unbounded telemetry;
- bounded decision and path-query work;
- completion without crash, hang, or permanent stuck state.

### Navigation and resource use

- reachable-goal success rate and time;
- useful distance/progress versus oscillation;
- stuck events and recovery time;
- route failure and replanning rate;
- pickup acquisition time and useful inventory value;
- map coverage and navigation-node coverage.

### Perception and combat

- first valid sighting, recognition, aim, and damage latency;
- time tracking a visible or remembered threat;
- hitscan and finalized-projectile accuracy;
- damage dealt and taken per minute;
- kills, deaths, suicides, and score margin;
- weapon selection value, friendly-fire attempts, and wasted shots.

### Survival and tactics

- survival time and post-damage life expectancy;
- damage exchange efficiency;
- retreat success when disadvantaged;
- exposure time and avoidable incoming damage;
- objective contribution for non-deathmatch modes;
- team assistance and contention for reserved resources.

### Performance

- AI CPU time per bot tick;
- perception traces and navigation expansions;
- peak known-actor, path, and decision-state memory;
- worst-case and percentile decision latency.

## Ranking protocol

The first tournament uses unrestricted best-play configurations. Each candidate
plays stock and every other candidate on several maps and seeds with role swaps.
Report:

1. match win/loss/tie and paired score margin;
2. combat efficiency and survival;
3. navigation/resource competence;
4. safety and determinism gates;
5. CPU and memory cost; and
6. a Pareto frontier showing candidates that are not dominated across strength,
   reliability, and cost.

An optional quality index may summarize results for dashboards, but it cannot
hide a failed gate or substitute for component metrics. Normalize each metric
against the stock baseline and cap extreme values before weighting. Store the
weights and schema version in the immutable manifest.

## Efficient test pyramid

1. Pure deterministic unit scenarios for perception, action selection, aim,
   scoring, and stuck-state transitions.
2. Synthetic engine fixtures for sight gain/loss, sound uncertainty, a single
   reachspec, a blocker, a pickup, a weapon duel, damage, and death.
3. Short headless smoke matches on one small map for every changed binary.
4. Paired multi-map/multi-seed candidate tournaments only after lower gates
   pass.
5. Retail UT comparison as a compatibility oracle, never as redistributed test
   data.

Caching the built executable and content manifest, running independent seeds in
parallel processes, and failing fast on structural validation keeps iteration
cost bounded. Raw event streams remain local artifacts; compact summaries,
schema fixtures, and analyzer tests can be committed.
