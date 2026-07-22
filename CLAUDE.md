# Repository instructions

This is the canonical instruction file for AI-assisted work in this Surreal
Engine fork. `AGENTS.md` points here so contribution rules are maintained in one
place.

## Project shape

This fork develops native desktop, native OpenXR, WebAssembly/WebGPU, optional
WebXR, additional Unreal Engine 1 game support, and deterministic bot testing on
shared engine foundations. Desktop behavior must remain the default. Platform
and XR providers translate into provider-neutral engine contracts; they do not
fork gameplay, VM, menu, or game-support implementations.

Read these before changing architecture or preparing upstream work:

- `Docs/UnifiedEngine.md`
- `Docs/ParallelImplementationWaves.md`
- `Docs/UpstreamCoordination.md`

## Branches

- `integration/unified-engine` is the product integration and cross-feature
  validation branch. It must never be used as the source of an upstream PR.
- `pr/*` branches are focused topics based on current upstream or their smallest
  declared dependency. Keep unrelated work and integration documentation out.
- Release and preserved feature branches are evidence. Reconstruct useful work
  into focused topics instead of merging those histories wholesale.

## Upstream contribution policy

Magnus Norddahl (`dpjudas`) clarified on the Surreal Engine Discord on
2026-07-22 that large AI refactoring is not the direction he wants for the
project. Bug fixes and improvements can be accepted when the PR is small enough
to evaluate and the contributor personally understands what it does to the
codebase. AI assistance is not itself prohibited; unowned or unreviewed AI
output is unacceptable.

Apply that as a hard gate for every upstream candidate:

1. Confirm the problem still exists on current `dpjudas/master`.
2. Produce a concise reproduction and observable before/after behavior.
3. Understand and inspect every changed line. Be able to explain why it belongs
   and what assumptions it makes.
4. Limit the PR to one correction or independently useful improvement. Remove
   unrelated formatting, refactors, planning files, and generated history.
5. Add a focused regression where practical and manually test the affected game
   behavior. Passing tests do not replace gameplay evidence.
6. Document risks, preserved behavior, and non-goals. Sensitive collision,
   Canvas, VM, serialization, and renderer changes require especially strong
   evidence.
7. Human-curate the commits and PR description. Do not ask upstream to review a
   raw agent branch or the integration branch.

Do not open an upstream PR, post externally, or contact a maintainer without the
user explicitly asking. Preparing and auditing a candidate does not grant that
permission.

## Working rules

- Preserve desktop keyboard, mouse, launcher, rendering, and non-XR execution
  while adding optional providers.
- Keep provider handles and APIs outside provider-neutral contracts.
- Prefer small commits with concise messages. Do not add AI/model/session
  attribution, generated prose, bug-tracker identifiers, or cryptic labels to
  commit messages or code comments.
- Be sparse with comments; explain only behavior that the code cannot make
  clear.
- Preserve unrelated user changes and inspect the worktree before editing.
- Build and run the narrowest relevant tests first, then the appropriate native,
  OpenXR, Emscripten, or browser integration gates from the documentation.
- Do not claim an upstream candidate is ready until the manual-understanding,
  current-upstream, reproduction, and validation gates above are recorded.

