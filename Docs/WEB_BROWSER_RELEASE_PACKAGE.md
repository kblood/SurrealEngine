# Shared flat/WebXR browser release package

Date: 2026-07-24

## Current stable website release

The single public Surreal Engine folder at
`https://dionysus.dk/webxr/Ports/SurrealEngine/` now serves clean integration
commit `e8a57fb8`. It was built as the shipping Release Asyncify/WasmFS variant
with no preloaded game data, packaged with matching corresponding source, and
deployed by a verified atomic folder swap on 2026-07-24. The importer links to
Epic-sanctioned OldUnreal acquisition pages for Unreal Gold and Unreal
Tournament without mirroring any game payload.

The live WASM SHA-256 is
`6d1899984e75da6481cf04aec655153d69e28ca449471702c3aead0562d4de92`;
the live manifest SHA-256 is
`f7a3e80504787653935b0c16b7157ced7b48a5a63377d7274f7e866b43b6d64e`.
Local and live release smokes pass. See `WEB_DEPLOYMENT_E8A57FB8.md`.

Do not publish numbered Surreal candidate directories. Use the one stable
folder, retain rollback copies outside `Ports`, and keep commit/hash identity
inside the manifest.

The historical extraction/integration record follows. Status statements in
that record describe the earlier `54283bd8` milestone, not the current live
release above.

Integration status: the package composition, compliance mechanism, revisioned
browser assets, and current shared XR input/weapon runtime are implemented at
`integration/unified-engine` commit `54283bd8`. A complete owned GOG UT99
folder has passed both local-package and public-origin flat WebGPU launch tests.
At that milestone, WebXR and the optional demo descriptors remained
experimental; Quest, Unreal Gold, persistence/upgrade, and human/legal release
gates remained open.

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
  selected UT demo 348, Unreal Special Edition demo 200, and Deus Ex demo 1002f
  folders; they are not release-support or redistribution claims;
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

For a non-public staging directory, keep the stable origin path explicit even
when the filesystem output includes a commit-qualified staging directory:

```powershell
node web/package_browser_release.mjs --engine-dir build-emscripten --corresponding-source C:\release-materials\SurrealEngine-corresponding-source.tar.gz.json --output C:\path\to\releases\staging\e9031169\SurrealEngine --intended-base-path /webxr/Ports/SurrealEngine/
```

The override must begin and end with `/` and use safe URL path segments. The
packager rejects traversal, query, fragment, backslash, encoded, and empty path
segments. It records the effective path in both `release-manifest.json` and
`HOSTING.txt`; omitting the option preserves the stable path from
`web/release-package.json`. Do not expose the commit-qualified staging parent
under `Ports`; atomically publish its `SurrealEngine` child to the one stable
public folder.

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

Packaged CSS and JavaScript references carry a twelve-character content-hash
query. The supplied hosting files also require revalidation. This prevents an
updated `index.html` from running with a cached pre-fix importer or launcher;
ETags can still make unchanged-file revalidation inexpensive. A deployment
must keep the generated `index.html`, assets, and `release-manifest.json`
together instead of copying individual files over an older package.

### Revisioned public candidate 54283bd8

The immutable candidate at
`https://dionysus.dk/webxr/Ports/SurrealEngine-Candidate-54283bd8/` was packaged
from clean commit `54283bd83053577eee88fb177d4c8f1cea81a465` and tree
`b08ce057c0ea2b3133dc86dabeb943dfbd86ee9c`. It deliberately does not replace
the stable or earlier experimental directory. Its generated page references
`ut99_importer.js?v=d1987b081e8e`; the accompanying hosting policy requires
revalidation, closing the stale unversioned-script failure seen on the older
public packages.

The package records WebAssembly SHA-256
`23dc8292efe22572ccece704c3e187df5179ffb15ca2b3bedae9fccc429a40e7` and
corresponding-source SHA-256
`a69293299a6f6ac1d92d6fdd6c7d888197722ab66e20c9da0b761a4da293443b`.
Both the locally served package and the deployed HTTPS directory passed the
release-shell smoke. A fresh Chrome profile then imported the owned GOG UT99
folder (496 files, 659,817,346 bytes), mounted all files through OPFS, detected
UT99, advanced simulation ticks, produced 41 WebGPU draws and 52 textures, and
reported zero page or WebGPU errors. This establishes the exact folder and
current importer/runtime path; it does not replace physical Quest testing.

