# WebGL 2 / `XRWebGLLayer` renderer investigation

Date: 2026-07-22

The investigation branch did **not** contain a WebGL renderer. The product
integration branch now implements the narrower WebGPU-canvas-to-WebGL
`XRWebGLLayer` bridge described below; see
`docs/WEBXR_WEBGL_BRIDGE_HANDOFF.md`. This still does not claim production Quest
support until its physical timing and correctness gates pass, and it is not a
full WebGL engine renderer.

## Current recommendation

Prototype the **WebGPU stereo-atlas -> WebGL 2 `XRWebGLLayer` compositor
bridge first**. It reuses the existing Surreal WebGPU renderer and is much
smaller than reimplementing every UE1 draw and texture path in WebGL 2. Keep a
full WebGL 2 `RenderDevice` as the slower but more predictable fallback if the
bridge misses the Quest frame budget or proves unreliable across browser
updates.

This recommendation is conditional on physical Quest measurements. The web
standards permit a canvas as a WebGL texture source, but provide no zero-copy
guarantee between WebGPU and WebGL.

## Branch and dependency

- Branch: `pr/web-renderer-selection`
- Exact base: `pr/web-platform-foundation` at
  `88980d6234d0d897e5c01f78cfc4b70f618c90c3`
- `integration/unified-engine` is not an ancestor of this branch.
- This branch does not modify `RenderDevice/WebGPU`, the browser launcher, or
  any WebXR provider.

The foundation is the smallest suitable base because it introduced the
Emscripten platform and WebGPU renderer. The new selection code can therefore
be reviewed before the WebXR provider and browser product shell.

## Evidence

### No reusable Surreal OpenGL renderer was found

The following local sources were searched for `OpenGLRenderDevice`,
`OpenGLDrv`, `WebGL2`, `XRWebGLLayer`, Emscripten WebGL context creation, and
common GL entry points:

- all Surreal worktrees and all refs in the shared Surreal git object store;
- the `dpjudas/SurrealEngine` remote refs currently present locally;
- the Quest/VR projects under `C:/Devstuff/QuestGames`;
- the local UE1 SDK/reference headers; and
- the QuakeQuest/DarkPlaces WebXR port and its prior-art mirrors.

Surreal has `RenderAPI::OpenGL` window-system plumbing in SurrealWidgets, but no
class implementing SurrealEngine's `RenderDevice` interface with OpenGL. The
SDK reference tree contains OpenGLDrv headers, not a redistributable renderer
implementation. A primary-source GitHub search likewise found no OpenGL render
device in `dpjudas/SurrealEngine`.

### The local Quake renderer is reference material, not source material

`webxr-port/src/darkplaces` has a working GLES/WebGL rendering path, and
`webxr-port/src/web-host/webxr_bridge.c` demonstrates the important WebXR
presentation sequence:

1. create or make the WebGL context XR-compatible;
2. create an `XRWebGLLayer`;
3. bind the layer framebuffer inside the XR animation-frame callback;
4. use `getViewport(view)` for each eye; and
5. restore the XR framebuffer after engine-owned offscreen passes.

That implementation is GPL-2.0. SurrealEngine's core is distributed under the
zlib license in `LICENSE.md`. Copying the DarkPlaces renderer into Surreal
would impose incompatible upstream/distribution expectations, so no DarkPlaces
renderer code was reused here. The architectural observations above are also
specified by the WebXR standard.

Primary references:

- WebXR Device API, `XRWebGLLayer` and its framebuffer/viewport contract:
  <https://immersive-web.github.io/webxr/#xrwebgllayer-interface>
- Emscripten WebGL optimization guidance, including WebGL 2 build/context
  requirements:
  <https://emscripten.org/docs/optimizing/Optimizing-WebGL.html>
- Emscripten HTML5/WebGL context API:
  <https://emscripten.org/docs/api_reference/html5.h.html>

### A renderer is substantially larger than an XR layer adapter

The current WebGPU backend is 17 files and about 2,048 source lines. It already
encodes Surreal-specific behavior that a WebGL renderer must reproduce:

- UE1 complex surfaces, Gouraud polygons, tiles, lines, and points;
- polygon flag combinations and blend/depth/cull state;
- paletted textures, mipmaps, light maps, fog maps, texture updates, sampler
  state, and cache lifetime;
- scene buffers, hit testing, readback, flash/gamma, bloom/present behavior;
- dynamic vertex/index upload and draw batching; and
- flat-window resize and presentation.

An `XRWebGLLayer` provider only supplies the target framebuffer and per-view
viewport. It does not implement those renderer responsibilities.

### Quest Browser evidence favors a compatibility bridge investigation

