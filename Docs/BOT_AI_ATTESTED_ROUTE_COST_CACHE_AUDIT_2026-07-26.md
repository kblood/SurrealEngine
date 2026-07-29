# Attested route cost and cache audit

Date: 2026-07-26
Scope: read-only analysis of the current 16-bot, 7,200-tick stock-parity
captures. This audit does not authorize a behavior change.

## Inputs and integrity boundary

The audited runs are:

| Family | Run |
| --- | --- |
| UT436 Deck16-II | `qa/runs/2026-07-26/ut436-deck16-reachspec-capability-stock-s104729-7200-r3-attested-parity` |
| Unreal Gold 226b DeathFan | `qa/runs/2026-07-26/unreal226b-deathfan-reachspec-capability-stock-s104729-7200-r1-attested-parity` |

Both manifests declare the native path-commit and reachspec-capability
observers active, and the direct-reach command observer inactive. Each has
7,200 contiguous route records and 16 roster participants (115,200 sampled
participant-ticks). `Analyze-NativePathCommits.py` and
`Analyze-RouteExecutionContext.py` completed against the matching immutable
catalogs. The latter reports an observed post-tick cache edge; it is not a
path-selection oracle.

## Results

| Signal | UT436 Deck16-II | Unreal Gold DeathFan | Interpretation |
| --- | ---: | ---: | --- |
| Native commit records | 6,137 | 481 | Normal stock path activity is present in both captures. |
| Explicit cache clears | 1,329 | 146 | Explicit empty outcomes, not silent corruption. |
| Non-empty committed paths | 4,808 | 335 | Catalog validation accepted the retained path evidence. |
| Raw versus adjusted endpoint cost differences | 0 | 0 | No failed-navigation cost adjustment occurred. |
| Failed-navigation penalty applications | 0 | 0 | No cost-penalty candidate is exercised. |
| Committed node sequences with a repeated node | 0 | 0 | No native path-loop candidate. |
| Truncated cache commits | 4 | 0 | The four UT records are explicitly marked bounded prefixes. |
| Active graph-edge changes | 266 | 159 | Ordinary route progress/selection, not repeated churn. |
| Immediate `A -> B -> A` active-edge returns within 60 ticks | 1 | 2 | Isolated returns, with 21--59 ticks between the first and final observation; no recurring oscillation. |
| Native move-stall detections | 10 | 9 | None had a native commit or first-hop change in the preceding 60 ticks. |

The observed active graph screen resolved 24,179 UT and 7,577 Unreal first-hop
samples. It had no first hop on 15,435 and 5,287 active `MoveToward` samples
respectively. That distinction is important: a retained cache or target name
alone does not establish that a graph edge owned the current movement command.

## Hazard and terminal correlation

| Signal | UT436 Deck16-II | Unreal Gold DeathFan |
| --- | ---: | ---: |
| Death-counter increments | 49 | 113 |
| In-hazard-zone death increments | 15 | 75 |
| Deaths with an active graph first hop at the death tick | 0 | 0 |
| Deaths whose last active graph first hop was within 12 ticks | 6 | 1 |
| Deaths whose last active graph first hop was more than 60 ticks earlier | 38 | 80 |
| Hazard entries | 20 | 94 |
| Hazard entries with a native commit in the preceding 60 ticks | 5 | 16 |
| Hazard entries with a first-hop change in the preceding 60 ticks | 1 | 3 |

All terminal ticks are inactive for the active-graph screen. A nearby old
commit/cache sample therefore cannot be assigned as the command that caused a
death. The direct-reach terminal observer is deliberately disabled in these
stock-parity captures, so the data also cannot classify a terminal movement as
an exact same-life direct command. This is a measurement limit, not evidence
for a graph-policy change.

## The four UT bounded-prefix records

All four are `FindPathToward` commits with identical raw and adjusted costs and
16 retained nodes:

- Tick 1282, `pri:2`, `PathNode144` first target: safe `MoveToward` in
  `LevelInfo0`.
- Tick 2120, `pri:8`, `PathNode145` first target: safe `MoveToward` in
  `LevelInfo0`.
- Tick 4992, `pri:12`, `LiftExit6` first target: safe in `LevelInfo0`; the
  same bounded route shape recurs later.
- Tick 6474, `pri:12`, `LiftExit6` first target: the bot was already in
  `SlimeZone0`, in `Sleep`, at 36 health after earlier `StrafeTo` and
  `WaitForLanding` frames. It dies eight ticks later while the route sample is
  unavailable.

The final record is temporally close to a death but is post-entry and
non-active; it cannot support a cache-capacity, route-clear, or reachspec-veto
change. The same route was harmless at tick 4992, and no committed sequence
loops.

## Decision

No path-cost, failed-navigation penalty, route-cache, route-loop, or
reachspec-selection candidate is supported by the current attested data.
Reject cache clearing, cache expansion, route pinning, route-edge vetoes, and
cost/penalty policy changes on this evidence.

If a future run seeks to distinguish direct from graph selection at a hazard
entry, it must enable the existing read-only direct-reach command observer and
retain a same-life command-to-terminal disposition. It must still pass the
observer-on/off stock-equivalence and zero-overflow gates described in
`BOT_AI_ROUTE_EXECUTION_FINDINGS.md` and `BOT_AI_QUALITY_EXECUTION_PLAN.md`.
