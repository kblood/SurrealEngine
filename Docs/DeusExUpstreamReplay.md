# Deus Ex upstream replay

This branch is the first upstream-sized extraction from the preserved
`deus-ex-support` line. The source line remains read-only at
`e2807b671a3d7367b8dd723b0f78578b13f14b47`.

## Current slice

`Fix cardinal-axis actor movement` is a generic UE1 runtime correction. The
walking path previously ran only when both horizontal velocity components were
nonzero. Swimming and flying used the same two-axis condition, so they also
ignored vertical-only motion. The new predicates accept movement on any axis
that is meaningful to the physics mode, with focused regression coverage for
stationary, cardinal-axis, diagonal, and vertical-only inputs.

This slice is based directly on `origin/master` at
`891082d9f05a0f7b9ffcb4f65c9653f36fce5613`. It has no dependency on the game
support registry or on Deus Ex-specific code. It can be reviewed and merged as
an independent upstream PR. The integration branch may merge it alongside
`pr/game-support-registry` in either order.

Validation on Windows x64 Release:

- `ActorMovementTests`: passed (1/1 through CTest)
- `SurrealEngine` target: compiled and linked
- `SurrealEngine.exe --help`: completed and printed usage

## Preserved source-series audit

There are fourteen commits after the upstream base, including five documentation
checkpoints. They should not be replayed as a single chain.

| Source commit | Area | Replay direction |
| --- | --- | --- |
| `0b5cd166` | Compatibility baseline docs | Do not replay verbatim; rewrite after the extracted slices land. |
| `3ef3a35e` | Deus Ex text parser fixes | Superseded in large part by `44f04e20`; review together. |
| `67f7685b` | Native token reference docs | Retain as research evidence; summarize without proprietary reference material. |
| `44f04e20` | Text tokenizer and paging | Extract as a game-specific module slice with its existing tokenizer tests; layer on the game-support registry. |
| `e7ff4b05` | AI perception natives | Split pure perception math from Deus Ex native/property glue; layer the glue on the game-support registry. |
| `91e56302` | Save flow, UI/edit behavior, launch flags, validation | Do not cherry-pick. Split into generic UI primitives, generic opt-in launch/platform behavior, and a Deus Ex save-path/game-directory slice. |
| `27644b50` | Package/property writing, snapshots, list UI, save completion | Do not cherry-pick. Extract property serialization with its regression test first, then package save mechanics, snapshots, and list behavior separately. |
| `861ea698` | Cardinal-axis movement | Reconstructed by the current independent slice. |
| `5aa6699e` | Initial no-activate window behavior | Combine with the relevant pieces of `91e56302` and `c17a7586` as one opt-in platform PR. |
| `ee48a00f` | Training checkpoint docs | Keep as local validation evidence; do not replay as engine source. |
| `f26d9773` | Conversation reconstruction after save load | Rework as a Deus Ex post-load hook registered through the provider-neutral hook boundary; do not put a direct game conditional back into `Engine`. |
| `cfbf2367` | Training orientation docs | Keep as local validation evidence. |
| `c17a7586` | Lifetime no-activate enforcement | Combine with the earlier launch/window pieces, preserving opt-in desktop behavior. |
| `e2807b67` | Unification handoff | Keep only on the preserved feature line. |

## Recommended next slices

1. Extract the generic property-serialization correction and
   `DXPropertySerializationTests` from `27644b50`, renaming the test to describe
   the engine behavior rather than the discovering game.
2. Replay the tokenizer/paging implementation from `44f04e20` on top of the
   game-support registry as the first explicitly Deus Ex-owned module.
3. Split and replay pure AI-perception math before registering the Deus Ex
   natives that consume it.
4. Decompose `91e56302` and `27644b50`; keep save/package mechanisms separate
   from edit/list widgets and from platform validation flags.
5. Implement `f26d9773` through the shared post-load/hook registry once that
   boundary is available.

Game-specific replay branches should depend on `pr/game-support-registry`.
Generic runtime, serialization, UI, and platform fixes should continue to start
from current upstream master so each can be proposed independently.
