# BotTelemetry

An UnrealScript mutator for UT99 (v469e) that emits tab-separated telemetry
from a retail match: per-tick pawn state, damage, death and frag events, plus
several one-shot native-behaviour probes (water-jump checks, world traces,
navigation-point reachability, the raw reachspec graph, and a CanSee/
LineOfSightTo vision sweep). The intent is to
record what the retail engine actually does so a reimplementation (e.g. a
from-scratch bot/engine port) can be compared against it record for record.

Source files in this folder (copied verbatim from a working UT99 install,
unmodified):

- `Classes/BotTelemetryMutator.uc` — the mutator.
- `Classes/BotTelemetryLog.uc` — a `StatLogFile` subclass used as the log sink.
- `run-telemetry-match.ps1` — launches a UT99 match with the mutator attached.

## What it does

`BotTelemetryMutator` hooks `Tick`, `MutatorTakeDamage`, `PreventDeath`,
`ScoreKill` and `HandleEndGame`, and writes one tab-separated line per event
to `bottelemetry.log` via `BotTelemetryLog`. Every line starts with a
single-character record type. The `#` lines emitted in `PostBeginPlay` (and
elsewhere) document the column layout for the other record types; the tables
below are transcribed from those `cols_*` lines and from the `Emit(...)`
call sites.

### Record types

| Type | Meaning | Columns (in order) |
|---|---|---|
| `#` | Header / footer / metadata lines (not fixed-width; see below) | varies, see below |
| `S` | Per-sample pawn state, one row per live pawn per sample tick | `seq ms name physics health x y z vx vy vz zone zoneflags footflags headflags movetarget route0 route1 base state enemy orders` |
| `H` | Damage taken (from `MutatorTakeDamage`, only if `bLogDamage`) | `seq ms victim damage type instigator health_before footflags zoneflags headflags` |
| `D` | Death (from `PreventDeath`) | `seq ms victim killer type x y z zone` |
| `K` | Frag credited (from `ScoreKill`) | `seq ms killer victim` (no `cols_K` header line is emitted; inferred from the `Emit` call, which passes `InstigatorName(Killer)` then `InstigatorName(Other)`) |
| `W` | Water-jump probe (`ProbeWaterJump`, only if `bProbeWaterJump`, emitted per sampled pawn while swimming or in/at a water or pain zone) | `seq ms name yaw x y z radius height maxstep t1_hit t1_class t1_nx1000 t1_ny1000 t1_nz1000 t2_hit t2_class verdict physics zone` |
| `T` | World trace corpus (`DumpTraceCorpus`, only if `bProbeTraceCorpus`, once at match start) | `idx node dir kind sx sy sz ex ey ez hit hitclass hx hy hz nx1000 ny1000 nz1000` |
| `R` | Navpoint-pair reachability (`DumpReachCorpus`, only if `bProbeReachCorpus`, once at match start) | `idx from to dist dz actorreachable pointreachable` |
| `G` | Raw navigation graph dump (`DumpNodeGraph`, only if `bProbeNodeGraph`, once at match start) | `idx name class x y z extracost endpoint playeronly paths upstream pruned visnoreach` |
| `V` | Navpoint-pair vision sweep (`DumpVisionCorpus`, only if `bProbeVisionCorpus`, once at match start) | `idx obs tgt dist dz yaw cos1000 periph1000 sightradius visibility cansee los` |

Column notes:

- All coordinates and velocities are truncated to integers (`int(...)`); the
  `*1000` suffixed columns (`t1_nx1000`, `nx1000`, etc.) are unit-normal
  vector components scaled by 1000 and truncated, to avoid emitting floats.
