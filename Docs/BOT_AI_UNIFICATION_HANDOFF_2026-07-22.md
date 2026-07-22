# Bot AI unification handoff — 2026-07-22

## Preservation identity

- Branch: `bot-ai-parity`
- Full source/test checkpoint HEAD before this handoff-only commit:
  `e853cf6bc00b3ac9413dd2b92ac82dbb66832b5c`
- Safe preservation remote: `fork` = `https://github.com/kblood/SurrealEngine.git`
- Pushed ref: `refs/heads/bot-ai-parity`
- Forbidden/unmodified remote: `origin` = `https://github.com/dpjudas/SurrealEngine.git`
- No merge, rebase, history rewrite, force-push, ancestry cleanup, or edit to
  another worktree was performed.

The handoff document is appended as a documentation-only commit, so the final
branch HEAD after that commit is reported by the pausing agent alongside this
path. The hash above is the exact checkpoint containing all intended engine,
benchmark, validator, and test work.

## Git status and remaining files

Immediately before creating this file, both `git status --short --branch
--untracked-files=all` and porcelain-v2 status showed a clean
`bot-ai-parity` worktree. After committing this handoff, no intended modified or
untracked source file should remain.

Complete remaining modified/untracked-file list inside this Git worktree:

- none.

Intentionally ignored local evidence/build data remains on disk and is not
source material:

- `build/`: generated Release build output;
- `botbench-*/`: benchmark, fixture, preflight, diagnostic, and analysis output;
- `q1-capture-*/`: raw Q1 matrices, traces, summaries, validator reports, and
  immutable manifests;
- `retail-oracle-script-smoke/` and `retail-touch-probe/`: generated retail
  oracle evidence.

External living research documents are outside this repository and therefore
cannot appear in this branch's Git status. They were updated through the pause:

- `C:\Devstuff\QuestGames\research\surreal-bots\README.md`;
- `C:\Devstuff\QuestGames\research\surreal-bots\IMPLEMENTATION_PROGRESS.md`;
- `C:\Devstuff\QuestGames\research\surreal-bots\results\skill-qualification-capture\README.md`;
- `C:\Devstuff\QuestGames\research\surreal-bots\results\skill-qualification-q1\README.md`;
- the focused agent audit/design notes linked from those indexes.

No commercial game package, retail INI, imported game data, screenshot, log,
capture artifact, or build output is committed.

## Tests and accepted runtime checks

Most recent focused pause test:

```powershell
python -m unittest Tools.BotBenchmark.tests.test_validate_bot_trace_qualification -v
```

Result: 9/9 passed on 2026-07-22.

Most recent complete discovery after the nested-damage validator correction:

```powershell
python -m unittest discover -s Tools/BotBenchmark/tests -p 'test_*.py' -v
```

Result: 73 tests discovered, 72 passed, one Windows symlink-capability test
skipped, zero failures.

Most recent engine/fixture verification before Q1:

```powershell
cmake --build build --config Release --target SurrealEngine --parallel 1
```

Result: Release build passed. The immutable controlled-death protocol then
passed 2/2 at digest `6ec8f05918b0fd98`; the final engine SHA-256 used by Q1
was `62d3da25feabb8f59759bd9b0d37ad9ee1198f79357a37e91a808001b949f833`.

## Q1 capture status

The clean v2 adjacent-tier capture is complete but deliberately unanalyzed.
All 14 matrices passed 72/72 rows with zero failed runs, zero nondeterministic
cases, and immutable identity reverified: 1,008/1,008 valid orientation trials.
Every root binds engine SHA-256 `62d3da25feabb8f59759bd9b0d37ad9ee1198f79357a37e91a808001b949f833`
and content-manifest SHA-256
`f9b41dca6f04a5f01fb3ee2a9c8b20075761393afb4e9e00b3db72cb70ed2414`.

Completed clean-v2 directories:

