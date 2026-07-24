# Harmful-zone escape v1 experiment

Status: implemented as an opt-in benchmark experiment; not qualified for release or merge.

## Scope

`--botbench-harmful-zone-escape=0|1` is default-off and accepted only by the
headless bot-benchmark driver.  It is recorded in the benchmark configuration
identity, manifest, summary, invocation, and run identity.  Ordinary desktop
and game sessions do not enable it.

The runtime observes positive-DPS pain-zone boundaries for stock UT99 `Bot`
and Unreal Gold `Bots` pawns.  An action is allowed only for an alive,
authority-owned, walking bot with a live movement latent action, a finite
incoming horizontal direction, and a non-water zone.  It is debounced to one
attempt per harmful-zone episode.  The action probes reverse, left, and right
at 64 units; every selected direction must have a clear sweep, static walkable
support, and a non-water foot region outside an exact positive-DPS pain zone.
It changes only acceleration and the established `MoveTimer = -1` replan
signal.  Falling, swimming, callbacks, movers, unknown support, and missing
navigation intent fail open.

## Required counters

Per-pawn telemetry reports exact harmful-zone escape episodes, center and foot
entries, recovery attempts, successful exits, forced replans, and no-safe-
candidate outcomes.  The analyzer rejects impossible success/replan counts.

## First result

The first controlled matrix is preserved under
`C:\Devstuff\QuestGames\SurrealEngine\qa\runs\2026-07-24\harmful-zone-escape-v1`.

- UT99 Deck16-II, seeds 104729, 271828, and 314159, two baseline and two
  candidate repetitions each: exact repeats; candidate safety totals were
  unchanged and all candidate escape action counters were zero.
- Unreal Gold DmDeathFan, seed 424242, two candidate repetitions: exact
  repeats; all escape action counters were zero while the existing 3
  unassisted environmental deaths and 7 hazard entries remained.

This is an intentional non-promotion: the observed harmful entries are mainly
falling/callback paths, which v1 correctly declines to steer after entry.  A
zero-action candidate is not a bot-quality improvement.

## Next experiment

Before adding any live pre-commit veto, retain the existing observer-only
walking-step/falling evidence and derive a deterministic table of which
pre-`MayFall` forecasts lead to the above deaths.  A new action requires a
separate opt-in mode, exact positive-DPS truth, episode telemetry, paired
non-regression on all three Deck seeds and DeathFan, and at least one removed
hazard-exposed death.  Morpheus remains held out until one candidate passes
those tuning gates.
