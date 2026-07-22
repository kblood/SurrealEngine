# WebXR actor-style HUD capture and replay plan

Status: implementation-ready design. No actor-style HUD rendering is enabled by
this document. The current renderer still suppresses these calls while a WebXR
HUD is being captured.

This plan closes the remaining M9 capture gap for `Canvas.DrawActor` and
`Canvas.DrawClippedActor` without evaluating HUD or menu script once per eye.
It also fixes the narrower disposition of `Canvas.Draw3DLine`: that native is
registered only for Unreal 1 227 in this codebase, so it is not a reachable
UT99 call and must not be counted as completed UT99 functionality merely
because a generic renderer method exists.

## 1. Audited behavior

The current WebXR HUD path in `Render/RenderCanvas.cpp` does the following:

1. It calls local-player `PostRender`, then console `PostRender`, once against
   a stable 1280x960 canvas.
2. `SubmitCanvasTile` and `SubmitCanvas2DLine` append immutable commands.
3. `DrawActor`, `DrawClippedActor`, and `Draw3DLine` increment the one aggregate
   unsupported-draw counter and return without rendering.
4. `Render/RenderScene.cpp` replays the resulting command list inside each
   already-selected eye pass. This ordering is required: selecting a finished
   WebGPU array layer again opens a clearing pass and erases the eye scene.

The three calls do not share one coordinate contract:

| Call | Desktop behavior | Required WebXR behavior |
|---|---|---|
| `Canvas.DrawActor` | Selects `MainFrame.Frame`, optionally clears depth, and renders a live actor at its world transform | Render a capture-time actor snapshot once per eye through that eye's `MainFrame`; this remains a world-space legacy overlay, not a 2D tile |
| `Canvas.DrawClippedActor` | Builds a local symmetric camera in the requested canvas rectangle, optionally clears depth, and renders the live actor | Map the capture-time logical rectangle into the current eye's HUD-plane viewport and render the capture-time actor snapshot there |
| `Canvas.Draw3DLine` | Submits its endpoints with `Canvas.Frame`; it is not a 2D-line alias | Retain a distinct 3D-line command and source-frame contract; do not flatten it to `Line2D` |

`VisibleMesh::DrawMesh` reads much more than an actor pointer. Its current read
set includes mesh, location, pre-pivot, rotation, draw scale, fatness, style,
skin/texture/multiskins, animation and blend state, owner animation state,
region, several render flags, ambient/scale glow, level/zone information, and
dynamic-light state. It also updates `actor->LightInfo`. Replaying a raw actor
pointer would therefore be neither immutable nor byte-exact.

The loaded stock UT99 scripts provide two distinct fixtures:

- `UMenu.UMenuPlayerMeshClient.Paint` and
  `UMenu.UMenuWeaponPriorityMesh.Paint` call the UWindow helper, which sets the
  location and rotation of an unlit `UMenu.MeshActor` and then calls
  `Canvas.DrawClippedActor` with `ClearZ=True`.
- older `UnrealShare` player/bot mesh menus call `Canvas.DrawActor` after
  positioning an unlit menu actor relative to the player.

The ordinary weapon/inventory `RenderOverlays` path also calls
`Canvas.DrawActor`, but WebXR already renders that path separately once per eye
as the first-person weapon pass. It is not a HUD-capture fixture and must not be
admitted as one accidentally.

There are two important boundaries outside this work:

- `Native/NCanvas.cpp` registers `Canvas.Draw3DLine` only when
  `IsUnreal1_227()` is true. Stock UT99 cannot emit it through this native.
- `UObject/UWindow.cpp` currently implements `UGC::DrawActor` only as
  `LogUnimplemented("GC.DrawActor")`. The Deus Ex-style `GC` API is a separate
  renderer contract, and routing it through the Canvas plan would make a false
  support claim. `UGC::ClearZ` also calls the device directly and would need a
  separate capture guard before any GC actor work.

## 2. Non-negotiable invariants

