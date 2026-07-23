# Native OpenXR controller adapter

This topic originally composed the optional native OpenXR provider (`e6150bf2`, including target binding `398dbfd8`) with the provider-neutral input composition dependency (`d5fa59ca`). The native XRCommon convergence lane bases directly on `pr/openxr-input-adapter` at `8b7a2153` and applies only the XRCommon code/docs from `pr/xr-common-spaces` (`baf656f8` and `9666a488`). It does not depend on `integration/unified-engine`.

## Boundary

`OpenXRProvider::SyncInput` publishes the shared `XRControllerSnapshot` and adds raw LOCAL-space grip/aim poses to `XRSpaceSamples`. `WaitBeginAndLocate` samples the VIEW reference space into `XRSpaceSamples::Head`. Positions remain in metres and orientations remain canonical OpenXR quaternions. The provider does not choose locomotion, body yaw, handedness, weapon aim, menu interaction, or game objects.

`XRInputAdapter` converts the shared controller snapshot into ordinary engine input commands. Each hand uses its own `InputSourceId` (`XRLeft` or `XRRight`) and stable logical control numbers, so keyboard, mouse, and both controllers can contribute simultaneously. Presses are edge-triggered, holds remain composed without repeated press commands, releases remove only their physical control, and a disconnect or session stop calls `Engine::ReleaseInputSource` for that hand. `XRSessionState::AcceptsInput` is the provider-neutral focus gate; a visible but unfocused session publishes neutral values so held controls cannot stick. Native trigger fire is the intentional exception: the Quest-validated route synthesizes `IK_LeftMouse`/`IK_RightMouse` press and release edges through `Engine::InputEvent`, because direct `bFire`/`bAltFire` property composition bypasses UT99's console `KeyEvent` gates. The raw OpenXR menu action similarly produces an Escape pulse when gameplay owns input; the menu navigation route owns close/back while UMenu is active.

`XRTurnPolicy` keeps comfort-turn interpretation out of both OpenXR and WebXR providers. The compatibility default is right-hand smooth turn with the existing deadzone and continuous axis value. An integration/settings layer can call `XRInputAdapter::SetTurnPolicy` to change smooth scale or explicitly select snap mode. Snap mode emits one configured turn-axis pulse when the stick crosses its activation threshold, latches while held, and rearms only after crossing the lower release threshold. Switching modes, reconnecting, or holding the stick through lost focus requires a neutral sample before a snap can fire. Launcher/in-game UI still needs to persist a user choice and call this narrow API; no provider-specific setting is required.

`XRStartupIntroTriggerRoute` is shared with WebXR. While the engine explicitly reports the startup intro active and no menu is open, native trigger edges are also mirrored into the ordinary primary/alternate fire key events expected by scripted UE1 startup maps. A press remains owned until its matching release even if the menu opens in between, preventing a held intro trigger from becoming a synthetic menu click. Session loss balances any mirrored edge before the native input source is released.

Tracked menu input is shared rather than implemented in the controller adapter. `OpenXRViewTranslator` converts the raw aim pose into the same engine-space ray as the eye views; `XRUIInputConnector` owns independent left/right contacts, exact hit coordinates, press ownership, and cancellation. The desktop mouse path remains available after tracked capture releases.

`XRInputBindings` owns policy as configurable command strings. `ConventionalUE1()` supplies these non-game-specific defaults:

- left stick: `aStrafe` / `aBaseY`
- right stick: `aTurn` / `aUp`
- left/right trigger: `bAltFire` / `bFire`
- right squeeze: `bDuck`

The conventional profile assigns face buttons to jump, next weapon, and the
menu/back route. Stick-click remains available for a game-support or settings
module without adding game-name checks to OpenXR.

