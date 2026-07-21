# Second-opinion analysis — 2026-07-20 (rendering issues from HANDOFF_2026-07-20.md)

Independent review of the three open rendering problems (stereo HUD double
vision, UI outside FOV, world-geometry warp) plus a brief note on the
`UObject::SetBool()` bug. All claims below were verified by reading the actual
code in this worktree, with file:line citations. No source was modified.

**Headline finding**: the "genuinely unexplained" geometry warp has a concrete,
derivable root cause in the code — the vertical half of the asymmetric per-eye
frustum is applied upside down in `DrawSceneVR()`. The horizontal half is
correct. The handoff's suspicion that the warp "could be inherent to wide
rectilinear FOV" is very likely wrong; this is a real bug, and it was invisible
to every earlier test because every previous projection in this engine was
vertically symmetric. Full derivation in section 3.

---

## 1. Stereo HUD double vision

### Diagnosis: AGREE with the handoff, with one refinement

The handoff's read is correct. The mechanism, traced through the code:

- `RenderOverlaysVR()` (`SurrealEngine\Render\RenderCanvas.cpp:85-137`) and
  `PostRenderVR()` (`RenderCanvas.cpp:151-195`) each split `Canvas.Frame` into
  two half-width viewports (`Frame.XB += halfWidth` for eye 1, `Frame.X =
  halfWidth`) and halve `canvas->ClipX()/SizeX()` so UnrealScript lays the HUD
  out inside each half.
- The 2D tile path gets its screen mapping from
  `VulkanRenderDevice::SetSceneNode()` (`RenderDevice\Vulkan\
  VulkanRenderDevice.cpp:1043-1081`). `Canvas.Frame` has no
  `ProjectionOverride`, so line 1061-1062 builds a **symmetric** frustum from
  `Frame->FovAngle`, and `ProjCenterX` (line 1072) lands at the geometric
  center of the half viewport.
- But the half viewport is displayed by the compositor through the **real
  asymmetric** per-eye FOV (`DrawSceneVR()`, `Render\RenderScene.cpp:155-159`;
  submitted as layer metadata in `VulkanXRSession::EndFrame()`,
  `RenderDevice\Vulkan\VulkanXRSession.cpp:637-639`). The pixel that
  corresponds to "straight ahead" for a given eye is NOT at the viewport
  center — it is at `x_fwd = halfWidth * (0 - tan(angleLeft)) /
  (tan(angleRight) - tan(angleLeft))`. Since each eye's horizontal FOV is
  biased toward the temporal side (|angleLeft| > angleRight for the left eye,
  mirrored for the right), "viewport center" maps to two *different* gaze
  directions in the two eyes. A crosshair drawn at both centers is therefore
  two different visual directions → diplopia. Exactly what the user reported.

**Refinement the handoff missed**: even if both centered copies were moved to
the per-eye forward direction, identical directions in both eyes fuse at
*infinity*. A crosshair/HUD at infinite depth over near world geometry is
uncomfortable (vergence conflict) and the crosshair would appear "behind"
walls. The proper target is convergence at a finite virtual depth (~1.5-2 m),
which requires a small opposite-signed horizontal offset per eye on top of the
forward-direction correction. Both corrections come from the same math (below).

There is also a **vertical** component nobody mentioned: `angleUp` !=
`|angleDown|` on most HMDs, so the vertical center of the half viewport is
also not the forward direction. Vertically the error is the same sign in both
eyes, so it does not cause diplopia — it shifts the whole HUD up/down instead
(contributes to issue 2).

### Concrete fix (phase 1 — canvas sub-rect reprojection)

Do the whole thing inside the existing per-eye loops in `RenderOverlaysVR()`
and `PostRenderVR()`; all inputs are already available as members
(`VREyeFov[eye]` and `VREyeFrame[eye]` live in `RenderSubsystem`, see
`Render\RenderSubsystem.h:35-37,127-132`).