- Player/HUD and console/menu script are each evaluated at most once per XR
  frame.
- Per-eye replay calls no UnrealScript event, actor tick, animation function,
  collision-aware `SetLocation`, `SetRotation`, spawn, destroy, or GC pass.
- Every command represents state at the exact native draw call, not state at
  the end of `PostRender` and not state when the second eye happens to render.
- A destroyed actor, unsupported mesh/state combination, invalid rectangle, or
  non-finite value skips only that command. It must not submit to an arbitrary
  eye, clear depth, or reuse a command from the previous frame.
- Actor and canvas state are restored by RAII on normal return and exception.
  A renderer/device exception is restored and rethrown into the existing
  device-loss path; validation rejection is counted and does not throw.
- Command order relative to tiles, text-derived tiles, and lines is identical
  in both eyes.
- No command may cause the active eye/layer to be reselected.
- The explicit vertical-orientation contract remains authoritative. HUD replay
  inherits the active eye convention and never guesses from projection matrix
  contents or browser identity.
- Desktop and non-XR behavior remain byte-for-byte on the existing direct path.

## 3. Command and snapshot model

Extend `WebXRHudCommandType` with separate `ActorWorld`, `ActorClipped`, and
`Line3D` values. Do not overload `Tile` or `Line2D`.

An actor command owns a render snapshot with:

- a rooted reference to the actor and rooted references to every captured
  UObject dependency that could be detached before replay: mesh, skin,
  texture, all eight multiskins, animation owner when applicable, and any
  other pointer added to the renderer read set;
- a stable identity tuple for diagnostics: class package/name, actor name,
  level identity, and capture-frame serial (never expose memory addresses to
  browser diagnostics);
- location, pre-pivot, rotation, draw scale, fatness, style, region, animation
  sequence/frame/tween state, blend-channel state, ambient/scale glow, and all
  mesh-render flags read by `VisibleMesh`;
- `WireFrame` and `ClearZ` from the native call; and
- for clipped actors, the unscaled logical `X`, `Y`, `XB`, `YB`, capture canvas
  size, source FOV, and source UI scale.

Do not copy the entire UObject property block. It contains unrelated gameplay
state and references, and applying it during replay would be a hidden
simulation rollback. Keep one reviewed, explicit render-read structure.

Rooting only the actor is insufficient: script after the native call can
replace its mesh or skin, causing the old dependency to become unreachable.
Use either a small GC-marking snapshot holder or explicit `GCRoot` members for
all captured UObject dependencies. The command is destroyed when the next
capture starts, after both eye replays have finished.

### 3.1 Conservative first eligibility set

The first production tranche accepts the stock UT99 menu subset:

- actor, class, level, and mesh are non-null;
- `bDeleteMe` is false at capture and replay;
- all captured scalar/vector values are finite and dimensions are bounded;
- actor is unlit (`bUnlit`) or in zone zero, so replay does not depend on a
  mutable dynamic-light graph;
- `bAnimByOwner` is false;
- mesh is a proven UT99 `UMesh`/`ULodMesh` path; and
- actor is not the local pawn, its current weapon, or an inventory actor owned
  by that pawn.

Those restrictions admit the stock `UMenu.MeshActor` and the audited unlit
legacy menu actors while keeping the weapon pass single-owned. Skeletal meshes,
owner-driven animation, dynamically lit actors, and unknown mesh subclasses
remain rejected by reason until they have a snapshot extension and loaded
fixture. Do not silently render them live.

### 3.2 Exact legacy side effect

Both existing actor draw methods force `bHidden=False` for rendering and leave
the actor with `bHidden=True`. Capture suppression currently omits this
script-visible side effect. When an actor command is accepted, capture its
pre-draw state and then set `bHidden=True` exactly once before returning to
UnrealScript. A rejected/null command performs no partial mutation.

