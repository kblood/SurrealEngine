# Quest 3 / VDXR physical qualification

Use this card only for the frozen native OpenXR and WebXR production artifacts
from commit `71650dd7e80411532cc2baaca6623ad9404b25a9`. The later integration head
`32a2948684b2166a269ba0aeaf2425cb9d03c406` adds documentation and a focused
test regression; it is not the production artifact identity. Automated tests
do not satisfy this physical gate.

## Evidence header

Copy this header for every run. Do not attach game files, private paths, saves,
screenshots of folder pickers, or unsanitized logs.

```text
candidate: native-openxr | webxr
production_commit: 71650dd7e80411532cc2baaca6623ad9404b25a9
artifact_identity: native-zip | webxr-directory
artifact_sha256: <native ZIP hash | n/a for directory>
js_sha256: <n/a | 64 hex characters>
wasm_sha256: <n/a | 64 hex characters>
source_sha256: <64 hex characters>
release_manifest_sha256: <n/a | 64 hex characters>
path: quest-3-vdxr-native | quest-3-vdxr-webxr-auto | quest-3-vdxr-webxr-bridge
game: ut99 | unreal-gold
profile: fresh | recognized-legacy
dominant_hand: right | left
quest_os: <version>
virtual_desktop: <version>
vdxr_runtime: <version>
browser: n/a | chrome <version>
refresh_rate_hz: 72 | 90 | <other>
gpu_driver: <version>
result: pass | fail:<short category>
```

## Frozen artifact identities

Native OpenXR ZIP:

`C:\Devstuff\QuestGames\release-candidates\SurrealEngine-Native-OpenXR-71650dd7-headless-rc.zip`

- ZIP SHA-256:
  `42d64b2660ef7c7dc208979ee1199a0b0b645a950a432ebc1d97aaa5cde6cf56`
- bundled tracked-source SHA-256:
  `2533a389bfa8baa046960fa6ab03c88d3052856c6a22f2052a28c7d0f67dacdf`

WebXR package directory:

`C:\Devstuff\QuestGames\release-candidates\SurrealEngine-WebXR-71650dd7`

- `engine/SurrealEngine.js` SHA-256:
  `22dd5ec417ea5debdb0d9d9a8bd5b0bd5d6d2f759e81c22ae5b1ddba4ce128dd`
- `engine/SurrealEngine.wasm` SHA-256:
  `1ca34875dbfbd822904195ea90bceb2727a91552c210d563c3a5ede7c04acb77`
- `source/SurrealEngine-71650dd7-corresponding-source.tar.gz` SHA-256:
  `25c47c542c26943adb7b01e928f25f2987959fd4c2ddc728c6761a79f27af101`
- `release-manifest.json` SHA-256:
  `0c5bd9a59e086fc6d69babfc7763ca17acd155013714c5f0e4d4dc9cecaf8fb1`

The WebXR manifest names `/WebXR/Ports/SurrealEngine/` as its intended hosting
base. Before a hosted run, hash the served/downloaded JS, WASM, source, and
manifest; never qualify a mutable website or a build whose hashes are unknown.

## Safe start — explicit launcher only

- [ ] Use a charged Quest 3 with both controllers awake. Connect Virtual
      Desktop, select VDXR, and confirm the headset is actively connected.
- [ ] Keep owned game folders outside both candidate directories. Back up the
      test profile before migration checks; use a disposable owned install copy
      or browser store when possible.
- [ ] Native: extract the exact ZIP, verify its sidecar hash, then run
      `./SurrealEngine.exe --openxr`. Do not pass `--autoplay`, `--url`, or a
      game-folder argument. In the launcher add/select the owned folder, choose
      Vulkan, choose the dominant hand, and click **Play** only when ready.
- [ ] WebXR: open the exact candidate URL in desktop Chrome from the active
      Virtual Desktop session. Do not use an auto-start URL or developer
      override. Import/restore the owned folder, select **Immersive WebXR**,
      choose **Automatic** first, choose dominant hand, open diagnostics, and
      click **Play** only when ready.
- [ ] If Automatic is unavailable or fails, preserve its report before testing
      **Force WebGL compatibility bridge** as a separate run. Do not silently
      substitute SteamVR or count flat-window rendering as immersive success.

## Required run matrix

At minimum, qualify these runs. A failure stays a failure even if another row
passes.

