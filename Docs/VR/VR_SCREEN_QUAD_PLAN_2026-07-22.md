# Plan: real world-anchored VR screen for the Entry flythrough and front-end menu

Date: 2026-07-22. Supersedes the head-locked `SetVRHudFrame`-based Entry/menu
sub-rect experiments from earlier today. Builds on the findings in
`CODEX_ANALYSIS_2026-07-22.md` (read that first for the full evidence behind
each claim below).

## Problem recap

Tonight's real-headset tests showed the Entry-map flythrough and front-end
menu both rendering as pixels stamped directly into the live-head-pose
OpenXR projection layer (`SetVRHudFrame` only remaps a sub-rectangle of the
existing per-eye render target - it does not create a texture, mesh, or
compositor layer). That makes anything shown this way feel mounted to the
headset rather than sitting in the room, regardless of which camera pose
feeds it. Additionally:

- `RenderSubsystem::DrawGame()` forces `isVR = false` whenever
  `console->bNoDrawWorld()` is true, bypassing `DrawSceneVR()` and
  `PostRenderVR()` entirely and falling back to plain non-VR `PostRender()`.
  UT99's compiled front-end menu very likely sets this flag on open - this is
  the leading explanation for the menu filling the whole view with no
  containment at all.
- The 40 degree half-FOV constants used tonight are too large to read as a
  small contained screen regardless of any other bug (80 degrees total width
  occupies 70-84% of a typical per-eye FOV).
- There is no native code path that ever sets `bShowMenu()` back to false -
  that's inside compiled UnrealScript this repo has no source for. The VR
  controller's menu button calls `ExecCommand({"ShowMenu"})` directly, which
  is a different route than physical Escape (goes through
  `console->KeyEvent()` first) - they are not equivalent inputs.

## Target architecture

Render the Entry flythrough and the front-end menu **once each, monoscopically,
into an offscreen texture**, using their existing plain (non-VR) render paths
untouched, then present that texture as a **real `XrCompositionLayerQuad`**
positioned once in the session's existing LOCAL reference space when the
state activates - not recomputed from the live head pose every frame. This
replaces the sub-rect HUD trick for these two use cases. `DrawVideoFrame()`'s
existing AVI quad can migrate to the same primitive later but is out of scope
for this pass (no AVI content exists in the tested install).

## Phase 1 - quick, low-risk fixes (land first, independently useful)

These don't require the new quad-layer plumbing and make the next headset
test decisive even before Phase 2 lands.

1. **State/branch diagnostics (Codex P0).** One log line per state edge
   covering: input source (physical Escape / VR menu button), `inEntryMap`,
   `bShowMenu()`, `console->bNoDrawWorld()`, `PendingVR`/`isVR`, which
   `DrawGame()` branch was taken, `LevelInfo->Pauser()`/`m_GamePaused`,
   active map/pawn class, and `CameraRotation` next to raw
   `xrHeadYawUE`/`xrHeadPitchUE`.
2. **Decouple `bNoDrawWorld` from VR frame handling (Codex P1).** In
   `RenderSubsystem::DrawGame()` (RenderSubsystem.cpp ~42-71), keep
   `isVR = PendingVR` authoritative regardless of `bNoDrawWorld()`. When world
   drawing is suppressed, still run the VR per-eye frame setup and
   `PostRenderVR()` (just skip the 3D draw call) instead of falling back to
   flatscreen `PostRender()`. This is the fix most likely responsible for
   tonight's "no containment at all" and controller-vs-keyboard
   inconsistency.
3. **Controller menu button parity (Codex P2).** In
   `Engine::UpdateVRControllerInput()` (~Engine.cpp:2678-2679), replace the
   direct `ExecCommand({"ShowMenu"})` call with an edge-triggered synthetic
   `InputEvent(IK_Escape, IST_Press)` / `IST_Release` pair, matching the
   physical keyboard route exactly (through `console->KeyEvent()` first).
   While `bShowMenu()` is true, route the same button to another synthetic
   Escape press (toggle-close) rather than leaving no way back - this is an
   interim usability fix, not a claim about what the compiled menu "should"
   do internally.

## Phase 2 - real world-anchored quad (the actual fix for "mounted to my head")

