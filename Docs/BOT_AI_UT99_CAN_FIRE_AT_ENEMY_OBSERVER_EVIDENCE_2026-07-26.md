# UT99 `CanFireAtEnemy` shadow-trace evidence

## Scope

`4503b6d7` adds a default-off, benchmark-participant-only observer for the
retail `Botpack.Bot.CanFireAtEnemy` script call.  It records the exact native
`Actor.Trace` inputs and result, then performs a read-only shadow trace with
the projectile-only collision restriction removed.  The observer never
changes the script return value or any bot decision.

The purpose was to test a narrow combat-safety hypothesis: stock
`CanFireAtEnemy` may authorize a shot when a regular actor trace finds a
nearer non-enemy blocker.

## Attested UT99 run

The following unattended, bot-only match completed with structural analysis:

- Artifact:
  `D:\SurrealEngineQA\2026-07-26\ut436-deck16-canfire-observer-s104729-r1\runs\000000-canfire-observer-dm-deck16-game-botpack-deathmatc-s104729-r0-192080e2fdb2`
- Engine: fresh `D:\SurrealEngineBuild\bot-canfire-observer\Release\SurrealEngine.exe`
- Map: `DM-Deck16][?Game=Botpack.DeathMatchPlus`
- Seed: `104729`; 16 stock Botpack bots; external skill 3; 7,200 fixed
  ticks (120 simulated seconds).
- Matrix and quality analysis: passed.  The exact initial-layout fingerprint
  was `sha256:3082c76e18e6122d8b13b4c65b1568938f3f12d300d6e19c576c592352bf76a4`.

## Result

| Observation | Count |
| --- | ---: |
| Exact `CanFireAtEnemy` calls | 231 |
| Observer overflows | 0 |
| Observer integrity failures | 0 |
| Stock return `true` | 125 |
| Shadow non-enemy blocker indications | 106 |
| Stock `true` *and* shadow blocker | 0 |
| Actual/shadow hit-presence disagreement | 0 |
| Actual/shadow hit-class disagreement | 1 |

The sole hit-class discrepancy was `TFemale2Carcass -> enforcer`; it was not
a stock-authorized shot.  The 106 shadow blocker observations were likewise
all calls where stock returned `false`.  Therefore this run supplies no
authorized shot that a simple “regular trace blocker” veto could safely
suppress.

## Decision

Do **not** add a `CanFireAtEnemy` behavior override or fire veto from this
evidence.  A generic collision-flag change would alter retail trace semantics
without a demonstrated unsafe authorization and could harm valid projectile
or hitscan combat.

Keep the observer available for future UT99 maps/seeds.  Reopen this candidate
only if an observer-attested record has all of the following:

1. Stock `CanFireAtEnemy` returned `true`.
2. The shadow trace has a finite, earlier non-enemy blocker.
3. The event can be joined to an attempted fire or damage outcome.
4. Repeated fixed-seed evidence shows that suppressing that exact class of
   authorization improves safety or combat quality without a map regression.
