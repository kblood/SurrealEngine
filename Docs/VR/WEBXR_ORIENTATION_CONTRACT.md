# WebGPU and WebXR vertical-orientation contract

Status: explicit renderer convention and deterministic regression coverage are
implemented; physical WebXR projection-layer validation remains required.

## Why the paths need separate conventions

The engine's desktop projection and screen-space drawing convention needs one
clip-space Y reflection when it is rendered through the WebGPU canvas. Before
commit `f067223b`, three per-draw texture-V mirrors made glyphs and textures
readable but did not correct vertex placement. The result was the exact mixed
failure reported on Brave: Deck geometry and UI ordering were upside down
while readable HUD content could remain in an apparently sensible corner.

The browser-owned WebXR/WebGPU path has a different contract. A session
created with the `webgpu` feature returns `XRView.projectionMatrix` in WebGPU's
`[0, 1]` clip-depth convention and exposes top-left WebGPU viewports. The
projection is already intended for direct WebGPU submission. Applying the
desktop canvas correction unconditionally would reflect every native eye a
second time.

Normative references: the WebXR/WebGPU Binding requires the `[0, 1]` depth
range for WebGPU-compatible sessions and demonstrates direct WebGPU viewport
submission, while WebGPU defines a top-left framebuffer/viewport origin:

- <https://immersive-web.github.io/WebXR-WebGPU-Binding/#feature-descriptor>
- <https://www.w3.org/TR/webgpu/#coordinate-systems>

The renderer therefore carries an explicit convention with every scene node:

| Convention | WGSL Y sign | Used by |
|---|---:|---|
| `EngineProjection` | `-1` | Desktop canvas and engine-manufactured diagnostic projections |
| `NativeWebGPUProjectionLayer` | `+1` | Browser `XRView` projection rendered to `XRGPUSubImage` |

The convention is state, not a matrix heuristic. In particular, the renderer
must not infer it from `projection[5]`, handedness, browser brand, viewport
shape, or whether the target happens to be an array texture.

## Propagation rules

- `FSceneNode` defaults to `EngineProjection`, preserving desktop behavior.
- `BuildSceneViews` marks decoded browser WebXR views as
  `NativeWebGPUProjectionLayer`.
- `ViewportOverride` transfers the convention into the world scene node.
- Sky, mirror, warp-zone, and other portal subframes inherit their parent.
- The first-person weapon canvas inherits the active eye's world convention.
- Captured HUD/menu commands are replayed with the active eye's convention.
- CPU projection of the head-locked HUD plane uses the matching NDC-to-WebGPU
  framebuffer mapping; native NDC `+1` maps to framebuffer Y `0`.
- Full-screen flash and other identity-matrix draws use the currently selected
  scene node's convention.

## Regression boundary

`web/test_webgpu_orientation_contract.py` guards the explicit opposite signs,
uniform-buffer alignment, shader use of the uniform rather than an
unconditional flip, the browser-view assignment, and propagation through
world, portal, weapon, and HUD paths. Desktop screenshot qualification must
also retain an asymmetric vertical oracle: on `DM-Deck16][`, the match text is
ordered `Tournament DeathMatch`, `30 frags`, then `Press Fire`, with upright
world geometry and readable glyphs.

Packed-IWER or synthetic stereo alone is not native-eye proof. The physical
Brave + VDXR test must confirm both eyes independently with an asymmetric
top/bottom marker, readable BSP/mesh textures, a bottom-right health element,
and a head-locked HUD marker. A mirrored desktop spectator canvas cannot be
used as evidence for the compositor image.

## Remaining physical gate

Record one screenshot or headset capture per eye if the runtime permits it,
plus the launch probe's native presentation phase and render-success counters.
Pass requires upright world geometry, correct marker order, correct HUD
corner, and no eye-specific inversion. Until then the native `+1` contract is
spec-driven and deterministically wired, but not physically qualified on the
Quest 3 / Brave / Virtual Desktop route.
