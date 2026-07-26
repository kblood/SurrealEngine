# Held-out map matrix screening — 2026-07-26

## Scope and decision

This is an owner-local screening recommendation for future *paired*, 16-bot,
7,200-tick qualification.  It deliberately does not tune a candidate on these
maps.  `DM-Deck16][`, `DM-Morbias][`, and `DmDeathFan` remain development
anchors and are not held-out evidence.

Use the following primary pair after a behavior candidate has been frozen:

| Game | Primary held-out URL | Why it is useful | Static catalog evidence |
|---|---|---|---|
| UT436 | `DM-Pressure?Game=Botpack.DeathMatchPlus` | Dense multi-level combat plus timed movers, water transitions, a pressure-zone hazard, jumps, and special traversal. | 256 navigation points; 1,833 reachspecs; 11 movers; 4 lift centers / 23 lift exits; two water zones; 44 swim, 207 jump, and 46 special reachspec incidences. |
| Unreal Gold 226b | `DmTundra?Game=UnrealShare.DeathMatchGame` | Independent water/mover/hazard and route-density generalization check.  It is intentionally more safety-sensitive than the compact DeathFan anchor. | 200 navigation points; 2,282 reachspecs; three movers; 3 lift centers / 10 lift exits; two water zones; 201 swim, 509 jump, and 20 special reachspec incidences. |

Use these as secondary held-out coverage, not substitutes for the primary
pair:

| Game | Secondary held-out URL | Purpose | Static catalog evidence |
|---|---|---|---|
| UT436 | `DM-Morpheus?Game=Botpack.DeathMatchPlus` | Vertical/low-gravity and mover/landing behavior, separated from the water-heavy Pressure result. | 131 navigation points; 655 reachspecs; eight lift centers / 22 lift exits; 40 special reachspec incidences; `Botpack.VacuumZone`; 16 PlayerStarts. |
| Unreal Gold 226b | `DmHealPod?Game=UnrealShare.DeathMatchGame` | Mover/lift execution without Tundra's water-heavy graph. | 152 navigation points; 1,315 reachspecs; six movers; 3 lift centers / 10 lift exits; 225 jump and 20 special reachspec incidences. |

`DM-Fractal` and `DmCurse` loaded and have valid catalogs, but are reserve
maps.  Fractal has six WarpZoneInfo actors, which makes route-coverage changes
harder to interpret before warp traversal has an exact execution witness.
Curse is a viable compact mover fallback, but contributes less distinct hazard
coverage than Tundra.

## Evidence collected

All collection used SurrealEngine's headless drivers; no UCC, UnrealEd, or
commercial content export was used.  The six read-only catalogs below all pass
`Tools/BotBenchmark/Validate-MapCatalog.py`:

`C:\Devstuff\QuestGames\SurrealEngine\qa\runs\2026-07-26\heldout-map-catalog-screen-v1`

The folders are `ut-pressure`, `ut-morpheus`, `ut-fractal`, `unreal-tundra`,
`unreal-healpod`, and `unreal-curse`.  They are owner-local evidence and are
not Git content.

Two bot-run checks establish that the selected primary maps are practical for
the intended benchmark lifecycle:

| Map | Runtime evidence | Result |
|---|---|---|
| `DM-Pressure` | 16 bots, seed `104729`, difficulty 3, fixed delta `0.016666667`, 7,200 ticks | Complete UT436 run: 47 kills, 48 deaths, one suicide, 11 movement-intent stuck events, 15 confirmed pickups, and 0.64453125 union navigation coverage. |
| `DmTundra` | 16 bots, same seed/difficulty/delta, 1,200 ticks screening smoke | Complete Unreal 226b run: 17 kills, 20 deaths, three suicides, zero movement-intent stuck events, and 0.235 union navigation coverage.  This proves load/spawn/combat and flags Tundra as a deliberately demanding safety check; it is **not** a 7,200-tick qualification result. |

The full Pressure artifact is at
`qa/runs/2026-07-26/heldout-map-matrix-v1/ut436-pressure-16bot-s104729-r1`.
The shorter Tundra smoke is at
`qa/runs/2026-07-26/heldout-map-matrix-v1/unreal226b-tundra-16bot-s104729-t1200-r1`.
Both have complete manifest, summary, realized-capability witness, event,
route-execution, and shadow-decision artifacts.  Raw event streams are retained
because the safety investigation is active; their retention decision remains
with the BOT AI owner.

## Required held-out qualification protocol

For each frozen candidate, run an unchanged stock/repaired-stock baseline and
candidate at the same URL, seed, bot count, skill, fixed delta, and disabled
experimental controls.  Run each side twice and require exact repeat
equivalence before comparing the pair.  Begin with seed `104729`; add a second
predeclared seed only after the first pair is reproducible.  Do not select or
drop a map based on its candidate result.

Use 16 bots, 7,200 ticks, difficulty 3, and fixed delta `0.016666667`:

```powershell
SurrealEngine.exe --autoplay --headless-driver=bot-benchmark `
  --botbench-url='DM-Pressure?Game=Botpack.DeathMatchPlus' `
  --botbench-output=<owner-local-output> `
  --botbench-seed=104729 --botbench-ticks=7200 `
  --botbench-fixed-delta=0.016666667 --botbench-difficulty=3 `
  --botbench-bots=16 '<UT436 game directory>'

SurrealEngine.exe --autoplay --headless-driver=bot-benchmark `
  --botbench-url='DmTundra?Game=UnrealShare.DeathMatchGame' `
  --botbench-output=<owner-local-output> `
  --botbench-seed=104729 --botbench-ticks=7200 `
  --botbench-fixed-delta=0.016666667 --botbench-difficulty=3 `
  --botbench-bots=16 '<Unreal Gold 226b game directory>'
```

Require the existing structural validators, manifest/roster/capability binding,
and exact stream comparison.  Compare combat, deaths/suicides, environmental
and harmful-residence attribution where available, movement stalls, navigation
coverage, traversal-specific route execution, and the map's active hazard
episodes.  A candidate is not qualified by a good aggregate score on one map;
it must be non-regressive on both games and explain any safety delta with
attributed evidence.

## Non-goals

This document does not claim that a map's authored reachspecs prove a route is
safe, that a catalog establishes bot behavior, or that the screened maps make
the current BOT AI merge-ready.  It only fixes the held-out map selection and
records the evidence needed to execute the full paired matrix reproducibly.