| Run | Game/profile | Dominant hand | Presentation |
| --- | --- | --- | --- |
| N1 | UT99, fresh | Right | exact native OpenXR RC |
| N2 | UT99, recognized legacy | Left | exact native OpenXR RC, relaunch after migration |
| N3 | Unreal Gold | Right | exact native OpenXR RC |
| W1 | UT99, fresh | Right | exact WebXR RC, Automatic |
| W2 | UT99, recognized legacy | Left | each working WebXR backend, relaunch after migration |
| W3 | Unreal Gold | Right | each working WebXR backend |

## Controls and persistence

- [ ] A fresh profile starts with `W/A/S/D` movement and non-inverted vertical
      mouse look. Arrow keys remain usable as secondary movement controls.
- [ ] A recognized classic arrow-key profile or older Surreal profile with
      `W=Fire` migrates once to the same WASD/non-inverted baseline.
- [ ] Quit normally, relaunch through the launcher, and confirm the migrated
      controls persist. Native persists `SE-User.ini`; WebXR restores it from
      the mutable overlay.
- [ ] Do not accept a pass if a custom profile was overwritten. Custom bindings
      and an explicit inversion preference must remain untouched.
- [ ] Mouse look and mouse menu selection remain usable as fallback. Vertical
      mouse look is not inverted unless a custom profile explicitly requested it.

## Intro, menu, and dominant-hand routing

- [ ] With intro skipping disabled, the intro/prompt appears on a readable,
      world-anchored surface rather than a head-locked or black frame.
- [ ] UT99 **Press Fire** advances on one dominant-trigger press. A held trigger
      does not repeat. The other trigger retains alternate-fire ownership.
- [ ] `Escape` reaches the game and opens/closes the menu; browser Escape must
      not be lost only to pointer-lock release. The controller menu/back action
      follows the same open/close route.
- [ ] Menu, loading, prompt, and cinematic surfaces have correct aspect and stay
      in front of the world and controller overlay. The menu is not mirrored.
- [ ] Both controller models are stereoscopic 3D objects at the tracked poses.
      They are not head-locked sprites and do not swap hands.
- [ ] Only the selected dominant hand owns the menu laser and hit marker. The
      beam begins at that controller, ends at the visible marker, highlights
      exactly the item under the marker, and produces exactly one click.
- [ ] Change dominant hand in the launcher and relaunch. Primary/alternate
      triggers, weapon aim hand, weapon presentation side/mirror metadata, and
      menu laser all change together. Physical left-move/right-turn sticks do
      not swap.

## Stereo, projection, and locomotion

- [ ] In a recognizable room, left/right eyes are not swapped, mirrored, or
      vertically flipped. The UI and world have consistent handedness.
- [ ] Slowly yaw, pitch, roll slightly, lean, and translate. Fail on shear,
      stretching, world swim, incorrect convergence, stale-eye frames, or a
      view that rotates/translates at a different rate than the head.
- [ ] Vertical and horizontal FOV feel symmetric and stable at the recorded
      refresh rate. Geometry does not crop into a tunnel or distort near the
      edges.
- [ ] Distant geometry remains visible at the expected UE1 map distance. Fail
      on an artificial short black cutoff, black ring, or view-dependent loss.
- [ ] Left stick moves and strafes relative to current head/body yaw; right
      stick turns in the expected direction and rate. Diagonals work, neutral
      sticks do not drift, and focus loss does not leave movement held.
- [ ] Exit to the still-responsive flat window and re-enter immersive mode at
      least three times. No black/stale frame, dead loop, stuck input, or
      recenter jump remains after re-entry.

## Weapon aim

Keep the head still and point the dominant controller clearly away from the
head direction so false head-aim passes are obvious.

- [ ] Enforcer/basic primary fire follows the dominant controller.
- [ ] Rocket launcher primary and alternate fire both follow controller aim,
      including target acquisition and projectile direction.
- [ ] Flak Cannon primary shards and alternate projectile both follow
      controller aim.
- [ ] Shots do not follow the headset, off hand, stale hit marker, or prior
      frame. Changing dominant hand moves aim ownership to the selected hand.

## Audio and lifecycle

- [ ] A user gesture unlocks audio; effects, ambient sound, voice/cinematic
      audio where present, and music are audible at sensible relative levels.
