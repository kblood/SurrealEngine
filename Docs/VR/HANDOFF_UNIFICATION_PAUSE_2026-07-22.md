# WebXR unification pause handoff — 2026-07-22

## Preservation identity

- Branch: `webxr-m1`
- Full feature-checkpoint HEAD before this document-only commit:
  `8576856bb155637208e6c34792c236290608d747`
- Last feature commit: `8576856b Add fail-closed actor HUD telemetry`
- Safe preservation remote: `fork`,
  `https://github.com/kblood/SurrealEngine.git`
- Pushed ref: `refs/heads/webxr-m1`
- Push result: a non-force fast-forward from `f8f457fe` through
  `8576856bb155637208e6c34792c236290608d747` succeeded.
- `origin` is `https://github.com/dpjudas/SurrealEngine.git`; nothing was
  pushed there.

This handoff file is necessarily committed after the feature-checkpoint hash
above. Its final document-only commit hash is reported with the handoff and is
pushed to the same safe fork/ref; a Git commit cannot contain its own hash.

The central plan
`C:\Devstuff\QuestGames\SURREAL_ENGINE_RELEASE_AND_UNIFICATION_PLAN.md` was
read and was not modified. No merge, rebase, native OpenXR modularization, or
other-worktree edit was performed.

## Final working-tree inventory before the handoff commit

There are no remaining modified tracked files. The complete untracked list is:

| Path | Why it remains untracked |
|---|---|
| `web/__pycache__/profile_webxr_matrix.cpython-313.pyc` | Generated Python bytecode from the performance matrix; never source. |
| `web/__pycache__/smoke_test_webxr.cpython-313.pyc` | Generated Python bytecode from browser smoke work; never source. |
| `web/__pycache__/test_webxr_performance_matrix.cpython-313.pyc` | Generated Python test bytecode; never source. |
| `web/__pycache__/webxr_performance_matrix.cpython-313.pyc` | Generated Python module bytecode; never source. |
| `web/check_orientation.py` | Pre-existing user diagnostic scratch script; intentionally untouched. |
| `web/index_webgpu_check.html` | Pre-existing user diagnostic scratch page; intentionally untouched. |
| `web/native_check_deck.png` | Pre-existing screenshot derived from commercial game assets; prohibited from commit. |
| `web/native_check_logo.png` | Pre-existing user screenshot/scratch evidence; prohibited from commit. |
| `web/webgpu_check_deck.png` | Pre-existing screenshot derived from commercial game assets; prohibited from commit. |
| `web/webgpu_check_logo.png` | Pre-existing user screenshot/scratch evidence; prohibited from commit. |

Build directories and browser profiles are generated/ignored and were not
added. No commercial package, imported user data, screenshot, log, browser
profile, release stage, or generated evidence was committed.

## Last atomic work preserved

The pause arrived while the six-map desktop/IWER profiler was running and just
after AH1 actor-HUD telemetry had begun. No new milestone was started after the
pause:

- The profiler wrote its fail-closed result. Three profiles completed and
  three maps (`DOM-Sesmar`, `AS-Overlord`, and `DM-Morpheus`) reported runtime
  errors during warmup. This is desktop Chrome + IWER evidence, not Quest
  performance evidence.
- The already-started AH1 edit was completed as one atomic commit only. It adds
  distinct `ActorWorld`, `ActorClipped`, and `Line3D` classifications, typed
  rejection telemetry, command limits, and deterministic self-test scaffolding.
  Actor replay remains suppressed with explicit `SnapshotUnavailable`; no raw
  UObject pointer is retained and AH2 was not begun.

Immediately preceding preserved commits include:

- `478c17e2` — fail-closed, default-off WebXR two-hand aiming with an empty
  production calibration table;
- `0694724b` — accessible/recoverable browser launcher and exact Brave/VDXR
  native route;
- `8dcd0985` — crash-safe mutable schema v1-to-v2 migration;
- `bf91dd90` — implementation-ready actor-HUD replay plan;
- `5714e662` — strict multi-map desktop/IWER performance matrix; and
- `34df8dcc` — unavailable-headset Brave/VDXR preflight evidence.

## Focused validation performed

Commands and outcomes relevant to the final work were:

```powershell
cd C:\Devstuff\QuestGames\ut99-vr\SurrealEngine\web
python -B -m unittest test_brave_vdxr_probe test_webxr_performance_matrix test_webxr_performance
```

Result: 28 tests passed.

```powershell
cd C:\Devstuff\QuestGames\ut99-vr\SurrealEngine
python -B web/smoke_test_mutable_persistence.py
python -B web/test_storage_robustness.py
```

Result: 13 deterministic mutable-persistence checks passed; 5 storage harness
contract tests passed. Separate disposable Chrome 150 and installed Brave 150
migration qualifications passed 7/7 each.

```powershell
python -B web/test_webxr_launcher.py
python -B web/test_webxr_launcher.py `
  --real-base-url http://localhost:8091 --real-build build-emscripten
