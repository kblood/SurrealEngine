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

- 2026-07-20: **Verified on the real Quest 3 — session/swapchain/frame-loop
  fully works end to end.** Registry `ActiveRuntime` and
  `steamvr.vrsettings` reverted off the null driver back to Virtual
  Desktop's runtime (both confirmed reverted); interface metrics on this
  machine also fixed (Wi-Fi was ranked *lower* priority than the Hyper-V
  Default Switch and VirtualBox host-only adapters for outbound routing,
  which was making Virtual Desktop Streamer advertise an unreachable
  virtual-adapter IP to the headset instead of the real Wi-Fi IP — fixed
  by setting explicit `InterfaceMetric` values, Wi-Fi lowest/highest
  priority, without disabling any adapter so WSL/Hyper-V/VirtualBox stay
  unaffected).

  With the Quest 3 connected via Virtual Desktop, ran `--autoplay --vr
  --url=DM-Deck16][` for 45s. Every OpenXR call that failed against the
  null driver succeeded against the real `"VirtualDesktopXR"` runtime:
  `xrGetVulkanGraphicsDeviceKHR result=0`, `xrCreateSession result=0`,
  swapchains created both eyes (2112x2304/eye). Session state walked
  `IDLE → READY → SYNCHRONIZED → VISIBLE → FOCUSED` and held for the full
  run. Clean `WM_CLOSE`-triggered shutdown (not a force-kill) confirmed
  the flushed log is genuine for this run. Desktop mirror screenshot shows
  the map rendering correctly, right-side up.

  User's in-headset report — double vision, no head tracking, fire button
  not working from mouse or controllers — matches expected, already-
  disclosed gaps exactly: double vision is the predicted effect of mono
  content submitted against two distinct real per-eye projection frustums
  (now empirically confirmed, not just theoretical); no head tracking and
  no controller fire are **M3 — VR input (6DoF, weapon aim, locomotion)**,
  not yet started (confirmed zero OpenXR action/controller code exists in
  this codebase). Mouse-fire not working is suspected to be an input-focus
  artifact of launching the process from a background script rather than
  a VR regression — flatscreen mouse input wasn't touched by any of this
  work. Full detail in `VR_IMPLEMENTATION_PLAN.md`'s M2 step 4 entry.

  **Next**: M3 (VR input) is now the natural next milestone — real 6DoF
  head tracking (folding `xrLocateViews`' pose into the view matrix, which
  will also fix the double-vision symptom as a side effect) and controller
  action bindings for movement/fire/aim.

