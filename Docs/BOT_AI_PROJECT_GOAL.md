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
discovery runs. Command-stable harmful-fall terminal correlations are now
available as nullable observer metrics, but avoidability remains unproven until
safe alternatives and external-intervention exclusions are captured. The
immediate behavior investigation is the default-off, replan-only
hazard-swim-egress experiment: it lowers UT436 Deck16-II hazard exposure without
changing kills, deaths, or suicides, so it is retained only for measurement.
The next slice records an exact terminal disposition for every planner handoff:
same stock command reissued, a changed stock command, clearance, falling,
death, or an explicit life/run/abandon censor. Initial UT evidence contains one
same-command and one changed-command reissue, neither with a survival result;
Unreal discovery runs currently have no qualifying handoff opportunity. No
target-redirection policy is authorized until a multi-seed opportunity set
shows a causal target-reissue pattern.
Recovery-time thresholds may be evaluated only when a candidate campaign
supplies a non-zero, complete opportunity set; the forced fixture qualifies the
measurement path, not bot quality.

The current observer slice records every autonomous stock-bot residence in a
positive-DPS zone, its exclusive terminal outcome, re-entry count, native
movement-command churn, and whether an already collision-probed direct
navigation candidate was followed by another command. It does not write
acceleration, destination, latent state, physics, or route state. Its purpose
is to distinguish a missing safe option from an option stock code abandons.

## Current status

The cross-game forced move-stall fixture passed twice on UT436 `DM-Deck16][`
and Unreal Gold 226b `DmDeathFan`: each run emitted exactly one stationary
targetless `MoveTo` detection and one genuine clearance in 0.25 seconds, with
no overflow or other terminal outcome. This validates observer accounting, not
a behavior improvement. No recent live behavior change has met the promotion
bar. The targetless `MoveTo` timeout experiment remains default-off and
rejected: it reduced a stuck proxy but regressed combat/score and did not
reduce suicides. The project is not merge-ready as a BOT AI behavior change.

The new UT436 Deck16-II 7,200-tick seed-271828 smoke retains K4/D9/S5 and five
hazard entries while reporting five harmful residences: all ended in death,
with 289 re-entries, two safe direct-navigation candidates observed, and two
subsequent other native movement commands. Unreal Gold 226b DeathFan retains
K7/D29/S22 and 33 entries while reporting 28 residences: three cleared and 25
ended in death, with no direct-navigation candidate. This is discovery
evidence, not a quality improvement or action authorization. The next policy
candidate must explain UT's observed candidate abandonment and independently
certify a controllable Unreal alternative.

The external-impulse fall witness now provides that negative certification for
the inspected Unreal DeathFan case: two byte-identical 7,200-tick seed-271828
runs (events SHA-256
`6761756157108FA7CF71E33D1323F15147FF9DC2705AAB951EBDBFAB74E3AD7E`) retain
K7/D29/S22 and record 260 harmful avoidance-relevant external-impulse
forecasts. All 2,080 bounded air-control counterfactuals fail to reach a
static, dry landing inside the existing forecast horizon; zero alternatives
are certified. Therefore air-control steering is rejected for this observed
failure class. This is not a claim that the deaths are unavoidable in every
game state: alternative recovery mechanisms need their own causal witness.
