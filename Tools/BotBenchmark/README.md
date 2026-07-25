# Bot quality analysis

## Matrix runner

`Run-BotBenchmarkMatrix.py` expands a versioned manifest into deterministic
map/seed/repetition/variant cases and runs them concurrently. A variant is an
independent engine executable; the runner does not claim that the current
engine can select several live bot policies from one binary.

Example `surreal-bot-benchmark-matrix-v1` manifest:

```json
{
  "schema": "surreal-bot-benchmark-matrix-v1",
  "game": {
    "family": "ut99",
    "root": "C:/Games/Unreal Tournament"
  },
  "variants": [
    {
      "id": "stock",
      "executable": "../../build-stock/Release/SurrealEngine.exe",
      "comparison_role": "baseline"
    },
    {
      "id": "navigation-candidate",
      "executable": "../../build-navigation/Release/SurrealEngine.exe",
      "comparison_role": "candidate"
    },
    {
      "id": "utility-experiment",
      "executable": "../../build-utility/Release/SurrealEngine.exe"
    }
  ],
  "map_urls": [
    "DM-Morbias][?Game=Botpack.DeathMatchPlus",
    "DM-Deck16][?Game=Botpack.DeathMatchPlus"
  ],
  "seeds": [104729, 271828],
  "max_ticks": 5400,
  "fixed_delta": 0.016666667,
  "difficulty": 7,
  "bot_count": 2,
  "per_bot_skills": [7, 7],
  "harmful_zone_escape_enabled": false,
  "concurrency": 4,
  "timeout_seconds": 180,
  "repetitions": 1
}
```

Paths are resolved relative to the manifest. `game.family` selects a verified
engine-side match adapter; map URLs are never rewritten, so each game family
retains its own game class and URL options. The implemented adapters are UT436
(`Botpack.DeathMatchPlus`) and Unreal Gold 226b
(`UnrealShare.DeathMatchGame`). `bot_count` defaults to one.
`per_bot_skills` and `requested_names` are optional, but when present each must
contain exactly `bot_count` entries. Skills are integers from zero through
seven. Names must be non-empty, trimmed, comma-free, and unique under the
engine's ASCII case-insensitive comparison. Roster configuration is included
in deterministic run and pair IDs. `harmful_zone_escape_enabled` is an optional
strict boolean that defaults to `false`; it selects the benchmark-only,
default-off harmful-zone escape experiment and is included in deterministic run
and pair IDs.

## Harmful-residence attribution

For observer-enabled owner-local runs, reconstruct harmful-zone episodes with
the strict offline analyzer:

```powershell
python .\Tools\BotBenchmark\Analyze-HazardResidence.py `
  .\qa\runs\owner-local-run `
  --output .\qa\runs\owner-local-run\hazard-residence-analysis.json
```

It fails if the benchmark stream is incomplete or lacks the complete current
residence counter group. A reported candidate is an observation only; neither
candidate presence nor a zone-clear terminal authorizes movement control.

## Owner-local map catalog

Extract a read-only map catalog outside the game root, then validate it before
using it to choose bot fixtures or diagnose traversal:

