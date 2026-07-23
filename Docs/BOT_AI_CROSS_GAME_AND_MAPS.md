# Bot AI across UT99, Unreal, and maps

## Decision

The bot policy boundary can be shared across Unreal Engine 1 games, but the benchmark lifecycle cannot yet be shared safely. The only controlled profile currently supported is Unreal Tournament 436 deathmatch. Unreal Gold 226b is recognized and its exported class identities are recorded, but it deliberately returns unsupported until its bot creation and skill-control behavior are verified from owner-supplied scripts and an end-to-end run.

`BotBenchmarkGameProfileResolver` is a pure C++ description of that boundary. It normalizes the detected game name and version, returns exact per-game class requirements, and refuses to copy UT assumptions into another game or patch. Its map feature tags are test-matrix requirements, not claims that a particular map has been scanned.

## What is shared

These parts belong above the game profile and should remain common:

- deterministic ticks, seeds, run limits, result serialization, and telemetry;
- world observations such as visible pawns, remembered threats, inventory utility, route danger, movement progress, and stuck duration;
- policy decisions such as attack, investigate, acquire an item, retreat, explore, or recover from being stuck;
- UE1 navigation primitives, collision, physics, zones, inventory, movers, and actor state access when the relevant game exposes them;
- capability checks, explicit unsupported reasons, and the map-feature vocabulary used to build a coverage matrix;
- controlled synthetic fixtures for corridors, vertical routes, lifts, doors, water, hazards, sightlines, and item choice.

Sharing those mechanisms does not mean sharing package names, bot classes, spawn commands, skill fields, team rules, objectives, or assumptions about how a game transitions a pawn into play. Those remain profile- or mode-specific adapters.

## What is UT-only today

The current `BotBenchmarkDriver` performs this exact UT99 sequence:

1. disable automatic bots with URL options `Bots=0`, `MinPlayers=0`, and `InitialBots=0`;
2. force spectator login through `OverrideClass=Botpack.CHSpectator`;
3. require a `Botpack.DeathMatchPlus`-compatible `GameInfo` with `BotConfig`;
4. set `BotConfig.Difficulty` and disable automatic skill adjustment;
5. execute `AddBots 1` through the spectator;
6. require exactly one newly created pawn derived from `Bot` and call `InitializeSkill`.

The supported resolver profile therefore records only:

- normalized identity `unrealtournament`, version `436`;
- mode `deathmatch` with `Botpack.DeathMatchPlus`;
- spectator `Botpack.CHSpectator`;
- bot base `Botpack.Bot`;
- configuration property `BotConfig`;
- command `AddBots 1`;
- capabilities for spectator login, automatic-bot suppression, controlled single-bot spawning, and explicit skill control.

Team deathmatch, CTF, Domination, Assault, Last Man Standing, other UT patches, and mods are not enabled merely because their packages are present. Each needs a verified mode or version profile and its own objective/roster assertions.

## What is verified for Unreal Gold 226b

Owner-supplied Unreal Gold registration exports verify these identities:

- game classes: `UnrealShare.DeathMatchGame`, `UnrealShare.TeamGame`, `UnrealShare.CoopGame`, `UnrealI.DarkMatch`, and `UnrealI.KingOfTheHill`;
- spectator class: `UnrealShare.UnrealSpectator`;
- bot metaclass: `UnrealShare.Bots`;
- bot classes: `UnrealShare.FemaleOneBot`, `UnrealShare.MaleThreeBot`, `UnrealI.FemaleTwoBot`, `UnrealI.MaleOneBot`, `UnrealI.MaleTwoBot`, and `UnrealI.SkaarjPlayerBot`.

Those exports establish class registration only. They do not prove that the 226b runtime accepts the UT benchmark's `AddBots 1` route, that its `BotConfig` object has UT's fields, that `InitializeSkill` has compatible semantics, or that the spectator login and automatic-bot suppression sequence behaves identically. Public v436 script documentation shows similarly named UnrealShare functionality inside UT99, but that is not a substitute for the user's 226b package behavior.

For that reason the Unreal profile records the class catalog and candidate game modes but supports none of them for controlled benchmarking yet. Its `BotConfigProperty` and `SpawnCommand` remain empty.

## What Unreal still needs

Before enabling the 226b profile:

1. export or otherwise inspect the user-owned 226b `Engine`, `UnrealShare`, and `UnrealI` scripts involved in `PlayerPawn.AddBots`, `DeathMatchGame.AddBot`, `BotInfo`, `UnrealSpectator`, and bot skill initialization;
2. write an Unreal-specific adapter for automatic-bot suppression, spectator login, deterministic single-bot spawning, bot discovery, and skill assignment;
3. prove the adapter against a tiny owner-supplied Unreal deathmatch map with assertions for exactly one bot, stable class selection, stable difficulty, possession, restart, death, and respawn;
4. add fixed-seed telemetry comparisons before and after a policy change;
5. test team, DarkMatch, King of the Hill, and cooperative behavior separately instead of promoting them from class-name evidence.

