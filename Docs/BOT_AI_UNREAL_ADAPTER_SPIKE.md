# Unreal bot benchmark adapter verification spike

## Decision

Enable a dedicated, fail-closed Unreal Gold 226b deathmatch profile. Do not enable a generic Unreal profile or the version 200 demo profile.

The exact owner-installed GOG 226b packages now pass end-to-end SurrealEngine runs covering spectator login, automatic-roster suppression, ordered controlled spawning, 0-through-3 skill assignment, ordinary AI execution, combat death, and respawn. A second stock map also passes and same-seed runs reproduce the same ordered roster. Unreal 224v, 225f, 226f, and the version 200 demo remain unsupported because this runtime evidence does not generalize to them.

## Evidence rules

This report uses the following confidence labels:

- **Verified package fact**: observed in script text, class metadata, configuration, or map metadata in the exact local package named below.
- **Verified engine fact**: observed in the current SurrealEngine implementation.
- **Public corroboration**: user-facing manuals or exported registration data, useful as supporting evidence but not a substitute for an exact-version package.
- **Inference / runtime question**: plausible from script comments, nearby versions, or native behavior, but not accepted as an adapter contract until exercised.
- **UT v436 comparison**: describes UT's bundled UnrealShare or Botpack code only; it is never used as proof for Unreal.

No commercial script body or game asset is copied into this repository. The descriptions below summarize behavior inspected in locally supplied packages.

## Evidence inventory and version coverage

| Version | Exact local evidence | What is established | Coverage decision |
| --- | --- | --- | --- |
| Unreal 200 Special Edition demo | `Unreal.exe` SHA-1 `b851dcc69c4f773252c0498bd12756d90bcb59c2`; `Engine.u` SHA-1 `b4c08fdaf75f912da7cdcaaec4a4790412f54d49`; `UnrealI.u` SHA-1 `a28cdd9d534888381195003ade42acdf797e8f95` | Exact game, spectator, bot manager, spawn, skill-adjustment, death, and respawn scripts | Strong static contract; runtime unverified |
| Unreal 224v | Executable hash recognition only in `UE1GameDatabase.h` | Identity only | Unsupported |
| Unreal 225f | Executable hash recognition only in `UE1GameDatabase.h` | Identity only | Unsupported |
| Unreal 226f | Executable hash recognition only in `UE1GameDatabase.h` | Identity only | Unsupported |
| Unreal Gold 226b | Owner-installed `Unreal.exe` SHA-1 `a4e8149a3e3a9aeba3921eb5004973c4cb1a5c35`; `Engine.u` SHA-1 `2877472679afa0da5b7286b41666864a80253ceb`; `UnrealShare.u` SHA-1 `2bd91ab92544dab22d353d3b64e47581090e8c14`; runtime reports identify 226b | Exact game, spectator, bot manager, spawn, skill-adjustment, death, and respawn scripts plus completed controlled runtime on `DmMorbias` and `DmDeck16` | Enabled only by the exact 226b profile |

SurrealEngine recognizes all of 224v, 225f, 226f, and 226b, but executable recognition is not behavioral compatibility. Public OldUnreal registration pages confirm later Unreal distributions use `UnrealShare.DeathMatchGame` and `UnrealShare.UnrealSpectator`, but those pages are not version-tagged behavioral exports. No authoritative, version-specific 224v/225f/226f bot script set was found, so this report does not project 226b behavior backward or sideways onto them.

## Exact Unreal 200 contract

### Game and spectator

**Verified package facts:**

- The deathmatch game class is `UnrealI.DeathMatchGame`.
- The spectator class is `UnrealI.UnrealSpectator`. It directly inherits `Engine.Spectator` and adds no behavior.
- `Engine.GameInfo.Login` spawns the `SpawnClass` it receives and treats `Spectator` subclasses as spectators for capacity/accounting checks. In current SurrealEngine, `Engine::LoginPlayer` obtains that class from the URL `Class` option. Therefore the candidate adapter option is `Class=UnrealI.UnrealSpectator`, not the current UT-only `OverrideClass=Botpack.CHSpectator` assumption.
- `UnrealI.DeathMatchGame` does not replace the base spawn mechanics, but its login override increments `NumPlayers` after every successful superclass login, including an `UnrealSpectator`. Its logout override decrements `NumPlayers` for every `PlayerPawn`. This is a real version 200 accounting quirk, not evidence that the spectator became a bot.
- Version 200's base game code enforces the spectator allowance and adjusts its spectator capacity counter on dedicated servers. The deathmatch class separately performs the `NumPlayers` accounting above.
- Deathmatch viewing permission is unrestricted in standalone play; outside standalone it requires the viewer to be a `Spectator`.

