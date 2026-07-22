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
- `Object.GetConfig` reads the requested key from the System INI, allowing the
  menu's render-device snapshot decision to run.

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
- all final launches used `--nosound --noactivate`; audio was disabled and the
  foreground window did not change.

The generated QuickSave was moved to
`build/test-saves/QuickSave-20260722-102403`. `Save0001` was copied to
`build/test-saves/Save0001-20260722-105116` before deletion testing. Both paths
are ignored local artifacts and are recoverable during this work session.

## Remaining save work

- Complete and validate the menu-driven delete confirmation round trip.
- Implement save thumbnails (`RootWindow.SetSnapshotSize` and
  `RootWindow.GenerateSnapshot`) instead of displaying an empty preview.
- Complete list sorting, scrolling, column sizing, and edit operations beyond
  the paths exercised by Save Game.
- Exercise overwrite, QuickLoad UI, multiple numbered slots, invalid/corrupt
  metadata, low disk space, and saves across scripted map travel.
- Add focused tests for generic `ScriptArray` wrapping and for metadata package
  round trips that do not require proprietary game data.
