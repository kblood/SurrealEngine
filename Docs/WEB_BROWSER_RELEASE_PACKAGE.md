# Shared flat/WebXR browser release package

Date: 2026-07-22

Integration status: the package composition and compliance mechanism are
implemented and automated at `integration/unified-engine` commit `a0fb4f93`.
WebXR and the optional demo descriptors remain experimental; Quest,
owner-data, final-hosting, and human/legal release gates remain open.

## Branch and dependencies

Original release branch: `release/webxr-browser-package`

This is a product/release composition branch, not an upstream pull-request
source. It starts from the smallest launcher/data stack and merges the existing
WebXR runtime topic without rewriting either history:

- `pr/web-desktop-launcher` at
  `6c614d56e66e6ea8882aada892b93bc6526e0a33`; this already contains
  `pr/web-data-persistence` at `2dda89bd7a3a0a5bfd2c2e14eccf63e75099fe61`;
- `pr/webxr-input-runtime` at
  `d472068ad5894dc8cdddeefbb4f491bd118f2592`; this already contains the flat
  web platform, view/presentation seams, WebXR provider, independent input
  composition, XR-common types, packed controller bridge, and live browser
  input adapter.

The dependency merge is `6d920b8c686b39344b2a66710cd1d7c9aec0f5ce`.
At that extraction point, `integration/unified-engine` was not a dependency or
ancestor. New changes on this release branch were confined to launcher,
diagnostics, static packaging, tests, and this release document; they did not
alter WebXR provider rendering or native XR UI capture.

The unified integration later reconstructs those release components alongside
the WebGL atlas bridge, controller/exact-contact review, asynchronous KHG
cinematic path, experimental demo descriptors, startup-map path, and
corresponding-source enforcement. Those later product commits do not change the
rule that this release composition is not an upstream PR source.

## User-visible behavior

`web/surreal_app.html` is one application for both modes:

- **Desktop window** uses the normal flat WebGPU canvas and ordinary browser
  keyboard/mouse input.
- **Immersive WebXR** is offered on a secure browser with `immersive-vr` when
  either direct `XRGPUBinding` presentation is available or the browser can use
  the `XRWebGLLayer`/WebGL 2 stereo-atlas compatibility bridge.

For direct presentation, the shared WebGPU device is requested from
`navigator.gpu.requestAdapter({ xrCompatible: true })` when the WebXR provider
is otherwise available. Compatibility presentation uses an ordinary WebGPU
device for the engine atlas and a WebGL 2 context for `XRWebGLLayer`. If neither
mode can initialize, the launcher reports why and keeps ordinary WebGPU flat
play available. Failed or declined immersive-session entry also returns a
visible flat fallback instead of terminating the running engine.

Before loading the engine, the page reports secure-hosting, WebAssembly,
WebGPU, local-storage, folder-import, and WebXR capability. Fatal platform
requirements stop with an actionable message. Optional WebXR failures do not.

The existing shared data layers remain authoritative:

- the user explicitly selects a local folder; no upload or automatic download
  exists;
- typed validation recognizes retail UT99 and Unreal Gold without weakening
  either layout contract. Separate experimental descriptors recognize locally
  selected UT demo 348, Unreal demo 205, and Deus Ex demo 1002f folders; they
  are not release-support or redistribution claims;
- imported commercial data remains in per-game OPFS/IndexedDB storage;
- mutable INIs, settings, logs, and strict saves remain in the separate
  allowlisted, crash-safe mutable store;
- game, map, renderer, and presentation selection become fixed validated
  native arguments.

At engine-integration level, startup has two distinct paths. UT99 and
Unreal/Gold normally use their game-owned `URL.LocalMap`; the launcher exposes a
checked-by-default skip option that supplies a validated direct map instead.
KHG's file-backed `INTRO.AVI` uses the separate asynchronous browser cinematic
path and existing IV50 decoder. The current release library does not claim KHG
folder import or launch support. The map path still needs owned UT99/Unreal
script tests, while the cinematic path needs owned KHG media tests and retains
the documented audio/mask/continuation limitations.

