# Walking `HitWall` cluster evidence — 2026-07-26

## Scope and method

This is a read-only mining pass over the current stock anchors, both with 16
bots, seed `104729`, and 7,200 fixed ticks:

| Adapter | Fixture | Artifact |
| --- | --- | --- |
| UT436 | `DM-Deck16][` | `qa/runs/2026-07-26/ut436-deck16-instrumented-stock-s104729-7200-r1/` |
| Unreal Gold 226b | `DmDeathFan` | `qa/runs/2026-07-26/unreal226b-deathfan-instrumented-stock-s104729-7200-r1/` |

Every non-empty `walking_hitwall_dispatch_diagnostics` event was joined to its
same-tick bot snapshot (position, latent action, and `MoveTarget`) and same-
tick route participant (route head).  Position cells of 128 Unreal units were
used only to discover clusters; the result below retains the raw positions and
route identities.  No UCC invocation or source/behavior change is included.

## Aggregate dispatch context

| Adapter | Raw `HitWall` events | walking dispatch observations | callbacks | diagnostic overflow |
| --- | ---: | ---: | ---: | ---: |
| UT436 Deck16 | 10,562 | 3,902 | 768 | 0 |
| Unreal DeathFan | 3,517 | 245 | 245 | 0 |

The aggregate counts alone are not a loop signal.  In particular, Unreal's
245 observations are all callback-dispatched and disperse over static world,
mover, and dynamic-actor blockers and several latent actions.  Its largest
exact context is only six `primary_forward` mover contacts while `Continue`
retains `LiftExit6`; the next is four at `LiftExit2`, and every other exact
context is three or fewer.  It does not show a repeated shared wall-loop in
this trace.

## UT shared geometry/route signature

UT has a distinct, repeated signature rather than undifferentiated collision
noise:

- nine multi-second contact sequences across six bots (`pri:2`, `pri:3`,
  `pri:4`, `pri:6`, `pri:14`, and `pri:16`);
- each uses `MoveToward` with `MoveTarget` and route head `PathNode131`; the
  next route node is consistently `PathNode132`;
- each reports a `static_world`, `primary_forward` blocker with
  `callback_dispatched=false`; and
- first contact is near `(2007, -376/-389, -504)`, then the pawn remains at
  `(1999, -335, -504)` until the command ends.

The long sequences span 158 or 165 ticks each: `172–329`, `498–662`,
`1458–1615`, `2128–2285`, `2682–2846`, `3494–3651`, `3812–3969`,
`5189–5346`, and `5694–5851`.  At their terminal samples, the active
`MoveToward(PathNode131)` timer is one fixed tick from expiry (`0.018` s).
The following tick either transitions to a new command or target, or completes
the stock turn/roam flow.

Existing exact stall terminals classify these same nine sequences as five
`excluded_intentional_stop` outcomes and four
`replanned_within_5_seconds` outcomes.  None is a missed five-second deadline,
harmful-zone terminal, or death witness.  Thus the repeated geometry/route
mechanism is real in this fixture, but the current trace does not show it
causing a harmful or unrecovered bot failure.

## Decision

This is enough context for a **narrow, opt-in read-only observer**, not for a
movement correction.  Its predicate can require the same life, `MoveToward`,
route head `PathNode131`, a `static_world` primary-forward contact, repeated
low-displacement contact near the recorded coordinates, and a terminal join.
It must keep callback status and terminal outcome rather than treating raw
`HitWall` volume as failure.

No correction is authorized yet: this is one UT map/route signature, generic
`static_world` telemetry does not identify the exact level brush or actor, and
all observed terminals are either stock replans or excluded intentional stops.
Any future action needs blocker/geometry provenance plus a repeated adverse
terminal (missed deadline, certified hazard, or death) on this predicate.
