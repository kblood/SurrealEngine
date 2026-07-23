# Optional native OpenXR provider

This topic extracts the native OpenXR/Vulkan presentation path behind the shared frame, view-family, and presentation contracts. The separate input adapter is documented in `OpenXRInput.md`; neither topic contains UT99-specific weapon behavior, menu interaction, launcher game selection, or a second engine loop.

## Build and run

OpenXR is disabled by default. A normal build does not download or link an OpenXR loader:

```
cmake -S . -B build -DSURREAL_ENABLE_OPENXR=OFF
```

Enable the provider explicitly to fetch and build Khronos OpenXR-SDK 1.0.34:

```
cmake -S . -B build-openxr -DSURREAL_ENABLE_OPENXR=ON
```

`--probexr` checks loader/runtime/HMD availability without opening a game. `--openxr` requests OpenXR for a normal game launch. If the build lacks OpenXR, the runtime/HMD is unavailable, the selected renderer is D3D11, or Vulkan session creation fails, the provider is released and the existing desktop path continues. `GameWindow` passes the graphics requirements hook only when the selected API is Vulkan.

Dominant-hand roles are shared with WebXR and documented in
[XR_DOMINANT_HAND.md](XR_DOMINANT_HAND.md). The launcher setting controls fire,
weapon pose, and menu-pointer ownership without changing physical locomotion or
desktop input.

## Architecture

- `OpenXRProvider` owns the OpenXR instance, system, session, LOCAL and VIEW spaces, stereo swapchains, events, and balanced wait/begin/end frame calls.
- Native runtime states are translated into provider-neutral `XRSessionState`; VIEW, aim, and grip samples are published as canonical `XRSpaceSamples` in metres. Renderer-specific eye data remains in `OpenXREyeView` until the separate renderer convergence work.
- `OpenXRProvider` implements `IXRHapticSink`. It accepts validated `XRHapticRequest` values and maps the selected canonical hand to the runtime vibration-output action. The provider-neutral `XRHapticFeedbackPolicy` chooses the same fresh-gameplay-edge and exact-UI-click outcomes used by WebXR.
- `VulkanGraphicsBinding` lets an optional consumer contribute instance/device extensions and the runtime-required physical device before Vulkan creation. No OpenXR type crosses that interface.
- `OpenXRViewTranslator` converts OpenXR axes, meters, quaternions, and asymmetric fields of view into two ordinary `ViewDescription` entries. It is SDK-free and covered by a fake-pose test.
- `OpenXRViewTranslator::CreatePointerRay` applies that same axis, scale, and recenter transform to canonical `XRSpaceSamples::Aim` poses. It does not choose a hand, button, menu, or gameplay policy.
- `OpenXRUIRuntime` is an SDK-free adapter over the shared `XRUIInputConnector` and `XRUISurfaceEngineBinding`. It asks an `OpenXRUICompositionSink` to allocate the four standard non-zero targets, then brackets canvas replay with begin/finish calls. The native provider acquires, waits, and binds every visible swapchain before replay and unbinds/releases it only after Vulkan submission. Allocation or frame failure leaves the runtime stopped, so an incomplete backend cannot create invisible clickable UI.
- `Engine::RunOneFrame` still advances once, renders once through a selected `ViewFamily`, and finishes once. XR wait/action sampling happens before simulation so controller contributions apply in that update; the begun frame remains balanced through rendering.
- `PresentationTargetBinding` registers two acquired opaque images in target slot 1. The Vulkan backend accepts the two world views and copies the side-by-side mirror halves into those images before releasing them to the runtime.

The opaque image array and per-view target callbacks are shared with WebXR. Image acquisition, synchronization, interpretation, and release remain provider/backend responsibilities.

## OpenXR Vulkan extension lists

OpenXR reports its Vulkan extension lists as space-delimited, NUL-terminated buffers and includes the terminator in the reported buffer size. `ParseOpenXRExtensionList` stops at the first NUL and tokenizes only the byte count returned by the second runtime call. This is significant: passing the full buffer to a whitespace-only stream parser attaches the final NUL byte to the final extension name. VDXR places `VK_KHR_external_semaphore_win32` last, so the malformed name previously caused the runtime-selected RTX device to be rejected even though the driver supports the extension. `OpenXRExtensionsTests` preserves the trailing-NUL, embedded-NUL, and trailing-whitespace cases.

The runtime-selected `VkPhysicalDevice` remains authoritative on hybrid-GPU systems; the engine does not substitute an integrated GPU for the headset GPU. `VulkanDeviceBuilder::EvaluateDevice` is shared by ordinary device filtering and the OpenXR-required-device check so their requirements cannot drift. If selection fails, the log names the required GPU and reports each missing extension, required feature, graphics queue, desktop-mirror presentation failure, or Vulkan loader/layer handle mismatch.

## Source mapping

The lifecycle and Vulkan requirements are derived from `vr-m2` commits `22f2e344`, `31f0bd1a`, `3f3089d1`, `46126eba`, `86bd88d8`, and `30d1ae72`. Eye pose, axis/scale conversion, and asymmetric projections are derived from `82a32ca7`. The earlier debug-stereo changes in `3233cbdf` and `04a9eab6` are replaced by the shared `ViewFamily` and presentation APIs.

The presentation-provider extraction did not transplant later controller, weapon, menu, fixed render-size, or UT99 gameplay changes. The separate controller extraction and its narrower source map are documented in `OpenXRInput.md`.

## Remaining validation and policy

The SDK-enabled build and no-HMD probe can be tested without a headset, but runtime validation still requires a Vulkan-capable OpenXR runtime and physical headset. Before calling this release-ready, verify session start/stop, both eye poses and projections, swapchain image layout/format compatibility, HMD output orientation, mirror output, resize/fullscreen behavior, and clean runtime exit.

The engine-side menu/HUD/cinematic/loading policy is shared by native OpenXR and WebXR: both use the same descriptor builder, viewer anchoring, composition ordering, both-hand pointer router, exact contact, click-edge semantics, mouse fallback, and cleanup behavior. `OpenXRProvider` now owns four descriptor-sized color swapchains. It validates the runtime layer limit and Vulkan blit support, rolls back partial acquisition/binding in reverse order, and appends front-facing, alpha-blended, no-depth `XrCompositionLayerQuad` entries after projection in replay order (menu last). Shared world poses are converted back through the exact recenter transform into LOCAL space; the fixed canonical U-axis reflection is applied during the final image copy so visible pixels agree with shared hit coordinates. `VulkanRenderDevice` retains dedicated attachment sets for single-image slots 2-5, clears each surface without touching the world target, resolves multisampling, copies into the acquired provider image, and resumes the preserved world render pass. Slot 1 keeps its existing stereo behavior.

Native blocking AVI playback still uses its older private draw loop rather than `Engine::RunOneFrame`, so it does not yet drive an OpenXR frame or submit the cinematic quad while that loop is active. Real map and save loads now drive the provider-neutral loading visibility scope and clear it on success or failure, but the synchronous native load still blocks the balanced XR frame scheduler while active. The swapchain, composition role, and authoritative state therefore exist; keeping the loading quad refreshed on a headset still requires scheduling load work without unbalancing OpenXR frames. Runtime validation still needs a real Vulkan OpenXR runtime/HMD, especially for alpha, image orientation, runtime-specific swapchain formats, session-loss timing, compositor layer limits, and per-hand haptic output. Fresh gameplay Select edges and exact menu clicks now submit shared feedback; weapon-specific, damage, and game-profile outcomes remain follow-ups.
