# Headless benchmark driver foundation

This layer depends on `pr/deterministic-runtime`. It adds an opt-in driver
registry and a bounded lifecycle runner without importing bot behavior,
telemetry, fixtures, rendering changes, or VR ancestry.

An engine extension registers a named factory in `HeadlessDriverRegistry`.
Selecting it with `--headless-driver=<name>` makes `Engine::Run()` execute the
driver before window, audio, and renderer creation. With no option, the normal
interactive path is unchanged. Unknown driver names fail explicitly.

The runner applies the driver's deterministic seed once, advances
`DeterministicRuntime` for every tick, stops when the driver completes or its
mandatory tick bound is reached, and passes an exact final summary to the
driver. A concrete benchmark owns map loading, login, engine ticks, telemetry,
and final artifacts; none of those policies live in this foundation.

## Evidence enabled

- lifecycle ordering and exit status independent of presentation;
- exact seed, fixed delta, tick count, and limit/completion outcome for a run;
- bounded failure behavior when a scenario never declares completion;
- a narrow factory seam for bot benchmarks and other commandlets.

The focused test uses a recording driver to cover natural completion, tick
limit termination, exit-code propagation, invalid limits, deterministic driver
name ordering, and duplicate-registration rejection.

## Remaining integration

The bot lane still needs a separate concrete driver that reconstructs the
controlled map/login/tick protocol from `bot-ai-parity`. Process-level startup
must also recognize that concrete mode before display/widget initialization so
it can run on display-free CI. Trace schemas, validators, analysis, engine
correctness fixes, and bot behavior remain separate review slices.
