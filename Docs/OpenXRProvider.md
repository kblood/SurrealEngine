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

- `OpenXRProvider` owns the OpenXR instance, system, session, local space, stereo swapchains, events, and balanced wait/begin/end frame calls.
- `VulkanGraphicsBinding` lets an optional consumer contribute instance/device extensions and the runtime-required physical device before Vulkan creation. No OpenXR type crosses that interface.
- `OpenXRViewTranslator` converts OpenXR axes, meters, quaternions, and asymmetric fields of view into two ordinary `ViewDescription` entries. It is SDK-free and covered by a fake-pose test.
- `Engine::RunOneFrame` still advances once, renders once through a selected `ViewFamily`, and finishes once. XR wait/action sampling happens before simulation so controller contributions apply in that update; the begun frame remains balanced through rendering.
- `PresentationTargetBinding` registers two acquired opaque images in target slot 1. The Vulkan backend accepts the two world views and copies the side-by-side mirror halves into those images before releasing them to the runtime.

The opaque image array and per-view target callbacks are shared with WebXR. Image acquisition, synchronization, interpretation, and release remain provider/backend responsibilities.

## Source mapping

The lifecycle and Vulkan requirements are derived from `vr-m2` commits `22f2e344`, `31f0bd1a`, `3f3089d1`, `46126eba`, `86bd88d8`, and `30d1ae72`. Eye pose, axis/scale conversion, and asymmetric projections are derived from `82a32ca7`. The earlier debug-stereo changes in `3233cbdf` and `04a9eab6` are replaced by the shared `ViewFamily` and presentation APIs.

The presentation-provider extraction did not transplant later controller, weapon, menu, fixed render-size, or UT99 gameplay changes. The separate controller extraction and its narrower source map are documented in `OpenXRInput.md`.

## Remaining validation and policy

The SDK-enabled build and no-HMD probe can be tested without a headset, but runtime validation still requires a Vulkan-capable OpenXR runtime and physical headset. Before calling this release-ready, verify session start/stop, both eye poses and projections, swapchain image layout/format compatibility, HMD output orientation, mirror output, resize/fullscreen behavior, and clean runtime exit.

This skeleton copies the final side-by-side desktop composition. It does not yet render directly into array layers or isolate UI/weapon/cinematic content into independently cleared targets. Consequently menu and HUD presentation policy remains the next presentation-layer task. Controller pose/actions are layered through the separate input-composition dependency described in `OpenXRInput.md`; haptics remain out of scope.
