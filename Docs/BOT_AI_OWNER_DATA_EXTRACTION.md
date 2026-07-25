# BOT AI owner-data extraction

## Purpose

Build a reproducible, read-only catalog of the game data the bots actually use.
For each owner-supplied UT436 or Unreal Gold 226b installation, extract bot
UnrealScript, map navigation, reachspecs, traversal actors, zones, pickups,
player starts, and bot configuration evidence without changing the game files
or committing commercial content.

The catalog is an evidence input for bot fixtures and map selection. It is not
a new navigation source of truth: loaded UE1 NavigationPoints and reachspecs
remain authoritative.

## Version boundary

Do not conflate executable/game version with package file version. The loader
must record each source package's package version, licensee mode, and SHA-256.
In particular, Unreal Gold 226b code packages and its retail maps can have
different package versions, and neither may be assumed equivalent to UT436
packages merely because maps or classes share names. Catalog identity is
`(game identity, map/package name, package SHA-256)`, never map name alone.

## Extraction architecture

Use the existing SurrealEngine loader and reflection layer; do not add an
external decompiler.

1. Export UnrealScript using the existing `Exporter::ExportClass` path used by
   the `export scripts` commandlet, but add an explicit output directory
   outside the game root. Scope exports to bot, game, navigation, inventory,
   mover, and configuration packages selected by the catalog manifest.
2. Add a `map-catalog` headless driver. It loads one owner-supplied map through
   the normal package loader and writes an external JSON report. It has a
   static mode that reads the resolved `ULevel` data without gameplay ticks and
   a live mode with at most a pinned, deterministic warm-up for runtime zone
   membership and linked navigation-list checks.
3. Share one serializer between the driver and the currently unimplemented
   `export level` commandlet. The headless driver is the automation interface;
   the commandlet remains an optional interactive tool.
4. Provide Python schema validation, referential-integrity checks, and
   same-input two-run comparison. Commit only synthetic catalog fixtures and
   validators, never real-map output.

## Required catalog contents

`surreal-map-catalog-v1` must record:

- game executable and every relevant package identity/hash/version;
- every NavigationPoint's stable level index, name, class, position,
  collision bounds, authored navigation flags, costs, and path-index arrays;
- every reachspec's index, start/end node identity, distance, bounds, flags,
  and prune state;
- inventory spots and their marked items, pickups, and player starts;
- LiftCenter/LiftExit/mover links, mover keyframes and trigger relationships;
- teleporters and warp-zone markers;
- zones and relevant properties: water, pain, kill, damage, gravity, velocity,
  friction, and terminal velocity; and
- static/live distinction for data which needs resolved runtime actor zones.

`surreal-bot-config-catalog-v1` separately records bot class defaults and the
game's roster/skill configuration. It must merge package class defaults with
the applicable owner-local `.ini` and `.int` metadata rather than assuming all
roster data is serialized in one package.

Use generic guarded reflection for optional, profile-specific properties.
Typed accessors are only valid when their property is known to exist for the
loaded class/version.

## CLI and artifact layout

The proposed automation interface is:

```text
SurrealEngine.exe --headless-driver=map-catalog
  --catalog-map=<map> --catalog-output=<external directory>
  --catalog-export-scripts=0|1 --catalog-script-packages=<comma-separated packages>
  --catalog-mode=static|live --catalog-seed=<seed>
  --catalog-fixed-delta=<seconds> --catalog-warmup-ticks=<count>
  --catalog-game-config=0|1 <game root>
```

Owner-local artifacts go under
`qa/runs/<date>/map-catalog/<game-and-package-hash>/`. Each run contains
`provenance.json`, `game.json`, optional `bot-config.json`, and one JSON file
per map. The extractor must refuse an output path inside the game root.

## Validation gates

- Reachspec start/end references and per-node path indexes resolve exactly.
- Catalog counts reconcile with loaded `ULevel` and `NavigationPointList`.
- In live mode, zone membership and runtime linked-list observations are
  explicit; static-only data never claims live resolution.
- Two equal static extractions are byte-identical. Live extraction additionally
  pins seed, fixed delta, warm-up count, and stable actor ordering.
- Anchor maps include UT436 `DM-Deck16][` and Unreal Gold `DmDeathFan`, then
  each map feature is confirmed by catalog evidence before it enters a bot
  tuning or held-out matrix.
- A malformed, dangling, partial, or overflowed catalog fails closed in the
  Python validator.

## Delivery order

1. Implement a static nav/reachspec/zone spike for one map with synthetic
   serializer tests.
2. Add the headless `map-catalog` driver and verify static/live reconciliation
   on the two anchor maps.
3. Add package inventory, bot configuration, pickups, movers, and multi-map
   wrapper support.
4. Use verified catalogs to replace map-feature hypotheses with evidence and
   to generate focused bot navigation/survival fixtures.

## Phase-zero evidence

