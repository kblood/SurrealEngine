# Flat Web Platform Foundation Handoff

Date: 2026-07-22

## Scope

This branch extracts the provider-neutral browser platform from `webxr-m1`:

- Emscripten build and requestAnimationFrame lifecycle
- flat WebGPU `RenderDevice`
- null rendering, audio, and video fallbacks used during browser bring-up
- wasm32 correctness fixes
- optional local game-data preload (no commercial data is required to build)
- flat browser development server and smoke tests

It deliberately excludes OpenXR, WebXR sessions, stereo views, tracked input,
haptics, XR HUD/menu presentation, and XR weapon behavior. WebXR should be a
later provider layered on this browser platform.

## Baseline and dependency

- Upstream baseline: `origin/master` at `891082d9f05a0f7b9ffcb4f65c9653f36fce5613`
- Branch: `pr/web-platform-foundation`
- The first branch commit, `ca33e756`, is an exact tree-equivalent cherry-pick
  of `pr/frame-pipeline` commit
  `5dbcf833f55ce45a68801f00572f5d28ad3431f0`. `git diff` between those commits
  for `Engine.cpp` and `Engine.h` is empty.

When applying this work to an integration branch that already contains
`5dbcf833`, skip `ca33e756` and cherry-pick only the commits after it.

## Source provenance

The implementation was extracted from these `webxr-m1` commits:

- `f41cc57a` — original Emscripten and WebGPU backend
- `b3fe55c4` — wasm32 allocation, VM local initialization, and robustness fixes
- `b5ae3bb3` — original flat browser harness
- `9d19c791` — generated screenshot/data ignore rules
- `962bb074` — browser fullscreen teardown fix
- `194696bd` — build without preloaded commercial game data
- `a9f41779` and `f067223b` — final flat WebGPU vertical-orientation correction

Adaptations made during extraction:

- removed inherited native OpenXR CMake and launcher diagnostics
- used the shared frame pipeline rather than retaining the fork's duplicate
  monolithic `RunOneFrame` split
- removed dependencies on XR-only `ProjectionOverride` and fixed-render-size
  members; flat rendering uses the normal UE1 frustum and native widget pixels
- renamed comments and harness UI to describe the browser platform rather than
  XR milestones
- made smoke-test origin selectable through `SURREAL_WEB_BASE_URL`
- kept the current upstream `Bytecode::FindStatementIndex` behavior instead of
  replaying an older unrelated semantic change

## Validation

Native Windows compatibility:

```text
cmake -S . -B build-native -G "Visual Studio 17 2022" -A x64 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmake --build build-native --config Release --target SurrealEngine --parallel
build-native\Release\SurrealEngine.exe --help
```

Result: Release build and link passed; `--help` exited successfully.

Emscripten, initially without game data:

```text
emcmake cmake -S . -B build-emscripten-flat -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build-emscripten-flat --target SurrealEngine --parallel 8
```

Result: configuration, compilation, and `SurrealEngine.js`/wasm link passed.

The same build was then linked with a local UT99 installation and exercised in
Chrome through `node web/serve.mjs 8093`:

- `python web/smoke_test.py`: booted, ticked from 63 to 605, stopped at 605,
  no crash flag, no unhandled page error.
- `python web/smoke_test_webgpu.py`: 0 WebGPU errors, 95 last-frame draw calls,
  75 cached textures, 100% non-background screenshot pixels, clean stop, no
  crash flag, no unhandled page error.
- `git diff --check`: passed.
- Provider audit:
  `rg -n -i "webxr|openxr|vulkanxr|xr_khr|probexr|debugstereo" CMakeLists.txt SurrealEngine SurrealWidgets web`
  returned zero matches.

The local server used for validation was stopped after the tests. Generated
build products and the WebGPU screenshot are ignored and are not committed.

## Current limitations

- Browser startup is `--autoplay`; the modal desktop launcher is not available.
- Browser audio and video are null backends in this foundation.
- WebGPU `ReadPixels` cannot synchronously read back an async WebGPU texture;
  the smoke test validates canvas output in JavaScript instead.
- The optional preload can produce a very large `.data` bundle. Redistributable
  builds omit it and need a browser-side legal game-data import/persistence UI.
- Emscripten warns that `-pthread` plus `ALLOW_MEMORY_GROWTH` may be slower.
- This branch contains no XR API or behavior. WebXR remains a separate follow-up
  provider and should depend on the view/input/provider seams in integration.
