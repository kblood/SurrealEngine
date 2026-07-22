# Browser game-data mount investigation

This note records the measured cost of the current browser import and the
filesystem alternatives available in the installed Emscripten SDK. It is a
prototype result, not a change to the release data path.

## Current cost

The browser importer currently makes two complete runtime copies:

1. the selected `File` objects are copied into origin-private OPFS or
   IndexedDB storage;
2. every stored blob is streamed into `/gamedata` on Emscripten MEMFS before
   native startup.

The owner-supplied GOG UT99 installation used for local validation contains
496 files and 659,817,346 bytes (629.25 MiB). The second copy therefore commits
about 629 MiB of WebAssembly memory before package decoding, textures, audio,
the renderer, and the engine heap are considered. The current build starts at
256 MiB, allows memory growth, and inherits Emscripten's 2 GiB default maximum.
That maximum does not make the peak safe on a memory-constrained standalone
headset; the browser or OS may terminate the process first.

A replacement import is more expensive temporarily because the crash-safe
store publishes the new dataset before deleting the old one. This is correct
for durability, but it can briefly require two persistent datasets while the
old runtime MEMFS copy is still live.

## Installed SDK audit

The installed SDK is Emscripten 6.0.2 at revision
`7a2d97d627ff4945eae28847ce0387ac52b92c09`.

| Backend | Synchronous C/C++ reads | Long-lived content copy | Relevant limitation |
| --- | --- | --- | --- |
| MEMFS | Yes | Entire dataset in Wasm memory | Current 629 MiB runtime copy |
| IDBFS | Yes after `syncfs` | IndexedDB plus a MEMFS mirror | Does not solve memory duplication; unavailable under WasmFS |
| legacy WORKERFS | Yes | Reads requested slices from selected `File`/`Blob` objects | Worker-only, session-only unless the user selects again, and explicitly unsupported by WasmFS |
| WasmFS OPFS | Yes | One persistent OPFS copy; reads are serviced on demand | WasmFS is experimental and OPFS cannot safely be created on the browser main thread in the current pthread build |
| WasmFS JS file backend | Yes | Stores complete typed arrays in JavaScript memory | Moves the duplicate rather than removing it |
| WasmFS FetchFS | Yes through a worker | Range/chunk cache | Intended for server URLs, not private user-selected local files |

The relevant installed implementations and tests are:

- `src/lib/libworkerfs.js`: `FileReaderSync` over requested blob slices and a
  hard worker-environment requirement;
- `src/lib/libidbfs.js`: IndexedDB reconciliation against MEMFS;
- `src/lib/libopfs.js` and `src/lib/libwasmfs_opfs.js`: WasmFS OPFS bindings;
- `system/lib/wasmfs/backends/opfs_backend.cpp` and
  `system/lib/wasmfs/thread_utils.h`: dedicated-worker proxy and main-thread
  safety assertions;
- `test/fs/test_workerfs.c` and `test/wasmfs/wasmfs_opfs.c`: seek/read and
  persistent synchronous-I/O coverage.

## Proven OPFS primitive

`web/wasmfs_opfs_symlink_probe.cpp` is a synthetic standalone probe. With
`-sWASMFS -pthread -sPROXY_TO_PTHREAD`, it:

1. mounts the origin-private filesystem at `/opfs`;
2. creates a synthetic 32 MiB file there;
3. creates a small memory-backed `/gamedata/System` tree;
4. symlinks `/gamedata/System/Core.u` to the OPFS file; and
5. uses ordinary synchronous `stat`, `fopen`, `fseek`, and `fread` through the
   symlink.

From an Emscripten environment, reproduce the standalone build with:

```powershell
em++ web/wasmfs_opfs_symlink_probe.cpp -O2 -sWASMFS -pthread `
  -sPROXY_TO_PTHREAD -sPTHREAD_POOL_SIZE=2 -sEXIT_RUNTIME=1 `
  -sASSERTIONS=1 -o build-wasmfs-probe/index.html
node web/serve.mjs 49001 build-wasmfs-probe
```

Chrome completed the test with a 16 MiB Wasm heap before and after the 32 MiB
file operation: zero heap growth. A complete Surreal Engine build also
compiled and linked with:

```text
-sWASMFS -sFORCE_FILESYSTEM -pthread -sPROXY_TO_PTHREAD
```

Both flat and WebGPU no-data gates passed, and the current JavaScript importer
successfully materialized a synthetic nine-file dataset after
`-sFORCE_FILESYSTEM` restored the full `Module.FS` compatibility API.