Instead of setting `Canvas.Frame` to the raw half viewport, set it to a
**sub-rectangle of the half viewport chosen so that a fixed "virtual screen"
(head-locked, ~50° wide, 4:3, at ~1.75 m) lands in the right place per eye**:

```cpp
// per eye:
float tanL = std::tan(VREyeFov[eye][0]);   // negative
float tanR = std::tan(VREyeFov[eye][1]);   // positive
float tanU = std::tan(VREyeFov[eye][2]);   // positive
float tanD = std::tan(VREyeFov[eye][3]);   // negative

// virtual HUD screen: total width 2*hudHalfTanX at depth hudDepth
const float hudHalfTanX = std::tan(radians(25.0f));  // ~50 deg wide
const float hudHalfTanY = hudHalfTanX * 0.75f;       // 4:3

// convergence: aim each eye's copy at the head-forward ray, not the eye's own axis
float ipdTan = (ipdMeters * 0.5f) / hudDepthMeters;  // hudDepthMeters ~ 1.75
float shift  = (eye == 0) ? +ipdTan : -ipdTan;       // left eye shifts right, right eye left

// tan-space -> pixel mapping of this eye's half viewport
// horizontal: pixel x = halfWidth * (t - tanL) / (tanR - tanL)
int x0 = (int)std::round(halfWidth  * ((-hudHalfTanX + shift) - tanL) / (tanR - tanL));
int x1 = (int)std::round(halfWidth  * (( hudHalfTanX + shift) - tanL) / (tanR - tanL));
// vertical: framebuffer top row corresponds to angleUp (see section 3 - do
// this AFTER the vertical-frustum fix, or these two errors will fight):
// pixel y = fullHeight * (tanU - t) / (tanU - tanD), t up-positive
int y0 = (int)std::round(fullHeight * (tanU - hudHalfTanY) / (tanU - tanD));
int y1 = (int)std::round(fullHeight * (tanU + hudHalfTanY) / (tanU - tanD));

Canvas.Frame.XB = eyeXB + x0;   Canvas.Frame.X = x1 - x0;
Canvas.Frame.YB = fullYB + y0;  Canvas.Frame.Y = y1 - y0;
// FX/FY/FX2/FY2, ClipX/ClipY, SizeX/SizeY scaled to match, as the current code
// already does for the plain half split.
```

- `ipdMeters` should come from the live tracked eye poses, not a constant:
  `length(eyes[1].pos - eyes[0].pos)` from `LocateViews()`
  (`VulkanXRSession.cpp:556-560`) — it changes per user/headset IPD setting.
  Plumb it into `SetPendingVREyes()` or compute it in `Engine::Run()` next to
  the existing composition (`Engine.cpp:385-429`) and store it alongside
  `VREyeFov`.
- Do it in two steps if you want to isolate variables in headset testing:
  step A with `shift = 0` (forward-direction only, converges at infinity —
  should already collapse the double image to a single slightly-"far" one),
  step B add the `ipdTan` shift (pulls it to a comfortable depth).
- This fixes issue 2 at the same time (see below) because the sub-rect is
  both *repositioned* and *shrunk*.
- The weapon viewmodel is unaffected: it is drawn through
  `MainFrame.Frame = VREyeFrame[eye]` (`RenderCanvas.cpp:110,178`), i.e. the
  real 3D per-eye projection, not the canvas rect. That's correct and should
  stay as is.

### Phase 2 (later, optional but better): OpenXR quad layer

Render the HUD once into its own offscreen texture / XR swapchain and submit
it as an `XrCompositionLayerQuad` (head-locked pose, ~1.75 m, on top of the
projection layer). The compositor then does the per-eye reprojection with
correct convergence and full sharpness for free, and every per-headset FOV
question disappears. More plumbing (a second swapchain + a render-to-texture
canvas pass), so treat it as the follow-up once phase 1 proves the layout, not
the first move.

### Known leftovers even after the fix (flag now so nobody re-diagnoses them)

- `PostRenderVR()` calls `DrawTimedemoStats()`/`DrawCollisionDebug()` **after**
  restoring the full-window frame (`RenderCanvas.cpp:185-194`) — those debug
  overlays will straddle the seam and appear once across both eyes. Harmless
  for gameplay, confusing in a headset with `timedemo 1` on.