- 2026-07-20 (continued): **M3 done and verified on real hardware; a
  separate rendering bug found + fixed same day (not yet re-verified in
  headset).** Session recap in `HANDOFF_2026-07-20.md`. After M3's
  controller-input bugs were fixed (`87d3b5aa`), a second-opinion review
  (Fable model, full findings in `FABLE_ANALYSIS_2026-07-20.md`) was run
  against the still-open rendering problems (stereo HUD double vision, UI
  outside the headset's FOV, world geometry warping as the head turns).
  Independently re-derived and confirmed all of it against the actual code
  (not taken on faith) before implementing:
  - **Root-caused the geometry warp**: `DrawSceneVR()`
    (`RenderScene.cpp`) fed the vertical half of the real per-eye OpenXR
    FOV into `mat4::frustum()` upside down — renderdev eye space is
    y-DOWN (`Coords::ViewToRenderDev()`) but `frustum()`'s bottom/top
    follow GL convention (bottom → Vulkan framebuffer top row), so
    `angleDown`'s extent was landing at the framebuffer top and
    `angleUp`'s at the bottom, disagreeing with the FOV metadata submitted
    to the compositor in `EndFrame()`. Invisible on every earlier
    symmetric-FOV test (`DrawSceneStereo`'s "proof" never exercised
    vertical asymmetry). One-line fix: `frustum(l, r, -u, -d, ...)`.
  - Added one-shot diagnostic logging (`VulkanXRSession::LocateViews()`,
    `VulkanRenderDevice::DrawPresentTexture()`) to validate the fix's
    premise with real data before applying it, per "do not guess-fix
    this" in the prior handoff. **Confirmed empirically on a real run
    against the user's Virtual Desktop XR runtime**: `angleUp=44.0°`,
    `angleDown=-55.0°` — an 11° asymmetry, exactly the kind of case that
    triggers the bug; letterbox was a no-op (0,0 2560x1440, matches window
    exactly); tracked IPD read back as 62.46mm, a realistic human value.
  - **Implemented the HUD/UI fix** (`RenderSubsystem::SetVRHudFrame()`,
    new, called from `RenderOverlaysVR()`/`PostRenderVR()` in
    `RenderCanvas.cpp`): replaced the old "centered half-viewport per eye"
    HUD split (which put each eye's "center" at a different real gaze
    direction, since real per-eye FOV is asymmetric — the double-vision
    cause) with a tan-space sub-rect reprojection: a fixed ~50°-wide 4:3
    virtual screen placed at each eye's real forward direction, with a
    small per-eye convergence shift (derived from the live tracked IPD,
    read from `VREyeLocation`, never a constant) so the fused HUD sits at
    a comfortable ~1.75m depth instead of infinity. Also fixes the
    "UI outside the FOV" issue as a side effect (the virtual screen is
    much narrower than the full ~90-100° per-eye FOV UI was previously
    stretched across).
  - **Two unrelated engine bugs found and fixed along the way**, both
    exonerated as unused-by-anything-that-matters or fixed cheaply:
    `UObject::SetBool()` (`UObject.cpp`) was blindly `static_cast`ing
    every resolved property to `UBoolProperty*`, which is undefined
    behavior for byte-typed "boolean-looking" properties like
    `Pawn.bFire` (root cause of the VR trigger-not-firing bug from M3,
    and a plausible suspect for any historical flaky keyboard/mouse fire
    reports) — now checks `ValueType` and writes through a raw byte
    pointer for `UByteProperty`. `quaternionT::operator*=`
    (`Math/quaternion.h`) had a comma-operator bug (overwrote `x` before
    using it to compute `y`/`z`/`w`) and the wrong Hamilton-product term
    layout besides; confirmed unused anywhere in the engine, fixed in
    terms of the already-correct non-member `operator*` rather than left
    as a trap.
  - Built clean (Release, MSVC/CMake). Ran two non-interactive flatscreen
    regression passes (`--autoplay`, with and without `--vr`, WM_CLOSE
    clean shutdown, log read back from `SE-Log-LastRun.txt`): both clean,
    no crashes; the `--vr` pass is the one that produced the diagnostic
    data above (Virtual Desktop XR runtime was available this session,
    session reached `FOCUSED` and held for the full ~35s run).
  - **Real-headset re-verification happened same session, mid-write-up**:
    user tested live while this doc update was in progress. **Confirmed
    fixed**: HUD text renders correctly and the geometry warp/distortion
    is gone - both the frustum fix and the HUD reprojection worked on the
    first real try. **Still broken**: could not fire (blocked at the
    pre-round "click fire to start" prompt, so never got a live crosshair
    to judge movement/aim against); movement direction relative to head
    turning was inconclusive (test was too short to tell, not a confirmed
    bug).
  - Investigated the fire issue same session: `UpdateVRControllerInput()`
    was gating the bFire/bAltFire/bDuck writes behind
    `TryCast<UPawn>(pawn)` and writing through native byte accessors
    (`vrPawn->bFire() = ...`) to sidestep the (now-fixed) `SetBool()` bug.
    Simplified to call `pawn->SetBool("bFire", ...)` directly - safe now,
    and matches the keyboard/mouse path's proven-everywhere mechanism
    exactly (one less way for the VR path to diverge). Added
    `VRControllerState::actionsActive` (true if `xrGetActionState*`'s
    `isActive` came back true for anything that frame) plus a throttled
    (~2s) diagnostic log of raw stick/trigger/grip values +
    `actionsActive` + session state in `UpdateVRControllerInput()`, since
    `isActive=false` (e.g. input focus not yet routed to this session)
    silently reads as "nothing pressed" with zero signal to the caller
    that anything's wrong.
  - Rebuilt, ran another `--autoplay --vr` flatscreen pass (no human
    interaction - just the real Quest 3 controllers connected via Virtual
    Desktop, apparently sitting nearby). **The new diagnostic already
    caught something concrete**: `actionsActive=0` (all inputs dead) for
    the first ~2-4s after the session reaches `FOCUSED`, then flips to
    `actionsActive=1` with genuine live analog trigger/stick values
    (`lTrig=0.31 rTrig=0.42` at the 4.4s mark, clearly real hardware
    noise from idle controllers, not synthetic). This means the action
    pipeline itself is verified working end-to-end against the real
    hardware/runtime - but there's a real ~2-4s dead window right after
    `FOCUSED` where every controller input is silently swallowed. Given
    the user described their fire-trigger test as short, this dead
    window is a plausible (not yet certain) explanation for "fire didn't
    register" - worth checking directly in the next headset session via
    the new log line instead of guessing further.
  - **Still not yet done**: real in-headset confirmation of whether the
    settle-time window explains the fire issue, and a longer test to
    actually judge movement-direction-vs-head-turning once fire works
    and a live crosshair is available.
  - Dispatched a research subagent (Fable) on Quest 3/Virtual Desktop
    OpenXR quirks per user request. Findings: (1) `isActive=false` while
    unfocused is spec-mandated, and Meta's own PC docs confirm apps
    "automatically stop receiving input" without VR focus - not a VD bug,
    a generic OpenXR behavior; (2) a known trap (cited: Godot issue
    #107612) is that `xrSyncActions` can return the non-error
    `XR_SESSION_NOT_FOCUSED` while callers only check per-action
    `isActive` and never look at the sync result itself; (3) our trigger/
    squeeze binding paths (`.../input/trigger/value`,
    `.../input/squeeze/value`) are spec-correct for
    oculus/touch_controller, confirmed against Unity's OpenXR plugin
    docs - Touch has no squeeze/click, which we already don't bind; (4) a
    common bug is querying `xrGetActionState*` with a subaction path that
    doesn't match how the action was created - doesn't apply here, our
    actions are created with zero subaction paths and queried with
    `XR_NULL_PATH` throughout, which is spec-legal. Acted on (2): added
    `VulkanXRSession::lastSyncResult`/`GetLastSyncResult()`, set from the
    real `xrSyncActions` return value, and added it to the throttled
    diagnostic log (`syncResult=` field) so a stuck `XR_SESSION_NOT_FOCUSED`
    would now be directly visible instead of only inferable from
    `actionsActive=false`.
  - Rebuilt, ran a second `--autoplay --vr` flatscreen pass (~65s, still
    no human interaction, real idle Quest 3 controllers via Virtual
    Desktop). Result this time: `syncResult=0` (XR_SUCCESS) and
    `actionsActive=1` from the very first sample at 2.2s onward - no dead
    window this run (the first pass's 2-4s dead window did not
    reproduce), with plentiful live stick noise but trigger/grip pinned
    at exactly 0.0 the entire run (consistent with an idle controller -
    nothing is physically touching the triggers). Net read: across both
    automated passes the OpenXR action pipeline itself looks healthy
    (focused session, successful sync, active + correctly-ranged reads
    from real hardware) far more often than not, which is evidence
    *against* a standing focus/binding bug and *for* re-testing live now
    that richer diagnostics exist, rather than continuing to guess-fix
    without a real trigger-pull data point. Also code-reviewed the
    keyboard input path (`Engine::InputEvent`) for comparison: keyboard
    Fire ultimately reaches the pawn via the exact same mechanism ours
    does (`Actor::SetBool` on the bind's property name) after
    `CallEvent(console, KeyEvent)` declines to swallow it - no separate
    exec/event chain that our direct `pawn->SetBool("bFire", ...)` write
    would be missing. No code bug found on a second pass; the fire issue
    remains most plausibly explained by either the (intermittent, now
    directly logged) post-FOCUSED settle window or simply needing a real
    trigger-pull data point, not a further guess-fix.
  - User's next live report (movement, not fire): "left and right are
    turned around... hard to tell whether turning my head affects
    forward." Found and fixed a real sign bug this time, not a diagnostic
    dead end. Traced `Coords::YawRotation(yaw)` (Math/coords.h): XAxis =
    (cos(yaw), -sin(yaw), 0), i.e. positive yaw rotates local forward
    toward world -Y. Cross-checked against this codebase's own
    established convention that +Y = "right" (the identity-orientation
    `rightUE` mapping in the eye-pose composition, and the standard UT99
    keybind convention that positive `aStrafe` = strafe right) - so
    positive yaw = turn LEFT here, not right. `UpdateVRControllerInput()`
    had `xrYawOffsetUE += rightStickX * turnRate * dt` for the
    comfort-turn stick - since `rightStickX` is positive when the stick
    is pushed right (standard OpenXR convention), this was adding
    positive yaw for a rightward push, i.e. turning left when the user
    turned right and vice versa. Changed to `-=`. Re-derived the
    head-yaw-sync path (`rawYaw`/`xrHeadYawUE` -> `pawn->Rotation().Yaw`)
    against the same convention and found it self-consistent - it's also
    indirectly proven correct already, since it's the exact same
    computation the render camera uses, and the user already confirmed
    the rendered view tracks their head correctly. So the working theory
    is the comfort-turn sign flip was solely (or mostly) responsible for
    both symptoms - a wrongly-signed turn control would fight the
    player's sense of "forward" and make head-relative movement hard to
    judge, without there needing to be a second bug in the head-tracking
    path itself. Rebuilt clean, ran a flatscreen smoke test (Virtual
    Desktop reported no HMD present for this particular run - harmless,
    falls back to flatscreen as designed) - 32s, no crash. **Not yet
    re-verified in headset.**

- **2026-07-20/21 session: comfort-turn confirmed, movement-direction and
  fire fully fixed and confirmed, weapon/swim pitch aim fixed and
  confirmed. Real headset (Quest 3, Virtual Desktop) used throughout.**
  - Live headset test of the comfort-turn sign fix above: **confirmed
    correct** ("Left and right are now left and right"). Same test also
    reported forward/backward movement still completely unaffected by
    either head turning or the stick, and fire still not registering from
    either trigger.
  - Root-caused the movement issue: UT99's own `PlayerPawn.PlayerMove`
    (UnrealScript, not reimplemented natively - confirmed via grep that
    `aBaseY`/`aStrafe` have zero native C++ consumers) computes its
    movement axes from `Pawn.ViewRotation`, not `Pawn.Rotation`. VR code
    only ever wrote `Rotation.Yaw`; `ViewRotation` was left wherever
    UnrealScript's own `aTurn`-driven re-derivation last put it
    (spawn-time), since VR never touches `aTurn`. Fixed by also writing
    `ViewRotation.Yaw` to the same value every frame in
    `UpdateVRControllerInput()`.
  - Root-caused fire: extensive diagnostic work (OpenXR focus/isActive/
    `xrSyncActions` result all read healthy on every real trigger pull -
    `rTrig` up to 1.0, `actionsActive=1`, `syncResult=0`) proved
    `pawn->SetBool("bFire", true)` was correctly reaching the property on
    every pull, yet gameplay never responded. Breakthrough: the user's own
    mouse click (real `IK_LeftMouse` via `GameWindow::OnMouseDown` ->
    `Engine::InputEvent()`) is what got them past the "click fire to
    start" prompt - proving the real gate is `InputEvent()`'s
    `CallEvent(console, EventName::KeyEvent, ...)` dispatch, not just the
    `bFire` property being true. Fixed by replacing the direct `SetBool`
    calls with edge-triggered `InputEvent(IK_LeftMouse/IK_RightMouse,
    IST_Press/IST_Release)` on trigger rising/falling edges - VR fire now
    routes through the exact same code path a real mouse click uses.
  - Added a throttled "VR movement diag" log line (headYawDeg/rotYawDeg/
    viewYawDeg/velHeadingDeg/velSpeed/physics/lStickY) to
    `UpdateVRControllerInput()`, in response to the user explicitly asking
    for turn-vs-movement comparison data instead of further guess-fixes.
  - Live headset re-test of both fixes: **fire fully confirmed working**
    (clean `InputEvent` rising edges throughout, no more stuck-at-the-
    start-prompt). Movement was now responsive (a real change from
    "totally unaffected") but the user reported it felt mirrored ("walk
    left when I look right"), and separately reported movement freezing
    entirely after clicking the flatscreen window to focus it. Read the
    full `SE-Log-LastRun.txt` from that session: confirmed the freeze
    coincided with `actionsActive` intermittently dropping to 0 for
    extended periods with all axes flat at zero for 3+ minutes, most
    consistent with Virtual Desktop's input routing being disturbed by
    the OS-level mouse click (no longer needed now that fire is
    `InputEvent`-based) plus the user simply not touching the sticks
    while typing feedback - not a code bug, no fix made for it.
  - Dispatched a research subagent (Fable, model `claude-fable-5`) with
    the full `SE-Log-LastRun.txt` and the relevant code to independently
    diagnose the movement mirroring, per the user's explicit request
    ("have Fable go over the logs"). Diagnosis, verified by hand
    afterward against the real code (not just trusted): `xrHeadYawUE` is
    computed and consumed in `Coords::YawRotation`'s convention (XAxis =
    `(cos yaw, -sin yaw, 0)`), but UT99's native `GetAxes(Rotator)` -
    which `PlayerMove` actually calls to turn `ViewRotation` into a
    movement direction (`Native/NObject.cpp` `NObject::GetAxes` ->
    `Coords::Rotation(rotator).GetAxes(...)`) - composes with the
    *opposite* sign. Verified algebraically: `Coords::Rotation` for a
    pure-yaw `Rotator` works out to XAxis = `(cos yaw, +sin yaw, 0)`,
    because `Coords::operator*` computes `dot(a.axis, b.axis)` term by
    term, which transposes whichever operand is `Identity` on the left
    (both `RollRotation(0)` and `YawRotation(0)` collapse to `Identity`
    for a pure-yaw rotator). Independently corroborated by
    `Rotator::FromVector` (Math/rotator.h), which already computes
    `Yaw = atan2(v.y, v.x)` with no negation - exactly the `+sin`
    convention, and the trig identity `atan2(y,x) = -atan2(-y,x)` shows
    this is precisely the negation of `rawYaw`'s own
    `atan2(-fwdUE.y, fwdUE.x)` formula. Six real full-speed movement
    samples from the log fit `velocityHeading = -rotationYaw +
    stickOffset` to <1 degree. Fixed by negating at the write point:
    `Engine.cpp`'s `int yaw = (int)(-xrHeadYawUE * ue1RadiansToYaw)`
    (was missing the `-`), plus a matching fix to the one-time
    view-recenter (`xrYawOffsetUE = -CameraRotation.YawRadians() -
    rawYaw`, was missing the same negation) so recentering still anchors
    to the pawn's true spawn facing instead of its mirror.
  - Live headset re-test: **movement direction, turning, and aim (yaw)
    all confirmed correct** ("Much much better"). Fire remained working.
  - New report: weapon doesn't aim up/down when looking up/down, and
    swimming is "impossible" (PHYS_Swimming's script-side movement, like
    `PlayerMove`'s walk axes, is `ViewRotation`-driven, so a missing pitch
    write would break both the same way). Root cause: `Rotation`/
    `ViewRotation` code only ever wrote `Yaw` - by design, to avoid
    tilting the pawn's upright collision cylinder via `Rotation.Pitch` -
    but `ViewRotation.Pitch` (which does *not* affect collision) was never
    written either, so it just sat at 0 (or spawn value) regardless of
    head pitch. Added `xrHeadPitchUE` (Engine.h/Engine.cpp), computed each
    frame from the same eye-pose forward vector already used for yaw:
    `atan2(fwdUE.z, sqrt(fwdUE.x^2+fwdUE.y^2))`. This formula is already
    in `Rotator`/game convention (matches `Rotator::FromVector`'s own
    `Pitch` formula exactly, verified by inspection) so - unlike yaw - it
    needs no sign flip and no recenter offset. Written into
    `vrPawn->ViewRotation().Pitch` only, never `Rotation.Pitch`.
  - Live headset re-test: **pitch aim confirmed working** ("I can aim up
    and down"). Swimming not yet separately re-tested but shares the
    exact same `ViewRotation.Pitch` dependency, so is expected fixed too -
    worth a direct confirmation next time a water level is available.
  - **User's follow-up feature request (not yet started, scoped as future
    work, not a bug)**: aim the weapon(s) from the motion controllers
    themselves rather than from head look - e.g. gun in one or both
    hands, aiming each independently of where the player is looking, up
    to dual-wielding with independent per-hand aim. This is a real,
    fairly large feature: UT99 has no native concept of aim decoupled
    from `ViewRotation` (the first-person viewmodel and hit-trace/firing
    direction are both driven off it), so it would need new per-frame
    logic to position the viewmodel at the controller pose and redirect
    the fire trace, not just another `ViewRotation` write. Needs its own
    design pass before starting.

- 2026-07-21: **Controller-aimed weapons feature fully implemented**
  (M-A through M-F, plus M-E1), fully autonomously per explicit user
  direction ("extend the plan to fully implement the proposed Fable plan
  without consulting me"). Full design/research pass done by a Fable
  research agent first, written to `Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md`
  (companion to this file and `VR_IMPLEMENTATION_PLAN.md`'s M3 section 3,
  which now points here). Headline finding from that design pass: the
  original idea of writing a controller-vs-head delta into the
  `AdjustedAim` property each frame **does not work** - `AdjustedAim` is
  an output clobbered by the script's own `AdjustAim()` call at fire time,
  not a persistent input. The real mechanism used instead: every
  UnrealScript call (native, virtual, or final) already funnels through
  one C++ choke point, `Frame::Call` (`VM/Frame.cpp`) - a new
  engine-installed interception hook there lets native code swap
  `Pawn.ViewRotation` to a controller-derived rotator for exactly the
  duration of a `TraceFire`/`ProjectileFire` call (restoring it
  immediately after), and separately override the viewmodel's transform
  (`InvCalcView`/`RenderOverlays`) and fire origin (`CalcDrawOffset`) the
  same way - all without ever touching a stock `.u` package, and without
  `PlayerMove`/camera/swimming ever seeing the swapped rotation. No
  UnrealScript source was written or modified; no decompiled Epic/Botpack
  code was copied anywhere.

  Implemented as six milestones, one sub-agent per milestone, each
  building Release and non-interactively verifying via new debug flags
  (`--debugvrhands`, `--debugvrfire`, `--debugvrtwohand`,
  `--debugvrdualenforcer` - all fake controller poses/inputs requiring no
  real headset) plus `PrintWindow`+`PW_RENDERFULLCONTENT` screenshots and
  `SE-Log-LastRun.txt` log evidence:
  - **M-A** (`b64a995f`) - four new `XR_ACTION_TYPE_POSE_INPUT` OpenXR
    actions (per-hand grip pose + aim pose), composed into world space by
    factoring the existing head-pose math into shared helpers with zero
    behavior change to the head path.
  - **M-B** (`458899c7`) - the `Frame::Call` interception seam itself
    (`Frame::InterceptCall`, null by default); intercepts `InvCalcView`
    (pre-219) or `RenderOverlays` (v436 and later - a real deviation from
    the design doc's assumption, found and fixed) to natively reposition
    the weapon actor at the main hand's grip pose. A per-weapon grip-offset
    table was added, starting with Enforcer.
  - **M-C** (`5c12bf88`) - the actual fire-direction redirect: a second
    seam, `Frame::InterceptCallPost` (a real, necessary extension of M-B's
    seam - the pre-call-only version can't express "let the original
    script run, then restore state after," which fire needs since damage/
    ammo/effects must still execute for real). Scoped `ViewRotation`
    save/swap/restore around `TraceFire`/`ProjectileFire`, verified
    re-entrancy-safe against a real case (Enforcer's `TraceFire` calls
    `Super.TraceFire` internally, double-firing the intercept per shot).
    Also intercepts `CalcDrawOffset` to move the fire origin to the hand,
    and rebinds crouch off the right-grip button to free it for M-D. A
    per-weapon fire-path audit (Sniper/Ripper/Bio lob confirmed fine;
    Translocator flagged as needing a wider intercept because
    `ThrowTarget`/`AdjustToss` permanently mutates `ViewRotation`, a stock
    camera-snap quirk that fights a naive restore; Redeemer flagged as
    needing a full clean-room spec pass, its classes aren't even present
    in the decompiled reference checked) is appended to
    `CONTROLLER_AIM_WEAPON_PLAN.md`.
  - **M-D** (`401a42bf`) - two-handed aim: grabbing a weapon's foregrip
    point with the off-hand (dual-hysteresis grab/release thresholds on
    the existing grip-analog input) switches the aim rotator from the
    single hand's aim pose to the normalized vector between both hands'
    grip positions, with a ~100ms blend on transition and a fade-back to
    single-hand aim when the hands get too close (the position-vector
    method's angular noise scales inversely with hand separation).
  - **M-F** (`f1ad7643`) - left/right-handed mode: one `mainHand` index
    flip (`--vr-lefthand` flag or ini) retargets every earlier milestone,
    since they all already went through `MainHand()`/`OffHand()` accessors
    by design - except two spots a grep audit caught and fixed (the fire-
    trigger mapping and the debug-hand synthesizer had snuck in hardcoded
    left/right instead of using the accessors). Weapon mesh mirroring
    (`scale(1,-1,1)` on the local player's weapon draw) was implemented as
    a real fix, not the design doc's documented fallback, after confirming
    this renderer's Vulkan pipelines use `VK_CULL_MODE_NONE` everywhere
    (no backface culling to fight with the flipped winding).
  - **M-E1** (`673b89be`) - dual-wielded Enforcers with independent
    per-hand aim. UT99's stock double-Enforcer mechanism (a master/slave
    actor pair, shared ammo, master-then-~0.2s-echo fire timing, slave
    selection-blocked from ever being `Pawn.Weapon`) was **properly
    clean-room specced first**: a dedicated research agent wrote
    `Docs/VR/ENFORCER_DUALWIELD_SPEC.md`, a plain-language-only behavioral
    spec (public docs first, one named decompiled mirror consulted only to
    settle what public docs didn't cover, zero code reproduced), then a
    *separate* implementation agent - barred from touching any decompiled
    source - built M-E1 from that spec alone. It found the real
    `SlaveEnforcer`/`bIsSlave` UnrealScript property names **empirically**,
    via this engine's own generic property-reflection system run against
    the user's legitimate game install, rather than reading them from
    source. Master's aim/pose continues following the main hand exactly as
    before; the slave's aim/pose now independently follows the off-hand.
    Firing still uses stock's master/slave echo timing rather than true
    independent per-trigger fire (documented as the accepted fallback -
    doing better would have meant guessing at an unnamed internal script
    call).

  **Process note**: an earlier per-weapon audit pass (inside the M-C work)
  read a decompiled source mirror directly in the same agent run that also
  wrote engine code - a lapse against this project's own two-agent
  clean-room policy, even though no code was actually copied or
  implemented from what it read. Caught and corrected for M-E1 (see
  above); worth remembering as a process point for any future weapon-
  specific work (Translocator/Redeemer follow-ups, generic dual-wield of
  arbitrary pairs) - always split research from implementation across two
  separate agents when decompiled source needs consulting.

  **Explicitly out of scope / deferred, per the design doc's own
  scoping**: M-E2 (generic dual-wield of arbitrary one-handed weapon
  pairs beyond the stock double-Enforcer) - a future item, not committed
  work. Translocator/Redeemer's fire paths need further work before they
  aim correctly in VR (see M-C's audit above). Every milestone's real
  in-headset feel/tuning (grip offsets, grab/release radii, mirrored-
  model legibility, overall aim comfort) is completely unverified -
  everything above is build/log/screenshot-verified only. Commits
  `b64a995f`..`673b89be` pushed to `fork vr-m2` 2026-07-21. Full
  session handoff: `Docs/VR/HANDOFF_2026-07-21_CONTROLLER_AIM.md`.