Recent Quest 3/3S reports continue to say that WebGPU itself works while the
WebXR-WebGPU binding does not work in production Quest Browser. A Meta employee
said the binding was in the backlog with no firm date. A developer reported a
working WebGPU-to-WebGL workaround costing about 4 ms in their application.
Another 2026 Quest developer reported finding no fast path and described the
transfer as GPU-to-CPU-to-GPU. These are field reports, not conformance tests,
so they establish priority and risk rather than guaranteed behavior:

- <https://communityforums.atmeta.com/discussions/WebXRDevelopment/webxr-webgpu-bindings-for-dynamic-gaussian-splatting/1255357/>
- <https://communityforums.atmeta.com/discussions/Questions_Discussions/webgpu-compute-into-webxr-on-quest/1360706/>
- <https://communityforums.atmeta.com/discussions/Questions_Discussions/webxr--webgpu-binding-browser-support/1369174>

## Architecture comparison

| Concern | Full WebGL 2 `RenderDevice` | WebGPU atlas -> WebGL compositor |
| --- | --- | --- |
| UE1 rendering | Reimplement all primitives, flags, shaders, textures, batching, hit/readback, and postprocessing | Reuse the existing WebGPU backend unchanged |
| XR target | Render directly into `XRWebGLLayer.framebuffer` | Upload one stereo atlas into a WebGL texture, then draw viewport quads into the XR framebuffer |
| New engine code | Roughly 2,350-4,250 lines across 16-26 files | Estimated 500-950 lines across 4-7 files, mostly provider/presentation glue |
| Per-frame transfer | None beyond ordinary WebGL rendering | One full stereo color-atlas cross-API transfer; may be GPU->CPU->GPU |
| Principal risk | Large correctness/maintenance surface | Bandwidth, synchronization, frame latency, orientation, browser lifecycle |
| Flat desktop web | Native WebGL path | Existing WebGPU path remains native; bridge activates only for XR fallback |
| Time to useful headset result | Several weeks | About 1-2 weeks to a measurable prototype |

### Proposed compositor flow

1. Create an XR-compatible WebGL 2 context on a dedicated presentation canvas
   and use it to construct `XRWebGLLayer`.
2. Size the existing WebGPU canvas to the layer framebuffer dimensions. Render
   both views into a stereo atlas using each `XRView`'s actual viewport; never
   assume side-by-side packing.
3. Convert each WebGL-style WebXR projection matrix before it enters the
   existing frame ABI. WebGL depth is `[-1, 1]`; WebGPU depth is `[0, 1]`:
   `z' = 0.5*z + 0.5*w`. In column-major storage this replaces matrix row 2
   with `0.5 * (row2 + row3)` and preserves asymmetric X/Y terms.
4. In the same XR animation-frame callback, after submitting both WebGPU eyes,
   call WebGL `texSubImage2D` once with the WebGPU canvas as the
   `HTMLCanvasElement` source.
5. Bind `XRWebGLLayer.framebuffer` and draw the atlas regions as two viewport
   quads. Make Y orientation explicit in UVs or `UNPACK_FLIP_Y_WEBGL`; do not
   infer it from the current flat WebGPU shader correction.
6. Instrument CPU submission time, blocking handoff time, optional WebGL timer
   queries, missed XR frames, and source-to-display latency. Treat a browser
   update as a reason to rerun the probe.

There is no standards-level way to share a `GPUTexture` directly with a
`WebGLTexture`. The canvas `TexImageSource` path is legal, but whether it stays
on-GPU is an implementation detail. At an atlas size of `W * H`, one RGBA8
image is `4*W*H` bytes per frame; a CPU-mediated readback plus upload moves at
least twice that logical payload. Record the actual Quest layer dimensions and
refresh rate when calculating the bandwidth budget.

## Implemented browser bridge probe

`web/probes/webgpu_webgl_bridge_probe.html` deliberately does not create an XR
session. It isolates the cross-API primitive:

- WebGPU renders a red/green two-eye atlas to an `HTMLCanvasElement`;
- WebGL 2 accepts that canvas in `texSubImage2D`, draws one textured quad, and
  verifies both eye colors with `readPixels`;
- 10 warm-up frames and 60 measured animation frames force a fresh WebGPU
  submission per sample; and
- each measurement includes cross-API synchronization, upload, draw, and
  `gl.finish`, not merely JavaScript command submission.

Desktop Chrome on the development machine passed at 512x256 with a 0.055 ms
median and 0.085 ms p95. This only proves API and pixel correctness on that
machine. The tiny atlas, desktop GPU/driver, and absence of an immersive
session make those numbers unsuitable for predicting Quest performance.

