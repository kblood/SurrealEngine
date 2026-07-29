# Shadow-policy recovery baseline (2026-07-26)

## Decision

The current shadow stream does **not** support enabling a utility recovery
action. It has no stuck-time observation, neither policy selected
`recover-from-stuck` at any of the 19 exact watchdog detections, and stock
outcomes classify 17 of those detections as intentional stops. The two
remaining UT cases are one ordinary stock replan and one life-boundary censor;
neither is an observed utility recovery success.

The utility policy does choose `retreat` frequently while a bot is already in
a hazard, but that is a strategic shadow choice, not a safe local steering
command. It supplies no target, path, probe, actuator, or same-life escape
outcome. Treating it as a recovery activation would conflate policy intent
with movement control and could preempt an active stock `MoveToward` or
`StrafeFacing` command.

No code, build, UCC invocation, or runtime change was made for this note.

## Attested stock baseline

Both runs are complete 16-bot, 7,200-tick stock configurations with listed
live corrective controls disabled. Their manifests and summaries carry the
same build identity:

`sha256:2E94166EA327BB28D4D3B7F1C93CDBCB15AC7AD5076E02E18EC7954DA39DED33`

| Adapter | Run | Map | Shadow contract |
| --- | --- | --- | --- |
| UT436 | `qa/runs/2026-07-26/ut436-deck16-reachspec-capability-stock-s104729-7200-r3-attested-parity/` | `DM-Deck16][` | 7,200 aligned shadow ticks; 16 ordered participants; `controls_live_bots=false` |
| Unreal Gold 226b | `qa/runs/2026-07-26/unreal226b-deathfan-reachspec-capability-stock-s104729-7200-r1-attested-parity/` | `DmDeathFan` | 7,200 aligned shadow ticks; 16 ordered participants; `controls_live_bots=false` |

Each shadow manifest contains only `tactical-state` v1 and `utility-arena` v1
and explicitly declares:

```json
{"items": false, "armor": false, "stuck_time": false}
```

The policies therefore cannot distinguish a real no-progress episode from an
ordinary combat/animation/callback wait. Their outputs are deterministic
observations beside stock scripts, not commands to the live pawns.

## Policy frequency in available shadow samples

Only samples where the participant is available and has a decision are counted.
This avoids treating a retained final decision after death as a new choice.

| Scope | UT decision samples | UT same / different action | Unreal decision samples | Unreal same / different action |
| --- | ---: | ---: | ---: | ---: |
| All available samples | 97,069 | 63,916 / 33,153 | 66,583 | 57,085 / 9,498 |
| Stock movement-intent samples | 56,243 | 39,774 / 16,469 | 50,088 | 43,358 / 6,730 |
| In-hazard samples | 2,325 | 1,319 / 1,006 | 9,332 | 6,637 / 2,695 |

| Scope | Tactical-state actions, UT | Utility-arena actions, UT | Tactical-state actions, Unreal | Utility-arena actions, Unreal |
| --- | --- | --- | --- | --- |
| Movement intent | attack 45,729; hunt 6,063; retreat 3,894; idle 557 | attack 36,479; retreat 12,568; explore 7,196 | attack 44,591; retreat 3,540; hunt 1,957 | attack 40,057; retreat 7,975; explore 1,996; hunt 60 |
| In hazard | attack 1,370; retreat 498; hunt 370; idle 87 | retreat 1,162; attack 821; explore 342 | attack 6,591; retreat 2,350; hunt 391 | retreat 4,790; attack 4,287; explore 255 |

Neither policy emitted `recover-from-stuck` in any of these samples.

## Exact watchdog-stall join

The event stream supplies a stronger predicate than the shadow snapshot:
`move_stall_recovery_decisions` records a live movement-intent watchdog
crossing after at least 2.0 seconds of no progress. Records were joined to the
same-tick shadow decision by roster identity, then to the later same-life
watchdog outcome by `(source pawn actor, life_id, episode_id)`.

