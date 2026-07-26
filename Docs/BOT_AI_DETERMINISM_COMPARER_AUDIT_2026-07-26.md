# Bot benchmark determinism and comparer audit — 2026-07-26

## Decision

The current comparer detects a divergence in captured per-tick bot trajectory
or enabled WarnTarget/TryToDuck telemetry. It cannot certify a **same-binary**
qualification repeat: the compared artifacts carry no executable hash. It also
silently omits `route-execution.jsonl`, so an exact route-commit/provenance
claim needs an additional comparison today.

No UCC/UnrealEd or game was launched for this audit.

## Existing duplicate check

I ran the current comparer against the smallest existing duplicate pair:

- UT436 `DM-Deck16][`, 16 bots, skill 3, seed `271828`, 1,200 ticks;
- config identity `fnv1a64:ffb8899fa01b13b0`;
- `ut436-deck16-16bot-s271828-pick-target-observer-r1` and `-r2` under
  `qa/runs/2026-07-26/`.

```text
python Tools/BotBenchmark/Compare-BotBenchmarkRuns.py <r1> <r2> --ignore-field output_directory
```

The result was `equivalent: true`. The 1,202-line `events.jsonl` files are
byte-identical (raw SHA-256
`0A3E6F99235F30CE689E287D1D00616D3E6982F3B1112261EE54FF79F98BF70`), as
are `shadow-decisions.jsonl` and `shadow-manifest.json`. Manifest and summary
match after removing only their run-local `output_directory` fields. The
comparer unit suite also passes: 22 tests in 0.228 seconds.

This is good evidence of deterministic sampled movement and enabled
PickTarget-observer output for that frozen pair. It is not a same-binary
certificate: neither run has `invocation.json` nor a recorded executable hash.

## What config identity covers

`BotBenchmarkTelemetryProtocol::ConfigIdentity` hashes the map URL, seed,
tick limit, fixed delta, difficulty, roster identity fragments, and all current
run selectors: harmful-zone escape, walking positive-DPS veto, hazard swim
egress and live mode, failed-navigation avoidance, falling-hazard recovery and
live mode, both move-timeout modes, target-selection observer, PickTarget
observer (and predicate mode when enabled), WarnTarget observer, inventory
direct-reach observer, inventory-marker safety, native-path-commit observer,
and direct-reach-command observer.

`config_id` is emitted in manifest, main telemetry, shadow manifest, and shadow
telemetry. The comparer protects it from ignoring and verifies those joins. It
also compares complete manifests and summaries, so a selector difference cannot
be ignored: its value is boolean/string metadata, while the only permitted
non-provenance ignores are integer `_exact` fields and the two run-local
output-directory fields.

The comparer does not recompute the FNV identity from the manifest. Runtime
generation and exact artifact comparison make this acceptable for ordinary
repeat comparison, but it is not an independent identity-integrity oracle.

## Divergence coverage

### Movement

Yes, for the main observable path. Each `events.jsonl` tick contains every
controlled bot's identity, position, velocity, physics mode, latent action,
acceleration, destination, move timer, target identity/name, health, score,
and accumulated exact counters. A differing sampled position, velocity,
movement command, physics transition, or recorded safety result fails strict
equivalence. The duplicate check above therefore covers 1,200 ticks of full
sampled trajectories.

### WarnTarget and TryToDuck

Yes when the WarnTarget observer is enabled. The main event stream carries the
observer status and, per bot, exact counts plus bounded
`warn_target_records`, `try_to_duck_outcome_records`,
`warning_dodge_launch_records`, and `warning_dodge_terminal_records`. These
arrays are not in the comparer's diagnostic-ignore allowlist, so a changed
warning call, duck argument/result, post-call velocity/state, launch token, or
continuity terminal fails the strict event comparison.

This needs an exercised duplicate to be a coverage claim. The available
7,200-tick Deck pair with WarnTarget enabled records 114 WarnTarget calls but
zero TryToDuck calls and zero warning-dodge launches. It demonstrates warning
observation, not duck-path determinism. The 1,200-tick pair compared above does
not enable WarnTarget.

### Route execution

No, not through `Compare-BotBenchmarkRuns.py`. The driver emits one
`route-execution.jsonl` record per simulated tick, but `_discover()` reads only
`manifest.json`, `events.jsonl`, `summary.json`, and the optional two
`shadow-*` files. A route-execution artifact can differ or be missing in both
runs without changing this comparer’s result. The main event trajectory often
reveals a downstream route difference, but route head, cache, and native commit
provenance are not fully duplicated there.

## Smallest admissible same-binary stock repeat

The next stock repeat should be a two-run, 1,200-tick UT Deck16 16-bot pair
with every behavior experiment disabled. Before the first launch, record the
SHA-256 and byte size of the exact Release executable and a digest of the game
content manifest. Use the same executable path, game root, URL, seed, fixed
delta, difficulty, roster, and all observer selectors for both runs. Write
those digests into a comparison-visible provenance object; they are not
configuration knobs and should not be folded into `config_id`.

Compare both repetitions with only `output_directory` ignored, and require byte
equality for:

1. `events.jsonl`;
2. `shadow-manifest.json` and `shadow-decisions.jsonl`, when present;
3. `route-execution.jsonl`, when present; and
4. normalized `manifest.json` and `summary.json`.

For WarnTarget/TryToDuck qualification, use the same binary and an existing
native test fixture that forces one valid nested `WarnTarget -> TryToDuck`
call, then run it twice. A natural stock match with zero duck observations
cannot prove that the duck outcome stream is deterministic.

## Minimal correction before qualification use

Do not change bot behavior. Extend `Compare-BotBenchmarkRuns.py` and its tests
to treat `route-execution.jsonl` as an optional paired artifact:

- reject it when present in only one run;
- validate schema, per-line sequence/tick order, `benchmark_config_id`, and
  participant roster against the benchmark manifest; and
- compare it strictly, with no new generic ignore path.

Add `executable_sha256` and the game-manifest digest to manifest and summary
provenance, validate their format and intra-run agreement, make them protected
comparer fields, and require equality across the pair. This is a small
measurement/provenance correction, but it needs focused comparer tests; it is
not a trivial test-only edit and was intentionally not implemented here.
