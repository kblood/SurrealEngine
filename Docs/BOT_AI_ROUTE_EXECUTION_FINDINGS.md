# Route-execution findings

## Purpose

Record what the deterministic route-execution observer has established, and
separate it from hypotheses. This document does not authorize a bot behavior
change.

## Observer

Commit `d816ff23` adds `route-execution.jsonl` to each bot-benchmark run. For
every controlled bot and simulated tick it records, read-only:

- availability, position, velocity, and displacement from the prior sample;
- `MoveTarget` identity/class;
- ordered pawn `RouteCache` identity/class; and
- resolved zone identity/number.

It does not call pathfinding, collision queries, or write route state. The
existing benchmark telemetry remains the source for script state, latent
action, physics, health, and watchdog counters.

## Qualification evidence

UT436 `DM-Deck16][`, four bots, difficulty 7, fixed delta `0.016666699`,
7,200 ticks:

- Seed `271828`, repetition 1: 7,200 route records, 24,908 non-null
  `MoveTarget` observations, and 24,753 non-empty route-cache observations.
- The same seed, repetition 2: byte-identical `route-execution.jsonl`, SHA-256
  `FDC420E3260A28F903DFEB7B8D101D8AA3B7C02A0F0E63987E422AC97DDE9776`.
- Seed `104729`, repetition 1: the simple low-displacement screen found 14
  episodes of at least 30 ticks, including one 1,621-tick interval for
  participant `pri:3` with `InventorySpot163` and a stable 11-node route
  cache.

One-tick retail smoke captures verified the observer emits four participants
and one record in both UT436 `DM-Deck16][` and Unreal Gold 226b `DmDeathFan`.

## Correct interpretation of the apparent long stalls

The 1,621-tick UT interval is **not** currently a proven navigation failure.
At representative ticks 3817, 3900, 4000, and 4500, the normal benchmark
telemetry reported:

- UnrealScript state `Roaming`;
- walking physics;
- latent action `Sleep`; and
- zero move-stall detections and forced replans.

Later in the interval the latent action was `WaitForLanding`, then the bot
returned to `MoveToward` with a new target. The move-stall watchdog correctly
excludes these non-movement latent states. It must not be broadened merely
because `MoveTarget` and `RouteCache` remain populated while a script sleeps.

The engine implementation decrements `SleepTimeLeft` each `UActor::Tick` and
wakes the frame when it reaches zero (`UActor.cpp`, `UActor::Tick`). Exported
stock `UnrealShare.Bots` script also contains intentional `Sleep` calls in
roaming and combat transitions. Therefore neither the scheduler nor pathing is
implicated by the low-displacement screen alone.

## Rejected changes

Until active-movement evidence exists, reject:

- forcing a replan during `Sleep` or `WaitForLanding`;
- clearing `MoveTarget`/`RouteCache` because they remain set during a pause;
- treating all low-displacement samples as stalls; and
- adding a generic unstick nudge, teleport, or velocity injection.

## Next evidence gate

Define an active-navigation stall only when all of the following hold for a
continuous window:

1. bot is live and its latent action is `MoveToward`;
2. it has a live movement target;
3. the same-life consecutive position samples show less than the configured
   progress radius; and
4. the interval lasts beyond the existing two-second watchdog threshold.

For every such episode, correlate the observer route state with the existing
watchdog counters and recovery records. A change may be proposed only after
three independent instances identify one mechanism and a deterministic
counterfactual shows that the proposed recovery resolves those instances
without unexplained decisions on stall-free frames.

## Static-catalog route context correlation (2026-07-25)

`Analyze-RouteExecutionContext.py` now validates and joins the existing
per-tick route trace, v3 map catalog, and realized capability witness without
calling into the engine. It reports an **observed post-tick route-cache first
hop**, not a native path-selection decision: route-cache sampling can miss
transient commits, and more than one reachspec can share a node pair.

The initial 7,200-tick controls establish useful negative evidence:

- UT436 Deck16-II (`104729`) had 10,783 resolved active first-hop samples.
  Its one recorded death was in a hazard zone at tick 1,265 while latent action
  was `Continue`; no active first hop existed at that tick.
- Unreal Gold DeathFan (`271828`) had 5,170 resolved active first-hop samples
  and 25 death-counter increments. Every death tick was inactive for this
  screen (most were in a hazard zone), so no death may be attributed to its
  current route-cache edge.

