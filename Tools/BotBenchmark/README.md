# Bot benchmark matrix runner

`Run-BotBenchmarkMatrix.ps1` launches the GUI-subsystem `SurrealEngine.exe`
through `Start-Process`, waits for every deterministic benchmark run, validates
its process result and `summary.json`, and aggregates the matrix into CSV and
JSON.

The default matrix covers:

- `DM-Morbias][` and `DM-Deck16][`;
- bot skill levels `0`, `3`, `4`, `6`, and `7`;
- seeds `104729` and `271828`;
- two identical repetitions per map/skill/seed case, so digest determinism is
  checked rather than merely assumed.

## Usage

From the repository root:

```powershell
.\Tools\BotBenchmark\Run-BotBenchmarkMatrix.ps1 `
  -GameRoot 'C:\Program Files (x86)\GOG Galaxy\Games\Unreal Tournament GOTY' `
  -OutputRoot 'C:\BotBenchResults'
```

The engine path defaults to `build\Release\SurrealEngine.exe`. A smaller smoke
matrix can be selected without editing the script:

```powershell
.\Tools\BotBenchmark\Run-BotBenchmarkMatrix.ps1 `
  -GameRoot 'C:\Program Files (x86)\GOG Galaxy\Games\Unreal Tournament GOTY' `
  -OutputRoot '.\botbench-matrices' `
  -Maps 'DM-Morbias][','DM-Deck16][' `
  -Skills 0,7 `
  -Seeds 104729,271828 `
  -Seconds 5 `
  -RunsPerCase 2
```

`GameRoot`, `OutputRoot`, `EnginePath`, maps, skills, seeds, simulated seconds,
fixed delta, repetitions, bot count, and per-process timeout are configurable.
`OutputRoot` is mandatory. The script never clears or reuses it: each invocation
creates a unique timestamp/PID batch directory and each run receives its own
sanitized child directory. Drive roots are rejected.

For combat skill separation, `-OpponentSkill 4` changes each case from uniform
self-play into a two-bot candidate-versus-fixed-opponent duel. `-Skills 0..7`
then means candidate skill; the requested per-bot list is validated in every
summary, and the run rows include candidate/opponent score, deaths, acquisition
timing, score margin, and death advantage. `-OpponentSkill` requires `-Bots 2`.

The engine also accepts `--botbench-skills=7,4` for controlled mixed-skill
matches. Its entry count defines the bot count (and must match an explicit
`--botbench-bots` value). Without this option, `--botbench-skill` remains the
uniform skill for every requested bot. JSON benchmark configs use a numeric
`"skills": [7, 4]` array. Each spawn is normalized through UT's stock
`Bot.InitializeSkill`, and the trace records the resulting novice/internal
skill mapping.

## Results and validation

Each batch contains:

- `matrix-runs.csv`: one row per engine process, including requested inputs,
  process exit, benchmark status, digest, deaths, inventory, final bot states,
  and validation errors;
- `matrix-cases.csv`: digest agreement for each repeated map/skill/seed case;
- `matrix-results.json`: configuration, totals, case results, and all run rows;
- one directory per run containing `invocation.txt`, `events.jsonl`, and
  `summary.json`.

A run is valid only when the process exits with code zero, event and summary
files exist, benchmark status is `passed`, requested seed/map/skill/bot/tick
values match, and the digest is a 64-bit hexadecimal value. A case is
deterministic only when all repetitions are valid and produce one identical
digest. The script exits nonzero if any run is invalid or any case differs.

## Analysis and paired comparisons

`Analyze-BotBenchmark.py` groups valid matrix runs by map and skill and writes
distribution statistics for weapon timing, weapon/ammo maxima, health recovery,
damage/death proxies, score, and wall time. With two labeled inputs it also
makes exact map/skill/seed/repetition-paired comparisons:

```powershell
python .\Tools\BotBenchmark\Analyze-BotBenchmark.py `
  baseline=.\results\baseline\matrix-results.json `
  candidate=.\results\candidate\matrix-results.json `
  --output .\results\analysis-baseline-vs-candidate
```

The output directory must be new. `analysis.json` retains full distributions,
adjacent-skill median checks, and paired wins/ties/losses; `groups.csv` is a
compact table for plotting. Equal-skill self-play cannot establish combat skill
separation, so the analyzer labels that limitation explicitly.

`Validate-BotTrace.py <run-directory>` performs streaming structural checks on
one JSONL trace. It verifies sequence/tick ordering, route cost and cache order,
inventory eligibility/path-field consistency, respawn-eligibility rules, and
spectator isolation. It also reconciles exact damage, fatal/self-damage,
hitscan-shot, and damaging-projectile events with each per-bot/per-weapon
summary. Inventory `eligible` means that spawn/timing rules permit
consideration; it does not imply a route exists from the current navigation
component. Missing sight-acquisition or loss events are reported as coverage
warnings rather than invented successes.

## Controlled fixtures

Pass `-FixtureId controlled-reachability-blockall-v1` to the matrix runner (or
`--botbench-fixture=controlled-reachability-blockall-v1` directly to the
engine) to run the first short native contract fixture. JSON configs use
`"fixture_id": "controlled-reachability-blockall-v1"`. This fixture requires
one bot; it checks clear and dynamically blocked `ActorReachable`, documents
the dynamic-actor behavior of `PointReachable`, and verifies that reachability
dry runs preserve the pawn location.

Fixture traces add `fixture_setup`, `fixture_assert`, and `fixture_complete`.
The summary contains their ID/status/counts, and `Validate-BotTrace.py`
reconciles the complete event/summary contract. Fixture runs retain roster and
spectator isolation checks, but short fixtures do not pretend to cover normal
match pickup or travel gates.

## Retail UT436 reference oracle

`Run-RetailBotOracle.ps1` runs an unattended stock-bot match with the installed
retail `UCC.exe`, isolated behind copied configs and private logs. It emits
ngStats combat events plus read-only one-second resource snapshots and verifies
that retail INIs/logs are unchanged. `Compare-BotBenchmarkToRetail.ps1` produces
a coarse normalized comparison against a Surreal `summary.json`. See
`RetailBotOracle/README.md` for usage and interpretation limits.
