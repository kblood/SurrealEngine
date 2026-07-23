# XR UI surfaces

`Render/XRUISurfaces.h` defines provider-neutral policy for presenting the
existing UE1 menu, HUD, intro/cinematic, and loading canvases on spatial quads.
It contains no OpenXR, WebXR, graphics API, swapchain, or texture ownership.

The ordinary desktop path is unchanged. An empty `XRUISurfaceFramePolicy`
produces no surfaces, and an unconfigured `PresentationPlan` still enables every
logical layer on target slot zero. Native render backends do not need to know
about this policy until an XR provider elects to consume it.

## Surface roles and stacking

The default roles are:

| Role | Content layer | Interactive | Composition order |
| --- | --- | --- | --- |
| HUD | `UserInterface` | no | 200 |
| Intro/cinematic | `Cinematic` | no | 300 |
| Loading | `UserInterface` | no | 400 |
| Menu | `UserInterface` | yes | 500 |

World and weapon rendering precede these values. `BuildFrame()` returns visible
surfaces in back-to-front order, so the menu is always last. Every frame item
also declares `IgnoreWorldDepth`; an XR compositor/backend must therefore use an
independent surface, clear/isolate depth, or perform an equivalent overlay
composition. A scene or decorative backing quad must never depth-occlude UI,
loading, or cinematic content.

The order is composition policy, not permission to reorder UE1 script callbacks.
Desktop `PreRender`, world, weapon, and `PostRender` ordering remains intact.

## Placement and aspect

Descriptors keep both canvas pixels and physical width. Physical height is
derived as:

```text
physical height = physical width * pixel height / pixel width
```

This makes ray-to-pixel mapping use the same aspect ratio as the source canvas.
Physical values are in the provider's presentation-space units; an XR adapter
normally supplies metres.

`WorldFixed` uses the descriptor's supplied pose. `HeadRelativeOnShow` samples
the viewer pose only when the surface becomes visible and places the quad at the
configured forward distance. Later head motion does not update it, so the quad
is world-anchored rather than uncomfortable head-locked UI. `Recenter()` is the
explicit user/action boundary for moving a visible quad. Hiding and showing it
again also takes a fresh initial pose.

The gameplay HUD uses `HeadRelativeEveryFrame`, a 50-degree-wide 4:3 surface at
1.75 metres. This preserves the physically validated first native VR release's
inset and convergence while rendering `PlayerPawn.PostRender` only once. Menus
remain `HeadRelativeOnShow` and therefore retain their world-fixed-on-open
interaction behavior.

## Pointer mapping and routing

Pointer geometry and button lifecycle are deliberately separate:

1. An XR adapter converts its provider/XRCommon pointer into `XRUISurfaceRay`.
2. `HitTestXRUISurfaces()` visits interactive surfaces front-to-back and returns
   a provider-free `XRUISurfaceContact` containing UV and canvas-pixel position.
3. Desktop code can create the same contact with `MapMouseToXRUISurface()`; its
   existing pixel coordinates are preserved.
4. `XRUISurfaceInputRouter` converts contacts and button states into hover, move,
   down, up, click, and cancel events for the existing UI adapter.

The ray type is intentionally a small geometry boundary, not a tracked-device
model. A sibling XRCommon pointer type can replace its producer without changing
surface hit testing or input routing.

Pointer state is keyed by `(source kind, source id)`. The desktop mouse and each
tracked hand therefore own independent hover and press capture. A release can
only complete the same source's press. Releasing off the original surface emits
`PrimaryCancel`, never `PrimaryClick`; controller loss/session exit should call
`Cancel()` to clear both capture and hover.

Quad bounds follow the engine's half-open pixel convention: left/top are
included, while right/bottom are excluded. This guarantees a hit never maps to
`PixelWidth` or `PixelHeight` and prevents edge rays from selecting an adjacent
or out-of-range control.

## Example provider flow

```cpp
auto menu = CreateXRUISurfaceDescriptor(XRUISurfaceKind::Menu, width, height);
menu.PhysicalWidth = 1.4f;
surfacePolicy.Configure(menu);
surfacePolicy.Show(XRUISurfaceKind::Menu, viewerPose);

XRUISurfaceFrame frame = surfacePolicy.BuildFrame();
XRUISurfaceContact contact = HitTestXRUISurfaces(frame, pointerRay);
Array<XRUIPointerEvent> events = inputRouter.Update(
    XRUIPointerSource::Tracked(controllerId), contact, selectPressed);
```

The UI adapter should translate `Move`, `PrimaryDown`, `PrimaryUp`, and
`PrimaryCancel` into its cursor/button primitives. `PrimaryClick` is the
router's semantic confirmation that press and release occurred on the same
surface; adapters whose existing button-up path already activates a control
must not activate it a second time from this notification.

