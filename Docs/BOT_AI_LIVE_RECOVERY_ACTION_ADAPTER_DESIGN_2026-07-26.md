# Per-slot live recovery-action adapter design

Date: 2026-07-26

## Decision

The smallest safe live bridge for an enhanced `BotPolicy` is a **next-tick,
per-slot, one-burst acceleration overlay** for an already-selected
`Action::RecoverFromStuck` decision.  It runs only for an eligible walking
`MoveTo` or `MoveToward` latent command, writes only horizontal
`Acceleration` for at most 15 fixed 60 Hz ticks, and otherwise leaves the
UnrealScript state machine untouched.

It must not manipulate `MoveTarget`, `Destination`, `Focus`, `Enemy`,
`MoveTimer`, `RouteCache`, reachspecs, physics mode, view rotation, weapon
state, latent state, or path APIs.  It is a temporary local steering overlay,
not a replacement path follower.

This design applies to UT436 `Botpack.Bot` and Unreal Gold 226b
`UnrealShare.Bots` through their shared `UPawn` walking/latent-movement
implementation.  Game-specific code is limited to participant/slot
qualification, never to the steering algorithm.

No code, UCC invocation, executable run, map edit, or commercial-game-data
copy occurred for this design note.

## Current-state findings

There is no live policy adapter today.

1. `PolicyShadowEvaluator` evaluates policies only as shadow instances.
   `BotBenchmarkDriver::WriteShadowTelemetry` calls it *after*
   `EngineRef.Level->Tick` for the current benchmark tick.  Its decisions have
   no command-application path.
2. `CaptureShadowObservation` fills health, weapon, enemies, and other data,
   but does not populate `RuntimeSelfSnapshot::StuckSeconds`.  The policy
   observation therefore receives its default zero and cannot currently select
   recovery from live watchdog evidence.
3. `UPawn::Tick` runs the useful native ordering:

   ```text
   ObserveMoveStallWatchdog
   -> decrement MoveTimer
   -> latent MoveTo / MoveToward calls TickMoveTo
   -> UActor::Tick consumes Acceleration in physics
   ```

   `TickMoveTo` is the narrow command seam.  On a walking command it first
   checks arrival, then gives existing harmful-zone, pain-ledge, and
   wall-adjust safety overlays an opportunity to own acceleration, and finally
   writes the normal acceleration toward the script-owned destination.
4. The move-stall watchdog already provides a same-life episode model,
   command-key identity, progress radius, detection threshold, and terminal
   outcomes (clear, replan, deadline miss, life boundary, and so on).  Current
   public methods expose counters/terminal drains but not a read-only current
   no-progress/episode snapshot for the policy builder.

These facts rule out three tempting shortcuts:

- applying a policy decision in the driver during the same tick it was
  evaluated (the physics tick is already complete);
- globally enabling recovery for every autonomous stock bot; and
- clearing `MoveTimer` or overwriting latent state to make a policy decision
  appear to have resumed a script action.

## Adapter shape

### Ownership and slot binding

Create a benchmark-owned live-action registry with exactly one optional
recovery entry per selected roster slot.  A slot is bound using all of:

- immutable benchmark configuration identity;
- roster index and stable participant identity;
- current pawn actor index; and
- current pawn life ID.

The request cannot be consumed if any binding differs.  This prevents a
decision from a dead pawn, stale actor instance, respawn, reused object slot,
or another participant from steering the current pawn.

The benchmark driver evaluates the selected enhanced policy after tick `T` and
publishes a **pending, next-tick-only** request.  During tick `T+1`, `UPawn`
may consume it at `TickMoveTo` only after re-validating its binding and native
eligibility.  A request that cannot be consumed on that next tick is terminal
and logged as stale/rejected; it is never queued indefinitely.

`stock-botpack` remains script-owned and has no registry entry.  The current
registry already identifies it as `ScriptOwnedPolicy`; live selection must
preserve that boundary.  The proposed experimental identity is
`utility-arena-recovery-probe-v1`, not a mutation of the baseline policy.

### Read-only policy input

Before a policy can request recovery, expose a read-only snapshot from the
native watchdog to the policy-observation builder:

```text
life_id, current actor index, current native tick,
eligible movement intent, latent mode, no-progress seconds,
stall-detected flag, active episode ID, command key identity,
progress anchor, and current recovery-owner state
```

`StuckSeconds` is populated only from an eligible, active native
movement-intent observation.  It is zero when no movement intent exists; it
must not be inferred from a low velocity, idle/aiming, a lift wait, or an old
terminal counter.  This preserves the policy's existing `>= 1.0` recovery
threshold while giving it an actual live signal.

