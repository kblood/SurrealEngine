# Unified engine implementation

This document tracks the provider-neutral foundations used to converge the
native VR, WebAssembly/WebGPU/WebXR, Deus Ex, and bot-AI work without turning
their development branches into one unreviewable fork.

## Branch policy

- `pr/*` branches are small upstream-facing topics. They stay separate and are
  never created from the integration branch.
- A topic is only an upstream candidate after it has been checked against
  current `dpjudas/master`, manually understood line by line, reproduced, and
  supported by focused evidence. Agent completion alone never makes a PR ready.
- `integration/unified-engine` combines accepted topic commits and is used for
  cross-feature builds and runtime testing. It is not a pull-request source.
- Release branches, especially the tagged UT99 VR release, remain independent
  and receive only release fixes until their behavior is migrated through the
  new contracts.
- The checkpointed WebXR, Deus Ex, and bot branches are evidence/reference
  branches. Changes are reconstructed into focused `pr/*` topics rather than
  merged wholesale.

## Foundation topics

| Topic branch | Commit | Purpose | Dependency |
| --- | --- | --- | --- |
| `pr/frame-pipeline` | `5dbcf833` | Advance simulation once, render, then finish deferred save/travel work | current upstream |
| `pr/view-family` | `c0e81509` | Render an explicit family of camera views with optional asymmetric projections | `pr/frame-pipeline` |
| `pr/presentation-layers` | `9d96e62f` | Route world, overlay, UI, and cinematic layers to provider-owned target slots | `pr/view-family` |
| `pr/presentation-target-binding` | `398dbfd8` | Bind opaque provider-owned images and select one image per rendered view | `pr/presentation-layers` |
| `pr/input-composition` | `d5fa59ca` | Compose buttons and axes from independent input sources | current upstream |
| `pr/vm-hook-registry` | `5d123dac` | Ordered, scoped VM-call extension hooks with safe argument replacement | current upstream |
| `pr/game-support-registry` | `2ccdb1d8` | Typed game identity, behavior capabilities, and per-game native registration | current upstream |
| `pr/deus-ex-runtime-fix` | `aada7f2a` | Fix generic cardinal-axis actor movement with regression coverage | current upstream |
| `pr/deus-ex-text` | `efca318e` | Tested Deus Ex tokenizer/paging module and thin UObject adapters | `pr/game-support-registry` |
| `pr/deterministic-runtime` | `234303bc` | Add opt-in seeded/fixed-step runtime state for benchmarks | current upstream |
| `pr/headless-benchmark-driver` | `868239ad` | Add a bounded, named, opt-in driver lifecycle | `pr/deterministic-runtime` |
| `pr/bot-benchmark-driver` | `8d568202` | Add controlled UT map/spectator/bot setup and exact run summaries | `pr/headless-benchmark-driver` |
| `pr/property-serialization` | `1965a397` | Serialize aggregate boolean values symmetrically and test all aggregate paths | current upstream |
| `pr/web-platform-foundation` | `88980d62` | Flat Emscripten/WebGPU platform and browser smoke harness | `pr/frame-pipeline` |
| `pr/web-data-persistence` | `2dda89bd` | Legal local UT99 import, safe map selection, and crash-safe mutable browser data | `pr/web-platform-foundation` |
| `pr/deus-ex-ai-perception` | `0842bd86` | Pure Deus Ex sight, hearing, and motion-visibility formulas | `pr/game-support-registry` |
| `pr/bot-benchmark-telemetry` | `a090cb6c` | Immutable manifests and bounded JSONL benchmark evidence | `pr/bot-benchmark-driver` |
| `pr/openxr-provider` | `e6150bf2` | Optional native OpenXR lifecycle, views, swapchains, and target binding | frame/view/presentation stack |
| `pr/webxr-provider` | `89608ca7` | Optional WebXR/WebGPU lifecycle, view packet, and projection target | web/frame/view/presentation stack |
| `pr/web-desktop-launcher` | `6c614d56` | Shared UT99/Unreal Gold browser library and flat/XR launch seam | `pr/web-data-persistence` |
| `pr/openxr-input-adapter` | `8b7a2153` | Semantic native controller snapshots composed by independent source | OpenXR provider and input composition |
| `pr/xr-common-spaces` | `9666a488` | Provider-neutral lifecycle, head/aim/grip spaces, recentering, pointer hits, and haptics | `pr/input-composition` |
| `pr/webxr-input-adapter` | `e69b7567` | Versioned browser controller packet and XR-common semantic adapter | WebXR provider, input composition, and XR common |
| `pr/xr-ui-surfaces` | `0f879d9c` | Ordered world-anchored menu/HUD/cinematic/loading surfaces and multi-source pointer routing | `pr/presentation-layers` |
| `pr/webxr-input-runtime` | `d472068a` | Apply live WebXR snapshots through ordinary engine input while preserving flat contributors | `pr/webxr-input-adapter` |
| `pr/openxr-xrcommon-adapter` | `d25f8186` | Translate native OpenXR lifecycle, spaces, controllers, and haptics through XR-common | OpenXR input and XR-common topics |
| `pr/xr-ui-runtime-adapter` | `6400bdec` | Capture real menu, cinematic, and loading canvases and route exact pointer contact through existing mouse primitives | `pr/xr-ui-surfaces` |
| `pr/webxr-presentation-runtime` | `a675d6d7` | Complete ABI-v2 per-eye WebGPU texture handoff, frame ownership, and capability diagnostics | `pr/webxr-provider` |

