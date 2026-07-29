# Rune support plan

Date: 2026-07-24

## Scope and dependency

This is a research/planning document only. No branch has been created and no
code has been changed. The proposed topic would be `pr/rune-support`, based on
`pr/game-support-registry` (`2ccdb1d8`) — the same dependency `pr/deus-ex-text`
and `pr/deus-ex-ai-perception` already use — never on `integration/unified-engine`.

## What Rune is (public sources, not yet cross-checked against an owned copy)

Rune (Human Head Studios / Gathering of Developers, Windows Oct 2000, Mac Dec
2000, Linux Jun 2001, PS2 2001) is a third-person Norse-mythology hack-and-slash
built on a licensed and modified Unreal Engine 1. Public sources describe Human
Head Studios and Epic jointly adding "a skeletal animation system, a new
particle effects system, and an enhanced shadowing system" to the licensed
engine, plus a melee/gore system: severed limbs can be picked up and swung,
severed heads can be thrown, and enemies missing a sword arm flee
(en.wikipedia.org/wiki/Rune_(video_game); reproduced with a fresh WebSearch/
WebFetch pass on 2026-07-24, not independently verified against a running copy).
"Rune Classic" (2012, Steam/GOG) bundles the original with the Halls of Valhalla
expansion.

An OldUnreal forum thread (oldunreal.com/phpBB3/viewtopic.php?t=2281) confirms
Human Head published native-modding SDK headers as `RunePubSrc100.zip` and
`runepubsrc107.zip`, but the admin states "there have been no releases of
headers for current 110 version which is sold @ steam and gog" — i.e. no
official headers exist for the exact "Rune Classic 1.10/1.11" build this
project already detects as the primary version. Community patch 1.08a is
described as touching `RuneServerAdmin.u`, `UWindow.u`, and `UBrowser.u`; no
authoritative public list of Rune's own native class/package set (equivalent to
`RuneI.u`) was found in this pass. **Treat "what native functions Rune's own
scripts call" as unknown until measured against a real build** (see the
discovery methodology below) rather than assumed from these secondary sources.

A Rune demo exists on the Internet Archive (`archive.org/details/RuneDemo`,
~86.6 MB). This is the same "preservation mirror, no located redistribution
grant" situation `Docs/UE1DemoAudit.md` and `Docs/UE1_DEMO_HANDOFF.md` document
for the UT99/Deus Ex/Unreal demos already in this project's local-import lane —
it is not itself evidence of a license to import, package, or advertise.
`Docs/GameDataDistributionAndDesktopWeb.md` already lists Rune Classic as
GOG-purchasable with "No third-party redistribution grant located" and
"Link/import only" as policy (line 37 of that doc) — this plan changes nothing
about that finding.

## What already exists in this codebase for Rune

| Behavior | Location | Note |
| --- | --- | --- |
| Hash-based detection of three builds | `SurrealEngine/UE1GameDatabase.h:194-199` (`RUNE_107`/`RUNE_110`/`RUNE_111` SHA-1 entries), enum at `UE1GameDatabase.h:35-37` | "Rune Gold" 1.07 and "Rune Classic" 1.10/1.11 are recognized by exact executable hash. |
| Executable name recognition | `UE1GameDatabase.h:242` (`"Rune.exe"` in `knownUE1ExecutableNames`) | `gameExecutableName` becomes the stem `"Rune"` (`GameFolder.cpp:72`). |
| Version metadata assignment | `SurrealEngine/GameFolder.cpp:321-347` (the three `case KnownUE1Games::RUNE_*` blocks) | All three set `ue1Version = 500` ("unknown version newer than UT but before UE2", per the field comment in `GameFolder.h:8`), i.e. Rune is treated like Undying/Tactical-Ops/Wheel of Time rather than given its own real UE1 build number. |
| `IsRune()` predicate | `SurrealEngine/GameFolder.h:31` (`gameExecutableName == "Rune"`), mirrored in `Package/PackageManager.h:46` | Unlike `IsUnreal1()`/`IsDeusEx()` (`GameFolder.h:22,27`), this does **not** go through `Support().Id()` / the `GameSupport` registry — it is a raw executable-name string check. |
| Old-format INI array parsing skip | `SurrealEngine/Package/PackageManager.cpp:226`: `if (!IsRune() && launchInfo.ue1Version <= 219)` | Added by a prior commit ("PackageManager: Rune uses the old array format in ini files"). **Apparent redundancy**: since Rune's `ue1Version` is 500 (> 219), the second operand is already false for Rune regardless of the `!IsRune()` guard, given the current version-assignment scheme. Either this guard is presently dead code (harmless, short-circuits first) or it is deliberate future-proofing against a later correction of Rune's `ue1Version`. This needs to be understood — not silently "cleaned up" — before touching `ScanPaths()` for Rune. |
| `Handedness` ini property skipped for Rune | `SurrealEngine/UObject/UActor.cpp:4144`: `if (!engine->LaunchInfo.IsRune()) Handedness() = ...` inside `UPlayerPawn::LoadProperties()` | Unconditional for every other UE1 game (even builds ≤219 like KHG); skipped only for Rune. This implies Rune's `PlayerPawn`-derived class either doesn't declare `Handedness` as a `float` config property, or reading it previously threw/misbehaved. Root cause not yet confirmed against real Rune scripts — flag as an open question. |

Grep for `IsRune` across `SurrealEngine/` on 2026-07-24 found exactly these four
call sites (two predicate declarations, two behavioral special cases); there is
no third hidden special case as of this pass.

## Registry gap: Rune has no `GameSupport` entry

`SurrealEngine/GameSupport.h:8-14` defines `GameId` with only `Unknown`,
`Unreal`, `UnrealTournament`, `DeusEx`. `GameSupportRegistry::Find()`
(`GameSupport.cpp:120-141`) matches executable name against exactly `"Unreal"`,
`"UnrealTournament"`, `"DeusEx"`; any other name (including `"Rune"`) falls
through to the `unknown` sentinel (`GameSupport.cpp:126`), whose
`registerNativeFunctions` is `nullptr`. Consequently:

