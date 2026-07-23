# Web mutable-data persistence

The default Emscripten runtime uses MEMFS, so writes disappear when the tab
closes. `mutable_persistence.js` snapshots only files the current UT99 Web build
is known to mutate and restores them after immutable game data is prepared but
before `Module.callMain()` reads configuration or enumerates saves. Ordinary
builds materialize `/gamedata`; the experimental WasmFS variant registers an
OPFS mount that native startup creates after the overlay has been restored.

## Audited write paths

The allowlist follows the current engine code:

- `PackageManager::SaveAllIniFiles()` writes
  `/gamedata/System/SE-UnrealTournament.ini` and
  `/gamedata/System/SE-User.ini`. Keybindings and engine extension settings are
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

### Qualified WasmFS defect and correction

The 2026-07-23 live owner-data matrix proved that the allowlist classification
is correct but the production snapshot walk is not yet reliable under WasmFS.
`FS.analyzePath("/gamedata/Save")` throws an `ErrnoError` for the real Save
directory even though `FS.stat`, `FS.readdir`, and direct file reads work. The
current `fsExists` helper treats that exception as absence, so a valid
`Save99.usa` was omitted from both the snapshot entries and stored metadata.
INI, Settings, last-run log, explicit flush, pagehide, clean quit, and restore
passed; real save persistence did not.

Commit `46d13173` makes `analyzePath` a positive-only fast path and falls back
to `FS.stat` whenever it throws or reports false. Sixteen deterministic browser
checks now include WasmFS-like throwing and false results while `stat`,
`readdir`, and reads succeed; `Save99.usa` checkpoints and restores, a
disallowed package stays excluded, and a genuinely absent directory remains
optional. Real UT99 and Unreal `.usa` round trips must still be repeated in the
rebuilt shipping artifact before save persistence is release-qualified. Deus
Ex uses nested `SaveNNNN/*.dxs` data and is outside the current flat allowlist;
it needs a separate typed path policy rather than a broad recursive exception.
The cross-blocker second-opinion review is in
`../Docs/CLAUDE_OPUS_RUNTIME_BLOCKER_ANALYSIS.md`.

## Storage and APIs

The independent `surrealengine-mutable-data` schema is version 2. The deployed
version-1 schema remains the one supported migration source. The physical
OPFS directory and IndexedDB database retain their historical
`surrealengine-mutable-data-v1` names so an existing browser profile can be
found and upgraded in place; those names are namespace identifiers, not the
current metadata version.

OPFS is preferred; IndexedDB is the fallback. A staged OPFS snapshot is
published by replacing `current.json` only after every file is written.
IndexedDB publishes files and metadata in one transaction.

On first load of version 1, the runtime validates the metadata allowlist and
sizes, copies only those mutable files into a fresh version-2 generation, and
publishes that generation with `format: "validated-copy-on-write"`. OPFS keeps
the version-1 `current.json` pointer authoritative until the new snapshot is
complete. IndexedDB copies the files and switches `current` in one read/write
transaction. Cleanup of the old generation happens only after publication and
is best effort. A crash before publication therefore leaves version 1 intact;
the next load retries. A crash after publication sees a complete version-2
generation. Loading version 2 again is idempotent and does not republish it.

Migration diagnostics report backend, state, source/target versions, strategy,
file count, byte count, and an error code. Future versions, malformed metadata,
disallowed paths, missing snapshots, and incomplete files fail closed. Save
does not silently replace a future or corrupt current pointer; the explicit
mutable-only clear remains required. The migration never enumerates the UT99
import store and never reads package, map, texture, sound, music, executable,
or original imported INI content.

The mutable controller implements these operations:

- `surrealGetMutableDataStatus()` — backend, state, schema, counts, last
  checkpoint, deterministic migration diagnostics, and any actionable
  restore/flush error.
- `surrealFlushMutableData(reason)` — serialize an immediate checkpoint.
- `surrealClearMutableData()` — clear only the mutable-data store. It neither
  deletes live MEMFS files nor calls the UT99 importer's clear operation, and
  pauses automatic checkpoints for the rest of that session.
- `surrealRequestQuitAndFlush()` — request the engine's clean shutdown, wait
  for native INI/log writes, then checkpoint them.

The production launcher exposes the controller as
`window.surrealApp.dataController`; it does not currently install the four
convenience globals named above. Tests and troubleshooting should call the
controller methods unless/until a deliberate public wrapper API is added.

Automatic checkpoints run every 30 seconds and on hidden/pagehide lifecycle
events. They are best effort because browsers do not guarantee completion of
asynchronous storage work during abrupt process termination. Use the explicit
quit-and-flush API for a confirmed final checkpoint.

If the WebAssembly runtime aborts, the launcher records
`window.surrealCrashed` and publishes `surrealruntimeabort`. Automatic
checkpoints stop immediately and explicit flushes reject with
`RUNTIME_ABORTED`; reading the Emscripten filesystem after an abort is not
safe. The already-published browser snapshot remains available after reload.

Restore failures are nonfatal: boot continues with the materialized imported
baseline and diagnostics report `restore-failed`. Automatic and explicit
flushes remain blocked until schema or integrity errors are cleared, preventing
an older client from overwriting an incompatible newer snapshot. Imported game
data remains separate.

Run `python web/smoke_test_mutable_persistence.py` while `node web/serve.mjs`
is serving the repository. Its deterministic browser checks seed real v1 OPFS
and IndexedDB metadata, prove success and idempotence, interrupt each backend
before publication, verify that the old pointer remains current, and retry.
The synthetic test page requires no commercial data and covers both storage
backends when the browser exposes them.
