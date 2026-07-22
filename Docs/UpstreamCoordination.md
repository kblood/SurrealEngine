# Upstream coordination with dpjudas

Date: 2026-07-22

This note records how the native XR, WebAssembly/WebGPU/WebXR, Deus Ex, and bot
work should be presented to the original Surreal Engine maintainer. It is an
outreach plan, not permission to post or open a pull request.

## Public project and contact evidence

Magnus Norddahl (`dpjudas`) is actively maintaining
[`dpjudas/SurrealEngine`](https://github.com/dpjudas/SurrealEngine). The public
profile does not list an email address, website, or social account. The
[Surreal Engine README](https://github.com/dpjudas/SurrealEngine#discord-server)
does provide the project's public [Discord invitation](https://discord.gg/5AEry4s).
The repository has GitHub Issues and Discussions disabled, while pull requests
are active and regularly reviewed.

Use the project's public Discord development channel for the broad architecture
question, then use focused draft pull requests for concrete finished work. Do
not hijack another contributor's pull request, scrape a commit email, seek
non-public contact information, or send an unsolicited private message when a
public project channel is available.

## Most relevant sibling repositories

| Repository | Relationship to this work | Direction |
| --- | --- | --- |
| [`SurrealGPU`](https://github.com/dpjudas/SurrealGPU) | Already imported into Surreal Engine as a maintained Git subtree. Its new `GPU*` API resembles WebGPU concepts but its current implementation is Vulkan. It replaced the old vendored ZVulkan tree. | Ask whether the browser WebGPU backend should eventually implement this API or remain an engine render device. Do not perform that large migration without agreement. |
| [`SurrealWidgets`](https://github.com/dpjudas/SurrealWidgets) | Already imported as the engine's maintained widget/window subtree. The fork currently adds Emscripten resources and a `WebGPU` window enum that are not yet in the standalone repository. | Submit generally useful Emscripten/window changes to SurrealWidgets first, or explicitly ask whether a Surreal Engine-only patch is preferred. |
| [`WebCPP`](https://github.com/dpjudas/WebCPP) | C++/Emscripten DOM view framework with routing, layouts, browser requests, and CSS packaging. It uses CPPBuild. | Potential future launcher-shell option, but not useful for in-world XR rendering. Keep the current small HTML/JavaScript launcher until the maintainer states that WebCPP convergence is wanted. |
| [`CPPBuild`](https://github.com/dpjudas/CPPBuild) | Build/project generator required by WebCPP and used by the standalone utility libraries. | Do not add it as a Surreal Engine build dependency merely for the launcher; CMake and the existing `Configure.js` path already build the engine targets. |
| [`ZWidget`](https://github.com/dpjudas/ZWidget) | Older/separate cross-platform UI framework. Surreal Engine now vendors SurrealWidgets instead. | Reference only. Do not create a second UI dependency. |
| [`ZVulkan`](https://github.com/dpjudas/ZVulkan) | Older Vulkan framework. Surreal Engine explicitly removed its vendored tree when SurrealGPU arrived. | Do not restore it. Use SurrealGPU for native Vulkan integration. |
| [`UT99VulkanDrv`](https://github.com/dpjudas/UT99VulkanDrv) | Archived renderer for the original UT99 executable, not the reconstructed engine. | Reference rendering behavior and compatibility choices only; do not make it a dependency. |
| [`UICore`](https://github.com/dpjudas/UICore) | Legacy UI/graphics toolkit last updated in 2019. | Superseded for this project; do not adopt. |

## Evidence from recent upstream reviews

Upstream pull request
[`#288`](https://github.com/dpjudas/SurrealEngine/pull/288) combined VR,
save/load, collision, VM, launcher, canvas, and refactor work. Magnus rejected
that shape even though some fixes were useful. His review specifically called
out:

- unrelated behavioral areas combined into one trust-heavy change;
- fixes whose intent and real-game evidence were unclear;
- changes to sensitive collision and Canvas behavior without isolated proof;
- Vulkan handles leaking through a supposedly generic VR abstraction;
- unattended-launch behavior that did not follow existing engine conventions;
- machine-produced commit segmentation instead of human-curated review units.

The contributor then split bug fixes into smaller pull requests. Focused work
was accepted quickly. Pull request
[`#289`](https://github.com/dpjudas/SurrealEngine/pull/289) was manually merged,
with the maintainer explicitly removing noisy AI-generated commit text.

Our branch strategy addresses the structural criticism, but the commits and PR
descriptions still require human review before submission. Passing tests do not
replace an explanation of why the behavior is correct in UT99 and Unreal Gold.

## Recommended upstream sequence

Do not open an integration-branch pull request. Establish a review relationship
with one small, proven correctness fix before asking the maintainer to evaluate
speculative engine seams.

1. Reconstruct `pr/deus-ex-runtime-fix` as a generic cardinal-axis actor
   movement fix. Include exact walk/swim/fly reproduction and a meaningful
   movement regression, not the branch's planning/handoff prose. Do not call it
   a Deus Ex architecture PR.
2. Follow, if useful, with the aggregate-boolean serialization correction from
   `pr/property-serialization`, backed by a round-trip/package test.
3. Offer `pr/input-composition` only after replacing XR-specific source names
   in the shared layer and proving overlapping sources, disconnect cleanup, and
   unchanged desktop behavior.
4. Offer `pr/frame-pipeline` only after a manual extraction that makes preserved
   call order easy to review and adds UT99/Unreal Gold desktop evidence. The
   current large moved block is too trust-heavy as a first architectural PR.
5. If those seams are accepted, offer `pr/view-family`, then
   `pr/presentation-layers`, then target binding. Do not include OpenXR in those
   PRs.
6. Ask explicitly where a WebGPU backend belongs relative to SurrealGPU before
   submitting `pr/web-platform-foundation`, and whether Emscripten additions
   under `SurrealWidgets/` should first target that standalone repository.
7. Submit OpenXR and WebXR only after their provider-neutral dependencies are
   accepted. Keep controller adapters, UI surfaces, weapons, locomotion, and
   game profiles separate. Game correctness and bot test work remain unrelated
   PR series.

## Proposed Discord message

Post this in the server's appropriate development channel and link the detailed
roadmap only if requested:

> Hi Magnus. I am maintaining a Surreal Engine fork that experiments with native
> OpenXR, flat WASM/WebGPU, optional WebXR, Deus Ex support, and bot test work. I
> read your review of PR #288 and agree that none of this should arrive as one
> feature branch.
>
> We have been reconstructing it as independent, provider-neutral topics with
> desktop behavior left as the default: frame phases, view families,
> presentation layers, input-source composition, and only then separate OpenXR
> and WebXR providers. Platform handles do not enter the shared contracts. The
> work has been AI-assisted, but I will own and manually review every submitted
> diff and present human-curated PRs rather than raw agent output.
>
> Before submitting architectural changes, I would value your direction on
> three boundaries: should browser WebGPU initially remain a Surreal Engine
> RenderDevice or target the newer SurrealGPU API; should generic Emscripten
> window changes go through SurrealWidgets first; and does a frame/view/layer
> seam fit the direction you want for the engine? I plan to begin GitHub work
> with one small proven gameplay correctness fix, not XR.

Keep the Discord conversation at the boundary/PR-order level. Do not paste the
full roadmap or source dump. A first bug-fix PR should discuss only its own
reproduction, root cause, fix, test, and non-goals.

## Pre-contact gate

Before contacting upstream:

- rebase or replay the chosen first topic on current `origin/master`;
- inspect every changed line manually;
- remove generated handoff prose from the commit message and PR body;
- state the observable behavior preserved or corrected;
- provide the shortest reproducible build/test command;
- verify that the topic contains no integration-only files or unrelated docs;
- disclose AI assistance without asking the maintainer to audit raw agent work;
- ask the repository owner for architectural direction rather than presenting a
  completed multi-year roadmap as a fait accompli.
