# Unreal Gold browser qualification

Date: 2026-07-23

This is an owner-data qualification record. No game files, imported filenames,
browser profile, or screenshots are part of the repository or release package.

## Test boundary

The test used the clean shipping Asyncify/WasmFS artifact from source commit
`5e6fba9257778833925e03c5350ee55b452dd040` and tree
`5d59f459558c312da263e0a7eb446e1a6926da1e`. Its build provenance records an
empty game-data directory and `sourceDirty: false`. The selected GOG Unreal Gold
installation contained 335 files and 586,421,656 bytes. Both a new Chrome
profile/import and a restored owner profile were exercised locally.

A later live same-origin matrix at immutable candidate `2904593c` imported the
same 335 files, kept Unreal isolated while UT99 was added and replaced, restored
both titles after Chrome restart and an older-to-newer candidate transition,
and left the original GOG directory unchanged.

## First-run map correction

Unreal Gold declares `Vortex2` as its safe direct-start default. The launcher
previously sorted the imported map manifest and left the first option selected
when no per-game preference existed. That made a new Unreal Gold import select
`Bluff`, while a new UT99 import could select `AS-Frigate` instead of its
declared Deck 16 default.

The launcher now chooses, in order:

1. a valid saved per-game map preference;
2. the game's declared default map when it is present in the imported manifest;
3. the first validated manifest map as the existing fallback.

The selection uses the map spelling stored in the validated manifest. A focused
browser test covers the Unreal Gold default and confirms that a later explicit
Bluff preference still overrides it.

## Visual evidence

The corrected Vortex2 direct path advanced simulation, rendered 240 draw calls
and 270 textures, and reported no browser or WebGPU errors. Its 1280 by 720
canvas had a mean luminance of 23.985, median luminance of 18.923, 45.420% of
pixels below luminance 16, and 64,349 distinct RGB values. The captured frame
clearly showed the blue-lit Vortex Rikers interior, HUD, and first-person view;
it was dark art direction rather than a blank render.

For comparison, the unintended first-run Bluff frame also rendered correctly
but was darker: mean luminance 20.246, median 16.689, and 48.037% below
luminance 16. A direct NyLeve start was a worse visual gate because that player
start was a cramped dark/red interior (mean 14.253 and 66.708% below luminance
16). Use Vortex2, not the alphabetically first map, for the Unreal Gold release
smoke.

The browser does not currently expose a product diagnostic for exact camera
coordinates. The launch selection, `LoginPlayer` completion, advancing ticks,
first-person HUD/view, and stable draw counts establish an active player camera
rather than an intro-only or menu-only frame. Coordinate-level debugging would
require temporary instrumentation and is not a release feature.

## Audio evidence

The launcher Start action was a sufficient trusted gesture in the tested Chrome
path. Chrome DevTools Protocol observed the 48 kHz realtime AudioContext change
from suspended to running. Browser and native bridge diagnostics then reported
running state, mute disabled, output volume 1, no resume failure, and no audio
error. The separate Enable audio control was correctly hidden once running.

Vortex2 produced nonzero mono effect/ambient buffers at its stationary opening.
Bluff and NyLeve produced continuous nonzero stereo PCM through the browser
OpenAL queue; sampled stereo buffers had nonzero samples in both channels and
nonzero RMS. This exercises the module decoder and browser music scheduling,
not merely AudioContext creation. SurrealVideo is unrelated to map music;
Unreal module music uses `AudioSource::CreateMod` and libopenmpt, while
SurrealVideo handles supported cinematic streams.

Headless automation cannot prove what reached physical speakers. A human pass
must still confirm audible music/effects and the intended relative levels on
the target browser/device. If that fails while the diagnostics remain running
and PCM remains nonzero, investigate the OS/browser output route and final
gain graph rather than folder import or module-format support first.

## Release gate

For the next immutable candidate:

1. import or restore the owned Unreal Gold folder;
2. confirm the initial direct-map selection is Vortex2;
3. launch Vortex2 and require advancing ticks, visible Vortex Rikers geometry,
   zero browser/WebGPU errors, and a running AudioContext;
4. audibly verify ambient effects and later music during real play;
5. separately exercise the unchecked game-owned LocalMap path.

The later owner matrix passed step 5: unchecked `URL.LocalMap` advanced from
startup into running Vortex2, continued after fire input, remained visibly
nonuniform, reported a running AudioContext, and produced zero page/WebGPU
errors. Human audible output and long play remain gates. Mutable INI/log/Settings
restore also passed, but `.usa` save persistence failed because the WasmFS
snapshot's `analyzePath` check skipped the otherwise readable Save directory;
see `web/MUTABLE_DATA.md`.

Bluff remains useful for continuous music-buffer diagnostics, but it is no
longer the first-run renderer gate.

## Current integration reproduction (e371041d)

The owner-data matrix was repeated from exact integration commit `e371041d`
with a fresh data-free Window-owned Asyncify/WasmFS build. The original GOG
folder remained outside the build and package. A local isolated profile imported
the same 335 files and 586,421,656 bytes.

Flat WebGPU did not reproduce a black-render regression:

- direct `Vortex2` advanced ticks `3 -> 61 -> 121`, rendered 238 draw calls
  and 236 textures, produced a 99.98% nonuniform canvas, and reported zero
  WebGPU or page errors. The captured frame visibly showed the Vortex Rikers
  interior;
- game-owned `URL.LocalMap` advanced ticks before and after input, remained
  nonuniform, and reported zero WebGPU or page errors. Its flyby explicitly
  displays **Press ESC to begin**. Primary fire does not satisfy that title-
  specific prompt; focused Escape opens the Unreal Gold menu, as the resulting
  menu frame confirmed; and
- a native Release build identified Unreal 226b, initialized Vulkan and audio,
  completed `Vortex2` login/inventory acceptance, stayed responsive, and shut
  down cleanly. This was a diagnostic comparison only; subsequent work remains
  headless/noninteractive.

The owner smoke now has aggregate, content-free WebAudio sampling and optional
audio gates. It reports context state and counts/RMS only; it does not retain
filenames, PCM, browser profiles, or game data. On `Vortex2`, the context was
running with no error and all four started mono buffers contained nonzero PCM.
No stereo buffer started at the stationary opening, matching the earlier
qualification. Direct `Bluff` exercised the music path: ticks advanced
`3 -> 64 -> 125`, the frame was visibly valid with zero WebGPU/page errors,
and `--require-stereo-audio` passed with 4,354 nonzero stereo buffers plus 19
nonzero mono buffers during the sampled window (maximum sampled RMS 0.307).

These results establish current flat rendering, package/music decoding, browser
queueing, and AudioContext state. They do not prove audible speaker/headset
output, relative levels, long-play stability, or WebXR presentation. No WebXR
claim can be made without an actively connected headset and a real immersive
session. The previously reported mostly-black/missing-audio observation should
therefore be reproduced with its exact launch mode and headset/runtime state;
there is no evidenced engine compatibility correction to apply from the flat
path.

Repeatable owner gates include:

```powershell
python web/smoke_test_owner_game.py --game-dir <owned-unreal-folder> `
  --expected-game unreal-gold --map Vortex2 --startup-mode direct-map `
  --renderer webgpu --require-audio --base-url <local-package-origin>

python web/smoke_test_owner_game.py --game-dir <owned-unreal-folder> `
  --expected-game unreal-gold --map Bluff --startup-mode direct-map `
  --renderer webgpu --require-stereo-audio --base-url <local-package-origin>
```
