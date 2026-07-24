# Hazard swim egress v1 observer

`hazard-swim-egress-v1` investigates a reproducible UT99 `DM-Deck16][` death:
on seed `271828`, Alys falls directly into a static positive-DPS water volume,
drifts in it, and dies. This is not a walking preflight or pain-ledge case.

The option is default-off and is only selected by the deterministic benchmark
flag `--botbench-hazard-swim-egress=1`. It currently observes only; it never
changes acceleration, movement targets, physics, or script state.

## Safety contract

- An episode requires the pawn's primary zone and center/foot/head evidence to
  be static water, with the primary zone an exact finite positive-DPS pain
  zone.
- A candidate needs a finite anchor captured while the same living stock bot
  was already swimming and all three zones were non-harmful.
- An anchor is discarded on any falling or walking tick and when swimming exits
  water. It cannot cross a non-swimming transition, a water exit, or a life
  boundary.
- The gate is keyed by life and episode and reports authorization at most once
  per episode. The current observer does not interpret a zero debounce count as
  coverage, because it evaluates each episode once.

## Telemetry and result

The benchmark emits exact per-bot episode, eligibility, authorization,
debounce, no-anchor, exit, death-before-exit, and forced-replan counters. The
quality analyzer accepts the eight counters only as a complete group and
checks conservative episode/eligibility/terminal bounds.

Two deterministic observer runs were completed on 2026-07-25 with four
skill-7 UT bots, 1,800 ticks, `0.0166667` fixed delta, and seed `271828`:

`qa/runs/2026-07-25/hazard-swim-egress-v1-observer/ut-deck271828-observer-runs`.

Both runs recorded exactly one Alys episode and one no-anchor rejection, with
zero eligible and authorized actions. The fall-to-water entry therefore has no
safe prior swimming anchor. This is the intended fail-closed result and means
that live egress steering is not ready to implement from this anchor model.

The next experiment must observe a separately proven, recent safe location
across the falling-to-water boundary before any live action is considered. It
must retain default-off behavior and demonstrate that the original death is
removed without a safety regression on UT99 and Unreal held-out matrices.