The snapshot must report whether another safety recovery owns the pawn:
harmful-zone escape, pain-ledge recovery, wall-adjust recovery, falling
recovery, water egress, or a pending forced native replan.  A policy can
observe these facts, but may not override them.

### Exact apply point

Consume the request only in the walking branch of `UPawn::TickMoveTo`, after:

1. the negative `MoveTimer` guard and arrival check;
2. harmful-zone escape; and
3. pain-ledge and wall-adjust recovery checks.

If none of those own acceleration, the adapter may replace the ordinary
``normalize(destination - location) * AccelRate`` assignment for that one
physics tick with:

```text
selected safe horizontal probe direction * AccelRate
```

Then normal pawn physics consumes the acceleration exactly as it would consume
script-selected acceleration.  On the next tick, the script/native `TickMoveTo`
again writes its normal direction unless another still-valid recovery tick is
admitted.

Restrict v1 to latent `MoveTo` and `MoveToward`.  Exclude `StrafeTo` and
`StrafeFacing` even though they share `TickMoveTo`: those modes deliberately
combine movement with aiming and are the most likely place to turn a locomotion
experiment into a combat regression.  Also reject falling, flying, swimming,
pain/water zones, movers/lifts, special traversal, deleted/dead pawns,
non-authority roles, invalid/non-finite state, and an expired `MoveTimer`.

The engine probe adapter supplies four stable-index candidates in the pawn's
horizontal frame (forward, left, right, reverse), including sweep clearance,
support, drop, pain-zone, and jump evidence.  It delegates selection to the
existing deterministic `MovementSafetyAdvisor`; no policy or benchmark code
performs geometry queries directly.

## Suspension, resumption, and rollback

The policy action may be suspended; the UnrealScript latent action must not
be.

| Concern | Safe behavior | Prohibited behavior |
| --- | --- | --- |
| Policy suspension | Store the pre-recovery policy action/target as telemetry context.  After the burst, normal policy evaluation may select it again only if still valid. | Treat policy bookkeeping as permission to replay, write, or restore a script state. |
| Script movement | Leave latent mode, target, destination, focus, and move timer in place.  Script code keeps ownership and naturally resumes its normal steering on the next tick. | Set latent mode to `Continue`, call a latent native, call `FindPath*`, or set `MoveTimer=-1` to force a replan. |
| Acceleration | Override only at the verified `TickMoveTo` slot before that tick's physics.  On cancel/end, stop writing it; do not restore a cached vector. | Restore an old acceleration after the script has produced a newer command, or write acceleration outside the walking movement slot. |
| Physical rollback | Before writing, fail closed.  After a tick's acceleration reaches physics, record that committed tick and judge its outcome. | Teleport or `TryMove` the pawn backward to undo a steering tick. |

There is intentionally no physical rollback after apply: once physics consumes
an acceleration vector, rewinding would be a second, less-compatible movement
intervention.  The rollback contract is instead **no residual override**:
cancel the per-slot entry immediately on death, deletion, life/actor mismatch,
physics/latent-mode change, competing safety owner, invalid probe, command-key
change, run end, or 15-tick expiry.  The next normal `TickMoveTo` call then
reasserts script-native steering.

## One-burst state machine

```text
shadow observation at T
  -> policy selects RecoverFromStuck
  -> publish request for T+1 (bound to slot/actor/life/episode/command)
  -> T+1 native validation
       -> reject/cancel (no write), or
       -> apply selected safe acceleration for one physics tick
  -> repeat only while same binding/episode remains valid, up to 15 ticks
  -> stop intercepting
  -> observe watchdog outcome and next script command
```

The request must be debounced to one burst per native stall episode.  A later
recovery requires a newly observed movement-intent episode after progress or
an explicit command-key reset.  Do not chain left/right probes, add route
memory, or turn a failed burst into a jump/replan in v1.

## Required attribution record

Each requested or applied episode needs a bounded same-life record, not only
aggregate counters:

```text
config_id, policy_id/version, roster_index, participant_identity,
life_id, actor_index, native_stall_episode_id, command_key,
observation_tick, decision_tick, request_tick, consume_tick,
latent_mode, move_target/destination snapshot, prior policy action/target,
eligibility/rejection reason, safety-owner state,
candidate probe evidence, selected probe, applied_tick_count,
pre/post location/velocity/acceleration, progress displacement,
script command at first post-burst tick, watchdog terminal outcome,
route_api_calls_by_adapter=0, target_writes_by_adapter=0,
move_timer_writes_by_adapter=0, weapon_or_view_writes_by_adapter=0
```

