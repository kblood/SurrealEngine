# WebXR launcher accessibility and recovery

## Scope

The opt-in launcher at `web/index_webxr.html?launcher=1` is the deliberate,
offline-only gate between imported UT99 data and `Module.callMain`. It remains
ordinary browser UI and does not require WebXR or DOM Overlay. The existing
automatic route is unchanged: when `launcher=1` is absent, valid data-backed
startup still calls the engine automatically.

This slice does not add multiplayer, an in-headset launcher, or proof of native
headset presentation. Once gameplay starts, launcher gamepad polling stops so
the launcher cannot steal inputs from the engine.

## Physical Brave + VDXR route

Use this exact route for the current Quest 3 setup:

```text
http://localhost:8091/web/index_webxr.html?build=build-emscripten&native-webgpu-xr=1&orientation-fix=1
```

The page exposes the same route as **Open Brave + Virtual Desktop/VDXR native
VR test route** when it detects the lifecycle-only mode. The intended physical
target is Brave 150 on the PC, Virtual Desktop with **Runtime: VDXR**, and a
connected Quest 3. The link deliberately replaces unrelated query parameters
instead of carrying launcher maps or game syntax into the native test.

The route selects native WebGPU WebXR and the current orientation correction.
It does not prove that Brave obtained an immersive session, that VDXR accepted
the headset, or that the compositor presented either eye.

On 2026-07-22, an actual Brave 150 preflight against that URL found:

- native opt-in enabled;
- `XRGPUBinding` present;
- `navigator.xr.isSessionSupported("immersive-vr")` returned `false`;
- the VDXR log reported `xrGetSystem failed with
  XR_ERROR_FORM_FACTOR_UNAVAILABLE`;
- the active software was Virtual Desktop 1.0.10 / Streamer 1.34.18.

The captured evidence is
`C:\Devstuff\QuestGames\webxr-brave-vdxr-preflight-20260722.json`. This means
the runtime/headset was not currently available to Brave. It does not isolate
an application bug and is not evidence of physical presentation.

## Keyboard and focus contract

When the deliberate launcher becomes ready, focus moves to the manual map
field. Native Tab/Shift+Tab traversal reaches controls in this order:

1. manual map field;
2. compatible imported-map list;
3. local game-preset list;
4. Refresh imported maps;
5. Start offline game;
6. Copy diagnostics;
7. Download diagnostics.

Arrow keys retain native select behavior. Enter in a valid manual map field or
on Start launches the selected offline game. Space retains native button
activation. Every interactive launcher control receives a high-contrast
`:focus-visible` outline. The map controls reference the visible keyboard and
gamepad help through `aria-describedby`.

In a crash state, focus moves to Retry startup. If Retry is unavailable, focus
moves to Restart to launcher. Controls are disabled while a navigation action
is already in flight, and repeated activation is coalesced.

## Pre-launch standard-gamepad contract

Only connected controllers with Gamepad API mapping `standard` participate.
The launcher first waits for a neutral sample, preventing a held button or
stick from activating a control as the page becomes ready.

The mappings are intentionally narrow:

| Input | Launcher behavior |
| --- | --- |
| A, button 0 | Activate the focused button |
| D-pad up/down, buttons 12/13 | Move focus |
| D-pad left/right, buttons 14/15 | Change a focused select, otherwise move focus |
| Left stick, axes 0/1 | Same directional behavior, with dead zone and held-repeat timing |
| B, Menu, View, system-like buttons 1/8/9/16 | Ignored |

Polling is active only while the visible launcher is `ready` or `crashed` and
no navigation is pending. Entering `booting` or `running` cancels its animation
frame and direct processing reports `inactive`. Non-standard mappings are
ignored. The policy therefore does not reserve gameplay actions.

Quest tracked controllers are not guaranteed to appear through the ordinary
Gamepad API before an immersive session exists. Virtual Desktop controller
exposure also needs physical validation. Keyboard/mouse remains the reliable
pre-XR fallback; controller availability must not be inferred from the
deterministic browser test.

## Honest startup and crash states

The launcher reports only named phases backed by existing application signals:

1. Checking browser support (`browser-preflight`)
2. Acquiring WebGPU device (`webgpu-device`)
3. Loading engine runtime (`runtime-script`)
4. Engine runtime initialized (`runtime-initialized`)
5. Restoring imported game data (`import-data`)
6. Restoring mutable browser data (`mutable-data`)
7. Ready for deliberate launch (`ready`)
8. Starting engine (`engine-start`)
9. Game running (`running`)

The progress element is indeterminate and has no `value`; the implementation
does not invent percentages. `aria-busy` is true only while meaningful startup
work is in progress.

A startup failure changes the phase to `crashed`, hides progress, displays a
generic sanitized message, and exposes two deliberate choices:

- **Retry startup** reloads the launcher and preserves only a currently valid
  map basename.
- **Restart to launcher** reloads with map and game parameters removed.

Neither control exports exceptions, stacks, local paths, filenames, dataset
identifiers, allowlists, or file contents. Asynchronous map refresh and
navigation actions use generation checks so an older completion cannot
overwrite a newer state.

## Verification

Deterministic Playwright coverage is in `web/test_webxr_launcher.py`:

```powershell
python -B web/test_webxr_launcher.py
```

It covers the automatic-route regression, exact keyboard order, honest phase
history, indeterminate progress, map validation, stale-refresh rejection,
neutral-latched standard-gamepad navigation, reserved-button negatives,
post-launch input shutdown, sanitized crash UI, and generation-safe retry and
restart targets.

With a locally served real Emscripten build and developer-preloaded data, run:

```powershell
python -B web/test_webxr_launcher.py --headed `
  --real-base-url http://localhost:8091 `
  --real-build build-emscripten
```

This second mode verifies that the real engine waits at the deliberate gate,
launches `DM-Deck16][` from actual data, advances engine ticks, remains
offline-only, and emits neither a page crash nor an engine fatal log. It still
does not automate a Quest headset or prove native presentation.

The 2026-07-22 run reached the deliberate gate and invoked the real engine, but
the concurrently changing shared build then logged `Fatal error: WebXR
two-hand weapon self-test failed` and remained at tick zero. The real-mode test
now reports that as a failure. It belongs to the in-progress two-hand weapon
integration/build, not to the launcher state machine; rerun this check after
that owning slice is fixed and the Emscripten build is regenerated.

## Remaining physical qualification

After Brave reports `immersive-vr` available, the following still require a
person wearing the Quest 3:

- confirm the native session and projection layer actually start;
- confirm both eyes receive correctly oriented, non-inverted game frames;
- confirm desktop mirroring and headset presentation agree;
- confirm HUD, weapon, menus, and world geometry use the intended orientation
  and placement independently;
- confirm keyboard, conventional controller, and tracked-controller ownership
  at the transition from launcher to gameplay;
- force startup failures and validate that Retry/Restart are understandable on
  the desktop fallback when DOM Overlay is absent;
- repeat disconnect/reconnect, focus loss, session exit, and browser restart;
- capture headset/runtime/browser versions and screenshots or video with each
  result.

The service worker cache version must be advanced when this launcher HTML or
script is packaged for release, because both are existing cached production
assets.