The static `map-catalog` headless-driver spike is implemented. It accepts
`--catalog-map` and `--catalog-output`, refuses outputs within the game root,
loads the requested map through the normal engine loader, and emits a
`surreal-map-catalog-spike-v3` JSON document. The current spike contains the
complete level actor-slot index, navigation points with authored path indexes
and flags, reachspecs, traversal relationships, resolved navigation-point zone
membership, model zone graph, and zone inventory/properties.
It deliberately does not claim the complete target catalog schema or live zone
membership yet.

On 2026-07-25 it extracted UT436 `DM-Deck16][` with 251 navigation points,
1,997 reachspecs, and four zones; it extracted Unreal Gold 226b `DmDeathFan`
with 116 navigation points, 1,431 reachspecs, and four zones. Both outputs
passed a referential-integrity check for every reachspec endpoint and every
per-node path index. Two independent DeathFan extractions were byte-identical:
SHA-256 `A14B5E9663C6CA2AB25CC37C7EEDEEEA8013B594351000AF03A5AFF3A656B679`.
All real-map artifacts remain owner-local under `qa/runs/`.

The current provenance fields distinguish UT436 `DM-Deck16][.unr` package
version 68 from Unreal Gold 226b `DmDeathFan.unr` package version 60, while
both installations' `UnrealI.u` and `UnrealShare.u` code packages report
package version 68. This is exactly why game-label matching alone is not a
safe input to bot tuning.

The same driver now has an explicit `--catalog-export-scripts=1` mode. It uses
the engine's existing class exporter and writes only beneath the external
catalog output directory. By default it attempts the available `Botpack`,
`Engine`, `UnrealI`, and `UnrealShare` packages; `Engine` is required to trace
the inherited Pawn movement, pain, and reachability contracts used by both
games. A caller can instead provide an exact,
comma-separated package list. The manifest records package version, loaded
class count, and exported-script count. It also fingerprints each source
package with its package-file name, UE1 package version, licensee mode, and
SHA-1; the complete v1 schema will upgrade this to SHA-256. Package, map, and
class output names are validated as path segments before any file is written. On the same owner
installations, UT436 exported 505 Botpack scripts (including `Bot.uc`), 146
UnrealI scripts, and 360 UnrealShare scripts. Unreal Gold 226b exported 146
UnrealI and 359 UnrealShare scripts (including `Bots.uc`); it correctly did
not require Botpack. These artifacts are analysis inputs only and are not
committed or redistributed.

## Next extraction tranche

An independent read-only review of the implemented driver and owner-local
artifacts confirmed that script export, raw reachspecs, and zone inventory are
only the first layer. The next extraction tranche remains read-only and should
be delivered with synthetic validators before it is used to authorize bot
behavior:

1. Add pinned live-spawn capability observations before treating a class-default
   capability flag as usable traversal authorization. Stock bot initialization
   can change those flags after class defaults load.
2. Upgrade provenance to SHA-256 before treating the catalog identity as
   complete.

These records select owner-local maps and deterministic fixture shapes; they
do not themselves prove a navigation correction is safe. In particular, a
pre-fall graph route or one collision sweep does not prove support, locomotion,
hazard exit, or live command ownership from the pawn's later position.

### Reachspec graph evidence

The first extension to the static spike is complete. Each reachspec now
preserves its raw `reach_flags`, emits the known UE1 capability names
(`walk`, `fly`, `swim`, `jump`, `door`, `special`, and `player_only`), and
retains any unknown flag bits separately. Each navigation point exports the
terminator-bounded forward `paths`, `upstream_paths`, `pruned_paths`, and
`visible_no_reach_actor_indexes` arrays. Serialization fails if an array
contains an out-of-range reachspec or one whose start/end owner disagrees with
the array direction; a visible-no-reach reference must resolve to an actor in
the loaded map.

Two owner-local extractions on 2026-07-25 were byte-identical for each anchor:
UT436 `DM-Deck16][` SHA-256
`CBA4EF069EB12A31BB22B8E683F3531E5DFA64AE36E8F925749EDAFD18CDDDDE`, and
Unreal Gold 226b `DmDeathFan` SHA-256
`136709AF23C476EC8577EFEE1A600030ADAAD3400BEBFC8219D67E01867BEC05`.
The Deck16 catalog has 251 navigation points, 1,997 reachspecs, and 1,370
visible-no-reach links; DeathFan has 116 navigation points and 1,431
reachspecs. These are owner-local evidence artifacts, not committed game data.

### Traversal, zone, and validator evidence

The v3 spike adds a complete actor-slot index so every relationship is
externally resolvable, including null slots preserved by UE1's level actor
array. It exports lift centers/exits, movers, teleporters, warp-zone markers
and zones, inventory spots, and player starts. The records retain relevant
actor links—for example a lift's mover/trigger, a mover's marker/triggers and
leader/follower, a teleporter's trigger and URL, and a marked pickup or warp
target. A non-null relationship missing from the loaded actor index fails the
extraction rather than being serialized as an ambiguous sentinel.

Each navigation point now records its resolved loaded-zone actor, and the model
zone graph records static connectivity and visibility masks. This is not a
claim about a later gameplay transition; live membership remains a separately
pinned catalog mode.