```

Result: 76 deterministic launcher checks passed. The real data-backed launcher
reached `running`, advanced from tick 3 to tick 47, emitted no fatal log, had no
page error, and retained `offline-only` networking.

```powershell
cmake --build build --config Debug --target SurrealEngine
cmake --build build-emscripten --target SurrealEngine -j 4
cmake --build build-emscripten-nodata --target SurrealEngine -j 4
python -B -u web/smoke_test_webxr.py --experimental-webgpu-xr
```

Result for the two-hand checkpoint: native Debug, data-backed WASM, no-data
WASM, portable state/math self-tests, and the complete experimental smoke
passed. The packed frame recorded 201 draws, 1,181 differing stereo samples,
and zero GPU errors. Two-hand aiming defaults off, production metadata rows are
zero, and current weapons remain one-handed.

For final AH1, native Debug and Emscripten no-data builds passed, with only the
existing UProperty override and pthread/memory-growth warnings. `git show
--check 8576856b` also passed.

```powershell
python -B -u web/run_brave_vdxr_probe.py --preflight-only `
  --output C:\Devstuff\QuestGames\webxr-brave-vdxr-preflight-20260722.json
```

Result: an honest fail-closed preflight. Brave 150 booted the engine, selected
native mode, exposed `XRGPUBinding`, and found the configured Virtual Desktop
runtime, but `isSessionSupported("immersive-vr")` was false and VDXR reported
`XR_ERROR_FORM_FACTOR_UNAVAILABLE`. No headset presentation is claimed.

```powershell
python -B -u web/profile_webxr_matrix.py `
  --warmup 0.5 --duration 1 --sample-interval 0.25 `
  --output C:\Devstuff\QuestGames\webxr-iwer-matrix-20260722-m108.json
```

Result: aggregate FAIL, as required when any child fails. `DM-Deck16][`,
`CTF-Face`, and stress map `CTF-Darji16` produced complete child reports;
`DOM-Sesmar`, `AS-Overlord`, and `DM-Morpheus` failed during warmup. Do not use
this as Quest, thermal, or release performance evidence.

## Generated evidence intentionally outside Git

- `C:\Devstuff\QuestGames\webxr-brave-vdxr-preflight*.json`
- `C:\Devstuff\QuestGames\webxr-profile-m11-validation.json`
- `C:\Devstuff\QuestGames\webxr-profile-m11-validation.txt`
- `C:\Devstuff\QuestGames\webxr-iwer-matrix-20260722-m108.json`
- `C:\Devstuff\QuestGames\webxr-iwer-matrix-20260722-m108.txt`
- `C:\Devstuff\QuestGames\webxr-iwer-matrix-20260722-m108-reports\`
- `C:\Devstuff\QuestGames\webxr-release-stage-20260722-m103\`
- `C:\Devstuff\QuestGames\webxr-release-stage-20260722-m106\`
- `C:\Devstuff\QuestGames\webxr-release-stage-20260722-m107\`
- `%TEMP%\surreal-storage-robustness-*.json` and the owned disposable
  `%TEMP%\surreal-storage-robustness-*` profile directories.

These include synthetic reports and immutable release-stage output only. They
are not source. The staged output and screenshots are specifically excluded
from Git.

## Process state at pause

- No SurrealEngine process is running.
- No compiler, linker, CMake, Emscripten, Playwright test, profiler, or other
  task-owned test process is running.
- No task-owned Brave probe profile process was found. The user's ordinary
  Brave browser session remains open.
- Two intentionally retained Node development servers remain:
  - PID `62104`, `web/serve.mjs 8091`;
  - PID `69780`, `web/serve.mjs 8092`.
- Three idle Visual Studio MSBuild node-reuse workers remain (PIDs `50604`,
  `62376`, and `62448`). They are shared build-service processes, not active
  builds, and were not killed.

## Platform/provider ownership for unification

The following completed work belongs to the general flat WASM/WebGPU platform
and must remain usable without `navigator.xr`, `XRGPUBinding`, a headset, or
experimental browser flags:

- Emscripten/no-data build seams and the browser RAF host;
- flat WebGPU canvas rendering, pipeline/bind-group/buffer work, and ordinary
  one-view presentation;
- keyboard, mouse, and standard browser-gamepad support;
- Web Audio output and ordinary browser lifecycle handling;
- user-owned UT99 import/materialization, OPFS/IndexedDB storage, v1-to-v2
  mutable migration, settings/saves/log checkpoints, and clear/recovery UX;
- deliberate offline launcher, safe map picker, loading/crash diagnostics,
  PWA/offline/HTTPS hosting, and release staging/auditing; and
- flat-browser test and performance infrastructure where it does not inject XR.

The following completed work belongs only to the optional WebXR provider or to
provider-neutral XR common that must be extracted before reuse:

- immersive-session lifecycle, XR frame ownership, `XRGPUBinding`, projection
  layers/subimages, packed XR pose/view transport, and stereo presentation;
- tracked head/controller input, locomotion, turning, recenter, dominant-hand
  selection, XR haptics, safe exit, tracking-loss policy, and XR menu pointer;
- per-eye HUD/weapon presentation, XR clip-space orientation policy, controller
  weapon direction, visual grip, muzzle-origin safety, and default-off
  two-hand aiming; and
- IWER/native-XR lifecycle, orientation, input, haptic, device-loss, and
  headset-specific qualification harnesses.

The advance-once/render-views/finish-once seam, multi-view renderer, scoped VM
hooks, input actions, and listener/view composition are shared architectural
material. They must be renamed and moved behind provider-neutral contracts;
flat browser boot must not call through a WebXR requirement.

## Exact next task

Stop and wait. When explicitly resumed under the central unification plan, the
next task is to inventory and replay the general Emscripten + flat WebGPU
platform onto the designated unified integration branch while proving flat
keyboard/mouse/gamepad/audio/import/persistence/canvas boot without WebXR. Do
not resume AH2, begin another WebXR milestone, merge native OpenXR, or perform
feature-branch modularization unless new instructions explicitly assign that
work. WebXR must later attach through the optional neutral XR provider.