`release/webxr-browser-package` adds the product-only shared launcher and
data-free static packager in `5ff8fd91` with documentation in `18b0449f`. It is
not an upstream PR source: it composes the launcher/data and WebXR topic stacks.

The integration branch contains the reviewed equivalents of every completed
topic above, including the small conflict and build-system follow-ups needed
when those topics coexist. The original foundation topics are preserved on the
`fork` remote. The newest runtime/package topics remain local until the combined
Quest-facing behavior passes final release review. No pull request has been
opened and the upstream `origin` has not been modified.

## Status language

This roadmap uses four deliberately separate claims:

- **Implemented** means the path exists in `integration/unified-engine`; it is
  not by itself a release or upstream-readiness claim.
- **Automated evidence** means a deterministic build or test passed at the
  stated integration commit without commercial game data or a headset.
- **Experimental** means the feature remains a product-integration lane and
  may still change after real runtime evidence. Experimental integration
  commits are not upstream PR sources.
- **Hardware-unverified** means synthetic or desktop-browser evidence cannot
  establish physical Quest presentation, timing, input, or comfort.
- **Hardware-observed failure** means a named physical path was attempted and
  exposed a release blocker; it is stronger evidence than a synthetic result
  but is not a partial release qualification.
- **Owner-data-unverified** means no automated fixture can establish behavior
  with a user's commercial packages, maps, media, and scripts.

At tested integration code commit `4dfceb0f`, the main product-only additions
beyond the foundation topics are:

