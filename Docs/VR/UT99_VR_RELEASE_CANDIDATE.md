# UT99 VR release

Release status: headset-validated on 2026-07-22 with Quest controllers through
Virtual Desktop's OpenXR runtime.

This worktree has a release-first startup path that avoids making the Entry
flythrough the gate for playing UT99. The supplied launcher opens UT's compiled
menu immediately over a known gameplay map. The menu is presented by default
as a world-fixed OpenXR quad with tracked controller interaction. The older
head-locked stereo presentation remains available as a fallback.

## Build

From this repository root:

```powershell
cmake --build build --config Release --target SurrealEngine -- /m
```

Every configured build records the Git commit, dirty/clean state, compile
time, and executable directory in its run log.

## Graphical launcher

The release entry point is Surreal Engine's existing graphical launcher. Run
`SurrealEngine.exe` or double-click `Launch-Surreal-VR-GUI.cmd`
(`Launch-UT99-VR.cmd` is retained as an alias), open the **Folders** tab, add an
installed game root, return to **Games**, select the detected game, confirm the
prominent **Selected game** line, and press **Play**. Pressing Play from another
tab returns to Games first instead of launching a stale selection.

The Games tab has two release controls:

- **Launch Unreal / Unreal Tournament in OpenXR VR** is on by default. It
  forces the Vulkan backend required by the current OpenXR implementation.
  Turning it off preserves the ordinary desktop-engine launch path.
- **Skip intro and open the VR menu** is on by default. UT99 loads
  `DM-Deck16][`; Unreal Gold loads `Vortex2`; each then opens that game's own
  compiled menu on the VR quad. Turning it off deliberately uses the game's
  normal Entry/intro flow so the cinematic quad can be tested independently.

Other games listed by Surreal Engine remain desktop-only. Unreal Gold shares
the engine-level OpenXR, controller, quad, and input paths, but is an
experimental compatibility target until its campaign/menu flow passes a full
headset test.

## Direct-launch fallback

```powershell
.\run-ut99-vr.ps1
```

The PowerShell launcher remains a UT99-only diagnostic/recovery route. It finds
UT99 through Windows' installed-program registry, Steam's app manifest and
library folders, common GOG/Steam folders, or the classic
`C:\UnrealTournament` location. It verifies that the selected root contains
`System\UnrealTournament.exe`. Detection can be tested without opening VR:

```powershell
.\run-ut99-vr.ps1 -FindOnly
```

If automatic detection cannot find a custom or portable installation:

```powershell
.\run-ut99-vr.ps1 -GamePath 'D:\Games\Unreal Tournament'
```

The launcher works both beside the packaged `SurrealEngine.exe` and from the
source tree's `build\Release` directory. It uses `DM-Deck16][` by default so the unfinished Entry flythrough
cannot block the player. Useful options are `-LeftHanded`, `-Map 'CTF-Face'`,
and `-WeaponTuning`. `-ProjectionMenu` selects the head-locked stereo fallback
instead of the default world-fixed quad. The earlier `-QuadMenu` test switch is
still accepted but is now redundant. Each launch writes a new timestamped log
below `%LOCALAPPDATA%\SurrealEngine\UT99-VR\Logs`, keeping generated files out
of the release folder and ensuring another Surreal Engine fork cannot overwrite
the evidence from the headset run.

## Menu controls

- Left stick: move selection in the non-quad fallback menu
- A: select / Enter in the non-quad fallback menu
- B or left menu button: back / Escape
- Right stick click: recenter the headset and menu panel
- Quad main-hand pointer and trigger: point and click (owns activation while tracked)

In the default quad menu, both tracked controllers are rendered as actual
stereoscopic 3D proxy objects at their OpenXR grip poses. The main controller has a
world-space laser originating at OpenXR's separate aim pose and terminating at
the real menu-plane intersection. A compact dot on the menu marks the clickable
pixel. The main hand follows `LeftHanded=true` / `-LeftHanded`.

Commit `fc569423` aligned the beam presentation with Farantir's native VR
implementation without changing the proven menu hit test. The visual beam now
starts 4 cm in front of the aim-pose origin, uses one translucent depth-cued 3D
line instead of five opaque parallel lines, and keeps the exact-contact marker
opaque. Vulkan and D3D11 line vertices now preserve premultiplied alpha. The ray
direction, menu-plane endpoint, logical-canvas mapping, trigger hysteresis, and
physical-mouse fallback are unchanged.

This uses two compositor layers in painter's order: the opaque menu quad first,
then a source-alpha stereo projection containing only controller and laser
geometry. The world, HUD, and black background remain transparent in the second
layer, keeping the menu on top without flattening tracked objects into its
texture. These are engine-generated controller proxies rather than the runtime's
vendor-specific controller meshes.

Pointer alignment has three explicit invariants:

1. The ray is calculated after the current OpenXR hand poses are located, so
   controller, beam, and endpoint come from the same predicted-display frame.
