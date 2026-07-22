# Deterministic runtime foundation

`DeterministicRuntime` is a small, opt-in service for commandlets, replay
tools, and future headless benchmark drivers. It provides:

- one explicit seed operation for both random streams currently used by the
  engine (`RandInt`/`FRand` and `std::rand`);
- fixed real and game elapsed time, tick count, and accumulated time;
- a caller-owned fixed wall-clock value for script-visible date/time fields.

Constructing the service does not alter engine state. A driver must explicitly
call `ApplyRandomSeed()` and use `AdvanceFrame()`, so normal interactive game
startup and timing are unchanged.

## Evidence enabled

A headless driver can bind a run manifest to a seed, fixed delta, and wall
clock, then record each returned tick and elapsed-time tuple. Reapplying the
same seed replays both existing random APIs within the same standard-library
implementation. This makes repeated-run nondeterminism checks possible without
pulling bot rules, fixtures, telemetry, rendering changes, or VR ancestry into
the engine foundation.

The random APIs use standard-library generators, so this slice promises
repeatability for a fixed executable/runtime, not identical sequences across
different C++ standard-library implementations. Cross-platform bit-exact
replay would require a separately reviewed stable PRNG migration.

## Bot work that remains separate

- an opt-in headless lifecycle that loads a controlled map and consumes this
  service without opening presentation devices;
- benchmark configuration, immutable manifests, trace telemetry, validators,
  matrices, and statistical analysis;
- generic engine correctness fixes proven by controlled fixtures;
- bot behavior changes proven against retail oracles.

Those layers should remain separate PRs. In particular, this foundation does
not change bot behavior and does not make a bot-parity claim.
