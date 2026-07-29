# Stock provenance candidate discovery — 2026-07-26

## Scope

This is a read-only mining pass over the current stock-instrumented anchors:

- UT436 `DM-Deck16][`, 16 bots, difficulty 3, seed `104729`, 7,200 fixed
  ticks: `qa/runs/2026-07-26/ut436-deck16-instrumented-stock-s104729-7200-r1/`.
- Unreal Gold 226b `DmDeathFan`, the same roster/difficulty/seed/tick budget:
  `qa/runs/2026-07-26/unreal226b-deathfan-instrumented-stock-s104729-7200-r1/`.

The runs use the current observer and analyzer stack with only the native
`PickTarget` predicate restored to stock.  This note introduces no behavior,
navigation, or telemetry change.

## Exact water result

`Analyze-HazardResidence.py` verifies 17 UT and 53 Unreal `PainTimer` deaths
while a harmful residence was active.  Neither adapter has an exact
command-owned terminal (`0/17` and `0/53`).  The direct-reach stream also has
zero hazardous-death terminals on both adapters.  Consequently, a harmful
zone, a candidate location, or a generic environmental death must not be
treated as a command-caused water failure.

The missing water predicate is unchanged: at the `PainTimer` terminal, the
same life must retain an active native direct-reach command whose target is
exactly the pawn's live `MoveTarget`.  The existing witnesses have
`hazard_residence_command_ownership_exact=false`, so unassisted status cannot
make any of them actionable.

## Same-life target-owned stall result

Bounded stall decision and terminal records were joined by participant, life,
and episode ID.  No relevant record overflow occurred.

| Adapter | Stall episodes | Same-life known/live target decisions | Result |
| --- | ---: | ---: | --- |
| UT436 Deck16 | 13 | 11 | 10 `MoveToward`, 1 `StrafeFacing`; all decisions were `none`. Six ended as intentional stops and five were replanned by stock within five seconds. No missed-deadline target-owned episode. |
| Unreal DeathFan | 4 | 1 | One `StrafeFacing` episode: `pri:11`, life 8, episode 1, live `LiftExit2` (`LiftExit`), detected at 2.016668081 seconds of no progress, decision `none`, terminal `missed_5_second_deadline`. |

The Unreal case is an exact stalled command witness, but it is a single,
non-repeated `StrafeFacing` episode.  It has no direct-reach hazardous terminal,
harmful-residence terminal, or death witness.  It therefore cannot authorize a
movement write, timeout, or reroute policy.

## Candidate decision

No actionable exact predicate is present in these anchors.  The next candidate
discovery capture must require all of the following before a policy is even
considered:

1. Same life, known and live target at stall detection, with the command kind
   and target identity retained through terminal.
2. A non-excluded terminal: missed deadline or unassisted hazardous/PainTimer
   death, rather than an intentional stop, command replacement, life boundary,
   or generic aggregate death.
3. A certified safe alternate or exact stock replan witness, so a proposed
   intervention has a bounded target rather than a geometric guess.
4. Repetition on the same fixture and qualification on the other adapter or a
   documented adapter-specific boundary.

Until that evidence exists, retain all timeout and water-egress behavior
default-off.