During each replay, a scoped render override directly assigns only the
captured render fields, forces hidden false, renders, and restores the actor's
then-current fields exactly. It must not call the actor's native/script
`SetLocation` or `SetRotation`. Snapshot and restore `LightInfo` as well,
because `VisibleMesh::DrawMesh` updates it even for otherwise render-only work.

## 4. Capture algorithm

At the entry of each of the three `RenderSubsystem` methods:

1. If HUD capture is inactive, execute the existing desktop path unchanged.
2. If capture is active, validate the call without touching the device.
3. Build the complete command in a temporary object. Only append after all
   roots and fields have been captured successfully.
4. For an accepted actor command, apply the one legacy `bHidden=True` result.
5. For a rejected call, increment its exact rejection reason and the legacy
   aggregate unsupported count. Never call `ClearZ` during capture.

Add a hard per-frame actor-command and total-command ceiling. On overflow,
reject later commands without reallocating indefinitely. The initial values
should be derived from loaded-menu traces; proposed safe starting gates are 64
actor commands and 4096 total HUD commands, with the observed maximum recorded
in diagnostics rather than silently raising the limit.

If capture throws while building a root/snapshot, clear the whole in-progress
command list before restoring canvas state. A partial list must never be
replayed.

## 5. Per-eye presentation

Replay remains inside `PresentWebXRHudEye` while the correct projection-layer
view is active. Iterate one ordered command list and dispatch by type.

### 5.1 World actor (`DrawActor`)

- Validate the rooted actor is still present and not `bDeleteMe` before any
  depth operation.
- Apply the capture-time render snapshot with an exception-safe scope.
- Set the device scene node to the current eye's `MainFrame.Frame`.
- Apply `ClearZ` only now, if requested.
- Call a render-only `VisibleMesh` entry point for opaque and, when requested,
  translucent passes. It must not update/tick animation or call script.
- Restore the actor snapshot target and select the current HUD canvas scene
  node before dispatching the next command.

This intentionally preserves the legacy world-space semantics. It must not be
reinterpreted as a headlocked icon simply because it was called during
`PostRender`.

### 5.2 Clipped actor (`DrawClippedActor`)

Map the capture logical rectangle into the already-calculated eye HUD viewport:

```text
left   = hud.left + logical.XB       * hud.width  / captureCanvas.width
top    = hud.top  + logical.YB       * hud.height / captureCanvas.height
right  = hud.left + (XB + X)         * hud.width  / captureCanvas.width
bottom = hud.top  + (YB + Y)         * hud.height / captureCanvas.height
```

Intersect it with the HUD viewport and active eye viewport. Use one documented
rounding rule (`floor` near edges, `ceil` far edges), require a positive result,
and reject overflow/non-finite input. Do not clamp an entirely off-plane actor
into an unrelated visible corner.

Construct the local preview scene node with the clipped rectangle, captured
FOV, identity world-to-view, and the existing `Coords::ViewToRenderDev()`
object transform. Inherit the parent HUD replay's explicit clip-space Y
convention and verify it with an asymmetric top/bottom mesh fixture. Apply the
snapshot and render through the same render-only path. This yields a
headlocked preview aligned with the finite-depth HUD rectangle; it does not
pretend that the preview has physically correct internal stereo depth.

`ClearZ` currently clears the selected eye's full depth attachment on WebGPU,
not merely the sub-viewport. Preserve that legacy ordering, but invoke it only
after eligibility and only in the active eye. Record it separately so physical
testing can catch later-order occlusion regressions.

### 5.3 3D line

Capture color, flags, endpoints, and the source logical scene-node parameters.
Reject non-finite endpoints. Replay with a dedicated local node mapped into
the HUD viewport; do not call `Draw2DLine` and do not discard depth-cue flags.

For the UT99 artifact, the acceptance condition is instead explicit
unreachability: loaded UT99 frames report zero `Line3D` capture attempts, and
the native-registration test proves native 1751 is gated by
`IsUnreal1_227()`. Production enablement of this command waits for an Unreal 1
227 loaded fixture because UT99 cannot validate its semantics. Until then a
227 call remains fail-closed and counted by type.