This proves the storage and libc mechanism, not the final engine threading
model. `PROXY_TO_PTHREAD` moves `main()` to a worker, while the current WebXR
provider calls exported render/input/frame functions from the browser XR frame
callback. Those calls and WebGPU presentation ownership must be deliberately
routed before the worker build can be considered correct.

An attempted `-pthread -sASYNCIFY` main-thread OPFS probe failed the installed
SDK assertion in `ProxyWorker`: the pthread implementation still tries to
create its synchronous OPFS proxy from the browser main thread. The SDK's
working Asyncify/JSPI OPFS tests are non-pthread builds. Therefore merely
adding Asyncify to the present threaded engine is not an alternative.

## Recommended persistent layout

The lowest-memory persistent design is:

```text
selected folder -> validated copy in OPFS
                              |
                              v
                  WasmFS OPFS mount (read on demand)
                              |
                     immutable symlink tree
                         at /gamedata
                              +
             existing allowlisted mutable MEMFS overlay
```

The `/gamedata` directories and symlinks consume only metadata. Immutable
packages remain in OPFS. Existing engine configuration, logs, and save slots
remain memory-backed and are checkpointed through the current allowlist. This
avoids mounting imported OPFS read-write as the game root, which would allow
arbitrary game/script writes to bypass the persistence allowlist.

The current OPFS layout can be reused. Metadata already identifies the
published dataset generation, so the symlink targets can point at that exact
generation. A re-import can continue using publish-then-delete semantics.

## Direct selected-folder mode

WORKERFS can expose canonical-path blobs directly and read only requested
slices. This avoids both the OPFS copy and MEMFS copy, but it is not a drop-in
website persistence solution:

- it must execute in a worker because it uses `FileReaderSync`;
- ordinary file-input selections cannot be reopened after a reload without
  another user gesture;
- transferring selected file objects to an already pooled engine worker needs
  an explicit message/ownership bridge; and
- WORKERFS cannot be combined with WasmFS.

It remains a useful future session-only mode and is especially appropriate for
an Electron host, where a worker or Node-backed filesystem can retain direct
host-disk access. It should not block the persistent OPFS work.

## Safe filtering result

The installed UT99 configuration names only these package paths:

- `System/*.u`
- `Maps/*.unr`
- `Textures/*.utx`
- `Sounds/*.uax`
- `Music/*.umx`

Engine startup also needs the selected system INI/localization files and one
recognized executable for version detection. Retaining the five content
directories plus `System/*.u`, `System/*.ini`, `System/*.int`, and the detected
game executable reduces the measured GOG dataset to:

| Set | Files | Bytes | MiB |
| --- | ---: | ---: | ---: |
| Selected folder | 496 | 659,817,346 | 629.25 |
| Conservative runtime set | 324 | 593,068,518 | 565.59 |
| Excluded | 172 | 66,748,828 | 63.66 |

The excluded 10.12% consists of manuals, installer/download archives, web
administration assets, root GOG support files, native renderer/audio DLLs,
extra executables, splash/icon/log/readme files, and similar host-only data.
No commercial contents or local paths are stored in this repository.

Filtering is worthwhile but cannot solve the headset memory problem by itself:
565.59 MiB is still copied into MEMFS by the current design. It should be a
separate conservative change with retail UT99, Unreal Gold, and demo coverage;
unknown game-specific raw media must remain fail-safe rather than be silently
discarded.

## Integration order and gates

1. Add an opt-in WasmFS build variant; keep the current release build intact.
2. Establish one owner for browser execution. Either route WebXR/WebGPU calls
   to a worker-owned engine, or build a proven non-pthread browser variant that
   can use WasmFS's Asyncify/JSPI OPFS path.
3. Mount OPFS before importer restoration, create only directory/symlink
   metadata in `/gamedata`, and preserve the mutable allowlist.
4. Verify startup detection, directory iteration, random package seek/read,
   configuration overlay, saves, re-import, clear, and crash recovery.
5. Measure Wasm heap and browser process memory during UT99 and Unreal Gold
   startup, map travel, texture load, audio, flat WebGPU, and both WebXR paths.
6. Run the physical Quest matrix before replacing the MEMFS release path.

Do not raise `MAXIMUM_MEMORY` as the primary fix. That changes the failure
ceiling but preserves the unnecessary full-data allocation and its headset
pressure.
