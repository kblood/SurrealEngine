# Retail `MinHitWall` oracle

This disposable UnrealScript package measures the retail UE1 physics gate that
decides whether walking collision dispatches `HitWall`. It is intentionally
separate from the normal all-bot benchmark: the custom bot uses a dedicated
probe state, so a result proves native callback eligibility and not stock bot
decision quality.

The UT436 probe dynamically selects a level `PlayerStart` and a horizontal
path with clear world geometry and walkable floor samples. It spawns a
temporary `BlockAll` at the selected path, then requires an expanded actor
trace to identify that exact blocker before the bot moves. Case `0` is a
head-on contact; case `1` applies an 89-unit lateral blocker offset, targeting
a glancing contact close to `-0.4`. The actual callback normal and dot, when a
callback is delivered, are the evidence; the nominal geometry is not treated
as a measurement.

The probe emits tab-separated `minhitwall_oracle` records through the normal
local stat log:

- `preflight_blocker`, bilateral `*_bump`, `move_begin`, and
  `postflight_blocker` establish blocker identity and physical contact;
- `hitwall_pre` records the threshold, native pre-handler velocity, contact
  normal, computed dot, wall, walking physics value, and active state;
- `handle_door_pre` / `handle_door_post` and `pick_wall_adjust_*` establish
  mover callback ordering when a future mover-specific profile produces that
  collision; the static comparator run does not claim that coverage.

Run it only from a timestamped isolated retail runtime. That runtime must copy
its `System` directory, compile `RetailHitWallOracleUT` locally with `UCC make`,
and use read-only links or copies for the retail asset folders. A before/after
hash inventory of the installed retail root is mandatory. No package, INI, map,
or log may be written into the installed game.

`Run-RetailMinHitWallOracle.ps1` creates that isolated runtime, redirects
`Engine.StatLog` to its own `Logs` directory, records a full before/after
SHA-256 inventory of the installed root, compiles the package locally, and
runs every requested case as a dedicated UCC process. For each case it accepts
exactly one new or changed UTF-16 local stat log, copies it to the case
directory, and parses only records bearing that case's unique run ID. The
manifest records the copied log hash/size, exact parsed records, process ID,
timeout, and termination method.

For example:

```powershell
pwsh .\Tools\BotBenchmark\Run-RetailMinHitWallOracle.ps1 `
  -RetailRoot 'C:\Program Files (x86)\GOG Galaxy\Games\Unreal Tournament GOTY' `
  -OutputRoot .\qa\runs\2026-07-25\retail-minhitwall-ut436
```

The runner refuses an existing output directory and fails if any installed
retail file changes. Use `-KeepRuntime` only when retaining the otherwise
disposable runtime is necessary to diagnose a failed compilation or process.

The verified UT436 dynamic-contact runs are:

- `v23`: head-on `MinHitWall=-0.500000`, one walking (`Physics=1`)
  `HitWall` callback, dot `-1.000000`;
- `v24`: glancing `MinHitWall=-0.500000`, bilateral blocker contact and no
  callback; and
- `v25`: glancing `MinHitWall=-0.350000`, one walking callback at observed dot
  `-0.397676`.

These observations support the documented threshold gate and its expected
direction, but do not prove its exact `<` versus `<=` comparator at the
floating-point boundary. They also do not establish mover ordering. An Unreal
Gold counterpart must use its own `Bots`/`BotInfo` package and dynamic blocker
smoke before a shared engine dispatch correction is eligible.

Use `-AllowMissingHitWall` only for an explicitly expected filtered case; its
run still requires preflight identity, a direct blocker-contact witness, and a
single terminal observation, so a missed wall or fall is not silently accepted
as filtered callback evidence.