### Revisioned public candidate 2904593c

The immutable corrected candidate is live at
`https://dionysus.dk/webxr/Ports/SurrealEngine-Candidate-2904593c/`. It was
built from clean commit `2904593ca6010d02da4eef7b4ccc33cd54d595cf` and tree
`bfa564e7a7b953ba5cc8f817250f9ed57b9c8616`. The package contains 25 files,
is 36,733,381 bytes, and contains no game or demo data. It does not replace the
older immutable candidate or either stable/experimental directory.

The release records WebAssembly SHA-256
`ab3752751dd97cbfe2cfcfc47b953da73d59935a76a5ad7be5fa1d49727f0506`,
JavaScript SHA-256
`47a01e685e7a0d85428134b8d2d05a43ca581410326942a62406276549bd4c7d`,
manifest SHA-256
`f9358d3da57c4683eeeb84a89d40ade25711ee939b57f53124e352b17acbedd2`,
and corresponding-source SHA-256
`bb55c5672d140237783b3c1eb6ba6ffac0d90ec430d7f2a37197bcc4492986ce`.
The staged server copy had the expected 25 files and all four hashes before an
atomic rename. The live HTTPS release-shell smoke then passed source retrieval,
cross-origin isolation, OPFS import gating, synthetic Vortex2 launch, input,
responsive canvas sizing, and fullscreen with no page error.

Exact owned-data qualification on the same tracked revision passed UT99 direct
startup with the declared Deck 16 default, the 85-second CityIntro-to-UMenu
transition, and Unreal Gold direct startup with the declared Vortex2 default.
All three visibly rendered and reported advancing ticks plus zero page/WebGPU
errors. The Unreal run exposed a running 48 kHz AudioContext. An automated
trusted click captured the exact canvas and hid the mouse-capture prompt; real
mouse-look, audible output, and physical Quest presentation remain human gates.

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

### Revisioned public candidate cef1e89b

The newer immutable diagnostic candidate is live at
`https://dionysus.dk/webxr/Ports/SurrealEngine-Candidate-cef1e89b/`. It adds a
visible **Automatic** / **Force WebGL compatibility bridge** selector and a
default-off, non-persisted blocking-timing QA option. It was built from clean
commit `cef1e89b3bbccba4d041ce5d00cc09bd99358c06` and tree
`15d82324767c24373b28090500f4176509e1357d`. The package contains 25 files,
is 36,746,638 bytes, and contains no game or demo data.

Its WebAssembly SHA-256 is
`e59508958452312affb8ee7ee4943a78ad14a64518619488d351c63b75fd81fd`,
JavaScript SHA-256 is
`47a01e685e7a0d85428134b8d2d05a43ca581410326942a62406276549bd4c7d`,
manifest SHA-256 is
`7fa5214f8dd837bf640a6d080d16dc1bd668680583bdc57cbb45bc019dcce58a`,
and corresponding-source SHA-256 is
`65e97f12a3619573fa711cffc821f9aae95d83f83ab080a0e9a14a30cd907679`.
The staged and live copies contained the expected 25 files with matching
hashes. The live flat release-shell smoke and synthetic Automatic/direct and
forced-bridge sessions passed without page or asset errors.

Physical Quest 3 testing through desktop Chrome, Virtual Desktop, and VDXR did
not qualify it. Automatic reached WebXR consent and then immediately returned
to flat mode. In that flat fallback the exact canvas acquired pointer lock and
mouse fire worked, but relative mouse motion did not rotate the view. The v2
failure report was not captured, and forced bridge had not yet been tested
separately. See `WEBXR_VDXR_QUALIFICATION.md`.

The three corrections are not part of immutable `cef1e89b`: WasmFS save
discovery (`46d13173`), enabled-feature WebXR negotiation and exact safe error
codes (`f3643b78`), and actual-lock browser relative-motion delivery
(`28906717`). They are packaged in the newer candidate below.

