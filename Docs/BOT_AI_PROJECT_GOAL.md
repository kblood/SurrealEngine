# BOT AI project goal

## Objective

Deliver merge-ready BOT AI for SurrealEngine's UT436 and Unreal Gold 226b
adapters that measurably improves navigation, survival, combat competence, and
movement safety. Preserve stock behavior unless a focused change passes
deterministic fixtures, exact telemetry, fail-closed analysis, and cross-game
qualification evidence.

## Definition of done

- Bots complete no-player matches on the required UT436 and Unreal Gold map
  matrices without avoidable self-kills or unassisted environmental deaths.
- A candidate improves or at least does not regress paired combat, score,
  movement-intent stall, and safety metrics against repaired stock.
- Every promoted behavior has a narrow, deterministic fixture that establishes
  its native and script contract in both games where it is shared.
- Telemetry is attributable, bounded, reconciled with exact counters, and
  rejected by the analyzer when evidence is incomplete, malformed, or
  overflows.
- Qualification runs use Release builds, pinned owner-local game data, frozen
  tuning maps before held-out maps, and versioned gates.
- The remaining causal avoidable-suicide, per-participant role-swap, and
  in-engine 16-bot AI-frame-time requirements are implemented and pass their
  gates.

## Operating rules

- Treat bot behavior as unproven until measurement says otherwise. A lower
  proxy alone is not a promotion.
- Keep experiments opt-in and default-off. Revert or retain a candidate as
  rejected when it regresses another required quality signal.
- Do not mix avatar work with BOT AI commits, branches, or evidence.
- Use parallel work for independent research, fixtures, telemetry, analyzer,
  and campaign verification; integrate only reviewed, focused slices.
- Never represent a zero-opportunity or incomplete telemetry run as a quality
  pass.

## Current milestone

Use the qualified move-stall observer and causal suicide evidence in owner-data
discovery runs. The immediate behavior investigation is the rejected live
hazard-swim-egress experiment: its command-safe, one-physics-step correction
now needs paired UT436 and Unreal Gold qualification. Live causal labels remain
unavailable until the pre-commit witness is reconciled with a terminal death.
Recovery-time thresholds may be evaluated only when a candidate campaign
supplies a non-zero, complete opportunity set; the forced fixture qualifies the
measurement path, not bot quality.

## Current status

The cross-game forced move-stall fixture passed twice on UT436 `DM-Deck16][`
and Unreal Gold 226b `DmDeathFan`: each run emitted exactly one stationary
targetless `MoveTo` detection and one genuine clearance in 0.25 seconds, with
no overflow or other terminal outcome. This validates observer accounting, not
a behavior improvement. No recent live behavior change has met the promotion
bar. The targetless `MoveTo` timeout experiment remains default-off and
rejected: it reduced a stuck proxy but regressed combat/score and did not
reduce suicides. The project is not merge-ready as a BOT AI behavior change.
