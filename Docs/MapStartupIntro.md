# UT99 and Unreal map startup intros

Date: 2026-07-22

## Scope

UT99 and Unreal/Gold do not use the engine's AVI path for their normal startup
sequence. With no explicit launch URL, `Engine::Setup()` reads `URL.LocalMap`
from the selected game's INI and loads that map. Its actors, scripts, console,
and canvas then own the intro, the “Press Fire” gate, travel, and menu opening.
This is separate from `BrowserCinematicPlayback`, which handles file-backed AVI
content such as Klingon Honor Guard's `INTRO.AVI`.

No commercial map or package is included in this work. The state and routing
tests use synthetic descriptors, controller snapshots, and launcher metadata.

## Launcher contract

The browser launcher's checked-by-default **Skip startup intro** option keeps
the release-safe behavior that existed before this change:

```text
--autoplay --url=<validated selected map> --render=<webgpu|null> /gamedata
```

An explicit URL overrides `URL.LocalMap`, so this enters the selected playable
map directly. When the option is unchecked, the map selector is disabled and
the launcher deliberately omits `--url`:

```text
--autoplay --render=<webgpu|null> /gamedata
```

The engine then uses the selected installation's own `URL.LocalMap`. The
launcher does not guess that map, special-case UT versus Unreal, or put game
paths in browser preferences. Flat browser and WebXR use the same arguments.

## Engine lifecycle

The engine records a startup-intro state only after the no-URL `LocalMap` load
in `Setup()`. Any later `LoadMap()` or `LoadFromSaveFile()` clears it. Detecting
an actual UE1 menu also completes it. This makes the state a narrow description
of the initial map/script transition rather than a permanent game mode.

Desktop rendering and input do not branch on this state. XR surface activation
has no visible effect unless a provider has configured the corresponding
surface, so native desktop and flat WASM continue to use the ordinary canvas.

## WebXR presentation and input

The projection views continue to render the LocalMap as normal stereo world
content. While its startup state is active and no menu is open, WebXR captures
the existing player/console `PostRender` canvas into the noninteractive HUD
surface. This places prompts such as “Press Fire” on a world-anchored quad
instead of making them head-locked or losing them when projection rendering
suppresses the slot-zero UI layer.

When UE1 opens either its scripted menu or UWindow menu, the startup HUD is
hidden and the existing interactive menu surface replaces it. Menu composition
order remains higher than HUD, cinematic, loading, controller, and world
content. The existing exact ray/contact path and desktop mouse fallback are
unchanged.

Preserved headset evidence established that setting a player `bFire` action was
not enough for the UT startup gate: the intro console/script needed an ordinary
`KeyEvent` fire path. WebXR therefore mirrors a trigger edge to primary fire
(right hand) or alternate fire (left hand) only while the startup state is
active and no menu is open. Its matching release is guaranteed even if the
intro ends or the controller disconnects. Outside that interval the shared
semantic XR profile handles Fire/AltFire directly without depending on
`IK_Joy1`/`IK_Joy9` or the user's legacy joystick bindings.

This implementation does **not** render the complete map from a separate mono
camera into a texture. It is the smallest fix for the observed black prompt and
unreachable-menu failure while preserving normal stereo map rendering. A full
map-to-quad mode should only be added if owner-data testing proves the intro's
world camera itself is uncomfortable or invalid; that would require an explicit
view-family/target policy rather than a game-specific renderer fork.

## Deterministic validation

The following checks pass without game data:

- Windows x64 Release links the full `SurrealEngine` executable.
- A no-data Emscripten Release build links `SurrealEngine.js` and
  `SurrealEngine.wasm`.
- `WebXRInputRuntimeTests` verifies intro-only fire mirroring, menu exclusion,
  left/right semantics, release after transition, and disconnect cleanup.
- `WebXRUIProviderTests` verifies the provider-owned HUD target is
  noninteractive and the menu remains interactive and topmost.
- `XRUISurfaceEngineBindingTests` verifies a world-anchored startup HUD hands
  off to the interactive menu without coexisting surfaces.
- The browser launcher smoke suite passes 11 checks, including both direct-map
  skip arguments and deliberate `LocalMap` arguments.

## Owner-data and Quest release gates

An exact owned GOG UT99 installation now has a repeatable flat-browser gate in
`web/smoke_test_owner_game.py`. `--startup-mode local-map-intro` unchecks the
launcher option, waits through the real `LocalMap` sequence, sends one primary
fire edge, and requires the browser main loop to keep advancing. The direct-map
mode remains the default so the two startup paths cannot hide failures in each
other.

The public Experimental build imported all 496 files (629.3 MiB), entered the
real UT99 fly-through, advanced from tick 1 through tick 1300, rendered a
nonblank WebGPU frame, and reported no page or WebGPU errors. A primary-fire
edge accepted late in the sequence then left the exported tick counter fixed at
1300 for 120 seconds. This is a failed gate, not evidence that the menu works:
it must be distinguished between a map-travel Asyncify stall and an intentional
paused-menu state with new browser diagnostics before normal intro can ship.

Before enabling normal intro by default, test both an owned UT99 installation
and an owned Unreal Gold installation:

1. Confirm the imported INI's `URL.LocalMap` exists and is loaded when the
   checkbox is unchecked; confirm the checked option still opens the selected
   game/map and never a stale UT99 map.
2. In flat WASM, complete the normal intro with mouse/keyboard and verify travel
   reaches the correct menu. Repeat skip-intro and direct map launch.
3. On Quest WebXR, verify the prompt quad is readable and world-anchored, a
   trigger advances the gate exactly once, releasing/repressing does not cause
   accidental menu selection, and the menu replaces the prompt above all other
   content.
4. Verify controller laser/contact, mouse fallback, menu close/reopen, session
   loss, and controller disconnect after the transition.
5. Check Unreal Gold separately: its LocalMap and script sequence may differ
   from UT99 even though the engine lifecycle is shared.

Native OpenXR does not yet use the WebXR startup-trigger bridge or configure
this browser HUD capture. Its UT99 VR release must retain its already validated
intro-skip behavior, or receive a separate provider-neutral input/presentation
follow-up backed by headset evidence. Do not claim normal native-OpenXR intro
support from these browser changes.