The required runtime assertion is that login produces a pawn which `IsA("Spectator")`, has spectator state where that field is present, and is never selected as the controlled bot. The probe must expect version 200 `NumPlayers` to increase by one and must exclude the spectator by class/identity rather than assuming the counter means human combatants only.

### Automatic roster suppression

**Verified package facts:**

- `DeathMatchGame.PostBeginPlay` creates `BotConfig`. In standalone play it copies `InitialBots` into `RemainingBots` regardless of `bMultiPlayerBots`; in network play the copy also occurs when multiplayer bots are enabled.
- The deathmatch timer attempts one `AddBot` while `RemainingBots` is positive and decrements it after success.
- Version 200's deathmatch class does not parse `InitialBots` in an `InitGame` override. The base `Engine.GameInfo.InitGame` parses difficulty and administration-related options, not this field. `InitialBots=0` in the URL is therefore not a proven suppression mechanism for this version.

The safe candidate sequence is: load the map, before the first gameplay tick write `InitialBots=0` and `RemainingBots=0`, and also set `bMultiPlayerBots=false` when present. Both counters matter: changing only `InitialBots` after `PostBeginPlay` is too late, while changing only `bMultiPlayerBots` does not suppress standalone bots. The current engine completes actor `PostBeginPlay` during `LoadMap` but does not run a gameplay tick before the driver regains control, so it has the required write window. Runtime must still prove that no bot appears over at least two timer intervals.

### Controlled spawn and BotInfo

**Verified package facts:**

- `Engine.PlayerPawn.AddBots(N)` delegates to `ServerAddBots`; standalone/admin authorization and a `DeathMatchGame` type check gate the call; each accepted iteration invokes `Level.Game.AddBot()`.
- `DeathMatchGame.AddBot` asks `BotConfig` for a free configuration slot, finds a player start, dynamically loads and spawns that configured class as `Bots`, rejects an invalid or disallowed pawn, applies teleport and individual configuration, grants default inventory, and increments `NumBots`.
- The bot base is the plural class `UnrealI.Bots`, which extends `ScriptedPawn`. Valid demo candidates include `UnrealI.FemaleOneBot` and `UnrealI.MaleThreeBot`; non-demo retail classes must not be assumed present.
- `BotInfo` exposes `bAdjustSkill`, `bRandomOrder`, configured names, teams, per-slot skill offsets, classes, skins, and slot-use state. It has no `Difficulty` property in version 200.
- `BotInfo.Individualize` applies the slot identity, clamps the spawned pawn's existing `Skill` plus its configured slot offset to the Unreal range 0 through 3, then calls `ReSetSkill`.

For a deterministic probe, disable automatic adjustment and random ordering and make slot zero explicit. Detect the new actor by set difference plus `IsA("Bots")`, not `IsA("Bot")`. Require exactly one new bot, the configured class/name, `NumBots` increasing by one, default inventory, and a live AI state.

### Skill initialization

**Verified package facts:**

- Version 200 has no UT-style `InitializeSkill`, `bNovice`, or external 0-through-7 tier mapping.
- Effective bot skill is a float clamped to 0 through 3 after the configured per-slot offset. `ReSetSkill` derives lead targeting, refire behavior, health at the lowest skill, and movement parameters from that value.
- When `BotConfig.bAdjustSkill` is enabled, deathmatch kill handling calls the bot's dynamic adjustment routine: bot success against a human lowers its skill and bot death to a human raises it, with a 0-through-3 clamp. A controlled benchmark must set the flag false.

**Inference / runtime question:** the base skill that exists before `BotInfo.Individualize` may depend on native pawn initialization and class defaults. The adapter must observe it rather than import UT semantics. A version 200 probe can establish deterministic tiers by fixing the class and slot offset and asserting the resulting 0-through-3 value; it must not require `BotInfo.Difficulty`.

