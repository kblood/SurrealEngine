# Browser audio

## Status

The Emscripten build now reuses Surreal Engine's `OpenALAudioDevice` through
Emscripten's built-in OpenAL-to-WebAudio implementation. It no longer selects
`NullAudioDevice`. Browser builds therefore share decoded `USound` buffers,
positional source updates, music `AudioSource` decoders, and engine volume calls
with native desktop and OpenXR builds.

The browser adaptation is deliberately small:

- native builds retain the existing music decoder/playback thread;
- Emscripten fills and queues music buffers from `AudioDevice::Update`, because
  browser OpenAL calls must remain on the browser thread;
- extensions absent from Emscripten, currently `AL_METERS_PER_UNIT`, are guarded
  at compile time without changing native OpenAL behavior;
- `web/browser_audio_library.js` is a narrow lifecycle bridge to the private
  WebAudio context owned by Emscripten OpenAL. It is not a second mixer.

## User gesture and lifecycle

After the engine creates its OpenAL context, the launcher exposes an **Enable
audio** button whenever the context is suspended. This click calls
`AudioContext.resume()` inside the required user gesture. The controller
suspends playback when the page becomes hidden, offers explicit resume when it
becomes visible, preserves the context across WebXR enter/exit, and suspends on
page hide or clean engine quit. Master mute and volume operate on the Emscripten
OpenAL context gain without replacing engine sound/music settings.

Diagnostics are allowlisted scalar state only: context state/time, bounded
volume/mute, operation counters, and fixed error code/stage strings. They omit
game filenames, imported content, paths, browser logs, device labels, and user
agent information.

## Verification

```powershell
ctest --test-dir build-browser-audio-native -C Release --output-on-failure
cmake --build build-browser-audio-em --target SurrealEngine BrowserAudioProbe -j 8
python web/smoke_test_browser_audio.py
```

`AudioStreamBufferQueueTests` exercises the production FIFO used by native and
browser music. The real-Chrome probe creates a noncommercial generated PCM tone
and two generated queued music buffers. It proves real OpenAL/WebAudio context
time and queued graph state, gesture resume, mute/volume, suspend/resume, WebXR
transition continuity, and no page errors. It does not claim CI speakers are
audible.

The complete Release Emscripten and native engines build, all 26 native tests
pass, and the browser release packaging and WebXR provider suites pass.

## Remaining device and content checks

- Run owner-data UT99 and verify positional effects, ambient loops,
  announcer/voice playback, tracker music changes, pause, and map travel.
- Repeat in flat Chrome and physical Quest across WebXR enter/exit/re-entry.
  Automated graph evidence cannot validate headset routing, loudness, latency,
  or spatialization quality.
- Validate Unreal Gold separately; it shares the device but not all assets and
  runtime paths.
- Browser capture/input devices are not implemented. This is playback only.

## Cinematic audio is a separate release gate

Browser cinematic audio remains deliberately disabled: `BrowserCinematicPlayback`
still creates its player with `decodeAudio=false`, so `TakeAudio()` is empty even
though a real browser device now exists. Enabling it needs its own bounded topic
and owner-media tests in flat Chrome and physical Quest for unlock timing, drift,
skip/finish cleanup, and the transition into menu music. Do not describe
cinematic audio as validated from the generated SFX/music probe.
