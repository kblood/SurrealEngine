# WebXR physical qualification card — `0cb8a55b`

This is the current data-free production candidate for flat browser play with
post-launch enter, exit, and re-entry to immersive WebXR. Automated and
synthetic-session evidence is complete; it is not physical-headset proof.

## Frozen artifacts

Browser candidate:

- directory: `SurrealEngine/out/web-release-production-0cb8a55b`
- build ID: `0cb8a55bd2fb-2c33259ba0d3e82d`
- source commit: `0cb8a55bd2fb71d56c2034603e9c29e91452f10e`
- source tree: `785e511e179e3adace5169bca8274e6b14ef7e34`
- release manifest SHA-256:
  `cdc29f7bca5f10f7a74eea4834712b36c2cbfae8d280fa5a8f0dee69fc96fb58`
- JavaScript SHA-256:
  `e80a19471ed92f32c0a54c56531cb48bc67be39467527d9fbd1b143159d562fa`
- WASM SHA-256:
  `2c33259ba0d3e82dacf56b85e6e07bfe4c29714cbd0aeb080c9cc547244810ab`
- corresponding-source SHA-256:
  `470f43d3bd7a6b0eed1a96bfd52252b4f84c147f2894a789be4a4e6f5330ef62`
- immutable generation URL after publication:
  `https://dionysus.dk/webxr/Ports/SurrealEngine/releases/cdc29f7bca5f10f7a74eea4834712b36c2cbfae8d280fa5a8f0dee69fc96fb58/`

Electron diagnostic candidate:

- directory: `SurrealEngine/out/electron-webxr-0cb8a55b`
- Electron/Chrome: `43.2.0` / `150.0.7871.129`
- ZIP: `SurrealEngine-WebXR-Test-0cb8a55bd2fb-2c33259ba0d3e82d-win-x64.zip`
- ZIP SHA-256:
  `06b1551b17979e0a91fc2b74f38e2f4b593758f568b60c123c5ec8a0173a6174`
- status: hardened, unsigned internal diagnostic; no game data bundled

Automated evidence:

- `SurrealEngine/qa/runs/2026-07-24/0cb8a55b/wp7-production-candidate/manifest.json`
- 13/13 native XR/presentation tests passed
- package/import/flat-browser smoke passed
- live UE1 direct-WebGL2 smoke passed flat -> VR -> flat -> VR with both-eye
  menu pixels, controller ray hit, trigger selection, teardown reset, and one
  unchanged engine/renderer instance
- Electron automatic and forced-OpenXR capability probes passed their
  non-immersive diagnostics; strict immersive preflight exited `2` because no
  headset was exposed

## Required physical rows

| Row | Client | Headset/runtime | Required artifact |
| --- | --- | --- | --- |
| Q1 | Meta Quest Browser | Quest 3, native Quest WebXR | hosted browser candidate |
| D1 | Chrome | Quest 3 + Virtual Desktop + VDXR | official Immersive Web sample baseline |
| E1 | Electron 43.2.0 / Chrome 150 | Quest 3 + Virtual Desktop + VDXR | automatic runtime ZIP |
| E2 | Electron 43.2.0 / Chrome 150 | Quest 3 + Virtual Desktop + VDXR | ZIP with `--force-openxr` |

WebGPU XR and the WebGPU-to-WebGL copy bridge are experimental rows and cannot
substitute for direct WebGL2 plus `XRWebGLLayer`.

## Per-row checks

1. Record exact Quest OS, browser, Virtual Desktop, OpenXR runtime, GPU driver,
   refresh rate, tester, UTC time, artifact hash, and user-imported game.
2. Play flat for 60 seconds, note a durable game-state marker, then use the
   post-launch **Enter VR** control.
3. Confirm world, HUD, flash, and menu are present in both eyes without a
   one-eye flash, corner crop, or doubled UI.
4. Move the head while checking that the controller ray and hit marker remain
   aligned with the head-locked menu; trigger one menu item and verify exactly
   one action occurs.
5. Verify tracked turning, firing, audio position, neutral input on handoff,
   pointer-lock recovery, and saved state.
6. Exit once through game UI and once through headset/system UI. Confirm the
   same flat game resumes, then re-enter without a reload.
7. Repeat enter/exit ten times; record resource growth, duplicate simulation,
   large frame deltas, skipped frames, visibility changes, and frame-time
   percentiles.
8. Exercise denied entry, headset sleep/obscuring, controller disconnect, and
   forced session end. Flat play must remain recoverable.

Run UT99 and Unreal Gold as separate named, user-imported rows. The current
direct path invokes legacy player/console `PostRender` once per eye; therefore
qualification is limited to the named games and menus actually tested. A
future offscreen UI compositor is the general solution for arbitrary UE1
scripts with render-time side effects.

Store each row under
`SurrealEngine/qa/runs/<date>/0cb8a55b/wp7-physical/<row>/` with a manifest,
diagnostics, screenshots, and video where useful.

## Promotion boundary

Before changing the live pointer, stage this exact candidate on Apache 2.4 and
verify rewrite, MIME, compression, COOP/COEP, immutable caching, source archive,
expected-manifest routing, atomic promotion, and rollback. At least Q1 and the
applicable desktop baseline/Electron rows must pass on named hardware. Until
then, retain the current live stable generation and do not publish this
candidate as qualified.
