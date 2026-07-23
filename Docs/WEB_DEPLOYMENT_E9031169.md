# Stable Web deployment: `e9031169`

Date: 2026-07-24  
Public URL: `https://dionysus.dk/webxr/Ports/SurrealEngine/`

## Outcome

The single stable Surreal Engine website folder was atomically updated from
source commit `04687fe1` to clean integration commit
`e90311692f09d06955bd9da244c695f2484c3421`.

No numbered public release folder was created. Older public candidate and
experimental directories were moved intact to a non-public archive. The
`Ports` catalog now contains only `QuakeQuest/` and `SurrealEngine/`.

## Artifact identity

| Item | Value |
| --- | --- |
| Source tree | `9820f8af9da371711a36f03c8c49003eb600b773` |
| Build mode | Release Emscripten 6.0.2, Asyncify/WasmFS OPFS, no game data |
| JavaScript | 529,284 bytes; `fb7595ab328bb9b51be9eab3a7e9b601564d6e484f73852032a18bab37e0dab6` |
| WASM | 11,041,108 bytes; `6d1899984e75da6481cf04aec655153d69e28ca449471702c3aead0562d4de92` |
| Release manifest | `b7a49d83ed2c9cd21bd6eb97ffd0fa723cb188e8e10a7a81497a5b58f0462b94` |
| Corresponding source | 25,146,313 bytes; `603ed057178310130b281e09ca24e2a598b452ddb04d8fff5c5fff167354e6d7` |
| Package | 25 files total |

Local paths:

```text
C:\Devstuff\QuestGames\SurrealEngine\out\web-e9031169
C:\Devstuff\QuestGames\SurrealEngine\releases\materials\e9031169
C:\Devstuff\QuestGames\SurrealEngine\releases\staging\e9031169\SurrealEngine
```

## Deployment transaction

The package was uploaded to a hidden sibling directory. Before the swap, the
remote file count and manifest, JavaScript, WASM, source archive, and source
commit were verified. The old live directory was moved to:

```text
/var/www/html/webxr/.SurrealEngine.rollback-04687fe1-20260724
```

The verified staging directory was then renamed to:

```text
/var/www/html/webxr/Ports/SurrealEngine
```

The former public candidate/experimental folders are preserved at:

```text
/var/www/html/webxr/.SurrealEngine-public-archive-pre-single-20260724
```

No release or candidate data was deleted.

## Validation

- 11/11 Node browser/WebXR suites passed.
- Clean Release Emscripten compile/link passed.
- Package, corresponding-source, provenance, and no-game-data gates passed.
- Local and live headless Chrome release smokes passed.
- Live HTML, JSON, JavaScript, WASM, and source responses returned HTTP 200,
  COOP/COEP/CORP, `no-cache`, and correct content types.
- The live manifest identifies `e9031169`; live WebAssembly hash matches the
  local package.
- Cross-origin isolation, WebGPU, import gating, source retrieval, responsive
  layout, fullscreen, input events, and synthetic Unreal flat launch passed
  with zero page errors.

Physical immersive WebXR remains a human/headset gate.

The public Ports progress page was updated separately so it references only
the stable folder and current commit. Its SHA-256 is
`8693854d16e43c80c72933a0078895e9245d88fd521097e11218cd9fa4c57894`.
The previous page is preserved at:

```text
/var/www/html/webxr/.progress.html.rollback-pre-e9031169-20260724
```

## Demo boundary

The deployed code recognizes local UT99 348, Unreal Special Edition 200, and
Deus Ex 1002f demo folders. No game/demo files are included. Publishing a demo
installer or extracted data requires an artifact-specific redistribution grant
and a fresh content audit; historical magazine-CD availability alone is not
recorded as permission.