2. Raw quad hit pixels are divided by Surreal Engine's `Canvas.uiscale` before
   being passed to `WindowsMouseX/Y`; UT then applies its independent
   `Root.GUIScale`. The on-panel dot is reconstructed from those final UWindow
   input coordinates and is therefore the hit-test ground truth.
3. Trigger edges are delivered only after `RenderUWindow` has consumed that
   frame's cursor and called `Root.MoveMouse`. A fresh release is required after
   opening the menu, with press/release hysteresis, so a held/noisy trigger
   cannot activate the first hovered option. Stick focus and A/Enter are
   disabled while the tracked quad pointer owns activation.

A real mouse remains usable; moving it temporarily gives it priority over
controller cursor updates. Visuals can be disabled independently in `User.ini`
without disabling interaction:

```ini
[Engine.VR]
MenuLaser=true
MenuControllers=true
```

For one launch, launcher switches `-NoMenuLaser` and `-NoMenuControllers`
(raw engine flags `--vr-no-menu-laser` / `--vr-no-menu-controllers`) override
those settings.

Gameplay input is suppressed while the menu is open. A failed quad acquisition
falls back to the regular stereo path rather than submitting black.

## Controller-aimed weapon coverage

Commit `9b78f2e6` closes a gap in the original weapon fire interception. That
implementation wrapped only script calls named `TraceFire` and
`ProjectileFire`; captured runs contained many `UT_Eightball` and
`UT_FlakCannon` render records but no matching old fire-intercept records, which
agrees with the headset report that those shots still followed head aim.

The engine now also wraps the local player's shared pawn aim sources,
`AdjustAim` and `AdjustToss`. It substitutes `WeaponAimRotator(MainHand())` only
while that exact script call runs and restores the previous `ViewRotation`
immediately afterward. The save stack records the exact UObject and UFunction,
so nested or unrelated calls cannot consume another call's restore. This covers
primary, alternate, automatic, held, and charged-release paths whenever they
ask the pawn to calculate aim, without making movement permanently follow the
controller. A once-per-weapon/function log line beginning
`VR aim-source intercept:` records which shared source a headset test reached.

The Release build and `--help` smoke test pass. The fake-hand runtime harness
confirmed real `AdjustAim` interception and nested save/restore behavior.
Rocket Launcher and Flak Cannon still require the final
in-headset shot-direction check before publishing; guided Redeemer steering and
any weapon that directly consumes raw `ViewRotation` without calling a shared
aim function remain separate follow-up cases.

## Headset release validation

The following release path was exercised on the target headset/runtime:

1. Startup bypassed the broken Entry flythrough and showed UT's menu on the
   world-fixed quad.
2. The tracked 3D controller proxies and main-hand laser rendered over the
   menu, not behind it.
3. The ray endpoint, visible panel dot, UWindow hover, and click target aligned
   after correcting canvas scaling and frame timing.
4. Trigger arming/hysteresis prevented held or noisy input from activating a
   menu option; selection required a deliberate release and press.
5. A full match was played, and the pause menu could be opened and controlled
   afterward with both the VR pointer and physical-mouse fallback.
6. The run log recorded the expected build, OpenXR session, quad swapchain,
   staged startup Escape, and source-alpha controller/laser overlay without an
   `xrEndFrame` failure.

## Deferred from this release

- The original UT Entry flythrough/intro is intentionally skipped. Its
  dedicated cinematic quad remains follow-up work and is not a startup gate.
- Menu hands use simple engine-generated controller proxies. Replacing these
  with UT weapon meshes is a visual enhancement for the next version.
- The projection menu remains available with `-ProjectionMenu` as a recovery
  path for runtimes that do not handle the quad composition correctly.

## Candidate archive test

Do not publish an archive that was only tested from the source tree. Build the
candidate, copy `SurrealEngine.exe`, `SurrealEngine.pk3`, `SurrealVideo.dll`,
`OpenAL32.dll`, `run-ut99-vr.ps1`, `Launch-UT99-VR.cmd`, this document, and the
license into a clean folder, then test that exact folder:

1. Run `run-ut99-vr.ps1 -FindOnly` and verify the detected installation.
2. Launch through `Launch-UT99-VR.cmd` with OBS Game Capture disabled for the
   clean shutdown test. A 2026-07-22 dump occurred after `Closing window...`
   inside OBS's injected `graphics-hook64.dll` 1.8.7.0; gameplay and the menu
   had completed normally, but an archive should not be released until exit is
   rechecked without that hook.
3. Repeat startup, menu pointer, one match, pause menu, and normal-exit checks.
4. Fire Rocket Launcher and Flak Cannon primary and alternate modes while head
   and controller point in visibly different directions. Include a held/release
   Rocket shot, and confirm `VR aim-source intercept:` names both weapons.
5. Inspect the fresh LocalAppData log for OpenXR/quad errors and retain it with
   the test notes. The archive itself must contain no commercial UT99 assets,
   user configuration, or logs.
