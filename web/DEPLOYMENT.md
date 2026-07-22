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
changes. Activation verifies the complete new shell cache before deleting older
`surrealengine-webxr-*` caches. HTML is network-first, so an online reload sees
updates; immutable no-data JS/Wasm is cache-first within that version.

Before publishing, verify the artifact contains no `.data`, UE1 packages, maps,
music, sounds, textures, install executables, or local import database exports.
Run `python web/smoke_test_pwa.py` against `node web/serve.mjs`.
