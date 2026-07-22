# Deus Ex save-system findings

This document records the observed Deus Ex 1112fm save lifecycle, the Surreal
Engine implementation, failures encountered during validation, and remaining
work. Proprietary game data and generated save packages are not committed.

## Slot and file layout

Deus Ex uses signed game indices:

| Input | Meaning | Directory |
| ---: | --- | --- |
| `-1` | QuickSave | `Save/QuickSave` |
| `0` | Allocate the first unused numbered slot | `Save/Save0001` onward |
| `1` through `9999` | Explicit numbered slot | `Save/Save0001` through `Save/Save9999` |

Each validated slot contains the current map package, such as
`01_NYC_UNATCOIsland.dxs`, and `SaveInfo.dxs`. The metadata package exports
`MyDeusExSaveInfo` as class `DeusEx.DeusExSaveInfo` and stores the description,
map name, play time, save count, timestamp, and cheat state.

The stock scripts use `?loadgame=<index>`. `?load=<index>` remains accepted as a
compatibility alias in Surreal Engine, but validation and documentation use the
game's actual spelling.

## Implemented lifecycle

- The package manager discovers and refreshes `SaveInfo.dxs` packages and can
  create a new metadata package using the active package-version fields.
- slot `0` allocates the first free numbered directory; slot `-1` maps to
  QuickSave without passing through unsigned formatting.
- startup URLs with `?loadgame=` resolve the metadata map name and load its
  `.dxs` package directly.
- explicit `DeleteGame <slot>` removes only the resolved numbered or QuickSave
  directory. Path validation rejects traversal and broad directory targets.
- `GameDirectory.DeleteSaveInfo` and `PurgeAllSaveInfo` release list ownership;
  they do not delete files. The stock menu performs disk deletion separately
  through `ConsoleCommand("DeleteGame " $ slot)`.
- edit controls implement insertion, selection, deletion, changed-state flags,
  maximum length, single-line activation, and bubbling `TextChanged` and
  `EditActivated` notifications. This enables descriptions and the Save button.
- list controls implement row focus and selection, mouse hit testing, keyboard
  movement, paging, scrolling the focus row into view, field margins, selection
  notifications, and selected-row rendering. This enables existing-slot
  selection for overwrite and deletion.
- `Object.GetConfig` reads the requested key from the System INI, allowing the
  menu's render-device snapshot decision to run.
- `RootWindow.GenerateSnapshot` captures the active viewport, center-crops it to
  the requested aspect ratio, optionally filters it, and creates a transient
  BGRA8 texture. Saving clones that texture into `SaveInfo.dxs` so it survives
  the transient package.

## Failures and solutions

### Incorrect zone count encoding

The level saver wrote the zone count as a compact Unreal index while the loader
read a fixed 32-bit integer. Every following field shifted, eventually reporting
that a `Level` object could not be converted to `Actor`. The writer now uses the
same fixed 32-bit representation as the reader.

### Invalid metadata package class

New saves initially reused an unrelated package/class context. New metadata now
uses a fresh save package and imports `DeusEx.DeusExSaveInfo` for the
`MyDeusExSaveInfo` export.

### Save screen native gaps

The menu initially stopped at missing edit/list behavior, then at
`Object.GetConfig`. Those native contracts are implemented. A later null focus
ancestor walk was identified from a minidump and made safe.

### Existing-slot list crash

The dynamic-array accessor returned a wrapper by reinterpreting the bytes of a
`ScriptArray`. This made the wrapper's array pointer equal the array's element
type pointer. Empty lists appeared to work, but the first `push_back` called
`Reserve` through that invalid address and crashed. `DynamicArray<T>` now
constructs a `TypedScriptArray<T>` around the actual property-data address.

### Windows deletion sharing violation

Deleting the active slot initially failed because both its saved map and
`SaveInfo.dxs` still had open package streams. Before removing a slot, the
package manager now fully materializes packages backed by that directory and
closes package streams. The running game remains independent of the removed
files, and Windows can remove the directory.

### Action-button character crash