## 6. Diagnostics

Keep the existing aggregate fields for compatibility and add cumulative plus
last-frame counters for:

- attempts, accepted commands, and per-eye presentations for `ActorWorld`,
  `ActorClipped`, and `Line3D` independently;
- actor rejection reasons: null, deleted, missing dependency, non-finite,
  forbidden local pawn/weapon/inventory, owner animation, dynamic lighting,
  unsupported mesh type, invalid rectangle, command limit, and stale level;
- replay skips after capture, state-restore checks, render exceptions, and
  depth clears;
- expected actor presentations (`accepted actor commands * active views`) and
  completed actor presentations; and
- a deterministic command-type/order digest, with object names omitted from
  the hash to avoid path/content leakage.

`UnsupportedDraws` remains the sum of rejected/unimplemented calls, not the
number of successfully captured actor commands. Browser exports expose only
numeric counters, finite dimensions, schema version, and the command digest.
They must not expose UObject addresses, local installation paths, mesh data,
or imported inventory.

On session exit, tracking loss, device loss, map change, disabled HUD, absent
HUD/console, and zero-view frames, clear command roots and reset last-frame
expectations. Cumulative counters may remain for diagnostics.

## 7. Deterministic tests

Extend the native HUD self-test without requiring retail data. It must prove:

1. one capture update and two replay plans for a mixed ordered stream
   `Tile, ActorWorld, Line2D, ActorClipped, Tile`;
2. exact command order in both eyes;
3. asymmetric eye viewport mapping and clipped-rectangle rounding;
4. active-eye clip-space convention propagation and an asymmetric vertical
   orientation oracle;
5. no call to script/tick/event hooks during replay;
6. two different snapshots of the same actor in one capture retain their own
   location, rotation, animation frame, skin, and rectangle;
7. actor fields and `LightInfo` restore byte-exactly after each eye, including
   an injected render exception;
8. `bHidden` changes once at capture and does not leak from either replay;
9. null/deleted/non-finite/dynamically-lit/owner-animated/local-weapon and
   oversized-command cases reject before `ClearZ` or device submission;
10. disabled/absent/UI-only paths clear roots and never replay stale actors;
11. `Line3D` is distinct from `Line2D`, preserves flags/endpoints, and is
    production-disabled for the UT99 launch profile; and
12. desktop capture-inactive calls still take the original direct path.

Add a small render-dispatch seam or fake device so tests count scene-node
selection, depth clears, opaque/translucent calls, and restores without a GPU.
Do not make a self-test pass by incrementing the same production diagnostics it
is supposed to validate.

## 8. Loaded UT99 fixtures

Automation must use user-imported retail packages and be explicitly opt-in. It
must not run during normal boot.

### 8.1 Stock clipped actor

Open the real `UMenu.UMenuPlayerWindow` through the existing UWindow root (the
stock `UMenuOptionsMenu.PlayerSetup()` path), allow its real
`UMenuPlayerMeshClient` to create/configure `UMenu.MeshActor`, and render two
packed IWER eyes. Require:

- player and console `PostRender` each remain at most one;
- at least one accepted `ActorClipped` command and exactly two presentations
  per accepted command;
- zero actor rejection and restore-failure counters;
- the actor class is the expected loaded UMenu class in native-only fixture
  logging, without exporting its pointer;
- different left/right eye buffers, no WebGPU validation error, upright
  asymmetric marker, and a non-background mesh region inside the expected
  window rectangle; and
- a second frame changes animation/rotation at most once, not once per eye.

Close the real window through its stock close path and prove the rooted command
is gone before the next frame. A destroyed `MeshActor` must be rejected if a
test deliberately destroys it after capture but before replay.

### 8.2 Stock world actor