The same rule applies to Unreal 224/225/226f, OldUnreal 227, UT 400/451/469, and other UE1 games: recognition by `GameFolder` is not evidence of a compatible bot benchmark contract.

## Map test matrix

Do not tune a bot on every shipped map. That encourages map memorization, makes regressions difficult to localize, and turns the whole commercial game library into an impractical test dependency. Use three layers:

1. tiny redistributable synthetic fixtures, one mechanic per map;
2. a small representative set of legally user-supplied stock maps for integration evidence;
3. held-out user-supplied maps used only for generalization checks, not parameter tuning.

An initial matrix is:

| Feature category | Synthetic fixture | UT99 owner-supplied candidate | Unreal owner-supplied candidate | Primary evidence |
|---|---|---|---|---|
| Basic navigation / compact combat | loop with two branches | `DM-Morbias][` | `DmMorbias` | route completion, target reacquisition, no oscillation |
| Corridors / verticality / inventory | ramps, ledges, alternate item routes | `DM-Deck16][` | `DmDeck16` | route diversity, drop recovery, pickup utility |
| Lifts / doors / timed movers | one call button, one lift, one door | `DM-Pressure` | `DmHealPod` | wait/board/exit success, blocker replanning |
| Environmental hazard | safe route beside damage floor | `DM-Fractal` or `DM-Pressure` | `DmDeathFan` | avoidable deaths, hazard-aware route cost |
| Low gravity / long sightlines | exposed platforms under low gravity | `DM-Morpheus` or `DM-Phobos` | no 226b candidate until map properties are scanned | fall recovery, aim and pursuit stability |
| Water / zone transition | dry route plus swim route | choose a user map only after zone scanning | `NyLeve` for traversal research, not DM scoring | enter/exit water, breathing, route continuity |
| Dark visibility | lit and unlit alternate routes | add only with an explicit perception fixture | `TheSunspire` or `Chizra` for research, not DM scoring | perception confidence and investigation behavior |
| Scripted objective / mover chain | trigger opens a multi-step route | Assault only after a mode adapter exists | `Vortex2`, `Chizra`, or `TheSunspire` after a cooperative adapter exists | trigger ordering, mover waits, recovery from interruption |

These stock-map assignments are starting hypotheses for fixture selection. Confirm each candidate by scanning its actor/zone/mover inventory and by a baseline replay before attaching a feature tag. In particular, Unreal single-player maps are valuable for navigation and scripted-world research but are not valid deathmatch benchmark scores without an explicit cooperative or scripted-objective contract.

Suggested tuning discipline:

- tune only on the synthetic fixtures plus one baseline and one complex map;
- freeze parameters before running the remaining representative maps;
- reserve at least one compact, one vertical/mover, and one hazard map as held-out checks;
- report results by feature tag and map, never only as an all-map aggregate;
- keep commercial maps outside the repository and require the user to supply/import them legally.

## Evidence and source boundary

Repository evidence:

- `SurrealEngine/BotBenchmark/BotBenchmarkDriver.cpp` is the executable source of the current UT-only lifecycle and failure checks.
- `Docs/BOT_BENCHMARK_DRIVER.md` describes the existing controlled runner and its present evidence level.
- `SurrealEngine/GameFolder.cpp` shows that the engine detects many Unreal and UT versions; the new resolver intentionally enables only the versions whose bot contract was examined.
- The Unreal 226b class list above came from local owner-supplied `UnrealShare.int` and `UnrealI.int` registration exports. No commercial game content is copied into this repository.

External corroboration and map research:

- [UnCodeX UT99 v436 `Botpack.DeathMatchPlus`](https://eatsleeput.com/undox/Uncodex-UT99-v436/botpack/deathmatchplus.html) lists `BotConfig`, bot creation functions, and the class hierarchy.
- [UnCodeX UT99 v436 `Botpack.CHSpectator`](https://eatsleeput.com/undox/Uncodex-UT99-v436/botpack/chspectator.html) shows the spectator class and its inherited `PlayerPawn.AddBots` path.
- [UnCodeX UT99 v436 `UnrealShare.DeathMatchGame`](https://eatsleeput.com/undox/Uncodex-UT99-v436/unrealshare/deathmatchgame.html) is useful comparison evidence, but describes UT's v436 package and is explicitly not treated as proof for Unreal Gold 226b.
- [OldUnreal's Unreal walkthrough index](https://www.oldunreal.com/wiki/index.php?title=English_Version_Walkthrough), [NyLeve's Falls](https://www.oldunreal.com/wiki/index.php?title=NyLeve%27s_Falls), [Chizra](https://oldunreal.com/wiki/index.php?title=Chizra_-_Nali_Water_God), and [The Sunspire](https://www.oldunreal.com/wiki/index.php?title=The_Sunspire) corroborate the proposed water, lift, dark, and scripted-route research candidates.

The resolver tests cover normalization, exact UT requirements, version gating, deterministic equivalent inputs, Unreal's verified class catalog, the absence of guessed Unreal spawn fields, map feature coverage, and actionable failure reasons.
