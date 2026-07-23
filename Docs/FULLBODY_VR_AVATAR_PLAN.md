# Full-body VR avatar plan

Date: 2026-07-23

## Why a literal VRIK port does not apply here

VRIK (Final IK's `VRIK.cs`/`IKSolverVR`, and every open-source relative of it —
FRIK, BeatSaberCustomAvatars, Godot-XR-Avatar, UltimateXR) is built on one
assumption: the avatar is a runtime bone skeleton, and the solver reaches its
targets by rotating bones and re-skinning the mesh every frame. That
assumption does not hold in this engine.

`UMesh` (`SurrealEngine/UObject/UMesh.h`) is Unreal Engine 1's original
vertex-animation format: `Array<vec3> Verts` is every frame's vertex
positions concatenated back to back, indexed by `FrameVerts` and a
`MeshAnimSeq` frame offset. `USkeletalMesh` does parse `RefSkeleton`/
`BoneWeights` out of the package, but at draw time
`VisibleMesh::DrawSkeletalMesh` (`Render/VisibleMesh.cpp:59-61`) just forwards
to `DrawLodMesh` — there is no bone-matrix skinning anywhere in this
codebase. `VisibleMesh::DrawMesh` (`Render/VisibleMesh.cpp:79-222`) confirms
the real mechanism: for each triangle it linearly interpolates between two
(or three, when tweening) named animation-frame vertex offsets
(`vertexOffsets[0..2]`, `VisibleMesh.cpp:92-114`, the `mix(...)` at
`VisibleMesh.cpp:181-193`), then submits three `GouraudVertex` at a time via
`frame->Device->DrawGouraudPolygon(...)` (`VisibleMesh.cpp:219`). There is no
`DrawMesh(vertexBuffer)` entry point on `RenderDevice`
(`RenderDevice/RenderDevice.h:99-126`) at all — `DrawGouraudPolygon` is the
only mesh-relevant virtual, and it takes a flat 3-vertex `GouraudVertex[]`
(`RenderDevice.h:36-42`: `Point, Light, UV, Fog`), the same struct for
D3D11, Vulkan, and WebGPU.

Also confirmed: nothing in this repo renders the local player's own body or
a controller-attached hand/viewmodel today. `VisibleActor::Process`
(`Render/VisibleActor.cpp:43-46`) suppresses drawing whenever
`isOwnedByViewport && actor->bOwnerNoSee()`, unmodified for VR — the same
rule that hides your own first-person Pawn mesh on flat desktop. (The
"viewmodel follows hand" work referenced in `vr-m2`'s history lives on that
release branch, not yet ported to this integration branch.)

Two consequences follow directly from this:

1. **A literal "bone-driven IK skeleton reposes the existing Pawn mesh"
   design is not buildable** — there is no bone skinning to drive.
2. **The render backends do not care what produces a triangle.** Any code
   that can hand `RenderDevice::DrawGouraudPolygon` a `GouraudVertex[3]` gets
   drawn identically on D3D11, Vulkan, and WebGPU, without touching any of
   the three backends. `VisibleMesh` happens to fill that buffer by
   interpolating `UMesh::Verts` — but nothing requires the input to come
   from a `UMesh` at all.

The design below builds a **separate, CPU-skinned avatar renderer** that
draws its own triangles rather than driving `UMesh`'s existing interpolation
path, and does not touch the UE1 package or VM. But the *skeleton and skin
weights it drives* are not a hand-authored asset — they are extracted
automatically from whichever player Pawn mesh is actually equipped in the
running game, so the player sees their own UT99/Unreal Gold/Deus Ex
character, not a stand-in. See "Runtime auto-rig" below for how that
extraction works and why it is possible without any manual rigging step.
This reuses only things this engine already has: the provider-neutral XR
pose contract and the collision trace used for ground probes, plus the
mesh's own existing vertex-anim data as the auto-rig's input. That keeps the
whole feature inside a self-contained module, matches this fork's
"provider-neutral contract, don't fork gameplay/VM/render backend code" rule
(`CLAUDE.md`), and — because it depends only on
`XRSpaceSamples`/`CollisionSystem`/`DrawGouraudPolygon`, all three already
shared by native OpenXR and WebXR — it runs unmodified on both without a
second implementation.

