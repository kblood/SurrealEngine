# Deus Ex Training validation

This document tracks progress through the stock Training flow. It records the
normal user path as well as direct-map iterations, because loading a map alone
does not prove that the menu, mission setup, player input, scripted events, and
travel work together.

## Validation method

The first pass starts at `--url=DX`, dismisses the Ion Storm splash with
targeted window messages, and selects **Training** in the stock menu. Once that
route is known to work, shorter investigations may start `--url=00_Training`.
Every run also uses `--autostart --nosound --noactivate`, a unique log under the
ignored build directory, foreground-window polling, and `PrintWindow` captures.
See [`DeusExValidation.md`](DeusExValidation.md) for the full non-interfering
launch contract.

## Confirmed behavior

On 2026-07-22 the normal menu route performed client travel to `00_Training`,
spawned `DeusEx.Mission00`, and started the `00_TRAINING` mission state machine.
The Training start room, HUD, inventory belt, player health, compass, and first
Jaime Reyes message rendered. The process remained responsive and never became
the foreground window during the final movement and interaction checks.

The first direct-map movement pass exposed a generic actor-physics defect.
`MoveForward` reached the input axis and changed the pawn velocity, but
`TickWalking` considered a pawn moving only when both X and Y velocity were
nonzero. Cardinal movement therefore accumulated velocity without calling
`TryMove`. The same conjunction also rejected cardinal or vertical-only motion
in swimming and flying.

The fix treats either horizontal component as walking movement and any of the
three components as swimming or flying movement. A background runtime check
then held only W for five seconds: JC advanced from the spawn point to the
first door and triggered Jaime's opening message. The process was responsive,
the null audio backend remained active, and the game window was foreground in
zero of 56 samples.

## Current boundary

The first door is visibly reported as unlocked, but a targeted `ParseLeftClick`
attempt has not yet produced a confirmed open-door capture. No new error or
unimplemented-native entry was logged during that attempt. This is the next
small boundary to isolate: distinguish input/range timing from missing mover,
frob, mission-event, or conversation behavior before changing code.

Startup also logs several known missing natives that are likely to matter later
in Training:

- `LevelInfo.InitEventManager`
- `Actor.AISetEventCallback`, `AIClearEventCallback`, `AISendEvent`, and
  `AIEndEvent`
- `Pawn.AIPickRandomDestination_Deus`
- `Actor.PlayBlendAnim`
- `DumpLocation.HasLocationBeenSaved`
- `PlayerPawn.ResetKeyboard`

These entries are investigation leads, not proof that each one blocks the
current door. Implement them only after a reproducible Training behavior points
to the native and its reference semantics have been established.

## Progression checklist

- [x] Enter Training through the stock title menu.
- [x] Spawn the player and start the mission state machine.
- [x] Render the starting room, HUD, and first mission message.
- [x] Move on a single cardinal input axis.
- [ ] Open and cross the first Training door.
- [ ] Exercise object frobbing, pickup, inventory, and DataCube text.
- [ ] Complete movement, lockpick, multitool, stealth, weapon, and demolition
  exercises.
- [ ] Complete Training and validate scripted travel back to the title/campaign
  flow.

Screenshots and logs named `build/dx-training-*` are local evidence only. They
must remain ignored because some captures contain proprietary game assets.
