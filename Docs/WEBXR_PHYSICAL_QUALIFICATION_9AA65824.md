# WebXR physical qualification card — `9aa65824`

This is the current data-free production candidate for flat browser play with
post-launch enter, exit, and re-entry to immersive WebXR. Automated,
synthetic-session, Electron, and actual Apache 2.4 gates pass. It is not
physical-headset proof.

## Frozen artifacts

Browser candidate:

- directory: `SurrealEngine/out/web-release-production-9aa65824`
- build ID: `9aa65824df3f-2c33259ba0d3e82d`
- source commit: `9aa65824df3fae35c987258702808becffa1f514`
- source tree: `7079a65e61ad8804e42a1e096de9fed41ae2fc36`
- release manifest SHA-256:
  `cb580e9c490e79917e6b5c10d9a905cf08d833a76eeb8ae810055712d0985bc3`
- JavaScript SHA-256:
  `e80a19471ed92f32c0a54c56531cb48bc67be39467527d9fbd1b143159d562fa`
- WASM SHA-256:
  `2c33259ba0d3e82dacf56b85e6e07bfe4c29714cbd0aeb080c9cc547244810ab`
- corresponding-source SHA-256:
  `80293d19f8dcaadce04d2e2c3d722761a0de9b5b2f16efb72a10117d443f2e2a`
- immutable generation URL after publication:
  `https://dionysus.dk/webxr/Ports/SurrealEngine/releases/cb580e9c490e79917e6b5c10d9a905cf08d833a76eeb8ae810055712d0985bc3/`

Electron diagnostic candidate:

- directory: `SurrealEngine/out/electron-webxr-9aa65824`
- Electron/Chrome: `43.2.0` / `150.0.7871.129`
- ZIP: `SurrealEngine-WebXR-Test-9aa65824df3f-2c33259ba0d3e82d-win-x64.zip`
- ZIP SHA-256:
  `981992d01e63ec7effb66968b39eb2062619fe61721c95d16c664b5118f69a86`
- status: hardened, unsigned internal diagnostic; no game data bundled

Evidence manifest:

- `SurrealEngine/qa/runs/2026-07-24/9aa65824/wp7-production-candidate/manifest.json`

The manifest records 13/13 native XR tests, browser lifecycle/backend tests,
the live UE1 direct-WebGL2 flat -> VR -> flat -> VR smoke, both-eye menu pixels,
controller hit/selection, teardown reset, Electron security/capability probes,
and matching source/package hashes.

Actual Apache 2.4.68 staging passed canonical `302` routing, expected-manifest
browser smoke, isolation and immutable-cache headers, Brotli/gzip negotiation,
correct JavaScript/WASM/source MIME types, directory-list denial, atomic
promotion, rollback to known-good `8728b24...`, and retained candidate access.
The live website was not changed.

## Required physical rows

| Row | Client | Headset/runtime | Required artifact |
| --- | --- | --- | --- |
| Q1 | Meta Quest Browser | Quest 3, native Quest WebXR | hosted immutable generation |
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
`SurrealEngine/qa/runs/<date>/9aa65824/wp7-physical/<row>/` with a manifest,
diagnostics, screenshots, and video where useful.

## Promotion boundary

Do not change the live pointer until the named physical rows pass. Promote only
this exact manifest, pin the externally expected hash in the remote smoke, and
perform the already-tested atomic rollback if any post-promotion check fails.
