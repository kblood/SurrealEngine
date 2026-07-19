![SEBANNER](Resources/surreal-engine-banner.png)

# Welcome to Surreal Engine!

Surreal Engine is a project that aims to reimplement Unreal Engine 1; currently focused on making Unreal (Gold) and Unreal Tournament (UT99) playable. The scope of this project might expand to cover more UE1 games in the future.

## About this fork

This fork adds two VR-related efforts on top of upstream Surreal Engine, both
tracked in [`Docs/VR/PLAN.md`](Docs/VR/PLAN.md):

* **Native VR** (`vr-m2` branch) — OpenXR stereo rendering injected into the
  existing Vulkan `RenderDevice`, aimed at Quest 3 via Virtual Desktop.
* **WebXR** (`webxr-m1` branch) — an Emscripten port with a new WebGPU
  `RenderDevice` backend, so the engine can eventually run and render in a
  browser tab. M1 (build harness) and M2 (WebGPU renderer) are done; see
  [`Docs/VR/WEBXR_IMPLEMENTATION_PLAN.md`](Docs/VR/WEBXR_IMPLEMENTATION_PLAN.md)
  for the full technical log.

Neither effort is merged to `master`, which stays a clean mirror of upstream.

## Current Status

Please refer to [Status.md](Docs/Status.md) for the current status of Surreal Engine!

## System Requirements

* Original copies of the UE1 games you want to run
* Windows 10+ or a modern Linux distro
* A Direct3D 11 or Vulkan capable graphics card

## Building Surreal Engine

Please refer to [Building.md](Docs/Building.md) for details!

## Downloads

[Nightly builds are available on the Releases section](https://github.com/dpjudas/SurrealEngine/releases/tag/nightly).

Additionally, Surreal Engine is available on following Linux distributions:

* Arch: [AUR](https://aur.archlinux.org/packages/surrealengine-git)
* Nix: [Package Search](https://search.nixos.org/packages?channel=unstable&show=surreal-engine) | [Quickstart](https://github.com/NixOS/nixpkgs/pull/337069)

## How to Play

* Run the `SurrealEngine` executable.
* Add the UE1 games you want in the Folders tab.
* Select the game you want to play in Games tab.
* Click "Play"!

## Discord Server

Visit us on Discord at https://discord.gg/5AEry4s

## Command Line Parameters

`SurrealEngine [--url=<mapname>] [--engineversion=X] [Path to game folder]`

If no game folder is specified, and the executable isn't in a System folder, the engine will search the registry (Windows only) for the registry keys Epic originally set.

If no URL is specified it will use the default URL in the ini file (per default the intro map).

The `--engineversion` argument overrides the internal version detected by the engine and should only be used for debugging purposes.
