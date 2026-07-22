# WebXR local map picker

## Scope

The opt-in browser launcher (`?launcher=1`) can browse compatible map packages
from the user's validated local UT99 import. Manual map entry remains the
authoritative launch field and the existing fixed offline-only game presets
remain unchanged. The default developer and smoke-test route still boots
automatically and never waits for map enumeration.

This is local launch metadata UX. It does not read package contents, advertise
multiplayer, scan arbitrary browser storage, or claim that every third-party
game type is compatible with the browser MVP.

## Importer contract

`ImportController.mapManifest()` returns schema
`surrealengine-ut99-map-manifest` version 1. Each call creates a distinct frozen
snapshot with a frozen `maps` array. The snapshot contains only:

- `state`: `ready`, `empty`, `unavailable`, `cleared`, or `error`;
- safe direct map-package basenames with the `.unr` extension removed; and
- an aggregate rejected-candidate count.

It never exposes dataset IDs, creation timestamps, storage paths, relative
file paths, sizes, blobs, package contents, or the full importer metadata.
Candidates are projected only from metadata which already passed the
importer's schema, layout, size, traversal, and case-collision validation.
There is no additional OPFS, IndexedDB, or MEMFS walk.

The controller updates the snapshot after a persisted import is successfully
materialized, after a first import is materialized, and after saved data is
cleared. Developer-preloaded builds report `unavailable`; a valid no-data
profile with no saved import reports `empty`; corrupt/unavailable saved
metadata reports `error`. Map projection failure is nonfatal and never opens or
closes the engine boot gate.

## Allowlist and ordering

Only a direct `Maps/<basename>.unr` metadata record can enter the manifest.
The basename must pass the same conservative launcher grammar:

```text
(DM|CTF|DOM|AS)-[A-Za-z0-9][A-Za-z0-9_\-\[\]']{0,63}
```

Nested paths, path separators, `.unr` in the returned value, query or fragment
delimiters, percent escapes, whitespace, network-like targets, and other game
prefixes are rejected. Accepted names are deduplicated case-insensitively and
sorted by an explicit ASCII case-fold order. The launcher independently
revalidates, deduplicates, and sorts the importer snapshot before creating DOM
options; all text is assigned through `textContent`/`value`.

Selecting an option copies its basename into the still-visible validated manual
field. `buildLocalSelection()` performs the final validation and constructs
only the existing fixed local URL, such as:

```text
DM-Morpheus?game=Botpack.DeathMatchPlus
```

No arbitrary Unreal URL option, server address, `listen` flag, or custom game
class can be supplied through the picker.

## UX states and failure behavior

- `refreshing`: picker and refresh action are temporarily disabled;
- `ready`: compatible imported basenames are browsable;
- `empty`: no compatible direct map was found;
- `unavailable`: developer preload or absent importer contract;
- `cleared`: the saved import was removed; and
- `error`: the provider threw or returned an invalid schema/state.

Every non-ready state presents a short explanation and keeps validated manual
entry plus Start enabled once game data itself is ready. Refresh uses a
generation counter, so a stale asynchronous result cannot overwrite a newer
request. Automatic mode returns before calling the provider at all.

The enumerated inventory, rejected names, importer metadata, and relative
paths are never added to downloadable/copyable diagnostics or recent logs. The
existing schema-v1 diagnostic field containing the single selected
`launcher.map` is preserved intentionally.

## Deterministic evidence

The following passed locally on 2026-07-22:

```powershell
node --check web/ut99_importer.js
node --check web/webxr_launcher.js
python -B web/smoke_test_ut99_importer.py
python -B web/test_webxr_launcher.py
python -B -u web/smoke_test_webxr.py
```

Importer coverage is 14 checks. It proves immutable copy snapshots,
first-import and restored-import updates, clear behavior, direct-map filtering,
case-fold ordering, and rejection of nested, query-like, and unsupported map
candidates using synthetic files only.

Launcher coverage is 44 checks. It proves automatic mode makes zero manifest
calls; explicit mode waits; picker selection feeds the validated field; a
malformed provider is revalidated; empty/unavailable/error states retain manual
launch; stale refresh completion cannot replace newer state; refresh recovers;
fixed offline arguments reach `callMain`; and the enumerated inventory is
absent from diagnostics.

The existing full IWER smoke also passed without changing automatic boot. A
real data-backed developer-preload page at localhost correctly reported the
picker as unavailable, retained manual Start, then launched Deck and advanced
to 62 ticks with no crash or page error. That is developer/browser evidence,
not a real user-import or Quest usability result.

## Remaining integration and product gates

- Bump the service-worker `APP_VERSION` and rerun PWA/offline/release checks so
  installed clients atomically receive the mutually compatible importer,
  launcher, and HTML shell. No new production file needs a release allowlist.
- Exercise a legitimate full-size user-owned OPFS and IndexedDB import without
  recording or publishing its map inventory.
- Test large map inventories, refresh and clear on Quest Browser, keyboard and
  controller accessibility, and browser/OS restart persistence.
- Decide separately whether additional local game prefixes/game classes can be
  supported. They must not be admitted by weakening the generic URL grammar.
