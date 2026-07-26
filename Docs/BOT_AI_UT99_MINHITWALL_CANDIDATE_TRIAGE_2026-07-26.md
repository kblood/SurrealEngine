# UT99 MinHitWall candidate triage — 2026-07-26

## Scope

This note records the outcome of the benchmark-only walking `HitWall`
`MinHitWall` candidate. The candidate is default-off and changes dispatch only
when `--botbench-walking-hitwall-minhitwall-candidate=1` is supplied.

The matched controlled matrix used the same Release executable, UT436 retail
data, 16 skill-3 bots, `DM-Deck16][?Game=Botpack.DeathMatchPlus`, seed
`104729`, 7,200 fixed ticks, and every other candidate switch disabled.

Run artifact:

`C:\Devstuff\QuestGames\SurrealEngine\qa\runs\2026-07-26\ut436-deck16-walking-hitwall-minhitwall-candidate-s104729-r2`

## Integrity result

The matrix and fail-closed quality analysis both passed. The first capture was
intentionally discarded because the selected-dispatch diagnostics did not
reconcile with the per-bot counter. Commit `647af923` corrected the missing
driver epoch accumulation and added a regression test; the retained `r2`
capture reconciles exactly.

The enabled candidate reported 2,270 exact selected `MinHitWall` dispatches
with no activation-overflow evidence. It therefore exercised the intended
behavior; this is not an inert or telemetry-only comparison.

## Paired result

| Signal | Stock | `MinHitWall` candidate | Candidate minus stock |
| --- | ---: | ---: | ---: |
| Kills | 35 | 36 | +1 |
| Deaths | 49 | 53 | +4 |
| Suicides | 14 | 17 | +3 |
| Unassisted environmental deaths | 11 | 9 | -2 |
| Damage dealt to participants | 5,175 | 5,560 | +385 |
| Raw `HitWall` events | 5,629 | 6,434 | +805 |
| Movement-intent stuck episodes | 24 | 32 | +8 |
| No-progress proxy seconds | 708.817 | 685.067 | -23.750 |

The candidate improved a few isolated signals, including two fewer unassisted
environmental deaths, but it materially regressed total deaths, suicides,
wall-contact volume, and stuck episodes. The isolated improvement is not a
license to trade overall survival and movement robustness for a narrower
terminal count.

## Decision

Reject the candidate as a UT99 bot-quality improvement. Keep the switch
default-off as a diagnostic parity experiment; do not enable it for normal
games, include it in a merge candidate, or spend additional quality matrix
budget repeating this adverse primary result.

The next survival path is command-provenance capture at `MoveTo` and
`MoveToward` issuance, carried through a same-life fall/hazard terminal. That
observer is necessary to identify an attributable action before proposing any
replacement movement or water-escape behavior.
