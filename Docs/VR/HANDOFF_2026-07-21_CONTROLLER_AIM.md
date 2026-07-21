# Handoff — 2026-07-21 session (controller-aimed weapons, branch `vr-m2`)

Single point-in-time snapshot for picking this back up. Covers only the
native VR mod (`vr-m2`, this worktree: `C:\Devstuff\QuestGames\ut99-vr\SurrealEngine-vr-m2`).
The WebXR/browser port (`webxr-m1`) is a separate effort, not touched today.

## Status: all 6 planned milestones (M-A, M-B, M-C, M-D, M-F, M-E1)
## implemented, built, and non-interactively verified. Nothing has been
## tested in a real headset yet.

Working tree is clean. Commits, oldest first, all on `vr-m2`, pushed to
`fork` (`github.com/kblood/SurrealEngine.git`) as of this handoff:

- `b64a995f` — **M-A**: per-hand grip+aim OpenXR pose actions/spaces.
- `458899c7` — **M-B**: `Frame::InterceptCall` VM seam; viewmodel
  repositioned at the main hand's grip pose.
- `5c12bf88` — **M-C**: `Frame::InterceptCallPost` (seam extension);
  fire-scoped `ViewRotation` swap around `TraceFire`/`ProjectileFire`;
  `CalcDrawOffset` fire-origin intercept; crouch rebind.
- `401a42bf` — **M-D**: two-handed aim from the between-hands vector,
  foregrip grab/release hysteresis, blend/stability.
- `f1ad7643` — **M-F**: left/right-handed mode, weapon mesh mirroring.
- `673b89be` — **M-E1**: dual-wielded Enforcers, independent per-hand aim.

Design doc: `Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md` (the full plan, prior-art
research, and the `ViewRotation`/`AdjustedAim` architectural resolution —
read this first if you're continuing the work). Clean-room spec used for
M-E1: `Docs/VR/ENFORCER_DUALWIELD_SPEC.md`.

## Why this exists

User's feature request (see `PLAN.md`'s 2026-07-20/21 status log): aim
weapons from the motion controllers instead of head-look, with left/
right-handed mode, two-handed aim from hand-to-hand vector, dual-wielding
with independent per-hand aim, and hand models that track the controllers.
User then explicitly directed full autonomous implementation of whatever
design a research pass produced, with no check-ins between milestones —
that's what this session did, end to end.

## The core architectural problem and its solution

UT99 has no native fire-direction hook — firing is entirely driven by
compiled UnrealScript reading `Pawn.ViewRotation` (which also drives
movement direction via `PlayerMove`, so it can't just be permanently
redirected). The design doc's first hypothesis — write a controller-vs-
head delta into the `Weapon.AdjustedAim` property every frame — turned out
to be wrong: `AdjustedAim` is an *output*, overwritten by the script's own
`AdjustAim()` call at the moment of firing, so a per-frame write gets
clobbered exactly when it matters.

