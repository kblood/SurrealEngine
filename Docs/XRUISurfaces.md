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
one frame, UE1 canvas replay captures menu/cinematic/loading content into
provider-owned textures, and the provider composites visible items into each
projection eye in `BuildReplayFrame()` order with no world-depth attachment.
The desktop slot-zero path is unchanged.

WebXR aim poses and eye poses share one coordinate/recenter transform. The
provider routes both hands through `UpdateRayPointer()` and exports the exact
ray/contact result for laser and hit-marker visualization. It deliberately does
not choose a dominant hand or bind weapon/locomotion behavior. Menu activation
comes from the existing engine menu state; cinematic activation comes from the
existing video guard. Loading still needs an explicit engine visibility signal,
and the Emscripten target currently has no video decoder for validating intro
content.
