# Deus Ex unification handoff — 2026-07-22

This checkpoint preserves the Deus Ex feature line before the unified-engine
work begins. Do not continue compatibility work from this worktree until it is
explicitly resumed under the ownership boundaries in the central unification
plan.

## Branch and checkpoint

- Branch: `deus-ex-support`
- Full implementation HEAD before this handoff-only commit:
  `c17a758673611bfdaa35ee9860080a5987555d66`
- Latest implementation commit: `Keep background validation from retaining
  focus`

## Remote preservation

The commit remains local. No push was attempted because the only configured
remote is:

```text
origin  https://github.com/dpjudas/SurrealEngine.git (fetch)
origin  https://github.com/dpjudas/SurrealEngine.git (push)
```

That remote is upstream and is prohibited for preservation. No separately
configured `kblood/SurrealEngine` fork remote exists in this clone. Configure
and verify that fork before pushing, then push `deus-ex-support` without force.

## Git status

Immediately before creating this handoff file, `git status --short
--untracked-files=all` was empty. There were no remaining modified or untracked
files. This handoff file is intentionally committed as a documentation-only
checkpoint, so the expected status after its commit is also clean.

Generated and proprietary evidence is ignored rather than shown by Git status;
its locations are listed below.

## Verification

The final source was built with:

```powershell
cmake --build build --config Release -j 4 --target SurrealEngine
```

Result: success; `build/Release/SurrealEngine.exe` linked successfully.

All existing Deus Ex-related CTest targets were run with:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Result: 5/5 passed (`ActorMovement`, `DXTextTokenizer`, `DXAIPerception`,
`DXSavePath`, and `DXPropertySerialization`).

The last source change hardens `--noactivate` for the whole window lifetime.
Four independent silent QuickSave reloads used `--nosound --noactivate` and a
per-process log. Across 982 foreground samples, the game was foreground zero
times; there were zero unresponsive samples. The final clean-binary run
accounted for 245 of those samples and closed normally.

## Last validated gameplay boundary

Training is validated through normal title-menu entry, the reception nanokey,
the upper-door unlock/open/cross, QuickSave restoration beyond that door, and
reconstruction of transient conversation bindings. A fresh reload plays the
intended `dl_start` Jaime Reyes transmission.

The next stock progression requirement was identified but not completed: a
second `NanoKey0` remains near `(415,1072)` on the lower landing and unlocks the
`keydoor` mover pair near `(352,576)` and `(416,576)`. The key was not collected
after the pause instruction, and the crate/lockpick exercise was not entered.
The dark surfaces reached during calibration were ordinary geometry, not a
confirmed engine defect.

Campaign validation has reached Liberty Island world/HUD rendering and the
save/create/load/overwrite/delete lifecycle. Scripted campaign travel and full
mission progression have not been validated. The pre-Training Liberty
QuickSave remains archived locally.

## Local evidence intentionally not committed

- `build/dx-training-*`: targeted-input scripts, per-run logs, PID markers, and
  `PrintWindow` captures. Some captures contain commercial game imagery.
- `build/test-saves/*`: generated save-system fixtures and the preserved
  `QuickSave-liberty-before-training-20260722-1335` archive.
- `C:\Program Files (x86)\GOG Galaxy\Games\Deus Ex GOTY\Save\QuickSave`:
  the active local Training QuickSave plus retained Liberty map data.
- Other `build` products, CMake output, and local validation logs remain ignored
  generated evidence.

No Surreal Engine game or validation process from this worktree remained
running at handoff time. Other worktrees were not modified or stopped.

## Exact next task when resumed

First replay this commit series into the unified tree according to the shared
game-support boundary. After the generic/core fixes and Deus Ex module are
separated and the existing tests pass there, resume behavior validation from
the Training QuickSave: collect the second nanokey, return to `keydoor`, open
it through stock script, and isolate only the first reproducible blocker in the
crate/lockpick exercise.

## Generic UE1 fixes versus Deus Ex support

Generic UE1/core work should remain independently reviewable. This branch
contains generic changes including actor movement/collision corrections,
dynamic-array/property serialization fixes, window/edit/list behavior, save
package mechanics, and the opt-in silent/non-activating validation path. These
belong in core or platform ownership when they are not tied to Deus Ex classes.

Deus Ex-specific work includes game detection and property/native registration,
text tokenization and paging, AI-perception semantics, `DeusExSaveInfo` and
signed-slot behavior, Deus Ex UI integration, save-menu behavior, Training
documentation, and reconstruction of transient `ConListItems` for saved Deus Ex
actors. These belong behind the Deus Ex game-support module boundary. No
commercial packages, saves, screenshots, or copied configuration files belong
in either layer.