`XRUIRuntime` is the provider-neutral front door used by both browser and
native XR. It supplies the standard four capture descriptors, averages an XR
`ViewFamily` into the viewer pose used for show-time anchoring, and converts
already-translated left/right aim rays plus `XRControllerSnapshot` state into
independent pointer updates and exact-contact feedback. WebXR retains only
browser pose translation and WebGPU composition. Native OpenXR retains only
OpenXR pose translation and the `OpenXRUICompositionSink` target-allocation and
composition boundary.

## Backend integration gates

The policy is complete and unit-tested, but visible runtime quads still require:

- provider selection of non-zero `PresentationTarget` slots;
- backend registration and lifetime management for those target images;
- a clear/retain policy that lets UE1 `PreRender` and `PostRender` accumulate in
  the intended UI target without clearing one another;
- provider-space quad submission using frame order and `IgnoreWorldDepth`;
- an engine UI adapter that applies routed pixel movement and button/cancel
  events while retaining the current desktop mouse path;
- controller visualization/laser rendering, which belongs to the XR provider
  and must use the exact ray supplied to this hit-test seam;
- lifecycle calls that hide surfaces and cancel pointer IDs on session loss,
  controller disconnect, map transition, or menu teardown.

No backend should infer visibility from texture contents. The surface policy is
the authoritative per-frame visibility and ordering contract.

## WebXR integration status

The `integration/webxr-ui-provider` product branch supplies the first complete
consumer of this contract. WebGPU accepts multiple non-zero target bindings in
one frame, UE1 canvas replay captures HUD/menu/cinematic/loading content into
provider-owned textures, and the provider composites visible items into each
projection eye in `BuildReplayFrame()` order with no world-depth attachment.
The desktop slot-zero path is unchanged.

WebXR aim poses and eye poses share one coordinate/recenter transform. The
provider routes both hands through `UpdateRayPointer()` and uses the exact
ray/contact result for procedural controller, laser, and hit-marker geometry in
both projection eyes. Controllers and beams render before captured UI; the
opaque marker alone renders after it at the exact contact point. Selecting
brightens and thickens only that hand's visuals, with no dominant-hand policy.
It deliberately does not bind weapon/locomotion behavior. Menu activation
comes from the existing engine menu state; cinematic activation comes from the
existing video guard. `Engine::LoadMap()` and `Engine::LoadFromSaveFile()` own
an exception-safe loading scope that drives the shared loading surface for the
lifetime of a real load. Invalid `entry` requests, missing save slots, and
headless runs do not expose a surface. Normal completion and failure both hide
it, while composition order keeps an independently open menu above loading.

This is the authoritative visibility producer, not a second loading-screen
renderer. A synchronous native load still blocks `Engine::RunOneFrame`, so it
cannot submit an additional OpenXR frame while the call stack is inside the
load. Async/yielding frame owners can consume the state immediately; native XR
still needs load scheduling that preserves balanced XR frames if it is to keep
refreshing the quad throughout a long load.

File-backed browser video playback and its owner-data gates are documented in
[`WebCinematicPlayback.md`](WebCinematicPlayback.md).

The map-driven UT99/Unreal startup path uses the same noninteractive HUD capture
as gameplay, so its player/console prompt and the later health/armor/ammo HUD
share one tested angular-size contract. The interactive menu replaces that HUD
at the higher composition order. This is not the file-backed cinematic surface.
See [`MapStartupIntro.md`](MapStartupIntro.md).

## Native OpenXR integration status

Native OpenXR now consumes the same runtime policy and has deterministic
coverage. Vulkan accepts independently bound single-image slots above the
stereo slot, renders each canvas replay into a dedicated transparent
color/depth attachment set, resolves it into the provider image, and reopens
the untouched world pass. The backing attachments survive per-frame provider
unbind/rebind, while acquired provider handles do not.

`OpenXRProvider` now implements the production `OpenXRUICompositionSink`. It
owns one descriptor-sized color swapchain for each standard surface, acquires
and binds visible images before canvas replay, releases them after Vulkan
submission, converts shared anchored poses back into OpenXR LOCAL space, and
appends ordered, alpha-blended `XrCompositionLayerQuad` entries after the
projection layer. Allocation and per-frame failures stop the UI runtime instead
of leaving an invisible interactive surface; there is no null production sink
or clickable fallback.

The native owner and deterministic runtime seam are therefore implemented, but
remain hardware-unverified. A physical OpenXR pass must still establish runtime
layer-limit support, alpha and orientation correctness, anchored placement,
pointer/contact agreement, repeated lifecycle cleanup, and menu-last ordering.
Long synchronous map loads also need scheduling that can keep balanced OpenXR
frames flowing if the loading quad is expected to refresh throughout the load.
