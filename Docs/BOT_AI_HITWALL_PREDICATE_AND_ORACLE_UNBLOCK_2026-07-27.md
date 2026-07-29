# HitWall predicate result and retail oracle unblock — 2026-07-27

## 1. The retail oracle compile fault was a path length overflow

`Iteration 104` records the quarter-step batches as blocked because "both
disposable retail compilers independently faulted during UCC driver-cache
startup". The location was right and the cause was not: retail `UCC.exe`
overflows a fixed path buffer and faults before compiling anything once its own
executable path grows long. It is not intermittent and no retry can clear it.

Measured on this machine against the GOG UT436 install, with a single copied
`System` tree renamed to vary only the path length:

- at or below 130 characters to `UCC.exe`: 5/5 clean startup
- 131 to 150 characters: nondeterministic, mixing caught general protection
  faults with raw `0xC0000005`
- 155 characters and above: 5/5 raw `0xC0000005` with empty stdout and stderr

The previously failing runs sat at 162 characters because the disposable runtime
was created under the evidence directory, which is nested far deeper than that.
A 2x2 over {short, long} x {pristine INIs, harness-modified INIs} showed the
INI edits are irrelevant: pristine INIs at a long path reproduce the exact
reported signature, and harness-modified INIs at a short path compile cleanly to
`Success - 0 error(s), 0 warnings`.

`Run-RetailMinHitWallOracle.ps1` now creates `_runtime` under a short root
(`-RuntimeRoot`, default `%TEMP%\sreo\<id>`) instead of under `-OutputRoot`, and
fails closed above a 120 character ceiling for the runtime `UCC.exe` path. Each
run writes `runtime-location.json` recording the runtime path and its length.
Evidence still lands in the output directory. `-KeepRuntime` now retains the
runtime at its short path rather than beside the evidence.

## 2. Quarter-step brackets

Both batches ran with byte-identical retail inventories before and after, two
agreeing repetitions per threshold.

- UT436 `DM-Deck16][`: `[-395000, -394500]` narrowed to `[-394750, -394500]`
  (`qa/runs/2026-07-27/retail-minhitwall-ut436-quarterstep-v2/`)
- Unreal Gold 226b `DmMorbias`: `[-396500, -396000]` narrowed to
  `[-396250, -396000]`
  (`qa/runs/2026-07-27/retail-minhitwall-unreal226b-quarterstep-v1/`)

These brackets refute the reconstruction rather than refine it. Dispatch flips
exactly where the threshold crosses the native operand, so UT436's operand lies
in `(-0.394750, -0.394500)` and Unreal Gold's in `(-0.396250, -0.396000)`. The
reconstructed Bump/trace dot is `-0.397676` for both. If that were the operand,
thresholds of `-0.395000` and `-0.394750` would both have dispatched; they
suppressed, in every repetition. Further bisection measures the operand
correctly while the reconstruction of its inputs stays wrong by about `0.003`,
so it cannot close the Iteration 104 objection and should not continue for that
purpose.

## 3. The walking HitWall predicate is not a bot-quality lever

`SelectWalkingHitWallDispatch` selects exactly one of two observable
predicates: the legacy vertical-wall Z-band, or the `MinHitWall` approach-angle
test. `Engine/Pawn.uc:86` documents `MinHitWall` as the "minimum HitNormal dot
Velocity.Normal to get a HitWall from the physics", and
`BOT_AI_DECK16_MOVEMENT_RESEARCH.md:773` already concluded it is an eligibility
condition rather than a wall classification rule. Both readings imply the two
predicates are conjunctive in retail, not alternative. No UE1 native source is
publicly available to confirm the `physWalking` control flow, so this remains an
inference.

A conjunctive candidate was therefore added, default-off behind
`SURREAL_WALKING_HITWALL_CONJUNCTION_CANDIDATE`
(`SurrealEngine/UObject/PawnWalkingHitWallConjunctionCandidate.h`), with unit
coverage in `Tests/PawnWalkingHitWallDispatchTests.cpp` for the three
discriminating geometries.

Paired runs, 7,200 fixed ticks, 16 skill-3 bots,
`DM-Deck16][?Game=Botpack.DeathMatchPlus`, every other switch disabled
(`qa/runs/2026-07-27/walking-hitwall-conjunction-candidate-ab/` and
`qa/runs/2026-07-27/walking-hitwall-conjunction-seeds/`). The stock arm
reproduced the Iteration 104 baseline exactly on all seven published signals,
confirming the uncommitted mover-collision work and this candidate are both
inert while disabled.

