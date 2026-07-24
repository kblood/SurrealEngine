# Browser static relinking materials

This document describes the reproducible source/rebuild materials shipped for
the static WebAssembly release. It is an engineering record, not legal advice
or a declaration that a particular distribution complies with every applicable
license or law. A release owner must have the final package and hosting terms
reviewed.

## Component and license audit

The Emscripten target appends `SURREALVIDEO_SOURCES` directly to
`SurrealCommon`, which is statically linked into `SurrealEngine.wasm`. Native
targets instead build `SurrealVideo` as a shared library.

`SurrealVideo/README.md` identifies the component as a stripped FFmpeg fork for
Indeo Video 5 and Microsoft ADPCM. The bundled FFmpeg-derived source headers
identify LGPL version 2.1-or-later terms. The repository carries the LGPL 2.1
and LGPL 3 texts. The precise upstream FFmpeg release or commit from which the
original 2025 import was made is not recorded; release metadata therefore does
not invent one. It identifies the exact SurrealEngine commit/tree and hashes
every tracked file below `SurrealVideo/`.

LGPL 2.1 section 6(a) describes corresponding library source plus a
machine-readable work, in object and/or source form, that permits modification
of the library and relinking. The mechanism here packages the complete tracked
source tree and its build system, rather than a partial decoder-only archive,
so a recipient can modify `SurrealVideo` and rebuild the complete WASM program.
Whether that mechanism and the publisher's surrounding terms satisfy all
requirements is a question for human/legal review.

Primary references:

- GNU LGPL 2.1: <https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>
- FFmpeg legal and license checklist: <https://ffmpeg.org/legal.html>

## Generated artifacts

Run from a clean committed source tree. Put the archive outside the repository:

```powershell
node web/package_corresponding_source.mjs `
  --source-root . `
  --output C:\release-materials\SurrealEngine-corresponding-source.tar.gz
```

This uses `git archive` and produces:

- `SurrealEngine-corresponding-source.tar.gz`, containing the complete tracked
  source tree at one commit; and
- `SurrealEngine-corresponding-source.tar.gz.json`, recording archive SHA-256,
  byte length, commit, tree, commit timestamp, repository URL, archive scope,
  build-instruction path, license paths, and per-file SurrealVideo hashes.

The command rejects dirty trees and outputs inside the source tree. Repeating
it for the same commit produces the same archive hash with the supported Git
tooling.

Every Emscripten build also writes `build-compliance-provenance.json` beside the
WASM output. Version 2 records the source commit/tree and dirty state; exact
Emscripten/compiler/CMake versions; build type; diagnostic or production
profile; assertion and stack-check levels; memory policy; pthread pool;
WasmFS/Asyncify/proxy execution mode; browser entry point; and `static-wasm`
linkage. The release packager rejects incomplete or contradictory provenance,
dirty provenance, and source/build identity mismatches.

## Rebuild with a modified SurrealVideo

The source archive intentionally contains no commercial game data. After
extracting it, use the compiler version recorded in `source-compliance.json`:

```powershell
& C:\path\to\emsdk\emsdk_env.ps1
emcmake cmake -S . -B build-emscripten -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Release `
  -DSURREAL_WEB_RELEASE_PROFILE=production `
  "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" `
  -DBUILD_TESTING=OFF `
  -DSURREAL_GAMEDATA_DIR=
cmake --build build-emscripten --target SurrealEngine --parallel 8
```

Modify the files under `SurrealVideo/` before the final command to produce a
WASM executable containing the modified decoder. The expected outputs are
`build-emscripten/SurrealEngine.js` and
`build-emscripten/SurrealEngine.wasm`. No UT99, Unreal, Deus Ex, or KHG data is
needed to compile or relink; users import their own data at runtime.

The build depends on Emscripten's public toolchain/ports and on the tracked
third-party source in the archive. Toolchain installation itself is not bundled
in the website package. Exact compiler and CMake versions are provenance, not a
promise that future toolchain releases reproduce byte-identical output.

## Package the binary and matching source

To include the archive in the website package:

```powershell
node web/package_browser_release.mjs `
  --engine-dir build-emscripten `
  --corresponding-source C:\release-materials\SurrealEngine-corresponding-source.tar.gz.json `
  --output C:\release\SurrealEngine
```

Alternatively, pass `--source-url https://example.invalid/path/archive.tar.gz`
to link to the already-generated archive rather than copying it. The packager
requires HTTPS for an external URL. The publisher must upload the exact archive
whose name, size, and SHA-256 appear in its metadata.

The package contains a visible source link in `index.html`, `SOURCE-OFFER.txt`,
the LGPL 2.1 text selected for binary distribution, the SurrealVideo notice,
these instructions, `source-compliance.json`, and matching hashes in
`release-manifest.json`.

## Human release review still required

Before distribution, a responsible person should verify at least:

- the binary and source are served together or the HTTPS source link is durable
  and publicly retrievable for the relevant distribution period;
- the hosted archive matches the advertised SHA-256 and can rebuild after a
  clean extraction with the recorded toolchain;
- all product terms permit user modification and reverse engineering for
  debugging those modifications where the license requires it;
- notices are prominent at every actual binary download/distribution point and
  are preserved in mirrors, app wrappers, and translated pages;
- modifications to SurrealVideo remain available under the applicable LGPL
  terms; and
- counsel reviews the final distribution model, jurisdictions, other bundled
  dependencies, trademarks, patents, and any store-specific terms.

This mechanism does not reconstruct the unknown historical FFmpeg import
baseline. The complete current fork source and exact file hashes are the
auditable materials corresponding to this build.