The real fix: every UnrealScript function call (native, virtual, final)
already funnels through one C++ choke point, `Frame::Call`
(`SurrealEngine/VM/Frame.cpp`). Two hooks were added there —
`Frame::InterceptCall` (pre-call, can fully replace a call) and
`Frame::InterceptCallPost` (post-call, needed once M-C required "let the
real script run, then restore state after" — M-B's pre-call-only seam
couldn't express that). Both are null-by-default `std::function` slots the
VM module doesn't know or care about; `Engine` installs real handlers only
while a VR session is active. Matched by function *name* (`TraceFire`,
`ProjectileFire`, `InvCalcView`/`RenderOverlays`, `CalcDrawOffset`) and
instance (is this the local player's current weapon, or its dual-wield
slave), this lets native code swap `Pawn.ViewRotation` for exactly the
duration of a fire call, and override the viewmodel's transform and fire
origin the same way — without ever touching a stock `.u` package or
writing a line of UnrealScript.

## Verification method used throughout (no headset required)

New debug CLI flags synthesize fake controller state so every milestone
is testable on a dev machine with no HMD connected:

- `--debugvrhands` — two fake, slowly-orbiting hand poses.
- `--debugvrfire` — synthesizes timed fire-button pulses.
- `--debugvrtwohand` — scripts a grab/hold/release sequence for M-D.
- `--debugvrdualenforcer` — force-summons and pairs a second Enforcer for
  M-E1 (via the engine's generic `summon` console command + calling the
  real `UActor::Touch()` collision handler directly, since just teleporting
  an actor's `Location` doesn't trigger stock pickup/pairing logic).

Standard recipe: launch `SurrealEngine.exe --autoplay --vr <debug flags>
--url=DM-Deck16][ "C:\Program Files (x86)\GOG Galaxy\Games\Unreal Tournament GOTY"`
via PowerShell `Start-Process`, let it run, send `WM_CLOSE` via `user32.dll
PostMessage` for a clean shutdown, read
`%LOCALAPPDATA%\SurrealEngine\SE-Log-LastRun.txt`, and/or capture
`PrintWindow`+`PW_RENDERFULLCONTENT` screenshots (needed because the
engine runs exclusive-fullscreen Vulkan, invisible to normal desktop
capture). This machine currently has no HMD connected (Virtual Desktop's
`VirtualDesktopXR` runtime is registered and reachable, but `xrGetSystem`
returns `XR_ERROR_FORM_FACTOR_UNAVAILABLE`) — every milestone's build/log/
screenshot verification was done this way, genuinely no headset needed.

## What is NOT yet verified — the real next step

**None of this has been tried in a real headset.** Everything above proves
the code runs, doesn't crash, and produces the right *data* (correct
rotators, correct transforms, decoupled aim vs. movement) — it does not
prove any of it *feels* right. Needs the user physically present with the
Quest 3 via Virtual Desktop for:

1. Basic sanity: does the viewmodel actually appear at your hand, does
   aiming work, does fire go where you point.
2. M-D tuning: grab/release radii, blend timing, minimum two-hand
   baseline — the design doc explicitly budgets a dedicated tuning session
   for this rather than guessing constants.
3. M-F: does the mirrored left-handed weapon model actually read correctly
   to the eye (mirroring math is confirmed correct via screenshots; "does
   it look right" is a perceptual judgment only a human can make).
4. M-E1: does dual-Enforcer pairing/aim feel right with two hands; does
   the stock master/slave fire-echo timing feel acceptable or annoying now
   that aim is independent per-hand.
5. Per-weapon audit follow-ups (see `PLAN.md`'s log or
   `CONTROLLER_AIM_WEAPON_PLAN.md`'s audit section): Translocator and
   Redeemer are known-incomplete and will very likely aim wrong or do
   something undefined right now — don't be surprised, it's documented.

## Standing process reminders (carried over, still apply)

- **Always re-confirm the user is actually wearing/ready with the headset
  immediately before each real-headset test.**
- UT99 game data (commercial) must never be committed to git.
- Native Vulkan/D3D11 backends on `webxr-m1` must never be touched from
  this worktree, and vice versa.
- No GUI/mouse-click automation for testing — use the debug-flag +
  `PostMessage`/`PrintWindow` mechanism above.
- **Clean-room policy**: any future work needing to understand undocumented
  UT99/Botpack behavior from decompiled source must go through a genuine
  two-agent split (one agent researches and writes a plain-language spec
  with zero code reproduced, a separate agent with no access to the
  decompiled source implements from the spec alone) — a mid-session lapse
  during M-C's per-weapon audit (one agent did both in the same run, no
  code copied but the split wasn't respected) was caught and corrected
  properly for M-E1; keep doing it the corrected way going forward,
  especially for Translocator/Redeemer follow-ups or M-E2 (generic
  dual-wield).
- Git safety protocol: no force-push, no `--no-verify`, new commits not
  amends.
