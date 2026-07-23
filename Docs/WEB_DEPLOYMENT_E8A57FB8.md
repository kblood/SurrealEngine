# Stable Web deployment: `e8a57fb8`

Date: 2026-07-24  
Public URL: `https://dionysus.dk/webxr/Ports/SurrealEngine/`

## Outcome

The single stable Surreal Engine website folder was atomically updated from
clean integration commit `e9031169` to `e8a57fb8`. The importer now links to
Epic-sanctioned OldUnreal acquisition pages for Unreal Gold and Unreal
Tournament. No installer, disc image, demo, or extracted game package is
hosted. Deus Ex remains local-import-only.

## Artifact identity

| Item | Value |
| --- | --- |
| Source commit | `e8a57fb8c2da898c83488c02110dab115cb89fd1` |
| Source tree | `5568b305241799fd045ffe94f7ba7e01daf57fa7` |
| Build mode | Release Emscripten 6.0.2, Asyncify/WasmFS OPFS, no game data |
| JavaScript | 529,284 bytes; `fb7595ab328bb9b51be9eab3a7e9b601564d6e484f73852032a18bab37e0dab6` |
| WASM | 11,041,108 bytes; `6d1899984e75da6481cf04aec655153d69e28ca449471702c3aead0562d4de92` |
| Release manifest | 5,929 bytes; `f7a3e80504787653935b0c16b7157ced7b48a5a63377d7274f7e866b43b6d64e` |
| Corresponding source | 25,146,970 bytes; `1ef83bc181c5bb948c436e002d534c3c46cb6e8af532a732f7be6c55704117f9` |
| Package | 25 files total |

## Deployment transaction

The package was uploaded to a hidden sibling directory. Its file count,
manifest, JavaScript, WASM, corresponding source, commit identity, acquisition
links, and no-game-data boundary were verified before the atomic swap. The
previous stable package is preserved at:

`/var/www/html/webxr/.SurrealEngine.rollback-e9031169-20260724`

The public progress page was updated separately and its previous version is
preserved at:

`/var/www/html/webxr/.progress.html.rollback-pre-e8a57fb8-20260724`

## Validation

- Browser/WebXR Node suites passed.
- Clean Release Emscripten compile and link passed.
- Package, corresponding-source, provenance, sanctioned-link, and prohibited
  game-data gates passed.
- Local and live headless Chrome release smokes passed with zero page errors.
- Live files matched the local hashes and returned HTTP 200 with COOP, COEP,
  CORP, no-cache, and the correct WASM MIME type.
- The live data-free gate, WebGPU, responsive layout, fullscreen, input, and
  synthetic Unreal Gold launch passed.

Physical immersive WebXR remains a human/headset gate.