### Match start, scoring, death, and respawn

**Verified package facts:**

- There is no UT warm-up/ready contract to satisfy. Loading `UnrealI.DeathMatchGame`, logging in, suppressing the initial roster, and calling `AddBots` is the relevant start sequence.
- The base restart routine finds a player start, moves and rotates the existing pawn, clears motion, restores default health/collision/visibility/damage and sound values, plays the teleport effect, and supplies default inventory.
- A dying bot waits briefly, creates/hides its corpse representation, then retries after a bot-count-dependent randomized delay. On successful `GameInfo.RestartPlayer(self)` it clears motion, reapplies `ReSetSkill`, uses falling physics, and returns to roaming.
- Deathmatch kill handling performs normal score/death accounting and frag-limit handling. Dynamic skill adjustment occurs only when enabled as described above.

The exact retry delay is intentionally not a determinism contract. The probe should assert successful transition back to a live roaming bot within a bounded timeout and stable effective skill, not an exact frame number.

## Exact Unreal Gold 226b contract

### Game and spectator

**Verified package facts:**

- The deathmatch game class is `UnrealShare.DeathMatchGame`.
- The spectator class is `UnrealShare.UnrealSpectator`. It directly inherits `Engine.Spectator` and adds no behavior.
- The registered bot base is `UnrealShare.Bots`; owner metadata registers `UnrealShare.FemaleOneBot`, `UnrealShare.MaleThreeBot`, and additional `UnrealI` bot classes.
- Base `Engine.GameInfo.Login` uses its supplied spawn class and has explicit spectator capacity/accounting behavior. As with version 200, current SurrealEngine must supply `Class=UnrealShare.UnrealSpectator` and verify the resulting pawn; the UT `OverrideClass` value is not an Unreal contract.
- Base login excludes a spectator from `NumPlayers`; it increments `NumSpectators` only for a dedicated server. Deathmatch viewing permission is unrestricted in standalone play and otherwise requires the viewer to be a `Spectator`.

### Automatic roster suppression

**Verified package facts:**

- `DeathMatchGame.PostBeginPlay` spawns the configured `BotConfigType` and copies `InitialBots` to `RemainingBots` in standalone play or when multiplayer bots are enabled.
- Its timer adds one bot per timer invocation while `RemainingBots` is positive.
- The 226b deathmatch `InitGame` parses match limits and cooperative weapon behavior, but not `InitialBots` or `bMultiPlayerBots`.

The same post-load, pre-first-tick writes are therefore required: zero `InitialBots` and `RemainingBots`, and disable `bMultiPlayerBots`. `MinPlayers` is a UT concept and is not part of this contract.

### Controlled spawn and BotInfo

**Verified package facts:**

- `Engine.PlayerPawn.AddBots(N)` permits an administrator, standalone session, or listen server, requires a deathmatch game, and calls `Level.Game.AddBot()` for each requested bot.
- `DeathMatchGame.AddBot` first copies `BotConfig.Difficulty` to the game difficulty, chooses a configuration slot and player start, spawns the configured class as `Bots`, applies eligibility checks, individualizes it, grants inventory, increments `NumBots`, marks its replication info as a bot, assigns a player ID, and emits player-connect logging.
- `BotInfo` has a configurable `Difficulty` byte in addition to the random-order, adaptive-skill, identity, class, and per-slot adjustment fields. Its UI constrains difficulty to 0 through 3.
- `BotInfo.Individualize` adds the selected slot offset to the spawned pawn's base `Skill`, clamps the result to 0 through 3, and runs `ReSetSkill`.
- The concrete owner-data class chain reaches `UnrealShare.Bots` through intermediate human-bot classes. There is no class named `Bot` in that chain, so a UT-style `IsA("Bot")` discovery predicate is not valid.

### Skill initialization

**Verified package facts:**

- Just before spawning the pawn, deathmatch copies `BotConfig.Difficulty` into `GameInfo.Difficulty`.
- `Engine.Pawn.Skill` is documented in the exact package as a skill value scaled by game difficulty, and its native class owns part of initialization. After spawn, the script-visible per-slot offset and `ReSetSkill` behavior are exact.
- 226b `Bots` has the same 0-through-3 adaptive adjustment direction and clamp as version 200. It has no UT `InitializeSkill` method and no verified `bNovice` split.