- `ResetCanvas()` sets `console->FrameX()/FrameY()` from the full window
  (`RenderCanvas.cpp:42-43`) and the VR splits never adjust them. Any UScript
  that lays out against `Console.FrameX` instead of `Canvas.ClipX` will still
  use full-window width. Check in headset whether console messages/chat land
  correctly after the phase-1 fix; adjust these two fields inside the per-eye
  loop if not.
- UWindow menus (`ShowMenu` is bound to left-menu in
  `UpdateVRControllerInput()`, `Engine.cpp:1811-1812`) draw through the
  console path against the full window — menus will span the seam and be
  effectively unusable in-headset until they get the same treatment (or until
  the fullscreen-console fallback path in `Render\RenderSubsystem.cpp:60-66`
  catches them, which only happens when the world isn't drawn at all).

---

## 2. UI laid out outside the comfortable FOV

### Diagnosis: AGREE — same root cause as issue 1, and the same fix covers it

Currently each eye's HUD canvas spans the **entire** per-eye FOV (~90-100°
horizontal on Quest-class hardware). UT99's HUD pins elements to the canvas
edges (`Canvas.ClipX - x` for ammo etc.), which lands them at the extreme
periphery — outside the lens sweet spot and partially outside the visible
area. On a desktop monitor (~30-40° of your visual field) edge-pinned UI is
fine; mapped onto a 100° cone it is not.

The phase-1 sub-rect above is the fix: the HUD canvas becomes a virtual ~50°
4:3 screen centered on the forward direction, so edge-pinned elements land at
±25° — comfortably inside the sweet spot. Tune `hudHalfTanX` in headset
(45-60° total are typical values in VR ports of flat games). No separate work
needed beyond exposing the constant somewhere tweakable.

One risk: with the canvas shrunk, `Canvas.uiscale` (computed from full
`ViewportHeight` in `ResetCanvas()`, `RenderCanvas.cpp:16`) may make text
small. If in-headset text is too small to read, scale `ClipX/ClipY/SizeX/SizeY`
by an extra factor (equivalent to lowering the virtual screen resolution)
rather than touching uiscale globally.

---

## 3. World geometry warps as the head turns

### Diagnosis: DISAGREE with "possibly inherent / unexplained" — there is a concrete bug

The handoff says do-not-guess-fix, and I did not guess: the following is
derived step by step from the actual code. I'd still classify it "very high
confidence" rather than "proven in headset", and the validation protocol below
is cheap, so run it.

**The bug: `DrawSceneVR()` applies the vertical FOV asymmetry upside down.**

Chain of evidence:

1. View space in this engine is UE1 convention (x fwd, y right, z up).
   `Coords::ViewToRenderDev()` (`Math\coords.h:184-192`) with the
   axes-as-columns convention of `Coords::ToMatrix()` (`coords.h:204-224`,
   columns 0/1/2 = XAxis/YAxis/ZAxis; confirmed by its use as
   `ViewToRenderDev().ToMatrix() * invRotation.ToMatrix() * ...` in
   `RenderScene.cpp:143`) maps a view vector v to renderdev space as
   `(v.y, -v.z, v.x)` — i.e. **renderdev eye space is x-right, y-DOWN,
   z-forward**. Y-down is deliberate: it makes the image come out upright in
   Vulkan without a negative-height viewport (and indeed
   `SetSceneNode()` uses a plain positive-height viewport,
   `VulkanRenderDevice.cpp:1052-1059`; nothing else flips Y).
2. `mat4::frustum()` (`Math\mat.cpp:132-161`) is the standard GL formula: the
   `bottom` parameter maps to NDC y = -1 and `top` to NDC y = +1. In Vulkan,
   NDC y = -1 is the **top row** of the framebuffer. So with this pipeline:
   **the `bottom` argument describes the top edge of the image, and because
   eye-space y is down-positive, negative eye-y is visually up.** For the
   symmetric case (`-Aspect*RProjZ, +Aspect*RProjZ`, `ResetCanvas()`
   `RenderCanvas.cpp:34`, `SetSceneNode()` line 1062, `DrawSceneStereo()`
   `RenderScene.cpp:90`) this is all invisible — which is why nothing ever
   caught it.
