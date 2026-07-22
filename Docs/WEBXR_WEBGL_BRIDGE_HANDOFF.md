# WebXR WebGL compatibility bridge handoff

Status: implemented on product-integration branch `integration/webxr-webgl-bridge`.
This is not an upstream PR source and is not yet a Quest release claim.

## What the bridge does

The browser provider selects direct `XRGPUBinding` when both the API and an
XR-compatible WebGPU device are available. Otherwise it requests a normal
`immersive-vr` session and creates an `XRWebGLLayer` with a WebGL 2 context.

Inside the same XR animation-frame callback it:

1. lays the two eye viewports out side by side on the existing WebGPU canvas;
2. converts WebXR/WebGL projection depth from `[-1, 1]` to WebGPU `[0, 1]`;
3. lets the existing native WebGPU renderer draw both world views and all XR UI
   surfaces into that shared canvas texture;
4. uploads the completed canvas once with `texSubImage2D`; and
5. draws the two atlas subrectangles into their `XRWebGLLayer` eye viewports.

Input snapshots, simulation ownership, recentering, UI pointer feedback,
session exit/re-entry, and flat WebGPU fallback remain in the existing provider.
The bridge contains no game data and is not a second engine renderer.

The frame ABI flags distinguish a pre-converted `[0, 1]` projection and a
shared stereo atlas. The native presentation binding reuses one texture view
for both eyes and preserves the first eye's color while clearing depth for the
second eye. Direct per-eye and texture-array paths keep their old behavior.

## Diagnostics

`surrealXRGetCapabilities()` reports `directWebGPU`, `webGLBridge`, and
`preferredMode`. `surrealXRGetState()` reports the active `presentationMode`
and rolling bridge `medianMs`, `p95Ms`, frame count, and error count.

Normal timings measure CPU submission only. Set
`window.surrealXRBridgeBlockingTiming = true` for a short QA run that includes
`gl.finish()` synchronization. Do not ship with that switch enabled.
`window.surrealXRForceWebGLBridge = true` forces the compatibility path on a
browser which also exposes direct binding support.

## Automated evidence

- `node web/test_webxr_webgl_bridge.mjs` verifies the shared projection
  conversion with an asymmetric frustum.
- `node web/test_webxr_webgl_fallback_provider.mjs` mocks fallback capability,
  normal session options, atlas flags/viewports, one upload presentation call,
  diagnostics, and cleanup.
- `node web/test_webxr_provider.mjs` protects the direct binding and lifecycle.
- `python web/probes/webgpu_webgl_bridge_probe_test.py` drives real desktop
  Chrome and checks the actual WebGPU-canvas to WebGL 2 upload/readback path.
- `WebXRFrameBridgeTests` validates known ABI flags and atlas invariants.

Desktop Chrome proves API behavior, not Quest cost or WebXR compositor
behavior. It cannot validate an opaque headset framebuffer.

## Physical Quest release gates

Test a production build on every supported Quest model/browser combination.
Record headset model, OS/runtime, browser version, layer size, refresh rate,
renderer mode, and build commit.

The compatibility mode may be called release-ready only when all of these pass:

- 20 consecutive enter/exit/re-entry cycles without a stuck engine loop,
  context loss, stale input, or flat-window regression;
- correct left/right eye assignment, vertical orientation, asymmetric frustum,
  head tracking, controllers, pointer rays, menu/intro quad ordering, and UI
  selection during at least 30 minutes of UT99 gameplay;
- Unreal Gold boot/menu/gameplay smoke coverage using user-supplied data;
- zero WebGL errors, uncaptured WebGPU errors, bridge frame exceptions, and
  unexpected skipped frames during a 30-minute thermal run;
- bridge blocking handoff p95 at or below 4.0 ms and p99 at or below 5.5 ms at
  the release render scale, with the entire application meeting the selected
  refresh-rate frame budget (13.89 ms at 72 Hz or 11.11 ms at 90 Hz); and
- no repeatable stale/black atlas frame during head motion, menu transitions,
  visibility loss/resume, or session shutdown.

If those timing or correctness gates fail, reduce the documented render scale
only if image quality remains acceptable. Otherwise the contingency is a real
WebGL 2 render device; do not hide a failing bridge behind looser claims.

## Known physical limitations

No physical Quest result exists on this branch yet. The canvas upload is legal
but not guaranteed zero-copy, so Quest Browser may perform a costly
GPU-to-CPU-to-GPU transfer. The browser controls synchronization between the
submitted WebGPU work and `texSubImage2D`. The implementation intentionally
keeps both operations in one XR callback because the desktop probe showed that
waiting for `queue.onSubmittedWorkDone()` could yield a black canvas handoff.