| Signal | s104729 | s271828 | s314159 |
| --- | ---: | ---: | ---: |
| Kills | -3 | -4 | -4 |
| Deaths | -1 | -11 | -12 |
| Suicides | +2 | -7 | -8 |
| Environmental deaths | +1 | -6 | -8 |
| Damage dealt | -271 | -450 | -952 |
| Movement-intent stuck episodes | +17 | +41 | +20 |
| No-progress proxy seconds | -21.7 | +233.1 | +176.1 |

Survival improves materially at two of three seeds while combat engagement and
movement robustness regress at all three. That is the same trade the
`MinHitWall`-only candidate was rejected for, so this candidate is rejected on
the same bar. The switch stays default-off as a diagnostic.

The mechanism matters more than the verdict. Raw `HitWall` volume rose under the
conjunction at seed `271828` (2,464 to 4,157) even though the conjunction is a
strict subset of the stock predicate and can only fire less often per contact.
Total volume can only rise if the bots generate more contacts, which is what the
stuck-episode increase reports. Withholding glancing notifications removes the
script's cue to adjust and the bots grind against geometry instead. `HitWall`
over-dispatch is therefore not a defect to be tuned away: it is load-bearing for
unsticking, and both directions of changing this predicate degrade bot quality.

## 3a. Read `deaths` as a derived signal

Across all eight captures in this note, `deaths_exact` equals
`kills_exact + suicides_exact` exactly. It is therefore not an independent
survival signal in a 16-bot free-for-all: deaths rise whenever the bots fight
each other more successfully. The independent signals are `kills_exact` for
competence, `suicides_exact` for self-harm, and
`environmental_deaths_exact`/`hazard_exposed_deaths_proxy` for hazard
avoidance. Read that way, roughly half of the conjunction candidate's apparent
survival gain in section 3 is bots killing each other less rather than bots
staying alive better.

## 3b. The corrected `CanSee` cone improves competence

Same matrix and the same three stock controls, varying only
`--botbench-pawn-vision-cone=1`
(`qa/runs/2026-07-27/pawn-vision-cone-ab/`). All three candidate manifests
record `pawn_vision_cone_enabled` true with a distinct config identity.

| Signal | s104729 | s271828 | s314159 |
| --- | ---: | ---: | ---: |
| Kills | +12 | 0 | +4 |
| Damage dealt | +1218 | -81 | +369 |
| Suicides | -4 | 0 | -7 |
| Environmental deaths | -5 | 0 | -8 |
| Hazard-exposed deaths | -4 | -1 | -6 |
| Movement-intent stuck episodes | +2 | +5 | +8 |
| No-progress proxy seconds | -154.6 | -84.5 | +2.4 |

Kills never regress and rise sharply at one seed, suicides and environmental
deaths never rise, and hazard-exposed deaths fall at every seed. This is the
first candidate in this effort to improve competence rather than trade it away.
The counterweight is a small movement-intent stuck increase at all three seeds,
about a quarter the magnitude of the conjunction candidate's, against
no-progress seconds that improve at two of three seeds.

Two qualifications. `Pawn.CanSee`'s exercised UT callers are `Killed` and
`Hunting` only (`BOT_AI_UT99_CANSEE_WITNESS_PROVENANCE_2026-07-26.md`), so this
governs hunting and post-kill reaction rather than primary target acquisition.
And the corrected path still passes `Location()` rather than the
`BaseEyeHeight()` view spot as the observer position
(`UActor.cpp:4982-4983`), so a residual vertical inaccuracy remains even with
the flag enabled.

Unlike the predicate work in section 3, the stock cone is a defect rather than a
tuning choice: it compares the facing axis against `normalize(origin)`, an
absolute world position, so its result depends on where the map's origin
happens to sit. Both open questions are now closed: section 3c explains the
stuck signal and section 3d confirms this result against a same-binary control.

## 3c. The stuck-episode counterweight is mostly a metric defect

The movement-intent stuck increase in section 3b was investigated against the
same six captures. It is not caused by the corrected cone.

`movement_intent_stuck_events_proxy` is not an engine event. It is
reconstructed in `Analyze-BotQuality.py:6207-6220` from per-tick telemetry: a
step counts as stalled when `movement_intent` is true and displacement is under
`0.25`, an episode latches once such a streak reaches `2.0` seconds, and the
`else` branch resets both the accumulator and the dedup latch. That reset fires
on an intent drop as well as on real movement, so a bot that stays physically
wedged while its state machine blips through `Attacking` and back mints a fresh
counted episode out of one continuous stall.

Recounting with the reset restricted to real displacement, death, or sample
discontinuity, and with the accumulated quantity left unchanged, isolates that
effect:

