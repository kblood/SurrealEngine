# Retail `MinHitWall` oracle

These disposable UnrealScript packages measure the retail UE1 physics gate that
decides whether walking collision dispatches `HitWall`. It is intentionally
separate from the normal all-bot benchmark: the custom bot uses a dedicated
probe state, so a result proves native callback eligibility and not stock bot
decision quality.

The UT436 and Unreal Gold probes dynamically select a level `PlayerStart` and a horizontal
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
- case `2` instead creates a `TriggerOpenTimed` mover and requires its exact
  trace identity, one walking callback, `HandleDoor` entry/handled return,
  and an explicit skipped `PickWallAdjust` event;
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

Use `-Profile UT436` for `DM-Deck16][` or `-Profile Unreal226b` for
`DmMorbias`. The Unreal Gold 226b UCC server does not construct the legacy
local stat logger in this mode, so its package mirrors tagged events into the
per-child redirected server stream. The runner explicitly labels that retained
source as `system-log`; it is still unique to the isolated child/run ID and is
not combined with logs from other cases.

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
Gold counterpart now uses its own `Bots`/`BotInfo` package and passed the same
initial dynamic-blocker matrix on DmMorbias: head-on `-0.500000` produced one
walking callback at dot `-1.000000`; the nominal glancing contact was
suppressed at `-0.500000` and dispatched at `-0.350000` with observed dot
`-0.397680`. This cross-game agreement still requires repeated neighboring
thresholds and a separate mover profile before a shared engine dispatch
correction is eligible.

The mover-ordering gate also has a controlled result on both games, without
claiming an unpinned map lift: case `2` spawns a custom, trace-verified
`TriggerOpenTimed` mover. UT436 and Unreal Gold each recorded the same ordered
chain: walking `HitWall` → bot `HandleDoor` → mover `HandleDoor` returning
`True` with the probe as `WaitingPawn` and `SpecialPause=2.5` → bot skip of
`PickWallAdjust` → `mover_handled` terminal. The runner rejects any case with
a missing/reordered mover record or a `PickWallAdjust` result.

Use `-AllowMissingHitWall` only for an explicitly expected filtered case; its
run still requires preflight identity, a direct blocker-contact witness, and a
single terminal observation, so a missed wall or fall is not silently accepted
as filtered callback evidence.
