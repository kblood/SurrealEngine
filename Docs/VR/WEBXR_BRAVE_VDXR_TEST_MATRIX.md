# Brave + Virtual Desktop WebXR/WebGPU test matrix

## Qualified environment

This matrix records the 2026-07-22 development setup used for the first
physical WebXR attempt:

- Brave `150.1.92.141` on Windows;
- Virtual Desktop Streamer `1.34.18`;
- VirtualDesktopOpenXR/VDXR `1.0.10`;
- Meta Quest 3 connected through Virtual Desktop;
- local page `http://localhost:8091`.

The Windows OpenXR `ActiveRuntime` points to:

```text
C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json
```

`%ProgramData%\Virtual Desktop\OpenXR.log` identifies the Quest 3, says it is
using the Virtual Desktop runtime, and records a Chromium 150 OpenXR instance.
This proves that the base Brave/Chromium -> OpenXR -> VDXR -> Quest route exists
on this machine. It does not prove WebGPU projection-layer submission.

Default Brave exposes `navigator.xr` and reports immersive VR support here, but
does not expose `XRGPUBinding`. Enabling `WebXRWebGPUBinding` and `WebXRLayers`
exposes the constructor. API presence and `isSessionSupported("immersive-vr")`
are preflight evidence only; neither tests `requiredFeatures: ["webgpu"]` nor
compositor acceptance.

## Exact launch command

Close other immersive sessions and launch a dedicated Brave profile:

```powershell
& "C:\Program Files\BraveSoftware\Brave-Browser\Application\brave.exe" `
  --user-data-dir="$env:TEMP\SurrealWebXR-Brave" `
  --enable-features=WebXRWebGPUBinding,WebXRLayers `
  "http://localhost:8091/web/index_webxr.html?build=build-emscripten&native-webgpu-xr=1&orientation-fix=1"
```

The equivalent persistent settings are:

- `brave://flags/#webxr-webgpu-binding` -> Enabled;
- `brave://flags/#webxr-projection-layers` -> Enabled;
- fully restart the dedicated Brave profile after changing either flag.

The `native-webgpu-xr=1` parameter is mandatory for a production-presentation
attempt. Without it the page deliberately starts only a throwaway WebGL
lifecycle test and never displays SurrealEngine frames in the headset.

## Result interpretation

| Observation | Interpretation / next action |
|---|---|
| `mode: "lifecycle-only"` | Wrong route for game presentation; add `native-webgpu-xr=1` |
| `xrgpu-binding-unavailable` | Experimental browser API is disabled/absent; this is not a VDXR failure |
| `immersive-vr-unsupported` | Base OpenXR device/runtime was not discovered; verify the VD connection, `Runtime: VDXR`, then restart Brave |
| `xr-gpu-unavailable` | No `{xrCompatible:true}` WebGPU adapter; browser/GPU/OpenXR interop failed before session creation |
| `canAttempt: true` | Preflight only; no session or compositor presentation is proven |
| `NotSupportedError` during `requesting-session` | The browser rejected the requested session. With ordinary immersive support true, the experimental `webgpu` feature is a prime suspect, but permission/runtime/device rejection can share this error |
| `SecurityError` during `requesting-session` | Check direct user activation, secure context, and permissions policy; click the button directly |
| `InvalidStateError` during `requesting-session` | Another immersive session is active or pending |
| Session created, binding not created | OpenXR session worked; XR-compatible WebGPU device/session pairing failed |
| Binding created, layer not created | Binding worked; projection-layer creation or `updateRenderState({layers})` failed |
| Probe `sessionCreated`, `bindingCreated`, and `layerCreated` are all true | The compositor accepted a WebGPU projection layer; the probe intentionally does not render game frames |
| Native phase `creating-binding`, then `error` | Session request worked; inspect binding/layer/render-state/reference-space error |
| Native phase `transferring-frame-loop`, then `error` | Browser XR setup worked; engine RAF ownership transfer failed |
| Native phase `running`, increasing `frameCount`, `lastRenderSucceeded: true` | Full native WebGPU XR frame path is operating |
| `running` with increasing `skippedFrames` | Viewer pose was null for those frames; inspect pose/runtime state if persistent |

Immediately after a failed click, capture:

```javascript
JSON.stringify({
  readiness: window.surrealGetWebXRLaunchReadiness?.(),
  probe: window.surrealXRWebGPUProbe,
  native: window.surrealXRNativeDiagnostics,
  error: window.surrealXRNativeError,
  log: window.surrealXRLog?.slice(-30)
}, null, 2)
```

Also record the Virtual Desktop overlay's runtime line and copy the new tail of
`%ProgramData%\Virtual Desktop\OpenXR.log` before another browser session
overwrites the most useful sequence.

## Support boundary

Base Windows WebXR through VDXR is a reasonable expectation because Chromium
uses OpenXR on Windows and VDXR advertises the Windows app-container extension,
projection layers, D3D11/D3D12, controllers, and Quest 3. WebGPU-backed WebXR
remains experimental: VDXR documents the native OpenXR pieces but does not claim
tested compatibility with Chromium's draft WebXR/WebGPU binding. Treat this as
a compatibility experiment until the page reaches native `running` with
successful increasing frames in the headset.

`http://localhost` is a Chromium trustworthy-localhost exception. A plain
`http://192.168.50.8:8091` page opened on another device is not equivalent;
serve that route over trusted HTTPS.

Authoritative references:

- Chromium Windows/OpenXR platform support:
  <https://chromium.googlesource.com/chromium/src/+/main/device/vr/>
- VDXR setup and diagnostics:
  <https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki>
- VDXR developer capabilities:
  <https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki/Developers>
- Chromium experimental WebXR/WebGPU feature:
  <https://chromium.googlesource.com/chromium/src/+/1c22e8e0f1f14071f0eae28d3f7d48408c841398>
- WebXR/WebGPU Binding draft:
  <https://immersive-web.github.io/WebXR-WebGPU-Binding/>
- WebXR session semantics:
  <https://immersive-web.github.io/webxr/>
- W3C Secure Contexts:
  <https://www.w3.org/TR/secure-contexts/>