- [ ] Open/close the menu, pass intro/loading transitions, temporarily open the
      VDXR/system overlay, remove/replace the headset, and resume focus. Audio
      neither duplicates nor permanently stops.
- [ ] WebXR: also exit/re-enter immersive mode and hide/restore the Chrome
      window or tab. The AudioContext returns to running after a user gesture if
      the browser suspended it.
- [ ] Continue for at least five minutes per test run and 30 minutes for a
      release pass. Fail on repeatable audio loss, growing latency, frame
      collapse, context/device loss, or a restart being required to restore sound.

## Title-specific gates

### Unreal Tournament

- [ ] The launcher identifies **Unreal Tournament**, not Unreal Gold, and the
      selected map/intro belongs to UT99.
- [ ] Start a ladder match, complete or legitimately advance the match, and
      reach the next ladder/map transition. Loading/menu surfaces, input,
      controller aim, audio, and immersive presentation survive the transition.
- [ ] Returning to the ladder/menu and starting another match does not produce
      a black screen, stale quad, lost audio, or trapped intro state.

### Unreal Gold

- [ ] The launcher identifies **Unreal Gold** and starts its own title/map path
      (normally `Vortex2`), never UT99 or Deck16.
- [ ] The Unreal Gold flyby/title prompt is visible. Its **Press ESC to begin**
      route responds to focused Escape/controller menu input; primary fire is
      not incorrectly treated as this title-specific Escape prompt.
- [ ] The Unreal Gold menu is readable, correctly oriented, and controllable by
      mouse and the dominant controller pointer.
- [ ] Vortex Rikers geometry renders beyond the former black/short-distance
      failure. Ambient effects are audible; continue far enough to hear music
      and confirm it survives menu/focus/immersive transitions.

## WebXR headset report gate

Copy the privacy-safe v2 report before entry, after at least 30 seconds in each
running backend, immediately on any failure, and after re-entry. A pre-entry
report with `enter_attempts: 0` does not qualify immersive presentation.

For a running report require:

- [ ] `schema: surrealengine-webxr-headset-report-v2` and final line
      `privacy: no-game-data,no-paths,no-logs`.
- [ ] `capability_code: ready`, `capability_available: yes`, and a recorded
      `xr_compatible_adapter` result.
- [ ] `provider_phase: running`, `provider_stage: running`,
      `provider_error: none`, `provider_error_stage: unknown`, and
      `provider_error_code: unknown`.
- [ ] `enter_attempts` and `successful_entries` are positive. After cycling,
      `exit_requests`, `ended_sessions`, `reentries`, and `transitions` reflect
      the observed lifecycle.
- [ ] `frames` and `input_packets` increase; `input_action_focus: focused`;
      `input_xr_standard_sources` is positive; left/right button and axis counts
      are present; `input_nonzero_thumbstick_samples` increases while moving.
- [ ] `reference_space: local-floor` is preferred (`local` accepted),
      `projection_format` is known, and exposed layer dimensions are positive.
- [ ] Automatic direct mode reports `presentation_mode: direct-webgpu` and
      `presentation_preference: auto`. Bridge fields may be zero/unknown.
- [ ] Forced bridge reports `presentation_mode: webgl-bridge`,
      `presentation_preference: webgl-bridge`, positive layer/atlas dimensions,
      increasing `bridge_frames`/`bridge_samples`, and `bridge_errors: 0`.
- [ ] For bridge runs, record `bridge_median_ms`, `bridge_p95_ms`,
      `bridge_p99_ms`, reprojection mode/counters, present-age current/max
      frames and milliseconds, reused presents, and `bridge_blocking_timing`.
      Increasing age/reuse or fallback counts must agree with observed behavior;
      they are not substitutes for stable tracking.

Share only the generated report plus the evidence header. Do not paste page
URLs, imported filenames, private paths, raw exceptions, console output, or
unsanitized engine logs.

## Sign-off

```text
native_openxr_71650dd7: pass | fail:<category>
webxr_71650dd7_automatic: pass | unsupported | fail:<category>
webxr_71650dd7_bridge: pass | unsupported | fail:<category>
ut99_ladder_transition: pass | fail:<category>
unreal_gold_title_menu_audio: pass | fail:<category>
tester: <name>
date_utc: <YYYY-MM-DD>
notes: <short, data-free summary>
```