| Integrated slice | State | Evidence and remaining boundary |
| --- | --- | --- |
| Flat desktop WASM/WebGPU | Implemented; audio endurance qualification remains | Immutable candidate `173bf623` restored exact owned GOG UT99 and Unreal Gold libraries at the live origin and launched both with advancing ticks, visible WebGPU rendering, and zero page/WebGPU errors. Its owner-data qualification reproduced WasmFS `analyzePath` failure, then proved the `46d13173` `stat` fallback by snapshotting, flushing, reopening, and byte-exactly restoring `Save99.usa` while excluding a disallowed sibling. A human Chrome/Virtual Desktop retest confirmed pointer capture, fire, and working relative mouse-look through the `28906717` browser-delta bridge. Audio was audible but later remained suspended after a lifecycle transition; `4dfceb0f` now retries resume on return-to-visible and presentation transitions. Audio recovery and longer play remain gates. |
| Direct WebXR/WebGPU presentation | Implemented, experimental; prior hardware-observed failure | On Quest 3 through desktop Chrome/VDXR, the old Automatic path reached consent but immediately left immersive mode. Commit `f3643b78` now negotiates optional WebGPU from `session.enabledFeatures`, fails unobservable state closed, and prohibits same-session layer mixing. Automated ABI/lifecycle/exclusivity tests pass; the new path remains physically unqualified. |
| Quest compatibility presentation | Implemented, experimental | `XRWebGLLayer` receives a WebGPU-rendered stereo atlas through WebGL 2; desktop API probes and provider tests pass. The first VDXR attempt did not force this backend, so physical correctness and transfer cost remain unverified. |
| WebXR UI and controllers | Implemented, experimental | World-anchored surfaces, both procedural controller proxies, lasers, and exact-contact markers share one hit result in both presentation modes; scale, latency, convergence, and comfort remain hardware-unverified. |
| Semantic XR gameplay input | Implemented, experimental | WebXR and OpenXR use the provider-neutral `XRInputAdapter`; right-dominant Select maps directly to Fire, the other Select to AltFire, sticks map to movement/turning, dominant primary is Jump, off-hand primary is NextWeapon, and off-hand secondary/provider Menu opens ShowMenu. Menu/focus ownership releases only XR contributors and blocks held buttons until a fresh press, while hostile `User.ini` Joy mappings are bypassed. WebXR retains one discrete state per simulation frame. The shared turn policy keeps continuous right-stick turn as the compatibility default and supports runtime smooth scaling or explicit snap mode with latch/hysteresis and focus-safe rearming. Settings persistence/UI, movement reference, remapping, and hardware comfort qualification remain follow-ups. |
| One-hand XR weapons | Implemented, experimental | A shared aim-pose solver, scoped full-tick firing direction, and contiguous per-eye weapon pass are used by WebXR and OpenXR. Default placement follows the Farantir hardware baseline (aim pose, zero offset, 5x scale); physical calibration, muzzle-origin rewriting, two-hand/dual-wield behavior, and loaded UT99/Unreal fixtures remain gates. |
| UT99/Unreal startup map intro | Implemented, experimental | `URL.LocalMap`, prompt-HUD capture, menu handoff, intro-only trigger routing, and explicit launcher skip policy have automated coverage. Owned UT99 reached its CityIntro-to-UMenu handoff; Unreal's normal `URL.LocalMap` path and both games in Quest remain unverified. |
| KHG browser AVI playback | Implemented, experimental | The existing IV50 decoder advances asynchronously under the flat/WebXR frame owner; synthetic scheduling and no-data linking pass, while actual KHG media, browser audio, masks, and same-call-stack script assumptions remain unverified or incomplete. |
| Local UE1 demo import | Implemented, experimental | UT demo 348 and Deus Ex demo 1002f reach bounded package scans; Unreal demo 205 advances past both the legacy `Parent`/`Outer` and `DynamicString` layout boundaries, then stops at the absent `UPak` content dependency. All remain local-import-only, gameplay-unverified, and redistribution-gated. |
| Static-WASM corresponding source | Implemented | Clean commit/tree provenance, matching source archive or exact HTTPS source URL, hashes, notices, and relink instructions are enforced by packaging tests. Human/legal review of the actual distribution is still required. |

## Contract boundaries

The frame pipeline has one simulation advance and one finish phase. A desktop
frame currently creates one `ViewDescription`; XR providers will create two or
more descriptions from the same simulation state. A view carries an explicit
camera transform, viewport, FOV, and optional projection matrix. It deliberately
does not contain OpenXR, WebXR, swapchain, browser, or headset types.

Input actions retain every contributing `(source, control)` pair. Releasing a
controller or browser session removes only that source. Keyboard/mouse remains
the default and can operate alongside native XR or WebXR instead of being
replaced by them.

Native and browser UI policy converges through `XRUIRuntime`: both use the same
surface descriptors, anchoring, menu-last ordering, exact pointer contact and
click ownership, startup fire route, mouse coexistence, and lifecycle cleanup.
Native OpenXR has an SDK-free allocation/composition seam and view-consistent
ray translation. Vulkan can render and retain independent single-image slots
2-5 without disturbing the world pass, and the OpenXR owner now allocates,
acquires, binds, and submits the ordered no-depth quad layers. This remains
hardware-unverified on the current integration branch.

The VM hook registry is a narrow attachment point for optional game and XR
behavior. Hooks are ordered, scoped to a call, mutation-safe, and unwind in
reverse order. Game-specific behavior should still prefer explicit engine
capabilities or native implementations; hooks are for behavior that genuinely
crosses VM calls and should not become a second scripting system.

