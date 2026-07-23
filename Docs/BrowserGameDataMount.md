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

## Measured presentation ownership boundary

The worker build has two separate presentation results. They must not be
reported as one generic "browser rendering" result.

### Null renderer: narrow worker-safe seam

`LauncherSettings` selects `NullRenderDevice` by default on Emscripten. That
device uses `RenderAPI::Bitmap` to create the game window, but immediately
replaces the temporary `BitmapCanvas` with a no-op `RenderDeviceCanvas` and
never presents bitmap pixels. SDL nevertheless used
`SDL_CreateWindowAndRenderer`, which creates its internal GLES shaders. With
`PROXY_TO_PTHREAD`, the browser main realm owned SDL's GL context table while
the shader call ran in the proxied worker. The exact standalone reproduction
failed in `_emscripten_glCreateShader` while reading an undefined context.

`-sOFFSCREEN_FRAMEBUFFER` did not change that failure.
`-sOFFSCREENCANVAS_SUPPORT -sOFFSCREEN_FRAMEBUFFER` stalled Chrome in the
installed SDK/browser combination. Transferring the product canvas to that
worker would also conflict with the current main-realm WebXR bridge, which
creates the XR layer and drives exported frame calls from `requestAnimationFrame`.

The opt-in `SURREAL_WEB_EXPERIMENTAL_PROXY_TO_PTHREAD` build therefore uses
`SDL_CreateWindow` (window/input only) for the worker Null/Bitmap path. It does
not create an SDL renderer or GL context. The default option is `OFF`, so
desktop and current browser builds retain their existing bitmap presentation.
The focused probe reports:

```text
PASS pthread-sdl-presentation worker=1 renderer=0 gl-context=0
```

The full opt-in engine compiles at 256 MiB and both flat and WebGPU product
pages reach the legal no-data import gate. The shared launcher also retains
its Unreal Gold selection and WebGPU native arguments. A full game startup
still requires owner-supplied UE1 data; those gate checks are not substitutes
for a rendered map test.

### WebGPU: browser device does not cross into the worker

The current launcher asynchronously creates a `GPUDevice` in the browser main
realm, stores it as `Module.preinitializedWebGPUDevice`, and then calls native
`main()`. The installed emdawnwebgpu port imports that JavaScript object from
the calling realm's `Module` and keeps realm-local JavaScript object tables.
The `PROXY_TO_PTHREAD` load message does not copy arbitrary `Module` objects to
the pthread worker.

`web/pthread_webgpu_device_probe.cpp` reproduces the product handoff without
commercial game data. Chrome exposes `navigator.gpu` in the worker, but the
main-realm preinitialized device is absent there:

```text
INFO pthread-webgpu-device worker-navigator-gpu=1
FAIL browser-thread preinitialized WebGPU device is absent in the proxied main worker
```

Consequently, the experimental worker build is valid for headless and Null
diagnostics, but it is not yet a flat WebGPU or WebXR rendering build. A full
flat WebGPU owner-data smoke would encounter this ownership boundary before
map rendering, so it was not mislabeled as a pass.

The preferred next experiment is to keep the engine/presentation owner on the
browser main thread and create/mount the WasmFS OPFS backend from a dedicated
pthread, if the shared WasmFS mount remains visible to main-thread libc calls.
That preserves the current WebGPU and WebXR ownership while moving only the
synchronous OPFS operation to a legal worker. If that is not viable, the
larger alternative is a worker-owned presentation architecture: acquire the
device asynchronously inside the engine worker, deliberately route WebGPU and
WebXR commands across the boundary, and define which realm owns the canvas.
Neither route is an Emscripten flag-only change.

An attempted `-pthread -sASYNCIFY` main-thread OPFS probe failed the installed
SDK assertion in `ProxyWorker`: the pthread implementation still tries to
create its synchronous OPFS proxy from the browser main thread. The SDK's
working Asyncify/JSPI OPFS tests are non-pthread builds. Therefore merely
adding Asyncify to the present threaded engine is not an alternative.

## Opt-in engine experiment

The integration experiment is now available behind the default-off CMake
option:

```text
SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS=ON
```

The option adds `-sWASMFS`, `-sFORCE_FILESYSTEM`, and
`-sPROXY_TO_PTHREAD`. An ordinary build keeps its existing flags and MEMFS
materialization path. The experimental browser importer detects the native
mount ABI; an IndexedDB dataset, a custom game root, or a build without that
ABI still takes the established materialization fallback.

