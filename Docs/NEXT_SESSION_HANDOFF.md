# Next-session handoff

Date: 2026-07-24 (Europe/Copenhagen)

## Current product and website state

The active integration worktree is:

```text
C:\Devstuff\QuestGames\surreal-webxr-release-gates
branch: integration/unified-engine
release source: e90311692f09d06955bd9da244c695f2484c3421
release source tree: 9820f8af9da371711a36f03c8c49003eb600b773
```

Push integration changes only to `fork` (`kblood/SurrealEngine`), never to
`origin` (`dpjudas/SurrealEngine`). The integration branch is a product branch,
not an upstream-PR source.

The current browser release is live at the one stable public URL:

```text
https://dionysus.dk/webxr/Ports/SurrealEngine/
server: /var/www/html/webxr/Ports/SurrealEngine
```

The site was atomically updated from `04687fe1` to clean commit `e9031169` on
2026-07-24. Do not create numbered public Surreal release folders. Commit and
hash identity belong in `release-manifest.json`, not in the URL. Build into a
hidden staging directory, verify hashes, move the live folder to a non-public
rollback path, and rename the staged folder to `SurrealEngine`.

The complete deployment record is `Docs/WEB_DEPLOYMENT_E9031169.md`.

## Live browser release identity

Local audited package:

```text
C:\Devstuff\QuestGames\SurrealEngine\releases\staging\e9031169\SurrealEngine
```

Build and compliance identity:

| Item | Value |
| --- | --- |
| Source commit | `e90311692f09d06955bd9da244c695f2484c3421` |
| Source tree | `9820f8af9da371711a36f03c8c49003eb600b773` |
| Browser entry point | `asyncify-opfs` |
| Emscripten | `6.0.2`, revision `7a2d97d627ff4945eae28847ce0387ac52b92c09` |
| JavaScript SHA-256 | `fb7595ab328bb9b51be9eab3a7e9b601564d6e484f73852032a18bab37e0dab6` |
| WASM SHA-256 | `6d1899984e75da6481cf04aec655153d69e28ca449471702c3aead0562d4de92` |
| Manifest SHA-256 | `b7a49d83ed2c9cd21bd6eb97ffd0fa723cb188e8e10a7a81497a5b58f0462b94` |
| Corresponding source SHA-256 | `603ed057178310130b281e09ca24e2a598b452ddb04d8fff5c5fff167354e6d7` |
| File count | 25 total: 24 manifest payloads plus the manifest |

The previous live release is retained outside the public catalog at:

```text
/var/www/html/webxr/.SurrealEngine.rollback-04687fe1-20260724
```

Older numbered/experimental Surreal folders were removed from the public
`Ports` catalog without deleting them. They are preserved at:

```text
/var/www/html/webxr/.SurrealEngine-public-archive-pre-single-20260724
```

The public catalog now contains only `QuakeQuest/` and `SurrealEngine/`.

## Validation completed for the live update

- clean Git release source and clean build provenance;
- all 11 Node browser/WebXR suites passed;
- Release Asyncify/WasmFS Emscripten compile and link passed;
- post-link ordinary growable-buffer verification passed;
- corresponding-source generation and source/build identity checks passed;
- static package audit and prohibited-game-data scan passed;
- local staged headless Chrome release smoke passed;
- remote file count and manifest, JS, WASM, and source hashes matched;
- HTTPS responses returned COOP, COEP, CORP, no-cache, and correct WASM MIME;
- live headless Chrome import gate, WebGPU, layout, fullscreen, input, synthetic
  Unreal launch, and source retrieval passed with zero page errors.

The headless browser intentionally has no immersive WebXR device. A physical
Quest/VDXR WebXR pass is still required for projection, HUD, controls, weapon
aim, audio, exit, and re-entry behavior.

## Demo support and distribution boundary

The live engine and importer recognize these local demo folders:

- Unreal Tournament demo 348 (`ut99-demo-348`);
- Unreal Special Edition demo 200 (`unreal-demo-200`);
- Deus Ex demo 1002f (`deus-ex-demo-1002f`).

All three descriptors are present in the live `ut99_importer.js`, and their
detector/launch tests pass. They remain experimental. No demo or retail game
data is hosted, bundled, or downloaded by the application.

Magazine-CD distribution history does not itself establish a current right to
republish the files. The local demo handoff explicitly prohibits treating the
engineering setup as redistribution permission, and the local UT99 demo also
contains an `UnrealI.u` copied from another locally extracted demo. Do not put
any local demo directory, installer, repair archive, or copied package on the
website until the exact artifact has an authoritative redistribution grant and
its contents have been audited.

Engineering details are in:

```text
C:\Devstuff\QuestGames\surreal-unified\Docs\UE1_DEMO_HANDOFF.md
```

## Native OpenXR state

The integration source after the earlier `71650dd7` freeze includes the user's
physically tested controller/menu/weapon fixes and the later HUD projection
work through `e9031169`. The latest native retest directory is:

```text
C:\Devstuff\QuestGames\release-candidates\SurrealEngine-Native-OpenXR-e9031169-hud-projection-retest
```

Do not substitute old numbered WebXR candidates for the stable live website.
Native and Web builds share the same source commit but remain separate build
artifacts and qualification lanes.

## Workspace and agent rules

Read `C:\Devstuff\QuestGames\AGENTS.md` before creating worktrees or output.
Subagents share the filesystem and do not automatically need separate
worktrees. New Surreal builds, releases, and QA evidence belong under the new
`C:\Devstuff\QuestGames\SurrealEngine` container. The first live Web release
has now exercised:

```text
SurrealEngine\out\web-e9031169
SurrealEngine\releases\materials\e9031169
SurrealEngine\releases\staging\e9031169
SurrealEngine\qa\runs\2026-07-24\e9031169\web-release
```

Do not create new top-level `surreal-*`, `build-*`, candidate, capture, or test
directories. Do not move existing registered worktrees until their agents and
processes have stopped and the migration ledger is ready.

## Exact next checks

1. Open the stable URL in the intended Quest/VDXR browser path.
2. Import a locally supplied supported game or demo folder.
3. Verify flat launch first, then enter WebXR.
4. Check projection, HUD bounds, menu orientation/hit testing, controller
   tracking, turning, trigger fire, Pulse beam direction, dual-Enforcer
   alternation, audio, immersive exit, and re-entry.
5. Save the headset report, logs, screenshots, and any video under a dated
   `SurrealEngine\qa\runs` directory.
6. If a regression is found, preserve the current server rollback and rebuild
   only from a clean committed source.

Historical `71650dd7` release notes and deployment dry-run documents remain for
provenance, but they no longer describe the live website.
