# UT99 → WebXR Port Plan (M5)

**Status: planning document only, not a build commitment.** Per `PLAN.md`'s
M5 milestone, moved up to be written now (2026-07-18) at the user's request,
rather than waiting for native VR (M2-M4) to finish — M2 is currently
blocked on an elevated-permissions step and/or physical headset time, so
this planning work fills that gap without touching the blocked native path.
This does **not** change the earlier decision to build native VR first were
it not blocked; it's parallel planning, not a replacement. Nothing here
should be implemented without a separate, explicit go-ahead — see
"Decision needed" at the end.

## The question that motivated this doc

User asked: given how well the sibling `webxr-port/` project (QuakeQuest →
WebXR) went, can the SurrealEngine WebXR port reuse more of the *existing*
native C++ work than previously assumed, rather than being a ground-up
JS/Three.js rewrite? Short answer: **partially** — the low-level graphics
API layer cannot be reused (browsers don't expose Vulkan or native OpenXR at
all), but there's a real middle path between "rewrite everything" and
"reuse everything" that wasn't fully on the table when this was last
evaluated. See "Architecture options" below.

## Precedent: `webxr-port/` (QuakeQuest), same workspace

Directly relevant prior art, not a different project's tangential research —
this is a **completed, deployed, working** WebXR port in this exact
workspace (`C:\Devstuff\QuestGames\webxr-port\`, live at
`https://dionysus.dk/webxr/quakequest/`), same user, same target headset
(Quest 3), same general toolchain (Emscripten). M1-M4 all shipped:
Emscripten-compiled DarkPlaces (C engine) running in-browser, real WebXR
stereo rendering, full 6DoF controller input ported from the Android VR
mod, comfort options, PWA packaging, even a sideloadable APK wrapper. It
proves the *harness* pattern — WASM engine + WebXR JS bridge + COOP/COEP
static hosting + IDBFS persistence + headless-Chrome (IWER) automated
testing without a headset — works extremely well for this exact kind of
"native VR mod → browser" port, on this exact hosting setup, for this exact
user's Quest 3 workflow.

**Why it isn't a direct template for SurrealEngine, though**: DarkPlaces
already had a GLES2/3 renderer, and GLES↔WebGL2 is a near-1:1 API match —
QuakeQuest's engine-side rendering code needed almost no changes (a few
call-signature fixes, a VBO modernization pass). SurrealEngine has **no**
GL-family renderer at all (`Docs/Status.md`: "There is no OpenGL renderer"),
only D3D11 and Vulkan. That gap is the entire reason this was scoped as
"native first, WebXR later" to begin with (see `PLAN.md`'s decision log).

## Architecture options considered

### Option A: Emscripten + WebGPU port of SurrealEngine itself (new option, researched this session)

Keep the existing C++ engine — UnrealScript VM, actor system, BSP/level
code, UPackage/UTexture/asset loading, the portal/mirror `VisibleFrame`
render-graph logic — and rewrite only the lowest layer, `SurrealGPU`
(`VulkanInstanceBuilder`/`VulkanDeviceBuilder`/pipeline/descriptor/command-
buffer code), against WebGPU's C API (`<webgpu/webgpu.h>` via Emscripten's
`emdawnwebgpu` port) instead of Vulkan. This is the same shape of thing
QuakeQuest did (compile the existing engine to WASM, replace only the
platform/graphics glue), just with a real rewrite at the graphics-API layer
instead of a near-free recompile.

**Researched and ruled out an even cheaper version of this**: hoped there
might be an automatic Vulkan→WebGPU translation layer that would make this
close to a recompile, the same way GLES→WebGL2 was for QuakeQuest. There
isn't one, and structurally can't easily be one — WebGPU is intentionally a
*higher-level* API than Vulkan (implementations like Dawn/wgpu run WebGPU
**on top of** Vulkan natively; nothing goes the other direction, and
emulating a lower-level API via a higher-level one is a fundamentally hard,
high-overhead problem, not just an unwritten library). Confirmed via web
search 2026-07-18 (Emscripten's own WebGPU docs, an open Emscripten issue
discussing exactly this gap, and community discussion converging on the
same conclusion). So `SurrealGPU`'s Vulkan backend would need a genuine
rewrite against `webgpu.h` — bounded to that one library (it already has a
clean, self-contained builder-pattern API surface — see
`VulkanRenderDevice.cpp`'s use of `VulkanInstanceBuilder`/
`VulkanDeviceBuilder`/etc., all touched again just this session), not
"rewrite the whole engine."

