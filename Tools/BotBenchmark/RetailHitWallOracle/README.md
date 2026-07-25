# Retail `MinHitWall` oracle

This disposable UnrealScript package measures the retail UE1 physics gate that
decides whether walking collision dispatches `HitWall`. It is intentionally
separate from the normal all-bot benchmark: the custom bot uses a dedicated
probe state, so a result proves native callback eligibility and not stock bot
decision quality.

The initial UT436 probe uses the static corridor face south of the pinned
`DM-Deck16][` `Mover0` profile. Case `0` is a head-on move from
`(1264,1550,-1222)` to `(1264,1900,-1222)`; case `1` targets a nominal
`-0.4` glancing dot from `(1264,1550,-1222)` to `(1900,1820,-1222)`. The probe
emits
tab-separated `minhitwall_oracle` records through the normal local stat log:

- `move_begin` / `move_return` establish setup and completion;
- `hitwall_pre` records the threshold, native pre-handler velocity, contact
  normal, computed dot, wall, and active state;
- `handle_door_pre` / `handle_door_post` and `pick_wall_adjust_*` establish
  mover callback ordering when a future mover-specific profile produces that
  collision; the static comparator run does not claim that coverage.

Run it only from a timestamped isolated retail runtime. That runtime must copy
its `System` directory, compile `RetailHitWallOracleUT` locally with `UCC make`,
and use read-only links or copies for the retail asset folders. A before/after
hash inventory of the installed retail root is mandatory. No package, INI, map,
or log may be written into the installed game.

`Run-RetailMinHitWallOracle.ps1` creates that isolated runtime, records a full
before/after SHA-256 inventory of the installed root, compiles the package
locally, and runs every requested case as a dedicated UCC process. For example:

```powershell
pwsh .\Tools\BotBenchmark\Run-RetailMinHitWallOracle.ps1 `
  -RetailRoot 'C:\Program Files (x86)\GOG Galaxy\Games\Unreal Tournament GOTY' `
  -OutputRoot .\qa\runs\2026-07-25\retail-minhitwall-ut436
```

The runner refuses an existing output directory and fails if any installed
retail file changes. Use `-KeepRuntime` only when retaining the otherwise
disposable runtime is necessary to diagnose a failed compilation or process.

The initial matrix is one process per case for head-on and glancing approaches
at `OracleMinHitWallMilli=-500` and `-350`. It is not sufficient to claim the
exact comparator at the floating point boundary; that requires boundary cases
after the first runtime/compilation smoke passes. An Unreal Gold counterpart
will use its own `Bots`/`BotInfo` package and a separately pinned mover profile.

Use `-AllowMissingHitWall` only for an explicitly expected filtered case; its
run record still includes the observed `hitwall_event_count` so a missing event
is not silently converted into a pass for a callback-required case.