## What already exists to build on

- **Head + per-hand poses, both grip and aim, already provider-neutral.**
  `XRSpaceSamples` (`SurrealEngine/XR/XRCommon.h:69-79`) holds `XRPose Head`
  and `std::array<XRPose,2> Aim/Grip`, accessed via `AimFor(hand)`/
  `GripFor(hand)`. Both the native OpenXR path
  (`Engine.cpp:313,328`, `openXR->SyncInput(xrSpaces, xrControllers)`) and
  `WebXRInputAdapter` (`Platform/WebXR/WebXRInputAdapter.h:14-15`) populate
  the same struct. `XRWorldTransform`/`TransformXRPoseToEngine`
  (`XRCommon.h:140-152`) is the one documented place metres-to-engine-units
  and recenter conversion happens (`Docs/XRCommonSpaces.md`) — the avatar
  solver must consume already-converted engine-space poses from here, not
  reimplement axis/unit conversion itself.
- **A ground-probe trace already used by pawn/actor movement.**
  `CollisionSystem::TraceFirstHit(from, to, tracingActor, extents, flags)`
  (`Collision/TopLevel/CollisionSystem.h:35`) is the same entry point
  `UActor::Trace` calls (`UObject/UActor.cpp:1474`). A straight-down segment
  with `TraceFlags{ world = true }` is a directly reusable foot-ground
  probe.
- **A precedent for force-drawing an actor outside normal visibility rules.**
  `RenderSubsystem::DrawActor(UActor*, bool WireFrame, bool ClearZ)`
  (`Render/RenderSubsystem.h:38`) is a public escape hatch already used
  elsewhere to draw something `bOwnerNoSee` would otherwise hide. The avatar
  will not call this directly (it isn't a `UActor`), but
  `RenderSubsystem::DrawScene(const ViewFamily&)`
  (`RenderSubsystem.h:121`, the per-view scene entry point) is the right
  place to add a sibling `DrawVRAvatar(...)` call after normal actor
  drawing, following the same "runs once per stereo view" shape.
- **Frame-to-frame vertex correspondence is already guaranteed by the
  format.** `mesh->Verts[vindex + vertexOffsets[frame]]`
  (`VisibleMesh.cpp:171-180`) means vertex index `vindex` refers to *the same
  physical point on the body* in every animation frame of that mesh — that
  is what makes vertex-anim/morph-target animation work at all. This is
  precisely the input a motion-based auto-rig needs and normally has to
  establish through expensive correspondence-finding; here it's free.
- **No skeleton, glTF, or external mesh format is involved.** The auto-rig
  and CPU skinner are new code operating directly on `UMesh::Verts`/`Tris`/
  `MeshAnimSeq` data already loaded by the existing package loader — no new
  asset type, no new loader, no new package format, no VM/UObject changes.

## Architecture

```
XR/Avatar/
├── AvatarAutoRig.h/.cpp    extracts joints + skin weights from a UMesh's own anim frames
├── AvatarRigCache.h/.cpp   on-disk cache keyed by mesh identity, so extraction runs once
├── AvatarIKSolver.h/.cpp   pure math: head/hand poses + ground probes -> joint transforms
├── AvatarSkinner.h/.cpp    CPU linear-blend skin: joint transforms -> world-space GouraudVertex[]
└── AvatarRenderer.h/.cpp   owns one extracted rig, called from RenderSubsystem::DrawScene
```

`AvatarAutoRig` runs once per distinct player mesh (see "Runtime auto-rig"
below) and produces the same kind of output a hand-authored rig would have:
a bind pose, a small joint hierarchy, and per-vertex skin weights over the
*mesh's own* `Verts`/`Tris` — just discovered instead of authored.
`AvatarIKSolver` takes `const XRSpaceSamples&` (already engine-space) plus a
`CollisionSystem&` and the local player's `UActor*` (for extents/zone/
movement-state queries — swim/crouch/falling already exist per the
controller-aim work) and produces a flat array of joint transforms using
the same reference-role vocabulary VRIK-style solvers use (pelvis, spine,
chest, neck, head, shoulders, upper arm, forearm, hand per side, thigh,
calf, foot per side) — this vocabulary is just a naming convention here, not
a Unity `HumanBodyBones` dependency, and `AvatarAutoRig` is what maps the
mesh's own discovered joints onto these roles. `AvatarSkinner` then does
ordinary CPU linear-blend skinning of the mesh's own bind-pose vertices by
those joint transforms and writes directly into
`RenderSubsystem::GetTempGouraudVertexBuffer(...)` (`RenderSubsystem.h:66-71`),
submitted via the same `Device->DrawGouraudPolygon` call
`VisibleMesh::DrawMesh` already uses (`VisibleMesh.cpp:219`) — no renderer
backend changes, and no new render state per mesh beyond what `VisibleMesh`
already looks up (`Textures`, UV scale, lighting/fog per vertex).