**Inference / runtime question:** the native step connecting game difficulty to the newly spawned pawn's pre-individualization `Skill` is not present in exported UnrealScript. It must be measured for requested values 0, 1, 2, and 3. Static evidence is not sufficient to promise an exact mapping, particularly in a reimplementation of native engine behavior.

### Match start, death, and respawn

**Verified package facts:**

- The start contract is immediate deathmatch play, not UT's readiness lifecycle.
- The base restart routine relocates the existing pawn to a player start, restores orientation, motion, health, collision, visibility, damage/sound defaults, and inventory.
- The bot's dying state waits, hides the dead pawn after creating its carcass, repeatedly invokes `Level.Game.RestartPlayer(self)`, and on success clears motion, reapplies `ReSetSkill`, selects falling physics, and returns to roaming. In 226b, failed relocation explicitly returns to the dying retry label.
- Deathmatch kill handling performs frag-limit and optional adaptive-skill processing; with `bAdjustSkill=false`, benchmark skill should remain stable through controlled kills and respawns.

## What UT v436 does and does not tell us

UT v436 generated documentation is useful for names and evolutionary comparison. It shows that UT still ships some UnrealShare classes, while UT's controlled deathmatch behavior is centered on `Botpack.DeathMatchPlus`, `Botpack.Bot`, a UT spectator class, `InitializeSkill`, and the novice/master tier split used by the current driver.

That is not the Unreal contract:

| Concern | Unreal 200 | Unreal Gold 226b | UT v436/current benchmark assumption |
| --- | --- | --- | --- |
| Game | `UnrealI.DeathMatchGame` | `UnrealShare.DeathMatchGame` | `Botpack.DeathMatchPlus` |
| Spectator | `UnrealI.UnrealSpectator` | `UnrealShare.UnrealSpectator` | `Botpack.CHSpectator` |
| Bot base | `UnrealI.Bots` | `UnrealShare.Bots` | `Botpack.Bot` |
| Skill manager | no `BotInfo.Difficulty`; slot offset and pawn skill | `BotInfo.Difficulty` 0-3 plus slot offset | UT external tier 0-7 |
| Bot initialization | `ReSetSkill` | `ReSetSkill` | `InitializeSkill` and `bNovice` |
| Automatic bots | `InitialBots` -> `RemainingBots` timer | `InitialBots` -> `RemainingBots` timer | `MinPlayers`/UT roster rules plus benchmark suppression |

Public v436 pages must therefore remain comparison citations only, especially because the public UnrealShare localization/registration pages combine Unreal and UT sections and do not identify a precise Unreal patch's function bodies.

## Implemented adapter and runtime evidence

The shared controlled-match code now selects behavior from a version-specific spawn contract. UT436 retains `OverrideClass`, singular `Bot`, `MinPlayers`, named bot support, `InitializeSkill`, and its 0-through-7 novice/master split. Unreal Gold 226b instead uses URL `Class`, plural `Bots`, `InitialBots`/`RemainingBots` plus `bMultiPlayerBots`, unnamed `AddBots`, an exact concrete-class catalog, `NumBots` accounting, and explicit effective `Skill` 0 through 3 followed by `ReSetSkill`. Requested names or skills outside that range fail before the map is mutated. Other Unreal versions remain unsupported.

The runtime validation used the owner-installed GOG data listed above:

- `DmMorbias`, seed 123, two skill-2 bots: 600 fixed ticks / 10.000019521 simulated seconds completed. The exact roster was `UnrealShare.MaleThreeBot` (`Dante`, PRI 1) followed by `UnrealI.MaleTwoBot` (`Ash`, PRI 2). Both entered AI states and moved; Dante recorded a kill, Ash recorded a death, and Ash was live again at 100 health in `Roaming`, proving normal death/respawn continuity.
- `DmDeck16`, seed 123, the same roster: 1200 fixed ticks / 20.000039041 seconds completed. Both bots executed AI; Ash recorded an environmental suicide/death and returned live at 100 health in `Roaming`.
- `DmMorbias`, seed 321, requested skills 0 and 3: two separate 120-tick runs completed with identical ordered class, actor, player-name, and PRI fields. Setup's runtime assertions verified each pawn's effective skill after `ReSetSkill`.
- UT GOTY v436 regression, `DM-Morbias][`, seed 123, two external-skill-7 bots: 120 fixed ticks completed with the normal `Botpack.DeathMatchPlus`/`CHSpectator` path and an ordered `Botpack` roster, confirming the Unreal specialization did not replace UT's contract.

