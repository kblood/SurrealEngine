# Browser cinematic playback

Date: 2026-07-22

## Scope and format audit

SurrealEngine's file-backed cinematic path is narrower than the general UE1
intro problem. `Engine::Setup()` starts `INTRO.AVI` only for Klingon Honor
Guard, and `PackageManager` only indexes `.avi` files for that game. The AVI
reader expects a RIFF AVI with both video and audio streams. The bundled
SurrealVideo decoder is the existing stripped FFmpeg fork for Indeo Video 5
(`IV50`) and Microsoft ADPCM.

UT99 and Unreal/Gold startup sequences do not enter this explicit AVI path.
They load the configured `URL.LocalMap` and run ordinary map, actor, script,
canvas, and menu behavior. Consequently the UT99 “Press fire” sequence and a
black Unreal intro are presentation/runtime issues, not evidence that the
browser AVI decoder is missing. Fixing browser AVI playback must not be used as
a substitute for testing those map-driven intros.

WebCodecs or an HTML `<video>` element is not a useful compatibility bridge for
the known KHG media: browser media stacks do not provide a portable IV50
decoder. Reusing SurrealVideo gives native and browser builds the same codec
semantics and avoids a browser-only media subsystem.

## Call-flow problem and implemented seam

Native `Engine::PlayAVI()` deliberately owns a synchronous inner loop. It
decodes toward a timestamp, updates audio, pumps window events, and draws each
frame before returning. That remains unchanged on desktop.

The browser cannot use that loop. Its flat mode registers an Emscripten
`requestAnimationFrame` callback after setup, and WebXR transfers ownership to
`XRSession.requestAnimationFrame`. A synchronous movie loop prevents either
owner from presenting.

The Emscripten path now does the following instead:

1. `PlayAVI()` creates `BrowserCinematicPlayback`, activates the existing
   provider-neutral cinematic surface, and returns.
2. `VideoFrameScheduler` advances an abstract `VideoPlayer` from the elapsed
   time supplied by the outer frame owner. It is independent of Emscripten,
   WebGPU, and WebXR.
3. While a movie is active, `AdvanceGameFrame()` advances decoding but pauses
   world simulation. `RenderGameFrame()` sends the current real-time texture
   through the existing `DrawVideoFrame(..., PresentationPlan)` path.
4. Flat browser presentation therefore draws the same frame on the ordinary
   canvas, while WebXR can capture/replay it through the existing cinematic
   surface. No provider, projection bridge, or controller-visual code owns the
   decoder.
5. End-of-stream, Escape/skip, quit, and decode failure clear the cinematic
   state and reset the game clock before normal simulation resumes.

The scheduler catches up if frame time advances by more than one video frame,
ignores negative/non-finite deltas, and never decodes after end-of-stream. Its
test uses a synthetic `VideoPlayer`; it contains no commercial media.

## Deliberate limitations

- Browser audio is still silent because Emscripten uses `NullAudioDevice`.
  Audio predecode and its decoder allocation are disabled for this path so
  startup does not synchronously decode an unused track. Native audio behavior
  is unchanged.
- The KHG `buildup.avi`/`breakdn.avi` mask transitions remain native-only. The
  browser logs this and plays the requested main AVI. Preserving those
  transitions requires a small asynchronous playlist/background state machine.
- Native `PlayAVI` blocks its caller until playback ends. Browser `PlayAVI`
  returns immediately and pauses simulation on subsequent outer frames. A
  script that depends on same-call-stack completion needs an explicit
  continuation contract before this can be considered identical behavior.
- The current reader requires an audio stream even when browser audio decode is
  disabled. This matches the known KHG files but is not a generic silent-AVI
  implementation.
- No proprietary IV50 fixture is committed. Compilation proves that the real
  decoder is in the WASM binary, and synthetic tests prove scheduling, but an
  owner-supplied KHG install is still required to validate actual pixels,
  timing, skip behavior, and recovery on flat browsers and a physical headset.
- This change does not fix or claim to validate UT99/Unreal map-driven intro
  presentation. Those paths need their own flat, native-XR, and WebXR tests.

## Build and license consequences

Native builds continue to link the `SurrealVideo` shared library. Emscripten
now compiles the same decoder sources into `SurrealEngine.wasm`; replacing
`NullVideoDecoder` added approximately 56 KB (0.88 percent) to the tested
Release WASM artifact.

SurrealVideo is LGPL 2.1-or-later. Static inclusion in WASM changes release
obligations compared with the native shared-library arrangement. The browser
packager now includes `SurrealVideo`'s LGPL text and project notice and warns
the host in `HOSTING.txt`. Those notices alone are not a complete static-link
compliance strategy. Before publishing, the release owner must provide the
exact corresponding SurrealVideo source, local modifications, and the engine
source/object or other relinkable materials needed to replace that library,
under a durable offer appropriate to the release. Legal review should confirm
the final source/relink mechanism.

## Validation performed without game data

- Windows x64 Release configured and linked the full `SurrealEngine` target.
- `VideoFrameSchedulerTests` passed through CTest.
- Emscripten Release compiled and linked the full no-data JavaScript/WASM
  target with SurrealVideo.
- The staged static package passed the data audit and served-browser smoke test:
  cross-origin isolation, import gate, no premature engine boot, and synthetic
  Unreal Gold flat launch all behaved as expected.
- The package test verifies that both SurrealVideo notices are present and
  recorded in the release manifest.

## Integration order and next slices

This topic should merge after the common presentation and XR UI-surface seams,
because it calls their provider-neutral cinematic activation/draw API. It does
not depend on the WebXR provider implementation itself and should merge before
provider-specific physical cinematic validation.

Follow-up slices should stay separate:

1. Add an asynchronous browser audio device and only then enable movie audio
   decode; do not put WebAudio policy into `VideoFrameScheduler`.
2. Add a generic asynchronous cinematic playlist/background state machine for
   KHG transitions, with synthetic players first.
3. Test owner-supplied KHG `INTRO.AVI` in flat WASM, then on the WebXR cinematic
   quad, including skip and return to gameplay.
4. Diagnose UT99 and Unreal/Gold map intros through normal world/UI presentation
   and script input, independently of AVI playback. The first isolated result is
   documented in [`MapStartupIntro.md`](MapStartupIntro.md).
5. Decide and document the exact LGPL corresponding-source/relink release
   mechanism before distributing the statically linked WASM artifact.
