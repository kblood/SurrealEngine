# UT99 hazard command-transition ledger evidence — 2026-07-26

## Scope

This note records the first successful, observer-only command-transition ledger
capture for UT436 `DM-Deck16][`. The ledger is default-off and changes neither
bot scripts, navigation, physics, nor movement commands. It starts at a
harmful-residence entry, retains ordered same-life native `MoveTo` and
`MoveToward` replacements, and seals at a death, clearance, life boundary, or
run-end censor.

The retained artifact is:

`D:\SurrealEngineQA\2026-07-26\ut436-deck16-hazard-command-ledger-s104729-r1\runs\000000-hazard-command-ledger-dm-deck16-game-botpack-deathmatc-s104729-r0-825d37e062c4`

It is a 16-bot, skill-3, seed `104729`, 7,200-tick UT99 run with native path
commit, movement-command provenance, and the command-transition ledger enabled.
The corrected quality analyzer accepts its manifest and config identity; the
dedicated ledger analyzer accepts all records.

## Exact aggregate

The capture contains 16 terminal ledgers, all integrity-valid with zero record
overflow:

| Terminal disposition | Episodes |
| --- | ---: |
| PainTimer death | 15 |
| Run-end censored | 1 |
| Cleared or life-boundary censored | 0 |

There are 55 later native command replacements after harmful-residence entry.
Replacement counts are heterogeneous: three entries have zero replacements,
one has one, five have two, two have three, two have four, two have six, and
one has eighteen. Entry callers are likewise mixed: Roaming (10), Hunting (4),
Retreating (1), and Charging (1). Entry targets include PathNodes, Jumpboots,
UDamage, ShieldBelts, Enforcer, LiftExit, PlayerStart, and targetless MoveTo.

This establishes real same-life command supersession rather than a dropped
token, overflow, or telemetry clearing defect. It does not establish one common
bad bot decision.

## `pri:15` repetition boundary

`pri:15` is the only local-looking repeat, but it remains insufficient for a
behavior change:

- Life 1 dies at tick 297 after Roaming `MoveToward(PathNode122)` at tick 3,
  then replacements at tick 84 (same target) and tick 248 (`PlayerStart16`).
- Life 2 dies at tick 1103 after Roaming `MoveToward(PathNode122)` at tick 847,
  followed by six replacements through `MoveTo`, `PlayerStart16`, and targetless
  movement.
- Life 6 dies at tick 5728 after a targetless Roaming `MoveTo` at tick 5486;
  later replacements mix Hunting Enforcer pursuit, Roaming `PathNode149`, and
  targetless movement.

The first two histories share one bot and an early `PathNode122` to
`PlayerStart16` pattern, but do not recur across independent bots. More
importantly, the ledger begins after the system knows the pawn is in harmful
residence. It does not identify the preceding fall, exact slime zone, contact
or ledge boundary, or physical transition that produced entry.

## Decision

No route rewrite, target veto, timeout, water egress, or movement override is
authorized from this artifact. A candidate would be guessing at a correction
after entry, while the evidence shows heterogeneous same-life command churn.

The next read-only requirement is a pre-entry causal slice that joins the
following to each ledger entry and later terminal:

1. exact harmful zone actor/index and entry location;
2. pre-entry physics and support/falling/swimming transition;
3. applicable `MayFall` or `HitWall` boundary; and
4. the bounded command chain immediately before entry.

Only a repeated causal entry pattern with a certified safe alternative can
authorize a live bot-safety candidate.
