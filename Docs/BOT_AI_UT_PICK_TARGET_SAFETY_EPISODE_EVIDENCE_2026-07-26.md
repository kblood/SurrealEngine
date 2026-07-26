# UT Deck16 PickTarget safety-episode evidence

Date: 2026-07-26  
Scope: bounded evidence note for the 16-bot, 7,200-tick UT436 Deck16
PickTarget stock/fixed investigation. This is **not** a complete death census:
trace processing was deliberately stopped after the first extracted terminal
snapshots. It must not be used to justify a movement or targeting change.

## Inputs and interpretation boundary

- Fixed capture inspected directly:
  `qa/runs/2026-07-26/ut436-deck16-16bot-s104729-pick-target-fixed-observer-7200-r1`.
- Corresponding 7,200-tick stock/fixed captures exist for seeds `104729` and
  `271828`, with identical initial-layout fingerprints within each seed.
- The observer evidence already recorded separately attributes observed UT
  `PickTarget` invocations to `Botpack.SniperRifle.Timer` (11 calls) and
  `Botpack.ShockRifle.Timer` (4 calls). That is caller provenance, not a
  weapon-to-death attribution.
- A death terminal snapshot contains the recorded target/navigation state and
  death partition, but it does not contain a weapon caller or a causal command
  ownership token. An empty `pick_target_records` array at death therefore
  means only that no retained PickTarget record is present in that sample.

## Exact terminal observations already extracted

All times/ticks below are the terminal telemetry sample; `PainTimer` is the
engine's recorded environmental source. Damage counters are cumulative for the
life/run, not a final-blow attribution.

| Tick (s) | Bot | Terminal state | Hazard / attribution | Target and movement evidence | Weapon/PickTarget evidence |
| --- | --- | --- | --- | --- | --- |
| 297 (4.950000258) | `TFemale1Bot1` (`pri:15`) | `Dying`, `Swimming`, `Continue`, health `-28` | in hazard zone; `pain_timer`; `unassisted_environmental_death`; no recent enemy or momentum contribution | `PlayerStart16`; movement intent true; velocity `(-108.139,114.651,35.879)`; acceleration `(-415.133,440.352,106.032)` | no retained PickTarget record |
| 741 (12.350000644) | `TMale2Bot1` (`pri:5`) | `Dying`, `Swimming`, `Continue`, health `-21` | in hazard zone; `pain_timer`; `unassisted_environmental_death`; no recent enemy or momentum contribution | `TMale1Bot1` (`pri:10`); movement intent true; velocity `(-116.425,-89.725,82.162)`; acceleration `(-311.762,-160.859,504.397)` | no retained PickTarget record |
| 801 (13.350000696) | `TMale1Bot2` (`pri:11`) | `Dying`, `Swimming`, `Continue`, health `-17` | in hazard zone; `pain_timer`; `unassisted_environmental_death`; no recent enemy or momentum contribution | `PathNode145`; movement intent true; velocity `(-27.792,156.369,25.215)`; acceleration `(-106.602,600.459,74.650)` | no retained PickTarget record |

The same extracted stream also contained direct enemy deaths, for example
`TFemale1Bot2` at tick 86 (enemy-player attribution; no hazard),
`TMale2Bot2` at tick 334, and `TFemale2Bot2` at tick 488. They are included
here only to prevent treating every `Dying` transition as a navigation
failure.

## What this proves

1. The fixed `104729` run has at least three terminal, unassisted,
   swimming-`PainTimer` deaths while bots have active movement intent and a
   target/navigation destination.
2. Those terminal samples do **not** establish a direct `PickTarget` call,
   `SniperRifle.Timer`, `ShockRifle.Timer`, `WarnTarget`, or any other weapon
   transition as the cause of any one water death.
3. They also do **not** establish that the current move target caused entry
   into the harmful water: no exact command-ownership witness is present, and
   prior hazard-residence work requires that witness before movement steering
   can be authorized.

## Correlation versus inference

The matched campaign has reported a UT safety regression after correcting the
living-pawn predicate, while the previously observed callers are weapon timers.
It is reasonable to investigate the post-selection weapon/evasion path,
including `WarnTarget`, as a *hypothesis*. It is not supported by this note as
a causal conclusion: the three recorded deaths lack both a retained weapon
caller and a proven owned movement command.

## Safety-candidate decision

No safety candidate is authorized from this evidence. In particular, do not:

- steer away from water based solely on map geometry or terminal swimming;
- suppress `PickTarget` for UT weapon callers; or
- alter `WarnTarget`/evasion based on the aggregate regression alone.

A future candidate must first collect, without changing behavior, a bounded
episode chain containing all of: (1) the exact `PickTarget` caller and selected
living target, (2) the resulting weapon/evasion transition, (3) an exact
engine-owned movement command, (4) harmful-water entry and terminal relation
for the same life, and (5) a deterministic counterfactual that removes the
unsafe owned command without degrading the cross-game matrix. Until then the
correct action is to hold the unconditional PickTarget correction from merge.