```powershell
SurrealEngine.exe --autoplay --headless-driver=map-catalog `
  --catalog-map=DM-Deck16][ `
  --catalog-output=C:\qa\map-catalog\deck16 `
  "C:\Games\Unreal Tournament"

python .\Tools\BotBenchmark\Validate-MapCatalog.py `
  C:\qa\map-catalog\deck16\DM-Deck16][.json
```

`surreal-map-catalog-spike-v3` is intentionally owner-local. It records level
actor slots, navigation/reachspec graph semantics, traversal relationships,
resolved navigation-point zone membership, and model zone graph; it does not
export game assets or authorize a behavior change.

Every controlled benchmark run additionally writes
`bot-realized-capabilities.json` after bot spawn and before tick zero. It is a
read-only capability witness for the actual roster; it is not part of the
telemetry stream and must be kept with its matching run identity when judging
reachspec feasibility.

`Validate-RealizedBotCapabilities.py <run-directory>` binds that witness to a
complete v2 `manifest.json`/`summary.json` pair. It rejects missing witnesses,
schema drift, non-finite or negative movement values, unknown capability
fields, and any participant identity, actor, class, order, or count that does
not exactly match `summary.json.actual_roster`. The standard matrix runner
invokes this validator by default and requires the fourth non-empty artifact;
an explicitly injected validator is test-only plumbing and is responsible for
its own structural gates.

Use the catalog and matching witness only through the read-only static audit:

```powershell
python .\Tools\BotBenchmark\Analyze-ReachspecCapabilities.py `
  C:\qa\map-catalog\deck16\DM-Deck16][.json `
  C:\qa\benchmark\deck16-run
```

The audit verifies both inputs, requires the catalog map to match the benchmark
URL, and reports every observed reach-flag incidence against each live bot's
capabilities. It intentionally sets `selection_safe: false`: reach-flag
combination semantics, player-only eligibility, dynamic collision, current
anchor, and traversal state require a subsequent route-execution observer.

Each current benchmark run also writes `route-execution.jsonl`. It is a
read-only, one-record-per-tick witness of each controlled bot's position,
velocity, `MoveTarget`, ordered pawn `RouteCache`, resolved zone, and
per-tick displacement. It does not call pathfinding, collision traces, or
write route state; its purpose is to attribute stalls before any routing
behavior is changed.

`Analyze-RouteExecutionContext.py` joins that trace with a validated v3 map
catalog and the realized bot-capability witness. It resolves only the observed
post-tick route-cache first hop and reports death-adjacent context; it does not
claim that the edge was the native search's selected reachspec or that it
caused a death. It remains fail-closed on map/config, roster, tick, node, and
edge mismatches:

```powershell
python .\Tools\BotBenchmark\Analyze-RouteExecutionContext.py `
  C:\qa\map-catalog\deck16\DM-Deck16][.json `
  C:\qa\benchmark\deck16-run --output C:\qa\reports\route-context.json
```

`Analyze-NativePathCommits.py` validates the narrower native provenance stream
emitted at the cache write itself. It verifies every committed bounded edge by
its exact catalog reachspec index and rejects missing records, overflow,
non-contiguous per-pawn sequences, cache-shape drift, pruned edges, and any
catalog mismatch. This establishes what the shared native search committed;
it does not yet justify a behavioral route policy. It also emits only
contextual last-commit links at hazard-entry and death-counter transitions; a
link is not a causal label or authorization for an intervention.

Move-stall detections additionally emit bounded decision-time records in each
bot's telemetry event. `move_stall_recovery_decisions` records the selector
input and result before a recovery can write movement state. The quality
analyzer requires these records and their overflow counter to partition native
detections and forced-replan counters exactly.

The current UT436 and Unreal Gold adapters do not expose a verified named-bot
spawn contract. Supplying `requested_names` is therefore parsed and recorded
deterministically but the engine run deliberately fails instead of silently
substituting random profiles. Omit names for runnable matrices until a
game-specific named-spawn contract is independently verified. Unreal Gold
also accepts only its native external skill range, zero through three; the
adapter disables random bot order and verifies the concrete `Bots` roster.
All executables receive:

```text
--autoplay --headless-driver=bot-benchmark
--botbench-url=... --botbench-output=... --botbench-seed=...
--botbench-ticks=... --botbench-fixed-delta=... --botbench-difficulty=...
--botbench-bots=... --botbench-harmful-zone-escape=0|1
[--botbench-skills=...] [--botbench-names=...]
<game root>
```

Preview exact commands without launching anything or creating the output:

```powershell
python .\Tools\BotBenchmark\Run-BotBenchmarkMatrix.py `
  --manifest .\bot-matrix.json --output .\bot-matrix-results --dry-run
```

Run the matrix and produce a quality report only if every process exits zero,
writes all four required non-empty artifacts, and passes the structural
analyzer:

```powershell
python .\Tools\BotBenchmark\Run-BotBenchmarkMatrix.py `
  --manifest .\bot-matrix.json --output .\bot-matrix-results --analyze
```

The output directory must be new. Each run gets a deterministic directory with
an ordinal, readable slug, and hash; colliding sanitized map or variant names
therefore remain distinct. `invocation.json`, stdout, stderr, and
`quality-metadata.json` preserve its inputs. Explicit role labels must contain
exactly one baseline and one candidate. They receive matching pair IDs for
each map/seed/repetition case. Additional unlabeled variants are analyzed only
as aggregates.

Timeout, nonzero exit, missing output, structural-validation failure, or
aggregate-analysis failure makes the matrix fail while preserving all run
diagnostics and `matrix-results.json`.

Every non-dry run also writes `provenance.json`. It records the matrix and
optional game-manifest hashes, source repository/branch/commit/tree, dirty
state and diff hash, runner invocation, only the explicitly
allowlisted environment variables, per-variant executable path/size/SHA-256,
UTC bounds, and hashes of the child run and analysis artifacts. Ordinary
tuning manifests remain compatible and default to development provenance.
Development mode hashes tracked changes and untracked paths without reading
every untracked build artifact; release mode additionally hashes untracked file
contents and records `untracked_contents_hashed: true`.

Release evidence must opt in explicitly and supply every declaration before
any benchmark process is launched:

```json
{
  "game": {
    "family": "ut99",
    "root": "C:/Games/Unreal Tournament",
    "manifest": "./ut99-fixture-manifest.json"
  },
  "provenance": {
    "mode": "release",
    "classification": "heldout",
    "environment_allowlist": ["PYTHONHASHSEED"]
  },
  "variants": [
    {
      "id": "candidate",
      "executable": "../../out/bot-candidate/Release/SurrealEngine.exe",
      "build_preset": "bot-candidate"
    }
  ]
}
```

`classification` is either `tuning` or `heldout`. Release mode also requires
`game.manifest`, an explicitly present environment allowlist (which may be
empty), and `build_preset` on every variant. It fails closed if Git source
state on an attached branch, the exact runner invocation, or any required
input hash cannot be captured. A dirty tree is recorded rather than silently
presented as a clean build; release policy can reject it from the captured
`source.dirty` field.

`Analyze-BotQuality.py` is the initial quality-analysis lane for the unified
engine's benchmark files. It reads legacy manifest/summary v1 runs and the
roster-aware manifest/summary v2 schema; telemetry remains v1. It validates
`manifest.json`, every line of `events.jsonl`, and `summary.json` before
producing a JSON report.
Malformed runs fail the command; they are never silently omitted.

For v2 runs, validation also requires integer `bot_count`, contiguous ordered
roster indexes, exact manifest/summary `requested_roster` agreement, a complete
`actual_roster` on successful runs, unique participant identities, actors, and
names, and telemetry participants that match the actual roster. The v2 config
hash is recomputed from the roster identity fragments as well as the original
map, timing, seed, and difficulty fields.

Run it from the repository root:

```powershell
python .\Tools\BotBenchmark\Analyze-BotQuality.py `
  .\botbench-output `
  --output .\bot-quality.json
```

## Executable quality gates

`Evaluate-BotQualityGate.py` applies explicit thresholds to an analyzer report
and exits nonzero when evidence is missing or a gate fails:

```powershell
python .\Tools\BotBenchmark\Evaluate-BotQualityGate.py `
  .\bot-quality.json .\bot-quality-gates.json `
  --output .\bot-quality-gate-result.json
```

For a matrix run, prefer the runner's `--quality-gates` option instead of a
separate manual evaluator invocation. It validates the named gate file before
launching a match, implies `--analyze`, writes `quality-gate-result.json`,
marks the matrix failed when that result fails, and hashes the exact gate file
in `provenance.json`:

```powershell
python .\Tools\BotBenchmark\Run-BotBenchmarkMatrix.py `
  --manifest .\owner-local-release-matrix.json `
  --output .\bot-matrix-results `
  --quality-gates .\Tools\BotBenchmark\QualificationCampaigns\UT436-tuning-quality-gates-v1.json
```

The checked-in UT436/Unreal 226b campaign authority is in
`QualificationCampaigns/`. It freezes the map matrix and points at the
per-game tuning and held-out gate files without committing game roots or
executables. Its current gates intentionally require unavailable causal,
role-swapped, recovery-time, and in-engine timing metrics, so they fail closed
until those measurements are implemented. See
`QualificationCampaigns/README.md` before treating any matrix as release
evidence.

Example `surreal-bot-quality-gates-v1` configuration:

```json
{
  "schema": "surreal-bot-quality-gates-v1",
  "required_metrics": ["completion", "kills_exact", "deaths_exact"],
  "required_runs": [
    {"id": "candidate-deck-runs", "variant": "candidate", "map": "DM-Deck16][", "min": 2}
  ],
  "aggregate_gates": [
    {"id": "candidate-kills", "variant": "candidate", "metric": "kills_exact", "statistic": "mean", "min": 1}
  ],
  "per_run_gates": [
    {"id": "candidate-completion", "variant": "candidate", "metric": "completion", "equals": true},
    {"id": "candidate-deck-deaths", "variant": "candidate", "map": "DM-Deck16][", "metric": "deaths_exact", "max": 3}
  ]
}
```

Paired gates make candidate-versus-baseline non-regression executable. They
cross-check `quality-metadata.json` against the analyzer's
`paired_comparisons`, and they evaluate every complete pair selected by map and
variant. For example, this rejects any increase in Deck16 deaths:

```json
{
  "schema": "surreal-bot-quality-gates-v1",
  "paired_gates": [
    {
      "id": "deck-death-non-regression",
      "baseline_variant": "repaired-stock",
      "candidate_variant": "enhanced-bot",
      "map": "DM-Deck16][",
      "metric": "deaths_exact",
      "comparison": "delta",
      "direction": "lower",
      "min_pairs": 2,
      "maximum_regression": 0
    }
  ]
}
```

Every paired gate must state `comparison` (`delta` or `ratio`) and `direction`
(`higher` or `lower`); the evaluator never guesses direction from a metric
name. Exactly one threshold is required:

- `"exact_equality": true` requires the candidate value to equal the baseline
  value in every selected pair.
- `"maximum_regression": N` permits at most `N` worsening in metric units for
  a delta comparison, or as a fraction of the baseline for a ratio comparison.
- `"minimum_improvement": N` requires at least `N` improvement in metric units
  for a delta comparison, or as a fraction of the baseline for a ratio
  comparison.

Ratio gates require a strictly positive baseline value. `min_pairs` defaults to
one and should be set to the complete expected seed/role count for release
evidence. Missing metadata, one-sided pairs, null or non-numeric metrics,
missing analyzer pair records, and a selector with too few complete pairs all
fail closed. Other maps and other variant pairs do not contribute to the gate.

Every reported run must be complete, successful, and structurally valid.
Selectors matching no runs, missing metrics, and null metrics fail closed. Metric
names are literal: the evaluator does not reinterpret `suicides_exact` as
avoidable deaths or synthesize death attribution. Such a metric remains
unavailable unless the analyzer report contains live telemetry evidence under
that exact name. Every required-run, aggregate-gate, and per-run-gate entry
must have an explicit, non-empty `id`, and IDs must be unique across the whole
configuration so results remain stable and auditable when entries are reordered.

Per-run attributed-death safety is expressible as separate zero-tolerance
gates. For example, a Deck16 candidate can require the live attribution fields
and reject any run containing a direct self-kill, unassisted environmental
death, recent-enemy-contributed environmental-death proxy, or ambiguous death:

```json
{
  "schema": "surreal-bot-quality-gates-v1",
  "required_metrics": [
    "completion",
    "deaths_exact",
    "direct_self_kills",
    "direct_enemy_kills",
    "unassisted_environmental_deaths",
    "recent_enemy_contributed_environmental_deaths_proxy",
    "ambiguous_deaths",
    "recent_enemy_momentum_contributed_environmental_deaths_proxy"
  ],
  "required_runs": [
    {"id": "candidate-deck-runs", "variant": "candidate", "map": "DM-Deck16][", "min": 2}
  ],
  "aggregate_gates": [],
  "per_run_gates": [
    {"id": "candidate-deck-completion", "variant": "candidate", "map": "DM-Deck16][", "metric": "completion", "equals": true},
    {"id": "candidate-deck-self-kills", "variant": "candidate", "map": "DM-Deck16][", "metric": "direct_self_kills", "max": 0},
    {"id": "candidate-deck-unassisted-environment", "variant": "candidate", "map": "DM-Deck16][", "metric": "unassisted_environmental_deaths", "max": 0},
    {"id": "candidate-deck-enemy-environment", "variant": "candidate", "map": "DM-Deck16][", "metric": "recent_enemy_contributed_environmental_deaths_proxy", "max": 0},
    {"id": "candidate-deck-ambiguous-deaths", "variant": "candidate", "map": "DM-Deck16][", "metric": "ambiguous_deaths", "max": 0}
  ]
}
```

`direct_enemy_kills` is intentionally not an unsafe-death gate. The momentum
counter is a subset of the recent-enemy-contributed proxy, not a sixth primary
partition bucket. The analyzer rejects partial groups and requires the five
primary counters to sum to `deaths_exact`; listing all six emitted fields in
`required_metrics` also makes absence explicit at the gate boundary.

Multiple runs may be passed. The output groups them by variant and describes:

- observed path distance and active-movement time;
- stationary/no-progress time and a two-second stuck-event proxy;
- observed health loss, minimum health, and presence/alive status in the final
  sample;
- successful benchmark completion.

The movement threshold is 0.25 Unreal units per sample. The no-progress and
stuck values are deliberately named proxies: telemetry v1 does not record a
latent movement action, destination, acceleration, enemy, or tactical intent.
A bot holding useful cover can therefore look stationary, while a blocked bot
with intermittent movement can evade the two-second window. Movement distance
also has no inherent preferred direction.

Telemetry v2 adds authoritative cumulative counters captured at UE1 script-call
boundaries for `Killed` and `HitWall`, plus persistent PRI score/death samples,
movement intent, and pain/kill-zone presence. The analyzer reports exact kills,
deaths, UT-style suicides (self or no valid player killer), environmental deaths,
wall-hit calls, score delta, and suicide-to-kill ratio. Its stricter stuck proxy
only accumulates no-progress time while a latent movement action or horizontal
acceleration indicates intent.

Hazard-exposed deaths remain a proxy: the victim occupied a pain, kill, or
positive-damage zone when `Killed` entered, but the hook does not prove that the
zone caused the final damage or that entering it was tactically avoidable.
Likewise, a `HitWall` call is an exact collision callback, not by itself proof of
bad wall-running. Compare rates across identical map/seed/roster runs and inspect
movement-intent stuck time alongside it. Telemetry v1 remains accepted, with
v2-only metrics reported as null.

## Exact paired comparisons

Place an optional `quality-metadata.json` beside a run's other three files:

```json
{
  "schema": "surreal-bot-quality-run-metadata-v1",
  "variant": "new-perception",
  "pair_id": "DM-Morbias-seed-104729",
  "comparison_role": "candidate"
}
```

Give the matching run the same `pair_id`, a baseline variant, and
`"comparison_role": "baseline"`. A pair is accepted only when URL, map, seed,
tick ceiling, fixed delta, difficulty, and initial bot count match. Missing,
duplicate, or incomparable pair members reject the analysis. Runs without pair
metadata still contribute to their variant aggregate.

Paired output reports candidate-minus-baseline deltas. Lower is preferred only
for stationary/stuck and observed health-loss proxies; higher is preferred for
minimum health, final-sample survival, and completion. Distance and activity
remain descriptive. The analyzer intentionally emits no composite quality
score.

## Deterministic run equivalence

`Compare-BotBenchmarkRuns.py` checks whether two completed, successful run
directories are semantically identical. It validates and compares
`manifest.json`, `events.jsonl`, `summary.json`, and the paired
`shadow-manifest.json`/`shadow-decisions.jsonl` artifacts when present:

```powershell
python .\Tools\BotBenchmark\Compare-BotBenchmarkRuns.py `
  .\baseline-run .\candidate-run `
  --ignore-field walking_preflight_candidates_exact `
  --ignore-field walking_preflight_rejects_exact `
  --ignore-diagnostic-field walking_step_preflight_diagnostics `
  --ignore-field output_directory `
  --output .\equivalence-report.json
```

The command exits 0 only for equivalence, 1 for a valid comparison with a
mismatch, and 2 for invalid or incomplete evidence. JSON object formatting and
key order do not affect the normalized comparison. JSONL line count and order,
array order, schemas, behavioral configuration, and rosters remain exact.

`--ignore-field` is repeatable and literal. It accepts integer-valued counter
fields whose safe identifier ends in `_exact`, plus the run-local
`output_directory` string needed when otherwise
identical A/B artifacts were written to different directories. Structural and
behavioral fields such as `schema`, `seed`, `config_id`, `tick`, `seq`, and
participant identities are protected and cannot be ignored. Every requested
counter or diagnostic ignore is confined to JSONL streams, so it cannot hide a
manifest or summary configuration difference. Every requested ignore must
occur, and the report records occurrence counts, first/last JSON
pointers, and a digest of every ignored pointer/value record. Each artifact
also records its raw and normalized SHA-256, and the first mismatch includes
its artifact, JSON pointer, and JSONL line where applicable. The report cannot
be written inside either input run, so the QA evidence remains immutable.

`--ignore-diagnostic-field` is a separate repeatable lane for one-sided,
read-only JSON payloads whose safe field name ends in `_diagnostics`. Its value
may be any finite JSON value, including an array or object, and receives the
same pointer/value occurrence audit. It cannot weaken the numeric `_exact`
counter rule or remove protected structural fields, and an unused diagnostic
ignore is an error.

## Current limits and future telemetry

Current engine builds may also emit `shadow-manifest.json` and
`shadow-decisions.jsonl`. These files contain read-only decisions from the
experimental policies while stock Botpack remains in control. This analyzer
does not treat hypothetical decisions as gameplay outcomes and intentionally
ignores the shadow stream until an action adapter and attributed combat/resource
telemetry exist.

Telemetry v2 still cannot honestly measure damage dealt, accuracy, opponent
strength, weapon/resource control, or objective progress. The report lists
those metrics as unavailable with their evidence requirements instead of
filling them with zeroes. Older games that lack a PRI or zone field emit the
schema's neutral sample for that field rather than dereferencing an invalid
generated property offset.

A future telemetry schema should add attributed, stable-participant events or
counters for:

- damage dealt/taken and exact causal environmental damage;
- hitscan shots/hits and finalized projectile hits/misses;
- weapon and pickup acquisition, firing intent, and objective interactions;
- movement goal/latent action, destination, route progress, and recovery
  reason;
- roster role, bot implementation ID, opponent implementation ID, and team.

Those fields must be reconciled against the final summary before they become
quality metrics. Quality comparisons should then use role-swapped fights and
exact map/seed pairs. Same-seed repetitions are determinism checks, not
independent evidence.

Run the deterministic contract tests with:

```powershell
$env:PYTHONDONTWRITEBYTECODE=1
python -m unittest discover -s .\Tools\BotBenchmark\tests -p 'test_*.py' -v
```
