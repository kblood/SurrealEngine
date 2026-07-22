# QuakeQuest screen-mode findings

The DrBeef QuakeQuest port in `webxr-port/QuakeQuest` treats menus, demos,
and the console as a distinct VR presentation mode rather than as overlays on
the stereo world.

## QuakeQuest design

`VR_UseScreenLayer()` becomes true for big-screen UI, demo playback, or the
console. The normal engine frame still renders through `Host_Frame`, including
the world and all normal 2D drawing. In screen mode, OpenXR then submits only
framebuffer 0 as an `XrCompositionLayerQuad`; it does not submit the projection
layer underneath it.

The panel is anchored from a captured player yaw and head position. Live head
orientation is no longer passed into the game camera while the screen is
active. Controller buttons are translated into ordinary engine keys: stick to
arrow keys, A to Enter, B/Menu to Escape.

The important properties are:

1. Screen mode is selected from the real UI state.
2. The engine renders a complete ordinary frame into the screen texture.
3. Quad and projection modes are mutually exclusive.
4. The UI is the final foreground content.
5. Input is translated back into the engine's existing keyboard/mouse model.

## UT99 mapping

UT99's real menu is not `PlayerPawn.bShowMenu`. Escape is intercepted by
`UTConsole`, which calls `LaunchUWindow()` and changes the console state to
`UWindow`. Its state-specific `PostRender` calls `RenderUWindow`, which paints
the real desktop and cursor.

The VR screen-mode predicate therefore covers both:

- `console->GetStateName() == "UWindow"` for the real UT99 interface;
- `PlayerPawn.bShowMenu` only as compatibility for legacy UE1 HUD menus.

When screen mode is active, UT99 VR has an exclusive quad path:

- render UWindow last into the dedicated 1024x768 target;
- submit only the quad layer;
- hold its world anchor until close or recenter;
- intersect the main-hand aim ray with that plane;
- write the hit point to `Viewport.WindowsMouseX/Y`;
- translate the main-hand trigger to `IK_LeftMouse`;
- preserve physical mouse movement/clicks through the same coordinates and
  input events.

The launcher exposes this as `run-ut99-vr.ps1 -QuadMenu` until it has passed
headset validation. Without that switch, the working projection menu remains
the fallback.
