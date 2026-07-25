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
`surreal-map-catalog-spike-v1` JSON document. The current spike contains the
loaded actor count, navigation points with authored path indexes and flags,
reachspecs, and zone inventory/properties. It deliberately does not claim the
complete v1 schema or live zone membership yet.

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
`UnrealI`, and `UnrealShare` packages; a caller can instead provide an exact,
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

## Non-goals

Do not decode or redistribute meshes, textures, sounds, or compiled bytecode.
Do not write to commercial game directories. Do not treat a catalog as proof
that a behavior change is safe: it enables better fixtures and causal analysis,
which still require the normal deterministic cross-game quality gates.
