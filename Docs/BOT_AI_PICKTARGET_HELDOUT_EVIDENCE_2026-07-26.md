# Held-out PickTarget qualification evidence — 2026-07-26

## Scope

This note records a single deterministic, no-UCC stock-versus-fixed comparison
of the shared native `UPawn::PickTarget` living-pawn predicate on maps not used
for the initial Deck16-II / DeathFan conclusion. It is negative qualification
evidence, not a reason to promote a policy.

Each run used 16 bots, difficulty 3, seed `104729`, 7,200 ticks, fixed delta
`0.016666668`, no behavior-control flags, and no optional observer flags.
`Analyze-BotQuality.py` accepted all four artifacts.

## Results

| Adapter and held-out map | Stock | PickTarget-fixed | Interpretation |
| --- | --- | --- | --- |
| UT436 `DM-Pressure?Game=Botpack.DeathMatchPlus` | K 51, D 52, S 1, stuck 9, coverage 0.60546875, pickups 15 | K 47, D 48, S 1, stuck 11, coverage 0.64453125, pickups 15 | Coverage improves, but kills fall and stuck events rise; net score and suicides do not improve. |
| Unreal Gold 226b `DmTundra?Game=UnrealShare.DeathMatchGame` | K 95, D 102, S 7, stuck 3, coverage 0.43, pickups 24 | K 97, D 104, S 7, stuck 1, coverage 0.33, pickups 29 | Kills, stalling, and pickups improve, but deaths rise and coverage falls by 0.10; net score and suicides do not improve. |

Artifacts are under
`qa/runs/2026-07-26/heldout-pick-target-comparison-v1/`.

## Qualification boundary

The pairs are one-repeat evidence only. Their mixed direction reinforces the
existing primary-map finding: the predicate correction is a real native
contract repair, but it is not yet a universally qualified bot-quality merge.
Do not enable it unconditionally or attribute either regression to an
unproven downstream mechanism.

`Validate-RealizedBotCapabilities.py` does not yet accept the current v3
summary schema because it requires summary-v2. That unrelated validator gap
means realized-capability validation cannot be claimed for these artifacts;
the accepted quality-analysis result is the only stated validation here.
