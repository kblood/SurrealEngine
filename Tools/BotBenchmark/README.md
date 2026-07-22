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

### Role-swapped skill qualification

`Analyze-BotSkillQualification.py` is the strict confirmatory analyzer for
mixed-skill crossover matrices. It requires a manifest because a matrix file
cannot infer whether the higher tier occupied the first/candidate or second/
opponent slot:

```json
{
  "schema": 1,
  "mode": "synthetic_test",
  "inputs": [
    {
      "label": "7v6-higher-candidate",
      "path": "q1-7v6-higher-candidate/matrix-results.json",
      "higher_tier": 7,
      "lower_tier": 6,
      "orientation": "higher_candidate"
    },
    {
      "label": "7v6-lower-candidate",
      "path": "q1-7v6-lower-candidate/matrix-results.json",
      "higher_tier": 7,
      "lower_tier": 6,
      "orientation": "lower_candidate"
    }
  ]
}
```

The compact manifest above is a one-pair plumbing smoke, so it must use
`synthetic_test`; that mode is permanently non-qualifying. A real
`mode: qualification` manifest must contain exactly the seven adjacent pairs
`1v0` through `7v6` and exactly one matrix for each orientation. Every matrix
must independently contain the same predeclared Cartesian grid: maps
`DM-Morbias][`, `DM-Deck16][`, and `DM-Phobos`, crossed with the analyzer's
fixed 24-seed S1 list. This produces 72 cases per orientation, 1,008 independent
orientation trials, and 504 role-swapped crossover units across the family.
Stable candidate/opponent profile IDs are mandatory.
Qualification Q1 uses exactly `RunsPerCase=1`; repeated same-seed processes are
diagnostic and are not independent qualification evidence.

Paths are relative to the manifest unless absolute. Each qualification input
also requires the lowercase SHA-256 of its matrix. Qualification inputs cannot
be split into disjoint seed blocks. Run:

```powershell
python .\Tools\BotBenchmark\Analyze-BotSkillQualification.py `
  --manifest .\qualification\manifest.json `
  --output .\qualification\analysis `
  --bootstrap-draws 10000 --bootstrap-seed 7436991