For an OPFS dataset, JavaScript registers only the published storage directory,
dataset generation, canonical paths, and 64-bit sizes. It never requests the
file blobs. `GameApp::main()` then runs on the application worker and:

1. mounts the browser OPFS root at `/.surreal-opfs`;
2. checks every published file and size before game discovery;
3. creates memory-backed `/gamedata` directories and symlinks immutable files;
4. marks OPFS targets and directories read-only; and
5. keeps allowlisted engine configuration and `Save<N>.usa`/`Save<N>.dxs`
   files memory-backed.

The existing mutable controller restores before `callMain()`. A restored
mutable file therefore wins over the imported baseline. If an imported
dataset itself contains an allowlisted save or generated `SE-*.ini` file, the
worker copies only that file into the memory backend rather than exposing a
writable link into the immutable generation. Checkpoints and clear operations
continue using the existing mutable-data allowlist and separate store.

Build the variant with:

```powershell
emcmake cmake -S . -B build-wasmfs-opfs -DCMAKE_BUILD_TYPE=Release `
  -DSURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS=ON -DBUILD_TESTING=OFF
cmake --build build-wasmfs-opfs -j 12
```

This remains an experimental storage/loader variant. It is not a release
build or WebXR activation.

## Measured runtime evidence

The synthetic engine bootstrap stores nine UE1-shaped files in real OPFS,
registers the mount without reading a blob, enters the worker, and repeats the
same path after a page reload. Both runs retain the configured 256 MiB Wasm
heap. The standalone probe separately proves a persistent 32 MiB file across
reload with synchronous `stat`, seek, and read, zero heap growth, and a denied
write through the read-only `/gamedata` link.

An owner-data validation used an existing local GOG UT99 installation in a
fresh persistent browser profile outside Git. No path or commercial content is
recorded in the repository. Observed results were:

- 496 files and 659,817,346 bytes mounted from OPFS;
- a 268,435,456-byte Wasm heap before and after the mount;
- three persisted allowlisted files restored and two imported mutable
  baselines kept memory-backed;
- UT99 identified as version 436 and its packages read through the mount; and
- the headless bot benchmark loaded `DM-Deck16][`, created a player and bot,
  simulated two deterministic ticks, wrote a complete summary, and exited 0.

This establishes that the former post-import memory block is removed for the
loader and headless runtime. It does not establish working window or XR
presentation.

## PROXY_TO_PTHREAD presentation boundary

The storage solution moves application `main()` to a worker because the
installed pthread WasmFS OPFS backend cannot be created on the main browser
thread. That changes ownership of every browser-backed presentation object.

The measured owner-data window run reached package loading, then both the
ordinary launch and an explicitly selected null render device failed in
SurrealWidgets' GL shader creation. `DisplayBackend::TryCreateBackend()` runs
before the command-line render-device selection, and the worker has no current
entry in the main browser thread's GL context table. The failure is therefore
after successful storage and package loading, at presentation ownership.

`web/proxy_to_pthread_dispatch_probe.cpp` measures the call boundary directly:

- an exported `Module.ccall()` executes on the main browser thread, not the
  application worker;
- 32 calls can be queued to the captured application pthread;
- every completion can return to the browser with
  `MAIN_THREAD_ASYNC_EM_ASM`;
- all completions arrive asynchronously after the originating animation-frame
  callback body; and
- the application worker cannot see a marker stored on the Window global.

Compile that standalone probe with:

```powershell
em++ web/proxy_to_pthread_dispatch_probe.cpp -O2 -pthread `
  -sPROXY_TO_PTHREAD -sPTHREAD_POOL_SIZE=1 -sALLOW_MEMORY_GROWTH=1 `
  '-sEXPORTED_RUNTIME_METHODS=ccall' `
  '-sEXPORTED_FUNCTIONS=_main,_Probe_CallingThreadKind,_Probe_SubmitToEngineThread' `
  -sASSERTIONS=1 -o build-proxy-thread-probe/index.html
```

The last result applies directly to the current
`globalThis.surrealWebXRFrameTextures` handoff: the `EM_JS` import runs in the
worker's JavaScript realm, while the XR callback stores textures on Window.
Dispatching `Surreal_RenderWebXRFrame` would also change its synchronous return
into an asynchronous completion. By then the originating XR animation-frame
callback has returned; this experiment does not prove that its frame-scoped
pose or compositor textures remain valid. Input packets that live entirely in
shared memory may be queueable, but that does not solve render completion or
texture ownership.

Consequently the experimental WasmFS variant must not advertise WebGPU,
desktop window, or WebXR presentation yet. A product solution needs one of:

- presentation and its browser objects deliberately owned by the application
  worker, with a browser-supported transfer model and frame-lifetime proof; or
- a non-pthread WasmFS OPFS build using a proven Asyncify/JSPI execution model
  so engine and WebXR frame work stay on the browser main thread.

An asynchronous worker dispatch bridge alone is insufficient for the current
WebXR render contract.

## Window-owned Asyncify variant

A second default-off experiment keeps engine, WebGPU, and WebXR ownership on
the browser Window thread:

```text
SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS_ASYNCIFY=ON
```

This variant is mutually exclusive with the `PROXY_TO_PTHREAD` option. It
removes browser pthread flags, enables `-sWASMFS -sFORCE_FILESYSTEM -sASYNCIFY`,
and uses mount ABI version 2, mode 2. JavaScript registers the validated OPFS
manifest and awaits `Surreal_PrepareBrowserOPFSMount` through an Asyncify-aware
`ccall` before mutable restore and native startup. The symlink tree and mutable
overlay policy are otherwise the same as the worker experiment.

The installed Emscripten 6.0.2 runtime defaults to exposing resizable
Wasm-backed views when the browser supports them. Chrome rejects those views
at several browser API boundaries, including `TextDecoder`, Web Crypto, and
WebGPU queue uploads. Browser builds therefore use
`GROWABLE_ARRAYBUFFERS=0`: Emscripten keeps ordinary `ArrayBuffer` views and
refreshes its HEAP views after each WebAssembly memory growth. The heap still
starts at the measured 256 MiB baseline and can grow as game demand requires.
A post-link generated-output gate verifies both the ordinary-buffer path and
`WebAssembly.Memory.grow`, so a future toolchain change cannot silently restore
resizable browser-API arguments or the fixed heap.

The owner-data Unreal Gold check mounted the complete 335-file GOG install
(559.3 MiB), grew the engine heap from 256 MiB to 307.25 MiB, loaded `Bluff`,
and advanced beyond 1,100 animation frames without a page error or runtime
abort. This qualifies the memory-view boundary and continued main-loop
ownership; visual, audio, and input acceptance still require device testing.

Two startup details are essential:

- Emscripten `callMain` mutates its argument array by prepending `argv[0]`, so
  the frozen launcher description must be copied before ordinary builds call
  it.
- `callMain` is not an Asyncify-aware boundary. The Window-owned variant uses
  the exported `Surreal_StartBrowserGame` entry through
  `Module.ccall(..., { async: true })`; otherwise the first asynchronous OPFS
  filesystem operation unwinds and startup returns at tick zero.

The single-thread audit found one active browser `std::thread` dependency:
`UInternetLink::Resolve`. The Window-owned variant runs its existing resolver
body synchronously after releasing the object mutex. Browser audio is already
main-thread pumped, the selected OpenMPT configuration supplies no-thread
mutex shims, and the WebXR mutexes do not create worker ownership.

Emscripten OpenAL reports `INT_MAX` for its virtual mono and stereo source
limits. Treating that value as an allocation count caused the apparent
fixed-heap `std::bad_alloc`. Browser builds now clamp unlimited, nonpositive,
or oversized reports to the requested voice count. The measured owner run
allocated 256 sources and completed audio initialization within the fixed
heap.

Browser pointer lock is also explicitly gesture-owned. Native startup records
pointer-lock intent on the browser main thread but does not request it. Once
flat startup completes, the product shell exposes a **Capture mouse** control;
that trusted click, or a trusted click on the exact game canvas, performs the
request. After capture is released the control becomes **Resume mouse look**.
Rejection leaves ordinary focused-canvas mouse input available, and an active
WebXR session suppresses or exits pointer lock.

Escape is reserved by the browser while pointer lock is active. A focused,
visible page therefore treats non-programmatic lock loss as one Escape intent
and queues it for the engine thread after SDL input is pumped. This lets the
same action skip an intro or reach the native menu even though no SDL Escape
arrives. Explicit engine unlock, WebXR entry, hidden/background loss, and an
Escape already delivered by SDL do not synthesize a second press. The product
shell uses SDL's expected `canvas` identifier, while WebGPU surface creation
marks and selects the exact `Module.canvas` rather than a separate hard-coded
node.

Measured owner-data evidence for the Window-owned variant is:

- 496 files and 659,817,346 bytes restored from OPFS without MEMFS
  materialization;
- fixed 268,435,456-byte Wasm heap;
- UT99 version 436, configuration, and packages read successfully;
- `DM-Deck16][` headless/null bot coverage completed and exited 0;
- flat WebGPU reached login and advanced beyond 450 ticks, with the exact
  `Module.canvas` owning the surface, nonzero draws/textures, zero engine
  WebGPU errors, and no page errors; and
- deterministic Chrome pointer-lock coverage proved inert startup, a visible
  trusted capture/resume path, one forwarded browser Escape intent,
  programmatic unlock suppression, WebXR suppression, and handled rejection.

The first flat WebGPU frame previously reported a destroyed swap-buffer
texture. The renderer acquired the canvas texture in
`WebGPURenderDevice::Lock()`, before scene, UI, and lazy asset work, then
submitted it in `Unlock()`. A non-threaded WasmFS OPFS read can await browser
promises through Asyncify. Emscripten's animation-frame runner does not await
that continuation, so the browser could present and expire the canvas texture
before `Unlock()` resumed.

The deterministic probe at
`web/probes/webgpu_canvas_async_lifetime_probe.html` proves the boundary in the
same Chrome/WebGPU stack: acquire, encode, and submit in one animation frame is
clean; inserting one `requestAnimationFrame` before submission produces the
exact `Destroyed texture ... used in a submit` validation error. Rendering to
a persistent offscreen texture across that boundary, then acquiring, copying,
and submitting the canvas texture synchronously is also clean. Run it against
the development server with:

```powershell
$env:SURREAL_WEB_BASE_URL='http://127.0.0.1:8091'
python web/probes/webgpu_canvas_async_lifetime_probe_test.py
```

The flat renderer now follows that design. `Lock()` renders to a persistent
offscreen color texture, and `Unlock()` finishes all scene submissions before
it acquires the canvas texture and immediately copies, submits, and releases it
without a suspension-capable call. A retail UT99 OPFS run advanced ticks with
41 draws, 52 textures, zero WebGPU errors, no page errors, and no destroyed
texture warning. The copy path preserves the default flat renderer and is
bypassed for provider-owned presentation targets.

WebXR now applies the same ownership rule through private frame ABI v3. Native
code renders into double-buffered, persistent JavaScript-owned eye textures (or
one persistent bridge atlas) from a later browser task. The XR animation
callback itself only copies the last completed front image into compositor
textures acquired late in that callback. It never calls Wasm, and the ordinary
Emscripten main loop is actually paused while XR owns scheduling so it cannot
re-enter a suspended Asyncify render. Controller/visibility snapshots and
browser-audio mutations are queued behind the same native-call gate. Exit,
failure, texture destruction, and re-entry wait for the producer and GPU queue
to drain.

Deterministic direct and bridge tests cover enter/exit/re-entry, one producer at
a time, dropped/repeated headset frames, old-generation rejection, late copy
ordering, and zero native calls while a render Promise is unresolved. This
resolves the known software lifetime hazard; it does **not** claim physical
Quest presentation. Direct compositor copy and especially the WebGPU transfer
canvas to `XRWebGLLayer` upload remain headset qualification gates.

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

1. Keep the implemented opt-in WasmFS build variant separate from the current
   release build.
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

The focused local validation commands are:

```powershell
python web/smoke_test_ut99_importer.py --base-url=http://127.0.0.1:49011
python web/smoke_test_wasmfs_opfs_mount.py --base-url=http://127.0.0.1:49011
python web/smoke_test_wasmfs_opfs_engine_mount.py --base-url=http://127.0.0.1:49011
python web/smoke_test_proxy_to_pthread_dispatch.py --base-url=http://127.0.0.1:49011
```

The two standalone probes must first be compiled into
`build-wasmfs-probe/index.html` and
`build-proxy-thread-probe/index.html` with the flags documented above.

Do not raise `INITIAL_MEMORY` as the primary fix. That reserves more memory at
startup and masks the actual peak. The OPFS mount keeps commercial game files
out of MEMFS, while the growable 256 MiB engine heap accommodates real runtime
demand. Keep measuring heap and browser-process peaks on Quest before changing
the initial or maximum limits.
