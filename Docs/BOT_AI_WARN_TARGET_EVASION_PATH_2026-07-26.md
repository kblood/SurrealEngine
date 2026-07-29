# Retail WarnTarget evasion path — 2026-07-26

## Finding

The immediate bot response to a weapon warning is a script-driven dodge
impulse, not a latent navigation command. A `MoveTo` or `MoveToward` observer
would therefore miss the path that can plausibly affect water entry or
short-term survival.

This finding comes from read-only UELib inspection of the retail packages:

- UT436 `BotPack.u`, SHA-1
  `b1365300c9b4111d30159f64e57628257fffd172`.
- Unreal Gold 226b `UnrealShare.u`, SHA-1
  `2bd91ab92544dab22d353d3b64e47581090e8c14`.

## Per-game flow

```text
weapon Timer
  -> native Pawn.PickTarget (531)
  -> Pawn(target).WarnTarget(shooter, projectile speed, fire direction)
  -> Bot(s).TryToDuck(duck direction, reversed)
  -> SetFall + direct Velocity assignment + dodge + GotoState(FallingState, Ducking)
```

UT436's `ShockRifle.Timer` and `SniperRifle.Timer` use that path when native
`PickTarget` returns a pawn. Unreal Gold's `ASMD.Timer` has the corresponding
path. In both adapters `WarnTarget(Pawn,float,vector)` is void and delegates
to `TryToDuck(vector,bool)` after its stock eligibility checks.

`TryToDuck` performs the immediate maneuver itself: it evaluates lateral
space, calls `SetFall`, writes a dodge velocity (including vertical lift),
plays the dodge, sleeps, and transitions to `FallingState/Ducking`. It does
not invoke `MoveTo` or `MoveToward`.

## Instrumentation consequence

The existing default-off `WarnTarget` observer correctly retains the nested
warning/duck relationship, but a next observer must capture the pre/post
`TryToDuck` outcome: requested direction/reversal, velocity, physics, state,
and latent action, with its existing warning sequence/life/tick identity.
That is behavior-neutral evidence only. It must not write velocity, override
the dodge, or equate a temporal warning with a later hazard death.

Native `MoveTo(vector, optional float)` and `MoveToward(Actor, optional
float)` remain useful navigation observations, but are a later and separate
path. They cannot certify the immediate warning response without an explicit,
durable cross-state handoff token.
