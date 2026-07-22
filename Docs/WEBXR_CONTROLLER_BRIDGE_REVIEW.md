# WebXR controller visual / compatibility bridge review

Base: `integration/unified-engine` at `dc4f2d60`.

Scope was limited to the controller proxy, laser, exact-contact marker, UI
composition, and the two WebXR presentation modes. Cinematic/demo-owned files
and behavior were not changed.

## Result

No combined-path implementation defect was found. The controller visuals are
built once from the provider-neutral pointer feedback and are projected once
per `ViewDescription`, so the same geometry reaches both direct WebGPU eye
textures and the WebGL bridge's shared stereo atlas.

For the shared atlas, both presentation images reference the same texture view.
The world renderer preserves the first eye when switching to the second eye,
and the UI compositor opens a load/store pass for each eye using that eye's
atlas viewport. The second eye's nonzero X offset therefore applies equally to
world, controller, laser, UI, and marker pixels.

Composition remains:

1. controller proxy and laser;
2. visible UI surfaces in ascending composition order, with menu last; and
3. the opaque marker centred on the exact contact point.

The input connector supplies the same `Selecting`, ray, and hit result in both
presentation modes. Session shutdown calls the native pose reset, which cancels
the UI pointer and clears active/selecting feedback. A later session can create
a fresh pointer and select normally. Flat rendering does not call the WebXR
compositor and remains unchanged.

## Added deterministic coverage

- `WebXRFrameBridgeTests` now builds a shared stereo-atlas packet and verifies
  both eye viewport offsets survive `DecodeFrame` and `BuildViewFamily`, while
  distinct asymmetric eye projections remain distinct after handedness
  conversion.
- `WebXRUIProviderTests` now verifies loading-before-menu ordering, menu as the
  selected topmost surface, controller/laser-before-UI and marker-after-menu
  order bounds, exact marker contact, selecting state, cleanup, and selection
  after re-entry.
- `test_webxr_webgl_fallback_provider.mjs` now carries a pressed right-controller
  trigger through the bridge-mode input packet, checks neutral input and frame
  cancellation on exit, and verifies a clean second entry.

The existing direct-provider test already covers two direct eye targets,
shared array textures, controller packets, disconnect/blur neutralization,
exit, re-entry, and flat fallback. The existing engine-binding test protects
menu-last replay and mouse fallback.

## Validation

- `WebXRUIProviderTests`: passed.
- `WebXRFrameBridgeTests`: passed.
- `XRUISurfaceEngineBindingTests`: passed.
- direct WebXR provider Node test: passed.
- WebGL bridge provider, projection helper, browser adapter, and release package
  Node tests: passed.
- complete no-data Emscripten `SurrealEngine` compile and link: passed.

These checks validate deterministic geometry, state, ordering, ABI, and build
integration. They do not replace a headset check for stereo convergence,
perceived controller scale, vertical orientation, occlusion, latency, or the
Quest browser's cross-API transfer timing.
