# SurrealEngine `55433b37` candidate release notes — draft

Source commit: `55433b37275a49bf9e5a8c61fe721165c37a96bc`

This candidate brings native desktop, native OpenXR, browser WebAssembly/WebGPU,
and experimental WebXR onto the same engine foundations. Desktop keyboard,
mouse, launcher, rendering, and game behavior remain the default; XR is an
optional presentation and input layer rather than a separate game-engine fork.

This is a test candidate, not a fully qualified public release. Automated gates
pass, but the native OpenXR and WebXR paths still require the physical headset
checks listed below.

## Included engine paths

- **Native desktop:** existing Windows launcher and Vulkan/D3D11 desktop play.
- **Native OpenXR:** Windows PC VR through an OpenXR runtime using Vulkan.
- **Browser desktop:** WebAssembly with a flat WebGPU canvas.
- **WebXR preview:** immersive browser presentation through Automatic direct
  WebGPU/WebXR where supported, with a forced WebGL compatibility bridge as a
  separately selected fallback.

Native and browser XR now share provider-neutral view, input, UI-surface,
weapon-aim, dominant-hand, haptic, and lifecycle policy. Providers translate
their platform APIs into those contracts; they do not fork gameplay or menus.

## Games and game data

The launcher currently exposes the two relatively playable UE1 targets:

- Unreal Tournament, version 436;
- Unreal Gold, principally versions 226/227 supported by SurrealEngine.

Users must select their own lawful game installation. The native launcher reads
the selected folder in place. The browser launcher imports a private local copy
into browser storage so it can reopen the game later; the browser may label the
folder operation “upload,” but game files are not sent to the website or a
server.

**No commercial game data is included in the native or browser candidate.** No
UT99, Unreal Gold, Deus Ex, demo, map, texture, music, save, user profile, or log
is bundled. Historical UE1 demos remain local-import experiments and are not
advertised as redistributable content.

## Launcher use

Native OpenXR should be started with:

```powershell
./SurrealEngine.exe --openxr
```

Do not add `--autoplay`, a map URL, or a game-folder argument when using the
release launcher. Add/select the owned game folder, choose Vulkan, choose the XR
dominant hand, and click **Play** explicitly. D3D11 cannot bind the native
OpenXR provider and retains the desktop fallback.

For the browser candidate, open the exact HTTPS candidate, import or restore an
owned game folder, select the game/map, renderer, presentation, and dominant
hand, then click **Play**. Choose **Desktop window** for flat WASM/WebGPU or
**Immersive WebXR** for the experimental headset path. Start WebXR with
**Automatic**; test **Force WebGL compatibility bridge** separately if needed.

## Input and migration

Fresh desktop profiles now use `W/A/S/D` movement and non-inverted vertical
mouse look. Arrow keys remain secondary movement bindings.

At startup, the compatibility overlay recognizes an untouched classic
arrow-key profile, the older Surreal profile with `W=Fire`, or an empty movement
layout. It migrates that profile once to WASD/non-inverted controls and persists
the result. Native shutdown writes `SE-User.ini`; the browser mutable overlay
checkpoints and restores the same file. Customized layouts and an explicit
inversion preference are left unchanged.

Keyboard and mouse remain active alongside XR sources. Releasing or losing an
XR controller removes only that source and does not erase desktop input.

## XR controls and UI

The launcher has one persistent **XR dominant hand** setting, with Right as the
default and Left as the alternative. It consistently selects:

- primary-fire versus alternate-fire trigger roles;
- startup-intro primary-trigger routing;
- the hand supplying weapon aim/grip pose;
- the dominant menu laser and exact-contact marker;
- left-handed weapon-presentation mirror metadata.

Physical locomotion remains conventional: left stick moves/strafe and right
stick turns. Changing dominant hand does not swap the sticks. This candidate
does not add dual Enforcers, two-hand weapon handling, or replacement weapon
meshes.

XR menus and startup surfaces are world-anchored rather than head-locked. Both
tracked controllers have stereoscopic procedural 3D proxies while UI is active;
only the dominant controller draws the menu laser and hit marker. The marker is
derived from the same exact surface hit that controls hover and click. Physical
mouse pointing/clicking remains available as fallback, and the menu is composed
above world, loading, intro, and cinematic surfaces.

UT99 weapon fire uses the controller aim only inside bounded weapon VM scopes,
including projectile/trace aim and the special Rocket Launcher, Flak Cannon,
Impact Hammer, and related primary/alternate paths. Ordinary movement, camera,
and unclassified script execution retain head/body orientation.

