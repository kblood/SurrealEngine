# UT99 → Native VR Mod Plan

**Goal:** Get Unreal Tournament 1999 playable in VR on the Quest 3 (via Virtual
Desktop/PCVR, same setup as the CP2077 VR work), by adding OpenXR stereo
rendering + 6DoF input to **SurrealEngine**, a clean-room reimplementation of
Unreal Engine 1. NOT a WebXR/browser port (that was considered and explicitly
deferred in favor of this smaller, native-first scope — see decision log
below). Multiplayer/networking is out of scope (SurrealEngine has none yet).

## Why SurrealEngine, and why native-first

- Epic never open-sourced UE1/UT99. The only public, legally clean, complete
  engine is SurrealEngine (`github.com/dpjudas/SurrealEngine`) — zlib-style
  license, explicit clean-room reimplementation (no Epic code), active
  development (commits as recent as yesterday).
- It already targets **Direct3D 11 and Vulkan** natively on Windows — no
  browser renderer exists (`Docs/Status.md`: "There is no OpenGL renderer"),
  which ruled out a QuakeQuest-style WebXR/Emscripten port for now (would
  require writing a whole new WebGL2/WebGPU renderer backend first). A
  Three.js + UTPackage.js (CC0) ground-up WebXR engine was also considered —
  lower rendering risk than Emscripten-porting SurrealEngine, but a much
  larger game-logic-authoring lift (write gameplay from scratch vs. extend
  an existing, if alpha-quality, engine). User chose native-first: reuse
  SurrealEngine's existing Vulkan/D3D11 backend + OpenXR injection, same
  playbook as the CP2077 VR mod (`cp2077-vr/`). WebXR remains a possible
  future phase, not committed to now.
- Caveat going in: SurrealEngine is alpha-quality (`Docs/Status.md`).
  Only UT v436 and Unreal Gold v226 are "relatively playable"; v469 (the
  common GOTY patch) boots but has broken touch events; anything past 469c
  won't boot. UnrealScript VM missing arrays + network conditionals. Bot AI
  barely functional (natives unimplemented). No networking. Frequent
  crashes on interaction. Expect to hit engine bugs unrelated to VR work.

## Legal approach for filling engine gaps

SurrealEngine is a legally clean clean-room reimplementation (same model as
OpenRA/ScummVM/OpenMW: reverse-engineer behavior/formats, never copy Epic's
code). To preserve that when filling in missing native functions / engine
gaps:
- Primary source: public UnrealScript community documentation (Unreal Wiki,
  BeyondUnreal, 25 years of modding docs) + observed in-game behavior.
- If decompilation is ever used to understand something undocumented: strict
  two-agent clean-room split. One agent studies/decompiles and writes a
  plain-language behavioral spec; a SEPARATE agent with no access to the
  original code, the decompiled output, or the first agent's transcript
  implements purely from that spec. Never copy or closely mirror decompiled
  code into what we write.

## Architecture (target)

```
SurrealEngine (existing, native Windows)      + VR layer (new)
├── SurrealGPU (D3D11RenderDevice, Vulkan)  →  ├── stereo render targets, per-eye pass
├── engine core (UnrealScript VM, actors)   →  ├── unchanged
├── SurrealVideo (windowing/input)          →  ├── OpenXR session replaces window loop
└── SurrealCommon                           →  └── OpenXR head/controller pose → view + input
```

Likely closest precedent in this workspace: `cp2077-vr`'s `dxgi_proxy.cpp` +
`openxr_manager.cpp` (OpenXR session management, D3D12 swapchain interop,
XInput merge for controller input) — same category of problem, different
engine and (probably) Vulkan instead of D3D12/DXGI hooking, since we control
the engine source directly here rather than injecting into a closed binary.

## Workspace layout

```
ut99-vr/
├── PLAN.md                 ← this file
├── SurrealEngine/           ← cloned engine (build target, will carry our VR patches)
└── (game data NOT stored here — user must own a legit UT99/Unreal copy;
     never commit/distribute game assets)
```

## Milestones

1. **M1 — Native flatscreen build verified. DONE (2026-07-18).** SurrealEngine
   compiles and actually runs a real UE1-family game flat on this PC, using
   the user's legitimately-owned game files (Deus Ex GOTY 1112fm — user
   doesn't own UT99 yet, see Status log). Proves the toolchain and gives a
   stability baseline before adding VR complexity.
2. **M2 — OpenXR session + stereo rendering.** Add an OpenXR path to the
   Vulkan render device: head-tracked stereo rendering, two render targets
   per frame, submitted to the XR compositor.
3. **M3 — VR input.** 6DoF controller poses → weapon aim decoupled from
   view, movement/turning, menu interaction — same category of design
   problem as the CP2077 mod's XInput/OpenXR merge layer.
