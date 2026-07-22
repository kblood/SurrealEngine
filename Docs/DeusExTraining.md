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

The first door also opens and the player can cross it. The initial attempt used
the wrong action: Deus Ex's stock bindings use left-click (`ParseLeftClick`) for
the item in hand and right-click (`ParseRightClick`) for the highlighted world
object. A one-run diagnostic confirmed that the right-click press and release
both targeted `DeusExMover16`; the stock `Frob` path opened the mover without a
new native implementation. The diagnostic logging was then removed.

Past the door, the stock keyboard turn, look-down, look-up, and center-view
bindings make background navigation possible without raw mouse input. The
reception desk's lamp highlights and responds to right-click, the level-1
nanokey can be separately targeted and disappears through its stock pickup
path, and walking climbs the stairs to the next door. This confirms decoration
frobbing, a small-inventory pickup, view rotation, pitch, and stair stepping in
the first room.

The nanokey unlocks the door at the top of the stairs on the first frob, a
second frob opens it, and the player can cross into the next room. The Liberty
Island QuickSave that previously occupied the stock slot was first copied to
`build/test-saves/QuickSave-liberty-before-training-20260722-1335`; hashes of
all files in the ignored archive matched the source. `Save0001` was not
modified. A new Training QuickSave was then created beyond the upper door.
The stock `DeusExPlayer.QuickSave` refuses to save while a DataLink is active,
so the validation waited for the queued Jaime messages to finish before using
the stock GreyPlus binding.

A fresh `?loadgame=-1` process restored the open mover, player position, HUD,
and inventory at that checkpoint. The apparent inability to walk straight
ahead was not a load or physics defect: the restored collision geometry showed
that JC's cylinder still overlapped the exposed edge of the correctly open
sliding door. A short strafe placed the cylinder in the narrow opening, after
which normal forward movement reached the next door.

That apparent next door was also a false boundary. A fresh-map/save comparison
identified it as the non-highlighted, non-frobbable mover pair behind the
initial spawn, not the next exercise entrance. The checkpoint camera faces back
through the working tutorial door toward that starting boundary. Moving in the
opposite direction returns through the open doorway toward the nanokey and
crate-exercise route. No mover change is justified by the closed pair.

Crossing that trigger after the first reload displayed `INFOLINK NOT FOUND!!
Name = dl_start`. `Actor.ConListItems` is transient, and the save-load path did
not repeat the `ConBindEvents` calls normally made by `DeusExPlayer`,
`ScriptedPawn`, and `DeusExDecoration` during `PostPostBeginPlay`. Replaying
the whole initialization event would risk changing restored state. The loader
now rebuilds only those three class families' conversation lists after linking
the saved actors. A clean Release reload rebuilt 56 actor bindings and rendered
the intended Jaime Reyes transmission. The process remained responsive, audio
was disabled, and the game was foreground in zero of 243 samples.

## Current boundary

Reorient away from the closed starting boundary, continue from the restored
checkpoint into the crate/lockpick exercise, and validate its weapon pickup,
breakable containers, lockpick, and next door. The reception sequence and its
first post-load DataLink are validated; the next run should follow the map's
forward route before implementing another native.

The camera can be driven reliably with movement plus keyboard turn/look
bindings. Background raw-mouse look is intentionally unavailable because the
window never takes focus.

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
- [x] Open and cross the first Training door.
- [x] Frob a decoration and pick up the reception-desk nanokey.
- [x] Climb the first stairway.
- [x] Unlock, open, and cross the door at the top of the stairs.
- [x] QuickSave beyond that door and restore the checkpoint in a fresh process.
- [x] Rebuild transient conversation bindings and play `dl_start` after load.
- [ ] Exercise general inventory and DataCube text.
- [ ] Complete movement, lockpick, multitool, stealth, weapon, and demolition
  exercises.
- [ ] Complete Training and validate scripted travel back to the title/campaign
  flow.

Screenshots and logs named `build/dx-training-*` are local evidence only. They
must remain ignored because some captures contain proprietary game assets.
