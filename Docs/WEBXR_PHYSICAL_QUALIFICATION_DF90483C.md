# WebXR physical qualification card — `df90483c`

This candidate is superseded by `adf773aa`; retain it only as prior automated
and rollback evidence. It was the data-free production candidate for flat browser play with
post-launch enter, exit, failure recovery, and re-entry to immersive WebXR. It
supersedes `9aa65824`. Automated, synthetic-session, Electron, and actual
Apache 2.4 gates pass; physical-headset qualification is still pending.

## Frozen artifacts

Browser candidate:

- directory: `SurrealEngine/out/web-release-production-df90483c`
- build ID: `df90483cb0fd-2c33259ba0d3e82d`
- source commit: `df90483cb0fd8b0a438962a1d954b4e0691ed53e`
- source tree: `ef6f1a14145e37516aac8ac74613dbeb54b87858`
- manifest SHA-256:
  `f3372349b5da45cb9d6bddec99317851f13735521a44c5e4932e09d63e0a99af`
- JavaScript SHA-256:
  `e80a19471ed92f32c0a54c56531cb48bc67be39467527d9fbd1b143159d562fa`
- WASM SHA-256:
  `2c33259ba0d3e82dacf56b85e6e07bfe4c29714cbd0aeb080c9cc547244810ab`
- corresponding-source SHA-256:
  `ee7374e4f545290ae15a7b70ccd1ad0a9a899881e99c5b9846c37a2f6e9b2f24`
- intended immutable URL after publication:
  `https://dionysus.dk/webxr/Ports/SurrealEngine/releases/f3372349b5da45cb9d6bddec99317851f13735521a44c5e4932e09d63e0a99af/`

Electron diagnostic candidate:

- directory: `SurrealEngine/out/electron-webxr-df90483c`
- Electron/Chrome: `43.2.0` / `150.0.7871.129`
- ZIP: `SurrealEngine-WebXR-Test-df90483cb0fd-2c33259ba0d3e82d-win-x64.zip`
- ZIP bytes: `176961856`
- ZIP SHA-256:
  `23c8842d021841720309c140e80e98b91c893d8bab1ed417c0d748ff35865489`
- status: hardened, unsigned internal diagnostic; no game data bundled

Evidence:

- manifest:
  `SurrealEngine/qa/runs/2026-07-24/df90483c/wp6-recovery-candidate/manifest.json`
- manifest SHA-256:
  `e0ba294470aba277ee2bd9ea60d7c86e3606d6992be4a87a2438c62d84881264`
- physical candidate pin: `expected-candidate.json` in the same directory

## Completed non-hardware gates

- 26 browser-launcher checks, including explicit WebGPU device-loss recovery;
- 17 mutable-persistence checks, 30 importer checks, and three browser-data
  bootstrap checks;
- version-one IndexedDB/OPFS upgrades, interrupted migration retry,
  future-schema and corrupt-data refusal, quota/interruption classification,
  and save-preserving clear behavior;
- generated WebAudio unlock, mute/volume, visibility suspend/resume, XR
  continuity, and shutdown;
- forced WebGL 2 context loss, new generation, and exact restored pixels;
- synchronous XR-session-end failure gating, flat-scheduler handoff failure,
  deny-once/next-trusted-click retry, and related WebXR suites;
- live UE1/WASM direct WebGL 2 flat → VR → flat → VR with one engine and
  renderer, both-eye menu pixels, controller hit/selection, and cleanup reset;
- data-free immutable browser packaging and corresponding-source audit;
- Electron automatic and forced-OpenXR diagnostics exit `0`; strict immersive
  diagnostics exit `2` as expected with no connected device;
- Apache 2.4.68 exact-manifest smoke, isolation headers, immutable caching,
  Brotli/gzip MIME, source MIME, directory-list denial, atomic promotion, and
  rollback to known-good `8728b24...` with both generations retained.

The public website was not changed.

## Required physical records

The matrix has seven records. D1 must pass on the same device/runtime tuple
before E1 or E2 can qualify.

| Matrix row | Game case | Client/runtime | Artifact |
| --- | --- | --- | --- |
| Q1 | `ut99` | Meta Quest Browser, Quest 3 native WebXR | hosted immutable generation |
| Q1 | `unreal-gold` | Meta Quest Browser, Quest 3 native WebXR | hosted immutable generation |
| D1 | `official-sample` | Chrome, Quest 3 + Virtual Desktop + VDXR | official Immersive Web sample |
| E1 | `ut99` | Electron 43.2.0, automatic runtime | pinned ZIP |
| E1 | `unreal-gold` | Electron 43.2.0, automatic runtime | pinned ZIP |
| E2 | `ut99` | Electron 43.2.0 with `--force-openxr` | pinned ZIP |
| E2 | `unreal-gold` | Electron 43.2.0 with `--force-openxr` | pinned ZIP |

Use the candidate-pinned procedure in `Docs/WEBXR_PHYSICAL_RUN_TEMPLATE.md`.
Generate each record with `web/qualification/new_physical_run.py` and accept it
only when `web/qualification/validate_physical_run.py` exits `0` against the
independent `expected-candidate.json` file. Generated `not-run` scaffolds and
capability-only Electron reports are not passing evidence.

Each game record requires exactly ten enter/exit cycles, both game-UI and
headset/system exits, and all four failure scenarios: denied entry, headset
sleep/obscuring, controller disconnect/reconnect, and forced session end.
Verify both-eye world/HUD/menu output, ray and hit-marker alignment, exactly one
menu action per trigger, turning/firing, audio position, neutral handoff input,
pointer-lock recovery, saved state, stable runtime identities, monotonic ticks,
bounded resource growth, and no large simulation delta. The official-sample
record must prove an actual two-view frame and exit, not only
`isSessionSupported()`.

Store records under:

```text
SurrealEngine/qa/runs/<date>/df90483c/wp7-physical/<matrix-row>/<game-case>/
```

Do not store game files, saves, private paths, raw logs, or folder-picker
captures in physical evidence.

## Promotion boundary

Do not change the live pointer until all seven records validate. Publish this
exact immutable generation first, run Q1 against its URL, then promote only the
manifest above. Pin that hash in the remote smoke and use the already-tested
pointer rollback if any post-promotion check fails. WebGPU XR and the
WebGPU-to-WebGL bridge remain experimental and cannot substitute for direct
WebGL 2 plus `XRWebGLLayer`.
