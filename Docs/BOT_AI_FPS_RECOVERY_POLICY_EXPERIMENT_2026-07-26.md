# Transferable FPS recovery patterns and a bounded policy experiment

Date: 2026-07-26

## Decision

Test one explicitly selected enhanced-policy experiment:
**`utility-arena-recovery-probe-v1`**.  On a genuine walking
movement-intent stall, it temporarily maps the existing
`RecoverFromStuck` action to **one bounded, safe lateral steering probe**, then
resumes the suspended existing action if it is still valid.

This is deliberately not a route rewrite, a target-selection rewrite, a jump
escape, a random strafe, or a change to `stock-botpack`.  It is an opt-in
enhanced policy plus a narrow action adapter.  Until it passes its fixture and
cross-game evidence gates, it remains shadow-only or disabled.

The existing code makes this unusually small:

- `UtilityBotPolicy` already gives `RecoverFromStuck` deterministic priority
  over combat, acquisition, retreat, and exploration, and keeps a resumable
  action stack.
- `TacticalBotPolicy` independently models the same interruption as
  `UnstuckRecovery`.
- `MovementSafetyAdvisor` already scores a bounded set of collision/landing
  probes; while recovering it favors a direction unlike the failed heading,
  rejects pain, unsupported landings, dangerous drops, and unsafe jumps, and
  avoids immediately repeating a prior recovery probe.

The experiment should reuse those contracts rather than introduce a fourth
unstuck mechanism.

No code, UCC invocation, executable run, map edit, or commercial-game-data
copy occurred for this note.

## Source-to-action mapping

The research sources are design references only.  Their code must not be
copied without a file-level license/provenance review; GPL and Source-SDK
references are clean-room architecture evidence, not donors.

| Reference family | Transferable observed pattern | Existing Surreal action surface | Adoption decision |
| --- | --- | --- | --- |
| YaPB / PODBot task-desire lineage | High-priority movement recovery interrupts lower-priority goals; navigation separates goal choice from local movement; recovery/stuck thresholds are context-sensitive. | `RecoverFromStuck` preempts `AttackEnemy`, `HuntEnemy`, `AcquireItem`, `Retreat`, `Explore`, and `InvestigateSound`; `MovementSafetyAdvisor` owns local safe-direction choice. | Use the interruption and local-steering separation.  Do not port waypoint/task code or difficulty rules. |
| ReGameDLL_CS / CSBot state model | Explicit states make recovery and combat interruption inspectable rather than an implicit side effect. | `TacticalState::UnstuckRecovery` and the `Decision` reason/action record; the experimental utility policy will emit the same existing action. | Use a named, telemetry-visible recovery episode.  Do not add CS-specific objectives, hiding spots, or global game state. |
| Quake III BotLib/AAS | Failed movement can temporarily avoid a bad traversal and try alternatives; route operations and local movement prediction are distinct. | `RecoverFromStuck` plus `MovementSafetyAdvisor` alternatives.  There is no generic policy action for mutating ReachSpecs or route cache. | Reuse only the **local alternative** idea for one steering burst.  Do not add avoid-reach memory, edge penalties, or replanning in this experiment. |
| UE1 Botpack and UnrealShare bot scripts | Script-owned `MoveTo`/`MoveToward`, path cache, `HitWall`, lifts, falls, and combat state remain the compatibility baseline. | `stock-botpack` is explicitly rejected by `PolicyRegistry` as script-owned; `Action` decisions are enhanced-policy data, not a back door into stock scripts. | Preserve stock behavior.  The adapter must neither alter route cache nor invoke path APIs while recovering. |
| Valve NextBot / general action-stack designs | Suspend a goal for a short failure-recovery action, then resume only if it remains valid. | `UtilityBotPolicy::ActionStack`; `RecoverFromStuck` already preempts and later re-evaluates viable actions. | Use action suspension/resumption, with a bounded time/progress contract.  Do not import a behavior-tree framework. |
| Pogamut / bot benchmark systems | Treat observation, action choice, and outcome as separately recorded; compare seeded unattended matches. | `PolicyShadowEvaluator`, fixed-step benchmark manifests, and quality telemetry. | Shadow the decision first; promote only with same-life recovery outcomes and paired matrices. |