4. **M4 — Playability pass.** Comfort options, whatever engine bugs M1-M3
   surfaced along the way, real in-headset verification via Virtual Desktop.
5. **M5 — Write (not build) a WebXR/Three.js port plan.** Originally
   scoped for after M1-M4 finish; **moved up 2026-07-18** (M2 blocked on an
   elevated-permissions step, see status log) to use the gap productively.
   Written as `WEBXR_PORT_PLAN.md` — a planning document only, does not
   change the underlying "native VR first" decision.

See `VR_IMPLEMENTATION_PLAN.md` for the granular, file:line-cited technical
plan for M2-M4 (architecture recon, exact injection points, a
fully-non-interactive verification strategy for each step, and the one step
— final in-headset feel check — that genuinely needs the user present).

## Verification

- Flatscreen builds: run directly on this PC (`build/Release/SurrealEngine.exe`).
- VR builds: Virtual Desktop streaming to Quest 3, same setup as `cp2077-vr`
  (`restart-vd-streamer.ps1` if the streamer needs a kick).

## Status log

- 2026-07-18: Project started. Researched existing UT99 VR ports (none
  exist — community wishlist only, Team Beef/Dr Beef never built one) and
  engine source availability (Epic never open-sourced UE1/UT99; OldUnreal
  has private/partial access only). Found SurrealEngine (clean-room,
  zlib-licensed, active) as the viable engine base. Considered and deferred
  a Three.js/WebXR ground-up path (UTPackage.js CC0 solves asset parsing,
  Three.js solves WebXR — rendering risk would be low, but game-logic
  authoring risk would be much higher than extending an existing engine).
  User decided: native VR mod on SurrealEngine first, WebXR later if ever.
  Cloned `dpjudas/SurrealEngine`, built clean via CMake + MSVC
  (`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` needed — vendored openal-soft's
  CMakeLists predates modern CMake's minimum-version floor). All three
  executables (`SurrealEngine.exe`, `SurrealEditor.exe`,
  `SurrealDebugger.exe`) built clean, zero errors.
- 2026-07-18: **M1 done.** Ran `SurrealEngine.exe` against the user's
  legitimately-owned Deus Ex GOTY install (`C:\Program Files (x86)\GOG
  Galaxy\Games\Deus Ex GOTY`, GOG's 1112fm build). Launcher auto-detected it
  correctly (game list showed "Deus Ex 1112fm" with the right path). Clicked
  Play — engine runs in **exclusive fullscreen D3D11/Vulkan** (window rect
  0,0,2560,1440, confirmed foreground), which is invisible to normal GDI
  screen capture (`CopyFromScreen`/`BitBlt` show stale desktop-compositor
  content, not the live frame — a known limitation with exclusive-fullscreen
  DirectX/Vulkan apps). Worked around it with `PrintWindow` +
  `PW_RENDERFULLCONTENT` (flag 2), which captures the real backbuffer
  directly from the GPU. Confirmed genuine real-time 3D rendering: the
  classic Deus Ex "Ion Storm" intro logo (rotating chrome emblem + small
  globe), moving between successive captures — not a static frame. Toolchain
  is proven end-to-end: build → load real game data → GPU-render real UE1
  content. Didn't push further into the main menu/training map this pass
  (not needed to prove M1; alpha-quality engine may still hit bugs there
  per Docs/Status.md). User does not yet own UT99 itself — will need to
  acquire a legit copy (UT v436 preferred per SurrealEngine's
  best-supported list) before testing the actual target game; Deus Ex GOTY
  was a valid stand-in for proving the toolchain.
- 2026-07-18 (later): User installed UT99 GOTY (GOG). GUI mouse-automation
  of the launcher (clicking "Add", browsing for a folder) proved unreliable
  cross-session and once caught an unrelated private window in a
  full-desktop screenshot — not acceptable to repeat. Fixed at the root:
  added a `--autoplay` command-line flag to `GameApp::main`
  (`SurrealEngine/GameApp.cpp`) that skips the launcher GUI entirely —
  `SurrealEngine.exe --autoplay "<game folder>"` resolves and launches the
  game directly. Rebuilt clean. Verified against the real UT99 GOTY install
  (`C:\Program Files (x86)\GOG Galaxy\Games\Unreal Tournament GOTY`): window
  title confirmed "Unreal Tournament (v436)" — v436 is one of SurrealEngine's
  best-supported versions. Captured live GPU rendering via `PrintWindow` +
  `PW_RENDERFULLCONTENT` (needed since the engine runs exclusive-fullscreen
  D3D11/Vulkan, invisible to normal desktop screenshot APIs): a real UT99
  city-rooftop map rendering correctly. **M1 is now proven against the
  actual target game**, fully non-interactively — `--autoplay` is the
  standard way to launch/verify from here on, no manual clicking required.
  User has directed autonomous work going forward (away from the machine);
  modify engine source directly wherever that removes a dependency on
  manual interaction. Wrote `VR_IMPLEMENTATION_PLAN.md` (granular M2-M4
  technical plan, file:line-cited engine recon, non-interactive
  verification strategy per step). Started M2: vendored the OpenXR loader
  via CMake `FetchContent` (`KhronosGroup/OpenXR-SDK`, `release-1.0.34`,
  same pin `cp2077-vr` uses), added
  `SurrealEngine/RenderDevice/Vulkan/VulkanXRSession.{h,cpp}` (instance +
  system probe only, no session/swapchain yet) and a `--probexr`
  diagnostic CLI flag. Built clean after two header-order fixes (needed
  `<surrealgpu/vulkaninstance.h>` before `openxr_platform.h` for `VkInstance`,
  and `<unknwn.h>` for `IUnknown` under `XR_USE_PLATFORM_WIN32`). Ran
  `SurrealEngine.exe --probexr`: **confirmed real, working OpenXR↔Vulkan
  plumbing** — instance created OK, runtime identified as
  `"VirtualDesktopXR"`, both `XR_KHR_vulkan_enable`/`enable2` advertised;
  `xrGetSystem` correctly returned `XR_ERROR_FORM_FACTOR_UNAVAILABLE` since
  no headset is connected right now (expected, user away from the machine).
  Entirely non-interactive, no window/game required.
- 2026-07-18 (continued): **Built `--debugstereo`.** Added a
  `ViewportOverride` struct threaded through `VisibleFrame::Process` /
  `SetupSceneFrame` (`SurrealEngine/Render/VisibleFrame.h/.cpp`, new
  optional trailing param, defaults to `nullptr` so normal flatscreen
  rendering is byte-for-byte unchanged), and
  `RenderSubsystem::DrawSceneStereo()` (`SurrealEngine/Render/RenderScene.cpp`)
  which renders the scene twice per frame — camera offset ±32 UE1 units
  along the rotation's Y-axis (fake debug IPD, uncalibrated), each eye's
  own `worldToView` + a left-half/right-half `ViewportOverride` — wired in
  behind the `--debugstereo` flag check in `RenderSubsystem::DrawGame`
  (`RenderSubsystem.cpp`). Built clean. Ran
  `SurrealEngine.exe --autoplay --debugstereo` against real UT99: no crash,
  window title confirmed "Unreal Tournament (v436)", `PrintWindow`
  screenshot shows **two clearly different, parallax-shifted views of the
  same map side by side** (city rooftop scene — a "WATCH THE TOURNAMENT ON
  NEUROVISION" billboard and a cracked-pavement floor detail both visible
  from two distinct angles). Real proof the viewport-override +
  per-eye-view-matrix plumbing is correct, entirely non-interactively,
  before any OpenXR session/swapchain code exists.
- 2026-07-18 (continued): **M2 step 6/7 done — asymmetric projection.**
  Added `FSceneNode::ProjectionOverride` (`RenderDevice.h`); when true,
  `SetSceneNode` in both `D3D11RenderDevice.cpp:2445` and
  `VulkanRenderDevice.cpp:966` uses `Frame->Projection` verbatim instead of
  deriving a symmetric `mat4::frustum` from `FovAngle` — a `nullptr`/false
  override reproduces the old behavior exactly, so every non-VR call site
  is unaffected. `ViewportOverride` (`VisibleFrame.h`) gained an optional
  `const mat4* Projection` so callers can supply the override; when
  present, `VisibleFrame::SetupSceneFrame` sets `Frame.Projection` and
  `Frame.ProjectionOverride = true` directly, which also fixes the
  CPU-side BSP clipper (`Clipper.Setup`) to use the real matrix instead of
  a symmetric approximation. Upgraded `--debugstereo` from the previous
  toe-in-free-but-symmetric approach to a proper **parallel-axis +
  off-axis-frustum** stereo method (the same shape of asymmetry a real
  per-eye OpenXR projection has) — each eye's frustum is horizontally
  sheared by `frustumShift = -sign * halfIPD / convergence` so both eyes
  converge on the same point at an arbitrary mid-range convergence
  distance. Verified via `SurrealEngine.exe --autoplay --debugstereo
  "<UT99 GOTY folder>"` (GOG install, `C:\Program Files
  (x86)\GOG Galaxy\Games\Unreal Tournament GOTY` — note: `--autoplay`
  needs the game folder as a positional arg unless one is already saved in
  `LauncherSettings`' search list, which was empty this run) — no crash,
  correct window title, `PrintWindow` screenshot shows two clean
  parallax-shifted views (caution-striped wall / torch pillar visibly
  shift position between halves), no geometry corruption. Proves
  `ProjectionOverride` end-to-end, both device backends.
- 2026-07-18 (continued): **M2 step 8 done — decoupled scene buffer from
  window size.** Added `RenderDevice::FixedRenderWidth/Height` +
  `SetFixedRenderSize(w,h)` / `GetRenderWidth()` / `GetRenderHeight()`
  (`RenderDevice.h`/`.cpp` — out-of-line, since `Widget` is only
  forward-declared in the header and inlining the calls to
  `Viewport->GetNativePixelWidth()` broke every other translation unit
  that includes `RenderDevice.h` without the full `Widget` definition).
  `0` (default) means "use the window size", unchanged behavior.
  `VulkanRenderDevice::Lock()`'s scene-texture recreation check and
  `ReadPixels()`'s screenshot-buffer sizing now call `GetRenderWidth/
  Height()` instead of reading `Viewport->GetNativePixelWidth/Height()`
  directly; `Unlock()`/`DrawPresentTexture()` (the actual swapchain
  present) intentionally still use the real window size, since that's the
  desktop mirror, not the scene target — VR's real per-eye render size and
  its windowed-mirror size are two different things (mirror is M2 step 9,
  tracked separately). D3D11 backend left untouched — Vulkan is the chosen
  VR backend per this plan, D3D11 isn't in the VR path. Added a
  `--debugfixedsize=WxH` diagnostic (parsed once in `Engine::Run()` right
  after the `RenderSubsystem`/`Device` are constructed, since `Device`
  doesn't exist until partway into `Run()` — calls
  `render->Device->SetFixedRenderSize(w,h)`). Verified via `--autoplay
  --debugfixedsize=800x600 "<UT99 GOTY folder>"`: no crash, correct window
  title, `PrintWindow` screenshot shows the full window filled with
  visibly blurrier/upscaled content (800x600 source stretched to the
  2560x1440 window by the present shader's texture sampling) vs. the sharp
  native-res screenshots from the asymmetric-projection test above —
  direct visual evidence the scene buffer really is being created at a
  size independent of the window.
  **Correction (2026-07-18, Fable review):** the "blurrier/upscaled"
  screenshot only proved the offscreen buffer's *pixel dimensions* were
  really 800x600 (confirmed by the blur-on-upscale artifact) — it did
  **not** prove the content rendered into that buffer was correct. A
  separate bug in `VisibleFrame::SetupSceneFrame` (`VisibleFrame.cpp`) was
  still deriving `Frame.X`/`Frame.Y` (which drives the GPU viewport rect
  set in `VulkanRenderDevice::SetSceneNode`) from
  `engine->viewport->ViewportWidth/Height()` (the window size) instead of
  `Device->GetRenderWidth/Height()` (the fixed render size). That meant the
  GPU viewport was set larger than the actual 800x600 framebuffer, so
  Vulkan clamped/discarded out-of-bounds fragments — the scene was being
  **cropped** to the buffer's top-left corner (wrong FOV/framing), not
  cleanly downscaled to fit it. The blurry screenshot looked plausible
  because upscaling a cropped image is still blurry; the crop itself
  wasn't visually obvious at a glance. **Fixed** by changing
  `SetupSceneFrame` to read `Device->GetRenderWidth()/GetRenderHeight()`
  for `Frame.X`/`Frame.Y`/`Frame.FX`/`Frame.FY` directly, so the viewport
  rect always matches the real framebuffer size. This matters a lot for VR
  specifically: a cropped-then-upscaled per-eye image would have shipped a
  wrong FOV to the headset.

  **Two more bugs found by the same Fable review, both fixed alongside the
  crop bug (2026-07-18):**
  - `VisibleFrame::DrawPortals()`'s sky/portal/mirror subframes
    (`skyframe.Process`/`portalframe.Process`/`mirrorframe.Process`) didn't
    receive the parent frame's `ViewportOverride`, so under `--debugstereo`
    a skybox or mirror visible in a scene would render full-window with a
    symmetric projection instead of respecting the per-eye viewport/frustum
    — spilling across the other eye's half. Fixed by building a
    `subViewport` from the parent `Frame` (XB/YB/X/Y + `&Frame.Projection`)
    and passing it through to all three subframe calls.
  - `VulkanRenderDevice`'s `RFX2`/`RFY2`/screen-space-center constants (used
    by `DrawTile`/`Draw2DLine`/`Draw2DPoint` to convert screen-space
    tile/line coords into view space) were derived from `FovAngle` alone,
    which is only correct for a symmetric frustum — under an asymmetric
    `ProjectionOverride` (a real per-eye OpenXR frustum), 2D
    tiles/coronas/lines would be horizontally displaced from the 3D
    geometry. Fixed by deriving `RFX2`/`RFY2` and new `ProjCenterX`/
    `ProjCenterY` members directly from the resolved projection matrix
    (`VulkanRenderDevice.cpp` `SetSceneNode`) instead of `FovAngle`,
    algebraically verified to reduce to the old formulas for a symmetric
    frustum. All 10 `Frame->FX2`/`FY2` usage sites in `DrawTile`/
    `Draw2DLine`/`Draw2DPoint` switched to `ProjCenterX`/`ProjCenterY`
    (`Draw3DLine`'s separate ortho/2D-UI branch intentionally left alone).

  **Re-verified 2026-07-18** with all three fixes applied, using a pinned
  `--url=DM-Deck16][` map (avoids the intro-camera's random attract-mode
  sweep, which had produced a misleading transient black frame during an
  earlier, inconclusive check) instead of bare `--autoplay`:
  - Flatscreen (`--autoplay --url=DM-Deck16][`): full 2560x1440 window
    filled edge-to-edge, correct HUD centering, no crop/black-half.
  - `--debugstereo`: clean L/R split, each eye fills its full half-viewport,
    correct parallax (a skylight visible through a gap is visible only in
    the right eye, consistent with a rightward eye offset), no black
    corruption from sky/portal subframes.
  - `--debugfixedsize=800x600`: full scene now renders with the *same*
    framing/composition as the native-res flatscreen shot (just blurrier
    from upscaling) — confirms the crop is gone, not just that the buffer
    was smaller.
  All three fixes committed to the `vr-m2` branch.
  Next: M2 step 4/10 — build the real OpenXR session/swapchain in
  `VulkanXRSession` (currently instance+system-probe only):
  `xrCreateSession` with a Vulkan graphics binding, `xrCreateSwapchain`,
  and `xrWaitFrame`/`xrBeginFrame`/`xrLocateViews`/`xrEndFrame` wired
  around `Engine::Run()`'s `render->DrawGame()` call site, gated behind a
  new `--vr` flag. This is the point where `--probexr`'s earlier
  `xrGetSystem failed (result=-35) - no HMD form factor available`
  becomes relevant again — a real VR session needs either a physical
  headset connected through Virtual Desktop, or continued no-headset
  testing needs to stop at the parts of the frame loop that don't strictly
  require a live HMD system (session creation itself may still be
  probable without one; this needs checking against the OpenXR spec /
  VirtualDesktopXR's behavior once attempted).
- 2026-07-18 (continued): **Decided not to implement step 4 blind.**
  Traced exactly what it requires: `XR_KHR_vulkan_enable` needs
  `xrGetVulkanInstanceExtensionsKHR`/`xrGetVulkanGraphicsDeviceKHR` to run
  *before* and *inform* `VulkanRenderDevice`'s constructor
  (`VulkanRenderDevice.cpp:13-36`), which today unconditionally creates
  its own `VkInstance`/`VkDevice` via engine-config heuristics at
  `RenderSubsystem` construction — a `--vr` run needs the OpenXR
  instance/system resolved *first*, then its required Vulkan extensions
  and mandated `VkPhysicalDevice` fed into `VulkanInstanceBuilder`/
  `VulkanDeviceBuilder` instead. Full API sequence and exact touchpoints
  (`VulkanDeviceBuilder::SelectDevice(int index)` needs a
  `PhysicalDevices` handle→index lookup, etc.) written up in
  `VR_IMPLEMENTATION_PLAN.md` M2 step 4. Not implementing this blind
  because (a) it restructures the shared init path every run goes
  through, VR or not — real regression risk to the one thing that's
  rock-solid (M1) — and (b) nothing past `xrGetSystem` can be exercised or
  observed at all on this machine (`--probexr` already fails there with
  no headset connected), so there is no way to catch mistakes before the
  user is actually in the headset. **Ran a final flatscreen regression
  check** (`--autoplay "<UT99 GOTY folder>"`, no debug flags) after all of
  today's changes: process runs, correct window title, consistent with
  every earlier M1 verification — today's M2 work hasn't regressed the
  baseline. Stopping here for this session; M2 step 4 onward needs the
  user physically present with the Quest 3 connected via Virtual Desktop.

- 2026-07-18 (continued further): Fable review suggested a way to make M2
  step 4 exercisable *without* the physical headset after all: SteamVR
  ships a "null" driver (`drivers/null` in the SteamVR install) that
  presents a fake, headset-less HMD to the OpenXR runtime via
  `requireHmd: false` + `forcedDriver: "null"` in `steamvr.vrsettings`,
  registered as the active OpenXR runtime via the
  `HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime` registry key (currently
  Virtual Desktop's `virtualdesktop-openxr.json`, backed up before any
  change). This machine has SteamVR installed and a known Quest 3
  previously paired via `oculus_virtualdesktop`
  (`steamvr.vrsettings`'s `LastKnown` block). User authorized attempting
  this. **Blocked**: changing the `HKLM` key needs an elevated
  (Administrator) process, which this session doesn't have —
  `Set-ItemProperty` returned "Requested registry access is not allowed."
  Declined to pursue GUI/mouse automation to click through the resulting
  UAC prompt (it runs on the Windows secure desktop specifically to
  require a human's physical consent — not something to script around).
  Reverted the `steamvr.vrsettings` edit (`driver_null.enable`/
  `requireHmd`/`forcedDriver`) rather than leave SteamVR half-configured
  to force the null driver while the registry still points elsewhere.
  **Needs**: either this session relaunched as Administrator, or the user
  running the one-line `Set-ItemProperty` themselves from an elevated
  prompt. Once unblocked: re-apply the `steamvr.vrsettings` change, launch
  `vrserver.exe`, confirm `xrGetSystem` succeeds via `--probexr` (currently
  fails with `XR_ERROR_FORM_FACTOR_UNAVAILABLE`), then implement/test the
  real `xrCreateSession`/swapchain/frame-loop.

  **Did the safe part now**: restructured `VulkanRenderDevice`'s
  constructor to optionally accept a `VulkanXRInitOverrides` (new struct in
  `VulkanXRSession.h`: `instanceExtensions`, `deviceExtensions`,
  `physicalDevice`) so that once a real OpenXR session exists, its
  `xrGetVulkanInstanceExtensionsKHR`/`xrGetVulkanDeviceExtensionsKHR`/
  `xrGetVulkanGraphicsDeviceKHR` results can be threaded into the existing
  `VulkanInstanceBuilder`/`VulkanDeviceBuilder` calls
  (`VulkanRenderDevice.cpp:14-64`) without restructuring them — a
  non-null `physicalDevice` is matched against
  `VulkanDeviceBuilder::FindDevices()`'s results by raw `VkPhysicalDevice`
  handle and forces `SelectDevice()` to that index, since OpenXR mandates
  the exact GPU backing the HMD compositor. Parameter defaults to
  `nullptr`; `RenderDevice::Create`'s only call site is unchanged. Verified
  no regression via screenshot (pixel-identical to the pre-refactor
  baseline on the same pinned map). Committed to `vr-m2`. This means once
  the headset-less runtime (or a real headset) is available, the remaining
  M2 step 4 work is just building the session/swapchain in
  `VulkanXRSession` and populating a `VulkanXRInitOverrides` from it — the
  Vulkan side is already prepared to receive it.

  Also closed out Fable review item 7 (convergence-sign verification) via
  a written math derivation rather than new code — see
  `VR_IMPLEMENTATION_PLAN.md` M2 step 6's addendum. Confirmed
  `RenderScene.cpp`'s eye-offset/frustum-shear signs are correct
  (non-inverted), both analytically (matches the standard toe-in-free
  asymmetric-frustum stereo formula) and empirically (matches the earlier
  observation that a partially-occluded skylight was visible only in the
  right eye's `--debugstereo` image, as a real rightward eye offset should
  produce). No pseudostereo bug exists.

  User asked whether to keep grinding the remaining (session-less-testable)
  M2 prep work, or move M5's WebXR/Three.js planning doc up to fill the
  time instead of waiting for M1-M4 as originally scoped. **Chose to move
  M5 up.** Wrote `WEBXR_PORT_PLAN.md`. Headline finding: researched (via web
  search) whether Emscripten could target WebGPU as a way to reuse more of
  SurrealEngine's existing Vulkan renderer, the way the sibling
  `webxr-port/` project (QuakeQuest, same workspace, M1-M4 complete and
  live at dionysus.dk) reused DarkPlaces's GLES renderer almost unchanged
  via GLES↔WebGL2's near-1:1 API match. **No such shortcut exists for
  Vulkan**: WebGPU implementations (Dawn/wgpu) run *on top of* Vulkan
  natively, not the reverse, and emulating a lower-level API via a
  higher-level one is a hard, unsolved-in-general problem, not just an
  unwritten library. So a real SurrealGPU→WebGPU rewrite (Emscripten's
  `emdawnwebgpu` port) is a genuine third option distinct from both "full
  native Vulkan/OpenXR" and "ground-up Three.js rewrite" — it reuses all
  gameplay/VM/asset/portal-rendering C++ logic (including this session's
  hard-won stereo-rendering fixes) and rewrites only the graphics-API
  layer, at unscoped-but-bounded effort. See `WEBXR_PORT_PLAN.md` for the
  full comparison against the ground-up Three.js path — genuinely
  undecided which is better, flagged as needing its own recon pass
  (SurrealGPU/WebGPU feature-parity survey) before committing either way.
  This is a planning deliverable only; does not change the native-VR-first
  decision or commit to building anything.

  **Ran that recon pass, same day.** One Explore agent read the full
  `SurrealGPU` source tree and produced a factual inventory (size, API
  surface, every Vulkan-specific feature actually used); two web searches
  answered the doc's other open items (WebGPU bindless-texture status,
  WebXR-WebGPU bridge maturity). Findings folded into `WEBXR_PORT_PLAN.md`
  (Option A's "Effort/risk profile", the comparison table, and "What's NOT
  solved" section all revised in place). Headline result: real port surface
  is ~12,000 lines (not just `SurrealGPU`'s ~7,400 — the Vulkan consumer
  layer in `SurrealEngine/RenderDevice/Vulkan/` counts too), several
  Vulkan-requested features (ray query, compute) turned out to be
  unused/dead weight and cost nothing to drop, and the WebXR-WebGPU bridge
  concern is resolved (now an official Immersive Web Editor's Draft +
  actively-maintained `emdawnwebgpu` toolchain). But the recon also
  surfaced a genuine, previously-unknown blocker: the renderer's entire
  scene-draw path depends on an unbounded, dynamically-indexed bindless
  texture array (`DescriptorSetManager.h:68`, up to 16,536 slots,
  `nonuniformEXT` per-draw indexing) that **core WebGPU has no shipped
  equivalent for** — fixed-size `binding_array` support is still an active
  proposal, true bindless is targeted later still. This walks back the
  previous "Option A looks more promising" lean: it's better-scoped now,
  not more clearly favored, since the one piece that matters most for a
  faithful port has no committed ship date on the platform side. See
  `WEBXR_PORT_PLAN.md`'s revised "Current lean" section for the full
  reasoning. Still a planning-only deliverable — no code written, no build
  decision made, native-VR-first priority unchanged. Option B's own open
  item (UTPackage.js maturity check) remains un-recon'd.

  **User made the build decision, same day: "Go with option A, do the WebXR
  port."** This supersedes native-VR-first — both efforts are now active,
  not sequential. Entered plan mode again to scope actual implementation
  (not just more research). Two research passes (an Explore agent across
  CMake/build-system/main-loop/windowing code, a Plan agent that read the
  real source to design concrete steps) grounded a milestone roadmap: M1
  Emscripten build harness → M2 WebGPU `RenderDevice` → M3 bindless-texture
  redesign → M4 WebXR session → M5 input/comfort/deploy. Full detail now
  lives in new file `WEBXR_IMPLEMENTATION_PLAN.md`, mirroring
  `VR_IMPLEMENTATION_PLAN.md`'s role for the native effort.

  Two findings worth flagging: (1) **the Emscripten toolchain is already
  installed and verified working on this machine** (`C:\Devstuff\emsdk`,
  Emscripten 6.0.2, set up for `webxr-port/`; SDL2 confirmed compiling and
  linking) — no toolchain setup needed, `webxr-port/web/serve.mjs` is a
  directly reusable dev-server template. (2) M1 can reuse more existing
  infrastructure than expected: `RenderAPI::Bitmap` already exists
  cross-platform (including SDL2) with no GPU context, so a no-op
  `NullRenderDevice` needs zero new window/canvas plumbing; the existing
  `--autoplay` flag already bypasses the Launcher GUI entirely, sidestepping
  its Asyncify-incompatible modal event pump; and `canvas.cpp`'s only
  in-scope SSE2 hotspot already has a working scalar fallback that
  Emscripten takes automatically. Starting M1 implementation next.
- 2026-07-19: **WebXR M1 (Emscripten build harness) done.** SurrealEngine
  builds to WASM, boots headlessly via `--autoplay`, runs the real
  tick/input/map-load loop through `emscripten_set_main_loop` with a no-op
  `NullRenderDevice`. Verified via `web/smoke_test.py` (Playwright,
  headless): all 7 "definition of M1 done" criteria met, native regression
  clean. Two wasm32-specific heap-corruption bugs found and fixed along the
  way (`UObject::LoadNow()`'s missing `StructAlignment` propagation,
  `AlignedAlloc()`'s unclamped sub-pointer-size alignment crashing musl's
  `aligned_alloc`) — full detail in `WEBXR_IMPLEMENTATION_PLAN.md`'s M1
  status section. Immediately continued into M2.
- 2026-07-19 (continued): **WebXR M2 (WebGPU `RenderDevice`) done.** New
  backend under `SurrealEngine/RenderDevice/WebGPU/`, structurally mirroring
  D3D11's fixed-4-texture-slot draw model (Vulkan's bindless model has no
  stable WebGPU equivalent yet — deferred to M3). Verified against real
  UT99 game data (`DM-Deck16][`): 0 WebGPU validation errors, 94 draw calls,
  73-75 textures cached, 99.8% non-blank screenshot output showing correctly
  rendered/textured geometry, native build and `RenderDevice/Vulkan/`+
  `RenderDevice/D3D11/` unchanged. Two real bugs found and fixed, both
  invisible to code review or a clean compile: (1) a WGSL
  uniform-control-flow shader-compile error that had silently zeroed out
  100% of rendering in every earlier "passing" test this session (fixed via
  `textureSampleLevel` instead of `textureSample` for three auxiliary-
  texture samples); (2) `DrawTile`'s 2D content (HUD text, menu glyphs)
  rendering upside-down/mirrored despite byte-identical vertex math to
  D3D11 — root-caused by a background agent via an instrumented A/B rebuild
  and fixed with a targeted V/VL swap. Full detail, exact stats, and one
  flagged-but-unconfirmed follow-up (whether `DrawComplexSurface` has an
  analogous latent V-orientation issue, invisible on symmetric wall
  textures) in `WEBXR_IMPLEMENTATION_PLAN.md`'s M2 status section. Next:
  M3 (bindless-texture-model redesign).

  **Note (2026-07-19, native VR track):** this file's copy on `vr-m2`
  predates the above WebXR-track updates (audit, M3 re-scope, querySelector
  fix, M4 pre-work, `DrawComplexSurface` fix, session handoff doc) — those
  all happened on `webxr-m1` only and aren't relevant to this branch except
  as a docs-sync note. `webxr-m1`'s `Docs/VR/*.md` copies are canonical for
  the WebXR effort; this file continues to be canonical for the native VR
  effort below.

- 2026-07-19: **M2 step 4 unblocked, then a real limitation found.** With
  the user's help (one elevated `Set-ItemProperty` registry command they ran
  themselves), swapped `HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime` to
  SteamVR's `steamxr_win64.json`, re-applied the `steamvr.vrsettings`
  null-driver edit (`driver_null.enable: true`, `steamvr.requireHmd: false`,
  `steamvr.forcedDriver: "null"` — original backed up to
  `steamvr.vrsettings.bak-pre-nulldriver`), launched `vrserver.exe`, and
  confirmed `--probexr: OpenXR instance + HMD system OK`.

  Implemented the full step-4 design (instance/device reordering,
  `xrCreateSession`, swapchains, frame loop, windowed mirror — see
  `VR_IMPLEMENTATION_PLAN.md`'s updated M2 step 4 entry for exact detail).
  **The null driver has a real limitation**: `xrGetVulkanGraphicsDeviceKHR`
  returns `XR_ERROR_RUNTIME_FAILURE` (confirmed not a code bug — it has no
  real compositor device to hand back), so `xrCreateSession` never actually
  succeeds against it. Steps 8/9 (frame loop, windowed mirror) are fully
  implemented and compile clean but have **never executed against a real
  session** — this remains genuinely unverified until either a workaround
  is found or the physical Quest 3 is used instead of the null driver.
  Found and fixed a real bug along the way: the constructor used to `throw`
  on this failure and take down the whole engine; now it logs and falls
  back to ordinary flatscreen device selection.

  Verified: `--autoplay --vr` runs ~55s against real UT99 data, no crash,
  clean graceful fallback. `--autoplay` (no `--vr`) log has **zero**
  OpenXR-related lines — flatscreen behavior confirmed byte-for-byte
  unaffected. `RenderDevice/D3D11/` and `RenderDevice/WebGPU/` diffs both
  empty. 4 commits (`3f3089d1`..`30d1ae72`), pushed to `fork`.

  **Remember**: the OpenXR registry key and `steamvr.vrsettings` are still
  pointed at the null driver on this machine as of this entry — revert both
  before using the physical Quest 3 again (exact commands in
  `Docs/VR/HANDOFF_2026-07-19.md` on the `webxr-m1` branch).

  **Next**: either find a way around the null driver's
  `xrGetVulkanGraphicsDeviceKHR` limitation (unclear if possible — may be a
  hard limitation of SteamVR's null driver, not something fixable from this
  side), or resume in-headset verification with the physical Quest 3
  connected via Virtual Desktop (reverting the registry/vrsettings changes
  first). Either way, the next real piece of work is wiring true per-eye
  stereo pose into the view matrix (currently mono-duplicated, deliberately
  not guessed at — see `VR_IMPLEMENTATION_PLAN.md`).