Rough shape if pursued:
- `SurrealGPU` (or a new `WebGPU`-target sibling to it) reimplements the
  same builder-pattern surface (`vulkanbuilders.h`'s ~15 builder classes)
  against `webgpu.h` instead of `vulkan/vulkan.h`. `VulkanRenderDevice.cpp`
  and friends would become `WebGPURenderDevice.cpp`, following the same
  `RenderDevice` abstract interface SurrealEngine already has (it already
  supports two backends — D3D11 and Vulkan — so the abstraction is proven
  to support a third).
- WebXR-specific layer: replaces `VulkanXRSession`'s native OpenXR calls with
  the WebXR/WebGPU Binding module (`XRGPUBinding`, an `xrCompatible`
  `GPUDevice`, WebGPU-backed projection layers instead of `XRWebGLLayer`).
  **Resolved 2026-07-18** (was previously flagged as an open maturity
  concern): this is now an official Immersive Web **Editor's Draft** (dated
  2026-06-15), and Emscripten's WebGPU port, `emdawnwebgpu`, is actively
  maintained by the Dawn team and enabled via a single
  `--use-port=emdawnwebgpu` flag (superseding the older, now-unmaintained
  `-sUSE_WEBGPU`). Not shipped/stable-across-browsers yet, but no longer the
  vague "needs its own scoped recon" unknown it was — a real spec and a real,
  maintained toolchain path both exist.
- Everything else — UnrealScript VM, gameplay, asset formats, the
  `VisibleFrame` portal/mirror system, and (once M2 finishes) the
  coordinate-convention and asymmetric-frustum math already verified this
  session — carries over close to unchanged, same as QuakeQuest's ~65-70%
  engine-side gameplay code survived compilation as-is.
- Main-loop inversion needed (browser `requestAnimationFrame`/
  `XRSession.requestAnimationFrame` drives the loop, not
  `Engine::Run()`'s own loop) — same restructuring QuakeQuest did via
  `emscripten_set_main_loop`.

**Effort/risk profile — scoped 2026-07-18** (previously "unknown until
surveyed"; a full recon pass over `SurrealGPU` plus targeted research now
gives a real picture, not an estimate):

*Real size*: `SurrealGPU` itself is ~7,400 lines (16 headers + 12 `.cpp`),
but the Vulkan-specific consumer layer in
`SurrealEngine/RenderDevice/Vulkan/` (42 files, ~5,000 lines) would also need
rewriting — the actual port surface is closer to **12,000 lines**, not "just
one library."

*What's cheap or free*: ray query/acceleration-structure support is
requested (`VulkanRenderDevice.cpp:34`, `OptionalRayQuery()`) but **never
actually used anywhere in the renderer** — zero porting cost, just drop it.
Compute pipelines are likewise present in the API but have **zero call
sites** in the engine (everything is fragment-shader full-screen passes) —
no compute-migration burden exists today. Push constants are three small,
fixed-size structs (`ShaderManager.h:20-46`) — a standard, low-risk
substitution for WebGPU's small per-draw uniform buffers. Shaders are a
small surface (~12 distinct embedded GLSL bodies, `FileResource.cpp`,
permuted via `#define` into ~49 pipeline variants) needing translation to
WGSL regardless of anything else — bounded, well-understood work. VMA
(the memory allocator) disappears entirely in a port rather than needing
translation, since WebGPU manages memory internally.

*What's real work, but mechanical*: the manual synchronization layer
(semaphores/fences/pipeline barriers/explicit image-layout transitions,
driven directly by engine code in `CommandBufferManager.cpp`,
`SceneTextures.cpp`, `UploadManager.cpp`, `TextureManager.cpp`, currently
with zero frames-in-flight overlap) has to be **deleted**, not translated —
WebGPU hides all of this behind an implicit model. This is a real rewrite of
that layer, but a well-understood one with a clear target shape.

*The headline blocker, found this session*: the entire scene renderer is
built around **one unbounded, dynamically-indexed bindless texture array**
(`sampler2D textures[]`, up to 16,536 slots, `DescriptorSetManager.h:68`,
flagged `PARTIALLY_BOUND`/`VARIABLE_DESCRIPTOR_COUNT`/`UPDATE_AFTER_BIND`,
indexed per-draw via a per-vertex non-uniform index —
`FileResource.cpp:78-81`, `nonuniformEXT(textureBinds.x)`). This isn't an
incidental detail; it's *how the renderer avoids per-draw descriptor
rebinding at all*. Core WebGPU today has **no equivalent**. Fixed-size
`binding_array<texture_2d<f32>>` support is an active proposal, not yet
shipped (Chrome/Dawn show only preparatory groundwork as of 2026-07). True
unbounded bindless matching SurrealGPU's actual model is targeted later
still, and is explicitly described by the WebGPU working group as needing
hardware support "missing in many devices WebGPU supports." Porting this
faithfully today isn't possible — it would require either waiting on a
spec/implementation with no committed ship date, or redesigning the
renderer's core texture-binding strategy (per-material bind groups, texture
atlasing, or a much smaller fixed-size array sized to typical
concurrent-unique-textures-per-frame rather than total-loaded-textures).
That's a real architecture change to the draw loop's core mechanism, not a
mechanical API swap — the one place this option stops being "rewrite one
library" and becomes "redesign a load-bearing piece of the renderer."