The successful artifacts are outside the repository under `C:/Devstuff/QuestGames/artifacts/unreal-226b-bot-probe-20260724-b` through `-e`. The first `-a` run exposed an unrelated UT-shaped telemetry property access; that access was property-gated and the same adapter then passed. No game data was modified.

## Smallest owner-data runtime probe

Add a headless, read-only-to-game-data C++ probe or commandlet beside the benchmark driver. It should use the existing loader, reflection, event, actor-list, and exec facilities and emit a compact JSON contract report. It does not need a new gameplay mutator or copied UnrealScript.

For each exact installation/package-hash tuple:

1. Load a deathmatch map with the exact game class: `UnrealI.DeathMatchGame` for 200 or `UnrealShare.DeathMatchGame` for 226b.
2. Immediately after `LoadMap` and before the first gameplay tick, require `InitialBots` and `RemainingBots`, write both to zero, and set `bMultiPlayerBots=false` when present.
3. Set URL `Class` to the exact spectator class, call normal login, and require a spectator pawn which is never selected as a bot. Record counter deltas and compare them with the version contract: version 200 increases `NumPlayers`; 226b excludes a spectator from `NumPlayers` and only increments `NumSpectators` for a dedicated server.
4. Require a non-null `BotConfig`; set `bAdjustSkill=false` and `bRandomOrder=false`; make slot zero's class, name, and skill offset deterministic. On 226b set `BotConfig.Difficulty` to the requested 0-through-3 value. On 200 record that the property is absent and use only the explicit class/slot contract.
5. Tick for at least two deathmatch timer intervals and require zero spontaneous `IsA("Bots")` pawns.
6. Snapshot the actor set, execute `AddBots 1`, and find the set difference using `IsA("Bots")`. Require exactly one new bot and validate its class, identity, `NumBots`, replication bot flag where available, inventory, health, and active state.
7. Record pre-individualization inputs where observable and the effective `Skill` after spawn. For 226b repeat spawn in clean sessions at difficulty 0, 1, 2, and 3. Never call `InitializeSkill`.
8. Tick long enough to observe ordinary AI execution with no script warning or VM exception and no second bot.
9. Apply a controlled lethal event through the normal pawn death path. Observe dying/hidden, then the same pawn returning alive through `RestartPlayer`, with restored health/inventory and roaming behavior before a generous timeout.
10. Require effective skill to remain stable through respawn and through one bot/human kill-direction check while adaptive skill is disabled.
11. Repeat the clean-session run twice with the same engine seed/configuration and compare class, name, roster count, effective skill, and lifecycle transitions. Do not require the randomized respawn delay to land on the same frame unless the engine explicitly promises seeded timing.
12. Emit game identity, version, package hashes, map, classes, requested difficulty/slot offset, observed skill, actor IDs, state transitions, warnings, and pass/fail checks.

The probe should fail rather than silently falling back whenever a required property, class, command, player start, or bot transition is missing.

## Acceptance gates for enabling one profile

A version-specific Unreal profile may be enabled only when all of these pass on its exact data tuple:

- correct game and spectator class load; spectator login succeeds, is never counted as the controlled bot, and produces the version-expected roster-counter deltas;
- zero spontaneous bots for at least two timer intervals after suppression;
- `AddBots 1` exists and produces exactly one new `Bots`-derived pawn;
- configured class and identity are honored, `NumBots` is correct, and no unrelated pawn is captured;
- effective skill is in 0 through 3, matches the adapter's version-specific requested mapping, and is stable when adaptive adjustment is disabled;
- the bot receives usable inventory and executes AI without script warnings, VM exceptions, invalid references, or runaway state code;
- normal lethal damage reaches the dying state and the same bot successfully respawns within the timeout with restored health/inventory and active roaming;
- no automatic replacement or second bot appears during death/respawn;
- two same-seed runs agree on the deterministic contract fields;
- the baseline map and one traversal-stress map both pass;
- the report records exact package hashes, so results cannot be generalized to 224v/225f/226f or another 226 build.

