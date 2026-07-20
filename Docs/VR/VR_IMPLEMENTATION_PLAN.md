# SurrealEngine Native VR — Detailed Implementation Plan

Companion to `PLAN.md` (keep that for the high-level decision history / status
log; this file is the granular, execute-without-supervision technical plan
for M2-M4). Written 2026-07-18 per explicit instruction: work autonomously,
modify the engine source directly wherever that removes a dependency on
manual/GUI interaction, and don't wait for check-ins that aren't actually
required.

Architecture facts below came from a dedicated source-tree recon pass
(file:line citations throughout) — not guesses.

## Ground truth about the engine (recon results)

- **Loop**: `Engine::Run()` (`SurrealEngine/Engine.cpp:86-274`), single-threaded,
  variable-timestep, order: input → UnrealScript tick → `PlayerCalcView` →
  audio → `render->DrawGame()` (line 199). No render thread, no
  double-buffered command queue — trivial to extend to "render twice" on the
  same thread.
- **RenderDevice** abstract interface (`SurrealEngine/RenderDevice/RenderDevice.h:97-161`),
  implemented by `D3D11RenderDevice` and `VulkanRenderDevice`. Per-frame
  contract: `Lock()` → `SetSceneNode(FSceneNode*)` (one or more times, for
  portals/mirrors/skyboxes) → draw calls → `Unlock(bool Blit)`.
- **Vulkan is the default and fully-featured** (`LauncherSettings.h:38`),
  cross-platform, and has first-class OpenXR-Vulkan graphics-binding support
  in the OpenXR spec (`XrGraphicsBindingVulkanKHR`, `VkImage` swapchain
  images directly). **Build the VR path on Vulkan, not D3D11.** D3D11 is a
  possible lower-priority Windows-only follow-up later, not now.
- **Camera → view matrix**: `RenderSubsystem::DrawScene()`
  (`SurrealEngine/Render/RenderScene.cpp:10-29`) builds `worldToView` from
  `engine->CameraLocation`/`CameraRotation` (set by UnrealScript's
  `PlayerCalcView`, `Engine.cpp:180-194`) and calls `MainFrame.Process(...)`.
  **This is the head-tracked-pose injection point.**
- **Projection matrix gotcha**: both `D3D11RenderDevice::SetSceneNode`
  (`D3D11RenderDevice.cpp:2445-2474`) and
  `VulkanRenderDevice::SetSceneNode` (`VulkanRenderDevice.cpp:966-994`)
  **ignore** the passed-in `Frame->Projection` and recompute a **symmetric**
  frustum from `Frame->FovAngle` alone. Stereo VR needs **asymmetric**
  (off-axis) per-eye frustums. Both call sites need a new path that accepts
  an explicit projection matrix.
- **Scene buffers auto-resize to the OS window's client size every `Lock()`**
  (`D3D11RenderDevice.cpp:1478-1500` and Vulkan equivalent) — for VR the
  target size is the headset's per-eye recommended resolution from OpenXR,
  not the desktop window size. This auto-resize needs to be bypassed when a
  VR session owns the render targets.
- **Strongest existing precedent for "render the scene twice with different
  view matrices/viewport rects"**: the recursive portal/mirror/skybox frame
  system in `VisibleFrame::Process()` (`SurrealEngine/Render/VisibleFrame.cpp:367-411`),
  which already calls `Device->SetSceneNode(&subframe.Frame)` multiple times
  per top-level draw with independent `worldToView` + viewport rects. Model
  the L/R eye pass on this, not on writing something from scratch.
- **Input**: event-driven (`Engine::InputEvent(EInputKey, EInputType, int)`,
  `Engine.cpp:1661-1704`), feeding `activeInputButtons`/`activeInputAxes`
  maps that `UpdateInput()` (`Engine.cpp:1418-1443`) applies to the pawn's
  UnrealScript input axes every frame. `EInputKey` already reserves
  `IK_Joy1..16`, `IK_JoyX/Y/Z/R/U/V`, POV hat slots
  (`SurrealEngine/GameWindow.h:10-76`) — unused today (no gamepad backend
  exists), but exactly the right shape to synthesize VR controller
  button/axis events into via the same `Engine::InputEvent` call the
  keyboard/mouse path already uses. No 6DoF pose plumbing exists at all —
  new state, threaded in alongside `UpdateInput()`.