*One relevant scrap of prior art*: the codebase already contains an
unfinished `GPU*` shim (`gpudevice.h`/`gpubuffer.h`/`gpubindgroup.h`/etc.)
whose method names (`createBindGroup`, `createRenderPipeline`, `writeBuffer`,
`dispatchWorkgroups`...) track the real WebGPU JS API almost 1:1 — real past
intent toward exactly this idea. It is **not usable as-is**: unreferenced
anywhere in the engine, and its core draw/dispatch/write methods are empty
stubs with commented-out bodies (`gpucommands.cpp:9-231`).

Net effect on the earlier optimism: this option inherits
SurrealEngine's *existing, this-session-hardened* rendering logic (the
asymmetric-projection math, the portal/sky/mirror viewport-propagation fix,
etc.) for everything **except** the resource-binding model, which is the one
piece that needs genuine redesign rather than translation. See "Current
lean" below for how this changes the comparison against Option B.

### Option B: Ground-up Three.js + UTPackage.js rewrite (previously considered, still viable)

Write a new engine in JS/TS: Three.js for rendering + WebXR, UTPackage.js
(CC0-licensed UPackage/UTexture/UModel format parser — researched during
this project's original scoping phase, see `PLAN.md`'s decision log; that
research was fairly shallow — a follow-up recon of UTPackage.js's actual
maturity/coverage would be needed before committing to this path) for
loading real UT99 assets, and hand-authored JS gameplay logic replacing the
UnrealScript VM.

**Effort/risk profile**: Lowest *rendering* risk (Three.js's WebXR support
is mature and doesn't require solving any translation-layer problem at
all), but by far the largest **gameplay-authoring** risk — this was the
original reason native-first was chosen over this path. UT99's actual
gameplay (weapon behavior, pickup/respawn logic, bot AI, game modes) lives
in UnrealScript bytecode that SurrealEngine already interprets; a Three.js
rewrite means re-implementing all of that from scratch in JS with no VM to
lean on, informed only by reverse-engineered behavior (same clean-room
policy as the native project — Unreal Wiki/BeyondUnreal docs + observed
behavior, never decompiled code directly).

### Option C: direct Vulkan→WebGPU auto-translation

**Ruled out** — no such library exists, and the API-level direction
mismatch (WebGPU is higher-level than Vulkan) makes it a fundamentally hard
problem rather than a missing-but-buildable tool. Not pursued further.

## Comparison

