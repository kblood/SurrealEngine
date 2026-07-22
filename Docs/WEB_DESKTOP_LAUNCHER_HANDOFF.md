# Shared Browser Launcher Handoff

Date: 2026-07-22

## Scope and dependency

Branch: `pr/web-desktop-launcher`

This branch is stacked only on `pr/web-data-persistence` at `2dda89bd`.
It is a flat-browser application and contains no immersive-session API,
headset/controller UI, stereo policy, or provider implementation. A later
presentation provider can register with the same launcher without forking the
import, persistence, game-library, or native argument code.

No game files are included. All automated fixtures are synthetic strings and
Blobs.

## Resulting layers

1. `ut99_importer.js` remains the compatibility filename/global and storage
   schema, but now strictly detects either UT99 or Unreal Gold. New callers use
   its `SurrealGameImporter` alias.
2. `mutable_persistence.js` remains the engine-owned writable overlay.
3. `browser_data_bootstrap.js` enforces immutable import, per-game mutable
   restore, explicit launch selection, then native `main()`.
4. `browser_app.js` owns the game library, launcher UI model, presentation
   registry, safe native arguments, WebGPU acquisition, and runtime loading.
5. `surreal_app.html` is the shared application shell. A specialized page can
   reuse all four modules and CSS while registering another presentation mode.

The older `index.html` and `index_webgpu.html` remain deterministic development
harnesses. They do not need to adopt the product launcher to keep testing the
engine seams beneath it.

## Games and browser library

The validators do not infer compatibility from a single file. UT99 still
requires its executable, INI, `Core.u`, `Engine.u`, `Botpack.u`, and non-empty
Maps/Textures/Sounds/Music directories. Unreal Gold requires `Unreal.exe`,
`Unreal.ini`, `Core.u`, `Engine.u`, `UnrealShare.u`, `UnrealI.u`, and the same
content-directory classes.

Schema version 1 was deliberately retained. Existing metadata without a
`gameId` is interpreted as UT99, so deployed browser profiles remain readable.
New metadata adds `gameId`. UT99 continues using the original storage namespace;
other games receive isolated immutable and mutable namespaces.

The library records only supported game IDs and the active game in localStorage.
The commercial files remain in OPFS or IndexedDB. “Add or replace a game” opens
an empty import gate; validation selects the correct per-game store. Switching
games reloads before materialization because replacing `/gamedata` beneath a
running engine would be unsafe.

UT maps keep the narrow `DM-`, `CTF-`, `DOM-`, and `AS-` launcher allowlist.
Unreal Gold accepts safe direct package basenames such as `Vortex2`; paths,
extensions, URL syntax, nested names, and traversal remain forbidden.

## Presentation-provider boundary

`PresentationRegistry.register()` accepts only:

```js
{
  id: "provider-id",
  label: "User-facing label",
  isAvailable: () => true,
  prepareLaunch: async ({ context, selection }) => {}
}
```

The shared app registers `flat`. A later provider supplies another entry from
its own page or script. `prepareLaunch` runs only after the user presses Play
and before `Module.callMain()`. Provider state never enters import metadata or
mutable-data schemas. The native argument builder remains fixed to a validated
direct map, `webgpu` or diagnostic `null`, and `/gamedata`.

## Electron wrapper reuse

Electron should load `surreal_app.html` unchanged. Keep `contextIsolation: true`,
`nodeIntegration: false`, and expose only this preload contract:

```js
contextBridge.exposeInMainWorld("SurrealHostBridge", {
  pickGameDirectory: () => ipcRenderer.invoke("surreal:pick-game-directory"),
  readGameFile: token => ipcRenderer.invoke("surreal:read-game-file", token),
});
```

`pickGameDirectory` returns either `null` for cancel or descriptors shaped as
`{ path, size, token }`. `path` is relative to the user-approved root and
`token` is opaque. `readGameFile(token)` returns an ArrayBuffer for that one
file. The main process must retain the approved absolute root privately, map
random session tokens to resolved files under that root, reject traversal and
symlinks escaping it, invalidate tokens when the window closes, and never
accept arbitrary paths from the renderer. Lazy reads avoid copying an entire
installation through IPC at once.

When the browser supports `showDirectoryPicker`, no bridge is needed. The
shared importer otherwise keeps its `webkitdirectory` folder-upload fallback.
Thus hosted Chromium, an Electron wrapper, and future presentation-specific
pages use the same validation, storage, launcher, WASM, and engine assets.

## Validation

With `node web/serve.mjs 8107` running:

```text
python web\smoke_test_ut99_importer.py --base-url=http://localhost:8107
python web\smoke_test_mutable_persistence.py --base-url=http://localhost:8107
python web\smoke_test_browser_data_bootstrap.py --base-url=http://localhost:8107
python web\smoke_test_browser_app.py --base-url=http://localhost:8107
python web\smoke_test_browser_app_runtime.py --base-url=http://localhost:8107
```

Current deterministic results:

- importer and game detection: 19 passed;
- mutable persistence and migration: 13 passed;
- boot ordering and per-game mutable isolation: 3 passed;
- launcher/library/provider/host bridge: 5 passed.
- a fresh no-data Emscripten build reached the product import gate, then a
  synthetic Unreal Gold import produced the exact validated launch argument
  vector without invoking the engine on fake data.

Also run `node --check` on the four JavaScript modules and `git diff --check`.
The runtime page still needs a generated no-data Emscripten build at
`build-emscripten/` for an end-to-end engine boot.

## Deliberate follow-ups

- The library supports UT99 and Unreal Gold only. Add each later UE1 title as a
  typed definition; do not weaken either existing validator.
- PWA installation/offline caching is not in this tranche. A service worker
  should cache only the redistributable shell/WASM artifacts and must never
  enumerate or cache imported commercial OPFS/IndexedDB data.
- A wrapper can add richer native library discovery behind the same bridge,
  but the user must still explicitly approve each game root.