| Pair/role | Matrix directory | Matrix SHA-256 |
|---|---|---|
| high 1v0 | `q1-capture-v2-high-1v0\botbench-matrix-20260722-130049-pid58452` | `1509aef5bf0cc7f67c886d704589ad7a6311474f5d607d364e9abb135425c9df` |
| low 1v0 | `q1-capture-v2-low-1v0\botbench-matrix-20260722-132621-pid58144` | `8af744d38b43dcb30a610a654f8a4e0410fa50578c53149a3e61a28eeaf8c74d` |
| high 2v1 | `q1-capture-v2-high-2v1\botbench-matrix-20260722-130907-pid65312` | `6c405e627f03300edfd95889eb1b78f82a1d6a0da6ee55ad7ceb9c63e47a6a7f` |
| low 2v1 | `q1-capture-v2-low-2v1\botbench-matrix-20260722-130059-pid69096` | `d96219239849d57590b8b3940ddf7fa777350d2415d91b7ca18e231d91b338a0` |
| high 3v2 | `q1-capture-v2-high-3v2\botbench-matrix-20260722-131917-pid69684` | `9d54a85f3c502a209718c646e60a5f45d4d09c91cc5388e74eeee6979cd07a80` |
| low 3v2 | `q1-capture-v2-low-3v2\botbench-matrix-20260722-131106-pid60360` | `c484ea25db31040d99338c7fd1cb54e9637b552a04000720a1fddfea2e4810af` |
| high 4v3 | `q1-capture-v2-high-4v3\botbench-matrix-20260722-132851-pid57984` | `0a56f077dc74f48217b9445994626534dd770e76a845703a8d0ce88e6b749e12` |
| low 4v3 | `q1-capture-v2-low-4v3\botbench-matrix-20260722-132010-pid55540` | `376f063e2bff2d637b39b2084d5c42db6875bb46ccd2bf97a4abf6c991adaa0e` |
| high 5v4 | `q1-capture-v2-high-5v4\botbench-matrix-20260722-130101-pid59172` | `bcfacd579b8baa8badf8a5cef487a346809ef73720d49dd1e2d804d062f3ad7c` |
| low 5v4 | `q1-capture-v2-low-5v4\botbench-matrix-20260722-130101-pid61876` | `a2b48c8367370529d0580e7da1e24589ec887f96da8aa6c66115593faa02fa8d` |
| high 6v5 | `q1-capture-v2-high-6v5\botbench-matrix-20260722-131004-pid55240` | `82c0aa3ce9bdf3112fd1624d3fbc946657cfc748dc4e243eb57f7d6117e3518c` |
| low 6v5 | `q1-capture-v2-low-6v5\botbench-matrix-20260722-130944-pid52828` | `4b87bf0c26df46f9e5513b668e87b1b97de72f36c004bd33087227a121b11160` |
| high 7v6 | `q1-capture-v2-high-7v6\botbench-matrix-20260722-131825-pid73668` | `2c0d8e6c28d23a172cc39ca59ea11313251026423b9068191c57da20b06aa32f` |
| low 7v6 | `q1-capture-v2-low-7v6\botbench-matrix-20260722-131724-pid66904` | `78c4066d39f1199d9abec06ce07e25f088c7b987a63b528b621607c9a28f8b2c` |

Incomplete clean-v2 directories: none.

The first wave is retained as diagnostic evidence and must not be mixed into
the final manifest. Its complete runner roots were:

- accepted under the stricter old validator: `q1-capture-high-1v0`,
  `q1-capture-high-2v1`, `q1-capture-low-2v1`, `q1-capture-low-5v4`;
- complete but rejected by the old emission-order false negative:
  `q1-capture-high-5v4`, `q1-capture-low-3v2`, `q1-capture-low-6v5`.

No Q1 qualification manifest, bootstrap analysis directory, adjacent-tier
verdict, Godlike anchor result, or skill-parity claim exists yet.

## Process state at pause

After the final atomic 4v3 capture completed:

- bot engine (`SurrealEngine.exe`) processes: none;
- bot matrix/capture PowerShell processes: none;
- bot validator/analyzer Python processes: none;
- active bot subagent capture tasks: none.

The workspace-wide audit observed an unrelated Emscripten/WebXR build process
in another worktree. It was not touched and is not part of this handoff.

## Change ownership for unification

### Generic engine correctness changes

- deterministic native/script random and fixed-clock behavior;
- stable simultaneous collision/contact ordering and contact-overflow parity;
- query-local navigation anchors, reachability wall-slide correction, node
  arrival envelope, actor-blocker `HitWall`, and mover candidate iteration;
- missing script-visible latent/runtime behavior and authoritative damage/death
  observation hooks.

These need reconstruction on current upstream as narrow, reproduced commits;
they should not be merged wholesale with the inherited VR ancestry.

### Bot behavior changes

- restored sight acquisition/loss callbacks and living-target selection;
- focus/strafe synchronization, latent sleep behavior, route cost/order,
  sleeping-pickup prediction, and stock movement/jump/fall contracts;
- stable Loque/Tamerlane profile selection and external per-bot skill control.

These require retail-oracle or controlled-scenario evidence and remain distinct
from generic engine correctness.

### Benchmark runtime

- opt-in deterministic fixed-step `BotBenchmark` lifecycle and spectator
  isolation;
- exact resource, traversal, combat, damage, death, skill, and profile
  telemetry;
- controlled reachability, `HitWall`, and death-outcome fixtures;
- fail-closed matrix/preflight protocols and immutable evidence layout.

The benchmark is inert unless explicitly selected. In the unified engine it
should become the optional headless driver, not remain a special branch in the
ordinary interactive loop.

### Validators and analysis

- structural trace validator with raw event/summary replay;
- controlled-fixture contracts and adversarial tests;
- exact 14-input adjacent-skill analyzer with clustered bootstrap, sign tests,
  Holm adjustment, provenance, and explicit non-Godlike scope;
- nested-damage completion-order correction at `e853cf6b`.

### Retail-oracle tooling

- disposable UT436 unattended oracle and installed-file manifest checks;
- contact-overflow probe and coarse benchmark-to-retail comparison.

Retail tooling and all commercial data stay outside engine/source artifacts.

## Exact next task after explicit resume

Do not launch another capture. Construct a 14-input `mode: qualification`
manifest using only the clean-v2 roots above; independently verify every label,
role, path, file SHA-256, engine/content identity, and 72-row Cartesian grid;
then run `Analyze-BotSkillQualification.py` with exactly 10,000 bootstrap draws
and bootstrap seed `7436991`. Document the Q1 result without claiming Godlike
parity. Use failed pair/map/role evidence to choose the next behavior slice.
Godlike anchors, retail calibration, acquisition, combat, and safety gates
remain separate even if Q1 passes.