| Measure | UT Deck16 | Unreal DeathFan |
| --- | ---: | ---: |
| Exact watchdog detections | 10 | 9 |
| Latent command at detection | `MoveToward`: 6; `StrafeFacing`: 4 | `StrafeFacing`: 9 |
| Detection while in hazard | 0 | 0 |
| Tactical `recover-from-stuck` | 0 | 0 |
| Utility `recover-from-stuck` | 0 | 0 |
| Stock outcome: excluded intentional stop | 8 | 9 |
| Stock outcome: replanned within five seconds | 1 | 0 |
| Stock outcome: life-boundary censored | 1 | 0 |
| Stock outcome: cleared within two seconds | 0 | 0 |
| Stock outcome: cleared after two, within five seconds | 0 | 0 |

At the UT detections, tactical-state chose `attack-enemy` ten times;
utility-arena chose `attack-enemy` eight times and `retreat` twice. At the
Unreal detections, tactical-state chose `attack-enemy` eight times and
`hunt-enemy` once; utility-arena chose `attack-enemy` seven times, `retreat`
once, and `explore` once. All 19 native watchdog decisions were `none`.

The single UT replan was a non-hazard `MoveToward(PathNode1)` at tick 5,756
while both policies selected `attack-enemy`. The censored UT event was a
non-hazard `MoveToward(MedBox2)` at tick 7,049 while tactical-state selected
`attack-enemy` and utility-arena selected `retreat`. These observations cannot
choose a lateral recovery direction, prove a safe command, or quantify a
benefit.

The observed recovery activation frequency is exactly **0 / 19**. The observed
live-stock conflict count is also zero only vacuously: no shadow recovery
action exists to preempt a stock command. It is not evidence that a future
recovery adapter would be conflict-free.

## Hazard-proximity join

For this baseline, hazard proximity means the first available shadow sample
where the event stream changes from `in_hazard_zone=false` to `true` for a
participant. It is post-entry telemetry, not a prediction or a certified
escape opportunity.

| Measure | UT Deck16 | Unreal DeathFan |
| --- | ---: | ---: |
| Available in-hazard samples | 2,325 | 9,332 |
| Available hazard-entry samples | 16 | 84 |
| Tactical action: attack / hunt / retreat | 12 / 2 / 2 | 75 / 4 / 5 |
| Utility action: attack / retreat | 12 / 4 | 63 / 21 |
| Utility `recover-from-stuck` at entry | 0 | 0 |

Utility-arena's `retreat` appears on 4/16 UT and 21/84 Unreal entry samples,
and on 1,162/2,325 UT and 4,790/9,332 Unreal in-hazard samples. The current
stream lacks a proposed destination, safe-probe evidence, command admission,
or post-command progress. It cannot tell whether `retreat` would help,
duplicate stock movement, or worsen the hazard episode.

## Required baseline report before any adapter

Keep the stream read-only and add a versioned per-episode report only after it
exposes the missing movement inputs. The unit of analysis must be same-life
`(identity, life_id, episode_id)`, never an aggregate per-tick action count.

For every watchdog crossing and recovery admission, record:

```text
run build/config identity; policy id/version; identity/life/episode;
shadow action and transition; stock latent action, target, destination, and
movement intent; no-progress seconds/displacement/radius; physics and hazard
state; safe-probe eligibility/reason and selected probe; whether control was
actually applied; suspended stock/policy action; stock-command preemption;
same-life outcome (cleared <=2s, cleared 2..5s, replanned <=5s, intentional
stop, death/hazard, life-boundary, run-end, or unknown); resumed action/target;
and forbidden route/target/fire write counters.
```

For each adapter and policy version, report exact denominators and numerators
for: available decision samples/action distribution; movement-intent samples,
watchdog detections, and valid admissions; recovery selections, applied
commands, and stock-command preemptions; same-life outcome partitions; and
hazard entries, safe-escape admissions, exits, and deaths.

Only repeated, attested cross-game runs with nonzero valid admissions, zero
forbidden writes, and better same-life recovery outcomes than this stock
baseline can answer whether a utility recovery action activates often enough
and without conflicting with stock movement.