| Seed | Published stock/cone | De-fragmented stock/cone |
| --- | ---: | ---: |
| 104729 | 24 / 26 (+2) | 20 / 23 (+3) |
| 271828 | 17 / 22 (+5) | 18 / 23 (+5) |
| 314159 | 29 / 37 (+8) | 26 / 27 (+1) |

The seed `314159` outlier is almost entirely fragmentation: that arm carries ten
fragment episodes against stock's three, and its `+8` collapses to `+1`. The
other two seeds barely move. Note the de-fragmented count is not a strict
undercount of the published one: carrying the accumulator across an intent blip
also lets streaks reach the two second threshold that the published metric
resets just short of, which is why seed `271828` counts higher under it.

Two candidate mechanisms were tested against the event stream and refuted.
Longer survival does not explain it: total alive time fell at every seed
(`-82.6`, `-25.9`, `-6.2` seconds), so the per-alive-second rate rose more than
the raw count, not less. Longer hunts do not explain it either: `Pawn.CanSee`
is reachable only from `Killed` and `Hunting`, and episodes touching those
states fell from five to four in absolute terms while the total rose from
seventy to eighty-five. The increase sits in `Roaming`, `Acquisition` and
`Retreating`, which `CanSee` never touches, and the largest new episode is an
on-nav-graph route stall with no `CanSee` call in window.

The residual after de-fragmenting is `+3/+5/+1` against a stock cross-seed
spread of `{20, 18, 26}`, so it is within seed-level dispersion. Run-to-run
variance remains unestimated at one run per cell, and no claim here should be
read as a variance-controlled result.

## 3d. Same-binary control, and the view-spot refinement is inert

Section 3b compared against stock captures produced for a different campaign
(`walking-hitwall-conjunction-*`). A same-binary control was run to remove that
substitution as a confound: seed `104729`, current binary, every switch fixed,
varying only `--botbench-pawn-vision-cone`. It reproduces section 3b exactly:
kills `+12`, damage `+1218`, suicides `-4`, environmental deaths `-5`,
hazard-exposed deaths `-4`, no-progress `-154.6` seconds, stuck `+2`. Raw
`HitWall` volume also falls sharply, `5,629` to `2,493`. The corrected cone
result is therefore confirmed, not merely inherited from a reused control.

The view-spot refinement, which measures the cone from `Location() +
BaseEyeHeight()` rather than the pawn's feet as `Bot.uc:1360` does, was added
default-off behind `SURREAL_PAWN_VISION_CONE_VIEW_SPOT`
(`SurrealEngine/UObject/PawnVisionConeViewSpotCandidate.h`). Paired runs at all
three seeds, with the corrected cone enabled in both arms
(`qa/runs/2026-07-27/pawn-vision-cone-viewspot-ab/`), are byte-identical on
every published signal, including `hit_wall_events_exact` and no-progress
seconds to full precision. The cone-only arm reproduces the section 3b captures
across 289 analyzer fields at all three seeds, confirming both this candidate
and the uncommitted mover work are inert while disabled.

The refinement is nonetheless active, so this is a real null rather than a
plumbing failure. With the pawn vision observer enabled at seed `104729` it
moves corrected-cone acceptance from 253 to 223 of 410 calls and flips
`CanSee`'s returned value on 29 of them, the first at tick 86 of 7,200.
`BaseEyeHeight()` is not zero.

The reason a tick-86 flip leaves the next 7,114 ticks bit-identical is that
every flip lands in one caller:

| `Pawn.CanSee` caller | Calls | Flipped by the view spot |
| --- | ---: | ---: |
| `Killed` | 211 | 29 |
| `Hunting` | 199 | 0 |

`Killed` governs post-death reaction and does not feed movement or target
selection, so its visibility answer has no trajectory consequence. `Hunting`
does, and the view spot changed none of its decisions. The cone correction
itself changes decisions in both callers, which is why it diverges the run at
all: its call count moves from 363 to 410 while the view-spot arms both stay at
410.

The refinement is faithful to `Bot.uc:1360` and costs nothing, but it buys no
measured bot quality on this map and roster. It should not be promoted on
current evidence, and carrying it as a third default-off switch is not
justified by these results.

## 3e. ReachSpec capability filtering is a no-op on all measured content

The four `To do: check reachFlags` omissions in route expansion
(`UPawn::ActorReachable` twice, `UPawn::FindPathToEndPoint`,
`UPawn::FindRandomDest`) are now implemented behind
`SURREAL_REACHSPEC_CAPABILITY_FILTER`
(`SurrealEngine/UObject/PawnReachSpecCapabilityFilterCandidate.h`).

