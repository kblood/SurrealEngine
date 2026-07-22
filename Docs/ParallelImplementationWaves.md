# Parallel implementation waves

This schedule keeps four execution slots productive without assigning two
agents to overlapping engine files before their shared dependency is stable.
The root thread owns review, integration, cross-platform builds, documentation,
and branch preservation. Three subagents own isolated topic worktrees.

## Scheduling rules

1. A topic branch starts from upstream or its smallest declared `pr/*`
   dependency, never from `integration/unified-engine`.
2. Agents treat release and checkpoint branches as read-only evidence.
3. A provider is not assigned until the provider-neutral contract it consumes
   has passed standalone review.
4. Each agent returns a clean commit, exact dependency, validation evidence,
   source-commit mapping, and remaining limitations. Root reviews before push.
5. Integration conflicts are resolved only on `integration/unified-engine`;
   topic history is not polluted with unrelated combined changes.
6. When one agent finishes early, it receives the next task whose dependencies
   are already complete. It does not start speculative work from an unstable
   sibling branch.

## Wave 2: completed

Three independent tasks run in parallel:

| Lane | Branch dependency | Deliverable | Unblocks |
| --- | --- | --- | --- |
| Presentation | `pr/view-family` | output target/layer seam, explicit world/overlay/UI layers, provider-neutral stereo diagnostic | OpenXR, WebXR, VR menu/cinematics |
| Deus Ex text | `pr/game-support-registry` | tokenizer and paging module plus focused tests | Deus Ex conversations/training |
| Bot benchmark | `pr/headless-benchmark-driver` | controlled deterministic map/login/tick driver without behavior changes | fixtures, telemetry, parity validation |

Acceptance requires standalone Release builds, focused tests or diagnostics,
clean topic worktrees, combined native build/tests, and an Emscripten link when
the changed engine surface is compiled for the web.

## Wave 3: current provider and deployment extraction

Start only after the presentation topic is reviewed:

| Lane | Dependencies | Source evidence | Scope |
| --- | --- | --- | --- |
| Native OpenXR provider | frame, view, presentation | `vr-m2` through the real session/swapchain/frame-loop and per-eye pose commits | OpenXR session, swapchains, and view/target translation; exclude controller input, UT weapons, and menu policy |
| WebXR provider | web platform, frame, view, presentation | `webxr-m1` session, packed frame/view bridge, lifecycle, pose and projection commits | browser session lifecycle and provider translation; exclude controller input and retain flat WebGPU fallback |
| Browser data/persistence | web platform only | importer, mutable-data persistence, PWA and recovery commits in `webxr-m1` | legal local data import, IndexedDB/OPFS persistence, recovery and schema migration; no XR requirement |

Provider branches may be staged on temporary composed bases for validation, but
their own commits must remain limited to the declared provider. OpenXR and
WebXR code must translate into `ViewFamily` and presentation output/layers
rather than introducing parallel gameplay paths. Controller input follows in
separate provider-adapter topics and must use `InputComposition`.

## Ready queue and slot reuse

The root keeps the next work dependency-ordered so the first available agent
can start useful work immediately:

1. `pr/deus-ex-ai-perception`: replay only the pure visibility, hearing, and
   attitude calculations from source commit `e7ff4b05`, with synthetic tests;
   keep native registration as a separate follow-up.
2. `pr/bot-benchmark-telemetry`: add an immutable run manifest and bounded,
   versioned telemetry records on top of `pr/bot-benchmark-driver`; do not
   change bot behavior or copy captured commercial-game data.
3. `pr/openxr-input-adapter`: after the native provider is reviewed, translate
   OpenXR actions and poses into independently owned `InputComposition`
   sources; no weapon or menu policy.
4. `pr/webxr-input-adapter`: after the web provider is reviewed, translate the
   browser controller snapshot into the same controls and pose contract while
   retaining mouse/keyboard and flat mode.
5. `pr/xr-common-spaces`: only after both providers are integrated, define the
   shared head/aim/grip spaces, lifecycle state, pointer hit result, and haptic
   event interface consumed by both adapters.

Items 1 and 2 are independent of Wave 3 and therefore take the first two freed
slots. Items 3 and 4 deliberately wait for provider review so their branches
do not encode an unstable session API. Item 5 waits for evidence from both
providers rather than favoring the shape of either implementation.

## Wave 4: shared XR behavior and game profiles

After both provider skeletons can present tracked views:

1. Extract XR-common pose spaces, locomotion reference, haptic event routing,
   tracked audio listener, pointer/raycast result, and session lifecycle state.
2. Implement captured/replayed UI surfaces for menu, intro/cinematics, HUD, and
   loading. UI is rendered after the world, is never depth-hidden by its own
   backing quad, and accepts mouse plus either tracked controller.
3. Reconstruct the UT99 VR game profile: controller-relative weapon visuals,
   fire/projectile aiming hooks, two-hand grip, handedness, dual Enforcers, and
   outcome-based haptics.
4. Add an Unreal Gold profile only for proven differences; otherwise reuse the
   UT/Unreal-compatible XR-common behavior.

The release branch remains available for users throughout migration. It is not
rebased onto unfinished architecture.

## Continuing Deus Ex and bot work

Provider work does not block these lanes. As slots become available:

- Deus Ex: split pure AI-perception math, then native registration; extract
  package/source-table preservation; add save/game-directory behavior; finally
  reconstruct conversation state through the VM/post-load boundary.
- Bots: add immutable manifests and telemetry, reconstruct controlled
  reachability/HitWall/death fixtures, then replay individual engine fixes with
  evidence. Behavior tuning begins only after retail capture analysis gives a
  falsifiable target.

## Integration gates

Every wave must keep these modes viable:

- native D3D11 flat;
- native Vulkan flat;
- Emscripten/WebGPU flat without WebXR;
- focused deterministic/headless tests; and
- existing UT99 VR release branch as an independently releasable checkpoint.

OpenXR and WebXR runtime gates are added when their provider branches begin.
No old feature worktree is archived until its source mapping shows that all
useful behavior is either extracted, deliberately rejected, or recorded as a
known remaining item.