Use a real loaded `UnrealShare` player/bot mesh menu that executes its own
`Canvas.DrawActor(Self, false)` path. Require one captured snapshot, two eye
renders, nonzero stereo difference in the actor region, no extra weapon pass,
and exact actor transform/hidden restoration. If that menu is not reachable in
the selected UT GOTY configuration, report the fixture as unavailable rather
than substituting the current weapon or a synthetic actor.

### 8.3 Browser and physical gates

The full Playwright/IWER smoke suite must retain world, weapon, HUD, menu,
input, repeat-entry, device-loss, and zero-WebGPU-error gates. IWER proves
dispatch and pixels, not comfort or headset fusion.

On Quest 3 through the supported Brave/VDXR route, inspect Player Setup and one
legacy actor menu in both eyes. Pass requires upright actors, no eye-specific
inversion, no double-speed animation, stable occlusion/order after `ClearZ`, a
usable surrounding menu, and no actor left floating after the window closes.

## 9. Milestones and commits

### AH1 — Type-safe command model and rejection telemetry

- Add command variants, snapshot schema/version, per-type counters, strict
  validators, ceilings, and clear/reset behavior.
- Keep every actor/3D-line call fail-closed.
- Land deterministic classifier, lifetime, invalid-input, and ordering tests.

Exit: no behavior claim changes; unsupported calls are attributable by exact
reason and stale roots cannot cross a frame/map/session boundary.

### AH2 — Unlit clipped-actor replay

- Implement rooted explicit snapshots and scoped direct-field overrides.
- Map logical rectangles into the per-eye HUD viewport.
- Add fake-device tests, native/Wasm builds, and the loaded Player Setup
  fixture.

Exit: the stock UMenu player and weapon-priority previews render once per eye
from one script evaluation with exact restoration and zero WebGPU errors.

### AH3 — Unlit world-actor replay

- Dispatch `DrawActor` through each eye's `MainFrame` while preserving command
  order and the single-owned weapon path.
- Add the real legacy menu fixture and local pawn/weapon rejection cases.

Exit: an available stock legacy menu actor renders in stereo without an extra
weapon/HUD script call. If no such menu is shipped/reachable, this remains
implemented but not loaded-qualified and is reported that way.

### AH4 — Compatibility expansion

- Add owner-animation, skeletal-mesh, or dynamic-light snapshots only one
  capability at a time, each behind a default-off eligibility bit and a loaded
  fixture.
- Prefer a snapshot-specific lighting input over temporarily mutating light
  actors.

Exit: each newly enabled capability has deterministic state restoration,
loaded pixels, and no fallback to live end-of-frame state.

### AH5 — 227-only 3D-line qualification

- Implement the distinct command and local-node replay.
- Add an Unreal 1 227 loaded fixture before enabling it for that launch
  profile.

Exit: UT99 continues to report the native as unreachable; a separate 227 test
proves depth flags, orientation, and endpoint semantics. This milestone is not
required to declare the UT99 actor-menu gap closed.

### AH6 — Hardware closeout and documentation

- Run packed automation, three enter/exit cycles, both-eye physical checks,
  and a 30-minute menu/readability session.
- Record browser/runtime/headset versions, exact fixture classes, counter
  deltas, screenshots/captures where available, and remaining rejected
  capabilities in the master plan.

Exit: both-eye actor menus are readable and stable on hardware, all unsupported
counts are explained, and the release notes distinguish UT99 support from
unqualified cross-title Canvas/GC features.

## 10. Definition of done for UT99 M9 actor draws

The UT99 portion is complete only when real Player Setup clipped actors and an
available real legacy world actor pass the loaded and physical gates; script
counts remain one per frame; every eligible command presents once per active
eye; state restores exactly; rejected commands cannot clear depth or submit;
the first-person weapon remains single-owned; and diagnostics report zero
unexplained unsupported draws on the representative map/menu matrix.

`Canvas.Draw3DLine` and `GC.DrawActor` are not hidden inside that claim. The
former is a 227-only qualification, and the latter remains a separate
unimplemented UGC project.
