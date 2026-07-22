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
Quest-facing behavior has been reviewed. No pull request has been opened and
the upstream `origin` has not been modified.

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

As of integration commit `61028b3b`, Windows x64 Release and the optional
OpenXR-enabled build both link the full engine and pass all 21 registered tests.
That matrix includes the existing UE1/Deus Ex/bot checks plus view families,
presentation targets, XR common, native XR input, WebXR frame/input bridges,
live WebXR input application, UI-surface policy, the runtime adapter, and the
real engine-canvas binding. The ordinary executable and `--help` path remain
available; desktop keyboard and mouse ownership is unchanged.

The same integration compiles and links the complete data-free Emscripten
JavaScript/WASM target. The served browser matrix currently passes:

- 9 shared launcher, provider-isolation, capability, and XR-adapter fallback checks;
- 19 UT99/Unreal Gold import and safe-map checks;
- 13 OPFS/IndexedDB mutable-persistence and migration checks;
- 3 provider-neutral data-bootstrap checks;
- the WebXR ABI-v2 eye-texture, controller-packet, failure, exit, and re-entry test;
- the flat/WebGPU legal no-data import gate; and
- a synthetic Unreal Gold launch with the expected native arguments.

The staged data-free package at the current integration point contains 15 files
and is 7,000,491 bytes unpacked. It is cross-origin isolated, reaches the local
import gate, launches the synthetic Unreal Gold selection, includes no
commercial packages, and records SHA-256 hashes for every payload file.

The direct WebGPU XR provider now owns actual left/right projection textures,
viewports, asymmetric projection matrices, controller packets, and failure-safe
frame-loop transfer. The engine binding captures actual menu, intro/video, and
loading canvases, preserves topmost menu ordering, delays click edges until the
new cursor position is consumed, and retains physical mouse fallback. The
remaining renderer work is composing those captured surfaces into each XR eye
and drawing pointer feedback from the exact same hit result.

Physical Quest presentation is still a hard gate. Current Meta Quest Browser
reports do not show production `XRGPUBinding` support even though ordinary
WebGPU works. The direct WebGPU XR path therefore remains experimental. A
separate `XRWebGLLayer` compatibility lane is evaluating a WebGPU-rendered
stereo canvas copied through WebGL2 versus a full WebGL2 render device; neither
fallback is represented as complete yet.

## Active extraction lanes

- Web: flat WASM/WebGPU, legal local data import, per-game persistence, and a
  UT99/Unreal Gold game-library launcher are extracted with no WebXR
  requirement. The WebXR provider composes with that launcher only on the
  integration target.
- Deus Ex: generic actor movement and property serialization plus the first
  game-owned tokenizer/paging and pure AI-perception modules are extracted;
  next are save/package slices and native behavior registration.
- Bots: deterministic runtime, bounded headless lifecycle, and the first
  controlled bot driver plus bounded deterministic telemetry are extracted;
  next are controlled fixtures and evidence-backed behavior fixes.
- XR: presentation layers, opaque target binding, and per-view target selection
  are explicit. Native OpenXR and direct WebGPU WebXR providers, live controller
  input, XR-common spaces, pointer hits, haptic routing, provider-neutral
  UI-surface policy, and actual engine canvas/input binding are integrated. The
  projection-eye UI compositor and Quest-compatible WebGL presentation fallback
  are active isolated lanes.

The upstream repository is active. Contact in the public Discord on 2026-07-22
confirmed that small bug fixes and improvements are welcome when maintainers can
evaluate them and the contributor personally understands their effect. Large AI
refactors and unowned agent output are not wanted. `Docs/UpstreamCoordination.md`
records that policy, the relationship to SurrealGPU/SurrealWidgets, and the
human-curated bugfix-first PR strategy. No upstream PR has been opened.

## Next integration gates

1. Finish projection-eye composition of captured menu, intro/video, loading,
   controller-ray, and exact-contact feedback without double-running script UI.
2. Complete the smallest viable `XRWebGLLayer` path for production Quest
   Browser, or document a verified target browser/runtime that exposes the
   direct `XRGPUBinding` path.
3. Rebuild the shared data-free browser artifact from the final integration and
   test real user-owned UT99 and Unreal Gold imports, persistence, save/quit,
   flat fallback, and XR enter/exit/re-entry.
4. Run the headset matrix: stereo/FOV, head pose, both controller mappings,
   disconnect/blur, menu laser/contact, mouse fallback, intro/menu ordering,
   audio, and return to desktop mode.
5. Publish under `/webxr/Ports/SurrealEngine/` only after those hardware and
   legal-data gates pass. Electron remains an optional flat wrapper.
6. Run the native flat, native OpenXR, flat WebGPU, WebXR, Deus Ex, Unreal Gold,
   and deterministic-bot regression matrix before retiring any old worktree.
7. Audit each possible upstream contribution independently against current
   upstream. Select the smallest manually reproducible Deus Ex or generic UE1
   correctness fix first; do not submit the integration architecture.
