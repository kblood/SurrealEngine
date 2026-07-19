# Next Steps Plan — verified state + prioritized tasks (2026-07-19)

Written after a fresh audit of the repo, both branches, the four VR docs, and
the WebGPU backend source. Companion to `PLAN.md` (status log),
`WEBXR_IMPLEMENTATION_PLAN.md` (WebXR technical log), and
`VR_IMPLEMENTATION_PLAN.md` (native VR technical log). Each task below states
whether an autonomous agent can complete it alone (**AGENT-SAFE**) or whether
it needs the user (**NEEDS USER**), and why.

## Verified current state (audited, not assumed)

- Working tree clean on `webxr-m1`. All three local branches (`master`,
  `vr-m2`, `webxr-m1`) are **exactly in sync** with `fork`
  (github.com/kblood/SurrealEngine); `master` matches `origin/master`
  (dpjudas upstream). Nothing unpushed.
- Branch ancestry: `webxr-m1` branches **off `vr-m2`'s code tip**
  (`bf8e76d5`), so it contains all native-VR commits plus the
  Emscripten/WebGPU work. `git diff vr-m2..webxr-m1 -- RenderDevice/Vulkan
  RenderDevice/D3D11` is empty — the WebXR work added zero changes to the
  native backends beyond what `vr-m2` already carried. (Note: vs `master`
  both branches *do* touch Vulkan/D3D11 — that's the intentional vr-m2 VR
  plumbing: `VulkanXRSession`, `ProjectionOverride`, `ProjCenterX/Y`, etc.)
- Each branch has its own duplicate "Add VR/WebXR planning docs" commit
  (`75630655` on vr-m2, `bfc70921` on webxr-m1). `vr-m2`'s copies of the
  docs are now **stale** (missing the WebXR M1/M2 completion logs, which
  live only on `webxr-m1`).
- WebXR M1+M2 are done and committed on `webxr-m1` (backend:
  `SurrealEngine/RenderDevice/WebGPU/`, 18 files, ~2,400 lines; harness:
  `web/smoke_test.py`, `web/smoke_test_webgpu.py`, `web/serve.mjs`). No
  TODO/FIXME markers anywhere in the WebGPU backend.
- **M3 has not been started.** Important finding below: M3's original
  framing no longer matches the implemented code — see Task 5.
- Native VR (`vr-m2`) M2 step 4 is **still blocked** for headset-less local
  testing: the OpenXR `ActiveRuntime` registry value
  (`HKLM\SOFTWARE\Khronos\OpenXR\1`) still points at
  `C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json`,
  and this session is still unelevated (verified 2026-07-19) — the
  SteamVR-null-driver workaround still can't be applied without an elevated
  step. Unblock options in Task 7.

---

## P0 — Quick verification/fix tasks (do before any M3 feature work)

### Task 1: Prove or disprove the `DrawComplexSurface` V-orientation question — AGENT-SAFE

The open flag from M2: `DrawTile` rendered V-flipped despite being a verbatim
port of D3D11's math (fixed by an empirical V/VL swap,
`WebGPURenderDevice.cpp:801-803`); `DrawComplexSurface` builds vertices
independently (`DrawComplexSurfaceFaces`, `WebGPURenderDevice.cpp:575-657` —
confirmed byte-identical `(v - VPan) * VMult` math to
`D3D11RenderDevice.cpp:1914`), and "verbatim port" already failed to
guarantee correctness once, so parity alone proves nothing. The M2 test map's
mostly-tileable wall textures couldn't show a flip.

Concrete method (all infrastructure already exists):
1. Pick a UT99 map view containing asymmetric/directional world-surface
   texture content — wall text or numbered panels. `DM-Deck16][` itself has
   directional signage; `CTF-Face` has large "FACE"-logo wall textures. Use
   the pinned `--url=<map>` autoplay flag both natively and in the browser
   harness.
2. Capture native reference: `build/Release/SurrealEngine.exe --autoplay
   --url=<map> "<GOG UT99 folder>"` + `PrintWindow`/`PW_RENDERFULLCONTENT`
   screenshot (established technique, see `PLAN.md` 2026-07-18 M1 entry).