## Static artifact layout

Create a no-data Emscripten build, then package it:

```powershell
& C:\Devstuff\emsdk\emsdk_env.ps1
emcmake cmake -S . -B build-emscripten -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" -DBUILD_TESTING=OFF
cmake --build build-emscripten --target SurrealEngine --parallel 8
node web/package_corresponding_source.mjs --source-root . --output C:\release-materials\SurrealEngine-corresponding-source.tar.gz
node web/package_browser_release.mjs --engine-dir build-emscripten --corresponding-source C:\release-materials\SurrealEngine-corresponding-source.tar.gz.json --output C:\path\to\webxr\Ports\SurrealEngine
```

For a separately hosted candidate, keep the same package inputs and set its
origin-absolute deployment path explicitly:

```powershell
node web/package_browser_release.mjs --engine-dir build-emscripten --corresponding-source C:\release-materials\SurrealEngine-corresponding-source.tar.gz.json --output C:\path\to\webxr\Ports\SurrealEngine-Experimental --intended-base-path /webxr/Ports/SurrealEngine-Experimental/
```

The override must begin and end with `/` and use safe URL path segments. The
packager rejects traversal, query, fragment, backslash, encoded, and empty path
segments. It records the effective path in both `release-manifest.json` and
`HOSTING.txt`; omitting the option preserves the stable path from
`web/release-package.json`.

The output is self-contained and position-independent:

```text
webxr/Ports/SurrealEngine/
  index.html
  browser_app.css
  browser_app.js
  browser_data_bootstrap.js
  browser_release.js
  mutable_persistence.js
  ut99_importer.js
  webxr_browser_app_adapter.js
  webxr_diagnostics.js
  webxr_webgl_bridge.js
  webxr_provider.js
  engine/
    SurrealEngine.js
    SurrealEngine.wasm
  source/
    SurrealEngine-corresponding-source.tar.gz
  licenses/
    SurrealVideo-LGPL-2.1.txt
    SurrealVideo-README.md
    SurrealVideo-Relinking.md
  SOURCE-OFFER.txt
  source-compliance.json
  release-manifest.json
  HOSTING.txt
  _headers
  .htaccess
```

All runtime URLs are relative, so the same directory can be hosted at the
intended `/webxr/Ports/SurrealEngine/` location or another HTTPS base path.
`_headers` and `.htaccess` provide examples for the COOP/COEP isolation needed
by the threaded WASM build and the `application/wasm` MIME type. Hosts that do
not consume either file must configure equivalent headers themselves.

The packager fails unless `CMakeCache.txt` records an empty
`SURREAL_GAMEDATA_DIR`, rejects every Emscripten `.data` payload, copies only a
fixed shell/runtime allowlist, and audits the output for UE1 game extensions.
`release-manifest.json` records dependency SHAs plus each file's length,
SHA-256, and expected MIME type. It never enumerates browser-private imports.

When the Emscripten build statically includes SurrealVideo, packaging is refused
unless a clean, generated corresponding-source archive matches the build's
commit and tree provenance. By default the complete tracked source archive is
copied into `source/`; `--source-url` may instead name an HTTPS location for
that exact archive. The package records its hash beside the WASM hash, includes
the LGPL 2.1 text selected for binary distribution, the project notice and
rebuild instructions, and inserts a
visible source link in `index.html`. See `BROWSER_STATIC_RELINKING.md` for the
mechanism and the remaining human/legal review.

## Validation

Synthetic tests contain no commercial data:

```text
node --check web/browser_app.js
node --check web/browser_release.js
node --check web/webxr_browser_app_adapter.js
node --check web/webxr_diagnostics.js
node --check web/package_browser_release.mjs
node --check web/package_corresponding_source.mjs
node web/test_corresponding_source.mjs
node web/test_release_package.mjs
node web/test_ue1_demo_imports.mjs
node web/test_mutable_persistence_native_gate.mjs
node web/test_webxr_diagnostics.mjs
node web/test_webxr_provider.mjs
node web/test_webxr_webgl_bridge.mjs
node web/test_webxr_webgl_fallback_provider.mjs
python web/smoke_test_browser_app.py --base-url=http://localhost:8112
python web/smoke_test_browser_app_runtime.py --base-url=http://localhost:8112
python web/smoke_test_ut99_importer.py --base-url=http://localhost:8112
python web/smoke_test_mutable_persistence.py --base-url=http://localhost:8112
python web/smoke_test_browser_data_bootstrap.py --base-url=http://localhost:8112
$env:SURREAL_WEB_BASE_URL="http://localhost:8112"
python web/probes/webgpu_webgl_bridge_probe_test.py

cmake -S . -B build "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

Serve a staged artifact directly for the final layout smoke:

```text
node web/serve.mjs 8113 C:\path\to\webxr\Ports\SurrealEngine
python web/smoke_test_release_package.py --base-url=http://localhost:8113
```

That Chrome smoke also forces every WebXR API absent, verifies a plain WebGPU
adapter request, exercises flat canvas input/resize/fullscreen behavior, and
retrieves the visible corresponding-source link with its recorded size and
SHA-256. See `FLAT_BROWSER_WASM_AUDIT.md` for the evidence boundary and the
remaining owner-data gates.

### Optional headset report

The collapsed **WebXR headset diagnostics** panel is observational and does
not start or stop presentation. Open it during a physical-headset test to see
the presentation capability code, XR-compatible adapter result, provider
phase and failure stage, rendered/skipped/input packet counters, projection
format, reference space, and enter/exit/re-entry counts. It refreshes only
while open; flat-mode users otherwise pay no polling cost.

**Copy test report** and **Download test report** produce the same fixed-schema
plain text. The formatter allowlists known scalar fields and transition tokens.
It does not read or include game data, imported file names, private paths,
launcher/engine logs, page URLs, or the browser user-agent string. Provider
error text is represented only as `present` or `none`; the separate error-stage
field identifies where the lifecycle failed without copying a potentially
sensitive exception message. Report schema v2 retains every v1 field name and
representation, then adds the selected presentation mode, known layer/atlas
dimensions, and allowlisted bridge frame/error/sample counts, median, p95, p99,
and blocking-timing state. Key-based v1 readers can ignore the appended fields;
strict schema readers must explicitly accept
`surrealengine-webxr-headset-report-v2`.

Bridge percentiles come from the most recent 120 valid nonnegative timing
samples. Raw samples remain private to the in-page timing window and are not
placed in provider state or the report. Direct-mode layer dimensions come from
the projection layer when the browser exposes them. Compatibility-mode layer
and atlas dimensions appear once their respective objects are available;
unavailable measurements are reported as `unknown` rather than inferred.

### Quest 3 experimental-site test card

Use the separate experimental URL, not the public Ports page. Test with a
charged Quest 3 and both controllers awake. Record the build commit, Quest OS,
refresh rate, and browser/runtime versions separately; those identifiers are
intentionally absent from the privacy-safe report.

Run the applicable path or paths:

1. **Quest Browser (primary standalone path):** open the HTTPS site in Quest
   Browser, select user-owned data, choose **Immersive WebXR** under
   **Presentation**, open **WebXR headset diagnostics**, and press **Play**.
2. **Desktop Chrome through Virtual Desktop/VDXR (separate PC path):** select
   VDXR as the active OpenXR runtime in Virtual Desktop, connect the Quest 3,
   start desktop Chrome from that session, and open the same HTTPS site. This
   tests desktop Chrome's WebXR-to-OpenXR path, not Quest Browser. If Chrome
   does not expose `immersive-vr`, copy the pre-entry report and mark the path
   unsupported; do not silently switch to SteamVR or count that as a provider
   rendering failure.

Start with automatic mode selection. If the experimental site exposes a
**Force WebGL bridge** control, repeat with it enabled before entry. Do not use
developer-console overrides as release evidence. Copy a report after at least
30 seconds of running presentation for every mode that actually starts.

The running report should satisfy the following criteria:

| Report field | Direct binding | WebGL compatibility bridge |
| --- | --- | --- |
| `capability_available` | `yes` | `yes` |
| `provider_phase` / `provider_stage` | `running` / `running` | `running` / `running` |
| `provider_error` / `provider_error_stage` | `none` / `unknown` | `none` / `unknown` |
| `presentation_mode` | `direct-webgpu` | `webgl-bridge` |
| `xr_compatible_adapter` | `xr-compatible` | may be `xr-compatible` or the site's recorded flat fallback |
| `projection_format` | a supported WebGPU color format | `rgba8unorm-webgl-bridge` |
| `reference_space` | `local-floor` preferred; `local` accepted | `local-floor` preferred; `local` accepted |
| dimensions | positive layer dimensions when exposed | positive layer and atlas dimensions after frames begin |
| frame/input counters | increase while moving the head and controllers | increase while moving the head and controllers |
| bridge counters | not applicable | `bridge_frames` and `bridge_samples` increase; `bridge_errors: 0` |

For bridge timing, leave the session running until `bridge_samples` reaches
120. Compare percentiles with the release thresholds only when
`bridge_blocking_timing: yes`: p95 must be at most 4.0 ms and p99 at most
5.5 ms. Also record the selected refresh rate separately and look for missed
frames against the whole-frame budget: 13.89 ms at 72 Hz or 11.11 ms at
90 Hz. Direct mode has no bridge timing values; `unknown` or zero bridge fields
are expected there.

Perform this behavior pass in each working mode:

1. Enter VR, wait for `running`, exit to the still-working flat canvas, and
   re-enter. Do three quick cycles for a test build; the release gate remains
   20 consecutive cycles. Counters must show another successful entry, exit,
   ended session, and re-entry without stuck input, a black canvas, or a dead
   engine loop.
2. Check stereo and FOV in a recognizable room: left/right eyes must not be
   swapped or vertically flipped; geometry must not crop, stretch, converge at
   the wrong depth, or move with the head. Yaw, pitch, and small positional
   movement must produce stable tracking and parallax without a stale eye.
3. Open the menu. Both controller proxies must track in 3D. Each laser must
   start at its controller, end at its visible contact marker, and highlight
   and click exactly the option under that marker. A trigger press produces one
   click, holding it does not repeat, the menu stays above world/intro quads,
   and controller sleep, focus loss, or exit does not leave a held action.
4. With intro skipping disabled, confirm the intro/prompt is visible on a
   world-anchored quad, **Press Fire** advances it, and the menu or game appears
   instead of a darker black frame. With skip enabled, confirm loading reaches
   the selected safe map. Check that loading, menu, and any available cinematic
   have correct aspect, never appear behind another quad, and return control
   after completion or skip.
5. Unlock audio with a user gesture. Confirm music and effects are audible,
   volume/mute work, and audio neither duplicates nor permanently stops over
   enter, exit, re-entry, intro/cinematic transition, and visibility loss.
6. Play while turning and moving for at least five minutes for a test build
   (30 minutes for release qualification). Fail on repeatable black/stale
   frames, eye disagreement, input lag that moves the selected menu item away
   from the contact marker, growing unexpected skipped frames, bridge errors,
   audio loss, context loss, or thermal frame collapse.

Copy back only the generated text from **Copy test report**, once before entry,
once while each presentation mode is running, and once after re-entry. If entry
fails, copy the report immediately while the failure stage is still present.
Paste each report verbatim inside a code block, preceded only by this manually
written, data-free header:

```text
build_commit: <commit>
test_path: quest-browser | desktop-chrome-vdxr
device: quest-3
os_runtime_version: <version>
browser_version: <Quest Browser or Chrome version>
refresh_rate_hz: <72 or 90>
selection: automatic | forced-webgl-bridge
behavior_result: pass | fail:<short category, no paths or game-data names>
```

The generated `surrealengine-webxr-headset-report-v2` is allowlisted and ends
with `privacy: no-game-data,no-paths,no-logs`. Inspect that line before sharing.
Do not copy console output, screenshots of local files, page URLs, imported
file names, private paths, browser user-agent text, engine logs, or raw browser
exceptions. A report with `provider_error: present` plus its allowlisted
`provider_error_stage` is sufficient to triage the lifecycle boundary.

Current results:

- 12 shared launcher/capability/diagnostics/startup-policy checks passed;
- 19 UT99/Unreal Gold importer checks passed;
- the three-demo descriptor/import suite passed without commercial data;
- 13 mutable-persistence checks passed;
- 3 browser bootstrap ordering/isolation checks passed;
- direct and `XRWebGLLayer` provider lifecycle/controller tests passed;
- the desktop Chrome WebGPU-canvas to WebGL 2 upload/readback probe passed;
- all 25 integrated native CTest tests passed;
- fresh no-data Emscripten compile/link passed;
- real staged package reached the import gate while cross-origin isolated and
  performed a synthetic Unreal Gold flat launch with the expected arguments;
- corresponding-source, static package/data audit, and `git diff --check`
  passed.

## Remaining release gates

Synthetic files prove validation and launch wiring, not real game behavior.
Before publishing:

1. Import complete user-owned UT99 and Unreal Gold folders independently;
   confirm detection, map lists, launch, switching, replacement, and persistence
   after a browser restart and package upgrade. Test both checked direct-map
   startup and unchecked game-owned `URL.LocalMap` startup.
2. Run flat UT99 and Unreal Gold with keyboard/mouse, fullscreen/resize, INI
   changes, saves, explicit quit-and-flush, and restore.
3. On a physical Quest browser, record browser/runtime versions outside the
   generated report, then test direct `XRGPUBinding` where exposed and automatic
   plus forced `XRWebGLLayer` compatibility selection. For the direct mode,
   verify an XR-compatible adapter; for the bridge, record its timing counters
   against the thresholds in `WEBXR_WEBGL_BRIDGE_HANDOFF.md`. Save the v2 report
   while each tested mode is running; it includes the mode, available dimensions,
   and bounded bridge summaries without requiring remote debugging.
4. Verify the generated report reaches provider phase/stage `running`, records
   a supported projection format, the expected presentation mode, and
   `local-floor` or `local` reference space,
   then compare rendered/skipped/input counters while checking stereo output,
   controller proxies, laser/exact-contact alignment, intro HUD, menu ordering,
   held-button disconnect, and blur. Confirm the expected presentation mode in
   the separate state snapshot from step 3.
5. Exit and re-enter once. Confirm the enter/exit/re-entry counters and bounded
   transition list change as expected, then copy or download the report. Inspect
   it before sharing and confirm it contains no game name/data, path, URL, log,
   or user-agent details.
6. After successful, declined, and failed XR entry, confirm the same flat canvas
   continues and keyboard/mouse still work.
7. Verify production HTTPS, COOP/COEP/CORP headers, WASM MIME, quota/persistence
   diagnostics, and storage survival at the final origin and path.
8. Generate the corresponding-source archive from the exact clean release
   commit and have the final hosted source offer, product terms, notices, and
   redistribution model reviewed. Keep all demo data out of the package and
   local-import-only unless explicit artifact-specific permission is obtained.

World-anchored UI/cinematic capture, procedural tracked-controller proxies,
lasers, and exact-contact markers are implemented shared engine/provider
features, not package-shell duplicates. Their automated geometry, ordering,
input, and lifecycle evidence has passed, but physical presentation, release
tuning, real game behavior, weapons, locomotion policy, and browser audio remain
separate runtime gates.