An important lifecycle result: awaiting `device.queue.onSubmittedWorkDone()`
before sampling the WebGPU canvas produced black pixels in this probe, while
submitting and sampling in the same animation-frame task passed. The production
bridge must keep render submission and canvas upload in the same XR callback
and add a regression test for this behavior.

`ClipSpaceConversion` and its asymmetric-matrix test implement and verify the
projection depth conversion independently of any provider.

## Implemented selection seam

`RenderDeviceSelection` is now the single mapping between a persisted/CLI
renderer choice, its SurrealWidgets `RenderAPI`, and whether an implementation
is compiled for the current platform.

- `webgpu` maps to `RenderAPI::WebGPU` and is compiled only for Emscripten.
- `webgl2` (with `opengl` as an alias) maps to the existing
  `RenderAPI::OpenGL` slot but is explicitly marked unavailable.
- Vulkan, D3D11, D3D12, and Null availability is described in the same table.
- `GameWindow` rejects an unavailable renderer with its specific reason before
  constructing a window or render device.
- Emscripten `--render=` uses the table. Asking for `--render=webgl2` produces
  an explicit "does not currently contain" failure instead of silently using
  Null or WebGPU.
- Desktop defaults remain unchanged. No new launcher option is exposed.

`RenderDeviceSelectionTests` locks down the mapping, alias, Null availability,
unknown-name behavior, and the requirement that WebGL 2 remain unadvertised
until a real device is compiled.

## Full WebGL renderer milestones (inference, contingency path)

These are engineering estimates based on the current 2,048-line WebGPU backend
and the narrower WebGL 2 API. They are not evidence of completed support.

| Milestone | Deliverable | Estimate |
| --- | --- | --- |
| 1. Context and device skeleton | Emscripten WebGL 2 context, context-loss handling, state cache, clear/resize, flat canvas smoke frame | 3-5 files, 300-550 lines, 3-5 days |
| 2. UE1 geometry and shaders | Complex surfaces, Gouraud, tile/line/point primitives, flags, GLSL ES 3 shaders, batching | 4-6 files, 750-1,250 lines, 6-10 days |
| 3. Texture system | Palettes, mip upload, light/fog/detail maps, sampler/cache/update behavior | 4-6 files, 550-950 lines, 4-8 days |
| 4. Offscreen and compatibility | Hit buffer/readback, scene depth/color targets, flash/gamma, optional bloom, flat regression suite | 3-5 files, 450-850 lines, 4-7 days |
| 5. WebXR presentation adapter | `makeXRCompatible`, `XRWebGLLayer`, XR rAF, opaque-FBO binding/restoration, per-eye viewport, session teardown | 2-4 files, 300-650 lines, 3-6 days |
| 6. Quest hardening | Context loss, resume, render scale, thermals/frame pacing, UT99 and Unreal Gold visual/performance QA | tests/docs plus 5-10 days and real hardware |

Expected total: roughly 16-26 files, 2,350-4,250 source lines, and 5-8
engineer-weeks before release confidence. A minimal visible renderer may arrive
earlier, but it should not be selected automatically until representative UT99
maps, menus, translucent surfaces, lightmaps, and Unreal Gold have passed.

## Integration order

1. Review this selection seam on top of `pr/web-platform-foundation`.
2. Build the atlas compositor as a dependent presentation-adapter topic branch
   on the WebGPU foundation plus neutral WebXR view/presentation contracts.
3. Add browser capability selection: native WebGPU-WebXR binding when it is
   genuinely functional, atlas compositor when its on-device probe passes, and
   an explicit unavailable result otherwise.
4. Measure full-resolution UT99 on Quest. Promote the compositor only if it
   meets frame time and motion-to-photon latency budgets through session
   enter/exit/resume cycles.
5. Start the full flat WebGL 2 renderer only if the bridge fails those gates or
   if a renderer without WebGPU becomes a separate product requirement.
6. Keep WebGPU and ordinary flat browser operation intact; presentation choice
   is not a second engine fork.

## Required release gates

- native Windows build and `--help` remain green;
- no-data Emscripten WebGPU link and existing browser smoke tests remain green;
- a dedicated flat WebGL 2 visual/readback smoke suite passes;
- atlas color/orientation tests pass at the exact XR layer size;
- Quest measurements record median/p95/p99 handoff cost, missed frames, and at
  least a coarse one-frame-latency check; a reported 4 ms from another project
  is context, not an acceptance threshold;
- context loss/restore and window resize pass in Chromium and Firefox where
  supported;
- synthetic WebXR testing validates two viewports and FBO restoration;
- physical Quest Browser testing validates session entry/exit/resume, controller
  input, menus, representative UT99/Unreal Gold maps, and sustained frame time;
- no renderer is advertised merely because the browser exposes an API.