The game-support registry names engine behavior (`SaveInfoPackages`,
`PostPostBeginPlayEvent`) rather than scattering game-name checks. It is a
compiled-in extension boundary, not a dynamic plugin ABI. More existing games
can be moved into it incrementally.

## Web and XR layering

WebAssembly is a platform target, not an XR mode. The reusable order is:

1. core simulation and game support;
2. ordinary Emscripten/browser platform services;
3. WebGPU rendering usable on a flat canvas;
4. optional XR-common presentation/input behavior;
5. optional WebXR provider.

Native OpenXR and browser WebXR should both translate provider poses, views,
buttons, haptics, and session lifecycle into the same view-family and
input-composition contracts. They may have separate binaries or web packages,
but must not fork gameplay, VM, menu, or game-support implementations.

## Current validation

At tested integration code commit `4dfceb0f`, both ordinary and OpenXR-enabled
Windows x64 Release builds pass all 34 registered CTest tests. Conventional
pthread and Window-owned Asyncify/WasmFS no-data Emscripten Release targets also
compile and link the complete `SurrealEngine.js`/WASM application. The ordinary
native executable, flat browser entry point, and desktop keyboard/mouse
contributor remain available; WebXR is an optional presentation provider rather
than a replacement desktop mode.

The served data-free browser matrix passes:

- 17 shared launcher, provider-isolation, capability, diagnostics, adapter
  fallback, and startup-policy checks;
- 30 retail UT99/Unreal Gold import and safe-map checks;
- the separate three-demo descriptor/import suite;
- 16 OPFS/IndexedDB mutable-persistence and migration checks;
- 3 provider-neutral data-bootstrap checks;
- direct WebXR ABI-v3 persistent eye-texture, input, failure, exit, and re-entry tests;
- `XRWebGLLayer` fallback selection, stereo-atlas, cleanup, and re-entry tests;
- the real desktop Chrome WebGPU-to-WebGL 2 upload/readback probe;
- corresponding-source and static release-package audits; and
- flat and WebGPU no-data import-gate smoke, including a synthetic Unreal Gold
  launch with the expected native arguments and a WebGPU render/tick/quit run.

This is **automated evidence**, not a content release result. No commercial
package is part of the test or static artifact, and no synthetic test proves
Quest compositor behavior, user-owned game scripts, browser storage durability
at the final origin, or distribution permission.

The immutable `cef1e89b` package also passed live HTTPS smoke and synthetic
Automatic/direct plus forced-bridge lifecycle checks. A physical Quest 3 test
through desktop Chrome, Virtual Desktop, and VDXR then found two blockers:
Automatic exits immersive presentation immediately after WebXR consent, and
the surviving flat canvas locks the pointer and receives mouse buttons but not
working relative mouse-look. See `WEBXR_VDXR_QUALIFICATION.md`. These results
supersede blanket "hardware-unverified" wording for that named path: it is now a
hardware-observed failed qualification.

Three focused follow-up commits now address the observed software paths:
`46d13173` fixes WasmFS save-directory discovery, `f3643b78` negotiates the
WebXR backend from enabled session features, and `28906717` bridges browser
relative motion without disabling requested-but-unlocked SDL fallback. Their
focused native/browser and conventional plus shipping Emscripten gates pass.
They are present in immutable public candidate `173bf623`, whose clean package,
live HTTPS smoke, and public hashes pass. A same-origin isolated owner-profile
qualification also restored and launched both commercial libraries and proved
an allowlisted UT99 save round-trip through the exact WasmFS fallback. A human
retest then confirmed working relative mouse-look. It exposed a reproducible
audio-resume lifecycle gap now corrected in `4dfceb0f`; recovery and headset
presentation still require human requalification.

The direct path owns runtime WebGPU eye textures. The compatibility path reuses
the same simulation, view-family, input, UI, and WebGPU renderer, drawing both
eyes to an atlas before a WebGL 2 bridge presents them through
`XRWebGLLayer`. Both paths composite the same captured HUD, cinematic, loading,
and topmost menu surfaces and consume the same exact controller contact. The
fallback is therefore implemented, but remains experimental until its
correctness, latency, and cross-API transfer cost pass the physical Quest
matrix.

