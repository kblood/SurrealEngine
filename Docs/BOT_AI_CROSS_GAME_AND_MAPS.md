# Bot AI across UT99, Unreal, and maps

## Decision

The bot policy boundary can be shared across Unreal Engine 1 games, but the benchmark lifecycle is deliberately profile-specific. Two controlled deathmatch profiles are currently supported: Unreal Tournament 436 and the owner-supplied Unreal Gold 226b. Their lifecycle adapters have separate class, roster, and skill requirements; a result from one is never used as evidence for the other.

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

## UT99 436 controlled profile

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

## Unreal Gold 226b controlled profile

Owner-supplied Unreal Gold registration exports verify these identities:

- game classes: `UnrealShare.DeathMatchGame`, `UnrealShare.TeamGame`, `UnrealShare.CoopGame`, `UnrealI.DarkMatch`, and `UnrealI.KingOfTheHill`;
- spectator class: `UnrealShare.UnrealSpectator`;
- bot metaclass: `UnrealShare.Bots`;
- bot classes: `UnrealShare.FemaleOneBot`, `UnrealShare.MaleThreeBot`, `UnrealI.FemaleTwoBot`, `UnrealI.MaleOneBot`, `UnrealI.MaleTwoBot`, and `UnrealI.SkaarjPlayerBot`.

The verified 226b adapter is intentionally distinct from UT's. It requires `UnrealShare.DeathMatchGame`, logs in with `UnrealShare.UnrealSpectator` through the game-specific player-class route, suppresses multiplayer auto-bots, disables random bot order, creates the deterministic roster via `AddBots 1`, requires `NumBots` accounting and a bot `PRI` flag, verifies the concrete bot class against the catalog above, and applies the verified external skill range 0–3 through the Unreal-specific effective-skill path. Fixed-seed end-to-end smoke runs have exercised the profile on `DmDeathFan`.

This support is limited to ordinary 226b deathmatch. TeamGame, CoopGame, DarkMatch, King of the Hill, Unreal 224/225/226f, OldUnreal 227, UT 400/451/469, and mods remain unsupported until each has a separately verified lifecycle and mode contract. The current runner also has no verified named-roster, role-swap, or start-layout control for Unreal; those are benchmark-coverage gaps, not capabilities to infer from a passing smoke run.

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

- `SurrealEngine/BotBenchmark/BotBenchmarkDriver.cpp` is the executable source of the UT436 and Unreal Gold 226b lifecycle adapters and failure checks.
- `Docs/BOT_BENCHMARK_DRIVER.md` describes the existing controlled runner and its present evidence level.
- `SurrealEngine/GameFolder.cpp` shows that the engine detects many Unreal and UT versions; the new resolver intentionally enables only the versions whose bot contract was examined.
- The Unreal 226b class list above came from local owner-supplied `UnrealShare.int` and `UnrealI.int` registration exports. No commercial game content is copied into this repository.

External corroboration and map research:

- [UnCodeX UT99 v436 `Botpack.DeathMatchPlus`](https://eatsleeput.com/undox/Uncodex-UT99-v436/botpack/deathmatchplus.html) lists `BotConfig`, bot creation functions, and the class hierarchy.
- [UnCodeX UT99 v436 `Botpack.CHSpectator`](https://eatsleeput.com/undox/Uncodex-UT99-v436/botpack/chspectator.html) shows the spectator class and its inherited `PlayerPawn.AddBots` path.
- [UnCodeX UT99 v436 `UnrealShare.DeathMatchGame`](https://eatsleeput.com/undox/Uncodex-UT99-v436/unrealshare/deathmatchgame.html) is useful comparison evidence only; the controlled Unreal Gold 226b support is based on the owner-supplied runtime adapter and end-to-end telemetry, not inferred from UT's v436 package.
- [OldUnreal's Unreal walkthrough index](https://www.oldunreal.com/wiki/index.php?title=English_Version_Walkthrough), [NyLeve's Falls](https://www.oldunreal.com/wiki/index.php?title=NyLeve%27s_Falls), [Chizra](https://oldunreal.com/wiki/index.php?title=Chizra_-_Nali_Water_God), and [The Sunspire](https://www.oldunreal.com/wiki/index.php?title=The_Sunspire) corroborate the proposed water, lift, dark, and scripted-route research candidates.

The resolver tests cover normalization, exact UT and Unreal requirements, version gating, deterministic equivalent inputs, both verified bot class catalogs, map-feature coverage, and actionable failure reasons.