## Native OpenXR changes

- Optional OpenXR lifecycle, Vulkan device/session selection, stereo swapchains,
  view translation, and balanced frame submission are integrated with the
  ordinary engine loop.
- Native and WebXR use the same semantic controller mapping, head-relative
  locomotion, weapon-pose solver, haptic outcomes, and UI input policy.
- Native UI uses projection plus ordered topmost quad layers. Runtimes with
  sufficient layer capacity can also receive the controller-visual overlay;
  lower-capacity runtimes retain the required world plus four UI layers.
- A no-map UT99 OpenXR launch uses the tested Deck16/startup-menu handoff rather
  than trapping the player in the looping Entry/CityIntro sequence. Explicit
  map launches remain ordinary gameplay launches.
- OpenXR failure or an incompatible renderer falls back to the existing desktop
  path instead of changing non-XR behavior.

## Browser and WebXR changes

- Browser game selection supports isolated UT99 and Unreal Gold libraries,
  validated safe-map choices, per-game preferences, and persistent mutable
  settings/saves under a strict allowlist.
- The flat browser path has relative mouse capture, forwarded Escape intent,
  resize/fullscreen handling, audio lifecycle recovery, and user-visible startup
  status independent from WebXR availability.
- WebXR has explicit session entry/exit/re-entry ownership, frame/input ABI
  validation, provider-neutral controller snapshots, direct WebGPU presentation
  where the browser supports it, and a forced WebGL bridge fallback.
- The collapsed diagnostics panel produces an allowlisted
  `surrealengine-webxr-headset-report-v2` containing lifecycle, frame/input,
  projection, dimensions, and bridge timing/counter fields. It excludes game
  data, paths, URLs, user-agent text, raw logs, and raw exception messages.

## Automated status

The exact native candidate passed the 40-test Release CTest registry, including
OpenXR view/extensions/UI, XR input/diagnostics/weapon/haptics, UI surfaces,
presentation, VM hooks, and desktop-default controls. The focused
OpenXR/XR/input/VM/default-controls selection passed 21/21.

Browser syntax, launcher/import, mutable-persistence, presentation-provider,
diagnostics, audio, and WebXR bridge/runtime harnesses are automated and
data-free. These gates establish contracts and lifecycle behavior; they do not
replace a Quest 3 headset pass with owned game data.

## Manual qualification still required

Before calling either XR path release-ready, qualify the exact immutable
artifact on Quest 3 and record the runtime/browser/refresh-rate matrix. Required
manual gates include:

- stable stereo projection and FOV with no swapped/mirrored eyes, vertical
  inversion, shear, world swim, incorrect convergence, or short black
  draw-distance cutoff;
- intro, loading, cinematic, and menu surfaces with correct aspect/orientation,
  topmost ordering, Escape/controller-trigger handoff, and no black-frame trap;
- both 3D controller proxies, dominant laser, exact marker/highlight/click
  agreement, mouse fallback, and right/left dominant-hand persistence;
- head-relative left-stick locomotion, right-stick turn, no drift/stuck input,
  and clean focus/session re-entry;
- Rocket Launcher and Flak Cannon primary and alternate shots following the
  controller rather than headset aim;
- audio surviving intro/menu/map changes, focus/visibility suspension, and
  immersive exit/re-entry without requiring a restart;
- a UT99 ladder match and next-map transition without lost presentation, input,
  menu, or audio;
- Unreal Gold detection/title path, focused Escape prompt/menu, Vortex Rikers
  rendering/draw distance, ambient audio, and later music;
- WebXR Automatic and forced-bridge reports captured before entry, while
  running, on failure, and after re-entry, with increasing frame/input counters,
  zero unexpected bridge errors, and the final privacy marker.

See `Docs/RC_PHYSICAL_QUALIFICATION.md` in the qualification-docs topic for the
full pass/fail card. A successful flat-window run is not evidence that immersive
OpenXR or WebXR presentation passed.

## Licenses and source

The native package includes project and third-party license notices, the
OpenXR-SDK notice files, the matching Microsoft VC runtime notice, and a complete
tracked-source archive identified by commit and SHA-256. `SurrealVideo.dll` is
dynamically linked and its LGPL 2.1-or-later license, notice, and corresponding
source are included.

The browser release must ship its matching clean source/relinking bundle and
release manifest because SurrealVideo is statically included in the WASM build.
Artifact and source hashes must match the exact published candidate. These
materials do not grant rights to redistribute any original game data.
