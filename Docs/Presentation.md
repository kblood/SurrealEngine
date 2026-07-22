# Presentation boundary

The renderer exposes provider-neutral view and presentation contracts. `ViewFamily` describes the cameras for one already-advanced game frame. Its `PresentationPlan` maps logical layers to numbered output target slots:

- `World`
- `WeaponOverlay`
- `UserInterface`
- `Cinematic`

Slot zero is always the render device's ordinary window target. Missing mappings resolve to an enabled layer on slot zero, so the default desktop path retains its original draw order and output. A presentation-aware render device may register and interpret non-zero, family-local slots without putting OpenXR, WebXR, or native graphics handles in engine code. Backends that do not implement external targets safely reject non-zero slots.

`RenderSubsystem` brackets each logical layer with `BeginPresentationLayer` and `EndPresentationLayer`. The existing callback order is unchanged: UI `PreRender`, world, weapon overlays, UI `PostRender`. Video playback uses the cinematic layer. These calls identify ownership and target selection; they do not yet prescribe compositor blending or resource lifetime.

## Multi-view diagnostic

Enter this console command while a game is running:

```
multiviewdiagnostic 1
```

The desktop window is split into two views with a small lateral camera offset. This exercises frame-once/render-many behavior on ordinary D3D11 or Vulkan without requiring an XR runtime. Disable it with `multiviewdiagnostic 0`.

## Deliberately deferred policy

Before menu and cinematic quads can be shared by native XR and WebXR, the presentation backend still needs an explicit registration API that maps non-zero slots to backend-owned images. That API belongs with the backend/provider integration because synchronization and image ownership differ between swapchains, WebGPU textures, and browser XR layers.

UI capture also needs a tested policy for clearing and accumulating `PreRender` and `PostRender` into the same UI target. The current boundary labels both as `UserInterface` and preserves desktop order, but it does not decide whether a provider should retain, clear, or composite that target between the two callbacks. Pointer coordinates, quad aspect ratio, and menu-versus-cinematic visibility likewise remain provider/UI policy rather than part of the renderer contract.