### Revisioned public candidate 173bf623

The superseding immutable candidate is live at
`https://dionysus.dk/webxr/Ports/SurrealEngine-Candidate-173bf623/`. It was
built in a clean detached worktree from commit
`173bf623fe738142c142c7b8d9c5c4e9c8c72071` and tree
`cff42cf266f53bb6572c59c12cc44330fb6b9f3f`. Its shipping
Asyncify/WasmFS build records an empty game-data directory. The package contains
25 files, is 36,776,160 bytes, and contains no game or demo data.

Its WebAssembly SHA-256 is
`829a37e2b2be4dd1a38d6d057fc99e6495a3f12407e832c87bebeac11f039d42`,
JavaScript SHA-256 is
`a8d28a2238f6db0c857619832caba7ac3562f9a57b69471bb8fd9c48c60ad3df`,
index SHA-256 is
`07c6c39003d5917b049b7e0c4d52c6d9559b8b42d0931061a0eddab4939a5b15`,
manifest SHA-256 is
`40f14a050646dbcf9c7f81d79efb742aae6b9a056023c004a7da6b0f18143a9e`,
and corresponding-source SHA-256 is
`626c31f8cf6e0bf490a3963a3da527cd7e92e7663b89bd6f6987e1ccc74fdf06`.

The clean build, growable-memory verifier, source/package/data audits, normal
and OpenXR Windows builds with 34/34 tests each, both Emscripten variants, and
the combined browser matrix passed. Deployment used a separately verified
staging directory followed by an atomic rename. The live HTTPS smoke passed the
no-data import gate, synthetic Unreal Gold flat launch, input, responsive
layout, fullscreen, source retrieval, and zero page errors. Public downloads
match all five hashes, and index/WASM/source responses carry the required
COOP/COEP/CORP headers and MIME types. A later human retest confirmed physical
mouse-look and initial audible output. Audio recovery/endurance and headset
presentation remain gates rather than claims.

The live packaged launcher also passed three synthetic trusted-Play WebXR
scenarios. Automatic requested `webgpu` only as optional and selected direct
presentation when the granted session enabled it; the same request selected the
WebGL bridge when the session omitted it. Forced bridge requested only
`local-floor`. Every session used exactly one layer API, reached `running`,
ended once, returned engine-loop ownership, and produced zero page, console, or
request errors. The formatted report retained the allowlisted error-code field
while removing injected private paths, URLs, and logs. This validates the live
launcher/provider/adapter/diagnostics assets with a bounded native seam; it does
not validate an opaque headset framebuffer.

An isolated clone of the prior same-origin owner profile then qualified the
live candidate without editing the GOG installations. It restored and launched
Unreal Gold (335 files, 586,421,656 bytes, `Vortex2`) and UT99 (496 files,
659,817,346 bytes, `DM-Deck16][`) in flat WebGPU mode. Both advanced ticks,
produced nonuniform frames, and reported zero page/WebGPU errors. The save
probe reproduced throwing `FS.analyzePath('/gamedata/Save')` while `stat` and
`readdir` worked. The corrected snapshot included a synthetic 32-byte
`Save99.usa`; an explicit flush stored it with the two UT INIs and last-run log,
and a fresh Chrome process restored all four files with the save's exact SHA-256
`1d6fd3a8de9d6466d1877f18238f33a794c1c32a00553f37aa49e89030870c36`.
A disallowed `.usa` sibling was absent from both metadata and the restored
filesystem. This is real owner-library and save-persistence evidence. A later
human Chrome/Virtual Desktop run additionally confirmed pointer capture, fire,
and working relative mouse-look. Audio was audible but later failed to recover
from a lifecycle transition until restart, which current source corrects by
retrying resume after visibility and presentation transitions.

