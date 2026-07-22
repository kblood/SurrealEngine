# Unified engine implementation

This document tracks the provider-neutral foundations used to converge the
native VR, WebAssembly/WebGPU/WebXR, Deus Ex, and bot-AI work without turning
their development branches into one unreviewable fork.

## Branch policy

- `pr/*` branches are small upstream-facing topics. They stay separate and are
  never created from the integration branch.
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

The integration branch contains the reviewed equivalents of every completed
topic above, including the small build-system follow-ups needed when those
topics coexist. All twenty-four completed topic branches and
`integration/unified-engine` are preserved on the
`fork` remote. No pull requests have been opened yet, and the upstream
`origin` has not been modified.

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

On Windows x64 Release, the combined foundation:

- configures and builds the full `SurrealEngine` target;
- compiles both D3D11 and Vulkan view-family paths;
- passes `InputCompositionTests`;
- passes `VMCallHookTests`;
- passes `ActorMovementTests`;
- passes `DeterministicRuntimeTests`;
- passes `HeadlessDriverTests`;
- passes `PropertySerializationTests`;
- passes `PresentationTests`;
- passes `BotBenchmarkProtocolTests`;
- passes `DeusExTextTokenizerTests`;
- links `SurrealEngine.exe`; and
- passes the `SurrealEngine.exe --help` smoke test.

The combined integration also configures, compiles, and links the full
Emscripten `SurrealEngine` target without preloaded commercial game data. The
standalone web topic additionally passed real-data browser lifecycle and WebGPU
smokes: 0 WebGPU errors, 95 draw calls, 75 cached textures, nonblank output,
and clean shutdown. The browser-data topic passes 30 synthetic importer,
persistence, migration, and bootstrap checks plus flat and WebGPU no-data
import-gate smokes. Their XR symbol/path audits returned no implementation
matches.

Native OpenXR and browser WebXR providers now exercise the shared multi-view
and presentation-target contracts. Their remaining release gates are physical
headset validation and runtime wiring for common controller input, per-eye
weapons, and captured/replayed UI. WebXR now has a tested versioned controller
packet and semantic adapter; XR-common defines provider-independent spaces,
pointer hits, and haptics. The UI-surface policy fixes ordering, anchoring,
aspect mapping, and source-isolated click behavior without yet owning backend
textures or existing UWindow input.

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
  are explicit. Native OpenXR and WebXR provider skeletons are integrated, and
  both provider controller lanes now have semantic snapshots. XR-common spaces,
  pointer hits, haptic routing, and provider-neutral UI-surface policy are
  integrated. Live WebXR input consumption, OpenXR-to-XRCommon adaptation, and
  existing menu/cinematic render/input adapters remain separate topics.

The upstream repository is active. `Docs/UpstreamCoordination.md` records the
public Discord contact route, the relationship to SurrealGPU/SurrealWidgets,
and the human-curated bugfix-first PR strategy derived from upstream review of
the rejected combined VR pull request. No upstream contact or PR has been made.

## Next integration gates

1. Validate the WebXR WebGPU projection layer on Quest hardware and the native
   OpenXR controller lifecycle on a physical headset.
2. Refactor native OpenXR snapshots onto XR-common types and connect the tested
   WebXR snapshot/adapter to the real browser frame input path without changing
   keyboard or mouse ownership.
3. Connect the tested UI-surface policy to captured/replayed HUD, menu,
   intro/cinematics, and loading targets plus existing UWindow mouse primitives.
   Providers must submit the ordered quads and render the pointer laser from the
   exact same ray used for hit testing.
4. Package the same shared browser launcher for flat and WebXR deployments;
   publish it under `/webxr/Ports/SurrealEngine/` only after the runtime and
   legal-data gates pass. Electron remains an optional flat wrapper.
5. Run the native flat, native OpenXR, flat WebGPU, WebXR, Deus Ex, Unreal Gold,
   and deterministic-bot regression matrix before retiring any old worktree.