- `S.zone`/`S.zoneflags` describe `P.Region` (the pawn's current zone);
  `footflags`/`headflags` describe `P.FootRegion`/`P.HeadRegion` the same way.
  A zone-flags value is `.` or a combination of `P` (pain zone) / `W` (water
  zone), followed by `/` and the zone's `DamagePerSec`, e.g. `P/5.0`.
- `S.route0`/`S.route1` are `P.RouteCache[0]`/`[1]` (the bot's current path),
  `-` if none.
- `S.orders` is `<Bot.Orders>/<ActorName(Bot.OrderObject)>` for bot pawns,
  `-` for non-bots (`Bot(P) == None`).
- `H.health_before` is `Victim.Health` read *before* damage is applied
  (`MutatorTakeDamage` runs pre-damage).
- `W.t1_*` comes from a trace along the pawn's forward yaw at its own height
  (mirrors the first trace inside the engine's `CheckWaterJump`); `W.t2_*`
  from a second trace 1.1×`MaxStepHeight` higher up. `W.verdict` is the
  actual return value of `P.CheckWaterJump()`, called after both probe
  traces, so the probe traces are diagnostic only — they don't drive the
  bot's real path.
- `T.kind` is one of `box` / `zero` / `boxact` / `zeroact`: `box` traces use
  a collision extent of `(ProbeRadius, ProbeRadius, ProbeHeight)` with
  `bTraceActors=false`; `zero` traces use a zero extent (line trace) with
  `bTraceActors=false`; `boxact`/`zeroact` repeat both with
  `bTraceActors=true`. `T.dir` is 0–3, one of 4 traces per navpoint at 0/90/
  180/270 degrees yaw (`d * 16384` Unreal rotation units).
- `R.actorreachable`/`R.pointreachable` are `1`/`0` for
  `Pawn.ActorReachable(B)`/`Pawn.PointReachable(B.Location)`, asked from a
  hidden, non-colliding `Botpack.TMale1` spawned at each `A` in turn. Only
  pairs within `ProbeReachDist` are tested/emitted.
- `G.paths`/`upstream`/`pruned` are the raw 16-slot `Paths`/`upstreamPaths`/
  `PrunedPaths` reachspec-index arrays on the `NavigationPoint`, comma
  joined. `G.visnoreach` is the same for `VisNoReachPaths`, but with each
  populated slot resolved to the target navpoint's name (or `-`).
- `V.obs`/`V.tgt` are navigation-point names, not pawn names, asked from a
  hidden, non-colliding `Botpack.TMale1` observer against a second,
  visible, non-colliding `Botpack.TMale1` target. Only every
  `ProbeVisionStride`'th navpoint is used as an observer, and only target
  pairs within `ProbeVisionDist` are tested/emitted. For each pair, the
  observer's `PeripheralVision` is swept over `0.7`, `0.0`, `-0.2` in turn,
  and for each of those, `V.yaw` sweeps `ProbeVisionYawSteps` evenly spaced
  samples over a full turn (Unreal rotation units, `Pitch`/`Roll` held at
  `0`). `V.cos1000` is `1000 * (Normal(target - observer) dot
  vector(observer.Rotation))`, truncated; `V.periph1000` is `1000 *
  Observer.PeripheralVision`, truncated; `V.sightradius` and `V.visibility`
  are the observer's `SightRadius` and the target's `Visibility`,
  truncated; `V.cansee`/`V.los` are `1`/`0` for
  `Observer.CanSee(Target)`/`Observer.LineOfSightTo(Target)`.

### `#` lines

The first `#` line is the stream header:
`# bottelemetry v1 map=<Outer name> title=<Level.Title> game=<GameInfo class> hz=<SampleHz>`.
This is followed by one `# cols_X ...` line per record type listed above
(`S`, `H`, `D`, `W`, `T`, `R`, `G`, `V` — there is no `cols_K` line, see the
`K` row above). During/after the one-shot probes, additional `#` status
lines appear: `# corpus_done <count>`, `# reach_probe <class> r=.. h=..
step=.. player=.. walk=.. swim=.. fly=..`, `# reach_skip <navpoint>
setlocation`, `# reach_corpus_done <count>` or `# reach_corpus_failed
<reason>`, `# nodegraph_done <count>`, `# vision_probe <class> r=.. h=..
sightradius=.. periphdefault=..`, `# vision_skip <navpoint> setlocation`,
`# vision_corpus_done <count>` or `# vision_corpus_failed <reason>`. At
match end (`HandleEndGame`), `DumpScores`
writes `# scores name frags deaths bot`, then one `# score <name> <frags>
<deaths> <bot>` line per pawn with a `PlayerReplicationInfo`, then
`# end <ms>`.

## Install

1. Copy this `BotTelemetry` folder (as-is, including `Classes\`) into the
   parent of the UT99 `System` directory, so you end up with
   `<UT99 root>\BotTelemetry\Classes\BotTelemetryMutator.uc` and
   `...\BotTelemetryLog.uc` sitting alongside the other package folders
   (`Botpack`, `Engine`, etc.).
2. Edit `System\UnrealTournament.ini`. In the `[Editor.EditorEngine]`
   section, add `EditPackages=BotTelemetry` **after** the existing
   `EditPackages=...` lines.
3. If `System\BotTelemetry.u` already exists (e.g. from a previous build),
   delete it first — `UCC.exe make` will not rebuild a package whose `.u` is
   already present and up to date.
4. From the `System` directory, run:
   ```
   UCC.exe make
   ```
   This should produce `System\BotTelemetry.u`.

## Running

The mutator is attached via the standard `?Mutator=` URL option:

```
DM-Deck16][?Mutator=BotTelemetry.BotTelemetryMutator
```

### `run-telemetry-match.ps1`

Launches `UnrealTournament.exe` with the mutator attached, using parameters
matching a fixed bot benchmark configuration (`DeathMatchPlus`, 16 bots,
skill 3, `DM-Deck16][`). It writes `System\BotTelemetry.ini` before launch so
the run is reproducible, then starts the game. Parameters (all optional):

| Parameter | Default | Meaning |
|---|---|---|
| `-Bots` | `16` | Bot count; `MinPlayers` is set to `Bots + 1` |
| `-Difficulty` | `3` | Game skill level |
| `-TimeLimit` | `5` | Match time limit in minutes (`?TimeLimit=`) |
| `-Map` | `DM-Deck16][` | Map to load |
| `-SampleHz` | `30` | Written to `BotTelemetry.ini` as `SampleHz` |
| `-ProbeWaterJump` | `$true` | Written to `BotTelemetry.ini` as `bProbeWaterJump` |
| `-ProbeTraceCorpus` | `$true` | Written to `BotTelemetry.ini` as `bProbeTraceCorpus` |
| `-ProbeReachCorpus` | `$false` | Written to `BotTelemetry.ini` as `bProbeReachCorpus` |
| `-ProbeVisionCorpus` | `$false` | Written to `BotTelemetry.ini` as `bProbeVisionCorpus` |
| `-ProbeVisionRotMode` | `0` | Written to `BotTelemetry.ini` as `ProbeVisionRotMode` |
| `-ExitAfterProbes` | `$false` | Written to `BotTelemetry.ini` as `bExitAfterProbes` |

The script also archives any pre-existing `Logs\bottelemetry.log` /
`.tmp` by timestamp-renaming it before the run, and fails fast if
`UnrealTournament.exe` or `System\BotTelemetry.u` isn't found (with a
reminder to run `UCC.exe make`).

Note: the script always writes `bBotsOnly=False` into `BotTelemetry.ini` (not
exposed as a script parameter). It does not write `bProbeNodeGraph` at all —
that field is left at whatever is already in `BotTelemetry.ini`, or at the
mutator's own default of `True` if the ini has no such line yet — so the
node-graph dump (`G` records) runs on a fresh install unless disabled by
hand.

## Config (`System\BotTelemetry.ini`)

All `var config` fields on `BotTelemetryMutator`, under
`[BotTelemetry.BotTelemetryMutator]`. Defaults are the class's
`defaultproperties`, used if the ini has no entry.

| Var | Default | Controls |
|---|---|---|
| `SampleHz` | `30.0` | Sample rate for `S` (and, where applicable, `W`) records. `Tick` accumulates delta time and calls `Sample()` once per `1/SampleHz` seconds. If `<= 0` at `PostBeginPlay`, it's reset to `30`. |
| `bLogDamage` | `True` | Emit `H` (damage) records from `MutatorTakeDamage`. |
| `bBotsOnly` | `False` | If `True`, `Sample()` only emits `S` (and `W`, if applicable) rows for pawns with `bIsPlayer` set; if `False`, all living pawns are sampled. |
| `bProbeWaterJump` | `True` | If `True`, `Sample()` also calls `ProbeWaterJump` (emitting a `W` row) for any sampled pawn that is swimming, or whose region/foot region is a water zone, or whose foot region is a pain zone. |
| `bProbeTraceCorpus` | `True` | If `True`, run `DumpTraceCorpus()` once in `PostBeginPlay` (emits `T` rows, one set of 4 traces × 4 directions per `NavigationPoint`). |
| `bProbeReachCorpus` | `True` | If `True`, run `DumpReachCorpus()` once in `PostBeginPlay` (emits `R` rows for reachable navpoint pairs within `ProbeReachDist`). |
| `bProbeNodeGraph` | `True` | If `True`, run `DumpNodeGraph()` once in `PostBeginPlay` (emits `G` rows, one per `NavigationPoint`). |
| `bProbeVisionCorpus` | `False` | If `True`, run `DumpVisionCorpus()` once in `PostBeginPlay` (emits `V` rows for navpoint pairs within `ProbeVisionDist`, observers strided by `ProbeVisionStride`). |
| `ProbeReachDist` | `1000.0` | Max straight-line distance between navpoint pair `A`/`B` for `DumpReachCorpus` to test/emit that pair. |
| `ProbeVisionDist` | `800.0` | Max straight-line distance between observer/target navpoint pair for `DumpVisionCorpus` to test/emit that pair. |
| `ProbeVisionYawSteps` | `32` | Number of evenly spaced observer yaw samples per pair per `PeripheralVision` value in `DumpVisionCorpus` (covers a full turn). |
| `ProbeVisionStride` | `16` | Only every `ProbeVisionStride`'th `NavigationPoint` (walked via `nextNavigationPoint`) is used as an observer in `DumpVisionCorpus`; values `<= 0` are treated as `1`. |
| `ProbeVisionRotMode` | `0` | Which rotation `DumpVisionCorpus` turns during the yaw sweep: `0` body `Rotation`, `1` `ViewRotation`, `2` both. `cos1000` is measured against whichever one is being swept, so a mode that turns a rotation the engine's cone does not read shows up as an answer independent of the sweep. |
| `bExitAfterProbes` | `False` | If `True`, close the log and quit the game as soon as the probe dumps finish, instead of waiting for the match to end. The probes do not need a match to run. |
| `ProbeRadius` | `17.0` | Collision-extent X/Y (and the reach-probe pawn's implicit size comes from `Botpack.TMale1` itself, not this var) used for the `box`/`boxact` traces in `DumpTraceCorpus`. |
| `ProbeHeight` | `39.0` | Collision-extent Z used for the `box`/`boxact` traces in `DumpTraceCorpus`, and (via `P.CollisionHeight`) reused as the Z half-height reported in `W` rows for the live pawn (not driven by this var — that one comes from the pawn's actual collision). |
| `ProbeDist` | `60.0` | Trace length from each `NavigationPoint`, per direction, in `DumpTraceCorpus`. |

(`ProbeRadius`/`ProbeHeight` only feed the world-trace corpus extent, not
the water-jump probe or the reach-corpus probe pawn — those use the actual
pawn's or `TMale1`'s own collision size.)

## Output

Written to `..\Logs\bottelemetry.log` relative to the `System` directory
(i.e. `<UT99 root>\Logs\bottelemetry.log`), as tab-separated lines.

The log is opened as `bottelemetry.tmp` and only renamed to
`bottelemetry.log` when the match actually ends (`HandleEndGame` →
`DumpScores` → `Sink.StopLog()`, which flushes and closes the file; the
`.tmp` → `.log` rename is handled by `StatLogFile`'s own log-final-name
mechanism using `StatLogFinal`). **Let the configured time limit expire**
instead of quitting or Alt-F4'ing out of the match — if the match doesn't
end cleanly, the log stays as `bottelemetry.tmp` and is never finalised.

## Encoding warning

On some UT99 builds, `StatLogFile`/`FileLog()` writes the log as UTF-16LE
(with a byte-order mark), not plain ASCII/UTF-8, despite the ini being
written as ASCII. Any parser reading `bottelemetry.log` must sniff for a
UTF-16LE BOM (`FF FE`) rather than assuming a single-byte encoding.

## Requirements

- UT99 GOTY (retail game files: `Botpack`, `Engine`, etc.).
- Tested against the OldUnreal v469e patch.