A subsequent Quest 3 test through Virtual Desktop/VDXR explicitly selected
**Force WebGL compatibility bridge** on this candidate and left **Temporary QA:
blocking bridge timing** unchecked. Immersive entry succeeded; native content
and blue/red procedural tracked-controller proxies appeared. This proves the
session, `XRWebGLLayer`, native-frame, UI-composition, and XR-input path in a
real headset. Severe rotational distortion and a short black world cutoff mean
the candidate still fails visual qualification. Audit attributes the cutoff to
WebXR metre projection depth being applied to Unreal-Unit view coordinates and
identifies the necessarily previous-pose asynchronous atlas as a second release
blocker.

### Physical candidate 802cfa62

The newer physical candidate `802cfa62b7436a0c7f473d6292052812bae33b5f`
contains the projection-unit/depth corrections and bounded atlas pose-age
diagnostics. On Quest 3 through Virtual Desktop/VDXR, immersive startup needed
several attempts and waiting before it remained interactive. Head look and
controller aim responded, and controller pointers could point at and click menu
items. Gameplay input did not qualify: the controller trigger did not satisfy
**Press Fire**, while mouse left did, and thumbsticks did not move the player.

The menu was horizontally mirrored and the world remained distorted after the
projection correction. The privacy-safe report was not preserved and remains
pending; the release page sends no remote telemetry, so presentation mode,
provider stage, projection/reference-space values, frame/input counters, and
atlas pose age cannot be reconstructed. This is a failed physical qualification
and candidate `802cfa62` remains non-final.

A controlled native A/B package from the same exact commit is available at
`C:\Devstuff\QuestGames\release-candidates\SurrealEngine-Native-OpenXR-802cfa62-VDXR-test`.
After connecting Quest 3 through Virtual Desktop with VDXR active, launch its
integrated game-library UI with:

```powershell
& 'C:\Devstuff\QuestGames\release-candidates\SurrealEngine-Native-OpenXR-802cfa62-VDXR-test\SurrealEngine.exe' --openxr
```

Select **Vulkan** on the launcher Video page before Play. Direct3D 11 cannot
bind OpenXR, and native `--render=vulkan` is not applied at this commit because
the renderer override is Emscripten-only. The data-free x64 Release package
passed all 34 native tests and contains no owner-game files; its executable and
runtime probe were deliberately not run during packaging. Repeat the identical
head, aim, menu, trigger, thumbstick, and visual-orientation checks and preserve
the native log.

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

The first physical VDXR failure showed that stage alone is insufficient for a
fast remote diagnosis. Commit `f3643b78` adds the provider's exact
`lastErrorCode` as an allowlisted `provider_error_code` token while continuing
to exclude raw exception text. The field still needs confirmation in a physical
failure report.

Bridge percentiles come from the most recent 120 valid nonnegative timing
samples. Raw samples remain private to the in-page timing window and are not
placed in provider state or the report. Direct-mode layer dimensions come from
the projection layer when the browser exposes them. Compatibility-mode layer
and atlas dimensions appear once their respective objects are available;
unavailable measurements are reported as `unknown` rather than inferred.

### Quest 3 experimental-site test card

Use the separate experimental URL, not the public Ports page. Test with a
charged Quest 3 and both controllers awake. For the PC path, the Quest must be
awake and actively connected through Virtual Desktop/VDXR as a WebXR headset
before loading the page or requesting `immersive-vr`; merely running desktop
Chrome is not a headset test. Record the build commit, Quest OS, refresh rate,
and browser/runtime versions separately; those identifiers are intentionally
absent from the privacy-safe report.

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

Start with **Automatic** under **WebXR backend**. Then repeat with **Force WebGL
compatibility bridge** selected before pressing **Play**. Do not use
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
| bridge pose age | not applicable / `unknown` | age becomes numeric after first completed atlas; record current/max frames and ms plus reuse count |

A pre-entry report with `enter_attempts: 0` confirms only that no immersive
request has been recorded. It cannot qualify either presentation backend. In a
running bridge, `bridge_present_age_frames` and `bridge_present_age_ms` are the
age of the currently submitted atlas; `bridge_max_present_age_frames` and
`bridge_max_present_age_ms` retain session maxima; and
`bridge_reused_presents` counts repeated submission of a completed target.
`unknown` age before the first completed target is expected. Positive age,
increasing maxima, or reuse are stale-presentation evidence, not acceptable
substitutes for stable head tracking. The values are bounded and reset on
entry/re-entry; no source pose, projection, game data, path, or log is included.

