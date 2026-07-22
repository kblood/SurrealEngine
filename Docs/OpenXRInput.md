# Native OpenXR controller adapter

This topic composes the optional native OpenXR provider (`e6150bf2`, including target binding `398dbfd8`) with the provider-neutral input composition dependency (`d5fa59ca`). The composition merge is `3f6db9b2`; later commits on this branch are the adapter-only range.

## Boundary

`OpenXRProvider::SyncInput` publishes a plain `OpenXRInputSnapshot`. It contains independent left/right connection state, action availability, thumbstick, trigger, squeeze, semantic primary/secondary/menu/stick-click buttons, and raw LOCAL-space grip/aim poses. Positions remain in meters and orientations remain OpenXR quaternions. The provider does not choose locomotion, body yaw, handedness, weapon aim, menu interaction, or game objects.

`XRInputAdapter` converts a snapshot into ordinary engine input commands. Each hand uses its own `InputSourceId` (`XRLeft` or `XRRight`) and stable logical control numbers, so keyboard, mouse, and both controllers can contribute simultaneously. Presses are edge-triggered, holds remain composed without repeated press commands, releases remove only their physical control, and a disconnect or session stop calls `Engine::ReleaseInputSource` for that hand. Loss of action focus publishes neutral values so held controls cannot stick.

`XRInputBindings` owns policy as configurable command strings. `ConventionalUE1()` supplies these non-game-specific defaults:

- left stick: `aStrafe` / `aBaseY`
- right stick: `aTurn` / `aUp`
- left/right trigger: `bAltFire` / `bFire`
- right squeeze: `bDuck`

Face, menu, and stick-click snapshots are exposed but deliberately unassigned by default. A game-support or UI module can bind them without adding a game-name check to OpenXR.

## Runtime actions

The provider creates one action set with left/right subaction paths and semantic actions shared by both hands. Suggested bindings cover Oculus Touch and `khr/simple_controller`. Grip and aim action spaces use the frame's predicted display time, matching eye-pose sampling. A current interaction profile reports connection; `isActive` reports whether the runtime is presently routing an action.

## Source mapping and exclusions

Action creation, Touch/simple-controller bindings, syncing, sticks, triggers, squeeze, and buttons are extracted from `vr-m2` commits `d01b36dc` and `87d3b5aa`. Only grip/aim action-space and pose-snapshot portions are extracted from `b64a995f`.

This branch intentionally excludes body/head-yaw policy, locomotion transforms, viewmodel and weapon/fire aiming, pointer raycasts, menu policy, haptics, handedness policy, dual wield, recenter commands, and UT-specific `UObject` hooks.

## Validation

`XRInputAdapterTests` uses a fake target backed by `InputComposition` to cover press, hold, per-control release, deadzone-to-zero, per-hand disconnect, session-stop cleanup, idempotence, and preservation of simultaneous desktop contributors. These tests do not need an OpenXR SDK or headset.

The SDK-enabled build still needs physical validation with a Vulkan-capable runtime and headset. Verify both interaction profiles, independent controller connect/disconnect, focus loss/recovery, every bound action, grip/aim orientation and scale, one-controller operation, controller sleep/wake, and clean session stop. Haptics and spatial use of pose snapshots are separate follow-up topics.