The adapter's own write counters are a contract check, while before/after
script snapshots are diagnostic context: a script may legitimately change its
own target or destination during the burst, and that change must not be
attributed to the adapter.

A recovery is causally qualified only if the same life, actor, stall episode,
and command key are present; the action starts before the measured progress;
no competing recovery or script command replacement occurs before that
progress; and the watchdog reports clear/replan within the registered window.
Death, hazard entry, wall contact, target change, latent completion, command
replacement, and run end become separate terminal categories rather than
being silently counted as either success or failure.

## Adapter-specific risks and controls

| Risk | Control |
| --- | --- |
| Shadow decision is one tick late or belongs to another pawn life | Next-tick expiry plus configuration, roster, identity, actor-index, life-ID, episode-ID, and command-key binding. |
| A live decision never reaches recovery because `StuckSeconds` is currently absent | Add the read-only watchdog snapshot first; prove observer-only equivalence before adding any action application. |
| Script/native movement state is damaged | Only override acceleration at `TickMoveTo`; prohibit route, target, timer, latent, physics, weapon, and aim writes; record zero adapter writes. |
| Existing hazard/wall recovery fights the policy | Native safety overlays have precedence; a competing owner cancels the request without a write. |
| Intentional combat strafing is labelled stuck | Exclude `StrafeTo`/`StrafeFacing`; require watchdog-confirmed `MoveTo`/`MoveToward` movement intent. |
| Lateral move enters hazard or falls | Require advisor-safe sweep/support/drop/zone evidence on every applied tick; invalid evidence fails closed. |
| One intervention turns into a hidden policy replacement | One burst/episode, 15-tick cap, per-slot opt-in, no automatic reroute or follow-up action. |
| Cross-game class/lifecycle mismatch | Use only shared `UPawn` seam; independently attest UT `Bot` and Unreal `Bots` roster/life bindings before enabling either lane. |

## Deterministic fixture plan

1. **Channel lifecycle fixture:** publish a request for slot A, actor/life X;
   prove only the matching pawn consumes it once on the next tick.  Wrong slot,
   identity, actor index, life ID, tick, episode ID, and command key all
   reject without a write.
2. **Shared walking-slot fixture:** eligible `MoveToward`, blocked forward,
   two safe lateral candidates; assert deterministic probe selection,
   exactly 15 maximum acceleration writes, and ordinary script acceleration on
   the first post-burst tick.
3. **State-preservation fixture:** snapshot latent mode, `MoveTarget`,
   `Destination`, `Focus`, `Enemy`, `MoveTimer`, route-cache head, physics,
   weapon, and view state.  Assert the adapter changes none; only the
   intended per-tick acceleration differs.
4. **Priority fixture:** arrival, negative timer, pain/water, falling,
   swimming, flying, mover/lift, `Strafe*`, active hazard/wall recovery,
   invalid probe, deleted/dead pawn, and non-authority pawn all cancel before
   a write.
5. **Abort fixture:** death, respawn/life increment, target deletion,
   latent command replacement, competing safety recovery, and run-end clear
   the entry.  Verify no stale acceleration override occurs afterward.
6. **Outcome fixture:** a synthetic progress result joins to the matching
   watchdog episode; a same-life command replacement, hazard terminal, and
   life boundary are classified as distinct, non-success results.
7. **Cross-game binding fixture:** separately instantiate/validate UT436
   `Bot` and Unreal 226b `Bots` participant adapters, while sharing the same
   channel and walking-slot test vectors.

## Live qualification order

1. Observer-only: publish no requests, expose native watchdog input, and prove
   observer on/off byte-equivalent runs in UT436 `DM-Deck16][` and Unreal
   `DmDeathFan`.
2. Shadow request: emit would-apply/reject records but do not write
   acceleration.  Validate that binding, episode IDs, and probe evidence are
   populated in both games.
3. Opt-in one-slot action: enable the adapter for exactly one manifest-listed
   participant; all other bots remain stock.  Require complete same-life
   records and zero forbidden-write counters.
4. Paired matrices: only after the one-slot lane is clean, compare selected
   enhanced policy versus its no-action baseline across UT Deck16 and Unreal
   DeathFan/Deck16 with fixed rosters, seeds, ticks, and repeated runs.

Promotion requires deterministic replay, zero state-ownership violations,
no combat/hazard regression, and a qualified improvement in bounded
movement-intent recovery.  If the action does not produce causal same-life
clearance evidence, retain it as a rejected experiment rather than widening
its authority.