`Tools/BotBenchmark/Validate-MapCatalog.py` validates schema v3, actor-slot
identity, reachspec endpoint and direction ownership, exact reach-flag decode,
navigation/visible-no-reach references, traversal references, zones, and zone
graph masks. Its synthetic tests include dangling relationships, wrong directed
reachspecs, flag-mask mismatch, and malformed zone masks. It validated two
byte-identical v3 extractions per anchor: UT436 `DM-Deck16][` SHA-256
`9CEA6430EAC67702E908C7B6C4C0F4E90EB9642B357EA46710764B1470F12C4F`
(1,989 actor slots, 103 traversal records, and 64 model zones), and Unreal
Gold 226b `DmDeathFan` SHA-256
`6A8A87A05D9D02E45BF5834AC6F667326D89DC0C495F4526C99DF1CF3E22017A`
(683 actor slots, 44 traversal records, and 64 model zones).

### Bot configuration evidence

`--catalog-game-config=1` writes a separate
`surreal-bot-config-catalog-spike-v2` artifact. It records every key/value in
the recognized loaded user-INI roster section and resolves each configured
`BotClasses[...]` entry to guarded class-default movement values. UT436's
loaded `[Botpack.ChallengeBotInfo]` resolves its four configured tournament bot
classes; the retained Unreal Gold fixture has no loaded `[UnrealShare.BotInfo]`
roster section, so it explicitly reports the canonical `UnrealShare.Bots`
class as a fallback rather than claiming a roster it cannot observe.

Both anchor installations report class-default GroundSpeed 400, JumpZ 325,
MaxStepHeight 25, and AccelRate 2048 for the observed bot classes. The catalog
also records class-default capability bits, but marks them
`class_default_capabilities_realized_at_spawn: false`: stock Pawn/Bot scripts
enable traversal capabilities during initialization, so these values are not
evidence that a live bot can or cannot use a reachspec. A pinned live-spawn
observer is required before a capability-gated policy is considered.

The bot benchmark now emits that separate immutable observer artifact at
`bot-realized-capabilities.json` immediately after controlled bot spawn and
before the first simulation tick. It records each actual participant's class,
movement values, and realized capability bits without altering bot state or
benchmark telemetry. Two byte-identical UT436 four-bot captures SHA-256
`C2A729AF3F4608A59949287A298F0533EE2BA4D63454319CBE70738D922215B5`; two
Unreal Gold captures SHA-256
`45789E4D0F5D4943F237596F47AF50D8B3A64D37081DBDD39610ED9C6D45C8F0`.
Every observed participant can walk, jump, swim, open doors, and use special
traversal but cannot fly. Their realized JumpZ is 357.5, while class defaults
are 325. Any future reachspec-capability decision must use this realized record
for its exact participant/seed run, not a class-default approximation.

`Tools/BotBenchmark/Validate-RealizedBotCapabilities.py` makes that linkage
fail closed. It accepts only a complete roster-aware benchmark v2 run, requires
one ordered witness participant for every `summary.json.actual_roster` entry,
and requires exact identity, actor, and class agreement. It rejects unknown
fields, non-boolean capabilities, and non-finite or negative movement values.
The default matrix runner now requires this artifact and invokes the validator
alongside the normal quality analyzer. This establishes a trustworthy witness;
it does **not** yet make a reachspec-capability policy safe. A future policy
still needs a deterministic join to the catalog and route-execution evidence.

`Tools/BotBenchmark/Analyze-ReachspecCapabilities.py` is that first join. It
validates the v3 catalog and exact live witness, requires the catalog map to
match the benchmark URL, then reports raw reach-flag incidence together with
each actual bot's capability bits. It maps only the engine-defined direct
names `walk`, `fly`, `swim`, `jump`, `door`, and `special` to their observed
capability bits; `player_only` and unknown bits remain opaque. Its output is
explicitly `selection_safe: false` because a static audit cannot establish
reach-flag combination semantics, player eligibility, collision clearance,
current anchor, or dynamic mover state.

The anchor audits are negative evidence against a capability-filter change:
UT436 `DM-Deck16][` has 1,976 walk, 448 jump, and 21 special incidences across
1,997 reachspecs; Unreal Gold `DmDeathFan` has 1,415 walk, 324 jump, and 16
special incidences across 1,431. Neither map has fly, swim, door, player-only,
or unknown incidences, and no observed live participant lacks a capability
named by a reachspec. Therefore the current suicide/navigation failures cannot
be attributed to a visible static capability mismatch on these fixtures; do
not add a reachspec capability filter. The next useful evidence is a pinned
route-execution observer that records the actual current anchor, selected
reachspec, progress/stall window, and capability-compatible candidate set.

## Non-goals

Do not decode or redistribute meshes, textures, sounds, or compiled bytecode.
Do not write to commercial game directories. Do not treat a catalog as proof
that a behavior change is safe: it enables better fixtures and causal analysis,
which still require the normal deterministic cross-game quality gates.