3. Capture WebGPU: extend/copy `web/smoke_test_webgpu.py` to load the same
   map URL and save the canvas screenshot (it already does screenshots).
4. Compare the same wall region: text readable and same-way-up in both →
   no bug, close the flag in `WEBXR_IMPLEMENTATION_PLAN.md`. Flipped →
   fix analogous to `DrawTile`'s (likely a V-axis sign in
   `DrawComplexSurfaceFaces`' `TexCoord[1]`), then re-verify both
   screenshots AND re-check lightmap alignment (TexCoord2 uses the same
   `v` — a flip would also misplace shadows relative to geometry, which is
   corroborating evidence either way).
5. While in there, eyeball `DrawGouraudPolygon` output too (weapon/actor
   meshes, `WebGPURenderDevice.cpp:659` — UV pass-through, also never
   explicitly orientation-checked; a skin with readable detail suffices).

Cameras may not frame identically between native and browser runs; that's
fine — the check is orientation of a recognizable texture, not pixel diff.
Document the result in `WEBXR_IMPLEMENTATION_PLAN.md` M2 section either way.

### Task 2: Root-cause (or consciously park) the post-quit `querySelector` JS error — AGENT-SAFE

Known since M1 (`WEBXR_IMPLEMENTATION_PLAN.md` M1 status): after
`Surreal_RequestQuit()` cleanly freezes the tick loop, Emscripten's SDL2-port
teardown (`restoreOldStyle`/`findEventTarget`) calls `querySelector` with a
garbage string — suspected stale C-string pointer during
`SDL_DestroyWindow`'s resize-listener teardown, same bug family as the two
M1 heap bugs. Non-blocking, but it's an unexplained memory-lifetime symptom
in a codebase that already produced two real wasm32 heap bugs — cheap to
chase now with the established technique (`-sSAFE_HEAP=1 -g2` temp rebuild,
capture the full stack in `smoke_test.py`), expensive to rediscover later if
it's masking corruption that M4's WebXR session teardown will retrigger.
Timebox it: if a `SAFE_HEAP` run + one stack trace doesn't localize it,
record findings in the doc and move on — do not let it block M3.

---

## P1 — Repo/docs hygiene

### Task 3: Commit this file + sync the shared docs story — AGENT-SAFE

1. Commit `Docs/VR/NEXT_STEPS_PLAN.md` (this file) on `webxr-m1`, push to
   `fork`.
2. Add one line to `Docs/VR/PLAN.md`'s status log noting the audit date and
   pointing here.
3. The stale-docs split: `vr-m2`'s `Docs/VR/*` copies predate the WebXR
   M1/M2 logs. Cheapest fix that avoids merge complexity: declare in
   `PLAN.md` (on `webxr-m1`) that **`webxr-m1` carries the canonical docs**
   for both efforts, and optionally cherry-pick the docs-only commits onto
   `vr-m2` when native work next resumes. Do NOT merge branches — `master`
   stays an untouched upstream mirror, and the two efforts stay separate
   per the established structure.

### Task 4: (Optional, low priority) prune `web/webgpu_smoke_screenshot.png` from the repo — AGENT-SAFE

A 1.1 MB binary test artifact is committed (`git diff --stat master..webxr-m1`
shows `web/webgpu_smoke_screenshot.png | Bin 0 -> 1121662 bytes`). It's
regenerated by every smoke-test run. Add it to `.gitignore` and `git rm
--cached` it in a follow-up commit. History still carries the blob — fine;
don't rewrite pushed history for this.

---

## P2 — WebXR M3: texture-binding model — RE-SCOPE FIRST, then implement

### Task 5: Re-scope M3 — the original framing is partially obsolete — AGENT-SAFE (analysis + proposal); scope sign-off NEEDS USER

