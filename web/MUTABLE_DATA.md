# Web mutable-data persistence

The Emscripten runtime uses MEMFS, so writes disappear when the tab closes.
`mutable_persistence.js` snapshots only files the current UT99 Web build is
known to mutate and restores them after `/gamedata` is materialized but before
`Module.callMain()` reads configuration or enumerates saves.

## Audited write paths

The allowlist follows the current engine code:

- `PackageManager::SaveAllIniFiles()` writes
  `/gamedata/System/SE-UnrealTournament.ini` and
  `/gamedata/System/SE-User.ini`. Keybindings and `[Engine.WebXR]` settings are
  stored in `SE-User.ini`.
- UT99's default `SavePath=../Save` and `Engine::SaveGameToSlot()` produce
  `/gamedata/Save/Save<N>.usa`.
- `LauncherSettings` uses
  `/home/web_user/.config/SurrealEngine/Settings.json` under Emscripten's
  default `HOME`.
- The engine's last-run logger targets
  `/home/web_user/.config/SurrealEngine/SE-Log-LastRun.txt`.
  Because the Web engine has static lifetime, the page also mirrors its
  browser/stdout diagnostic log to that path immediately before each
  checkpoint.

No other path is eligible. The snapshotter does not recursively walk
`/gamedata`; it checks the four exact files above and admits only the strict
`Save<N>.usa` name pattern from `/gamedata/Save`. Original game INIs, `.u`,
`.unr`, `.utx`, `.uax`, `.umx`, executable, cache, and arbitrary `.usa` files
are excluded and rejected again when stored metadata is loaded.

## Storage and APIs

The independent `surrealengine-mutable-data` schema is version 1. OPFS is
preferred; IndexedDB is the fallback. A staged OPFS snapshot is published by
replacing `current.json` only after every file is written. IndexedDB publishes
files and metadata in one transaction.

After boot, the page exposes:

- `surrealGetMutableDataStatus()` — backend, state, schema, counts, last
  checkpoint, and any actionable restore/flush error.
- `surrealFlushMutableData(reason)` — serialize an immediate checkpoint.
- `surrealClearMutableData()` — clear only the mutable-data store. It neither
  deletes live MEMFS files nor calls the UT99 importer's clear operation, and
  pauses automatic checkpoints for the rest of that session.
- `surrealRequestQuitAndFlush()` — request the engine's clean shutdown, wait
  for native INI/log writes, then checkpoint them.

Automatic checkpoints run every 30 seconds and on hidden/pagehide lifecycle
events. They are best effort because browsers do not guarantee completion of
asynchronous storage work during abrupt process termination. Use the explicit
quit-and-flush API for a confirmed final checkpoint.

Restore failures are nonfatal: boot continues with the materialized imported
baseline and diagnostics report `restore-failed`. Automatic and explicit
flushes remain blocked until schema or integrity errors are cleared, preventing
an older client from overwriting an incompatible newer snapshot. Imported game
data remains separate.

Run `python web/smoke_test_mutable_persistence.py` while `node web/serve.mjs`
is serving the repository.