```

Qualification is an exact, fail-closed protocol. Each matrix must declare
protocol ID `surreal-bot-skill-qualification-v1`, game class
`Botpack.DeathMatchPlus`, no fixture, 5,400 ticks at `1/60` fixed delta (90
seconds), exact integer configuration/count fields, and reconciled root totals,
cases, and runs. It must also declare an existing engine binary and its matching
lowercase SHA-256, an existing canonical `game_root`, and a hashed JSON content
manifest. That content manifest has integer `schema: 1`, the exact protocol ID
and versioned scope, and a `packages` array
of `{path, sha256}` objects. It must contain exactly these case-sensitive
root-relative logical paths, with no omissions or additions:

```text
System/Core.u
System/Engine.u
System/BotPack.u
System/UnrealShare.u
System/UnrealI.u
System/SE-User.ini
System/SE-UnrealTournament.ini
Maps/DM-Morbias][.unr
Maps/DM-Deck16][.unr
Maps/DM-Phobos.unr
```

Each file is resolved beneath `game_root` with exact physical casing, read, and
hashed. Absolute paths, traversal, separator aliases, duplicate logical or
resolved targets, and symlinks escaping the root are rejected. All 14 inputs
must resolve to the same canonical game root and carry the same content-manifest
SHA-256. The same protocol and immutable identity hashes must be present in
every run. `content_closure` additionally freezes every currently present file
under `System/*.u`, `Textures/*.utx`, `Sounds/*.uax`, and `Music/*.umx`
(extensions case-insensitive), plus the two active SE INIs and three canonical
maps. The analyzer independently re-enumerates that closure.

The matrix runner has an explicit fail-closed capture mode. It rejects any
departure from the canonical maps, S1 seeds, adjacent skill pair, two bots,
90-second duration, 1/60 delta, one run per case, no fixture, or exact
Loque/Tamerlane profiles. A safe preflight hashes and rechecks the closure
without launching benchmark processes:

```powershell
.\Tools\BotBenchmark\Run-BotBenchmarkMatrix.ps1 `
  -QualificationProtocol -QualificationPreflightOnly `
  -GameRoot 'C:\Path\To\Unreal Tournament GOTY' `
  -OutputRoot .\qualification-preflight `
  -Maps 'DM-Morbias][','DM-Deck16][','DM-Phobos' `
  -Skills 7 -OpponentSkill 6 -Bots 2 -RunsPerCase 1 -Seconds 90 `
  -Seeds 104729,130363,155921,181081,207073,233021,259033,285007,311041,337069,363073,389029,415021,441011,467003,493067,519031,545023,571021,597031,623011,649069,675067,701009
```

Real capture omits `-QualificationPreflightOnly`. Every row requires the exact
`<run_id>/events.jsonl`, `summary.json`, `invocation.txt`, and
`trace-validation.json` evidence layout and binds each file by SHA-256. The
strict validator report must bind its event/summary hashes and raw map, seed,
ordered skills/profiles, digest, scenario, bot count, tick count, and fixed
delta back to that matrix row. Claim-driving score, adjudicated-death, damage,
acquisition, skill, and profile fields must also match the bound validated
summary; copied, shared, or self-consistently fabricated evidence fails closed. Engine
and content identities are rechecked after capture, so mid-matrix mutation also
fails the result.

The output directory must be new. `qualification-analysis.json` contains SHA-256
hashes for the manifest, every matrix, and this versioned tool; it also records
the command parameters and reconciled engine/content/duration/tick/fixed-delta/
scenario/fixture/profile provenance. It contains input
audits, seed-clustered bootstrap intervals, a seed-cluster sign test, Holm-
adjusted one-sided p-values, paired rank-biserial and Cliff/sign effects,
map/role reversal flags, exposure counts, and predeclared separation flags.
`crossover-units.csv` is one joined map/seed/tier-pair unit per row;
`pairs.csv` is the compact verdict table.
The JSON report sets `claim_scope` to `adjacent_skill_monotonicity` and always
sets `godlike_evaluated=false` and `godlike_qualified=false`; the Godlike anchor,
controls, retail comparison, and collection/safety gates are separate work.

Every matrix run must be valid, deterministic, exactly two-bot, and carry the
expected candidate/opponent tiers and exact-combat fields. Same-seed
`RunsPerCase` repetitions must share a digest, protocol/identity fields, and
critical metrics; they are collapsed to one orientation trial and reported as
determinism duplicates, never counted as independent evidence. Missing role
swaps, overlapping labeled
inputs, metric disagreement, provenance/config mismatch, partial profile data,
Cartesian gaps/duplicates, negative or impossible counters, noncanonical hashes
or seeds, exposure-total disagreement, or orientation mismatches fail the
command. A statistical separation flag also requires positive point effects on
at least two thirds of maps. Damage-share
bootstrap estimates, reversal checks, and gates all use the median specified by
the qualification design, not the mean.

Synthetic and canonical contract tests cover successful crossover joins,
qualification statistics/output, duplicate collapse, and adversarial rejection
of family/grid, protocol, identity, Cartesian, type/range, repetition-drift, and
exposure-reconciliation false passes:

```powershell
python .\Tools\BotBenchmark\tests\test_analyze_bot_skill_qualification.py
```

Statistical scope is intentionally narrow. Bootstrap intervals are deterministic
seed-cluster percentile intervals, not BCa intervals. The one-sided exact sign
test first sums score margins across maps within each seed, excludes zero seed
clusters from its binomial denominator, and Holm-adjusts only the tier pairs in
that manifest. Score ties still contribute 0.5 to reported superiority. Current
matrix rows expose participant score/death/damage and acquisition timing but
only aggregate shot/projectile exposures, so the tool cannot attribute accuracy
or self-damage to a tier. Qualification traces partition damage by configured
roster into participant-opponent, self, `environment_null`, and
external-nonparticipant categories. They separately record TakeDamage fatal
diagnostics and exact outermost `GameInfo.Killed` outcomes, including direct
deaths that bypass TakeDamage. External contamination must be zero;
environmental and self outcomes reconcile instead of being falsely rejected.
The analyzer reports evidence; it does not implement
sequential stopping decisions.

### Stable named crossover profiles

Mixed-skill matrices use fixed roster/profile roles. Candidate slot 0 is Loque
and opponent slot 1 is Tamerlane by default; swap only their skill values
between orientation runs. The matrix runner selects summary metrics by
`roster_index`, validates the canonical player names/profile IDs, and writes
`candidate_profile_id` and `opponent_profile_id` to every run, case, and matrix
configuration:

```powershell
& .\Tools\BotBenchmark\Run-BotBenchmarkMatrix.ps1 `
  -GameRoot 'C:\Program Files (x86)\GOG Galaxy\Games\Unreal Tournament GOTY' `
  -OutputRoot '.\q1-7v6-higher-candidate' `
  -EnginePath '.\build\Release\SurrealEngine.exe' `
  -Maps 'DM-Morbias][' -Skills 7 -OpponentSkill 6 -Bots 2 `
  -CandidateBotName Loque -OpponentBotName Tamerlane `
  -Seeds '104729' -RunsPerCase 2 -Seconds 3 -FixedDelta (1.0/60.0)
```

Run the lower-candidate orientation with `-Skills 6 -OpponentSkill 7` while
keeping both names in the same roster slots. Explicit names use the stock
`BotConfig.DesiredName` + `ForceAddBot` path, must be distinct and match the bot
count, and fail setup if the actual PRI name differs. No random-profile fallback
is accepted. Legacy uniform and controlled-fixture runs retain the singular
`-BotName` path for compatibility.

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
match pickup or travel gates. Controlled-fixture matrices invoke that validator
for every run and save `trace-validation.json`; raw `HitWall` lifecycle,
geometry, state, and callback count are independently validated and hashed.

`controlled-hitwall-blockall-v1` requires one bot and should be given at least
three simulated seconds. It waits for the stock bot to land naturally, builds
the same deterministic nine-actor BlockAll wall, starts the real native
`MoveToward` latent action without relocating the pawn, and lets normal outer
level ticks produce the collision. It proves the paired actor-blocker
`HitWall` callback and the stock non-mover `Roaming.AdjustFromWall` response,
then destroys its runtime actors and stops as soon as the contract passes:

```powershell
& .\Tools\BotBenchmark\Run-BotBenchmarkMatrix.ps1 `
  -GameRoot 'C:\Program Files (x86)\GOG Galaxy\Games\Unreal Tournament GOTY' `
  -OutputRoot '.\botbench-controlled-hitwall-v1' `
  -EnginePath '.\build\Release\SurrealEngine.exe' `
  -Maps 'DM-Morbias][' -Skills 7 -Seeds '104729' -Bots 1 `
  -RunsPerCase 2 -Seconds 3 -FixedDelta (1.0/60.0) `
  -TimeoutSeconds 60 -FixtureId 'controlled-hitwall-blockall-v1'
```

Walking-wall diagnostics use monotonically increasing `hit_id` values shared
by `walking_hit_wall` and `walking_hit_wall_result`. The validator requires
every before event to have exactly one matching result and checks the extended
state, latent-action, destination, focus, move-timer, and `bFromWall` fields.
Fixture runs may stop before their configured tick ceiling after successful
cleanup; ordinary benchmark runs still require the exact requested tick count.

`controlled-death-outcomes-v1` requires exactly two explicitly named profiles
(`Loque,Tamerlane`), `Botpack.DeathMatchPlus`, and enough ceiling for three
bounded stock respawns (12 simulated seconds is sufficient for the canonical
Morbias seed). It freezes the inactive bot and executes real stock-script
paths: lethal null-instigator `TakeDamage`, roster-opponent `gibbedBy`, and
`FellOutOfWorld`. The trace validator independently requires one linked fatal
environment damage event, three adjudicated deaths, two direct deaths, exact
per-slot summary reconciliation, and two nested `GameInfo.Killed` dispatches
collapsed into one outcome per action. It also validates the full ordered
setup/action/result/respawn/cleanup lifecycle and final live-bot cleanup:

```powershell
& .\Tools\BotBenchmark\Run-BotBenchmarkMatrix.ps1 `
  -GameRoot 'C:\Program Files (x86)\GOG Galaxy\Games\Unreal Tournament GOTY' `
  -OutputRoot '.\botbench-controlled-death-outcomes-v1' `
  -EnginePath '.\build\Release\SurrealEngine.exe' `
  -Maps 'DM-Morbias][' -Skills 7 -OpponentSkill 6 -Seeds '104729' `
  -Bots 2 -CandidateBotName 'Loque' -OpponentBotName 'Tamerlane' `
  -RunsPerCase 2 -Seconds 12 -FixedDelta (1.0/60.0) `
  -TimeoutSeconds 60 -FixtureId 'controlled-death-outcomes-v1'
```

This controlled fixture does not relax `-QualificationProtocol`: Q1 evidence
continues to require an empty fixture ID.

### Fail-closed controlled-death provenance capture

Use `-ControlledDeathProtocol` when the controlled death fixture is intended as
reproducible acceptance evidence rather than an ordinary fixture smoke. This
mode is separate from skill qualification and accepts only the canonical
contract: `DM-Morbias][`, seed `104729`, skill 7 versus 6, two runs, two bots,
Loque/Tamerlane, 12 seconds at 1/60, and fixture
`controlled-death-outcomes-v1`. The requested ceiling is 720 ticks; the fixture
must complete and clean up at tick 300.

```powershell
& .\Tools\BotBenchmark\Run-BotBenchmarkMatrix.ps1 `
  -GameRoot 'C:\Program Files (x86)\GOG Galaxy\Games\Unreal Tournament GOTY' `
  -OutputRoot '.\botbench-controlled-death-provenance-v1' `
  -EnginePath '.\build\Release\SurrealEngine.exe' `
  -Maps 'DM-Morbias][' -Skills 7 -OpponentSkill 6 -Seeds '104729' `
  -Bots 2 -CandidateBotName Loque -OpponentBotName Tamerlane `
  -RunsPerCase 2 -Seconds 12 -FixedDelta (1.0/60.0) `
  -TimeoutSeconds 60 -FixtureId 'controlled-death-outcomes-v1' `
  -ControlledDeathProtocol
```

The matrix records protocol ID
`surreal-bot-controlled-death-outcomes-v1`, scenario
`controlled-death-outcomes`, engine SHA-256, the same exhaustive UE1 content
closure used by qualification capture, and relative paths plus SHA-256 for the
invocation, events, summary, and validator report of each repetition. It
requires the validator's controlled-death report contract, exact raw run
identity, deterministic duplicate digest, and pre/post engine/content identity
reverification. Any parameter drift, missing evidence, tick mismatch, content
change, validation failure, or nondeterminism fails the matrix. This protocol
proves the controlled death-outcome telemetry contract only; it cannot be used
as skill-separation or Godlike evidence.

## Retail UT436 reference oracle

`Run-RetailBotOracle.ps1` runs an unattended stock-bot match with the installed
retail `UCC.exe`, isolated behind copied configs and private logs. It emits
ngStats combat events plus read-only one-second resource snapshots and verifies
that retail INIs/logs are unchanged. `Compare-BotBenchmarkToRetail.ps1` produces
a coarse normalized comparison against a Surreal `summary.json`. See
`RetailBotOracle/README.md` for usage and interpretation limits.
