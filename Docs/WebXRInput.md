# WebXR input provider

Date: 2026-07-22

## Scope

Branch: `pr/webxr-input-runtime`

This layer samples browser-owned WebXR input, crosses the JavaScript/WASM
boundary through a packed replacement snapshot, and converts the decoded data
to the provider-neutral types in `XRCommon.h`. It does not choose locomotion,
weapons, dominant hand, menu clicks, UI raycasts, controller rendering, or
haptic behavior.

Keyboard/mouse and flat presentation remain active. WebXR uses the independent
`XRLeft` and `XRRight` contributors in `InputComposition`; replacing or clearing
XR input never clears another source.

## Packed ABI version 1

All integer and floating-point fields are little-endian. Structures are packed
without padding.

`PackedInputHeader` is 24 bytes:

- `Version`, `ByteSize`, `SourceCount`, and `Flags` as 32-bit unsigned values;
- `Timestamp` as a finite 64-bit float;
- `SourceCount` is zero, one, or two;
- flag bit 0 is session active and bit 1 is action focused.

Each `PackedInputSource` is 112 bytes:

- handedness and connected/aim-valid/grip-valid flags;
- pressed and touched bitsets for trigger, squeeze, primary, secondary, menu,
  and thumbstick click;
- six normalized button values and four normalized axes;
- independent `targetRaySpace` aim and `gripSpace` poses in canonical WebXR
  local space: metres, +X right, +Y up, -Z forward.

Included sources must be connected, left or right, and unique by hand. All
numbers must be finite. Button values must be in `[0, 1]`, axes in `[-1, 1]`,
and valid poses need non-zero quaternions. The decoder rejects unknown flags,
wrong sizes/versions, duplicate hands, and malformed fields without updating
the last accepted snapshot.

Every accepted packet replaces both hands atomically. A zero-source packet is
valid. Disconnect immediately submits a replacement containing the hands still
reported by the session; only the removed hand becomes disconnected. Blur
keeps reported hands connected while action focus, buttons, and axes become
neutral. Session end submits a zero-source inactive snapshot. These lifecycle
packets are applied immediately, even when no later animation frame arrives.

## Browser mapping

The provider follows the W3C
[WebXR Gamepads Module](https://www.w3.org/TR/webxr-gamepads-module-1/).
For `mapping === "xr-standard"`, indices 0, 1, and 3 map to trigger, squeeze,
and thumbstick click, and axes 0–3 are preserved. Missing, non-finite, and
out-of-range values become neutral.

The standard does not assign semantics to additional buttons. A/X and B/Y are
therefore mapped from indices 4 and 5 only for the `oculus-touch-*` profile
family. Unknown profiles keep primary, secondary, and menu neutral. The menu
field is intentionally not guessed: runtime/UA-reserved buttons may not be
exposed to content at all. Adding another family requires a tested profile
mapping, not an array-length heuristic.

Gamepads without `xr-standard` retain connection and poses but publish neutral
buttons and axes. A `visible-blurred` or hidden session remains represented but
loses action focus and publishes neutral actions.

## Shared semantic adaptation

`AdaptInputSnapshot` produces:

- canonical-metre `XRSpaceSamples` for left/right aim and grip;
- `XRControllerSnapshot` semantic controls;
- `XRSessionState` with inactive, visible, or focused state.

`ComposeInputSnapshot` accepts caller-supplied action names. Empty bindings do
nothing. This keeps policy outside the WebXR provider while using the engine's
ordinary multi-source `InputComposition`. Each update replaces only `XRLeft`
and `XRRight`; keyboard/mouse and gamepad contributors survive XR disconnect,
focus loss, and session end.

## Runtime engine input

Every accepted browser snapshot is stored, adapted, and then applied through
the ordinary engine input entry points. Button transitions become press/release
events and thumbstick axes are refreshed with each focused snapshot. The two
hands have separate `XRLeft` and `XRRight` input sources, so disconnecting one
hand releases only its contributors. Blur and session end release the XR
sources that were active; keyboard, mouse, gamepad, and synthetic contributors
are never cleared.

The runtime presents hardware controls as bindable UE1 joystick keys:

- left trigger, squeeze, primary, secondary, menu, and stick click are
  `Joy1` through `Joy6`; its stick axes are `JoyX` and `JoyY`;
- right equivalents are `Joy9` through `Joy14`; its stick axes are `JoyU` and
  `JoyV`.

This is a stable hardware layout, not a game binding table. Existing `.ini`
key bindings still decide what those keys do. This branch does not choose
locomotion, weapons, dominant hand, menu clicks, raycasts, or controller
rendering.

## Validation

Native:

```text
cmake -S . -B build-input-native -G "Visual Studio 17 2022" -A x64 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmake --build build-input-native --config Release --target WebXRInputBridgeTests WebXRInputAdapterTests WebXRInputRuntimeTests InputCompositionTests XRCommonTests --parallel 4
ctest --test-dir build-input-native -C Release --output-on-failure -R "WebXRInput|InputComposition|XRCommon"
```

Browser-provider lifecycle:

```text
node web/test_webxr_provider.mjs
```

The synthetic tests cover two independent hands, aim/grip poses, axes and all
semantic bit positions, action focus, profile-defensive mapping, duplicate and
malformed packet rejection, per-hand disconnect, blur/session-end
neutralization, held-button edge handling, shared-type adaptation, and
keyboard/gamepad/XR composition. They contain no game data. The runtime branch
also completes a no-data Emscripten link to verify the browser-to-WASM export.

## Hardware gates

Automated tests cannot establish real runtime mappings or timing. Before merge
or release, test on a physical Quest browser with WebXR/WebGPU enabled:

1. enter, exit, and re-enter an immersive session;
2. verify left/right aim and grip poses do not swap and remain metre-scaled;
3. verify trigger, squeeze, stick click/axes, A/X, and B/Y independently;
4. confirm the browser-reserved system/menu control is not misreported;
5. remove a controller battery or otherwise disconnect while holding a button
   and confirm immediate neutral state;
6. blur/hide the session while holding controls and confirm actions neutralize;
7. exit while holding controls, then use keyboard/mouse in flat mode and confirm
   desktop input was never cleared;
8. record any non-Oculus input profile before adding a profile-specific mapping.
