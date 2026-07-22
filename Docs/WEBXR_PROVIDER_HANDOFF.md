# WebXR provider skeleton handoff

Date: 2026-07-22

## Scope

This branch layers an optional WebXR/WebGPU provider on the flat Emscripten
platform. It provides:

- generation-safe immersive session enter, exit, failure cleanup, and re-entry;
- explicit transfer between the canvas requestAnimationFrame loop and the
  XRSession requestAnimationFrame loop;
- a packed, versioned JavaScript/WASM frame ABI;
- runtime asymmetric projection and tracked per-eye pose conversion into the
  shared `ViewFamily` abstraction;
- opaque presentation-target binding to WebGPU texture views;
- per-view selection of WebGPU projection texture array slices;
- `bgra8unorm`, `rgba8unorm`, and `rgba16float` projection pipelines selected
  from `XRGPUBinding.getPreferredColorFormat()`; and
- a separate `web/index_webxr.html` harness. The existing flat
  `web/index_webgpu.html` remains unchanged and does not require WebXR.

The provider deliberately excludes controller input, locomotion, weapon
behavior, haptics, HUD/menu/cinematic policy, PWA packaging, game-data import,
data persistence, and game-specific VM hooks.

## Dependency and commit order

Branch: `pr/webxr-provider`

Base: `pr/web-platform-foundation` at `88980d6234d0d897e5c01f78cfc4b70f618c90c3`.

The provider branch preserves its prerequisites as distinguishable commits:

1. `f2959604` — provider-neutral view families (cherry-picked from `9a88d556`)
2. `141b6d1e` — view-family legacy source-list entry (from `c0e81509`)
3. `99a8e56a` — provider-neutral presentation layers (from `2c9c452b`;
   CMake test placement reconciled with the Emscripten block)
4. `5082bc63` — frame-flash presentation correction (from `9d96e62f`)
5. `0ca84163` — opaque target binding and per-view selection seam
   (cherry-picked from OpenXR lane commit `398dbfd8`)

The WebXR implementation follows those prerequisites and should not be used as
the source for upstreaming the shared seams.

## Checkpoint provenance

The implementation was extracted and adapted from `webxr-m1`:

- `288d027c` — initial WebXR session harness
- `5be186f1` — WebGPU presentation groundwork
- `264ffe23` — two-view browser frame path
- `e58a6f43` — packed frame ABI
- `1e6cc90e` — opt-in XRSession-owned frame loop
- `14451d9f` — projection color-format support
- `b4b02dab` — tracked head pose composition and recentering
- `8f29634d` — generation-safe session lifecycle

Adaptations made during extraction:

- replaced checkpoint-specific `WebXRSceneView` calls with shared
  `ViewFamily` construction;
- replaced direct external-target calls with `PresentationTargetBinding` and
  `BeginPresentationView`;
- kept the external native handles opaque to engine code;
- restored the flat canvas pipeline after every XR target unbind;
- retained the browser-preferred XR projection format instead of assuming the
  canvas format; and
- cached one WebGPU pipeline family per encountered color format so XR frame
  bind/unbind does not rebuild pipelines, while still restoring the flat canvas
  family after each frame; and
- removed every controller, gameplay, UI, persistence, and PWA dependency.

Provider C++ lives under `SurrealEngine/Platform/WebXR`. Browser ownership and
packet packing live in `web/webxr_provider.js`.

## Frame ownership and failure behavior

The ordinary Emscripten main loop remains registered for the process lifetime.
It yields while an XR session owns frame scheduling. Ownership transfers only
after the session, `XRGPUBinding`, projection layer, and reference space are
ready. Session end or any frame exception restores canvas scheduling and resets
the tracked-pose origin.

Each callback packs at most two views, validates the ABI in native code,
advances simulation exactly once, builds one multi-view family, renders it, and
finishes deferred save/travel work once. A missing viewer pose skips the frame
without advancing simulation.

Unsupported projection formats fail closed during entry. A rejected imported
texture or presentation target fails the active session rather than silently
rendering to the flat canvas.

## Validation

Completed locally:

- native Windows Release compile/link of `SurrealEngine`;
- `PresentationTests` and `WebXRFrameBridgeTests`, both passing;
- synthetic Node lifecycle test covering capability, preferred RGBA format,
  duplicate-entry rejection, packed two-view frame, exit, and re-entry;
- Emscripten compilation and final JavaScript/WASM link;
- flat Chrome/WebGPU UT99 runtime after the provider changes: ticked from 61
  to 604, 95 draw calls, 75 cached textures, zero WebGPU errors, 100% nonblank
  screenshot pixels, and clean quit;
- `git diff --check`.

Commands:

```text
cmake -S . -B build "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" -DBUILD_TESTING=ON
cmake --build build --config Release --target SurrealEngine WebXRFrameBridgeTests PresentationTests --parallel 4
ctest --test-dir build -C Release --output-on-failure -R "WebXRFrameBridgeTests|PresentationTests"
node web/test_webxr_provider.mjs

& C:\Devstuff\emsdk\emsdk_env.ps1
emcmake cmake -S . -B build-emscripten -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" -DBUILD_TESTING=OFF
cmake --build build-emscripten --target SurrealEngine --parallel 8
node web/serve.mjs 8094
$env:SURREAL_WEB_BASE_URL="http://localhost:8094"; python web/smoke_test_webgpu.py
```

## Hardware gate and known limitations

No automated test proves physical headset presentation. A real Quest browser
and runtime must expose the draft WebGPU WebXR path (`XRGPUBinding`), accept an
immersive session with the `webgpu` feature, import the runtime-owned
`GPUTexture` through Emscripten's WebGPU bridge, and present both views. This is
the release gate for the provider, not something the synthetic lifecycle test
can emulate.

The current skeleton renders the world layer only. Weapon, UI, menu, and
cinematic layers are disabled intentionally until their shared presentation
policies are defined. Browser audio remains the flat platform's null backend.
Controller input and tracked-hand visuals are not part of this branch.
