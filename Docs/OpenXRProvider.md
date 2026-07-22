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

## Architecture

- `OpenXRProvider` owns the OpenXR instance, system, session, LOCAL and VIEW spaces, stereo swapchains, events, and balanced wait/begin/end frame calls.
- Native runtime states are translated into provider-neutral `XRSessionState`; VIEW, aim, and grip samples are published as canonical `XRSpaceSamples` in metres. Renderer-specific eye data remains in `OpenXREyeView` until the separate renderer convergence work.
- `OpenXRProvider` implements `IXRHapticSink`. It accepts validated `XRHapticRequest` values and maps the selected canonical hand to the runtime vibration-output action without choosing gameplay feedback policy.
- `VulkanGraphicsBinding` lets an optional consumer contribute instance/device extensions and the runtime-required physical device before Vulkan creation. No OpenXR type crosses that interface.
- `OpenXRViewTranslator` converts OpenXR axes, meters, quaternions, and asymmetric fields of view into two ordinary `ViewDescription` entries. It is SDK-free and covered by a fake-pose test.
- `OpenXRViewTranslator::CreatePointerRay` applies that same axis, scale, and recenter transform to canonical `XRSpaceSamples::Aim` poses. It does not choose a hand, button, menu, or gameplay policy.
- `OpenXRUIRuntime` is an SDK-free adapter over the shared `XRUIInputConnector` and `XRUISurfaceEngineBinding`. It asks an `OpenXRUICompositionSink` to allocate the four standard non-zero targets, then supplies the shared ordered replay frame and exact-contact feedback. Allocation failure leaves the runtime stopped, so an incomplete backend cannot create invisible clickable UI.
- `Engine::RunOneFrame` still advances once, renders once through a selected `ViewFamily`, and finishes once. XR wait/action sampling happens before simulation so controller contributions apply in that update; the begun frame remains balanced through rendering.
- `PresentationTargetBinding` registers two acquired opaque images in target slot 1. The Vulkan backend accepts the two world views and copies the side-by-side mirror halves into those images before releasing them to the runtime.

The opaque image array and per-view target callbacks are shared with WebXR. Image acquisition, synchronization, interpretation, and release remain provider/backend responsibilities.

## Source mapping

The lifecycle and Vulkan requirements are derived from `vr-m2` commits `22f2e344`, `31f0bd1a`, `3f3089d1`, `46126eba`, `86bd88d8`, and `30d1ae72`. Eye pose, axis/scale conversion, and asymmetric projections are derived from `82a32ca7`. The earlier debug-stereo changes in `3233cbdf` and `04a9eab6` are replaced by the shared `ViewFamily` and presentation APIs.

The presentation-provider extraction did not transplant later controller, weapon, menu, fixed render-size, or UT99 gameplay changes. The separate controller extraction and its narrower source map are documented in `OpenXRInput.md`.

## Remaining validation and policy

The SDK-enabled build and no-HMD probe can be tested without a headset, but runtime validation still requires a Vulkan-capable OpenXR runtime and physical headset. Before calling this release-ready, verify session start/stop, both eye poses and projections, swapchain image layout/format compatibility, HMD output orientation, mirror output, resize/fullscreen behavior, and clean runtime exit.

The engine-side menu/HUD/cinematic/loading policy is no longer the missing part: native OpenXR and WebXR use the same descriptor builder, viewer anchoring, composition ordering, both-hand pointer router, exact contact, click-edge semantics, mouse fallback, and cleanup behavior. Vulkan target production is now implemented as an independently useful backend primitive. `VulkanRenderDevice` retains dedicated attachment sets for single-image slots 2-5, clears each surface without touching the world target, resolves multisampling, copies into the currently acquired provider image, and resumes the preserved world render pass. Slot 1 keeps its existing stereo behavior.

OpenXR composition remains deliberately disconnected. The next seam is a production `OpenXRUICompositionSink` that owns four descriptor-sized swapchains and performs this exact frame sequence: acquire visible surface images; bind them to slots 2-5; let the existing engine replay render them; submit Vulkan work; unbind and release the images; convert the shared world-unit poses through the same recenter transform as `OpenXRViewTranslator`; then append no-depth `XrCompositionLayerQuad` entries in replay order after the projection layer, with the menu last. Allocation, partial-acquire, session-loss, and re-entry rollback must all leave `OpenXRUIRuntime` stopped or clean. Until that exists, connecting the runtime would expose invisible interaction and is prohibited. Haptic transport exists, but no weapon, UI, or game-profile policy submits feedback yet.
