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
  "concurrency": 4,
  "timeout_seconds": 180,
  "repetitions": 1
}
```

Paths are resolved relative to the manifest. `game.family` is recorded as
provenance; map URLs are never rewritten, so each game family can retain its
own class and URL options. The current unified bot driver still requires UT's
`Botpack.DeathMatchPlus`; recording other families does not imply that their
engine-side bot setup is implemented yet. `bot_count` defaults to one.
`per_bot_skills` and `requested_names` are optional, but when present each must
contain exactly `bot_count` entries. Skills are integers from zero through
seven. Names must be non-empty, trimmed, comma-free, and unique under the
engine's ASCII case-insensitive comparison. Roster configuration is included
in deterministic run and pair IDs.

The current UT436 benchmark spectator does not expose the stock
`AddBotNamed` command. Supplying `requested_names` is therefore parsed and
recorded deterministically but the engine run deliberately fails instead of
silently substituting random profiles. Omit names for runnable matrices until
the named-spawn contract is independently verified.
All executables receive:

```text
--autoplay --headless-driver=bot-benchmark
--botbench-url=... --botbench-output=... --botbench-seed=...
--botbench-ticks=... --botbench-fixed-delta=... --botbench-difficulty=...
--botbench-bots=... [--botbench-skills=...] [--botbench-names=...]
<game root>
```

Preview exact commands without launching anything or creating the output:

```powershell
python .\Tools\BotBenchmark\Run-BotBenchmarkMatrix.py `
  --manifest .\bot-matrix.json --output .\bot-matrix-results --dry-run
```

Run the matrix and produce a quality report only if every process exits zero,
writes all three required non-empty artifacts, and passes the structural
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

## Current limits and future telemetry

Current engine builds may also emit `shadow-manifest.json` and
`shadow-decisions.jsonl`. These files contain read-only decisions from the
experimental policies while stock Botpack remains in control. This analyzer
does not treat hypothetical decisions as gameplay outcomes and intentionally
ignores the shadow stream until an action adapter and attributed combat/resource
telemetry exist.

Telemetry v1 cannot honestly measure kills, deaths, score, damage dealt,
accuracy, opponent strength, weapon/resource control, or objective progress.
The report lists those metrics as unavailable with their evidence
requirements instead of filling them with zeroes.

A future telemetry schema should add attributed, stable-participant events or
counters for:

- score, kills, deaths, damage dealt/taken, and self/environment damage;
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
