# Controlled bot benchmark driver

This topic depends on `pr/headless-benchmark-driver` and registers the first
concrete driver as `bot-benchmark`. It is selected explicitly with:

```text
--headless-driver=bot-benchmark
```

With no selection, registration is inert and interactive behavior is unchanged.
The driver consumes `DeterministicRuntime`, loads a caller-selected UT map with
automatic bots disabled, logs the viewport in as a spectator, spawns exactly
one bot at an external skill tier from 0 through 7, and advances a bounded
fixed-step level loop without window, audio, or renderer creation.

The immutable configuration is parsed once from `--botbench-url`,
`--botbench-output`, `--botbench-seed`, `--botbench-ticks`,
`--botbench-fixed-delta`, and `--botbench-difficulty`. `summary.json` binds the
exact configuration to game/version/map identity, controlled bot identity,
ticks, simulated time, status, failure reason, and exit code. Seed and tick
integers are serialized as decimal strings to avoid JSON precision loss.

## Manifest and bounded telemetry

An opted-in run also creates two comparison inputs in the output directory:

- `manifest.json` is written once at driver start using
  `surreal-bot-benchmark-manifest-v1`. It records the parsed configuration, a
  deterministic FNV-1a configuration identity, and the telemetry event cap.
  The output directory is recorded for provenance but excluded from the
  configuration identity because it cannot affect simulation.
- `events.jsonl` uses `surreal-bot-benchmark-telemetry-v1`. It contains one
  `run_start` record, at most one `tick` record for each simulated tick, and
  one `run_result` record. The hard cap is therefore `max_ticks + 2`; no bot or
  script event can expand it.

Every record carries a decimal-string sequence and tick, fixed nine-decimal
simulated time, configuration identity, resolved map, status, and failure
reason. Each tick captures only state already observable by the driver: bot
PRI-or-actor identity, actor/player/class names, state, position, velocity, and
health. Bots are sorted by identity and actor name before serialization. JSON
keys have a fixed order, strings are escaped explicitly, numbers use the
classic locale, position and velocity use six fixed decimals, negative zero is
normalized, and non-finite values fail the benchmark instead of entering the
trace.

Setup and tick failures are repeated in `run_start` or `run_result` when the
telemetry stream remains writable and always remain in `summary.json`. Failure
to serialize or finalize telemetry changes the run to a nonzero failure. The
protocol tests use synthetic configurations and bot snapshots; they do not
contain captured game data.

## Evidence mapping

This is enough to prove repeatable setup, roster cardinality, requested stock
skill mapping, bounded lifecycle completion, and exact run identity. It does
not yet prove navigation, acquisition, combat, damage, death, or skill parity.

The read-only evidence source was `surreal-bot-ai` commit `a65ae787`, especially
its `BotBenchmark` JSONL convention and `Tools/BotBenchmark` structural
validator. This topic deliberately maps only its generic protocol ideas:

| Evidence idea | Upstreamable implementation here |
| --- | --- |
| Versioned run configuration | Immutable `manifest.json` plus configuration identity |
| Ordered JSONL events | Fixed-schema records with sequence/tick ordering |
| Per-tick pawn observation | Bounded bot identity, transform, health, and state snapshot |
| Explicit setup/runtime failure | `run_start`/`run_result` status and existing summary exit |

The reference branch remains the source for later, separate topics: AI event
instrumentation, controlled reachability/HitWall/death fixtures, retail oracle
work, structural trace validation, matrix orchestration, and statistical
analysis. None of its fixtures, tools, captured data, or bot behavior changes
are included here. This telemetry does not prove navigation, acquisition,
combat, damage attribution, death causality, or skill parity; it only provides
a stable observation stream on which later comparison topics can build.

## Process bootstrap remains separate

`GameApp` still creates its display backend and launcher before constructing an
engine. A display-free CI entry path should be a second topic that detects the
registered headless mode before widget initialization and resolves the game
folder non-interactively. It is intentionally not folded into this driver.