Passing only spawn or only navigation is insufficient: spectator and roster isolation, skill observability, and death/respawn are part of the profile contract.

## Map applicability

### Unreal 200 Special Edition demo

The local demo supplies `DmBayC.unr`, `DmCreek.unr`, and `DmKrazy.unr`. They are valid initial adapter fixtures because they contain deathmatch player starts and are part of the demo data. Start with the smallest/stablest map after a quick package scan, then use a second map that exercises more traversal. The non-`Dm` single-player maps are not acceptable substitutes for deathmatch scoring and respawn validation.

The profile must remain scoped to maps whose required packages exist in the exact demo. It must not advertise retail-only bot classes, skins, or maps.

### Unreal Gold 226b owner data

The owner's manifest contains stock deathmatch maps including `DmMorbias.unr`, `DmDeck16.unr`, and `DmDeathFan.unr`. `DmMorbias` is a reasonable compact baseline; `DmDeck16` is the stronger second gate for vertical routing, inventory choice, and movers; `DmDeathFan` can later add environmental-hazard coverage. These names establish local availability, not permission to redistribute the maps.

Mission Pack maps under `Maps/UPak` introduce another package/game-content dimension and should not be part of the first base-Unreal adapter acceptance run.

### 224v, 225f, and 226f

No map is an approved bot fixture for these profiles until the user supplies matching data, the exact packages are hashed and inspected, and the same probe passes. Sharing a map name with 226b does not establish the same script or native contract.

## Provenance and public references

- Local Unreal 200 source: `C:/Devstuff/QuestGames/demos-audit-20260722/unreal-special-edition`, identified by the executable hash already mapped to `UNREAL_200_DEMO` in this repository. Package script text and demo configuration/registration metadata were inspected in place.
- Local Unreal Gold 226b source: owner-supplied OPFS artifacts under `C:/Devstuff/QuestGames/artifacts/unreal-gold-owner-profile-fresh`. Package hashes agree with the existing 226b runtime reports. Only metadata and summarized behavior are recorded here.
- [OldUnreal Unreal manual](https://oldunreal.com/guides/Unreal%20manual.pdf) publicly documents starting a bot game, choosing a DM map, setting bot count, configuring appearance/aggressiveness/combat style, and using bots in network games.
- [OldUnreal UnrealScript language reference](https://www.oldunreal.com/wiki/index.php?title=Unrealscript_Language_Reference) describes the language and class/state model used by the exported scripts.
- [OldUnreal `UnrealShare.int`](https://www.oldunreal.com/wiki/index.php?title=UnrealShare.int) corroborates public UnrealShare registrations, but its combined Unreal/UT presentation is not used as version-specific behavior proof.
- [Unreal Wiki: exporting UnrealScript source](https://wiki.beyondunreal.com/UnrealScript_source_code) explains that game UnrealScript is exported from the packages/editor; this is the provenance category of the inspected script text, not a release of native commercial engine source.
- [UnCodeX UT99 v436 `UnrealShare.DeathMatchGame`](https://eatsleeput.com/undox/Uncodex-UT99-v436/unrealshare/deathmatchgame.html), [UT99 v436 `UnrealShare.UnrealSpectator`](https://eatsleeput.com/undox/Uncodex-UT99-v436/unrealshare/unrealspectator.html), and [the v436 individual-bot menu](https://eatsleeput.com/undox/Uncodex-UT99-v436/Source_unrealshare/unrealindivbotmenu.html) are comparison evidence only.

## Recommended next action

Keep Unreal Gold 226b scoped to the exact detected version and deathmatch mode. Add CI/package-fixture coverage when legally and operationally possible; until then, retain the runtime evidence above as the owner-data acceptance record. If Unreal 200 support is wanted, run the same full probe against its exact demo packages and implement a separate adapter because its `BotInfo` contract differs.

Do not create a generic "Unreal 224-226" profile from the two verified endpoints.
