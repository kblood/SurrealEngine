# Controlled bot benchmark driver

This topic depends on `pr/headless-benchmark-driver` and registers the first
concrete driver as `bot-benchmark`. It is selected explicitly with:

```text
--headless-driver=bot-benchmark
```

With no selection, registration is inert and interactive behavior is unchanged.
The driver consumes `DeterministicRuntime`, loads a caller-selected UT map with
automatic bots disabled, logs the viewport in as a spectator, spawns a bounded
ordered roster, and advances a fixed-step level loop without window, audio, or
renderer creation. The default remains one unnamed bot at the uniform external
difficulty, so existing invocations keep their gameplay setup.

The setup is verified for Unreal Tournament 436 and Unreal Gold 226b
deathmatch. A fail-closed game-profile check selects the game's independently
verified spectator, bot-spawn, roster, and skill contract; unverified engine
versions, modes, and mods report an actionable unsupported-profile failure. See
[`BOT_AI_CROSS_GAME_AND_MAPS.md`](BOT_AI_CROSS_GAME_AND_MAPS.md) for the shared
engine boundary and the separate game-profile contracts.

## Rendered unattended spectator matches

`--bot-spectator` is the visual counterpart to the headless benchmark. It
opens the normal window, audio, and selected renderer, but logs the viewport in
as a `Botpack.CHSpectator` instead of a player. The same shared controlled-match
setup suppresses automatic bots, creates the requested exact roster, verifies
each stock skill mapping, and leaves all gameplay control with those bots. The
camera automatically follows a living bot in third person and selects another
living bot when its current target dies. It requires no player input.

