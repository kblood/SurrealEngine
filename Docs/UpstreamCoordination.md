# Upstream coordination with dpjudas

Date: 2026-07-22

This note records how the native XR, WebAssembly/WebGPU/WebXR, Deus Ex, and bot
work should be presented to the original Surreal Engine maintainer. Contact was
made in the public project Discord on 2026-07-22. This document records the
resulting contribution policy; it is not permission to post or open a pull
request.

## Public project and contact evidence

Magnus Norddahl (`dpjudas`) is actively maintaining
[`dpjudas/SurrealEngine`](https://github.com/dpjudas/SurrealEngine). The public
profile does not list an email address, website, or social account. The
[Surreal Engine README](https://github.com/dpjudas/SurrealEngine#discord-server)
does provide the project's public [Discord invitation](https://discord.gg/5AEry4s).
The repository has GitHub Issues and Discussions disabled, while pull requests
are active and regularly reviewed.

Use the project's public Discord development channel only when further broad
direction is genuinely needed, then use focused pull requests for concrete
finished work. Do not hijack another contributor's pull request, scrape a
commit email, seek non-public contact information, or send an unsolicited
private message when a public project channel is available.

## Maintainer response and governing policy

Magnus responded that large AI refactoring is not where he wants to take
Surreal Engine. He also clarified that the project has no formal task backlog,
but bug fixes and improvements are welcome when each PR is small enough for the
maintainers to evaluate and decide whether they trust it. His primary AI concern
is contributors submitting changes they do not understand. He explicitly
confirmed that AI-assisted contributions can be accepted when that is not the
case.

Treat this as a human-ownership requirement, not merely a diff-size limit. The
contributor must personally understand every changed line, reproduce the
problem on current upstream, explain the correction and its assumptions, and
verify the claimed behavior. An agent may investigate, implement, and test, but
its output is evidence to review, not an upstream-ready contribution by
default.

Maintain two separate tracks:

1. `integration/unified-engine` is the experimental product branch where the
   features can be exercised together. It is never an upstream PR source.
2. Each upstream candidate starts from current `dpjudas/master` and contains
   one independently useful, human-reviewed correction or improvement.

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

## Upstream candidate gate

Before proposing any upstream PR:

- rebase or replay the chosen first topic on current `origin/master`;
- inspect every changed line manually;
- remove generated handoff prose from the commit message and PR body;
- state the observable behavior preserved or corrected;
- provide the shortest reproducible build/test command;
- verify that the topic contains no integration-only files or unrelated docs;
- verify manually that the user understands every changed line and can answer
  review questions about the mechanism and tradeoffs;
- disclose AI assistance truthfully when relevant without asking the maintainer
  to audit raw agent work;
- keep experimental architecture on the fork unless a small independent seam
  has clear upstream value and sufficient evidence.