Primary provenance index: `BOT_AI_RESEARCH.md`.  It records YaPB as an MIT
reference (with submodule audit required), ReGameDLL_CS as reverse-engineered
MIT-transition material requiring file/history audit, Quake III/PODBot/Pogamut
as study-only under GPL, and Valve NextBot as architecture-only under Source
SDK terms.

## Exact experiment contract

### Policy identity and ownership

Register a new immutable enhanced identity,
`utility-arena-recovery-probe-v1`; do not silently change `utility-arena` or
`stock-botpack`.  It uses the existing `Action` enum—no new policy action is
needed.

The integration is a per-participant action adapter, enabled only by the
benchmark/policy selection for that participant.  It must be absent from stock
participants, from an unselected enhanced policy, and from normal gameplay
unless explicitly configured.

### Entry gate

Enter exactly one recovery episode only when all conditions hold:

1. The selected enhanced policy reports `Action::RecoverFromStuck` because
   `StuckSeconds >= 1.0` while the participant has an actual walking movement
   intent.
2. The pawn has finite pose/velocity and a finite desired horizontal heading;
   it is not falling, swimming, flying, on a mover/lift transition, in a
   pain/water escape episode, or executing a verified special traversal.
3. A fixed, stable-index set of at most four local probes is available:
   forward, left, right, and reverse in the current horizontal frame.  The
   engine adapter supplies collision, support, drop, pain, and jump evidence;
   the policy never fabricates geometry.
4. `MovementSafetyAdvisor` returns a valid
   `MovementAdviceAction::RecoverFromStuck` with a viable probe.  Invalid or
   unsafe evidence fails closed and produces no command.

The fixed probe set makes the decision comparable across UT436 and Unreal
226b.  Existing advisor ordering gives reproducible ties and does not repeat
the last recovery direction on a retry; this first experiment stops after one
burst rather than using a retry ladder.

### One-burst behavior

When admitted, the adapter:

1. snapshots the suspended policy action and target identity;
2. applies only the selected horizontal steering/acceleration direction for
   15 fixed 60 Hz ticks (0.25 seconds);
3. leaves aim, fire, weapon selection, `Enemy`, inventory choice, and script
   state untouched;
4. does **not** call `FindPathTo`, `FindPathToward`, `FindBestInventoryPath`,
   `ClearPaths`, `SetEnemy`, or mutate route cache/ReachSpecs; and
5. ends the burst on first confirmed progress or at the 15-tick budget.

Progress means the existing movement-intent watchdog clears the stall with a
finite displacement at least one collision radius from the episode start.
After the burst, re-evaluate the suspended action through the normal policy:

- If the original target/action is still valid, resume it.  Example:
  `AttackEnemy(enemy-7)` -> `RecoverFromStuck` -> `AttackEnemy(enemy-7)`.
- If it is no longer valid, make an ordinary deterministic policy decision;
  do not retain a stale target merely to claim continuity.
- If the burst did not produce progress, relinquish control to the existing
  script/native movement path for that episode.  No second probe, route
  penalty, jump, or goal replacement is allowed in v1.

This is the smallest transferable form of FPS-bot recovery: interrupt only
the failed locomotion command, preserve the strategic/combat decision, and
fail closed when local geometry cannot prove a safe alternative.

## Mapping during an episode

| Prior policy action | During valid stall | On clear progress | On invalid target/no progress |
| --- | --- | --- | --- |
| `AttackEnemy` | `RecoverFromStuck`; preserve target identity and weapon/aim ownership | Resume attack only if enemy remains a valid visible target | Ordinary policy re-evaluation; never fire/retarget as a recovery side effect |
| `HuntEnemy` / `InvestigateSound` | `RecoverFromStuck`; retain subjective memory only | Resume only if confidence/age rule still passes | Re-evaluate; expired memory must not be revived |
| `AcquireItem` | `RecoverFromStuck`; retain item identity only | Resume only if item remains reachable/valid | Re-evaluate reachable items normally |
| `Retreat` / `Explore` | `RecoverFromStuck`; retain no new tactical target | Resume only if regular score/hysteresis keeps it viable | Re-evaluate normally |
| `Idle` | No episode: it lacks movement intent | Not applicable | Not applicable |