| | Option A: Emscripten+WebGPU port | Option B: Three.js rewrite |
|---|---|---|
| Reuses gameplay/VM/asset logic | Yes (unmodified UnrealScript VM, actors, BSP) | No — rewritten in JS |
| Reuses rendering logic/math | Yes (portal/mirror system, this session's frustum math) — only the graphics-API calls change | No — new renderer from zero |
| Graphics API risk | Mostly bounded (one library's worth of rewrite, proven abstraction boundary already exists) **except** the bindless-texture core, which is blocked on an unshipped WebGPU proposal | None (Three.js WebXR is mature) |
| WebXR-in-browser precedent in this workspace | Partial (QuakeQuest proves the harness, not this graphics stack) | Partial (QuakeQuest proves the harness, not this asset/gameplay stack) |
| Biggest unscoped unknown | ~~SurrealGPU→WebGPU rewrite size/parity gaps~~ **Resolved 2026-07-18** — now known: bindless-texture-model redesign, since it has no shipped WebGPU equivalent and no clean 1:1 fix | UTPackage.js real maturity; JS gameplay rewrite scope (still open) |
| Informed by native VR work already done (M2, this session) | Directly — same C++ code | Indirectly — conventions/math transfer conceptually, code doesn't |

**Current lean, revised 2026-07-18, still not a final call**: the recon this
doc previously called for is now done (see Option A's "Effort/risk profile"
above) — SurrealGPU's real size (~12,000 lines including its Vulkan
consumer layer), what's dead weight (ray query, unused compute), what's
mechanical-but-real (the sync/barrier layer), and what's genuinely blocked
(the bindless texture model) are all known now, not estimated.

That recon does **not** support the previous "Option A looks more
promising" lean as stated — it should be walked back. Most of Option A's
surface turned out cheaper than feared (dead-code drops, small shader
surface, WebXR-WebGPU bridge now has a real spec + maintained toolchain),
but the one piece that matters most for a faithful port — the renderer's
core resource-binding mechanism — has no shipped WebGPU path today and a
proposal timeline with no committed date. Option A is **better scoped now,
not more clearly favored**: it's no longer "rewrite one bounded library,"
it's "rewrite one bounded library, plus redesign the core texture-binding
strategy of the draw loop, while a relevant piece of the target platform is
still an unshipped proposal." Whether that's still preferable to Option B's
UTPackage.js-maturity-and-full-gameplay-rewrite risk is a genuine judgment
call between two different kinds of risk (bounded-but-blocked-on-a-platform-
gap vs. large-but-fully-in-your-control), not something this recon resolves
on its own. Option B's own biggest unknown (UTPackage.js maturity) remains
unrecon'd — a fair comparison would need that pass too before leaning either
way with confidence.

## What already transfers regardless of which option is chosen

- **Coordinate/camera math** verified this session: UE1's `YAxis` = camera
  right vector, the asymmetric (off-axis, parallel-camera) stereo frustum
  formula and its sign conventions (`RenderScene.cpp:68-96`,
  `VR_IMPLEMENTATION_PLAN.md` M2 step 6), the portal/sky/mirror recursive
  frame system's viewport-propagation requirements
  (`VisibleFrame::DrawPortals`). These are engine-agnostic facts about
  UE1's conventions and stereo rendering math, not C++-specific — directly
  useful for Option B's from-scratch renderer too, not just Option A.
- **WebXR harness pattern** proven by `webxr-port/`: session lifecycle,
  COOP/COEP hosting requirements, IDBFS-style persistence, headless-Chrome
  (IWER) automated verification without a physical headset, PWA/deploy
  pipeline to `dionysus.dk`. Directly reusable regardless of which rendering
  option is chosen, since it's about the WebXR/browser/deploy layer, not
  the renderer.
- **Clean-room legal policy**: unchanged from the native project — public
  UnrealScript docs + observed behavior, two-agent split if decompilation
  is ever needed. Applies identically to either option's gameplay-logic
  gaps.

## What's NOT solved by this document (deliberately)

- ~~Exact WebGPU feature-parity survey for `SurrealGPU` (Option A)~~
  **Resolved 2026-07-18** — see Option A's "Effort/risk profile" above.
- Real UTPackage.js maturity/coverage assessment (Option B) — the original
  research was shallow; needs a fresh look before committing. **Still open**
  — the one recon item from this doc's original list that hasn't been done.
- ~~WebXR-WebGPU bridge maturity in Emscripten (Option A)~~ **Resolved
  2026-07-18** — official Editor's Draft exists (`XRGPUBinding`), Emscripten
  toolchain support (`emdawnwebgpu`) is actively maintained. See Option A's
  rough-shape bullets above.
- Any actual code. This is a planning document per M5's explicit framing in
  `PLAN.md`/`VR_IMPLEMENTATION_PLAN.md` — implementation is a separate,
  explicit future decision.

## Decision needed (not made here)

1. ~~Is it worth spending recon time now...~~ **Partially answered
   2026-07-18**: Option A's half of this (SurrealGPU/WebGPU survey) is done,
   per above. Option B's half (UTPackage.js maturity check) is not — still
   an open choice whether to do that recon now too, or hold for the original
   M5 sequencing.
2. If/when a build decision is made: Option A, Option B, or continue
   treating WebXR as deferred indefinitely in favor of the native mod. Now a
   sharper call than before (real Option A costs are known), but still not
   made here.

Nothing below M5's original framing has changed: this is still "write a
plan, then stop and hand it back for a decision."