On 2026-07-23, the `cef1e89b` Automatic path on Quest 3 through Virtual
Desktop/VDXR reached consent but did not reach this running-state table. That is
a failed qualification, not an unsupported preflight and not evidence against
the forced bridge. Candidate `173bf623` later entered forced bridge with blocking
timing off and displayed native content plus both procedural controller proxies,
but failed the stereo/FOV behavior gate through rotational distortion and short
distance cutoff. Preserve this entry evidence; do not treat it as a successful
presentation qualification.

For the short bridge timing qualification run, select **Force WebGL
compatibility bridge**, enable **Temporary QA: blocking bridge timing
(slower)**, and leave the session running until `bridge_samples` reaches 120.
The checkbox is intentionally off by default and does not remain enabled after
a reload or later launch. Compare percentiles with the release thresholds only
when `bridge_blocking_timing: yes`: p95 must be at most 4.0 ms and p99 at most
5.5 ms. Disable the QA option for normal behavior and thermal runs. Also record
the selected refresh rate separately and look for missed frames against the
whole-frame budget: 13.89 ms at 72 Hz or 11.11 ms at 90 Hz. Direct mode has no
bridge timing values; `unknown` or zero bridge fields are expected there.

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
selection: automatic | forced-webgl-bridge | forced-webgl-bridge-blocking-timing
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
- at clean candidate `54283bd8`, all current browser package, audio,
  persistence, demo-import, WebXR provider/diagnostics, and WebGL bridge gates
  passed; the Window-owned Asyncify build exposed OPFS mount ABI 2/mode 2 and
  WebXR frame ABI 3 without page errors;
- the exact 629.3 MiB GOG UT99 installation passed recursive import and flat
  WebGPU launch both before deployment and from the revisioned public HTTPS
  candidate, with 496 OPFS-mounted files and no page/WebGPU error.
- at corrected candidate `2904593c`, 10 Node suites, 7 syntax gates, 11 Chrome
  browser probes, 33 native tests, and 33 OpenXR tests passed; both conventional
  pthread/non-Asyncify and shipping Asyncify/WasmFS Emscripten links passed;
- the exact owned UT99 direct and CityIntro-to-UMenu paths plus the exact owned
  Unreal Gold Vortex2 path visibly rendered with advancing ticks and no
  page/WebGPU errors; the live immutable HTTPS package passed its data-free
  release-shell and corresponding-source smoke.
- at clean diagnostic candidate `cef1e89b`, the live package and both synthetic
  presentation modes passed, while the first physical Quest 3/VDXR Automatic
  attempt exited immersive mode after consent; flat pointer lock and mouse
  buttons worked, but relative mouse-look did not.
- at immutable candidate `173bf623`, a physical Quest 3/Virtual Desktop/VDXR
  forced-bridge attempt with blocking timing disabled entered immersive VR and
  showed native output plus blue/red tracked procedural controller proxies.
  Rotational distortion and a short black cutoff failed visual qualification.
  Commits `0331ef21` and `0218d5b7` now correct and test metre-to-UU scaling
  plus mode-specific depth convention, but need physical requalification.
  Stale-atlas pose age remains a release blocker; `078a6d2f` now reports it.
- at physical candidate `802cfa62`, immersive startup required several attempts
  and waiting. Head look, controller aim, and menu point/click responded, but
  gameplay trigger and thumbstick movement did not; mouse left cleared **Press
  Fire**. The menu was horizontally mirrored and the world remained distorted.
  No report was preserved and no remote telemetry exists, so the candidate is
  non-final and the exact provider/presentation counters remain pending.