`RecoverFromStuck` thus has one meaning at both layers: policy-level temporary
preemption and movement-level safe local steering.  The adapter must record
both meanings separately so a policy choice is never mistaken for a movement
command that actually executed.

## Deterministic fixture plan

Add a pure adapter fixture beside the existing `BotMovementSafetyTests` and
`UtilityBotPolicyTests`; it consumes synthetic observations/probes and records
commands, not a map or retail script.  Required cases:

1. **Combat continuity:** armed visible target -> movement-intent stall ->
   blocked forward plus equal safe left/right probes -> exactly one stable
   lateral 15-tick command -> measured progress -> same `AttackEnemy` identity
   resumes.  Assert no aim/fire/target/path callback occurred.
2. **Safety failure:** pain, unsupported landing, excessive drop, unsafe jump,
   invalid/non-finite probe, duplicate index, and no safe probe each emit no
   recovery command.  Assert stock/native ownership remains untouched.
3. **No false recovery:** idle, no movement intent, sub-threshold stall,
   falling, swimming, flying, lift/mover, pain/water escape, and special
   traversal never enter the adapter.
4. **Expired continuity:** a target disappears or subjective memory expires
   during the 15-tick burst; after the burst the policy re-evaluates and does
   not resume its old identity.
5. **Episode bound:** a failed burst records one attempt and rejects a second
   attempt until the watchdog has observed a new, independent movement-intent
   stall episode.  Probe permutations must choose the same index/reason.
6. **Policy isolation:** equivalent stock-botpack and unselected-policy
   fixture inputs produce no adapter command; enabling shadow telemetry alone
   changes no command stream.

The fixture's oracle is intentionally mechanical: selected probe index,
duration, progress flag, suspended/resumed action identity, and a zero count
for forbidden route/target/fire calls.  It does not claim retail navigation
parity from a synthetic corridor.

## Observation and cross-game measurement

First add shadow-only records for the candidate, one per episode:

```text
life_id, episode_id, policy_id/version, prior_action/target,
stuck_seconds, physics/movement-intent eligibility, probe evidence,
selected_probe/reason, command_started, burst_ticks, progress,
post_action/target, route_api_calls=0, target_writes=0, fire_writes=0
```

This lets analysis distinguish a policy request from a successful recovery
command and prevents a later stale route-cache sample from being assigned as
the cause.

Only after observer-on/off equivalence and fixture success, run paired,
manifest-attested bot-only matrices with the same roster, map, seed, tick
budget, build identity, and fixed delta for baseline and candidate:

| Lane | Tuning map | Required comparisons |
| --- | --- | --- |
| UT436 | `DM-Deck16][` | repaired stock, `utility-arena` baseline, and `utility-arena-recovery-probe-v1`; at least three seeds with two same-config repetitions each |
| Unreal 226b | `DmDeathFan`, then `DmDeck16` | same three variants and repetition discipline; no UT class/skill assumptions |

Report both per-run and paired aggregates for:

- movement-intent stall episodes; recovery attempts; safe-probe admission;
  cleared within 2 seconds; cleared-or-replanned within 5 seconds; failed
  single bursts; and repeated same-life stalls;
- hit-wall events, route/path calls during recovery (must remain zero),
  hazard entries/exposure, falls, suicides, and environmental deaths;
- kills, deaths, score delta, exact damage dealt/taken, combat engagement and
  firing continuity; and
- deterministic replay digest, policy identity/version, bounded probe work,
  structural completion, and observer-on/off equivalence.

Promotion requires every structural/safety gate, no forbidden callback, and
cross-game non-inferiority in combat and hazard metrics against the selected
enhanced-policy baseline.  Its claimed benefit must be a paired reduction in
open/repeated movement-intent stalls with a corresponding increase in
same-life cleared-within-2-seconds episodes—not merely fewer deaths or a
larger score on one map.  A combat regression like the prior targetless
`MoveTo` timeout result closes this v1 experiment rather than broadening its
scope.

## Explicit exclusions

Do not combine this experiment with ReachSpec capability filtering, route-cost
changes, failed-node/avoid-reach memory, danger learning, target-selection
changes, splash-fire policy, hazard escape, jump logic, or skill tuning.
Those may be valid later studies, but combining them would make a positive or
negative result impossible to attribute to the local recovery probe.