## Runtime auto-rig: turning the player's own mesh into a skeleton

This is the part that makes "your actual UT99/Unreal Gold/Deus Ex character"
possible instead of a bolted-on stand-in avatar, and it is a solved graphics
problem, not a novel algorithm: James & Twigg, "Skinning Mesh Animations"
(SIGGRAPH 2005) extracts bones and per-vertex skin weights from a sequence
of *unrigged* animated mesh poses, using exactly the same precondition this
engine's vertex-anim format already gives for free — every frame shares
vertex indices with every other frame of the same mesh. The algorithm below
is that technique specialized to a known-biped prior (we know a Pawn mesh is
roughly humanoid, so topology doesn't need to be discovered blind), plus a
degenerate-mesh fallback:

1. **Sample poses.** Collect frame indices from the mesh's own
   `MeshAnimSeq` list, preferring sequences likely to exercise limb
   swing/bend — Walk, Run, Crouch, CrouchWalk, Turn, Fire/AltFire if
   present — plus one reference/idle frame as the bind pose (`f0`).
2. **Rigid-cluster growing.** Start with every vertex as its own cluster.
   Using the mesh's own triangle adjacency (`mesh->Tris`) as the merge
   graph, greedily merge adjacent clusters whenever the *combined* vertex
   set still admits one rigid transform (rotation + translation, fit via
   Kabsch/Procrustes against `f0`) that reconstructs all sampled frames
   within an error tolerance. Stop merging when no valid merge remains or a
   cluster-count ceiling (~24) is hit. This finds the mesh's actual rigid
   body parts — shoulders bend, so vertices across a shoulder joint refuse
   to merge; a rigid shin does not, so it collapses to one cluster — driven
   entirely by this specific mesh's own geometry and animations, not a
   guessed boundary.
3. **Topology labeling from the bind pose alone.** Order cluster centroids
   by height in `f0` into bands (feet, calves, thighs, pelvis, spine/chest,
   shoulders/neck, head), split left/right by lateral offset from the
   mesh's central axis, and assign arm chains (shoulder → upper arm →
   forearm → hand) to chest-height clusters with large lateral offset,
   ordered by distance along the limb. This is spatial-heuristic labeling
   against a known biped structure, not blind topology inference — the hard
   part (where the actual joints are) already came from step 2's motion
   data.
