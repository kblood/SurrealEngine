# Minimal cross-game bot qualification matrix — 2026-07-26

## Purpose

This note fixes the smallest owner-local matrix that can test a frozen BOT AI
candidate across UT436 and Unreal Gold 226b without treating one map as a
general bot-quality result. It is a plan, not merge evidence.

All cells use 16 bots, difficulty 3, seed `104729`, 7,200 ticks, and fixed
delta `0.016666667`. Freeze both the candidate and this matrix before running
the baseline/candidate pairs.

## Frozen matrix

| Cell | Exact URL | Required feature evidence | Why it belongs in the minimum |
|---|---|---|---|
| UT anchor | `DM-Deck16][?Game=Botpack.DeathMatchPlus` | 251 navigation points / 1,997 ReachSpecs; controlled runs exercise combat, direct inventory movement, vertical routes, water-hazard residence, and collision pressure. | Main UT combat/navigation anchor; it has the strongest existing direct-command and swimming-death provenance. |
| Unreal anchor | `DmDeathFan?Game=UnrealShare.DeathMatchGame` | 116 navigation points / 1,431 ReachSpecs; the catalog contains `UnrealShare.Fan2`; stock seed-104729 has high terminal/environmental hazard pressure and 0.9224137931 union coverage. | Main Unreal combat/hazard anchor; it exercises the distinct 226b lifecycle and void/fan-style terminal risk. |
| UT held-out | `DM-Pressure?Game=Botpack.DeathMatchPlus` | 256 navigation points / 1,833 ReachSpecs, 11 movers, 4 LiftCenters, 23 LiftExits, two WaterZones, a PressureZone, and 44 swim / 207 jump / 46 special incidences. | Water, timed-mover, lift, and pressure-zone generalization. Stock completed with 47 K, 48 D, one suicide, and 0.64453125 union coverage. |
| Unreal held-out | `DmTundra?Game=UnrealShare.DeathMatchGame` | 200 navigation points / 2,282 ReachSpecs, three movers, 3 LiftCenters, 10 LiftExits, two WaterZones, and 201 swim / 509 jump / 20 special incidences. | Unreal water/lift generalization. Stock completed with 95 K, 102 D, seven suicides, and 0.43 union coverage. |

The Pressure/Tundra catalogs are owner-local under
`qa/runs/2026-07-26/heldout-map-catalog-screen-v1/`; each passed
`Tools/BotBenchmark/Validate-MapCatalog.py`. `DM-Morpheus` and `DmHealPod`
remain reserves for low-gravity and mover-only follow-up. They are not swapped
into the matrix after a candidate result is known.

## Capture protocol

For every cell, retain repaired-stock r1/r2 and frozen-candidate r1/r2 with
manifest, summary, realized-capability witness, events, route trace, and
quality report. This is 16 full matches per candidate. All live behavior
controls except the declared candidate must be false. Observer lanes are
separate from quality lanes unless their schema already passes the analyzer.

The controlled game adapters are fixed:

```text
UT436:       ?Game=Botpack.DeathMatchPlus
Unreal 226b: ?Game=UnrealShare.DeathMatchGame
```

## Fail-closed validity gates

Before comparing quality values, every repetition must have:

- complete `summary.json`, exit code 0, 7,200 ticks, 16 actual participants,
  exact game/version/map/seed/difficulty/delta, and only the declared control;
- a successful full-stream `Analyze-BotQuality.py` result with no partition or
  overflow error;
- a realized-capability witness that validates against manifest and summary;
- exact baseline r1/r2 and candidate r1/r2 equivalence via
  `Compare-BotBenchmarkRuns.py`, ignoring only output-directory identity;
- a validated matching map catalog; and
- for a navigation candidate, zero-overflow exact route-execution/native-commit
  and direct-command links for the claimed mechanism. Static graph data or an
  old RouteCache entry never proves route causation.

The former summary-v3 compatibility blocker is resolved by commit `92c59bc5`:
`Validate-RealizedBotCapabilities.py` now validates the required v3
`ai_frame_timing` structure fail-closed while preserving v2 support. Its
focused tests and saved 16-bot UT/Unreal artifacts pass. The realization-witness
gate remains mandatory for every new matrix cell; do not waive it.

## Outcome criteria after validity passes

Judge every cell independently before any cross-map summary.

| Area | Non-regression in every cell | Required positive evidence |
|---|---|---|
| Survival | Suicides, deaths, unassisted environmental deaths, and relevant map hazard terminals do not increase. | Declared safety mechanism activates with exact ownership and an attributed beneficial terminal in two independent cells. |
| Movement | Movement-intent stuck events do not increase; union navigation coverage does not decrease; integrity/overflow stays zero. Raw HitWall totals are not a quality verdict. | Intended recovery/terminal counter activates in two cells with exact route/direct-command provenance. |
| Combat | Both kills and match score (`kills_exact - deaths_exact`) do not decrease. | At least one combat measure improves without a survival or movement regression in that cell. |
| Lifts/water/void | Pressure/Tundra retain valid route and hazard evidence; Deck16/DeathFan retain anchor hazard evidence. | A traversal-safety claim has positive evidence on a water-or-void/fan cell and a lift/mover cell. |

A candidate that is inert, wins only on an aggregate, or improves Unreal while
degrading UT fails. Merge readiness requires every relevant gate and retained
owner-local evidence.

## Present status

The map selection and stock opportunities exist, but no current BOT AI
candidate passes this matrix. Targetless timeout is inert on DeathFan;
wall-adjust/pain-ledge lacks active ownership; hazard egress lacks a causal
command chain; and unconditional PickTarget has mixed UT/Unreal results. The
next step is observer/fixture qualification, not enabling a movement policy.
