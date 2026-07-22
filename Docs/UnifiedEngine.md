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
| `pr/view-family` | `9a88d556` | Render an explicit family of camera views with optional asymmetric projections | `pr/frame-pipeline` |
| `pr/input-composition` | `09bd2737` | Compose buttons and axes from independent input sources | current upstream |
| `pr/vm-hook-registry` | `d0faef6d` | Ordered, scoped VM-call extension hooks with safe argument replacement | current upstream |
| `pr/game-support-registry` | `ff175b0a` | Typed game identity, behavior capabilities, and per-game native registration | current upstream |

The integration branch contains equivalent cherry-picked commits at
`0ba840c7`, `121428f5`, `f081ab56`, `91e77db4`, and `7e208406`.
All five topic branches and `integration/unified-engine` are preserved on the
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
- links `SurrealEngine.exe`; and
- passes the `SurrealEngine.exe --help` smoke test.

Runtime multi-view rendering still needs a stereo diagnostic/provider. Render
target/layer selection is intentionally deferred to a backend interface, and
the policy for per-eye weapons plus captured/replayed UI is not yet defined.

## Active extraction lanes

- Web: reconstruct a flat WASM/WebGPU platform topic from the checkpointed
  WebXR branch, with no WebXR requirement.
- Deus Ex: replay the clean compatibility work as a sequence of generic and
  game-specific PR topics on top of the game-support boundary where needed.
- Bots: extract deterministic clock/random and opt-in benchmark infrastructure
  before reconstructing behavior fixes backed by captured evidence.
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