The integration profile assigns both controller secondary/menu buttons to a
synthetic Escape key pulse. This intentionally uses the same console
`KeyEvent` path as the keyboard: calling the `ShowMenu` exec function directly
bypasses UMenu's input/state transition in UT99. When the shared XR menu surface
is active, `XRMenuNavigationRoute` owns left-stick arrow navigation, A/Enter,
and B/Menu/Escape while `XRInputAdapter` publishes neutral gameplay controls.
The route observes held buttons while inactive so the button that opened a menu
cannot close it again on the following frame.

## Runtime actions

The provider creates one action set with left/right subaction paths for semantic buttons, axes, and vibration. Grip and aim poses use separate per-hand actions and action spaces, matching the original Quest/VDXR hardware-qualified implementation. Suggested bindings cover Oculus Touch and `khr/simple_controller`. Head, grip, and aim spaces use the frame's predicted display time. A current interaction profile reports connection; `isActive` reports whether the runtime is presently routing an action. OpenXR `IDLE`, `READY`, `SYNCHRONIZED`, `VISIBLE`, `FOCUSED`, `STOPPING`, and loss/exit states map explicitly to `XRSessionState` lifecycle and focus.

## Bounded hardware diagnostics

`XRInputDiagnosticsAccumulator` observes the same provider-neutral controller
snapshot without changing it. Native OpenXR logs session/focus transitions,
per-hand connection and profile-or-binding availability, active semantic
button/axis masks, grip/aim pose-validity transitions, and cumulative trigger,
menu/back, and nonzero-thumbstick counters. OpenXR sync, interaction-profile,
and action-state query failures use
fixed stage names with a four-event cap. Session, availability, and activity
messages have separate small caps and are otherwise emitted only on changes or
first activity, with a cumulative summary on focus loss.

The diagnostics retain no poses, interaction-profile paths, game paths, or raw
button/axis samples. They are intended to identify the failing boundary during
a physical gate: missing focus, missing runtime binding/action activity,
provider query failure, or a downstream engine mapping problem after semantic
activity has already been counted.

## Source mapping and exclusions

Action creation, Touch/simple-controller bindings, syncing, sticks, triggers, squeeze, and buttons are extracted from `vr-m2` commits `d01b36dc` and `87d3b5aa`. Only grip/aim action-space and pose-snapshot portions are extracted from `b64a995f`.

This topic branch intentionally excludes body/head-yaw policy, locomotion transforms, viewmodel and weapon/fire aiming, haptic feedback policy, handedness policy, dual wield, recenter commands, and UT-specific `UObject` hooks. Integration now composes its snapshots with the provider-neutral comfort-turn, weapon-pose, UI, and haptic-outcome layers without putting those decisions back into the OpenXR adapter.

## Validation

`XRInputAdapterTests` uses a fake target backed by `InputComposition` to cover press, hold, per-control release, deadzone-to-zero, per-hand disconnect, session-stop cleanup, focus loss, idempotence, preservation of simultaneous desktop contributors, continuous-turn compatibility, runtime smooth scaling, and snap latch/rearm/focus safety. `XRInputDiagnosticsTests` covers focus loss/recovery, held-button edge suppression, reconnect baselines, action availability changes, and cumulative trigger/menu/thumbstick counters. `XRCommonTests` covers the common pose, pointer-hit, and haptic-routing contracts. `XRHapticFeedbackPolicyTests` covers fresh gameplay edges, exact UI clicks, misses, holds, focus/controller recovery, mode handoff, and rejected transports. `OpenXRUIRuntimeTests` covers menu-topmost ordering, both controller sources, held-trigger startup/menu handoff, mouse fallback, allocation refusal, and exit/re-entry cleanup. These tests do not need an OpenXR SDK or headset and are built in both OpenXR-disabled and OpenXR-enabled configurations.

The SDK-enabled build still needs physical validation with a Vulkan-capable runtime and headset. Verify both interaction profiles, independent controller connect/disconnect, focus loss/recovery, every bound action, head/grip/aim orientation and scale, one-controller operation, controller sleep/wake, per-hand vibration, exact-hit menu feedback, and clean session stop. Weapon-specific and damage feedback remain follow-up topics.
