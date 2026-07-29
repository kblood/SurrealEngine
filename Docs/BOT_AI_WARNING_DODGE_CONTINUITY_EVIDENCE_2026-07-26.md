# Warning-dodge continuity evidence — 2026-07-26

## Scope

This note records the completed, read-only continuity capture for the stock
Unreal Gold 226b warning-dodge path:

`qa/runs/2026-07-26/unreal226b-deathfan-warning-dodge-continuity-stockmode-s104729-7200-r1/`

The fixture is `DmDeathFan` with 16 bots, seed `104729`, and 7,200 fixed
ticks.  It adds no Botpack/UnrealShare or engine behavior change, and no UCC
invocation was used.

## Exact result

The run completed and `quality.json` reports an analyzer pass with zero errors
and zero warnings.  The continuity observer records 419 `WarnTarget`
observations, three nested `TryToDuck` observations, and two qualified
warning-dodge launches.  All observer and launch overflow counters are zero.

Each qualified launch received a terminal record exactly two ticks later:

| Launches | Continuity terminal | Delay | Harmful-water exit | Death |
| ---: | --- | ---: | ---: | ---: |
| 2 | `unknown` / `command_transition` | 2 ticks | 0 | 0 |

The terminal label means the tracked launch ceased to own the subsequent
command before a safe/harmful physical outcome could be certified.  It is not
evidence of a harmless landing, nor of a fall, water entry, or death.

## Decision

This closes the **lift-dodge safety lead for this trace**: both launches have
an exact, non-overflowed continuity disposition, and neither continues to an
uncategorized harmful-water exit or death.  It does not make a general
cross-map, cross-adapter, or causal safety claim.  A future lead requires a
new trace with a terminal that remains causal through its later command
transitions (or explicitly proves that no such transition occurred).

The appropriate action remains no behavior change.  These records are
diagnostic reachability and bounded-continuity evidence only.