- **`--autoplay <folder>`**: already added this session
  (`SurrealEngine/GameApp.cpp`) — skips the launcher GUI, launches the first
  resolved game directly. This is now the standard way every step below gets
  verified without touching the mouse/keyboard.

## Non-interactive verification philosophy

Every step below has to be checkable one of these ways, since the user will
not be present to click through a GUI or wear the headset for most of this
work:

1. **Build succeeds** (`cmake --build . --config Release`, exit code +
   grep for "error").
2. **Process runs without crashing** for N seconds against a real game via
   `--autoplay`, confirmed via `Get-Process ... Responding`.
3. **Log-based assertions** — add clear, greppable log lines (following the
   existing `Logger` usage in the codebase) for state transitions (e.g. an
   OpenXR session hitting `XR_SESSION_STATE_READY`/`SYNCHRONIZED`/
   `VISIBLE`/`FOCUSED`) and grep the log file after a timed run.
4. **Screenshot via `PrintWindow` + `PW_RENDERFULLCONTENT`** (established
   this session — required because the engine runs exclusive-fullscreen
   D3D11/Vulkan, invisible to normal `CopyFromScreen`/GDI capture) — for
   anything with a visual flatscreen component.
5. **A flatscreen "debug stereo" mode** (see M2 step 2.9 below) — the key
   trick that lets stereo rendering itself be visually sanity-checked
   *without a headset at all*, by rendering both eyes side-by-side into the
   normal window. Build and verify this before ever touching real OpenXR
   session/swapchain code — it de-risks the projection-matrix and
   dual-render-pass work in isolation.

Only the **very last** step of M4 (real in-headset feel/comfort/tracking
check) genuinely requires the user physically present with the headset on.
Everything else in M2-M3 should be buildable and verifiable solo.

## M2 — OpenXR session + stereo rendering (Vulkan)

1. **Vendor the OpenXR loader.** Add `OpenXR-SDK`'s `openxr_loader` (same
   dependency `cp2077-vr` already uses, MIT-licensed, header-only API +
   prebuilt/buildable loader lib) to `Thirdparty/` or fetch via CMake
   `FetchContent`, wired into `CMakeLists.txt` alongside the existing
   Vulkan/SurrealGPU targets. Verify: clean configure + build with no new
   dependency errors.

2. **New files**: `SurrealEngine/RenderDevice/Vulkan/VulkanXRSession.h/.cpp`
   (session/swapchain lifecycle) — keep this VR-specific code physically
   separate from `VulkanRenderDevice.cpp` so flatscreen play is untouched
   when `--vr` isn't passed. Add to `CMakeLists.txt`'s
   `SURREALCOMMON_SOURCES` (cross-platform, matching how the rest of the
   Vulkan backend is already built per-platform-agnostically, unlike D3D11).

