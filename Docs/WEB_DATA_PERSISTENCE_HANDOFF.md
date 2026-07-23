# Browser Data and Persistence Handoff

Date: 2026-07-22

## Scope and dependency

Branch: `pr/web-data-persistence`

This work is stacked only on `pr/web-platform-foundation` at `88980d62`. It
adds provider-neutral browser data deployment; it has no WebXR, OpenXR,
controller, session, stereo, or VR dependency.

Apply the commits after `88980d62` in order:

1. legal local UT99 import and safe map manifest/selection
2. crash-safe mutable persistence and schema migration
3. flat-browser bootstrap, import-gate UI, and deployment validation

## Source mapping

The implementation was extracted from the checkpointed `webxr-m1` worktree:

- `87f8324f` — local UT99 folder importer and synthetic tests
- `ca262715` — mutable-data overlay and importer-before-main ordering
- `e5e3e2e5` — immutable safe direct-map manifest
- `8dcd0985` — transactional version-1 to version-2 mutable migration

The following source-fork pieces were deliberately excluded: `index_webxr`,
the WebXR launcher, XR settings, session/controller code, VR documents, PWA
service-worker changes, and XR-specific robustness probes. Flat `index.html`
and `index_webgpu.html` now consume the extracted modules directly.

## Storage model

Imported game data and engine-owned mutable data are separate stores.

### Immutable local import

- Schema: `surrealengine-ut99-data`, version 1. The compatibility name is kept
  so browser profiles created by the earlier fork remain readable.
- OPFS is preferred; IndexedDB is the fallback.
- The user explicitly chooses a local installation folder. No network upload
  exists in the importer.
- Paths are canonicalized, traversal and case collisions are rejected, and a
  complete synthetic UT99 layout contract is validated before publication.
- A replacement import is fully saved before reload; it never mutates the live
  Emscripten filesystem in place.
- Redistributable no-data builds wait at this gate. Developer-preloaded builds
  bypass the store without reading it.
- Import failures are classified by the phase that failed (`storage-check`,
  `storage-copy`, `runtime-copy`, or `startup`) and by a stable error code.
  Quota, storage permission/interruption, unreadable-file, runtime-filesystem,
  and browser-memory failures therefore remain actionable. Diagnostics retain
  only the browser exception name; exception messages, selected paths, and file
  contents are never copied into the UI or log.

### Mutable overlay

- Schema: `surrealengine-mutable-data`, version 2.
- Format: `validated-copy-on-write`.
- Only `SE-UnrealTournament.ini`, `SE-User.ini`, `Settings.json`, the last-run
  log, and strict `Save<N>.usa` paths are eligible.
- Packages, maps, textures, sounds, music, executables, original imported INIs,
  traversal paths, and arbitrary save names fail closed.
- A version-1 dataset is validated and copied into a fresh generation. The
  current pointer changes only after all files are present. Interrupted OPFS
  and IndexedDB migrations leave version 1 authoritative and retry cleanly.
- Future schemas and corrupt pointers cannot be overwritten until the user
  explicitly clears only the mutable store.

### Boot order and map selection

`browser_data_bootstrap.js` enforces this sequence:

1. materialize validated immutable data at `/gamedata`;
2. restore the allowlisted mutable overlay;
3. select a safe direct map from the immutable manifest;
4. invoke the caller's native-main launch callback.

Mutable storage failure is nonfatal by default, so a valid user import remains
bootable. Imported-map selection accepts only `DM-`, `CTF-`, `DOM-`, or `AS-`
direct basenames. For imported datasets an unlisted preference falls back to
the first validated manifest entry. Developer-preloaded builds may use a
syntactically safe direct basename because no import metadata exists.

## Validation

All browser tests use synthetic strings, blobs, metadata, and fake filesystems.
No commercial files are read or bundled by the tests.

Exact browser commands (with `node web/serve.mjs 8094` running):

```text
python web\smoke_test_ut99_importer.py --base-url=http://localhost:8094
python web\smoke_test_mutable_persistence.py --base-url=http://localhost:8094
python web\smoke_test_browser_data_bootstrap.py --base-url=http://localhost:8094
python web\smoke_test_no_data_boot.py --base-url=http://localhost:8094
```

Exact no-data Emscripten build commands:

```powershell
. C:\Devstuff\emsdk\emsdk_env.ps1
emcmake cmake -S . -B build-emscripten -G "MinGW Makefiles" "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmake --build build-emscripten --target SurrealEngine --parallel 8
```

Exact native compatibility commands:

```text
cmake -S . -B build-native -G "Visual Studio 17 2022" -A x64 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmake --build build-native --config Release --target SurrealEngine --parallel
build-native\Release\SurrealEngine.exe --help
```

- Importer, safe-map, and privacy-safe failure-diagnostic checks: 21 passed.
- Mutable allowlist, recovery, and migration checks: 13 passed.
- Bootstrap ordering and nonfatal-storage checks: 2 passed.
- Fresh redistributable Emscripten configure/build/link with no game-data path:
  passed.
- No-data runtime smoke: both null/flat and WebGPU pages visibly stopped at
  `waiting-for-import`; native main remained unstarted; no crash or page error.
- Native Windows Release configure/build/link and `--help`: passed.
- `git diff --check`: passed.
- Scoped provider audit for XR API/provider terms: zero matches in the
  extracted modules, tests, and flat pages. This handoff names excluded
  providers only to record the scope boundary.

The development server on port 8094 was stopped after validation.

## Limitations and follow-ups

- A live WasmFS owner-data run proved that `FS.analyzePath` throws for the real
  `/gamedata/Save` directory while `FS.stat` and `FS.readdir` succeed. The
  current existence helper then skips valid `.usa` files. INI/log/Settings and
  explicit/pagehide/quit restoration passed, but save persistence did not.
  Commit `46d13173` adds the `FS.stat` fallback and throwing/false
  `analyzePath` regressions, including save round-trip and exclusion checks.
  The real owner-save matrix must still pass in the rebuilt artifact.
- Deus Ex save slots use nested `SaveNNNN/*.dxs` files and are not covered by
  the current flat UT/Unreal `.usa` allowlist. Add a typed Deus Ex policy rather
  than recursively widening the existing rule.
- Folder validation is currently UT99-specific. Unreal Gold and other UE1 games
  need separate typed validators rather than weakening this contract.
- This tranche has no PWA/offline cache shell or browser game-library UI.
- Imported map selection is safe and deterministic but the flat pages currently
  auto-select the preferred map or first manifest map; a richer launcher can
  present the same immutable manifest later.
- Browser eviction is still possible when persistent-storage permission is not
  granted; the importer reports quota and persistence diagnostics.
- Abrupt browser termination cannot guarantee an asynchronous final checkpoint.
  Explicit quit-and-flush remains the confirmed shutdown path.
