# WebXR physical qualification card — `ff89e9d1`

This card is for the first production-profile candidate that starts flat with
WebGL 2 and enters/exits WebXR after launch without restarting WASM. Automated
and mock-XR evidence does not qualify headset presentation.

## Frozen artifacts

Browser candidate:

- directory: `SurrealEngine/out/web-release-production-ff89e9d1`
- build ID: `ff89e9d1aec1-34c387709c89fff4`
- source commit: `ff89e9d1aec186a929545a7b571954c6c4a3125e`
- source tree: `7683ee83d54e67ecb3d072a0e4bbba58aa238706`
- release manifest SHA-256:
  `c039c946c1f48457f4cac881682159d004f3ff592cc612ef18e7883c74eac35e`
- JavaScript SHA-256:
  `22bd5108c7b75270e5adb42663dbe49097c6b43ee9fab651259d5952e3f9a3c3`
- WASM SHA-256:
  `34c387709c89fff4b7d33cf88f0bbaa43cbae4c2577de1865e6f3e9ac69f72c0`
- corresponding-source SHA-256:
  `fef669a6309c5fd92ce2a815ee74a699b69de69edacf6f540aa8cba68f5c289f`
- intended stable URL: `https://dionysus.dk/webxr/Ports/SurrealEngine/`
- immutable generation URL after publication:
  `https://dionysus.dk/webxr/Ports/SurrealEngine/releases/c039c946c1f48457f4cac881682159d004f3ff592cc612ef18e7883c74eac35e/`

Electron diagnostic candidate:

- directory: `SurrealEngine/out/electron-webxr-af790cce`
- wrapper commit: `8fcefa4d5a0ca928bdebc7b8db377c7bbcaf11cf`
- browser build ID: `af790cceabbd-cfc806ac7c101188`
- ZIP SHA-256:
  `cd8731a87a797d86055f50f12482ad20c7326dc5ae6d372d3e0c1c0f32c25b75`
- status: unsigned internal diagnostic package

Do not substitute a newer checkout, rebuild, mutable website, old candidate,
or a different ZIP without creating a new card.

## Required named rows

Record exact versions, not just product families.

| Row | Client | Headset/runtime | Required path |
| --- | --- | --- | --- |
| Q1 | Meta Quest Browser | Quest 3, native Quest WebXR | hosted production browser candidate |
| D1 | Chrome | Quest 3 + Virtual Desktop + VDXR | official Immersive Web sample baseline |
| E1 | Electron 43.2.0 / Chrome 150 | same Quest 3 + Virtual Desktop + VDXR | automatic runtime selection |
| E2 | Electron 43.2.0 / Chrome 150 | same Quest 3 + Virtual Desktop + VDXR | `--force-openxr` comparison |

WebGPU XR and the WebGPU-to-WebGL copy bridge are separate experimental rows;
they cannot substitute for direct WebGL2 plus `XRWebGLLayer`.

## Run header

```text
row: Q1 | D1 | E1 | E2
date_utc: <ISO-8601>
tester: <name>
result: pass | fail:<category>
quest_model: Quest 3
quest_os: <version>
quest_browser: <version or n/a>
virtual_desktop: <version or n/a>
openxr_runtime: <name and version or n/a>
gpu_driver: <version or n/a>
refresh_rate_hz: <value>
artifact_build_id: <browser build ID>
manifest_or_zip_sha256: <64 hex>
game: ut99 | unreal-gold
game_source: user-imported; not attached to evidence
```

## Preflight

1. Verify the manifest or ZIP hash above and preserve the exact artifact.
2. Confirm both controllers are awake and the headset is actively connected.
3. Run the D1 official-sample baseline before E1/E2. A failed baseline makes
   the runtime tuple unqualified; do not attribute that failure to Surreal.
4. Run packaged Electron diagnostics with `--expect-immersive-vr`. Exit code 2
   means the device is not exposed and the Electron rows stop there.
5. Import only the tester's own supported game folder. Never place game files,
   private paths, saves, folder-picker screenshots, or raw logs in QA evidence.

## Flat-to-VR sequence

For Q1, E1, and E2:

1. Launch the game flat with WebGL2 and play for at least 60 seconds.
2. Record map, player position, health, weapon, tick/frame counters, audio state,
   and the downloadable privacy-safe WebXR report.
3. Press **Enter VR**. Pass only after a current-pose stereo frame is visible in
   the headset; a desktop mirror or successful `requestSession()` is not enough.
4. Verify handed stereo, head translation/rotation, readable menu/HUD, weapon
   aim, both controllers, movement/turning, fire/alternate fire, and audio.
5. Exit once through the game UI and once through headset/system UI. Each exit
   must resume the same flat map/player/runtime without a second loading screen.
6. Repeat entry/exit ten times. Fail on a duplicate simulation update, stuck
   input/haptics, large time step, black/stale eye, lost audio, unwanted pointer
   lock, resource growth, or a WASM/runtime restart.
7. Deny one entry, sleep/obscure the headset, disconnect/reconnect one
   controller, and force one session end. Flat play must remain recoverable and
   a later entry must still work.
8. Save, quit, relaunch at the same origin, and confirm the save/import remains
   available. Capture the final privacy-safe report.

## Evidence and promotion rule

Store one manifest-backed directory per row under
`SurrealEngine/qa/runs/<date>/<commit>/wp7-physical/<row>/`. Include the run
header, browser/Electron diagnostics, the two privacy-safe headset reports, and
sanitized screenshots or video. Record failed attempts as failures.

Do not move the public stable pointer until all required rows pass. Promotion
must publish and verify the remote 52-file payload at its immutable full-manifest
hash URL, then atomically switch only the non-cacheable stable redirect using
`Docs/WEB_VERSIONED_RELEASE.md`. Rerun remote flat checks, then perform Q1 on the
exact canonical URL and record its final generation URL. A failed
post-promotion check triggers the recorded pointer rollback immediately;
preserve the bootstrap compatibility payload, every published generation, and
every transaction record.
