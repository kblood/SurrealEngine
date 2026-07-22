# XR common spaces

`SurrealEngine/XR/XRCommon.h` is the provider-neutral boundary shared by native
OpenXR and browser WebXR integrations. It contains data contracts and pure
space conversion only. It deliberately does not include provider headers,
rendering, locomotion, weapon, UI, or game-loop policy.

## Canonical provider data

Providers publish tracking in canonical XR local space:

- positions are metres;
- +X is right, +Y is up, and -Z is forward;
- orientations are quaternions in that same right-handed space;
- `XRPose::Valid` is set only when both position and orientation are usable.

`XRSpaceSamples` holds independent head, aim, and grip poses. Aim and grip
poses are indexed by the canonical `XRHand::Left` and `XRHand::Right` values.
`XRControllerSnapshot` follows the same indexing and reports semantic controls
(`Select`, `Squeeze`, face buttons, menu, and thumbstick) without choosing game
actions for them.

Providers should clear or replace the complete snapshot each sample rather
than retaining controls when a hand disconnects. Code consuming snapshots must
check pose validity and controller connection independently: a runtime can
temporarily provide one without the other.

`XRSessionState` separates lifecycle from visibility/focus. A session accepts
game input only while its lifecycle is `Running` and focus is `Focused`.
Adapters map their provider-specific lifecycle events into this state; engine
code should not infer focus from frame availability.

## Engine world conversion

Meters remain meters until an explicit `XRWorldTransform` conversion. With no
world yaw or recenter, the canonical-to-engine axis mapping is:

| Canonical XR | Engine |
| --- | --- |
| right (+X) | right (+Y) |
| up (+Y) | up (+Z) |
| forward (-Z) | forward (+X) |

`UnitsPerMeter` performs the only scale conversion. `EngineOrigin` places the
tracking origin in the engine world, and `EngineYawRadians` rotates the result
about engine +Z. Position and orientation are converted together through
`TransformXRPoseToEngine`; invalid input produces an invalid engine pose.

`MakeXRRecenterState` captures the head's horizontal position and yaw. Applying
that state resets horizontal position and heading while preserving tracked
height. Pitch and roll are not baked into recentering. If head orientation has
no usable horizontal forward direction, recentering remains invalid.

This conversion belongs at the provider-to-engine boundary. Provider adapters
must not independently swap axes, multiply by Unreal units, or apply a second
recenter transform.

## Pointer surfaces

`XRPointerHit` is an opaque hit report, not a raycaster. Engine or UI code owns
the raycast and assigns a stable, non-zero `SurfaceId`. It passes the normalized
surface UV, pixel dimensions, and hit distance to `MakeXRPointerHit`.

UI UV has a top-left origin, increasing right and down. Pixel values address
pixel centres, so UV `(1, 1)` maps to `(width - 1, height - 1)`. The default
`RejectOutside` policy returns an invalid hit for UV outside `[0, 1]`;
`ClampToSurface` clamps it to the closest addressable point. Zero surface IDs,
zero dimensions, negative distances, and non-finite values are invalid.

The common module does not decide which surface is foremost, which hand owns
the pointer, whether a trigger means click, or how a pointer is drawn. Those
remain presentation and input-binding policy above this boundary.

## Haptics

Providers implement `IXRHapticSink`. Callers submit an `XRHapticRequest` through
`RouteXRHaptic`, which validates and forwards it without altering the selected
hand or values. Amplitude is `(0, 1]`, duration must be positive, and frequency
must be non-negative; frequency zero asks the provider to use its default.
Missing sinks and rejected provider submissions return `false`.

## Adapter checklist

An OpenXR or WebXR adapter should:

1. map provider hands to `XRHand`;
2. sample head, aim, and grip poses in metres without engine conversion;
3. map provider actions to the semantic controller snapshot;
4. map provider lifecycle and focus events to `XRSessionState`;
5. convert poses once with the current `XRWorldTransform` when publishing them
   to engine consumers;
6. expose provider haptics through `IXRHapticSink`;
7. leave movement, weapons, UI ownership, rendering, and input composition to
   their existing engine-level systems.

This keeps provider integrations replaceable and lets desktop, native VR, and
WebXR builds share the same engine behavior without forcing XR code into builds
that do not use it.
