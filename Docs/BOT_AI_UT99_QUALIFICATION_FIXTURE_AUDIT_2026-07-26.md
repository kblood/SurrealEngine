# UT99 unattended-match and qualification-fixture audit

## Confirmed test mechanism

UT99 already supports unattended, bot-only deterministic matches.  The
benchmark match controller suppresses automatic bots, logs in a spectator
viewport, creates exactly the requested bots itself, and fails if the realized
roster differs.  The matrix runner launches the engine with `--autoplay` and
`--headless-driver=bot-benchmark`; it does not use UCC, UnrealEd, or player
input.

This is the approved integration-test path for BOT AI changes.  It records
fixed-tick telemetry, the realized roster, initial positions, and a structural
analysis result.

## Existing UT99 evidence

- `DM-Deck16][?Game=Botpack.DeathMatchPlus`: complete 16-bot, 7,200-tick
  captures, including the current clearance observer artifact.
- `DM-Pressure?Game=Botpack.DeathMatchPlus`: one complete 16-bot, 7,200-tick
  capture at seed `104729` in
  `qa/runs/2026-07-26/heldout-map-matrix-v1/ut436-pressure-16bot-s104729-r1`.
- `DM-Morbias][?Game=Botpack.DeathMatchPlus`: complete 16-bot 1,200-tick
  smoke at seed `271828`.
- `DM-Morpheus` and `DM-Fractal`: static headless catalog/load evidence only;
  neither yet has a 16-bot match artifact.
- `DM-Phobos`: no local load, catalog, or bot-match evidence.  It must not be
  used for qualification until a bounded headless catalog and a 16-bot smoke
  pass.

## Layout-fingerprint finding and repair

Initial layout fingerprints include per-bot initial positions.  At seed
`104729`, Deck16 and Pressure have different fingerprints, so a single global
layout declaration cannot truthfully qualify both maps.

Commit `d9a0550b` repairs this in
`Tools/BotBenchmark/Run-BotBenchmarkMatrix.py` with
`matrix.map_start_layouts`:

1. Layouts are keyed to exact map URLs and must cover the manifest's map set.
2. Unknown, malformed, duplicate, or mismatched map layout entries fail before
   running the engine.
3. Layout ID, seed, map URL, and expected fingerprint are bound into run and
   pair identities and into emitted metadata.
4. Existing `seeds` and global `start_layouts` manifests remain compatible.

The focused runner suite passed 30 tests and the full BotBenchmark test suite
passed 239 tests after this repair.

## Recommended UT99 progression

1. Keep Deck16 as the deterministic diagnosis and tuning anchor.
2. Use Pressure as the first 7,200-tick cross-map qualification case.
3. Run a 16-bot, 7,200-tick Morpheus smoke before treating it as additional
   tuning coverage.
4. Screen Fractal separately because warp zones make it a distinct traversal
   case.  Screen Phobos only after basic headless-load evidence exists.
5. Freeze three observed stock layout fingerprints per map before comparing a
   behavior candidate.  Do not use a layout hash from one map for another.

## Current release limitations

The checked-in release campaign remains intentionally fail-closed for three
claims that no current run proves: role-swapped participant-policy coverage,
causal avoidability of every environmental death, and a 16-bot in-engine AI
frame p95 below 2 ms.  Pressure's existing observation-scope p95 is 3.242 ms,
which is not a valid substitute for the in-engine performance metric.