3. **`--vr` command-line flag** (`GameApp.cpp`, same `commandline->HasArg`
   pattern as `--autoplay`). Gate all of the below behind it. Flatscreen-only
   behavior (today's, already verified against real UT99) must stay
   byte-for-byte unaffected when the flag is absent — this is the single
   most important regression guard for the whole project, since the engine
   is alpha-quality already per `Docs/Status.md` and VR work must not make
   flatscreen play worse.

4. **xrCreateInstance / xrGetSystem / xrCreateSession** with
   `XrGraphicsBindingVulkanKHR`. **[DESIGNED, NOT YET IMPLEMENTED — see the
   architectural note below, found 2026-07-18.]** This needs the live
   `VkInstance`, `VkPhysicalDevice`, `VkDevice`, and graphics queue
   family/index out of `VulkanRenderDevice`/SurrealGPU's `vulkandevice`.
   Log every step's `XrResult` and session-state transition.

   **Architectural finding — instance/device creation order coupling.**
   `VulkanRenderDevice`'s constructor (`VulkanRenderDevice.cpp:13-36`)
   already creates SurrealEngine's own `VkInstance` (via
   `VulkanInstanceBuilder`) and picks/creates a `VkDevice` (via
   `VulkanDeviceBuilder().SelectDevice(VkDeviceIndex)`, an engine-config
   heuristic index) **unconditionally, at `RenderSubsystem` construction
   time in `Engine::Run()`** — this happens whether or not `--vr` is
   passed, and happens *before* any OpenXR code runs today.
   `XR_KHR_vulkan_enable` (the simpler of the two Vulkan graphics-binding
   extensions the probe confirmed are both available — see `--probexr`
   result) requires the opposite order: `xrGetVulkanInstanceExtensionsKHR`
   must inform which extensions the `VkInstance` is created with, and
   `xrGetVulkanGraphicsDeviceKHR(xrInstance, systemId, vkInstance, &out)`
   returns the **exact** `VkPhysicalDevice` the runtime requires — the
   spec doesn't allow substituting a different GPU, so the engine's own
   physical-device-selection heuristic can't be used for a VR run.
   Concretely, a `--vr` run needs to reorder engine startup to:
   (a) create the `XrInstance` + resolve `XrSystemId` first (i.e. hoist
   today's `VulkanXRSession` instance/system creation to *before*
   `RenderSubsystem`/`VulkanRenderDevice` construction, not after, when
   `--vr` is set);
   (b) call `xrGetVulkanGraphicsRequirementsKHR` (spec requires this be
   called before `vkCreateInstance`);
   (c) call `xrGetVulkanInstanceExtensionsKHR`, pass the (space-separated,
   needs splitting) extension list into
   `VulkanInstanceBuilder().RequireExtensions(...)` alongside the existing
   `Viewport->GetVulkanInstanceExtensions()` call
   (`VulkanRenderDevice.cpp:19-23`);
   (d) after `VkInstance` creation, call `xrGetVulkanGraphicsDeviceKHR` to
   get the mandated `VkPhysicalDevice`, then find its index in
   `instance->PhysicalDevices` (a `std::vector<VulkanPhysicalDevice>`,
   compare `.Device` handles) and pass that index to
   `VulkanDeviceBuilder().SelectDevice(index)` instead of the configured
   `VkDeviceIndex` — `SelectDevice` only takes an `int` index
   (`vulkanbuilders.h:42`), not a raw handle, so this lookup step is
   required;
   (e) call `xrGetVulkanDeviceExtensionsKHR` similarly for
   `VulkanDeviceBuilder().RequireExtensions(...)`.
   This is a real, non-trivial restructuring of a code path every run
   (VR or not) goes through — high regression risk to touch blind. It was
   **deliberately not implemented this session**: `--probexr` already
   confirmed `xrGetSystem` fails with `XR_ERROR_FORM_FACTOR_UNAVAILABLE`
   (result=-35, "no HMD form factor available") on this machine without a
   headset connected through Virtual Desktop, which means nothing past
   this point (`xrGetVulkanGraphicsDeviceKHR` included) can be exercised
   or verified at all right now — writing 100+ lines of Vulkan/OpenXR
   interop code with literally zero ability to run or observe it would
   risk shipping subtle spec-compliance bugs that waste real headset time
   debugging instead of verifying. **Implement this step once the user is
   at the machine with the Quest 3 connected via Virtual Desktop**, so
   each API call can be confirmed against real `XrResult` values as it's
   written, same iterative loop the rest of M2 used successfully.

   **[IMPLEMENTED, 2026-07-19 — real limitation found, not fully verified.]**
   Unblocked via SteamVR's null driver (fake headset-less HMD; see `PLAN.md`
   2026-07-19 entries for exact registry/`steamvr.vrsettings` setup) instead
   of the physical headset. The reordering itself works: with `--vr` passed,
   `VulkanXRSession` now resolves `XrInstance`/`XrSystemId` before
   `RenderSubsystem`/`VulkanRenderDevice` construction; `xrGetVulkan
   GraphicsRequirementsKHR` and `xrGetVulkanInstanceExtensionsKHR` both
   succeed (6 extensions returned) and are folded into
   `VulkanInstanceBuilder` before `vkCreateInstance`. **But
   `xrGetVulkanGraphicsDeviceKHR` returns `XR_ERROR_RUNTIME_FAILURE` (-2)
   against the null driver** — confirmed not a calling-convention bug
   (parameters match spec exactly); the null driver has no real compositor
   device to report, so this is a genuine limitation of the workaround, not
   a code bug. `xrCreateSession` (called with a substitute device)
   consequently returns `XR_ERROR_VALIDATION_FAILURE` (-1). Both failures
   are logged and handled gracefully now — fixed a real bug where the
   constructor originally `throw`s on this failure and takes down the whole
   engine; it now falls back to ordinary flatscreen device selection.
   **Net effect: steps 8 (frame loop) and 9 (windowed mirror) are fully
   implemented and spec-conformant, compile clean, but have never actually
   executed against a real session** — they remain unverified at runtime
   until either the null driver's limitation is worked around (unclear if
   possible) or the physical Quest 3 is used instead. Verified: `--autoplay
   --vr` runs ~55s with no crash and a clean graceful fallback to
   flatscreen; `--autoplay` (no `--vr`) produces a log with **zero**
   OpenXR-related lines, confirming flatscreen behavior is byte-for-byte
   unaffected when the flag is absent. `RenderDevice/D3D11/` and
   `RenderDevice/WebGPU/` diffs both empty. 4 commits on `vr-m2`
   (`3f3089d1`..`30d1ae72`), pushed.

   **[VERIFIED ON REAL HARDWARE, 2026-07-20.]** With the user's Quest 3
   connected via Virtual Desktop (registry ActiveRuntime and
   steamvr.vrsettings reverted back off the null driver first - see
   PLAN.md), ran `--autoplay --vr --url=DM-Deck16][` for 45s. Every call
   that failed against the null driver succeeded against the real runtime
   ("VirtualDesktopXR"): xrGetVulkanGraphicsDeviceKHR result=0,
   xrCreateSession result=0, xrCreateReferenceSpace(LOCAL) result=0,
   swapchains created both eyes at 2112x2304/eye. Session state walked
   IDLE -> READY -> SYNCHRONIZED -> VISIBLE -> FOCUSED and stayed there
   for the full run - confirmed via the flushed SE-Log-LastRun.txt (clean
   WM_CLOSE-triggered shutdown, not a force-kill, so the log is genuine
   for this run). Desktop-mirror PrintWindow screenshot shows the map
   (DM-Deck16]['s pre-match lobby) rendering correctly, right-side up, no
   corruption. Steps 8/9 are now fully runtime-verified, not just
   compile-verified - this closes out the previous entry's open question.

   User's in-headset report: game visible and audio audible, but (a)
   noticeable double vision / misalignment between eyes, (b) no head
   tracking (view doesn't respond to head movement), (c) fire button did
   nothing from either mouse or Quest controllers. All three are expected
   given documented, already-disclosed scope gaps, not new bugs: (a) is
   the predicted visible symptom of feeding identical mono content through
   two distinct real per-eye projection frustums (exactly the "not yet
   folded into the view matrix" gap above, now empirically confirmed
   rather than theoretical); (b) and (c) are M3 - VR input (6DoF, weapon
   aim, locomotion) below, not started, confirmed via grep that zero
   xrCreateAction/XrAction/controller-input code exists anywhere in this
   codebase yet. The mouse-fire symptom specifically is most likely the
   game window never receiving OS input focus (launched from a background
   script, no explicit focus grab) rather than a VR regression -
   flatscreen mouse input is unrelated to any of today's changes; worth a
   quick re-check with manual focus before treating it as a real bug, but
   not blocking.

5. ~~**Debug stereo flatscreen mode first**~~ **[DONE, 2026-07-18.]** Added
   `--debugstereo`: renders the scene twice per frame (left half / right
   half of the window, two `SetSceneNode` + draw passes with two different
   `worldToView` matrices offset by a fake fixed IPD, modeled on the
   portal/mirror recursive-frame pattern from `VisibleFrame.cpp`) and
   presents through the normal window swapchain. Verified via `--autoplay
   --debugstereo` + `PrintWindow` screenshot: two visibly different
   (parallax-shifted) views of the same UT99 map side by side, no crash,
   no visual corruption. See `PLAN.md` status log for the full
   implementation writeup (`ViewportOverride`, `DrawSceneStereo()`).

6. ~~**Extend `FSceneNode`/`SetSceneNode`**~~ **[DONE, 2026-07-18.]** Added
   `FSceneNode::ProjectionOverride` (`RenderDevice.h`) — when true,
   `SetSceneNode` (`D3D11RenderDevice.cpp:2445`/`VulkanRenderDevice.cpp:966`)
   uses `Frame->Projection` verbatim instead of deriving a symmetric
   frustum from `FovAngle`; default `false` reproduces old behavior
   exactly. `ViewportOverride` (`VisibleFrame.h`) gained an optional
   `const mat4* Projection`. `--debugstereo` upgraded to use this for a
   real off-axis (asymmetric) per-eye frustum shear (parallel-axis
   cameras, not toe-in) — the same shape of asymmetry a real per-eye
   OpenXR projection has. Verified via screenshot: clean parallax, no
   corruption. See `PLAN.md` status log for the full writeup.

   **Convergence-sign verification (2026-07-18, closes Fable review item
   7):** checked `RenderScene.cpp:68-96`'s eye-offset/frustum-shear signs
   aren't accidentally inverted (which would produce uncomfortable
   pseudostereo — plausible-looking parallax in a flat screenshot but wrong
   depth in a headset). UE1's `YAxis` is the camera's right vector (derived
   from `Coords::Rotation`'s identity-at-zero-yaw axes: `Z×X = Y`, i.e.
   "right = up × forward" resolves to `+YAxis`). `eye==0` (rendered into
   the *left* screen half, `XB=fullX`) uses `sign=-1`, so its eye position
   is offset `-halfIPD` along `YAxis` — physically to the left, correctly
   paired with the left screen half. Its frustum shear
   (`frustumShift = -sign*halfIPD/convergence` = **positive**) shifts the
   frustum right, which is the correct direction for a left-shifted eye to
   converge on a point in front of it — matches the standard toe-in-free
   asymmetric-frustum stereo formula (Kooima et al.: shift sign is opposite
   the eye-offset sign). `eye==1` mirrors this. Empirically consistent with
   the `--debugstereo` screenshot evidence too: a skylight occluded by a
   pillar from center view was visible only in the *right* eye's image,
   which is exactly what a real rightward-shifted eye should reveal. No
   inversion bug exists; no code change needed.

7. ~~**Bypass the window-size auto-resize of scene buffers**~~ **[DONE,
   2026-07-18, Vulkan only — D3D11 untouched since it's not the VR
   backend.]** Added `RenderDevice::SetFixedRenderSize(w,h)` /
   `GetRenderWidth()` / `GetRenderHeight()` (`RenderDevice.h`/`.cpp`,
   out-of-line to avoid needing `Widget`'s full definition in the header).
   `VulkanRenderDevice::Lock()`'s scene-texture recreation and
   `ReadPixels()` now use these instead of `Viewport->GetNativePixelWidth/
   Height()` directly; `Unlock()`/`DrawPresentTexture()` (the actual
   window swapchain present) intentionally still use the real window size
   — that's the desktop mirror (M2 step 9), a separate concern from the
   scene render target size. Verified via a `--debugfixedsize=WxH`
   diagnostic flag (parsed in `Engine::Run()`) + `--autoplay
   --debugfixedsize=800x600`: no crash, `PrintWindow` screenshot shows the
   window filled with visibly blurrier/upscaled content vs. native-res
   screenshots, confirming the scene buffer really was created smaller
   than the window. **Correction (2026-07-18, Fable review):** that
   screenshot only proved the buffer's pixel dimensions were really
   smaller (blur-on-upscale) — it did not prove the buffer's *content* was
   framed correctly. `VisibleFrame::SetupSceneFrame` was still deriving
   `Frame.X`/`Frame.Y` (the GPU viewport rect) from the window/viewport
   size rather than `Device->GetRenderWidth/Height()`, so the viewport
   exceeded the real framebuffer and Vulkan cropped to its top-left corner
   instead of rendering the full (correctly-FOV'd) scene into it. Fixed by
   sourcing `Frame.X/Y/FX/FY` from `Device->GetRenderWidth/Height()`
   directly; re-verification pending. See `PLAN.md` status log for the
   full writeup. Once real OpenXR swapchain images exist (step 8),
   `SetFixedRenderSize` should be called with the OpenXR-recommended
   per-eye extent from `xrEnumerateViewConfigurationViews` instead of a
   debug constant, and `--debugfixedsize` can be removed.

8. **Real OpenXR frame loop**: `xrWaitFrame`/`xrBeginFrame` wrapped around
   the existing `render->DrawGame()` call site in `Engine::Run()`
   (`Engine.cpp:199`); `xrLocateViews` for per-eye pose+fov each frame;
   compose head pose (times per-eye offset) with the existing
   `engine->CameraLocation/CameraRotation` in `RenderScene.cpp:25-26`
   (matching UE1's coordinate basis via the existing `Coords::ViewToRenderDev()`
   helper — check its handedness/up-axis convention against OpenXR's before
   assuming a naive transform works); create OpenXR swapchains, get
   `VkImage`s via `xrEnumerateSwapchainImages`, feed them into the same
   scene-buffer/framebuffer machinery already used for the offscreen
   color-buffer-then-blit pattern (`VulkanRenderDevice`'s `SceneTextures`);
   `xrEndFrame` with a projection composition layer.

9. **Windowed mirror**: while a VR session is active, keep presenting
   *something* to the desktop window (e.g. the left eye's buffer) purely so
   `PrintWindow` screenshots stay useful for verification without a
   headset — the actual submitted-to-headset content is judged by whether
   the OpenXR session reaches `XR_SESSION_STATE_FOCUSED` and stays there
   without erroring, plus what the mirror shows.

10. **Verify**: build clean; run `--autoplay --vr` against real UT99 for a
    fixed duration (e.g. 15s) with Virtual Desktop's streamer running (per
    `restart-vd-streamer.ps1` if needed) so a real OpenXR runtime is present
    to negotiate a session against; grep the log for session-state
    transitions reaching `FOCUSED`, confirm no crash, confirm the mirror
    window screenshot looks like a plausible single-eye UT99 view. Full
    stereo-in-headset comfort/correctness judgment is still an M4 item —
    this step only proves the pipeline runs end-to-end without erroring.

## M3 — VR input (6DoF, weapon aim, locomotion)

1. **Poll OpenXR actions** each frame (grip pose, aim pose, trigger, grip
   button, thumbstick, menu button) alongside `UpdateInput()`
   (`Engine.cpp:158`) — new `Engine::VRUpdateInput()` step, only active
   under `--vr`.
2. **Feed buttons/axes into `Engine::InputEvent(EInputKey, EInputType, int)`**
   using the already-reserved-but-unused `IK_Joy*` slots
   (`GameWindow.h:10-76`) — trigger→Fire, grip→AltFire, thumbstick→movement
   axes. This reuses the exact same seam the keyboard/mouse path already
   goes through; no new UnrealScript-side plumbing needed for basic
   fire/move/turn.
3. **6DoF weapon aim decoupled from view** — the hardest, most
   UT99-specific part. Default UnrealScript weapon-fire traces use
   `PlayerPawn`'s view rotation. Needs either (a) a new native C++ function
   exposed to UnrealScript that substitutes a controller-aim rotation for
   the trace, or (b) overriding the relevant `Weapon`/`PlayerPawn`
   UnrealScript state via the project's own subclassed package rather than
   touching stock game `.u` packages (keeps it clean-room and
   non-invasive — a "mod" `.u` in the vein of how UT99 itself supports
   gameplay mutators). Research via public UnrealScript modding docs
   (Unreal Wiki / BeyondUnreal) per `PLAN.md`'s clean-room policy — this is
   exactly the kind of gap that policy exists for.
4. **Locomotion**: joystick smooth-move mapped onto pawn input axes (same
   `activeInputAxes` mechanism as step 2); optional snap-turn as discrete
   `CameraRotation`/pawn-yaw increments. `cp2077-vr`'s OpenXR action-based
   input code (`downloads/source/cyberpunk-vr-port/src/dxgi/openxr/`) is a
   same-author, same-workspace precedent for the action-set/action-space
   boilerplate, even though the target engine is completely different —
   fine to reference for OpenXR API usage patterns, not gameplay code.
5. **VR menu interaction**: UT99's UnrealScript-driven UI is a flatscreen
   mouse-and-keyboard surface; full VR-native menu interaction (laser
   pointer etc.) is out of scope for M3, deferred to M4 or later — initial
   fallback is Virtual Desktop's controller-to-virtual-mouse passthrough
   (same category of punt the CP2077 mod took on menu nav initially).

## M4 — Playability pass

1. Comfort options: vignette-on-turn/move, snap-vs-smooth turn toggle,
   configurable move speed — same design space as any VR locomotion comfort
   layer.
2. Fix whatever UnrealScript VM gaps or engine bugs M2/M3 exposed (expected
   per `Docs/Status.md`'s alpha-quality caveats — missing native functions,
   occasional crashes on interaction).
3. **Real in-headset verification via Virtual Desktop, Quest 3.** This is
   the one step in the entire plan that requires the user physically present
   and wearing the headset — stereo depth/comfort/tracking-latency feel
   cannot be judged from a flatscreen mirror or a log file. Everything in
   M2/M3 above is designed to be as fully proven as possible *before*
   reaching this point, so this step is a confirmation pass, not a debugging
   session from zero.
4. Iterate on real feedback from step 3.

## M5 — (after, and only after, M1-M4 native VR is working) Write the
## WebXR/Three.js port plan

Per explicit instruction: once native VR on SurrealEngine is genuinely
working end-to-end, the final step of *this* plan is to write a **new**
planning document — not implementation — for porting the (by-then
VR-proven) engine architecture to JavaScript + Three.js for a browser-based
WebXR version. That plan should draw on:
- The UTPackage.js (CC0) format-parsing research already done during this
  project's scoping phase (see `PLAN.md`'s decision log) as the asset
  pipeline foundation.
- Whatever's learned from the native OpenXR stereo-rendering work above
  (M2) about UE1's coordinate conventions, camera/projection math, and
  portal/mirror rendering — this transfers directly to a Three.js
  reimplementation even though the code itself won't.
- An honest scope assessment of gameplay/UnrealScript-VM-equivalent logic
  that would need to be authored from scratch in JS (this was flagged as
  the dominant risk of the JS path back when it was first considered and
  deferred — that assessment should be revisited with real numbers once
  there's a working native reference implementation to measure against,
  rather than the earlier purely-speculative comparison).
- Not committing to Emscripten-porting the actual C++ engine (ruled out
  early — no OpenGL/WebGL renderer exists in SurrealEngine, would need a
  whole new renderer backend first) — the ground-up Three.js rewrite path
  remains the right framing, informed by a real native VR implementation
  instead of pre-implementation guesses.

This is a **planning deliverable**, not a commitment to build it — write the
plan, then stop and hand it back for a decision, same as this document's own
role relative to the M2-M4 work above.