- `IsRune()` bypassing the registry (see table above) is not currently a bug —
  there is no `GameId::Rune` to check against yet — but it is an inconsistency
  with the pattern the registry was built to establish
  (`Docs/UnifiedEngine.md:130-133`: "names engine behavior... rather than
  scattering game-name checks").
- No Rune-specific native class (equivalent to `NDeusExPlayer`,
  `NDeusExDecoration`, etc.) exists anywhere under `SurrealEngine/Native/` or
  `SurrealEngine/GameSupport/`.

### Proposed registry entry

```cpp
// GameSupport.h
enum class GameId
{
    Unknown,
    Unreal,
    UnrealTournament,
    DeusEx,
    Rune            // new
};
```

```cpp
// GameSupport.cpp
namespace
{
    void RegisterRuneNativeFunctions(const GameLaunchInfo&)
    {
        // Deliberately empty at scaffolding time (M0). Populate one native
        // class at a time as gap discovery (M2) identifies concrete
        // "Unknown native function Rune-side-class.FunctionName" failures.
    }
}

const GameSupport& GameSupportRegistry::Find(std::string_view executableName)
{
    ...
    static const GameSupport games[] =
    {
        { GameId::Unreal, "Unreal", 0, RegisterUnrealNativeFunctions },
        { GameId::UnrealTournament, "UnrealTournament", 0, nullptr },
        { GameId::DeusEx, "DeusEx", deusExCapabilities, RegisterDeusExNativeFunctions },
        { GameId::Rune, "Rune", 0, RegisterRuneNativeFunctions }  // new
    };
    ...
}
```

```cpp
// GameFolder.h — align IsRune() with the registry instead of the raw string
bool IsRune() const { return Support().Id() == GameId::Rune; }
```

No `GameCapability` flags are proposed for M1: neither `SaveInfoPackages` nor
`PostPostBeginPlayEvent` has any evidence behind them for Rune yet (both are
Deus-Ex-observed behaviors — `GameSupport.cpp:122-124`, consumed at
`PackageManager.cpp:73` and `Engine.cpp:1358`). If gap discovery (M2) finds
Rune needs an equivalent extra post-`SetInitialState` event or a save/package
scan quirk, add a new named `GameCapability` at that point — do not guess one
in now.

**Build-system note**: any new file under `SurrealEngine/GameSupport/Rune/`
must be registered in both `CMakeLists.txt` (native + test targets, following
the `SurrealEngine/GameSupport/DeusEx/*.cpp` entries) and `Configure.js`
(Emscripten source list), or the flat WebAssembly build will silently omit it.

## Milestones

### M0 — Registry scaffolding, no behavior change
Add `GameId::Rune` and an empty `RegisterRuneNativeFunctions`, align `IsRune()`
to route through `Support().Id()`, add both to `CMakeLists.txt`/`Configure.js`.
Acceptance: existing 34 CTest tests plus the Emscripten link still pass
unchanged (this is a pure additive/refactor change with zero behavior delta
for Unreal/UT/Deus Ex, so regression risk is close to zero, but it must still
be proven, not assumed).

### M1 — Package/asset load verification (requires an owned copy)
Using `--autoplay --url=<startmap> --render=null <RuneRootFolder>` (the same
scriptable pattern already used for UT99/Deus Ex/Unreal demo verification, see
`Docs/UE1_DEMO_HANDOFF.md` and `Docs/FLAT_BROWSER_WASM_AUDIT.md`), confirm the
package manager reaches `ScanPaths()`/`ScanForMaps()`/`RegisterFunctions()`/
`InitPropertyOffsets()` (`PackageManager.cpp:60-77`) without crashing for at
least one real Rune install (Gold 1.07 or Classic 1.10/1.11 — whichever the
user owns). Inspect `%LOCALAPPDATA%\SurrealEngine\SE-Error-LastRun.txt` for the
first fatal error. Confirm/refute the `ScanPaths()` redundancy noted above
using the real INI layout of that build. This milestone produces no new code
beyond whatever `PackageManager`/`UE1GameDatabase` fixes are needed to get past
pure package loading (e.g. an equivalent of the Unreal-205-demo
`Parent`/`DynamicString` serialization fixes in `Docs/UE1_DEMO_HANDOFF.md`, if
Rune's package format turns out to need one).

### M2 — Native-function gap discovery
`Frame::CallNative` throws `"Unknown native function " + ClassName + "." +
FunctionName` whenever `NativeFuncIndex`/`NativeByName` has no registered
handler (`SurrealEngine/VM/Frame.cpp:336-361, 363-389`) — this is the exact
mechanism `Docs/Status.md:5` refers to ("you will therefore see exceptions
shown if you interact with them"). Methodology:
1. Run `--autoplay` through menu construction, then a single-player map, then
   basic movement/attack input, capturing every distinct "Unknown native
   function" string from the log/`SE-Error-LastRun.txt`.
2. Deduplicate by `(ClassName, FunctionName)` into a gap list.
3. Cross-reference class/function names against the public `runepubsrc107.zip`
   headers (1.07) as a naming/signature hint only — treat any behavior beyond
   name/argument-type as unverified for the 1.10/1.11 build actually being
   tested, since no public headers exist for that exact version. Do not carry
   over decompiled implementation logic from any non-public source; behavior
   must come from observed retail behavior and public documentation, not
   copied source, per this project's clean-room posture.
4. Rank the gap list by what blocks menu → map-load → move → attack, deferring
   everything else (see non-goals).

### M3 — Cross-cutting risk gate: skeletal mesh capability
`VisibleMesh::DrawMeshAtLocation` (`Render/VisibleMesh.cpp:57-73`) special-cases
`USkeletalMesh` into `DrawSkeletalMesh`, which is a one-line forward to
`DrawLodMesh` with no bone-matrix skinning (`VisibleMesh.cpp:537-540`) —
confirmed identical to what `Docs/FULLBODY_VR_AVATAR_PLAN.md` already
documents as an engine-wide gap, not something scoped to VR. Public sources
describe Rune as one of the titles that added skeletal animation to licensed
UE1. **Before scoping M4's vertical slice**, determine from real Rune package
data whether the player/monster meshes actually ship as `USkeletalMesh`
(bone-driven) or as ordinary vertex-animated `UMesh`/`ULodMesh` — this
determines whether Rune characters would render as frozen/garbled T-poses
(skeletal, unimplemented skinning) versus merely missing individual gameplay
natives (vertex-animated, already-working path). This is a generic UE1
rendering gap, not a Rune-only fix, and any real fix here needs the
"especially strong evidence" bar `CLAUDE.md` sets for renderer changes — treat
implementing bone skinning as explicitly out of scope for the first Rune
milestone unless M3's investigation shows it is unavoidable for even a static
character to appear.

### M4 — Minimal native surface for a vertical slice
Implement, one at a time, only the native classes/functions M2 shows are
required to reach: boots to a menu; a single-player map loads; the player can
move and perform at least one basic attack; without a fatal VM exception.
Each native implementation follows the `NDeusExPlayer`/`NDeusExDecoration`
pattern (`SurrealEngine/Native/NDeusEx*.{h,cpp}`, registered only through
`RegisterRuneNativeFunctions`) — thin, Rune-scoped, no engine-wide
conditionals. Melee-hit resolution, if it turns out to be native-backed rather
than script-only, is implemented only to the extent needed for "an attack does
not crash"; exact damage/animation fidelity is explicitly deferred.

### M5 — Regression and evidence
Full native + Emscripten build; all existing CTest targets green (34 as of
`Docs/UnifiedEngine.md:153`, more by the time this lands); manual smoke of
UT99 v436 and Unreal Gold v226 (the two builds `Docs/Status.md:22` calls
"relatively playable") to confirm no shared-code regression; document the
exact `--autoplay` command, map name, and observed behavior (per
`Docs/UE1_DEMO_HANDOFF.md`'s "inspect SE-Error-LastRun.txt... capture the game
window" discipline) rather than asserting "Rune works."

## Non-goals for the first milestone (explicit)

- Dismemberment/gore system (limb pickup/throw, decapitation-as-weapon).
- Rune Power abilities, berserk mode, weapon-tier progression fidelity.
- Bot/NPC AI beyond whatever generic UE1 pawn behavior already exists.
- Multiplayer/network (the engine has no networking support at all per
  `Docs/Status.md:51`, unrelated to Rune specifically).
- Full skeletal-mesh bone skinning (see M3) unless M3 proves it's unavoidable
  for a visible character.
- Mac/Linux/PS2 executable variants — Windows `Rune.exe` only, matching the
  hash table.
- Any Rune demo local-import lane (optional future work, same shape as
  `Docs/UE1DemoAudit.md`, but not requested here and not needed for M1-M5).

## Testing plan

**Non-interactive first.** Every milestone above is verified through
`--autoplay --url=<map> --render=null <RuneRootFolder>` plus
`SE-Error-LastRun.txt` inspection, matching this project's existing preference
for scriptable verification over manual mouse-driven testing
(`Docs/UE1_DEMO_HANDOFF.md`, `Docs/MapStartupIntro.md`,
`Docs/FLAT_BROWSER_WASM_AUDIT.md`).

**Requires a human with a legitimate copy.** No commercial Rune package can go
in this repository or any CI fixture (`Docs/GameDataDistributionAndDesktopWeb.md`
decisions 1 and 3). M1 onward is therefore gated on a human — this plan's
author does not know whether the user owns Rune Gold, Rune Classic (Steam/GOG),
or neither; **this must be asked, not assumed** (see Open questions). Any
manual mouse/keyboard playtest of the actual attack loop, gore triggers, or
menu navigation remains a human task; it is not a CTest dependency.

**Regression coverage for other games.** The full native Windows x64 Release
CTest suite (34 tests as of `Docs/UnifiedEngine.md:153`) and the Emscripten
build must both stay green after every commit in this line, exactly as
required for every other topic in `Docs/ParallelImplementationWaves.md`. Any
new pure-logic extraction discovered during native gap-filling (e.g. a
melee-resolution formula that turns out to be a pure function of inputs) should
get a focused unit test in `Tests/`, following the Deus Ex AI-perception test
model — synthetic inputs, no game data required, defensive NaN/finite handling
if applicable.

**Branch discipline.** `pr/rune-support` starts from `pr/game-support-registry`
(`2ccdb1d8`) only. If M3 concludes bone skinning is unavoidable, that work is
its own separate topic (its own dependency chain, likely just current
upstream/`pr/frame-pipeline` since rendering isn't otherwise entangled with the
registry) — do not fold a renderer change into the same topic as native
function registrations, per `CLAUDE.md`'s "one correction or independently
useful improvement" rule.

## Publishing plan (upstream gate applied concretely)

Per `CLAUDE.md`'s seven-point gate and `Docs/UpstreamCoordination.md`, apply
per artifact, not as one "Rune support" bundle:

- **`GameId::Rune` + `RegisterRuneNativeFunctions` scaffolding**: not
  independently upstream-able. `GameSupportRegistry` itself is a fork
  invention not yet offered to or accepted by `dpjudas/master`. A Rune entry
  only makes sense once/if the registry itself is upstream — do not attempt to
  submit the registry entry ahead of its own foundation.
- **Individual new native-function implementations** (M4) are plausible small
  upstream candidates *only if* they fix a generically-described, currently
  broken UE1 behavior that reproduces on stock `dpjudas/master` independent of
  the registry — e.g., if a native function Rune happens to need turns out to
  be a real UE1-wide function other games also call but never exercise. Most
  of what M4 implements will likely be genuinely Rune-only script hooks with
  no upstream-visible bug on other games, in which case they simply stay in
  this fork.
- **Package/INI format fixes** found in M1 (e.g. resolving the `ScanPaths()`
  redundancy) are the most plausible upstream candidates if the underlying
  logic is shown to affect any currently-supported game, not just Rune —
  follow the exact `pr/deus-ex-runtime-fix` precedent: concise reproduction,
  before/after behavior, focused regression, human-curated PR.
- **Skeletal-mesh bone skinning** (if M3 forces it) is exactly the kind of
  "sensitive... renderer change" `CLAUDE.md` singles out as needing especially
  strong evidence; it should not be bundled with Rune-specific work even
  internally, let alone proposed upstream, without its own dedicated
  reproduction and manual test evidence across every game that uses
  `USkeletalMesh`.
- **A "Rune support" mega-PR is explicitly the shape Magnus rejected** per
  `Docs/UpstreamCoordination.md` — never assemble one.
- No Rune game data, headers, or decompiled source may appear in any commit,
  PR body, or doc, per `Docs/GameDataDistributionAndDesktopWeb.md` decision 1
  and the demo-audit precedent.

## Legal / redistribution

No new redistribution question is introduced beyond what
`Docs/GameDataDistributionAndDesktopWeb.md` already records for Rune Classic
(GOG purchase page exists; no third-party redistribution grant found;
link/import only). The Internet Archive `RuneDemo` mirror is the same
"preservation mirror, not a rights grant" situation documented for the other
three demos (`Docs/UE1DemoAudit.md`); if a Rune demo local-import lane is ever
wanted, it must go through the identical per-artifact evidence record before
being added — out of scope for this plan.

## Confirmed 2026-07-24: real install available

The user owns Rune Classic via GOG, installed at
`C:\Program Files (x86)\GOG Galaxy\Games\Rune Classic\` — the same GOG Galaxy
games folder already used for the UT99 GOTY, Unreal Gold, and Deus Ex GOTY
installs this project already tests against. `System\Rune.exe` and
`System\RuneI.u` (plus `RuneI101.u`/`RuneI107.u` version-specific copies,
`RuneServerAdmin.u`) are present, confirming `RuneI` (not a differently-named
package) is the game-specific native/gameplay module referenced in the "what
Rune is" section above. This resolves open question 1 below — M1 can proceed
using this path as `<RuneRootFolder>`.

## Progress log

**2026-07-24, M0 done.** `GameId::Rune` added to the registry, `RegisterRuneNativeFunctions` (empty), `IsRune()` now routes through `Support().Id()`. Commit `9605ba9e` on `pr/rune-support` (isolated worktree, branched off `pr/game-support-registry` `2ccdb1d8`). Note: this base commit predates the `Tests/` CTest suite entirely (`CMakeLists.txt` has no `add_subdirectory(Tests)` at this point in history) — the 34-test regression claim in this plan's M0/M5 sections only applies once this work is combined onto `integration/unified-engine`; it does not exist as a check on this topic branch in isolation.

Also added (commit `392cd79d`, separate from the registry change): a minimal `--autoplay` launch flag (`GameApp.cpp`, ported from the sibling `ut99-vr` fork's already-proven implementation — this branch predates it and the launcher GUI otherwise blocks all scripted verification) and a `--logfile=<path>` override (`Engine.cpp`'s destructor). The `--logfile` addition turned out to be necessary, not optional: the default log path (`%LOCALAPPDATA%\SurrealEngine\SE-Log-LastRun.txt`) is shared machine-wide across every SurrealEngine build, and was actively being overwritten mid-verification by an unrelated concurrent session's own QA harness. Any future verification of this or any other topic on this machine should use `--logfile` to an isolated path rather than trusting the shared default.

**2026-07-24, M1 done, M2 started.** Ran `--autoplay --logfile=<isolated path> "C:\Program Files (x86)\GOG Galaxy\Games\Rune Classic"` (confirmed hash `4a517c7f96a27cf7e25534c80d50af8db4065276` = `RUNE_110`). Result: correct game detection ("Game: Rune Classic (Version: 1.10)"), real Vulkan device init (RTX 5090), package/map loading proceeds through `GameInfo.InitGame`/`GameInfo.Login`, and the real player class (`Runei.Ragnar`) spawns with no fatal "Failed to spawn the player actor" error and a clean engine shutdown. This resolves M1: package/asset loading does not crash for the default `Entry`/`RuneIntro` boot path.

First concrete M2 native-function gap, found in that same run: `RunePlayer.PostBeginPlay` throws `Unknown native function Actor.JointNamed`, from script call stack `sarkeye.PostBeginPlay` (`AttachActorToJoint(Flame, JointNamed(R_Eye))`) ← `RunePlayer.PostBeginPlay` line 196 (`BloodLustEyes = Spawn(...)`) ← `GameInfo.Login`. This is Rune's "Sark Eyes" blood-lust visual effect attaching a flame decal to a named skeletal joint on the player mesh — directly relevant to M3's skeletal-mesh question, since `JointNamed` presupposes some notion of a named joint/socket even if full bone-skinning isn't implemented.

Second finding, booting directly into a real gameplay level instead of the `Entry` cinematic (`--url=Dwarf1wwheel`, the first non-cutscene map in `Maps/`): package loading logs a very large number of `Skipping unknown property 2DLoftSIDE` and `Skipping unknown property Particle` warnings (property names this engine's `UClass`/`UProperty` system doesn't recognize on whatever actor classes this map uses — likely fog-sheet/particle-emitter decorations). Three runs at increasing timeouts (30s, 90s, 180s) were all killed without ever reaching `GameInfo.Login` for this map, all producing the exact same 589-line log, byte-for-byte — this looked like a genuine stall with no further engine milestone reached, no exception, no crash dialog.

**2026-07-24, stall root-caused and fixed.** It was never a hang. `GameApp::main`'s catch block calls `ErrorWindow::ExecModal(...)`, a *modal* dialog, on any unhandled `std::exception` — and under `--autoplay` there is nobody to dismiss it, so a legitimate, quickly-thrown exception presented identically to an indefinite hang (flat CPU usage confirmed via repeated `Get-Process` sampling, ruling out a spin loop). Fixed by making `--autoplay` runs print the exception to stderr instead of blocking on a dialog (`GameApp.cpp`, and the same fix in `MainGame.cpp`'s `wWinMain` catch for exceptions thrown outside `GameApp::main`). Note the fix must capture `--autoplay` into a plain `bool` *before* the `try` block: reading it back through the global `commandline` pointer from inside the `catch` block is a dangling-pointer read, since `CommandLine cmd` is a stack local inside the `try` block and gets destructed during unwind — this cost real debugging time before being caught.

With the dialog removed, the real exception was: `Name index out of bounds!: Dwarf1wwheel index=49668 tableSize=4191`, thrown from `Package::GetName`. Instrumented `PropertyDataBlock::Load` (`UObject.cpp`) to log every property's name/type/size and stream position; the last object loaded before the throw was `ClimbableChain4` (class `ClimbableChain`, a Rune-specific climbing actor). Its `ParticleArray` property (`UPT_Struct`, declared `size=123`) only consumed 87 bytes in `UStructProperty::LoadValue`/`LoadStructMemberValue` — a 36-byte under-read. `LoadStructMemberValue` blindly iterates whatever fields this engine's loaded `UStruct` definition has for that struct name and trusts that list matches the serialized data with no independent length check; when Rune's actual data has fields this engine's struct definition doesn't know about, every property read after that object desyncs, eventually decoding stream garbage as a wildly out-of-range name index.

Fix (`UObject.cpp`, `PropertyDataBlock::Load`): after `prop->LoadValue(...)` returns for any non-bool property, compare the stream position against the declared header size (`valueStart + header.size`) and `Seek()` back to the expected end if they don't match, logging a warning. The header's size prefix is supposed to be self-describing precisely so a parser that doesn't fully understand a property's contents can still skip it safely — this makes that invariant hold universally instead of only for top-level "unknown property" skips. Committed as `a050b6da` on `pr/rune-support`.

Verified: `--autoplay --url=Dwarf1wwheel` now logs repeated (expected, harmless) `ParticleArray` resync warnings, then proceeds all the way through `GameInfo.InitGame` (`Base Mutator is Dwarf1wwheel.Mutator1`) — far past the previous stall point — before hitting the next real gap: `Unknown native function Actor.AttachActorToJoint`, thrown from `Runes.PreBeginPlay` line 13 (`AttachActorToJoint(spheres, 1)`). This is Rune's second concrete M2 native-function gap (alongside `Actor.JointNamed` found earlier), both squarely in the skeletal-joint-attachment area relevant to M3.

**2026-07-24, M2 gap catalogue completed.** Each native-function gap aborts the run at first occurrence, which only surfaces one gap at a time. To catalogue the full set in one pass, temporarily changed `Frame::CallNative`'s two "unknown native function" `Exception::Throw` call sites (`VM/Frame.cpp`) to log and return `ExpressionValue::NothingValue()` instead — a local, uncommitted, throwaway change made purely for this investigation and reverted immediately afterward (`git checkout -- SurrealEngine/VM/Frame.cpp`; confirmed `git status`/`git diff` clean before rebuilding again). This is **not** a proposed engine behavior change; unknown natives should keep throwing in the real fix, since silently no-oping them would hide real gaps rather than reveal them. It was a one-off diagnostic tool only.

With that diagnostic build, ran `--autoplay --url=Dwarf1wwheel` for 28 seconds (ended with a graceful window close via `taskkill` without `/F`, which the engine handles as a normal quit — `Shutting down... / Saving configurations... / Closing window...` — so the log flushed normally instead of being lost to a forced kill). Result: the level fully loads, `GameInfo.AcceptInventory` runs, `Actor.SLog: AutoSave Checkpoint at Level Start` fires, and the in-game HUD/console/menu system (`RuneHUD`, `RuneConsole`, `RuneMenu`, `UWindowRootWindow`) comes up — i.e. this is a live, stable, playable level once the property-resync fix from above is in place, gaps notwithstanding. No crashes, no further stream corruption, no new stalls in 28 seconds of real (non-cutscene) gameplay initialization.

Six distinct unknown native functions were logged, all on `Actor`:

| Native function | Observed call sites |
|---|---|
| `Actor.AttachActorToJoint` | `Actor.Spawn`, `FireObject.PostBeginPlay`, `RuneI.RuneOfPowerRefill`, `RuneI.RuneOfStrength`, `Torch.PostBeginPlay`, `Runes.PreBeginPlay` |
| `Actor.JointNamed` | `Actor.Spawn`, `FireObject.PostBeginPlay`, `Torch.PostBeginPlay` (plus the earlier `RunePlayer.PostBeginPlay`/`sarkeye` finding) |
| `Actor.TraceTexture` | `RuneI.BabyCrab`, `RuneI.Lizard` (monster AI — likely line-of-sight/target texture tracing) |
| `Actor.GetJointPos` | `Torch.PostBeginPlay` |
| `Actor.ResetAnimationCache` | `RunePlayer.ClientReStart` |
| `Actor.SetDefaultPolygroups` | `RuneMenu.Paint` |

Three of these (`AttachActorToJoint`, `JointNamed`, `GetJointPos`) are all named-skeletal-joint operations and are by far the most frequently hit — decorative fire/torch effects (`FireObject`, `Torch`, the two Rune-of-* pickups) attach flame/glow effects to named joints on their meshes, in addition to the player's own "Sark Eyes" effect found earlier. This is the concrete evidence M3 needs: Rune's content leans on named-joint queries pervasively, not just for one cosmetic effect, so M3's skeletal-mesh risk decision should treat "can we resolve a named joint to *some* usable transform" as a near-blocking requirement for a convincing vertical slice, even if full bone-skinning is out of scope. `TraceTexture`, `ResetAnimationCache`, and `SetDefaultPolygroups` are unrelated to joints (visibility/texture tracing for AI, animation-cache invalidation, and a menu polygon-group cosmetic) and are lower priority for M3 specifically, though still required for M4's native surface.

Also observed, non-blocking and lower priority than the native-function gaps: `Unknown command: LANGUAGE` and `Unknown command: ISADDON` (console commands, not native functions), and repeated `Object.DynamicLoadObject: could not load 'UWindow.Icons.RCBack0'` through `RCBack8` (missing pause-menu icon assets — likely a package/path naming difference for this UI icon set under Rune specifically, cosmetic only, not investigated further).

This completes M2's gap-cataloguing goal for the `Dwarf1wwheel` level. Moving to M3 with this catalogue as the input for the skeletal-mesh risk decision.

**2026-07-24, M3 resolved: Rune's skeletal system is not `USkeletalMesh` at all.** Runtime instrumentation (forcing early package/export enumeration to check whether any object's class is `Mesh`/`LodMesh`/`SkeletalMesh`) proved unreliable and was abandoned after repeated dead ends: `Package::NewObject`/`GetAllObjects`-based forced loading triggered unrelated native-index/class-resolution failures from forcing packages to fully load out of their normal lazy, dependency-ordered sequence (this is an artifact of the forcing technique itself, not a real engine bug — not investigated further, and all of this instrumentation was reverted, confirmed via `git status --short` showing a clean worktree before rebuilding).

The real, conclusive answer came from a completely different, zero-risk method: reading the raw package bytes directly. `strings -a System/Engine.u | grep -i mesh` (no engine code involved at all) shows Rune's `Engine.u` script source text embedded in the compiled package, including:

```
DT_SkeletalMesh skel="$Skeletal@"SkelMesh="$SkelMesh;
Skeletal, SkelMesh, SkelGroupSkins, SkelGroupFlags, JointChild, bHasShadow,
```

and the full `EDrawType` enum order (`strings -a Engine.u | grep -n "^DT_\|EDrawType"`):
`DT_None, DT_Sprite, DT_Mesh, DT_Brush, DT_RopeSprite, DT_VerticalSprite, DT_SkeletalMesh, DT_SpriteAnimOnce, DT_ParticleSystem`.

Compare this engine's hardcoded C++ enum (`SurrealEngine/UObject/UActor.h:148-158`):
`DT_None, DT_Sprite, DT_Mesh, DT_Brush, DT_RopeSprite, DT_VerticalSprite, DT_Terraform, DT_SpriteAnimOnce` (8 values, stock UE1/UT order).

Rune's fork replaced index 6 — stock UE1's `DT_Terraform` — with its own `DT_SkeletalMesh`, and appended a 9th value, `DT_ParticleSystem`, that this engine's enum doesn't have at all. `DrawType` is stored as a raw byte and read by `VisibleActor::Process` (`Render/VisibleActor.cpp:55`) via a direct cast — `EDrawType dt = (EDrawType)actor->DrawType();` — with no reflection against the actual UEnum values loaded from the package. The consequence: **a Rune actor using `DT_SkeletalMesh` (byte value 6) is misread by this engine as `DT_Terraform`, which `VisibleActor::Process`'s if-chain doesn't handle at all — the actor is silently dropped and never added to `frame->Actors`.** Same fate for `DT_ParticleSystem` (byte 8, out of this engine's enum range entirely). This is not a T-pose/garbled-mesh risk as the plan originally framed it — it's a **fully invisible actor** risk, and it is definitively confirmed from Rune's own shipped script data, not inferred.

Further, the skeletal data itself isn't carried on the stock `Mesh` property at all: `SkelMesh` is a separate property, and `Skeletal` (used as `Other.Skeletal != None` / `A.Skeletal` in Engine.u) is an **object reference to a companion actor**, not a boolean flag as first guessed — i.e. Rune's skeletal rendering appears to hand off to a distinct associated actor carrying `SkelMesh`/`SkelGroupSkins`/`SkelGroupFlags`/`JointChild`, none of which exist in this engine's `PropOffsets_Actor` today. This is a materially different, and more involved, mechanism than "cast `Mesh` to `USkeletalMesh`" — confirming the plan's original caution that full bone-driven skinning is a real, separate feature (joint hierarchy via `JointChild`, per-group skins via `SkelGroupSkins`/`SkelGroupFlags`, a companion-actor indirection), not a small cast-and-render fix, and should stay out of scope for the first Rune milestone exactly as this plan's M3 section anticipated.

**M3 conclusion for M4 scoping:** implementing real bone-driven skeletal skinning stays out of scope (confirmed non-trivial, and still subject to `CLAUDE.md`'s "especially strong evidence" bar for renderer changes, this time with concrete justification rather than a guess). But there is a smaller, Rune-scoped, additive option worth weighing for M4: recognizing `DrawType == 6` as Rune's `DT_SkeletalMesh` (only when `engine->LaunchInfo.IsRune()`, so stock UE1's `DT_Terraform` at the same numeric value is untouched for every other game) and rendering whatever `SkelMesh` resolves to through the existing non-skinned `DrawLodMesh`/`DrawMesh` path — i.e. a static/vertex-animated fallback rather than true joint skinning. That would turn "invisible" into "visible but not bone-animated" for skeletal actors, which may or may not be worth the added property-offset surface (`SkelMesh`, and whatever `Skeletal`'s companion-actor indirection turns out to require) for a first vertical slice; this is an M4 scoping decision, not one this milestone needs to make.

**2026-07-24, M4 done: all six M2-catalogued native gaps implemented.** Rather than guessing native function indices, extended the level-start diagnostic technique from M3 with the engine's own existing `NativeFuncExtractor` commandlet logic (`Commandlet/Native/NativeFuncExtractor.cpp`, normally driven through the separate interactive `native extractfuncs` debugger command): a temporary hook in `Engine::Engine` walked `enginepkg->GetClass("Actor")->Children` looking for the six named `UFunction`s and logged each one's real `NativeFuncIndex` and parameter list straight from Rune's compiled `Engine.u` class metadata — no lazy content-object instantiation involved, so none of M3's forced-load pitfalls applied. This is exactly the same kind of "read the package's own truth instead of guessing" pivot that resolved M3. Confirmed indices: `AttachActorToJoint`=613, `JointNamed`=619, `TraceTexture`=666, `GetJointPos`=602, `ResetAnimationCache`=620, `SetDefaultPolygroups`=610 — all on `Actor`, all take/return plain `int`/`NameString`/`vector`/`UObject*` types (no engine struct extensions needed). None of these indices collide with any other currently-registered native in this engine at runtime: dispatch is purely by numeric index once a compiled `UFunction` carries a nonzero `NativeFuncIndex` (`VM/Frame.cpp:320-322`, `VM/NativeFunc.cpp`'s `RegisterHandler`/`RegisterNativeFunc`), and the one name that already exists elsewhere (`Actor.TraceTexture`, a real UT/Unreal Gold native registered at index 1000 in `Native/NActor.cpp:105`) is registered at a different index with completely different arguments — a coincidental name reuse across two unrelated engine forks, not a conflict. The diagnostic hook itself was reverted immediately after use (confirmed via `git status --short` showing `Engine.cpp` clean before the real implementation build).

Implemented all six in a new `Native/NRuneActor.{h,cpp}` (registered via `NRuneActor::RegisterFunctions()` from `RegisterRuneNativeFunctions` in `GameSupport.cpp`, matching the `NDeusExPlayer`/`NDeusExDecoration` pattern), added to `CMakeLists.txt` and `Configure.js`. Per M3's finding that Rune's named-joint/skeletal-attachment mechanism (`Skeletal`/`SkelMesh`/`JointChild`) isn't modeled by this engine at all, these are deliberately coarse, honestly-documented approximations rather than a pretense of full fidelity — consistent with the plan's "an attack does not crash" bar, not full joint fidelity:

- `AttachActorToJoint(A, j)`: bases `A` directly on `Self` (`UActor::SetBase`) — follows Self's overall position, not a named joint offset (no joint transform table exists to place it precisely).
- `JointNamed(name)`: always returns `-1` (a "no such joint" sentinel that's also safe to feed straight into `AttachActorToJoint` above, which ignores the joint index anyway).
- `TraceTexture(TraceEnd, TraceStart, Flags, ScrollDir, ReturnValue)`: runs a real trace via the existing `UActor::Trace` for `ReturnValue` (useful, real line-of-sight data for the `BabyCrab`/`Lizard` AI callers) but reports `Flags=0`/`ScrollDir=(0,0,0)` — this engine's textures don't track moving-surface scroll data, so those two outputs are honestly "no scroll" rather than fabricated.
- `GetJointPos(joint)`: returns `Self`'s own `Location()` as a fallback (no per-joint transform exists to return instead).
- `ResetAnimationCache(seq)` and `SetDefaultPolygroups()`: no-ops (an animation-cache invalidation and a menu-cosmetic polygon-group setup call, neither of which this engine needs to do anything with).

Verified with `--autoplay --url=Dwarf1wwheel`: previously this run threw `Unknown native function` and stopped within seconds of `GameInfo.Login`; with these six natives registered, the same run produced **zero** `Unknown native function` and **zero** `Script error` lines across a full 59.6-second run (more than double the prior ~28s ceiling, which had been imposed by the diagnostic build's own instrumentation overhead, not a real limit) — through `GameInfo.InitGame`/`Login`/`AcceptInventory`, the level-start autosave checkpoint, and the `RuneHUD`/`RuneConsole`/`RuneMenu` UI coming up, ending only on a deliberate graceful shutdown. The only remaining log lines are the already-catalogued, lower-priority cosmetic gaps from M2 (`Unknown command: LANGUAGE`/`ISADDON`, missing `UWindow.Icons.RCBack0`-`8` assets) — nothing new surfaced. This satisfies M4's automatable bar ("boots to a menu; a single-player map loads;... without a fatal VM exception"); the "player can move and perform at least one basic attack" fidelity check is explicitly a human playtest task per this plan's own Testing plan section (`--autoplay` has no input-simulation capability, by design, per `GameApp.cpp`), not something this milestone can self-verify.

No `Tests/` CTest suite exists on this topic branch to regress (unchanged from the M0 note above — this branch predates that suite; M5's regression pass is the point where that applies, once combined onto `integration/unified-engine`).

**2026-07-24, worktree and doc relocated into the new workspace structure.** Per `SURREAL_WORKSPACE_STRUCTURE_PLAN_2026-07-24.md` (QuestGames root), this topic's worktree was a clean, unintegrated `surreal-pr-*` tree and has been moved via `git worktree move` from `C:\Devstuff\QuestGames\surreal-pr-rune-support` to `C:\Devstuff\QuestGames\SurrealEngine\repos\worktrees\review\rune-support`. This plan document itself had never actually been committed anywhere — it had been sitting as an untracked file in `C:\Devstuff\QuestGames\surreal-unified`, a separate, unrelated worktree that was (and remains) checked out to `feature/fullbody-vr-avatar` with ~19 unrelated dirty files; every prior progress-log entry above was written to that loose file, not to git history. It is committed here, at `Docs/RUNE_SUPPORT_PLAN.md` inside the `pr/rune-support` worktree itself, so it now has real history and travels with its own topic branch rather than depending on an unrelated worktree remaining in its current state.

**2026-07-25, M5 scoped to standalone (integration deferred).** A disposable `git worktree add --detach` + `git merge --no-ff --no-commit pr/rune-support` test against `integration/unified-engine`'s tip (282 commits ahead; `pr/game-support-registry`'s history was never linearly merged there, though an equivalent registry pattern was independently reimplemented) surfaced 6 conflicting files. Five are trivial add/add conflicts (unrelated features landing at the same lines). The sixth, `GameApp.cpp`, has a real overlap: integration already built its own more advanced `--autoplay` plus a "don't block on a modal error dialog" fix — but scoped only to `#ifdef __EMSCRIPTEN__`, so a native-build `--autoplay` run hitting an unhandled exception there would still call the blocking `ErrorWindow::ExecModal(...)`, the same class of bug this plan's earlier "stall root-caused and fixed" entry addressed for this branch specifically. Worth a focused look independent of Rune. The test-merge was aborted and the disposable worktree removed without touching any real branch. Given the divergence and the judgment call needed in `GameApp.cpp`, M5 was explicitly scoped to standalone verification of `pr/rune-support` only, deferring any real integration merge.

**2026-07-25, added a non-interactive `--exec=<consolecommand>` launch flag.** While regression-testing, manual interactive testing (a human, not `--autoplay`) surfaced that Rune's menu is stuck: choosing single or multiplayer prompts "Rune must exit Classic Mode to... proceed?", and clicking through it does nothing. Reproducing and fixing this without driving the GUI needed a way to issue a console command non-interactively. `--exec` runs one `Engine::ConsoleCommand` right after `LoginPlayer()`, before the main loop starts, so any queued `ClientTravel` is picked up on the first iteration — this reuses `Engine.cpp`'s existing `ConsoleCommand`/`ClientTravel` machinery rather than adding a new mechanism, and only activates when explicitly passed. `SurrealDebugger.exe`'s existing "native extractfuncs" commandlet was tried first for unrelated signature lookups below and confirmed impractical to drive non-interactively (`GetInput()`/`WaitForInput()` reads real GUI window input events, not stdin — piping via a shell produced no output at all).

**2026-07-25, root-caused and fixed the "Classic Mode" menu stall.** With `--exec="RELAUNCH rune://intro.run?video=." --logfile=<isolated path>`, the log showed `RuneMenu.MessageBoxDone: Unknown command: RELAUNCH rune://intro.run?video=.` — Rune's own `RMenu.u` issues a custom `RELAUNCH rune://<map>?<options>` console command to leave Classic Mode, which this engine didn't recognize (logged and silently dropped). `Maps/intro.run` is a real Rune map (matches `Rune.ini`'s `LocalMap=intro.run`); `video=.` mirrors the ini's `Video=intro.ogv` key set empty to skip the intro cinematic — this is not a process-level relaunch, just Rune's own wrapper around a plain map travel. Fixed in `Engine::ConsoleCommand` (`Engine.cpp`) with a `relaunch` handler gated to `LaunchInfo.IsRune()` (same precedent as the existing Klingon Honor Guard/Deus Ex one-off command hacks already in that function): strips the `rune://` scheme prefix, then — unlike `open`/`start`'s bare-map-name convention — also strips the literal map file extension Rune's URL embeds (`intro.run` → `intro`) before matching against `packages->GetMaps()`'s stemmed names, then travels exactly as `open`/`start` already do (`ClientTravel(...)`). Confirmed fixed: the "Unknown command" line is gone and the log shows `Client travel to intro?video=....`

**2026-07-25, that fix surfaced a second, previously-uncatalogued native gap: `Pawn.SkeletonLook`.** Once the intro map actually loads, it throws immediately: `Unknown native function Pawn.SkeletonLook`, called every tick from `RunePlayer.Tick`. This blocks the real "menu → single player" path outright, distinct from the six `Actor`-scoped natives M4 already implemented — none of those six were reachable from this map via `--autoplay --url=Dwarf1wwheel` alone, since that bypasses the menu/Classic-Mode-exit path entirely. Extracted the real signature and index the same M3/M4 way ("read the package's own truth instead of guessing"), but via a narrower, temporary hook this time: forcing `NativeFuncExtractor`'s full `GetAllObjects<UClass>()` sweep across every package (as M4's diagnostic did) threw an unrelated `Could not find the object class for MongolArm` from eagerly resolving classes outside their normal lazy load order — the same class of forced-load side effect M3's notes already flagged. Instead, temporarily made `NativeFuncExtractor::CreateClassJson` public and called it directly on just `packages->FindClass("Engine.Pawn")` (`FindClass` needs a package-qualified name), avoiding the wider sweep entirely. Confirmed: `SkeletonLook`, index `670`, signature `(float DeltaTime)`, no return. Implemented as a no-op in `NRuneActor` (per-tick head/bone look-at with no joint table to rotate, consistent with `GetJointPos`/`AttachActorToJoint`'s precedent) — the diagnostic hook and the `NativeFuncExtractor.h` visibility change were both reverted immediately after (confirmed via `git diff`/`git status --short` showing only the intended four files changed before the final build).

**2026-07-25, M5 regression pass complete.** All runs via `--autoplay --render=null --logfile=<isolated path>`, each ended with a graceful `taskkill` (without `/F`) rather than a hard kill so the log flushes normally (a raw `timeout <n>` SIGTERM does not reach the shutdown/log-flush path on this build):
- Rune, `--url=Dwarf1wwheel` (M4's original target): 113.7s, zero `Unhandled Exception`/`Unknown native function`/`Script error` lines — unaffected by this milestone's changes.
- Rune, `--exec="RELAUNCH rune://intro.run?video=."` (menu → leave Classic Mode → intro map): no crash; progresses through several real in-map level transitions (the intro sequence self-advances through multiple `Client travel to intro?...` steps over ~12 simulated seconds) and exits gracefully on its own once the sequence ends.
- Unreal Tournament GOTY, `--url=DM-Deck16][`: 101.7s, zero errors — confirms the Rune-gated `relaunch` handler and the generic `--exec` addition don't regress a non-Rune game.
- Unreal Gold 226b, default Entry map: 76.5s, zero errors — same confirmation for a second, older engine version.
No `Tests/` CTest suite exists on this topic branch (per the M0/M4 notes above); a real disposable-worktree test-merge against `integration/unified-engine` showed 282 commits of divergence and one non-trivial `GameApp.cpp` overlap needing a real judgment call (see the M5-scoping entry above) — actually merging was deliberately deferred rather than attempted unprompted, so the 34-test CTest suite and Emscripten build referenced in this plan's M5/Testing-plan sections remain unexercised by this pass; that regression coverage applies once this branch is actually combined onto `integration/unified-engine`, not before. The "player can move and perform at least one basic attack" fidelity check remains a human playtest task, unchanged from M4's note, and is not something `--autoplay`/`--exec` can self-verify.

**2026-07-26, root-caused (but only partially fixed) the "Classic Mode" menu never going away.** Live human testing after the `relaunch` fix above showed the underlying `Client travel to intro?...` succeeding in the log every time, but visually the screen never changes — clicking "Yes" *or* "No" on the confirm dialog produces the identical result (still the same menu), which rules out anything travel-related and points at rendering. Found the gate: `Render/RenderSubsystem.cpp:41` skips `DrawScene()`/`RenderOverlays()` (the entire 3D world) whenever `engine->console->bNoDrawWorld() == true`, for any `ue1Version > 219` (Rune is 500). Confirmed via `grep` that `bNoDrawWorld` (`UObject/UClient.h:115`) is a script-write-only bitfield, and that Rune's own `Engine.u`/`Engine101.u`/`Engine107.u`/`UWindow.u` reference it — the shared `UWindow` UI framework sets it true when its menu opens. In the original retail engine, "leaving Classic Mode" meant restarting the whole `Rune.exe` process, which implicitly resets this flag to its default and never recreates the menu at all — exactly why `--url=<map>` runs (below) never show a menu in the first place. Added a fix: `Engine::ConsoleCommand`'s `relaunch` handler (`Engine.cpp`) now also clears `console->bNoDrawWorld() = false` right before its existing `ClientTravel(...)` call. **Confirmed insufficient on its own** by a further live test: no visible change even with this clear in place. Root cause of *that*: `Render/RenderCanvas.cpp`'s `PreRender()`/`PostRender()` both call `CallEvent(engine->console, EventName::PreRender/PostRender, ...)` *unconditionally*, outside the `bNoDrawWorld` gate — this is almost certainly where `RuneMenu`'s own `Canvas` draw logic runs, so the menu keeps painting itself on top of the world regardless of `bNoDrawWorld`. Actually dismissing the menu likely requires telling `RuneMenu`'s `UWindowRootWindow` to close/destroy itself (a script-side state change this native fix doesn't attempt), not just unblocking the world render. **This remains an open, unresolved gap** — the `bNoDrawWorld` clear is committed as a real, defensible partial fix (correct given the evidence) but does not by itself make the in-menu "leave Classic Mode" path visually work.

**2026-07-26, found the practical workaround: direct-to-level launch bypasses the broken menu path entirely.** `Engine::Run()` (`Engine.cpp:122-128`) always calls `LoadEntryMap()` first, but if `LaunchInfo.url` (the existing `--url=<mapname>` flag) is non-empty, it immediately calls `LoadMap()` again with the target map *before any frame ever renders* — so `RuneMenu` is technically constructed but never gets a chance to paint or block anything. Combined with `--autoplay` (skips SurrealEngine's own launcher/folder-picker dialog), `--autoplay --url=Dwarf1wwheel <folder>` boots straight into real, playable gameplay with zero menu interaction of any kind. This is now the recommended way to get a genuinely playable Rune session while the Classic-Mode-menu-overlay bug above remains open — confirmed working end-to-end by live human testing (WASD/mouse interaction, not just log inspection).

**2026-07-26, second native gap found via a second copy of the game: `Actor.SetJointRot`.** The user separately installed a second, GOG-purchased copy — Rune Gold (`C:\GOG Games\Rune Gold`, distinct from the earlier Rune Classic copy) — as an alternate test target. Direct-to-level launch (`--autoplay --url=Dwarf1wwheel`) against this copy crashed almost immediately (~0.36s in): `Unknown native function Actor.SetJointRot`, called every tick via `Pawn.Jaw` → `Pawn.Tick` → `ScriptPawn.Tick` (jaw/mouth-joint rotation for talking NPCs) — apparently not reachable in the earlier M5 regression run against Rune Classic's copy of the same map (NPC dialogue is presumably conditional/timing-dependent, so the same nominal map+URL didn't happen to trigger it there). Extracted the real signature/index the established way: temporarily made `NativeFuncExtractor::CreateClassJson` public, added a throwaway `--dumpclass=<Package.Class>` flag to `GameApp.cpp`, ran it against `packages->FindClass("Engine.Actor")` only (not the full sweep, avoiding M3/M4's already-documented forced-eager-load pitfall), then reverted both temporary changes immediately after (confirmed via `git status --short` showing only the intended `NRuneActor.h`/`.cpp` files changed). Confirmed: `SetJointRot(int joint, const Rotator& Rot)`, index `621`, no return. Implemented as a no-op in `NRuneActor`, consistent with `GetJointPos`/`SkeletonLook`'s established precedent (no joint offset table exists to rotate anything against). Verified fixed: the same `--autoplay --url=Dwarf1wwheel` run against Rune Gold now goes a full 50s with zero `Unhandled Exception`/`Unknown native function` lines (previously died at 0.36s), through `GameInfo.Login`/`AcceptInventory` and into `RuneMenu.Created` territory with no crash.

**2026-07-26, Rune Gold vs. Rune Classic (GOG) data note.** Rune Gold's own package data logs ~2,200 repeated `Property ParticleArray did not consume its declared size (expected 123 bytes) - resyncing stream position` warnings during load — a property-layout mismatch for some class with a `ParticleArray` property, present in this build's data but not observed against the Rune Classic (GOG) copy. Cosmetic/noisy only — the engine's existing stream-resync recovery handles it and it doesn't block anything — not investigated further since it isn't fatal. With `Actor.SetJointRot` fixed, live human testing confirmed Rune Gold plays noticeably better than the Rune Classic copy tested earlier in this session ("works a lot better"), specifically via the direct-to-level launch path (menu-based play still blocked by the unresolved Classic-Mode-overlay gap above, equally for both copies).

**2026-07-26, session paused here at the user's request (moving to another project).** Uncommitted on top of commit `50f1d1e2`: the `console->bNoDrawWorld() = false` clear in `Engine.cpp`'s `relaunch` handler, and the new `Actor.SetJointRot` no-op stub in `NRuneActor.h`/`.cpp` (registered index `621`). Both are real, defensible fixes on their own merits (confirmed via non-interactive `--autoplay`/`--exec` testing) and neither has been committed yet. **Status if resuming:** Rune is fully playable today via direct-to-level launch (`--autoplay --url=<mapname> <game folder>`) against either copy; the in-game main menu's "leave Classic Mode" flow is not — the menu overlay itself needs to be actively closed/destroyed (likely a `RuneMenu`/`UWindowRootWindow` state change), not just unblocked at the render-gate level, and that's the next concrete thing to chase.

**2026-07-27, Classic-Mode menu teardown fixed and the Gold intro path extended.** Rune's shipped `UWindow.u` contains the authoritative in-process teardown that the earlier native approximation was missing: `WindowConsole.CloseUWindow()` hides `Root`, clears `bUWindowActive`/`bQuickKeyEnable`, releases the Windows mouse, unpauses the player, leaves the `UWindow` state, resumes precaching, and clears `bNoDrawWorld`. The Rune-only `RELAUNCH` handler now calls that script function when the console's `Root` property exists, then retains the direct `bNoDrawWorld=false` assignment as a defensive fallback for a custom console or the pre-first-render `--exec` path where no root exists yet. A temporary delayed diagnostic (removed before the final build) opened the real Rune UWindow after root creation and measured `root=set visible=true active=true mouse=true` before `RELAUNCH`, then `root=set visible=false active=false mouse=false` immediately after it; the queued map travel completed normally. This resolves the overlay's actual script/input/render state instead of suppressing console render callbacks globally.

Testing the same exact intro relaunch against Rune Gold 1.07 exposed two additional blockers not reached by the prior direct-to-level smoke. First, the package scanner compared configured extensions case-sensitively, so `Paths=..\\Textures\\*.utx` skipped the retail file `RUNESTONES.UTX`; `ScanFolder` now compares extensions case-insensitively, matching UE1's package-name semantics and allowing the Gold intro to load. Second, the now-running intro reached `Actor.DetachActorFromJoint`. A targeted, temporary `NativeFuncExtractor::CreateClassJson(packages->FindClass("Engine.Actor"))` hook (also reverted before the final build) read the 1.07 package's own metadata: index `614`, extractor arguments `(int j, UObject*& ReturnValue)` (script return type `Actor`). The new Rune native reverses the existing coarse `AttachActorToJoint` basing fallback by unbasing and returning an owned based actor; it deliberately refuses to detach an unrelated pawn merely standing on `Self` when no real joint table exists.

Final native Release verification is recorded under `SurrealEngine/qa/runs/2026-07-27/795068d1-dirty-rune-menu-close`: Rune Classic 1.10 exact intro relaunch (15.6s), Rune Gold 1.07 exact intro relaunch (34.0s), Rune Gold `Dwarf1wwheel` direct play path (14.1s), UT99 v436 `DM-Deck16][` (22.7s), and Unreal Gold v226b default map (23.2s) all reached gameplay/map initialization and shut down gracefully with zero `Unhandled Exception`, missing-package, `Unknown native function`, `Script error`, or `Accessed None` matches. The branch still predates the repository's CTest suite, so this remains native build plus retail smoke evidence rather than a CTest result.

**2026-07-27, remaining Rune menu contracts and grouped UWindow assets fixed.** The previously-catalogued `LANGUAGE` and `ISADDON` lines are real Rune menu console contracts. `LANGUAGE` now returns `Engine.Engine.Language` from the loaded system ini (defaulting to `int`), which is also how Rune's scripts select `RMenu.RussianRootWindow` for `rut`. `ISADDON` now returns `ADDON` when `HallsOfValhalla.u` is present on the package path and `NONE` otherwise; the recognized `Rune.exe` hashes are base-game executables rather than a separately-registered standalone HOV executable. Both handlers are Rune-gated.

The nine `UWindow.Icons.RCBack0`-`8` failures were not missing retail files: Rune Classic's `UWindow.u` embeds those exports under the `Icons` group. `Object.DynamicLoadObject` previously treated everything after the first dot as the object name, so `UWindow.Icons.RCBack0` searched for the literal object `Icons.RCBack0`. Both VM signatures now preserve the old group-agnostic `Package.Object` behavior while resolving `Package.Group.Object` with the package's grouped lookup. Rune Classic's exact intro relaunch now creates its UWindow root with no RCBack or unknown-command errors.

**2026-07-27, Rune particle struct decoding and decal ABI fixed from the 1.07 public headers.** The `Dwarf1wwheel` run still emitted 3,469 `ParticleArray ... expected 123 bytes` resync warnings. A temporary layout diagnostic (removed before the final build) showed that SurrealEngine knew every field of Rune's `Particle` struct but consumed only 87 bytes; its `Points` field reported one 12-byte vector element. The official Rune 1.07 public header release (`Engine/Inc/UnObj.h`) supplies the missing fact: `FVector Points[4]`. The generic `UStructProperty` load and save paths iterated each nested field only once even when its `ArrayDimension` was greater than one. They now iterate every fixed-array element symmetrically. The three newly-consumed vectors account for the exact 36-byte under-read, and both Rune Classic and Rune Gold `Dwarf1wwheel` logs now contain zero property-size resyncs while still reaching login/inventory acceptance and clean shutdown.

That corrected run made Rune Gold's intro debris sequence reliably expose another ABI difference: Rune's released declaration is `native function bool AttachDecal(float TraceDistance, optional vector DecalDir)`, while stock Unreal returns a decal object. SurrealEngine already had a boolean implementation for Deus Ex; Rune now selects that same return ABI. Repeated A/B runs proved the debris event occurred with both the old and fixed struct decoders, and the Rune boolean dispatch removed every `Accessed mismatched value type`/`Decal.AttachToSurface` script error. At that commit, six nonfatal `LimbWeapon.Drop.EndState: Blood.Destroy()` `Accessed None` warnings remained in the timed Rune Gold intro debris sequence; they reproduced independently of the array fix and were recorded rather than hidden.

The committed Release matrix is recorded at `SurrealEngine/qa/runs/2026-07-27/d8745fad-dirty-rune-ui-contracts` for tested commit `cd173087`: Rune Classic and Rune Gold exact intro relaunch, both versions' direct `Dwarf1wwheel` path, UT99 v436 `DM-Deck16][`, and Unreal Gold v226b all shut down normally with zero command/grouped-load/fatal/package/native/type/resync matches. Rune Gold intro alone has the six known `Accessed None` script warnings above. This topic branch still has zero CTest cases (`ctest -N`).

**2026-07-27, falling reachability noise and Rune joint attachment identity fixed.** A temporary caller diagnostic showed the remaining `ActorReachable called for unsupported physics mode` lines came from intro dwarves in `PHYS_Falling`. Inspection of the owned Rune Gold 1.07 `Engine.dll` confirmed stock `APawn::Reachable` returns false without logging for modes other than walking, swimming, and flying (including falling). SurrealEngine already returned the correct false result, so the misleading `LogUnimplemented` call was removed without changing reachability behavior.

The six Gold intro `Accessed None` warnings were a real native-compatibility bug rather than a Rune script omission. An output-only `UCC batchexport` of the owned `RuneI.u` showed `LimbWeapon.Drop` spawning `Blood`, attaching it to the named `offset` joint, then assigning the result of `DetachActorFromJoint(offset)` back to `Blood` before destroying it. The earlier fallback searched for a based actor whose `Owner` was `Self`, but Rune's spawn omitted an owner, so it could never return the actor it had attached. Actors now retain runtime-only joint-to-actor identity alongside the existing coarse whole-actor basing fallback. `JointNamed` supplies stable case-insensitive synthetic IDs in a negative namespace (leaving Rune's numeric joint IDs untouched and zero as `None`), detach returns the actor at the requested joint, stale entries are removed with the basing relationship, and `ActorAttachedTo` is registered at Rune's package-declared native index 617. This does not add per-joint transforms or skeletal skinning; it makes the existing approximation internally consistent.

Release verification is recorded under `SurrealEngine/qa/runs/2026-07-27/dade8133-dirty-rune-reachability`: Rune Classic 1.10 and Rune Gold 1.07 exact intro relaunches, both versions' direct `Dwarf1wwheel` paths, UT99 v436 `DM-Deck16][`, and Unreal Gold v226b all shut down normally with zero command/package/native/type/resync, `Accessed None`, or unsupported-reachability matches. In particular, Rune Gold's timed intro now reaches the same debris sequence with none of its previous six warnings. The Release build succeeds; this topic branch still has no CTest cases.

**2026-07-27, Rune melee sweep compatibility added and verified without visible windows or audio output.** Inspection of the owned Rune Gold 1.07 `Engine.u` supplied the package-declared indices and signatures for the next actor-native group: `GetJointRot` (603), `GetJointName` (605), `ApplyJointForce` (608), `NumJoints` (609), `SetDefaultJointFlags` (611), `TurnJointTo` (616), `ClosestJointTo` (618), and `FrameSweep` (622), plus the `SweepActors` iterator (313). A matching disassembly check of the owned 1.07 `Engine.dll` confirmed that retail `FrameSweep` computes the prior/current weapon segment, invokes the scripted `FrameSwept` event, and updates the two previous-segment outputs. These functions are now registered only through Rune's native registry, so the iterator's index does not collide with stock-engine native 313 in other games.

The compatibility layer remains deliberately actor-level because SurrealEngine has no Rune skeletal transform or per-joint collision geometry. `GetJointRot` returns the actor rotation, `GetJointName` reports no concrete name, `NumJoints` reports zero, `ClosestJointTo` returns the root/body fallback, and the three joint-mutating helpers are safe no-ops. `FrameSweep` constructs the current weapon segment from the weapon actor transform and Rune's weapon vector, invokes the real script `FrameSwept(B1,E1,B2,E2)` callback, then persists the current endpoints. `SweepActors` approximates the swept quadrilateral with six capsule traces (old and new weapon segments, both endpoint edges, and both diagonals), deduplicates hits, excludes the weapon and its owner, maps BSP hits to the level actor, and returns zero joint masks so Rune scripts use their root/body handling. This closes the missing VM/native execution path, but it is not per-joint hit detection or bone-driven collision.

A temporary argument-gated diagnostic spawned `RuneI.VikingShortSword`, entered its real `Swinging` state, and invoked `FrameSweep` through the VM. The resulting script callback reached the new `SweepActors` iterator; the diagnostic hook and trace log were removed before the committed build. Release compilation succeeded. This branch still contains no CTest cases (`ctest --test-dir build -C Release --output-on-failure` reports `No tests were found`). The exact committed Release executable was copied to `SurrealEngine/out/rune-support-native-2026-07-27/Release` (SHA-256 `2958E6840249F464A515C51383513090086D6655C0A32F8D51F0D28372AC3C62`).

The final runtime matrix used `Start-Process -WindowStyle Hidden` for every process and checked that every process retained `MainWindowHandle == 0`. Every case also set `ALSOFT_DRIVERS=null`; a separate OpenAL diagnostic log records `Initialized backend "null"` and device `No Output`. Rune Classic and Rune Gold exact `RELAUNCH rune://intro.run?video=.` paths, both versions' direct `Dwarf1wwheel` paths, UT99 v436 `DM-Deck16][`, and Unreal Gold v226b all reached normal shutdown with zero command/package/native/type/resync, `Accessed None`, unsupported-reachability, or script-error matches. The manifest and logs are under `SurrealEngine/qa/runs/2026-07-27/e72e9541-dirty-rune-melee`. This verifies the native/VM/iterator path and cross-game runtime stability; interactive target damage and true per-joint hit masks remain human/future skeletal-collision validation work.

## Open questions and risks (do not assume answers)

1. ~~Does the user actually own a legitimate copy of Rune~~ — **Resolved
   2026-07-24**: yes, Rune Classic via GOG, installed at
   `C:\Program Files (x86)\GOG Galaxy\Games\Rune Classic\`. M1 is unblocked.
2. No official native-modding headers exist for the 1.10/1.11 build this
   project already detects as primary (only 1.00/1.07 headers were ever
   published, per the OldUnreal thread). Gap discovery (M2) may need to lean
   more heavily on live exception cataloguing than on any published reference,
   and cross-version differences between 1.07 and 1.10/1.11 native surfaces
   are unverified.
3. ~~Whether Rune's actual shipped player/monster meshes are `USkeletalMesh` or
   ordinary vertex-animated meshes is unverified.~~ **Resolved 2026-07-24:**
   Rune uses a separate `SkelMesh` property and `Skeletal` companion-actor
   indirection with joint/group state. The M3 investigation above confirms that
   full bone-driven skinning is a distinct renderer/data-model project, not a
   prerequisite for the current playable compatibility slice.
4. The `!IsRune()` guard at `PackageManager.cpp:226` and the `Handedness`
   skip at `UActor.cpp:4144` both need their original root cause confirmed
   against real Rune data before this plan's M1/M4 code touches nearby logic;
   right now the former looks logically redundant given `ue1Version=500`, and
   the latter's exact failure mode (crash? wrong value? absent property?) is
   not recorded anywhere found in this pass.
5. Whether `NUPakPathNodeIterator`/`NUPakPawnPathNodeIterator` (currently
   registered only inside `RegisterUnrealNativeFunctions`, i.e. gated to
   `GameId::Unreal`) are needed by Rune's own pathing is unknown; if Rune ships
   a `UPak` package, that registration path may need to become shared rather
   than Unreal-exclusive — decide only after M1/M2 evidence, not
   speculatively.
6. No GameCapability flags are proposed yet; if M2 discovery finds Rune needs
   an equivalent of Deus Ex's `PostPostBeginPlayEvent` or `SaveInfoPackages`
   behavior, name it explicitly rather than reusing an existing Deus-Ex-named
   flag for different semantics.
7. ~~The in-game "leave Classic Mode" menu flow does not visually work.~~
   **Resolved 2026-07-27:** the `RELAUNCH` handler now invokes Rune's own
   `WindowConsole.CloseUWindow()` when its root exists. Live-state diagnostics
   confirmed that the root becomes hidden, UWindow becomes inactive, Windows
   mouse capture is released, and travel proceeds. The direct-to-level launch
   remains useful for automation, but is no longer the required workaround for
   the menu overlay itself.
