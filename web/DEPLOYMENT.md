# WebXR deployment shell

The installable entry point is `web/index_webxr.html?pwa=1&build=build-emscripten-nodata`.
The `pwa=1` registration seam is accepted automatically only with the known
redistributable `build-emscripten-nodata` output. The development preload must
use a separate origin/browser profile and must never be deployed.

Required response headers on every HTML, JavaScript, Wasm, worker, manifest,
icon, and service-worker response are:

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
Cross-Origin-Resource-Policy: same-origin
```

Serve over HTTPS (localhost is allowed for development). Serve `.wasm` as
`application/wasm`, `.js` as JavaScript, and `.webmanifest` as
`application/manifest+json`. Do not transform or append content to the Wasm or
generated JavaScript after release hashing.

`service-worker.js` uses an explicit allowlist. It precaches only the launcher,
bridge/importer scripts, manifest, offline page, and existing SurrealEngine
icons. It lazily caches only `SurrealEngine.js` and `SurrealEngine.wasm` from
`build-emscripten-nodata` (or a release `dist` directory). It blocks `.data`,
UE1 package/map extensions, and `/gamedata`; all other URLs are network-only.
Imported UT99 data remains solely in OPFS/IndexedDB and is outside the Cache
Storage/service-worker path.

Mutable configuration, keybindings, WebXR settings, saves, and last-run logs
use a separate local-only store with a strict path allowlist. See
`MUTABLE_DATA.md`; these records are also outside Cache Storage and must not be
exported into a release artifact.

Bump `APP_VERSION` in `service-worker.js` whenever shell/runtime compatibility
changes; the current integrated shell is `2026.07.22-m10.3`. Activation verifies the complete new shell cache before deleting older
`surrealengine-webxr-*` caches. HTML is network-first, so an online reload sees
updates; immutable no-data JS/Wasm is cache-first within that version.

Before publishing, verify the artifact contains no `.data`, UE1 packages, maps,
music, sounds, textures, install executables, or local import database exports.
Do not assemble the upload directory by hand. Stage a new release into a
nonexistent or empty directory; the stager writes deterministic hashes and runs
the independent fail-closed audit before reporting success:

```powershell
python -B web/stage_web_release.py --source-root . --runtime-directory build-emscripten-nodata --destination <new-empty-directory> --pretty
python -B web/audit_web_release.py <staged-directory> --pretty
```

The tools reject symlinks, commercial package/media extensions and paths,
`.data`, local import databases, generated preload markers, malformed or
duplicate runtimes, missing shell files, and unsafe runtime references. The
stager never deletes or overwrites an existing nonempty destination. Review the
generated manifest and third-party licenses independently before upload.

Run `python -B web/smoke_test_pwa.py` against a fresh
`node web/serve.mjs --port <port>` process so service-worker MIME and shell
changes are not hidden by an older server process. Also run:

```powershell
python -B web/smoke_test_ut99_importer.py
python -B web/smoke_test_mutable_persistence.py
python -B web/smoke_test_webxr_settings.py
python -B web/test_stage_web_release.py
python -B web/test_audit_web_release.py
```
