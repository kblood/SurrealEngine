# AllianceTelemetry

An UnrealScript mutator that both retail `DeusEx.exe` and Surreal Engine load, so
the same probe observes both engines and their captures can be compared directly.
Everything it reports is visible to script, which means a divergence in a capture
is a divergence the game itself would see.

Findings and corrections produced with it are recorded in
`Docs/DeusExBehaviorParity.md`.

## Layout

- `Classes/AllianceTelemetryGameInfo.uc` - the `GameInfo` the map is launched with
- `Classes/AllianceTelemetryMutator.uc` - the probe: sampling, config, emitters
- `Classes/*Probe.uc` - focused probes for tick and physics ordering
- `harness.ps1` - build and run wrapper

## Setup

`ucc` will not compile from a path containing spaces, and it compiles from a real
directory rather than a junction back into this repository. The harness therefore
copies `Classes\*.uc` into a game directory - `C:\DXGame` by default - and builds
there. That directory needs a Deus Ex install with `ucc.exe`, and
`AllianceTelemetry` registered in `EditPackages`.

## Running

```powershell
.\harness.ps1 -Build
.\harness.ps1 -Run both -Tag mytest -TimeoutSec 260 -Ini @{
    PopGenerator = 'PigeonGenerator0'
    PopOffset    = 400
    AutoQuitDelay = 120
}
```

Captures land in `captures\<tag>-<engine>.raw`.

## Things that will cost you a run

- **`AllianceTelemetry.ini` persists between runs.** A key you leave out of `-Ini`
  keeps whatever the previous run set. Restate every key that matters, every time.
- **Run length is `AutoQuitDelay`.** There is no `SampleSeconds` config var.
- **Surreal Engine needs a larger `-TimeoutSec` than retail** for the same
  `AutoQuitDelay`. A run reported as `TIMEOUT` still produces a usable capture.
- **Retail writes UTF-16LE and Surreal Engine writes UTF-8.** Decode by testing
  whether the second byte is zero.
- **`ucc` caps a string constant at 256 characters.** Split long header literals
  with `$`.
- **Keep `PopOffset` at 1000 or below.** Past roughly 1200 units retail stops
  ticking its birds; beyond the 1500-unit `ActiveArea` nothing generates at all.
- **Run animal probes for at least 120 seconds.** Retail's first `LoudNoise`
  arrives around t=90s, and a shorter window reads as a state divergence that is
  really just a short capture.
- **`SamplePawnGenerators` runs at the tail of `SampleEnemyGate`**, which returns
  early unless `TeleportTargetName` names a real `ScriptedPawn`.

## Capture format

Tab-separated. Each row starts with a prefix naming its record type, and the file
carries its own column headers as `#\tcols_<prefix>\t<space-separated names>`.
Read those headers rather than assuming a column order - the layout has changed
as probes were added.

Prefixes in use: `K V P W S T R G D I E C N F L Q A B Y Z X J O H U M`, plus `DR`,
`AW`, `TA`, `PT`, `TO`, `PO`, `EB`, `PG`, `AL`, `SC`, `PN`, `PC`, `PB`, `NZ`, `NP`.

## Probe design

Two rules earned the hard way:

- Isolate before concluding. Most first hypotheses in this work were wrong, and a
  probe that varies one input is the cheap way to find out.
- Prefer a controlled emitter over ambient content. Comparing which actor happened
  to make a noise is not comparable across engines; comparing a noise of known
  volume and radius from a known point is. The `NoiseProbe*` config vars exist for
  exactly this, and they settled the AI event attenuation model.
- A bird that has already fled is deaf to the next probe. `bNoiseProbeReset` puts
  them back into `Wandering`, but it must fire a second early: `GotoState` called
  from outside a pawn does not take effect before that pawn's next tick, and a
  bird that has not re-registered its callback reads as a refused delivery.