4. **Hierarchy and bind transforms.** Parent-child edges are assigned
   directly from the canonical biped structure once roles are labeled
   (pelvis is root; spine/chest/neck/head chain above it; shoulder→hand
   chains off chest; thigh→calf→foot chains off pelvis) — no separate
   hierarchy-discovery step is needed once step 3 has labeled every
   cluster. Store each joint's bind-pose local transform relative to its
   parent.
5. **Skin weights.** Each vertex gets full weight to its own rigid cluster's
   joint by default; smooth the boundary between two adjacent joints with a
   short distance-based falloff so elbows/knees don't facet harshly. This
   smoothing is a quality refinement, not required for a first working
   version.
6. **Fallback tier.** If a mesh has too few usable animation frames (a
   near-static prop mistakenly treated as a Pawn, or a mesh with a single
   idle-only sequence) or clustering can't find enough distinct rigid parts,
   fall back to pure spatial banding of the bind pose alone — slice the
   bounding box into the same canonical bands/left-right halves, weight
   vertices to the nearest band by distance. Lower quality, but always
   produces *something* rather than silently disabling the avatar.
7. **Cache, don't recompute.** Key the result by mesh identity (package +
   object name, or a content hash of `Verts`/`Tris`) and persist it (e.g.
   under `%LOCALAPPDATA%/SurrealEngine/AvatarRigCache/`). Extraction is a
   one-time cost the first time a given player mesh is seen in VR — not a
   per-frame or even a per-launch cost after the first run — and can run on
   a background thread during level load so it never has to block a frame.
   This is what makes it genuinely "runtime": no external tool, no manual
   step, works on whatever mesh the player happens to be using (stock
   skins, mods, Unreal Gold, Deus Ex), the very first time they use it.

Explicitly out of scope for the auto-rig: non-biped Pawns (Skaarj, vehicles,
monsters) — the topology prior in step 3 assumes two arms/two legs/one
spine. A mesh that doesn't fit that shape should be detected (e.g. cluster
count/symmetry sanity checks fail) and the avatar simply disabled for that
Pawn rather than producing a wrong rig.

### Milestones

