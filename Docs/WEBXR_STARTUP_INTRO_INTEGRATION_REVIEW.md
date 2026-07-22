# WebXR startup-intro combined integration review

Date: 2026-07-22

Base: `integration/unified-engine` at `f1246102`.

Review result: integrated at `a0fb4f93`. The map-intro path and automated
evidence are implemented; actual UT99/Unreal owner-data behavior and physical
Quest presentation remain unverified.

## Scope and result

This review combined the UT99/Unreal map-startup lifecycle with both WebXR
presentation paths, shared controller input, controller/laser/contact visuals,
XR UI ordering, flat browser launch, and the experimental demo descriptors.
It did not modify Unreal 205 serialization or browser cinematic licensing
material.

No production-path defect was found. One stale integration-test index was
found: adding the HUD capture at descriptor slot zero moved loading from slot
one to slot two, while `WebXRUIProviderTests` still configured slot one and then
asserted loading/menu order. The runtime configures all descriptors by their
typed surface kind and was unaffected. The test now uses the correct loading
descriptor and covers the startup handoff explicitly.

## Shared direct/fallback path

`XRGPUBinding` and the `XRWebGLLayer` compatibility bridge differ only in how
they obtain the projection images:

- direct mode supplies WebGPU eye textures or array layers;
- bridge mode supplies one WebGPU stereo atlas, then copies its completed eye
  viewports to the WebGL XR framebuffer.

Both modes pack the same controller snapshot and call the same native frame
entry point. After frame decoding, both therefore use the same
`InputRuntime`, startup-trigger route, view family, UI binding, pointer contact,
controller visuals, and UI compositor. Bridge frame flags change projection
depth and atlas viewport handling; they do not create a second UI or input
implementation.

The deterministic evidence chain is:

1. Both provider tests carry a pressed right trigger through the same input
   packet and apply it to the native runtime.
2. `WebXRInputRuntimeTests` proves that the trigger is mirrored to real primary
   fire only during the startup state, is not stolen from an open menu, and is
   released across transitions/disconnects.
3. The UI-provider test now starts with a held trigger over the noninteractive
   startup HUD. Controller and laser geometry are visible, but there is no
   contact marker or menu press.
4. When the menu replaces the HUD, the held trigger gains exact menu contact
   without producing a click. A release and fresh press are required; the new
   press produces the marker at the authoritative hit and reaches menu input.
5. Loading remains behind menu, controller/laser remain before UI, and the
   opaque exact-contact marker remains after the menu.
6. Pointer cancellation, session exit/re-entry, and desktop mouse input use the
   existing shared binding tests and remain independent of presentation mode.

## Launcher and demo descriptors

The browser launcher retains its checked-by-default safe path:

```text
--autoplay --url=<validated map> --render=webgpu /gamedata
```

Unchecking **Skip startup intro** omits `--url`, leaving each imported game's
own `URL.LocalMap` authoritative. The browser demo-descriptor test now applies
both argument policies to the UT 348 demo, Unreal 205 demo, and Deus Ex 1002f
demo. This validates descriptor/launcher composition only; it does not claim
that each demo's normal intro, menu, or gameplay works.

## Validation

- Windows x64 Release: complete build passed; all 25 CTest tests passed.
- Emscripten Release: complete no-data `SurrealEngine.js`/WASM link passed.
- Direct WebXR provider Node test: passed, including eye texture variants,
  controller packets, focus/disconnect neutralization, exit, re-entry, and
  failure-to-flat lifecycle.
- XRWebGLLayer provider and WebGL bridge helper tests: passed, including atlas
  viewports, trigger packet, neutral exit, resource teardown, and re-entry.
- Shared WebXR launcher adapter and three-demo descriptor tests: passed.
- Served browser tests passed: 11 launcher, 19 importer, 13 mutable storage,
  and 3 bootstrap checks.
- The real generated flat product page reached its no-data import gate and
  launched a synthetic Unreal Gold selection with the expected direct-map
  arguments.
- Flat and WebGPU no-data pages waited at the legal local-import gate. The
  existing WebGPU harness rendered, advanced, and quit cleanly.

## Remaining physical and owner-data gates

Synthetic checks cannot validate the Quest browser's cross-API copy timing,
perceived stereo scale, controller latency, or commercial script behavior.
Before release, repeat the matrix with owned UT99 and Unreal Gold installs on a
physical Quest:

- direct `XRGPUBinding` where an actual target browser exposes it;
- automatic and forced `XRWebGLLayer` bridge selection;
- skip and normal `LocalMap` launch for both games;
- prompt readability, one-shot Press Fire, HUD-to-menu replacement, and no
  release-through click;
- controller/laser/contact alignment, mouse fallback, exit, and re-entry; and
- flat mode after leaving XR.

The Unreal 205 demo remains blocked by its separately owned serialization
investigation. Demo descriptors remain experimental and local-import-only
unless their independent compatibility and distribution gates are satisfied.