The roadmap describes M3 as "bindless-texture-model redesign — retarget
`DescriptorSetManager`'s flush-and-clear policy from Vulkan descriptor-set
updates to WebGPU bind-group recreation." **The implemented M2 backend
doesn't use `DescriptorSetManager` at all** — it mirrors D3D11's
fixed-4-slot model and already works (0 validation errors, 94 draws,
~57 fps in desktop Chrome on DM-Deck16][). The "bindless has no WebGPU
equivalent" problem (the `WEBXR_PORT_PLAN.md` headline blocker,
`DescriptorSetManager.h:68`'s 16,536-slot array) was *sidestepped*, not
ported — so M3 as originally worded (redesigning a bindless model that was
never brought over) has largely already happened by construction.

What genuinely remains, in the code as it exists today:

- **Per-draw bind-group creation**: `DrawEntry`
  (`WebGPURenderDevice.cpp:500-546`) calls `wgpuDeviceCreateBindGroup` +
  `wgpuBindGroupRelease` for every draw call, every frame (documented as a
  deliberate M2 simplification at line 513). This is the clearest
  M3-shaped work item: cache bind groups.
- **Batch fragmentation**: batches break on any texture/sampler change
  (`SetDescriptorSet`, lines 401-443) — inherent to the fixed-slot model,
  same as D3D11; acceptable unless profiling says otherwise.
- **Small geometry buffers**: `VertexBufferCapacity = 16*1024`,
  `IndexBufferCapacity = 32*1024` (`WebGPURenderDevice.h:96-97`) force a
  mid-frame end-pass/submit/reopen (`DrawBatches` `submitBoundary` path)
  when full. Cheap to enlarge; measure first.