For example, this starts a four-bot exhibition match on Deck16][ with mixed
stock skills and exits after two minutes:

```powershell
SurrealEngine.exe --autoplay --bot-spectator `
  --bot-spectator-url="DM-Deck16][?Game=Botpack.DeathMatchPlus" `
  --bot-spectator-bots=4 --bot-spectator-skills=7,5,3,1 `
  --bot-spectator-seconds=120 "C:\Games\Unreal Tournament"
```

The equivalent two-bot Unreal Gold match is:

```powershell
SurrealEngine.exe --autoplay --bot-spectator `
  --bot-spectator-url="DmDeathFan?Game=UnrealShare.DeathMatchGame" `
  --bot-spectator-bots=2 --bot-spectator-skills=0,3 `
  --bot-spectator-seconds=120 "C:\Games\Unreal Gold"
```

`--bot-spectator-seconds=0` (the default) runs until the window is closed.
`--bot-spectator-url` defaults to DM-Morbias][, the roster defaults to four
bots, and an optional `--bot-spectator-skills=...` list must match the bot
count. UT436 accepts external skills 0 through 7; Unreal 226b accepts 0 through
3 and requires an explicit Unreal deathmatch URL. This is a live rendered
match, not a recorded Unreal demo and not a special demo edition of either
game. Both verified profiles run as unattended bot-only matches.

The spectator mode intentionally does not write benchmark telemetry or claim
determinism. Use it for visual inspection, debugging, exhibition matches, and
recording external video. Use `--headless-driver=bot-benchmark` for fast,
fixed-step, machine-comparable quality runs.

## PickTarget regression fixture

`--headless-driver=bot-pick-target-fixture` is a separate, no-UCC regression
fixture for the shared native `UPawn::PickTarget` path. It creates two
controlled deathmatch bots, relocates them to visible `PlayerStart` positions,
and passes only when the caller selects the other living bot with the native
routine. It leaves ordinary benchmark and gameplay runs unchanged.

Run it against each verified adapter with its native deathmatch map:

```powershell
SurrealEngine.exe --autoplay --headless-driver=bot-pick-target-fixture `
  --botbench-url="DM-Deck16][?Game=Botpack.DeathMatchPlus" `
  --botbench-output="C:\qa\ut-pick-target" "C:\Games\Unreal Tournament"

SurrealEngine.exe --autoplay --headless-driver=bot-pick-target-fixture `
  --botbench-url="DmDeathFan?Game=UnrealShare.DeathMatchGame" `
  --botbench-output="C:\qa\unreal-pick-target" "C:\Games\Unreal Gold"
```

The fixture writes `pick-target-fixture-result.txt`, including the two actors,
visible-candidate result, selected actor, and failure reason.

## WarnTarget / TryToDuck observer

`--botbench-warn-target-observer=1` is a default-off, read-only VM observer for
the stock `Pawn.WarnTarget(Pawn,float,vector)` and
`TryToDuck(vector,bool)` script paths. It requires
`--botbench-pick-target-observer=1`; requesting it alone fails before match
setup. At setup it discovers the two functions across each live controlled
bot's class and state ancestry, validates their exact retail signatures, and
otherwise emits a stable disabled-contract status with a reason.

When active, it records only participant receivers, with bounded per-bot
records, event type, declaring contract, receiver/state, warning shooter, and
an exact active-call nesting link from `TryToDuck` to `WarnTarget`. It never
replaces VM arguments or results and never writes movement, weapon, or target
state. A missing signature, argument mismatch, non-finite vector, lifecycle
mismatch, record overflow, or integrity failure makes analyzer evidence
incomplete. The link is local VM nesting only: it does not claim a causal
connection to a prior native `PickTarget` call or a later death.

Each retained `TryToDuck` call also has one bounded, post-call outcome record:
the requested finite duck vector/reverse flag plus resulting velocity, physics,
state, and latent action. It shares the exact nested WarnTarget sequence,
tick, life, invocation, and receiver identity. This observes the stock
`TryToDuck`→fall/velocity/dodge/state path without modifying it; missing,
overflowed, or non-finite outcomes make the evidence incomplete.

The PickTarget and warning records additionally carry observer tick, participant
actor index, life id, and a lazy VM-frame invocation token. These are a
bounded, default-off provenance envelope for offline investigation; they are
not a causal metric. In particular, the engine currently has no behavior-
neutral, exact handoff record proving that a target selection caused a later
movement command. Analysis must therefore treat a missing or ambiguous join as
unknown, never as a bot-caused death.

For example:

```powershell
SurrealEngine.exe --autoplay --headless-driver=bot-benchmark `
  --botbench-url="DM-Deck16][?Game=Botpack.DeathMatchPlus" `
  --botbench-output="C:\qa\ut-warn-target" --botbench-bots=16 `
  --botbench-pick-target-observer=1 --botbench-warn-target-observer=1 `
  "C:\Games\Unreal Tournament"
```

The immutable configuration is parsed once from `--botbench-url`,
`--botbench-output`, `--botbench-seed`, `--botbench-ticks`,
`--botbench-fixed-delta`, and `--botbench-difficulty`. The optional roster
arguments are `--botbench-bots=1..16`, `--botbench-skills=0..7,...`, and
`--botbench-names=name,...`. Skill and name lists must exactly match the bot
count. An absent skill list repeats the uniform difficulty; an absent name list
selects stock random profiles. Present empty lists, empty entries, invalid
skills, name-count mismatches, and case-insensitive duplicate names fail before
engine setup. There is no bot-policy selector in this configuration.

Each unnamed participant is spawned sequentially through the verified stock
`AddBots 1` command. The driver applies `InitializeSkill` independently, checks
the stock novice/internal-skill mapping, identifies exactly one newly created
bot, and binds its PRI identity, actor, player name, and class to that roster
index. It finally requires the complete live bot set to equal the controlled
set. This makes roster order a spawn contract rather than an accidental sort
order in telemetry.

Explicit names never fall back to random profiles. The only allowed named path
is stock `AddBotNamed`; the verified benchmark viewport is a
`Botpack.CHSpectator`, whose exec surface does not provide that
`TournamentPlayer` function. Current named runs therefore fail explicitly with
an unavailable-command reason. Supporting stable named profiles later requires
an independently verified command contract; this slice does not substitute a
native `ForceAddBot` path.

## Manifest and bounded telemetry

An opted-in run also creates two comparison inputs in the output directory:

- `manifest.json` is written once at driver start using
  `surreal-bot-benchmark-manifest-v2`. It records the parsed configuration, a
  deterministic FNV-1a configuration identity, and the telemetry event cap.
  The output directory is recorded for provenance but excluded from the
  configuration identity because it cannot affect simulation. Bot count and
  every ordered canonical participant fragment are included in that identity.
- `events.jsonl` uses `surreal-bot-benchmark-telemetry-v2`. It contains one
  `run_start` record, at most one `tick` record for each simulated tick, and
  one `run_result` record. The hard cap is therefore `max_ticks + 2`; no bot or
  script event can expand it.

Every record carries a decimal-string sequence and tick, fixed nine-decimal
simulated time, configuration identity, resolved map, status, and failure
reason. Each bot sample carries roster-stable identity and actor/player/class
names, state, position, velocity, health, persistent PRI score/deaths,
movement-intent and hazard-zone flags, and cumulative kills, deaths, UT-style
suicides, environmental deaths, hazard-exposed deaths, and `HitWall` calls.
The cumulative combat/collision counters come from outermost UE1 `Killed` and
`HitWall` script-call boundaries; they do not expand the bounded event stream.
Hazard-exposed death is deliberately a proximity proxy, not causal attribution.
Older games without a specific PRI or zone property emit its neutral sample.
Bots are sorted by identity and actor name before serialization. JSON
keys have a fixed order, strings are escaped explicitly, numbers use the
classic locale, position and velocity use six fixed decimals, negative zero is
normalized, and non-finite values fail the benchmark instead of entering the
trace.

`summary.json` uses `surreal-bot-benchmark-summary-v3`. It contains the exact
requested roster and the successfully created portion of the actual roster.
Actual participants are serialized by roster index, not PRI or actor sort
order. Failed setup can therefore retain partial spawn evidence. A successful
run has one actual participant for every requested index.

## Version 2 roster and version 3 timing contract

The manifest adds `bot_count` and `requested_roster` to the original
configuration fields:

```json
{
  "schema": "surreal-bot-benchmark-manifest-v2",
  "bot_count": 1,
  "requested_roster": [
    {
      "roster_index": 0,
      "requested_name": "",
      "external_skill": 7,
      "identity_fragment": "participant-v1:index=0;external_skill=7;requested_name_hex="
    }
  ]
}
```

The array always contains exactly `bot_count` entries in ascending index order.
`identity_fragment` is an unambiguous canonical string whose requested name is
encoded as lowercase hexadecimal bytes. It is intended for configuration
hashing, not as the runtime pawn identity. The existing top-level `difficulty`
remains the uniform fallback and is still identity-bound when explicit skills
are supplied.

The v3 summary retains the v2 lifecycle, game, and roster fields, then adds a
performance-only `ai_frame_timing` group:

```json
{
  "schema": "surreal-bot-benchmark-summary-v3",
  "requested_roster": [
    {
      "roster_index": 0,
      "requested_name": "",
      "external_skill": 7,
      "identity_fragment": "participant-v1:index=0;external_skill=7;requested_name_hex="
    }
  ],
  "actual_roster": [
    {
      "roster_index": 0,
      "identity": "pri:1",
      "actor": "Bot1",
      "player_name": "Loque",
      "class": "Botpack.Bot"
    }
  ],
  "ai_frame_timing": {
    "schema": "surreal-bot-ai-frame-timing-v1",
    "scope": "benchmark_observation_policy_driver_sampling",
    "clock": "host_steady_clock_performance_only",
    "behavioral_determinism": "not_behavioral_evidence",
    "sample_count": "600",
    "histogram_bucket_overflows_exact": "0",
    "bucket_max_microseconds": "10000",
    "p50_microseconds": 410,
    "p95_microseconds": 970,
    "p99_microseconds": 1200,
    "max_microseconds": "1520"
  },
  "config": {
    "url": "DM-Morbias][?Game=Botpack.DeathMatchPlus",
    "output_directory": "botbench-output",
    "seed": "104729",
    "max_ticks": "600",
    "fixed_delta": 0.016666668,
    "difficulty": 7,
    "bot_count": 1
  }
}
```

The example omits unchanged lifecycle fields. V2 removes the ambiguous
singular `bot_class` and `bot_name`; consumers must use `actual_roster`. Seed
and tick counts remain decimal strings. Fixed delta and simulated time use
fixed nine-decimal, classic-locale formatting. Telemetry remains v1 because its
existing sorted `bots` vector already supports multiple actors. Consumers must
use the v2-or-later summary when roster-slot identity matters. The v3 timing
scope deliberately excludes `Level->Tick`, renderer/audio/package work, JSON
serialization, telemetry file writes, UCC, and launcher wall time. It includes
only benchmark-owned navigation coverage observation, shadow observation
building/policy evaluation, and per-tick driver state sampling. Durations are
host performance observations, never deterministic behavior evidence. A fixed
10 ms, 1 us histogram bounds storage; a percentile whose rank falls in the
explicit overflow bucket is `null`, never clamped. `ai_frame_p95_ms` is only
available to the quality analyzer for a complete run with exactly one timing
sample per simulated tick.

Setup and tick failures are repeated in `run_start` or `run_result` when the
telemetry stream remains writable and always remain in `summary.json`. Failure
to serialize or finalize telemetry changes the run to a nonzero failure. The
protocol tests use synthetic configurations and bot snapshots; they do not
contain captured game data.

## Read-only policy shadow stream

Every successful controlled-roster setup also creates two separate shadow
artifacts. They do not change the v2 benchmark manifest, v1 movement telemetry,
or the stock Botpack controller:

- `shadow-manifest.json` uses
  `surreal-bot-benchmark-shadow-manifest-v1`. It binds the existing benchmark
  configuration identity to the ordered actual roster, sorted policy IDs and
  versions, a `max_ticks` record cap, and `controls_live_bots: false`.
- `shadow-decisions.jsonl` uses
  `surreal-bot-benchmark-shadow-event-v1`. It contains exactly one bounded
  record per simulated tick. Each roster slot states whether its pawn is live
  and contains the latest deterministic decision/counters from every policy.

Each controlled identity owns a separate policy evaluator that survives pawn
replacement and respawn. A live observation includes current health, current
weapon/ammunition usability, health lost since the previous observed tick, and
only other controlled bots that pass the engine's line-of-sight test. Hidden
enemy positions are not exposed. Policy memory is therefore built from earlier
subjective sightings instead of current omniscient actor state.

This first bridge deliberately reports no items, armor, or stuck-time signal;
those fields are named as false in the shadow manifest. It also does not issue
movement, aiming, firing, inventory, route, or state commands. The output can
show what `tactical-state` and `utility-arena` would choose beside stock
Botpack, but it cannot yet rank their real gameplay because they still do not
act.

## Evidence mapping

This is enough to prove repeatable setup, roster cardinality, requested stock
skill mapping, bounded lifecycle completion, exact run identity, and stable
read-only policy evaluation against live pawn observations. It does not yet
prove experimental-policy navigation, acquisition, combat, damage, death, or
skill parity.

The read-only evidence source was `surreal-bot-ai` commit `a65ae787`, especially
its `BotBenchmark` JSONL convention and `Tools/BotBenchmark` structural
validator. This topic deliberately maps only its generic protocol ideas:

| Evidence idea | Upstreamable implementation here |
| --- | --- |
| Versioned run configuration | Immutable `manifest.json` plus configuration identity |
| Ordered JSONL events | Fixed-schema records with sequence/tick ordering |
| Per-tick pawn observation | Bounded bot identity, transform, health, and state snapshot |
| Explicit setup/runtime failure | `run_start`/`run_result` status and existing summary exit |

The reference branch remains the source for later, separate topics: AI event
instrumentation, controlled reachability/HitWall/death fixtures, retail oracle
work, structural trace validation, matrix orchestration, and statistical
analysis. None of its fixtures, tools, captured data, or bot behavior changes
are included here. This telemetry does not prove navigation, acquisition,
combat, damage attribution, death causality, or skill parity; it only provides
a stable observation stream on which later comparison topics can build.

## Process bootstrap remains separate

`GameApp` still creates its display backend and launcher before constructing an
engine. A display-free CI entry path should be a second topic that detects the
registered headless mode before widget initialization and resolves the game
folder non-interactively. It is intentionally not folded into this driver.
