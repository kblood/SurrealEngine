# Bot policy architecture and shadow-experiment audit

Date: 2026-07-26

## Decision

The current enhanced policies can be evaluated safely only as deterministic
shadow policies. They do run on the live observations of both UT436 and
Unreal Gold controlled bots, but they do not run the bots. A policy-selection
or action-adapter experiment is not yet present, so neither policy has
gameplay, combat, or survival outcomes that can be compared with stock.

The most useful next experiment is an explicitly selected, shadow-only
tactical-survival variant: it should recommend Retreat rather than attack when
an observed visible threat coincides with low health or recent incoming damage.
It must not issue a command. This is the narrowest combat/survival question
supported by the current observation surface in both game families.

## What actually runs

The current 16-bot stock-parity anchors are:

| Family | Stock classes in the actual roster | Native policy status |
| --- | --- | --- |
| UT436 Deck16-II | Botpack.TFemale1Bot, TFemale2Bot, TMale1Bot, TMale2Bot | Botpack UnrealScript owns live control. |
| Unreal Gold 226b DeathFan | UnrealI.FemaleTwoBot, MaleOneBot, MaleTwoBot, SkaarjPlayerBot; UnrealShare.FemaleOneBot, MaleThreeBot | Unreal/UnrealShare UnrealScript owns live control. |

PolicyRegistry creates exactly two native experimental policies, sorted by ID:
tactical-state v1 and utility-arena v1. It explicitly returns
ScriptOwnedPolicy for stock-botpack. There is no native policy selected by the
benchmark configuration and no code path that replaces a stock script state.

After controlled roster setup, the benchmark creates one PolicyShadowEvaluator
per immutable roster slot, seeds it with seed + roster_index, and preserves it
through pawn replacement/respawn. On every tick where that slot has a live
pawn, the driver builds one sanitized observation and evaluates both policies.
The write occurs after the regular game tick; no policy result is passed back to
the pawn.

shadow-manifest.json in both current anchors records controls_live_bots: false,
exactly those two policies, all 16 roster slots, and a 7,200-record cap. There
is one shadow-decisions.jsonl record per simulated tick. This is the current
answer to “can it be evaluated without replacing stock scripts?”: yes for
deterministic recommendation comparison, not for real gameplay effectiveness.

## Policy action surface

Both policies return the same passive Decision shape:

| Action | tactical-state v1 | utility-arena v1 |
| --- | --- | --- |
| attack-enemy | Highest-priority visible, line-of-sight enemy except critical-resource state | Scored visible, line-of-sight enemy with usable weapon |
| hunt-enemy | Four-second subjective enemy memory | Scored recent confident enemy memory |
| retreat | Critical resources, or low resources without a visible enemy | Health/incoming-damage/firing-threat survival pressure |
| acquire-item | Safest useful reachable item while resupplying | Highest scored reachable item |
| explore / idle | Objective utility or no stimulus | Baseline scored exploration |
| investigate-sound | Recent uncertain observation | Defined in the common action enum but not selected by this implementation |
| recover-from-stuck | One-second stuck threshold with 0.25-second clear debounce | One-second stuck preemption, then hysteretic task-stack resumption |

The action surface is descriptive only. There is no adapter from any of those
actions to UnrealScript latent actions, destination/path requests, MoveTo,
MoveToward, view rotation, fire flags, weapon choice, inventory selection, or
pawn state.

## Current observation limits

The driver supplies per-tick identity, position, health fraction, health lost
since the previous observed tick, weapon/ammunition usability, and other
controlled live bots that pass LineOfSightTo. Hidden actors are not exposed;
the tactical policy's memory is consequently subjective and local.

The current shadow manifest truthfully declares these inputs absent:

- items and item reachability/route danger;
- armor; and
- stuck time.

Consequently acquire-item and recover-from-stuck cannot be meaningfully
exercised by the live benchmark bridge. There is no tactical-query route,
cover, escape, objective, aim, firing, damage-source, or hazard forecast
input. A route-choice, pickup, aim, or locomotion policy experiment would
therefore be artificial at this boundary.

## Current shadow evidence

The following counts were streamed from the attested 7,200-tick, 16-bot
artifacts. They are recommendation samples, not policy-attributed outcomes.

| Family | Live policy evaluations per policy | Tactical actions | Utility actions | Exact action-and-target agreement | Tactical / utility action transitions |
| --- | ---: | --- | --- | ---: | ---: |
| UT436 Deck16-II | 97,069 | attack 74,135; hunt 13,670; retreat 5,949; idle 3,315 | attack 58,983; retreat 20,237; explore 17,849 | 44,359 / 97,069 | 566 / 600 |
| Unreal Gold 226b DeathFan | 66,583 | attack 59,051; hunt 2,567; retreat 4,965 | attack 52,361; retreat 11,640; explore 2,522; hunt 60 | 33,984 / 66,583 | 464 / 599 |