Proposed re-scoped M3 (write this into `WEBXR_IMPLEMENTATION_PLAN.md` and
get the user's one-line OK before large refactors):

1. **Measure first** (AGENT-SAFE): add frame-time + `Stats` (DrawCalls,
   BuffersUsed) reporting to `smoke_test_webgpu.py` on a heavier map than
   DM-Deck16][ (e.g. a large DM or CTF map via `--url=`), desktop Chrome.
   This decides how much of steps 2-4 is worth doing. Note the real M4/M5
   target is Quest 3's browser — desktop numbers are only a lower bound on
   trouble, so don't over-conclude from a fast desktop result.
2. **Bind-group cache** (AGENT-SAFE): key = the 4 `WebGPUCachedTexture*` +
   3 sampler-mode ints already stored per `WebGPUDrawBatchEntry`
   (`WebGPURenderDevice.h:15-28`). Either an `std::unordered_map` on the
   device with an eviction hook, or store per-combination groups on the
   primary `WebGPUCachedTexture`. Invalidation points: texture destruction
   (`WebGPUTextureManager` flush paths — `Flush(bool)`,
   `UpdateTextureRect`) must drop dependent cached groups. Verify: same
   screenshots as M2, `Surreal_GetWebGPUErrorCount()==0`, measurable
   reduction in per-frame `CreateBindGroup` churn (add a counter to
   `Stats`).
3. **Buffer sizing** (AGENT-SAFE): if step 1 shows multiple `BuffersUsed`
   per frame, raise vertex/index capacities and/or count submit boundaries;
   trivial and low-risk.
4. **`binding_array` watch item — do NOT build on it now**: fixed-size
   `binding_array<texture_2d<f32>>` was still an unshipped proposal as of
   the 2026-07 recon (`WEBXR_PORT_PLAN.md`). Re-check its Chrome/Dawn status
   once, note the finding, and only revisit if the fixed-slot model proves
   an actual bottleneck on Quest 3 hardware in M4/M5 testing. Anything
   deeper than steps 1-3 (texture atlasing, a redesigned binding strategy)
   is currently unjustified by evidence — flag, don't build.

The genuinely unresolved question — whether steps 1-3 are *sufficient* for
Quest 3 browser performance — cannot be answered until M4 runs on the
headset. That is an M4/M5 finding, not an M3 blocker. If the user agrees
with this re-scope, M3 becomes days, not weeks, and **M4 (WebXR session)
becomes the real next substantial milestone.**

### Task 6: M4 pre-work that needs no scope decision — AGENT-SAFE

Independent of Task 5's outcome, these M4 preparations are safe to start:
- Port the `webxr-port/` (QuakeQuest) WebXR session/IWER-headless-test
  harness pattern into `web/` (session lifecycle JS, `xrCompatible` device
  request, IWER-based headless stereo verification — proven in this
  workspace, see `WEBXR_PORT_PLAN.md` "Precedent" section).
- Confirm `emdawnwebgpu` + `XRGPUBinding` current browser support status
  (Editor's Draft dated 2026-06-15 per the docs; verify what Chrome
  stable/Quest browser actually ship today) and record it in
  `WEBXR_IMPLEMENTATION_PLAN.md` before designing the M4 render path.
- Design note: stereo math reuse comes from `vr-m2`'s verified
  `ProjectionOverride`/`ViewportOverride` work (already on this branch,
  since `webxr-m1` contains it — see "Verified current state").

---

## P3 — Native VR mod (`vr-m2`)

### Task 7: Unblock M2 step 4 (OpenXR session/swapchain) — NEEDS USER (one of three options)

Still blocked, re-verified today: `ActiveRuntime` unchanged, session
unelevated. `VulkanXRInitOverrides` plumbing (`bf8e76d5`) means the Vulkan
side is already prepared; only the session/swapchain code in
`VulkanXRSession` remains, and the plan deliberately refuses to write it
unrunnable-blind (`VR_IMPLEMENTATION_PLAN.md` M2 step 4). Options, any one
of which unblocks:

- **Option A (cheapest, ~1 minute of user time):** user runs, from an
  elevated PowerShell, a registry swap to SteamVR's runtime, e.g.:
  `Set-ItemProperty "HKLM:\SOFTWARE\Khronos\OpenXR\1" -Name ActiveRuntime
  -Value "C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json"`
  (verify the exact json path on disk first; back up the current value —
  it is `C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json`).
  Then an agent re-applies the SteamVR null-driver `steamvr.vrsettings`
  edit (documented in `PLAN.md` 2026-07-18), starts `vrserver.exe`, and
  confirms `--probexr` passes `xrGetSystem`. Everything after that
  (implementing + testing session/swapchain/frame-loop against the null
  driver) is AGENT-SAFE. Registry must be restored afterward.
- **Option B:** user relaunches the agent session elevated (then the
  registry step itself also becomes AGENT-SAFE — but elevation for the
  whole session is a bigger hammer than Option A).
- **Option C (no registry change at all):** user puts on the Quest 3 with
  Virtual Desktop connected; `xrGetSystem` then succeeds against the
  *current* runtime and step 4 can be implemented/tested live per the
  original plan. Best fidelity, most user time.

Until one happens, there is **no productive agent-only work left on
`vr-m2`** — everything session-less-testable was already done. Recommend
the user pick Option A at their convenience; don't hold WebXR work on it.

---

## Suggested execution order

| # | Task | Who |
|---|------|-----|
| 1 | DrawComplexSurface/Gouraud orientation check (+fix if real) | Agent |
| 2 | Commit this plan, PLAN.md pointer, push (Task 3.1-3.2) | Agent |
| 3 | Post-quit querySelector root-cause, timeboxed (Task 2) | Agent |
| 4 | M3 re-scope written into WEBXR_IMPLEMENTATION_PLAN.md (Task 5) | Agent, then **user OK on scope** |
| 5 | M3 steps 1-3: measure, bind-group cache, buffer sizing | Agent |
| 6 | M4 pre-work: WebXR harness port, XRGPUBinding status check (Task 6) | Agent |
| 7 | Screenshot-artifact gitignore, docs-canonical note (Tasks 3.3, 4) | Agent |
| 8 | vr-m2 unblock: registry one-liner / elevated session / headset (Task 7) | **User** (then agent) |
| 9 | M4 proper (WebXR stereo session) | Agent, design doc first |
| 10 | In-headset verification (both efforts, eventually) | **User** |

Items 1-7 are executable autonomously today with zero clarifying questions.
Item 8 needs one small user action; item 10 fundamentally requires the
headset on a human head.
