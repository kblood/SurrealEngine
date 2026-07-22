# Retail UT99 bot oracle

`Run-RetailBotOracle.ps1` runs the original Unreal Tournament 436 bot code under
the retail `UCC.exe` and emits machine-readable reference metrics. It does not
write into the installed game directory.

## Isolation model

Each run creates a disposable runtime beneath its result directory:

- retail executables, DLLs, packages, and localization files are hard-linked
  (copied if hard links are unavailable);
- INI files are copied, never linked;
- Maps, Textures, Sounds, and Music are exposed through read-only-use directory
  junctions;
- server, compiler, and ngStats logs are redirected to the run directory;
- a before/after SHA-256 manifest verifies that the retail INIs and existing
  retail log files did not change;
- the runtime is removed after a successful run unless `-KeepRuntime` is used.

The temporary `RetailBotOracleGame` class inherits `Botpack.DeathMatchPlus`. It
only bypasses the retail dedicated server's requirement for a human connection,
sets the requested stock difficulty, adds stock bots, starts the match, ends it
after the requested duration, and records read-only snapshots. It does not
replace or modify `Botpack.Bot`.

## Run

```powershell
./Tools/BotBenchmark/Run-RetailBotOracle.ps1 `
  -GameRoot 'C:\Program Files (x86)\GOG Galaxy\Games\Unreal Tournament GOTY' `
  -Map 'DM-Morbias][' -Skill 7 -Bots 2 -Seconds 60 -Port 7799
```

Use `-TouchProbe` for the opt-in native overlap probe. It places one stock bot
inside five newly spawned health vials and records the resulting health and
four-slot `Touching` state as `touch_probe` in the summary. This isolates the
retail engine's behavior when more actors overlap than fit in `Actor.Touching`;
it is disabled for ordinary bot-performance oracle runs.

The unique result directory contains:

- `summary.json`: aggregate and per-bot metrics;
- `bot-metrics.csv`: per-bot tabular metrics;
- `retail-ngstats.log`: the original UTF-16 ngStats event stream;
- `installed-file-manifest.json`: the immutability check evidence;
- compiler and server stdout/stderr logs.

Compare one retail result to a Surreal benchmark summary with:

```powershell
./Tools/BotBenchmark/Compare-BotBenchmarkToRetail.ps1 `
  -SurrealSummary ./surreal-run/summary.json `
  -RetailSummary ./retail-run/summary.json `
  -Output ./comparison.json
```

Only compare matching map, skill, bot count, and duration scenarios. The compare
tool reports mismatches but does not pretend they are equivalent.

## Observable metrics

The stock ngStats stream provides bot names and IDs, effective internal skill,
novice mode, timestamped item pickups, kills/suicides, kill weapon and damage
type, and game duration. The read-only observer adds one-second samples of health, inventory
count, weapon count, total ammo, armor charge, selected weapon, UnrealScript
state, and location. From these, the parser derives:

- exact first/total item pickup events and pickup counts by retail item name;
- first additional weapon acquisition (bounded to the sample interval);
- maximum inventory, weapon, ammo, health, and armor values;
- kills, deaths, suicides, first-kill latency, weapon mix, and kills/minute.

## Limits and interpretation

Retail UT436 does not expose a supported RNG seed control. Results are therefore
an oracle distribution, not a deterministic golden trace. Run several retail
samples per case and compare medians/percentiles to matching Surreal cases.

The observer cannot expose native perception checks, route-search costs, item
desire scores, aim error, or exact pickup instants without modifying the retail
engine. Its one-second snapshots are sufficient for coarse weapon/ammo/health
collection rates and combat outcomes; Surreal's native JSONL telemetry remains
the source for causal diagnosis.

Ammo is not perfectly identical between summaries: retail reports all ammo in
inventory, while current Surreal summaries report useful ammo. Treat that row as
directional until both sides use the same definition.