## Active extraction lanes

- Web: flat WASM/WebGPU, legal local data import, per-game persistence, and a
  UT99/Unreal Gold game-library launcher are extracted with no WebXR
  requirement. Experimental local-import descriptors also recognize UT demo
  348, Unreal demo 205, and Deus Ex demo 1002f without placing game data in the
  package. The WebXR provider composes with that launcher only on the
  product integration and release targets.
- Deus Ex: generic actor movement and property serialization plus the first
  game-owned tokenizer/paging and pure AI-perception modules are extracted;
  next are save/package slices and native behavior registration.
- Bots: deterministic runtime, bounded headless lifecycle, and the first
  controlled bot driver plus bounded deterministic telemetry are extracted;
  next are controlled fixtures and evidence-backed behavior fixes.
- XR: presentation layers, opaque target binding, and per-view target selection
  are explicit. Native OpenXR and direct WebGPU WebXR providers, live controller
  input, XR-common spaces, pointer hits, haptic routing, provider-neutral
  UI-surface policy, actual engine canvas/input binding, projection-eye UI
  composition, exact-contact controller visuals, map-intro prompt/menu handoff,
  and the Quest-compatible WebGL presentation fallback are integrated. The two
  WebXR presentation modes remain experimental pending the physical matrix.
  Semantic Fire/AltFire/movement input and the first shared one-hand weapon
  pose/firing/per-eye-rendering slice are integrated. Retained WebXR input
  edges now advance at most one state per simulation frame, so a quick
  press/release cannot collapse before gameplay observes it; comfort-setting
  UI/persistence and hardware qualification, configurable face-button/menu policy,
  movement reference, muzzle origins, two-hand support, and
  loaded game qualification remain active work.

The upstream repository is active. Contact in the public Discord on 2026-07-22
confirmed that small bug fixes and improvements are welcome when maintainers can
evaluate them and the contributor personally understands their effect. Large AI
refactors and unowned agent output are not wanted. `Docs/UpstreamCoordination.md`
records that policy, the relationship to SurrealGPU/SurrealWidgets, and the
human-curated bugfix-first PR strategy. No upstream PR has been opened.

## Next integration gates

1. Validate both direct `XRGPUBinding` (where exposed) and automatic/forced
   `XRWebGLLayer` atlas modes on Quest, including stereo/FOV, transfer cost,
   controller/laser/contact alignment, menu ordering, and repeated lifecycle.
2. Test normal and skipped `URL.LocalMap` startup for owned UT99 and Unreal Gold
   data. Keep this map/script path separate from owner-supplied KHG AVI tests;
   validate the new map/save loading-visibility scope without double-running
   script UI, including success, failure, and menu-topmost behavior.
3. Keep the deployed data-free browser artifact and matching corresponding
   source reproducible from the exact clean release commit. Candidate
   `2904593c` satisfies that mechanical gate and uses revisioned browser assets;
   have the actual source offer, hosting terms, notices, and redistribution
   model reviewed by a responsible human/legal reviewer.
4. Extend the successful owned UT99 and Unreal Gold direct-start smokes to
   title switching, persistence, save/quit, package upgrade, flat fallback, and
   XR enter/exit/re-entry. Keep all demo support local-import-only; determine
   whether Unreal 205's missing `UPak` dependency can come from a lawful
   original artifact, and validate every demo before advertising compatibility.
5. Run the headset matrix: stereo/FOV, head pose, both controller mappings,
   disconnect/blur, menu laser/contact, mouse fallback, intro/menu ordering,
   audio, and return to desktop mode.
6. Keep `/webxr/Ports/SurrealEngine/` visibly labeled as an experimental
   preview until the hardware and legal-data gates pass. Electron remains an
   optional flat wrapper.
7. Run the native flat, native OpenXR, flat WebGPU, WebXR, Deus Ex, Unreal Gold,
   and deterministic-bot regression matrix before retiring any old worktree.
8. Audit each possible upstream contribution independently against current
   upstream. Select the smallest manually reproducible Deus Ex or generic UE1
   correctness fix first; do not submit the integration architecture.
