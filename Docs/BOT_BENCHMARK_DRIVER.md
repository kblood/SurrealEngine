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

## Evidence mapping

This is enough to prove repeatable setup, roster cardinality, requested stock
skill mapping, bounded lifecycle completion, and exact run identity. It does
not yet prove navigation, acquisition, combat, damage, death, or skill parity.

The reference `bot-ai-parity` branch remains the source for later, separate
topics: event telemetry, controlled reachability/HitWall/death fixtures,
structural trace validation, matrix orchestration, and statistical analysis.
Generic engine correctness and bot behavior changes must remain independently
reviewed commits with their own evidence.

## Process bootstrap remains separate

`GameApp` still creates its display backend and launcher before constructing an
engine. A display-free CI entry path should be a second topic that detects the
registered headless mode before widget initialization and resolves the game
folder non-interactively. It is intentionally not folded into this driver.