3. `DrawSceneVR()` (`RenderScene.cpp:155-159`) passes
   `frustum(tan(angleLeft), tan(angleRight), tan(angleDown), tan(angleUp))`.
   Work through what lands where:
   - Framebuffer **top row** ← NDC -1 ← eye-y = `tan(angleDown)` (negative) ←
     negative eye-y = visually **up**, extent `|tan(angleDown)|`.
   - Framebuffer **bottom row** ← NDC +1 ← eye-y = `tan(angleUp)` (positive) ←
     positive eye-y = visually **down**, extent `tan(angleUp)`.
   So the rendered image covers `|angleDown|` of upward view at its top and
   `angleUp` of downward view at its bottom.
4. The compositor is told the opposite. `EndFrame()` submits the raw OpenXR
   fov (`VulkanXRSession.cpp:639`), and per the OpenXR spec the top of the
   subimage corresponds to `angleUp`. So the image content is vertically
   mislabeled by the difference between `angleUp` and `|angleDown|`.
5. The horizontal half of the same reasoning comes out **correct**:
   framebuffer left column ← eye-x = `tan(angleLeft)` = visually left (x-right
   eye space, no flip), which matches the metadata. So only vertical is wrong.
   Note `DrawSceneStereo()` — the "proven" template this code cites at
   `RenderScene.cpp:111-116,153` — only ever exercised **horizontal**
   asymmetry (its vertical bounds are symmetric, line 90). The proof never
   covered the axis that's broken. The comment at `RenderScene.cpp:151-154`
   ("angleLeft/angleDown are negative ... so these are already the correct
   signed frustum bounds") is exactly the plausible-but-wrong conclusion this
   flip produces.

**Why this looks like "warp while turning"**: HMD FOVs are vertically
asymmetric (typically several degrees more downward than upward). The flip
means the rendered content disagrees with the declared FOV by
`2*(|angleDown| - angleUp)` worth of vertical mapping — the image is
effectively vertically offset and slightly rescaled relative to what the
compositor's distortion + timewarp assumes. The compositor error is
gaze-direction-dependent, so world geometry appears to shear/swim as the head
rotates (worst during pitch, but visible during yaw too since the error
interacts with the lens distortion correction across the whole view). Both
eyes get the same vertical error, so it does NOT cause double vision — which
matches the report exactly: world fuses fine but warps.

### The fix (one line, after validation)

`RenderScene.cpp:159` — the vertical arguments must be negated-and-swapped so
that the framebuffer top row gets the `angleUp` extent:

```cpp
// bottom (NDC -1, framebuffer TOP) must be -tan(angleUp);
// top (NDC +1, framebuffer BOTTOM) must be -tan(angleDown).
mat4 projection = mat4::frustum(l, r, -u, -d, 1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);
```

(`-u` is negative and `-d` positive, so bottom < top holds. Horizontal `l, r`
unchanged. `SetSceneNode()`'s derived `ProjCenterY`/`RFY2` at
`VulkanRenderDevice.cpp:1070-1073` adapt automatically since they read the
matrix. The layer metadata in `EndFrame()` stays as-is — it is already
correct.)

### Validation protocol (do this first, per the handoff's instruction)

1. **Add the missing FOV log.** The comment at `Engine.cpp:367` says
   `LocateViews` is "logged internally", but it only logs *failures*
   (`VulkanXRSession.cpp:552`). Add a one-shot (first successful call) log of
   all eight angles in degrees plus the per-eye pose positions. Expected
   sanity values for Quest-class hardware: |angleLeft/Right| ~40-50°,
   angleUp ~40-50°, |angleDown| a few degrees larger than angleUp, eye
   position separation ~0.055-0.072 m. If `angleUp == |angleDown|` on this
   runtime, this bug is masked and the warp has another cause — that single
   log line decides it.
2. **A/B in headset**: build with only the one-line frustum change and compare
   "world warps when I turn my head" before/after. If the FOV log shows
   asymmetry and the flip fix kills the warp, done. If the warp persists,
   continue down the secondary list below (in order).

### Secondary suspects, in priority order (checked; mostly exonerated)

- **Letterbox contamination of the eye blit.** `DrawPresentTexture()`
  letterboxes the composited image into the window swapchain
  (`VulkanRenderDevice.cpp:1482-1500`) and the XR blit then copies raw window
  halves (`:1546,1565-1566`). If `presentWidth/Height` (window swapchain,
  `CommandBufferManager.cpp:84`) ever differs from
  `Viewport->GetNativePixelWidth/Height()`, black bars and a scale/offset
  enter the eye images and break the FOV mapping the same way the vertical
  flip does — in both axes. Normally they're equal (letterbox = 0), but log
  `letterboxX/Y/Width/Height` once when `HasPendingXRTargets()` is true to
  rule it out, especially if running windowed / with DPI scaling.
- **World scale.** `UUPerMeter = 1/0.0254` (`Engine.cpp:111`, 1 UU = 1 inch).
  If perceived world scale is off, head *translation* parallax disagrees with
  the compositor and produces swim during motion (a different symptom than
  rotation-warp, but users conflate them). The 1 UU = 1 inch convention is
  defensible via `BaseEyeHeight = 64`; just be aware some UT geometry was
  authored "chunky" and may read as slightly wrong scale regardless. Not a
  code bug; do not touch unless headset testing after the frustum fix still
  reports scale/swim problems.
- **Timewarp pose consistency** — exonerated. `EndFrame()` submits the same
  raw tracked poses used (via the constant recenter-yaw world transform,
  `Engine.cpp:418-423`) to render. The tracking-space→world mapping is rigid
  and constant per recenter, so submitted pose/fov and rendered content are
  consistent. Not the warp source.
- **Pose composition math** — exonerated. Quaternion ctor order (x,y,z,w)
  matches usage (`quaternion.h:21` vs `Engine.cpp:389`), `q * vec3` is the
  correct rotation formula (`quaternion.h:281-285`), `XRVecToUE1` is a
  consistent relabeling applied uniformly to basis vectors and positions
  (`Engine.cpp:97-100`), and the per-eye basis is orthonormal by construction.
  I found no rotation-composition bug.
- **Genuinely inherent rectilinear-FOV distortion** — real but should NOT
  cause frame-to-frame warp. A correctly rendered + correctly declared
  rectilinear projection is exactly what the compositor expects; edge
  stretching exists *within* a frame but the world stays solid under head
  rotation. If after the frustum fix the world is stable but edges still look
  stretched when you *saccade* to them, that residual is the inherent part and
  is normal.

### Related quality note (not the warp, but same code path)

Per-eye source resolution is `(windowWidth/2) x windowHeight` (e.g.
960x1080 from a 1080p window) stretched to the runtime-recommended per-eye
swapchain (`VulkanXRSession.cpp:420-421`; typically ~1800-2200 px square-ish).
That is roughly quarter of the pixel density the headset wants — the image
will be soft/shimmery even when geometrically correct. Eventually the VR path
should render at `2 * recommendedWidth x recommendedHeight` (or at least allow
forcing the window/scene buffer that large). Worth logging
`swapchainWidth/Height` next to the FOV log so the gap is visible in numbers.

---

## 4. `UObject::SetBool()` byte/bool type confusion (brief)

Confirmed as described: `UObject.cpp:311-315` unconditionally
`static_cast<UBoolProperty*>`s whatever property resolves and does bitfield
math on it; `Engine::UpdateInput()` calls it for every active button binding
every frame (`Engine.cpp:1691-1692`), so keyboard/mouse `bFire` (a byte
property) goes through the same UB the VR path just side-stepped. The right
fix is a type check in `SetBool` (fall back to `*static_cast<uint8_t*>` for
`UByteProperty`, or route through a shared "set truthy" helper), plus an audit
of `SetInt`/`SetFloat`/`SetByte` (`UObject.cpp:301-320`) which have the same
blind-cast shape. Generic engine work, correctly deprioritized — but do it
before ever debugging another "input feels flaky" report on the flatscreen
path.

Drive-by (unrelated, unused by the VR path, noting so it doesn't bite later):
`quaternionT::operator*=` (`Math\quaternion.h:33-38`) is wrong — it overwrites
`x` and then uses the new value computing `y/z/w`, and treats `x` as the
scalar slot besides. The non-member `operator*(quat, quat)`
(`quaternion.h:270-278`) is correct and is what `euler()` resolves to, so
nothing currently hits the bad one. Delete or fix it before someone does.

---

## 5. Prioritization / sequencing recommendation

1. **Re-verify the three input fixes in headset** (already the handoff's next
   step; nothing here changes that). While you're in there, capture the two
   new one-shot logs below in the same run — zero extra headset sessions.
2. **Add diagnostics (no behavior change)**: one-shot FOV+pose+swapchain-size
   log in `LocateViews()`; one-shot letterbox log in the XR branch of
   `DrawPresentTexture()`. These decide the warp question with data.
3. **Vertical frustum fix** (`RenderScene.cpp:159`, one line) — gated on the
   FOV log confirming `angleUp != |angleDown|`. Test in headset: warp should
   collapse. Smallest possible change, biggest expected payoff.
4. **HUD phase 1** (sub-rect reprojection in `RenderOverlaysVR()`/
   `PostRenderVR()`): step A forward-direction centering, step B convergence
   shift, plus the ~50° virtual-screen shrink. Depends on step 3 being in
   first — the vertical mapping formula assumes the fixed frustum; doing HUD
   first means calibrating against a broken vertical mapping and redoing it.
5. **Follow-ups as separate items**: console/menu/full-window overlay handling
   in VR (seam-straddling draws), per-eye render resolution, quad-layer HUD
   (phase 2), `SetBool` engine fix.

This ordering means the "M4 — VR HUD/UI" scoping question in the handoff
resolves to: the geometry warp is NOT part of that milestone — it is a
one-line projection fix that should ride along with the input re-verification
cycle. Only the HUD/UI work (items 4-5) deserves the milestone label.

## 6. Risks / edge cases for the fixes

- **Per-headset variation**: all HUD math must read the live
  `VREyeFov[eye]`/eye poses each frame, never bake constants — FOV differs per
  device, per runtime, and (on Quest) per IPD slider position; some runtimes
  even vary FOV at runtime. The proposed formulas do this; keep it that way.
  The only tunables that should exist are `hudHalfTanX` (virtual screen size)
  and `hudDepthMeters` (convergence), both taste, not hardware.
- **Symmetric-FOV runtimes** (some WMR/SteamVR configs report `angleUp ==
  |angleDown|`): the vertical-flip bug is invisible there. If a second headset
  ever tests this build, don't let "no warp on device B" argue against the fix.
- **Odd window widths**: `halfWidth = fullWidth / 2` truncates
  (`RenderScene.cpp:137`, `RenderCanvas.cpp:89,160`, blit `:1546`) — a 1-px
  mismatch between the rendered half and the blitted half at odd widths.
  Cosmetic, but clamp/round consistently when touching this code.
- **HUD legibility**: shrinking the canvas shrinks text; may need a virtual
  resolution bump (section 2). Test with the smallest text in the game
  (console messages) before calling it done.
- **Crosshair depth vs aim**: a fixed-depth crosshair won't sit exactly on the
  aimed surface. Acceptable baseline for M-whatever; the eventual answer is a
  world-space crosshair raycast to the hit point, which becomes natural once
  aiming moves to the controller rather than the head.
- **Mirror window**: after the HUD fix the desktop mirror shows two shrunk,
  offset HUD copies. Cosmetic; don't burn time on it.