- a fresh live owner-data matrix on `2904593c` imported UT99 (496 files,
  659,817,346 bytes) and Unreal Gold (335 files, 586,421,656 bytes), switched
  both directions before and after Chrome restart, replaced only UT99 without
  disturbing Unreal, and launched Unreal through unchecked `LocalMap` with
  advancing ticks, visible rendering, running AudioContext state, and zero
  page/WebGPU errors. Explicit, pagehide, clean-quit, INI, Settings, and
  same-origin old-to-new candidate restoration passed. WasmFS directory
  detection omitted real `.usa` saves from snapshots, so save persistence did
  not pass.

## Remaining release gates

The owned-data matrix proves import, initial startup, rendering, title
isolation/switching/restart, UT replacement, Unreal `LocalMap`, and configuration
restoration, but not save restoration, long-play behavior, or successful
physical headset presentation. Before
stable publication:

1. Retain the passed `173bf623` physical relative-mouse result while testing
   capture/resume, browser Escape, wheel/buttons, and the new aggregate pointer
   counters. Retest the `4dfceb0f` audio recovery after visibility and failed or
   successful presentation transitions; verify music/effects, volume/mute, and
   the explicit Enable audio fallback over longer play.
2. Retain the passed real owner `.usa` save/restore evidence, including the
   reproduced `FS.analyzePath` throw with working `FS.stat`. Complete
   fullscreen/resize and long-play persistence checks.
3. Retain the proved forced-bridge immersive entry with blocking timing off.
   Repeat `802cfa62` and preserve its local privacy-safe report, then run the
   exact native OpenXR package through VDXR with Vulkan selected in the
   integrated launcher. Compare the same head, aim, menu click, gameplay
   trigger, thumbstick, menu-handedness, and world-distortion checks. Requalify
   the landed WebXR-metre-to-Unreal-Unit projection mapping without
   replacing the runtime's asymmetric/sheared matrix. Bridge converts its
   WebGL projection to WebGPU depth and flags it; direct `XRGPUBinding` flags
   Chromium's already-zero-to-one projection. Native code scales both. Retain
   the passing near/far, asymmetric-eye, and rotated-pose regressions.
4. Replace or correct the previous-pose persistent-atlas presentation. The
   stable bridge gate requires current-pose render and upload/present in the
   same XR callback, or compositor-quality depth/motion-aware reprojection.
   Use the landed bounded atlas age/reuse diagnostics and fail qualification on
   positive age associated with stale-pose rotation or increasing reuse/maxima.
   Blocking `gl.finish()` is not a pose correction.
5. Repeat Automatic on Quest 3/VDXR and capture the v2 failure report before
   reloading. Test direct `XRGPUBinding` only where actually enabled. Record
   bridge timing only after projection and pose correctness pass.
6. Verify the generated report reaches provider phase/stage `running`, records
   a supported projection format, the expected presentation mode, and
   `local-floor` or `local` reference space,
   then compare rendered/skipped/input counters while checking stereo output,
   controller proxies, laser/exact-contact alignment, intro HUD, menu ordering,
   held-button disconnect, and blur. Confirm the expected presentation mode in
   the separate state snapshot from step 5.
7. Exit and re-enter once. Confirm the enter/exit/re-entry counters and bounded
   transition list change as expected, then copy or download the report. Inspect
   it before sharing and confirm it contains no game name/data, path, URL, log,
   or user-agent details.
8. After successful, declined, and failed XR entry, confirm the same flat canvas
   continues and keyboard/mouse still work.
9. Verify production HTTPS, COOP/COEP/CORP headers, WASM MIME, quota/persistence
   diagnostics, and storage survival at the final origin and path.
10. Generate the corresponding-source archive from the exact clean release
   commit and have the final hosted source offer, product terms, notices, and
   redistribution model reviewed. Keep all demo data out of the package and
   local-import-only unless explicit artifact-specific permission is obtained.

World-anchored UI/cinematic capture, procedural tracked-controller proxies,
lasers, exact-contact markers, semantic XR gameplay input, and the first
controller-aimed per-eye weapon path are shared engine/provider features, not
package-shell duplicates. Automated geometry, ordering, input, lifecycle, and
30-test native evidence passes; physical presentation, loaded-game weapon
behavior, comfort locomotion, face-button/menu policy, calibration, and
long-play browser audio remain separate runtime gates.