The disagreement is material and mostly survival/exploration preference, so
there is already a useful shadow comparison. It does not say which choice was
better because stock Botpack, rather than either recommendation, produced the
match trajectory.

The benchmark writes policy ID/version, evaluation count, action-transition
count, latest observation tick, selected action, target, score, and reason.
It does not serialize the alternatives vector or the full input observation.
The regular event stream independently records stock kills, deaths, suicides,
hazard/death attribution, damage, pickups, movement, and navigation counters;
none is joined to a policy decision or eligible as a policy score today.

The host-performance-only shadow_observation_and_policy timing component is
also present: UT p50/p95 is 1,325/2,134 microseconds and Unreal p50/p95 is
611/1,033 microseconds (7,200 samples each, zero histogram overflow). Timing
is capacity evidence, not behavioral determinism evidence.

## Is an opt-in variant currently possible?

Not as a selectable benchmark option. BotBenchmarkRunConfig has no policy set,
policy ID, policy version, roster-to-policy assignment, or live-control field.
OpenShadowTelemetry unconditionally enumerates every registry policy for every
roster slot. Adding a policy to the registry would change the shadow manifest
and every shadow stream for all benchmark runs, even though it would remain
behavior-neutral.

An opt-in shadow variant is feasible without replacing stock scripts if it
first adds a default-preserving, config-identity-bound shadow_policy_set input.
The default must keep the current two IDs in sorted order; a requested variant
must be validated against PolicyRegistry, written into manifest and summary
identity, and rejected by the comparer if it differs. It must remain separate
from a future per-participant live action binding.

An opt-in live variant is not feasible yet. It remains blocked on the
per-participant immutable action adapter, balanced role-swap binding, and
policy-attributed combat/resource/survival metrics required by
BOT_AI_CONTINUATION_PLAN_OPUS5.md workstream 7.

## Recommended first experiment: tactical survival preference

Add tactical-survival as a third, shadow-only policy behind the explicit
policy-set input. Its scope should be only this deterministic precedence rule:

    when a visible, line-of-sight enemy is firing at the bot and
    (health < 0.45 or recent incoming damage > 0.15), recommend Retreat(target)
    instead of AttackEnemy(target); otherwise preserve tactical-state v1 behavior.

This is promising because the required inputs are already populated in both
games, and the existing utility policy independently recommends retreat much
more often than tactical-state (UT: 20,237 versus 5,949 samples; Unreal:
11,640 versus 4,965). It should not invent a retreat destination, path, or
fire suppression. Those need later tactical-query and action-adapter evidence.

### Deterministic fixture

Create a pure policy fixture with a stable sequence of synthetic observations:

1. armed, healthy, visible threat: AttackEnemy;
2. same visible firing threat, health 0.44: Retreat;
3. same threat, health 0.80, incoming damage 0.16: Retreat;
4. no survival pressure: exact tactical-state v1 decision; and
5. shuffled enemy input and reset/replay: identical decision, target, score,
   reason, and transition count.

The fixture must also assert that Tick() has no engine, pawn, pathfinding,
random, or command dependency. Existing tactical, utility, registry, shadow,
and serializer tests provide the appropriate pure-test pattern.

### Cross-game shadow gate

For each current anchor, run two fixed-seed repetitions with the default policy
set and two with the opt-in set. Require:

1. byte/semantic equivalence of each repetition's complete stock events.jsonl,
   route-execution.jsonl, manifest/summary apart from the declared policy-set
   identity, and the expected shadow policy list;
2. exact repeated shadow-decisions.jsonl for each policy set;
3. per-policy accounting: one evaluation per live participant tick, monotonic
   transition counters, ordered slot/policy records, and no serialization or
   timing-histogram overflow;
4. decision metrics: retreat/attack counts, transitions, disagreement with
   tactical-state v1, and windows where a survival recommendation precedes
   observed health loss, hazard entry, or death; and
5. explicit labeling of the last window metric as correlation only. Stock
   outcomes must remain unchanged because no policy controls a bot.

Do not claim a quality improvement, adjust a skill level, or enable policy
control from this experiment. A live experiment becomes eligible only after a
slot-bound adapter can prove command ownership and the full cross-game outcome
metric set can attribute the resulting combat and survival effects.
