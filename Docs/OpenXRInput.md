# Native OpenXR controller adapter

This topic originally composed the optional native OpenXR provider (`e6150bf2`, including target binding `398dbfd8`) with the provider-neutral input composition dependency (`d5fa59ca`). The native XRCommon convergence lane bases directly on `pr/openxr-input-adapter` at `8b7a2153` and applies only the XRCommon code/docs from `pr/xr-common-spaces` (`baf656f8` and `9666a488`). It does not depend on `integration/unified-engine`.

## Boundary

`OpenXRProvider::SyncInput` publishes the shared `XRControllerSnapshot` and adds raw LOCAL-space grip/aim poses to `XRSpaceSamples`. `WaitBeginAndLocate` samples the VIEW reference space into `XRSpaceSamples::Head`. Positions remain in metres and orientations remain canonical OpenXR quaternions. The provider does not choose locomotion, body yaw, handedness, weapon aim, menu interaction, or game objects.

`XRInputAdapter` converts the shared controller snapshot into ordinary engine input commands. Each hand uses its own `InputSourceId` (`XRLeft` or `XRRight`) and stable logical control numbers, so keyboard, mouse, and both controllers can contribute simultaneously. Presses are edge-triggered, holds remain composed without repeated press commands, releases remove only their physical control, and a disconnect or session stop calls `Engine::ReleaseInputSource` for that hand. `XRSessionState::AcceptsInput` is the provider-neutral focus gate; a visible but unfocused session publishes neutral values so held controls cannot stick.

`XRStartupIntroTriggerRoute` is shared with WebXR. While the engine explicitly reports the startup intro active and no menu is open, native trigger edges are also mirrored into the ordinary primary/alternate fire key events expected by scripted UE1 startup maps. A press remains owned until its matching release even if the menu opens in between, preventing a held intro trigger from becoming a synthetic menu click. Session loss balances any mirrored edge before the native input source is released.

Tracked menu input is shared rather than implemented in the controller adapter. `OpenXRViewTranslator` converts the raw aim pose into the same engine-space ray as the eye views; `XRUIInputConnector` owns independent left/right contacts, exact hit coordinates, press ownership, and cancellation. The desktop mouse path remains available after tracked capture releases.

`XRInputBindings` owns policy as configurable command strings. `ConventionalUE1()` supplies these non-game-specific defaults:

- left stick: `aStrafe` / `aBaseY`
- right stick: `aTurn` / `aUp`
- left/right trigger: `bAltFire` / `bFire`
- right squeeze: `bDuck`

Face, menu, and stick-click snapshots are exposed but deliberately unassigned by default. A game-support or UI module can bind them without adding a game-name check to OpenXR.

## Runtime actions

The provider creates one action set with left/right subaction paths and semantic input plus vibration-output actions shared by both hands. Suggested bindings cover Oculus Touch and `khr/simple_controller`. Head, grip, and aim spaces use the frame's predicted display time. A current interaction profile reports connection; `isActive` reports whether the runtime is presently routing an action. OpenXR `IDLE`, `READY`, `SYNCHRONIZED`, `VISIBLE`, `FOCUSED`, `STOPPING`, and loss/exit states map explicitly to `XRSessionState` lifecycle and focus.

## Source mapping and exclusions

Action creation, Touch/simple-controller bindings, syncing, sticks, triggers, squeeze, and buttons are extracted from `vr-m2` commits `d01b36dc` and `87d3b5aa`. Only grip/aim action-space and pose-snapshot portions are extracted from `b64a995f`.

This branch intentionally excludes body/head-yaw policy, locomotion transforms, viewmodel and weapon/fire aiming, haptic feedback policy, handedness policy, dual wield, recenter commands, and UT-specific `UObject` hooks. Aim samples now feed the shared UI pointer seam, but OpenXR/Vulkan surface allocation and composition remain the renderer blocker documented in `OpenXRProvider.md`.

## Validation

`XRInputAdapterTests` uses a fake target backed by `InputComposition` to cover press, hold, per-control release, deadzone-to-zero, per-hand disconnect, session-stop cleanup, focus loss, idempotence, and preservation of simultaneous desktop contributors. `XRCommonTests` covers the common pose, pointer-hit, and haptic-routing contracts. `OpenXRUIRuntimeTests` covers menu-topmost ordering, both controller sources, held-trigger startup/menu handoff, mouse fallback, allocation refusal, and exit/re-entry cleanup. These tests do not need an OpenXR SDK or headset and are built in both OpenXR-disabled and OpenXR-enabled configurations.

The SDK-enabled build still needs physical validation with a Vulkan-capable runtime and headset. Verify both interaction profiles, independent controller connect/disconnect, focus loss/recovery, every bound action, head/grip/aim orientation and scale, one-controller operation, controller sleep/wake, per-hand vibration, and clean session stop. Spatial use of pose snapshots and decisions about when to request haptics are separate follow-up topics.
