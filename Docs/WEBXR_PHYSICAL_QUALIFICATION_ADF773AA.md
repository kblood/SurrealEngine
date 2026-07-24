# WebXR physical qualification card — `adf773aa`

This is the current data-free production candidate for flat browser play with
post-launch enter, exit, failure recovery, and re-entry to immersive WebXR. It
supersedes `df90483c`. Automated browser, persistence, Electron, synthetic-XR,
and Apache gates pass; physical-headset qualification is still pending.

## Frozen artifacts

Browser candidate:

- directory: `SurrealEngine/out/web-release-production-adf773aa`
- build ID: `adf773aab1e9-2c33259ba0d3e82d`
- source commit: `adf773aab1e97aaa6d36b8f74c41d7e260372b44`
- source tree: `fe366458a298a5024c722f672aa4e1fa3fca6ad5`
- manifest SHA-256:
  `d6d606d114d2cea8edb09b2517579901fef3f249dc8680cb9ec1efd3559ae7b9`
- JavaScript SHA-256:
  `e80a19471ed92f32c0a54c56531cb48bc67be39467527d9fbd1b143159d562fa`
- WASM SHA-256:
  `2c33259ba0d3e82dacf56b85e6e07bfe4c29714cbd0aeb080c9cc547244810ab`
- corresponding-source SHA-256:
  `b05f4eff8e07e62268473f6debdc9cbfe9dc09b724b102fcd5982425a4a66056`
- intended immutable URL after publication:
  `https://dionysus.dk/webxr/Ports/SurrealEngine/releases/d6d606d114d2cea8edb09b2517579901fef3f249dc8680cb9ec1efd3559ae7b9/`

Electron diagnostic candidate:

- directory: `SurrealEngine/out/electron-webxr-adf773aa`
- Electron/Chrome: `43.2.0` / `150.0.7871.129`
- ZIP: `SurrealEngine-WebXR-Test-adf773aab1e9-2c33259ba0d3e82d-win-x64.zip`
- ZIP bytes: `176974463`
- ZIP SHA-256:
  `cd9204ed72a498f02bcd6f005da25ada5b0321a386bf2b246bee16b3ca5a1348`
- executable SHA-256:
  `f56ca335e0d856f288dea44c7037c54145222e0f81f4ddfa14014d5aa4fe9f53`
- status: hardened, unsigned internal diagnostic; no game data bundled

Evidence directory:

```text
SurrealEngine/qa/runs/2026-07-24/adf773aa/diagnostics-persistence-candidate
```

- evidence manifest: `manifest.json`
- evidence manifest SHA-256:
  `b791040dd9476204a539eee53de352ea482603a53202efad915177a8e3425017`
- physical identity pin: `expected-candidate.json`

## New qualification coverage

The packaged startup report now records actual immutable JS/WASM GET status,
MIME, content encoding and cache policy; streaming compilation duration with a
tested array-buffer fallback; separate instantiation and runtime-initialization
durations; importer and mutable-storage state, schema and migration result;
selected renderer/presentation; and the first native WebGL 2 frame counter.
The report remains bounded and contains no imported filenames, game data,
saves, logs, private paths, or user-selected folder identity.

A local pointer test kept one Chrome context and origin, imported a synthetic
legal fixture, checkpointed an allowlisted `Save0.usa` sentinel to OPFS,
promoted from the prior `df90483c` generation to this exact manifest, restored
the sentinel, rolled back, and restored it again. Both generations remained
immutable and the original pointer was restored. This is rollback/save
continuity evidence, not permission to publish game data.

Completed non-hardware gates include:

- 29 browser-launcher checks, 17 mutable-persistence checks, 30 importer
  checks, and three browser-data bootstrap checks;
- all 15 Node release/WebXR suites (the Linux-only atomic-exchange test is
  explicitly skipped on Windows);
- exact packaged flat Unreal Gold launch with WebGL 2, Brotli WASM streaming
  compile, separate instantiation/init diagnostics, OPFS readiness, first-frame
  observation, layout, fullscreen, input, source compliance, and zero page
  errors;
- local immutable publish, canonical promote, save restore, rollback, and
  second save restore at one origin;
- Electron package audit, file inventory, hardened fuses and security checks;
- Electron automatic and forced-OpenXR diagnostics exit `0`; strict immersive
  diagnostics exit `2` as expected because no connected device is visible.

The native JavaScript and WASM payload hashes are unchanged from `df90483c`, so
the prior real-engine direct WebGL 2 flat → VR → flat → VR synthetic-session
evidence remains byte-applicable. The browser release assets, source archive,
manifest, and Electron ZIP are newly pinned above.

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

Use `Docs/WEBXR_PHYSICAL_RUN_TEMPLATE.md`. Generate each record with
`web/qualification/new_physical_run.py` and accept it only when
`web/qualification/validate_physical_run.py` exits `0` against the independent
`expected-candidate.json` in the evidence directory. A capability-only report
or incomplete scaffold is not passing physical evidence.

Each game record requires exactly ten enter/exit cycles, both game-UI and
headset/system exits, and the four recovery scenarios: denied entry,
headset sleep/obscuring, controller disconnect/reconnect, and forced session
end. Verify both-eye world/HUD/menu output, aim and hit-marker alignment,
exactly one menu action per trigger, turning/firing, audio position, neutral
handoff input, pointer-lock recovery, saved state, stable runtime identities,
monotonic ticks, bounded resource growth, and no large simulation delta.

## Promotion boundary

Do not change the live stable pointer until all seven physical records validate
for these exact identities. Publish the immutable generation first without
promotion for Q1. After all rows pass, promote atomically, rerun the remote
exact-manifest smoke, and preserve a tested one-command rollback record. If any
row fails, keep the current live release and retain this generation only as a
diagnostic candidate.