Pressing Enter on an overwrite/delete action button reached the root character
handler, which unconditionally cast the focused control to `EditWindow`. Action
buttons are not edit controls. The handler now uses a checked cast and leaves
non-edit character handling to the focused control.

### Reordered package tables corrupted overwrites

The first map overwrite rebuilt its name and object tables in dependency
discovery order. Native and unchanged serialized bytes still contained indices
from the source package, so index 3056 changed from `UNATCO` to `Player` and a
fresh load eventually reported `Unexpected end of file`. The writer now keeps
all source name, export, and import entries at their original indices and
appends only new dependencies. Resolvable imports are mapped back to their old
references; the validated map added four imports instead of duplicating more
than 500.

### Boolean struct members were omitted

After preserving the package tables, reload diagnostics identified the next
failure precisely as `AllianceTrigger0` at byte 108 of 108. Its
`InitialAllianceInfo` value contains a one-byte `bPermanent` member. Struct
loading read that byte, but nested serialization called the tagged-boolean
writer, which correctly emits no separate payload for a tagged property and
therefore made every struct value one byte short. Nested struct, dynamic-array,
and fixed-array elements now use a separate member serializer; booleans emit a
zero or one byte there. Object-stream EOF errors also include the package,
object, position, size, and requested read size for future diagnosis.

### Transient conversation lists were not rebuilt

A Training QuickSave restored the world correctly, but crossing the next
trigger displayed `INFOLINK NOT FOUND!! Name = dl_start`. The stock
`Actor.ConListItems` property is transient, so omitting it from the saved
package is correct. Fresh actors reconstruct it through the native
`ConBindEvents` calls in the `PostPostBeginPlay` implementations for
`DeusExPlayer`, `ScriptedPawn`, and `DeusExDecoration`; loaded actors do not run
that initialization path.

Save loading now invokes the common conversation-binding implementation for
those three actor class families after the level and actors have been restored.
It deliberately does not replay `PostPostBeginPlay`, which contains unrelated
initialization with the potential to overwrite valid saved state. Runtime
validation rebuilt 56 bindings and resolved `dl_start` to the intended Jaime
Reyes transmission.

## Validated on 2026-07-22

- QuickSave created an approximately 11 MB Liberty Island map package and a
  metadata package; direct `?loadgame=-1` restored it.
- The Save Game menu accepted the description `Codex Enter Save`, enabled the
  action through `TextChanged`, and created `Save0001` with an 11,177,964-byte
  map package and a 485-byte `SaveInfo.dxs`.
- direct `?loadgame=1` remained responsive and rendered the restored Liberty
  Island world, HUD, inventory belt, pistol, and live actors.
- reopening Save Game with the existing slot now renders `Codex Enter Save` in
  the list after fixing the dynamic-array accessor.
- menu-driven deletion removed `Save0001` while the loaded game remained
  responsive. The archived slot was then restored and loaded successfully.
- menu-driven overwrite produced an 11,175,330-byte map and a 77,489-byte
  metadata package. A fresh process loaded the overwritten map, reached the
  Liberty Island mission state machine, and remained responsive.
- the serialized 160-by-120 snapshot reopened in the Save Game preview with
  the expected orientation and colors. The metadata growth from 485 to 77,489
  bytes is consistent with the BGRA thumbnail plus package data.
- the Liberty Island QuickSave was preserved in an ignored, hash-verified local
  archive before the stock slot was reused for Training. A fresh Training
  QuickLoad restored the open stair mover, player, inventory, HUD, and the
  post-checkpoint conversation binding.
- all final launches used `--nosound --noactivate`; audio was disabled and the
  game window was never observed as the foreground window.

Generated QuickSave, pre-deletion, failed-overwrite, and successful-round-trip
packages are retained under ignored `build/test-saves` directories. They are
local diagnostic/recovery artifacts and are not part of the repository.

## Remaining save work

- Complete list sorting, column resizing, double-click activation, and edit
  operations beyond the paths exercised by Save Game.
- Exercise QuickLoad UI, multiple numbered slots, invalid/corrupt
  metadata, low disk space, and saves across scripted map travel.
- Add focused tests for generic `ScriptArray` wrapping and for metadata package
  and nested-property round trips that do not require proprietary game data.