### 2a. Offscreen mono render target

Add a small, standalone Vulkan render target (color image + framebuffer,
independent of `Textures->Scene`) sized like a normal desktop resolution
(e.g. 1024x768 for the menu's 4:3, or a 16:9 equivalent for the Entry
flythrough - reuse one shared surface size per Codex's P4 finding rather than
two different contracts). This is what the Entry scene / menu PostRender
will draw into, once per engine frame (not once per eye - avoids the
double-`PostRender`-per-frame stateful-menu risk Codex flagged).

### 2b. Render content through the existing plain (non-VR) path

- Entry flythrough: call the same single-camera render machinery
  `DrawScene()` already uses (same `Coords::Rotation(CameraRotation)`
  conversion, same `MainFrame.Process()/Draw()`), targeting the new offscreen
  surface instead of the normal window/scene target. No VR eye-pose math
  involved at all, so head tracking cannot influence it by construction. The
  existing `!inEntryMap` write-gate in `UpdateVRControllerInput()`
  (Engine.cpp:2506) must stay - it is what keeps `CameraRotation` itself
  undisturbed regardless of which code renders it.
- Menu: call the existing `CallEvent(engine->viewport->Actor(),
  EventName::PostRender, ...)` chain into a plain 2D canvas sized for this
  offscreen surface, once per frame - not per eye.

### 2c. Real `XrCompositionLayerQuad` submission

Extend `VulkanXRSession` (VulkanXRSession.h/.cpp):

- A new swapchain sized for the offscreen surface (separate from the
  existing per-eye stereo swapchains), created/destroyed alongside the
  existing ones.
- `EndFrame()` gains an optional quad layer: when the Entry/menu screen is
  active, blit the offscreen mono texture into this swapchain's current
  image, and submit an `XrCompositionLayerQuad` alongside (or instead of,
  while blanked) the existing projection layer, in the session's existing
  `appSpace` (LOCAL reference space - already created in `CreateSession()`),
  with a fixed pose computed **once** when the screen activates (see 2d) and
  held constant every frame after that, not derived from the current
  `xrLocateViews` pose.

### 2d. Fixed world anchor, established once

When `inEntryMap` first becomes true (boot) or `bShowMenu()` first becomes
true, compute a quad pose from the player's head position/forward direction
**at that moment only** (e.g. some fixed distance in front of, and at head
height relative to, the recentered play-space origin) and store it (a new
`Engine`/render-subsystem member). Do not recompute this pose on subsequent
frames while the same state remains active - this is what makes it read as a
stable screen in the room instead of a HUD. Use OpenXR's `LOCAL` reference
space semantics (already the session's `appSpace`) so it doesn't need to
track headset movement at all once placed.

### 2e. Cursor/click mapping onto the fixed quad

Update `Engine::UpdateVRMenuCursor()` to intersect the controller aim ray
with the *same* fixed anchor plane from 2d (not a per-frame head-relative
plane), and fix the coordinate-space mismatch Codex found: `WindowsMouseX/Y`
must be computed and consumed against one single shared surface size (2a),
not the desktop viewport size in one place and the raw quad render-target
size in another.

### 2f. Explicit pause/visibility invariant (Codex P6)

Once `inEntryMap && bShowMenu()`, the 3D world must not become visually
dominant again until the menu closes or a new map loads - keep this as one
clear invariant rather than two loosely-coupled flags. Simulation/audio may
keep running behind the quad (harmless, since nothing of the world is
rendered anywhere once the quad takes over); pausing it outright is optional
and out of scope unless it turns out to matter after 2a-2e land.

## Sequencing

Phase 1 is small, independently valuable, and touches `RenderSubsystem.cpp`
+ `Engine.cpp` narrowly - land it first as its own pass. Phase 2 is
substantial new engine capability (real quad-layer support does not exist in
this codebase yet) touching `VulkanXRSession.h/.cpp`, a new offscreen render
target, `RenderScene.cpp`/`RenderCanvas.cpp`, and `Engine.cpp` - land it as a
second, separately-verified pass once Phase 1's diagnostics confirm the
`bNoDrawWorld` bypass theory (or rule it out) on a real headset.