**M1 — Auto-rig extraction + static render, no IK.** Implement
`AvatarAutoRig` per the algorithm above against a real stock UT99 player
mesh from the user's legitimate GOTY install, plus `AvatarRigCache` to
persist the result. Render the extracted rig in its own bind pose via the
new `AvatarRenderer` path (no IK driving it yet — this milestone is "did we
correctly rebuild a skeleton and can we redraw the mesh from it," not "does
it move"). Verify by screenshot in both a flat build and a headset-less
`--debugstereo` build that the auto-rigged bind-pose render is
pixel-equivalent (or near enough — CPU skinning at bind pose should
reproduce the source frame exactly) to the engine's normal vertex-anim
render of the same frame, and that native/WebXR builds are otherwise
untouched (`git diff --stat` clean outside the new files). Log the
extracted joint count/labels for at least two different stock player
meshes to sanity-check the topology heuristic generalizes beyond one mesh.

**M2 — Arm IK + spine/pelvis extrapolation.** Two-bone (upper arm/forearm)
analytic IK per arm targeting `GripFor(hand)` as the end effector, elbow
hint derived from hand orientation (the same law-of-cosines two-bone solve
VRIK-style systems use — plain trigonometry, no proprietary dependency).
Pelvis/spine position and yaw extrapolated from `Head` pose with a
configurable "pelvis follow head" weight, same as every VRIK-family
implementation surveyed (Skyrim VRIK, FRIK, Godot-XR-Avatar all do this
identically since none of them have a tracked pelvis by default either).
Verify arm reach visually against known controller positions (e.g. touch a
known world point with both hands, screenshot, confirm hand-mesh position
matches controller pose within tolerance).

**M3 — Leg placement and locomotion.** Straight-down `TraceFirstHit` probes
from each extrapolated thigh root find ground height; a small step
state machine (idle / stepping-forward / stepping-back) advances a
grounded foot toward the pelvis's horizontal projection once it exceeds a
step-distance threshold, same shape as the Godot-XR-Avatar reference cited
in the VRIK guide, chosen deliberately over a black-box solver since this
is new C++, not a ported asset. Blend to a neutral hanging-leg pose while
airborne/jumping/swimming (states this engine's controller-aim work already
tracks) since ground probes are meaningless there.

**M4 — First-person integration and calibration.** Skip submitting any
triangle belonging to the head/neck joint group when rendering from the
player's own first-person eye (standard "headless body" treatment, avoids
the avatar's own head clipping the camera) — this is a filter on which
triangles `AvatarSkinner` emits, not a renderer change. Height/arm-length
calibration: derive scale from `Head` pose height above the floor already
established by `XRRecenterState`/`XRWorldTransform`, with a manual
offset for correction (same escape hatch Godot-XR-Avatar and
BeatSaberCustomAvatars both expose, since automatic height detection is a
known failure point in every implementation surveyed). This is purely
additive — no `bOwnerNoSee` or `VisibleActor` change is needed, since the
avatar was never gated by that flag to begin with.

**M5 — Reconcile with existing hand/weapon attachment, handedness.** Once
`vr-m2`'s per-hand grip/aim pose and viewmodel-follows-hand work
(`Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md` on that branch) is ported to this
integration branch, the avatar's forearm IK target and any visible
hand/weapon-grip mesh must terminate at the same `GripFor(hand)` sample so
there is no visible gap between forearm and weapon. Left-handed mode
mirrors the avatar the same way that work already mirrors the weapon mesh.

**M6 — Confirm WebXR parity is real, not assumed.** Because the whole
solver only reads `XRSpaceSamples`/`CollisionSystem` and only writes
`GouraudVertex` through `DrawGouraudPolygon`, the same avatar code should
compile and render unchanged in the WebGPU/Emscripten build. Treat this as
a milestone to verify (screenshot from the flat-WebGPU harness), not a
free assumption — confirm no native-only headers leaked into
`XR/Avatar/`.

## Explicit non-goals for this pass

- Not porting Final IK/`IKSolverVR` itself — it is a closed-source Unity
  asset; every technique used above is the openly-documented algorithmic
  idea (two-bone IK, head-relative pelvis extrapolation, ground-probe foot
  placement, rigid-cluster auto-rigging), reimplemented from scratch in this
  engine's own math types.
- Not real GPU bone skinning in any renderer backend — CPU skinning into
  existing `GouraudVertex` submission is the whole point of fitting this
  engine's architecture instead of inventing a skinning pipeline across
  three backends.
- Not a general-purpose/arbitrary-mesh auto-rigger — the topology prior is
  a two-arm/two-leg/one-spine biped. Non-biped Pawns are detected and
  skipped, not force-fit.
- Not a hand-authored or externally generated avatar asset (no `pixal3d`
  pipeline, no glTF import) — the whole point of the auto-rig is to use the
  mesh the player is already using in-game.
- Not third-person visibility of your own avatar to other players in
  multiplayer, and not networked IK state — local cosmetic first-person
  presence only, same scope this engine's existing VR work has kept so
  far.
- Not holsters, gesture-cast, or "selfie mode" (Skyrim VRIK's consumer
  extras) until the base solver is proven in a headset — these are cheap
  additions once joint transforms are known, not part of the core feasibility
  question.

## Where this lives

This is new feature work, not an upstream bug fix, so it does not need to
satisfy `CLAUDE.md`'s upstream-contribution gate. It belongs on its own
topic branch off `integration/unified-engine` (matching how `vr-m2` and
`webxr-m1` are organized), under a new `SurrealEngine/XR/Avatar/` folder —
flat, matching this branch's current `SurrealEngine/XR/` layout rather than
the aspirational `XR/Common/` split described in
`SURREAL_ENGINE_RELEASE_AND_UNIFICATION_PLAN.md`'s target tree, since that
split has not actually been created yet on this branch.