These reports are context only and do not authorize a route, reach-flag, or
hazard-policy change. The next observer must capture native provenance at the
path commit: the selected reachspec index while `FindPathToEndPoint` unwinds,
the bounded cache prefix written by `SetRouteCache`, and the phase before
`SpecialHandling` can redirect the script target. It must record explicit
coverage/overflow status and qualify positive native selections in both game
families before it can be used for a behavior candidate.

## Rejected direct-actor timeout experiment (2026-07-25)

The seed `104729` trace contains a real active `MoveToward` stall against the
direct `enforcer13` actor near tick 5363. A temporary, opt-in experiment
ended that latent move by setting `MoveTimer` to `-1`, while excluding pawn
combat targets and navigation points. It is **not retained**.

Both 7,200-tick UT436 Deck16-II runs completed cleanly, but the enabled run
failed the quality gate: its forced-replan counter increased while no recovery
attribution counter reconciled with it. The reproducer is
`qa/runs/2026-07-25/direct-actor-timeout-v1/ut436-deck16-s104729-t7200-candidate-r2`.
The disabled paired run completed without that inconsistency.

This is useful evidence, not a behavior result. The next candidate must first
provide a deterministic, per-decision native record that partitions every
watchdog action before it is allowed to change `MoveTimer` for direct actors.

## Decision-time watchdog witness (v1)

The required witness now exists in telemetry v2 as the bounded
`move_stall_recovery_decisions` array. It emits exactly one record for every
native move-stall detection before any recovery write. Each record binds the
pawn life and episode to latent mode, target identity/class/liveness, timer,
and selector result. The analyzer rejects a run unless records plus their
explicit overflow counter partition detections, and `navigation_replan`,
`targetless_timeout`, and `direct_actor_move_toward_timeout` records partition
the aggregate forced-replan counters.

Fresh 7,200-tick stock-equivalent qualification runs passed the quality gate:

- UT436 Deck16-II, seed `104729`, difficulty 7: five decisions. Two were
  normal navigation replans; three selected `none`, including direct
  `enforcer13` at tick 5363 and direct `PAmmo1` at tick 7060.
- Unreal Gold 226b DmDeathFan, seed `271828`, difficulty 3: three decisions,
  all `none`; one was for `LiftExit2` and two had no current move target.

The evidence confirms that direct actor targets are a real, narrow recovery
gap in UT, but does not yet establish a safe recovery outcome. The next
behavior candidate must extend this witness with a new explicit decision value
and prove the same partition invariant on both game families.

## Opt-in direct-actor MoveToward timeout (v2, 2026-07-25)

The v2 experiment is now implemented behind the benchmark-only
`--botbench-direct-actor-move-toward-timeout=1` switch. It is disabled by
default. On a watchdog detection it may end only a live, non-navigation,
non-pawn `MoveToward` whose `MoveTimer` is finite and positive. Mover context
remains excluded. The action uses the existing stock recovery handoff
(`Acceleration = 0`, `MoveTimer = -1`) rather than injecting motion or
rewriting a route.

The telemetry has a distinct native
`move_stall_direct_actor_move_toward_timeouts_exact` counter and decision
value. The quality analyzer requires the exact invariant:

```text
forced replans = navigation replans + targetless timeouts + direct-actor timeouts
```

Qualification to date passed the quality gate:

- UT436 `DM-Deck16][`, seed `104729`, difficulty 7, 7,200 ticks: the disabled
  run had zero direct-actor timeouts; the enabled run had exactly one. Aggregate
  kills changed from 7 to 6, deaths from 17 to 14, and suicides from 10 to 8.
  The enabled repeat emitted a byte-identical `events.jsonl` with SHA-256
  `D5C265108A2BDBECADDA359D230DFB55F65B26221A5060B75568B41038814BB8`.
- Unreal Gold 226b `DmDeathFan`, seed `271828`, difficulty 3, 7,200 ticks:
  enabled and disabled runs had identical measured kill/death/suicide metrics
  and zero direct-actor timeouts.

This is a qualified, deterministic test hook, not a release-ready bot behavior
change. It still needs activating evidence from at least two additional
independent map/seed cases and paired combat/resource telemetry before it can
be enabled outside benchmark experiments or proposed as an improvement.
