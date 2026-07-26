# Warning-dodge launch evidence — 2026-07-26

## Scope

This is a read-only result note for the stock-mode `WarnTarget` / `TryToDuck`
observer.  It records qualified launch snapshots only; it makes no engine,
Botpack, UnrealShare, or telemetry behavior change, and no UCC invocation was
used.

| Adapter and fixture | Run artifact | Result artifact |
| --- | --- | --- |
| UT436 `DM-Deck16][`, 16 bots, seed `104729`, 1,200 fixed ticks | `qa/runs/2026-07-26/ut436-deck16-warning-dodge-launch-stockmode-s104729-1200-r2/` | `quality.json` |
| Unreal Gold 226b `DmDeathFan`, 16 bots, seed `104729`, 7,200 fixed ticks | `qa/runs/2026-07-26/unreal226b-deathfan-warning-dodge-launch-stockmode-s104729-7200-r1/` | `quality.json` |

## Observations

The UT capture has an active warning observer over its 1,200-tick run, but it
records no `TryToDuck` observation and no qualified warning-dodge launch.
That is an absence of this narrowly qualified path in this fixture and budget,
not evidence that UT warning logic cannot dodge.

The longer Unreal capture completed and the analyzer reported no errors or
warnings.  Its exact terminal aggregate is:

| `WarnTarget` observations | nested `TryToDuck` observations | qualified launches | observer/launch overflow |
| ---: | ---: | ---: | ---: |
| 419 | 3 | 2 | 0 / 0 |

The two valid immediate post-call launch snapshots occurred at these observer
ticks and retained these movement targets:

| Observer tick | Retained `MoveTarget` |
| ---: | --- |
| 3040 | `LiftExit1` |
| 5250 | `LiftCenter0` |

The third nested `TryToDuck` did not satisfy the qualified-launch predicate.
The two qualified records establish that the stock Unreal path can reach the
observer's launch state while map navigation targets are live.  They do not
establish why either warning occurred or what happened after the snapshot.

## Causal boundary

No terminal causal claim is licensed by these results.  A launch record is an
immediate post-call observation, not evidence that the pawn subsequently fell,
entered a harmful zone, received `PainTimer` damage, or died.  In particular,
the target names above must not be interpreted as the cause of a later route,
hazard, or death outcome.

## Required next evidence

Extend the observer with a durable per-launch continuity token and retain it
through later latent actions and command changes.  A terminal claim must join,
without overflow or missing records:

1. one `WarnTarget` sequence to its nested `TryToDuck` sequence and qualified
   launch token;
2. the same participant and life ID through subsequent sampled movement and
   any command replacement or latent transition; and
3. an exact terminal in that same continuity chain: a certified fall or
   harmful-zone entry, followed where relevant by `PainTimer` harm and the
   death partition.

The join must explicitly censor life boundaries, run end, observer overflow,
and lost/replaced continuity before it can label a launch as causal.  Until
that tokenized terminal witness exists, these captures remain reachability
evidence only.
