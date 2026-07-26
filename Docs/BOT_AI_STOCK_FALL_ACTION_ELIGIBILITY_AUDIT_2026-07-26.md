# Stock Deck16 / DeathFan fall-action eligibility audit — 2026-07-26

## Decision

There is no cross-game, release-eligible fall-action candidate in the currently
captured stock-map telemetry.

`DM-Deck16][` does contain exact, confirmed positive-DPS walking fall forecasts,
so the narrowly scoped walking-step veto has a real UT opportunity.  It is not
a shared candidate: the equivalent DeathFan capture has no such preflight
authorization.  Moreover, the current 16-bot Deck candidate capture reaches
only the existing pain-ledge guard; its six recorded opt-in attempts are all
`legacy_pain_ledge_superseded`, not a new rollback/replan.

The falling-hazard recovery path sees verified harmful entries in both games,
but never reaches gate eligibility in either.  This is a correct fail-closed
result, not evidence that recovery should be loosened.

This audit is read-only.  It did not launch a game, run UCC/UnrealEd, or modify
runtime behavior.

## Evidence set

| Game / map | Captured configuration | Result |
| --- | --- | --- |
| UT436 `DM-Deck16][` | two stock bots, skill 7, seed `104729`, 7,200 ticks (120 s), falling-recovery live flag enabled | Exact preflight and vertical-fall evidence; recovery never eligible. |
| Unreal 226b `DmDeathFan` | two stock bots, skill 3, seed `271828`, 7,200 ticks (120 s), falling-recovery live flag enabled | Exact vertical harmful-entry evidence; no positive-DPS walking preflight and recovery never eligible. |
| UT436 `DM-Deck16][` | 16 stock bots, skill 3, seed `271828`, 1,200 ticks (20 s), positive-DPS preflight veto enabled | Six opt-in episodes, all already handled by the legacy guard; no new live veto. |

The corresponding manifests, event streams, and summaries are under
`SurrealEngine/qa/runs/2026-07-26/falling-hazard-recovery-scout-v1/` and
`SurrealEngine/qa/runs/2026-07-26/ut436-deck16-16bot-s271828-walking-positive-dps-veto-r1/`.
All three runs completed successfully.

## UT436 `DM-Deck16][`

The 120-second two-bot capture records 1,845 exact
`walking_step_preflight_reason_harmful_pain_fall_exact` decisions.  The paired
diagnostic stream contains the provisional and post-`MayFall` records for
these opportunities: 1,658 end in `begin_falling` and 187 in
`restore_grounded`.  The forecast is unusually complete for this class: static
BSP landing, walkable normal, known downward gravity `-950`, known positive
pain damage `40 DPS`, and no continuation segment in the sampled examples.

For example, `TMale2Bot0`, life generation 1, invocation 269, targets
`PathNode121`; its forecast starts near `(1861.08, 1293.50, -600.17)`, drops
160.08 units, and predicts a 40-DPS pain landing.  The confirmed transition is
`begin_falling`.  This proves a narrow UT preflight opportunity; it does not
prove that an extra action is safe after stock script processing.

The same capture has three vertical episodes that predict and then confirm a
harmful water entry.  They have known foot-zone identity and actual foot-zone
entry, but each is sourced as `external_impulse_commit`, has
`not_aligned_continuation` command provenance, and terminates after one sampled
segment (0.01833 s).  Thus they are exact harm witnesses, not a demonstrated
bot-command lead for a falling recovery steer.

The enabled recovery hook made 5,140 advance calls.  It recorded three
persistent harmful-fall promotions, but zero single-harmful-prefix promotions,
zero recovery eligibility, zero live applies, zero active recovery ticks, and
zero anchor/probe rejections.  The decisive failure is before the anchor/probe
gate: 4,959 calls report `no_prefix` (and 181 lack an active fall episode).

The 16-bot positive-DPS experiment independently confirms that the preflight
hook can be reached: 129 eligible observations collapse to six distinct
episodes after 123 debounces.  Its six action records are all
`legacy_pain_ledge_superseded`; none attempts a rollback, forces a replan, or
counts as `applied`.  The stock pain-ledge guard already owns those outcomes.

## Unreal 226b `DmDeathFan`

The 120-second capture has no
`walking_step_preflight_reason_harmful_pain_fall_exact` decision and no
positive-DPS preflight action.  It therefore supplies no DeathFan counterpart
to the UT walking-step candidate.

It does have six exact vertical harmful-entry confirmations.  Each forecast
expects harmful water and observes the known harmful foot zone.  Two are
`callback_return_commit` trajectories lasting about 0.51–0.53 seconds; four
are one-segment `external_impulse_commit` entries.  Every one has
`not_aligned_continuation` command provenance.  This is strong evidence of the
harmful outcome but not of a controllable, pre-entry navigation command.

Recovery made 3,652 advance calls.  It saw three persistent harmful-fall
promotions, but again zero single-harmful-prefix promotions, zero recovery
eligibility, zero live applies, and zero active recovery ticks.  Of the calls,
3,597 stop at `no_prefix` and 55 have no active fall episode.

## Why the existing recovery action remains ineligible

`UPawn::AdvanceFallingHazardRecovery` requires an active falling episode,
stock autonomous bot context, a promoted **single** harmful-fall prefix, and a
safe captured anchor.  The subsequent gate additionally requires a finite
same-life/same-episode anchor whose horizontal distance is 8–256 units before
it can authorize an action.  Neither stock capture reaches that gate: no
single-prefix promotion occurs, so there is no measured valid anchor distance,
probe result, or live air-control outcome to justify a behavior change.

Changing recovery to consume a persistent prefix, or treating an external
impulse/callback-return trace as a bot-command lead, would replace these
missing prerequisites with inference.  That candidate is rejected for now.

## Candidate disposition

| Candidate | UT Deck16 | Unreal DeathFan | Disposition |
| --- | --- | --- | --- |
| Positive-DPS walking-step veto after legacy guard declines | Exact local opportunity, but current opt-in records only legacy supersession | No qualifying preflight opportunity | Not shared; remain experimental and UT-only. |
| Existing falling-hazard recovery steer | Verified harm, but no single-prefix promotion or eligible anchor | Same | Shared observation class, but no eligible shared action. |
| Broaden recovery from persistent prefixes / external impulses | Would be an inference beyond the gate evidence | Would be an inference beyond the gate evidence | Rejected. |

The next admissible work is observational: correlate each confirmed harmful
vertical episode with whether a single-prefix can be truthfully promoted and,
only then, record the same-life anchor position, 8–256-unit distance check,
clear probe, and air-control availability.  A live recovery change remains
blocked until at least one such complete record exists in each game and paired
non-regression evidence is available.
