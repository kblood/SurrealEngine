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

The integration branch contains the reviewed equivalents of every completed
topic above, including the small build-system follow-ups needed when those
topics coexist. All thirteen completed topic branches and
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
and clean shutdown. Its XR symbol/path audit returned no matches.

Runtime multi-view rendering still needs a stereo diagnostic/provider. Render
target/layer selection is intentionally deferred to a backend interface, and
the policy for per-eye weapons plus captured/replayed UI is not yet defined.

## Active extraction lanes

- Web: the flat WASM/WebGPU platform is extracted and verified with no WebXR
  requirement; the next web topic is browser-side legal data import and
  persistence, while WebXR remains a later provider.
- Deus Ex: generic actor movement and property serialization plus the first
  game-owned tokenizer/paging module are extracted; next are AI perception and
  save/package slices.
- Bots: deterministic runtime, bounded headless lifecycle, and the first
  controlled bot driver are extracted; next are display-free bootstrap,
  telemetry/fixtures, and then evidence-backed behavior fixes.
- XR: after the view target/layer and UI policy are explicit, migrate the
  released native OpenXR behavior into XR-common plus an OpenXR provider, then
  attach WebXR to the same contracts.

## Next integration gates

1. Add a render target/layer selection seam and a provider-neutral stereo
   diagnostic.
2. Define world, weapon, HUD, menu, intro/cinematic, and loading presentation
   layers. Menu and cinematic surfaces must render after the world and remain
   pointer-addressable.
3. Add input-source lifecycle tests for disconnect, WebXR exit/re-entry, and
   simultaneous mouse/controller use.
4. Integrate each extraction topic only after its standalone build and tests
   pass.
5. Run the native flat, native OpenXR, flat WebGPU, WebXR, Deus Ex, Unreal Gold,
   and deterministic-bot regression matrix before retiring any old worktree.