The rule is `FReachSpec::supports()` from the reference header:
`(reachFlags & moveFlags) == reachFlags`. Note this is not the same predicate
as the existing `EvaluateReachSpecEligibility`, which reports a zero
requirement as ineligible. That is correct for a diagnostic and wrong for a
live filter, because `supports()` admits a zero requirement. The route-search
rule is therefore a separate function,
`PawnMovement::ReachSpecSupportedByCapabilities`, with its own unit coverage;
the diagnostic evaluator is unchanged.

Paired runs at seed `104729` are identical on all nine published signals. The
reason is structural rather than incidental. Across eight map catalogs, four
UT99 and four Unreal 226b, `reachFlags` never takes a value outside
`{Walk, Walk+Jump, Special, Swim, Walk+Swim+Jump}`, is never zero, and never
carries a bit outside the known mask:

| Map | ReachSpecs | flags == 0 | unknown bits | flag values present |
| --- | ---: | ---: | ---: | --- |
| `DM-Deck16][` | 1997 | 0 | 0 | Walk 1528, Walk+Jump 448, Special 21 |
| `DM-Fractal` | 611 | 0 | 0 | Walk 539, Walk+Jump 58, Special 14 |
| `DM-Morpheus` | 655 | 0 | 0 | Walk 567, Walk+Jump 48, Special 40 |
| `DM-Pressure` | 1833 | 0 | 0 | Walk 1540, Walk+Jump 203, Special 46, Swim 40, Walk+Swim+Jump 4 |

The live bot capability mask is `125`
(`Walk|Swim|Jump|Door|Special|PlayerOnly`), uniform across 6,618 capability
snapshots in two attested observer runs, with `bIsPlayer` true. Every flag
value above is a subset of `125`, so no edge is rejectable. The single
capability the bots lack is `Fly`, and no sampled map contains a `Fly`
ReachSpec. No sampled map contains a `PlayerOnly` ReachSpec either. Both
attested runs recorded 100 percent `eligible` committed edges, 5,219 of 5,219
on `DM-Deck16][` and 823 of 823 on `DmDeathFan`.

This closes a real semantic divergence from the reference header, but it buys
no measurable bot quality, and on this evidence it cannot: the content contains
no edge these pawns cannot traverse. It would begin to bite only for a pawn
with `bIsPlayer` false against a `PlayerOnly` edge, for a non-flying pawn
against a `Fly` edge, or if `bCanJump` went false while a bot was routing,
which would make the 22 percent `Walk+Jump` share of `DM-Deck16][`
untraversable. `bCanJump` was true in every observed snapshot.

The switch therefore stays default-off. Promoting it is a correctness argument,
not a quality argument, and the risk it carries is unmeasured rather than
measured to be low.

## 4. Where the remaining bot-quality gaps actually are

Recorded here because the predicate work above is closed, not because it was
qualified by this iteration.

- `UPawn::CanSee`'s default vision cone (`UActor.cpp:5006-5009`) compares the
  facing axis against `normalize(origin)`, the target's absolute world position,
  instead of the eye-to-target direction; it also takes `abs()` of the cosine
  and rejects when the cosine is large. `PawnVisionCone.cpp:36-37` already
  implements the correct test but only runs under `--botbench-pawn-vision-cone`,
  which is default-off.
- `FindPathToEndPoint` (`UActor.cpp:5496`) still carries `To do: check
  reachFlags`, so route search admits edges whose movement type the pawn cannot
  perform. The ReachSpec graph traversal itself is faithful; only the capability
  filter is absent. `PawnReachSpecEligibility.cpp` exists but feeds diagnostics
  only.
- `UActor.cpp:5493` ORs `endActor->bPlayerOnly() && !bIsPlayer()` with itself.
- Pain-ledge vetoes are live in the default path (`UActor.cpp:8036`) and fired
  4,776 times, 4,744 of them repeats, in a single 120 second stock capture,
  while `ApplyPainLedgeRecovery` is reachable only from `UActor.cpp:7702` behind
  `IsBotBenchmarkHarmfulZoneEscapeEnabled()`, which is default-off.
- `Pawn.FindStairRotation` is an unimplemented stub (`NPawn.cpp:142-146`).

`wall_adjust_calls_exact` reads zero in every capture above because
`WallAdjustCallCountValue` is incremented only inside the
`IsBotBenchmarkFailedNavigationAvoidanceEnabled()` branch
(`UActor.cpp:4695-4698`). It does not indicate that stock wall adjustment is
inactive; the default fallback at `UActor.cpp:4783` still runs uninstrumented.
