# Local UE1 demo compatibility audit

Date: 2026-07-22

## Scope and handling

This audit inspected `C:\Devstuff\QuestGames\demos` read-only. Archives were
listed in place. Three original distributions were extracted only into the new
local audit directory `C:\Devstuff\QuestGames\demos-audit-20260722`; originals
were not executed, overwritten, moved, or deleted. No game file belongs in a
source commit, browser package, or GitHub release.

The `.crdownload` file was incomplete and locked by its producing application,
so it was not hashed, opened, or identified.

## Artifact inventory

| Artifact | Bytes | SHA-256 | Format and provenance found inside |
|---|---:|---|---|
| `ut99-demo-v348.exe` | 55,647,232 | `59e65276c67af7859935d79001b93934928b3e1c3a8cc300807cdfa95208f708` | Unsigned x86 self-extracting ZIP; `Manifest.ini` says `Unreal Tournament Demo`, version `348demo`; readme names Epic Games, Digital Extremes, and GT Interactive. |
| `deus-ex-demo-v1.002f.exe` | 146,220,544 | `997700876bbc3af74fa0a7d336dbe0e9ea085f871a73261239dfc2801d9fa6bd` | Unsigned x86 self-extracting ZIP; `Manifest.ini` says `Deus Ex Demo`, version `1002f_DEMO`; readme copyright notice names Ion Storm and Eidos. |
| `UnrealSpecialEdition.7z` | 128,609,961 | `c158b030b39987aebbcbcd766b281b0b6020263a17b1989f8fd2c63a25a63855` | 7z repack of an InstallShield-style Unreal distribution. The readme names Epic MegaGames, Digital Extremes, and GT Interactive. Inspection of the executable and packages identifies this as Unreal build 200 “Special Edition”; no bundled redistribution license was found. |
| `Unreal_S3TC_MapPack.7z` | 134,768,847 | `a2143a0a52216226aa3b1cc60a2a707ca9872b2effe7ff9bf4c9547fd4b3ffa0` | 7z add-on only: 11 maps, 22 texture packages, and `Help/S3TC.txt`; not a complete game. |
| `UnrealDemoFixes.7z` | 22,803,813 | `2886b6b2cb7f9aacffb568d955ea2aeebcbc0f1650b07b794ed3175f72d402f4` | 7z third-party patch only. `delacroix.txt` describes a no-CD executable, UnrealEd executable, and patched proprietary `UnrealI.u`; no license grant was found. |

The repository-local `demos/README.md` says the two self-extracting demos came
from a Data Dungeon preservation mirror. A preservation mirror establishes
provenance of the download, not permission for this project to redistribute it.

## Detection and launch matrix

The following status is from the isolated `integration/ue1-demo-import` topic.
All three definitions are deliberately labeled **experimental**. “Package scan”
means construction of the engine and its package manager with the original
audit folder; it is not a menu, map, gameplay, input, or save-system claim.

| Distribution | Browser folder validation | Native detection | Package scan / boot | Remaining support | Redistributable by this project |
|---|---|---|---|---|---|
| UT99 demo 348 | Passes requested and automatic synthetic import as `ut99-demo-348`. Its distinct `*DEMO.unr` evidence wins before full UT and Unreal definitions. | Passes exact SHA-1 `4bb5e71f78cf4806d9240df01f72236134af4a31` as `UT99_348_DEMO`, version `348demo`. | Package scan passes. Bounded headless setup reaches the driver seam and exits as expected for the deliberately unknown scan driver. Menu/map/gameplay remain untested. | Run `DM-TurbineDEMO` or another included demo map through flat and XR presentation; verify old package/VM behavior, menu, input, audio, and mutable saves before advertising it. | **Unclear.** The archive has no game redistribution license, and no current authoritative Epic grant for mirroring this legacy binary was located. Use local user import unless written permission is obtained. |
| Deus Ex demo 1002f | Passes requested and automatic synthetic import as `deus-ex-demo-1002f`; `.dx` maps, `DeusEx.ini`, isolated storage, `SE-DeusEx.ini`, and `.dxs` saves are recognized. | Passes exact SHA-1 `4be582d4194400e87f64894c92b3f2119e012251` as `DEUS_EX_1002f_DEMO`, version `1002f_DEMO`, without changing the retail 1002f identity. | Package scan passes after treating a fresh install's absent `Save/` directory as an empty save list. Menu/map/gameplay remain untested. | Controlled boot of `00_Training`; verify demo-specific classes, conversations, UI, input, map travel, saves, and flat/XR presentation. | **No for a hosted project copy.** The bundled Eidos license limits use to private/domestic use and describes transfer only as giving away the entire product while retaining no copy, which is incompatible with maintaining a public mirror. Obtain permission or require local import. |
| Unreal Special Edition demo 200 | Passes requested and automatic synthetic import as `unreal-demo-200`. Its real contract uses `UnrealI.u`, `UnrealIOrder.u`, and `Default.ini`; it does not pretend to be Unreal Gold. Mutable `SE-Unreal.ini` is allowed. | Passes exact SHA-1 `b851dcc69c4f773252c0498bd12756d90bcb59c2` as `UNREAL_200_DEMO`, version `200`. | Detection and a bounded read-only package scan pass. Early Unreal reads configuration from the system INI, missing `DefUser.ini` falls back to `Default.ini`, optional `UPak` registration is package-gated, and VM return parameters are excluded from the input argument stream. Menu/map/gameplay remain untested. | Run the included startup map through flat and XR presentation; verify old package/VM behavior, menu, input, audio, and mutable saves before advertising it. Do not use the third-party no-CD/fix archive. | **Unclear.** “Shareware” is historical evidence, not an explicit license in this archive. No authoritative current publisher grant for this exact repack was located. Require local import unless permission and an authoritative original artifact are obtained. |
| S3TC map pack | Not a complete selectable folder. | Not applicable. | Not standalone. | If useful, design a separate optional add-on importer with collision/override policy and verify ownership of every package. | **Unclear.** The bundled note explains use but contains no redistribution grant. Do not package it. |
| Unreal Demo Fixes | Not a complete selectable folder. | Patched executable/package are not in the database. | Not standalone. | None recommended for release. Any needed engine compatibility should be implemented in SurrealEngine without distributing patched proprietary files or a no-CD binary. | **No.** No license grant was found, and the archive contains modified proprietary executable/package content. |

Browser validation was run against file names/sizes with dummy blob providers;
it did not copy game contents. Native detection and package scans read only the
three local audit folders. No gameplay process or public demo download was
started. Passing detection or browser validation alone is not an engine-support
claim.

## Redistribution evidence and release rule

The bundled documents are the strongest artifact-specific evidence. The Deus
Ex `System/license.int` grants private/domestic use and reserves other rights;
its narrow one-copy transfer language does not authorize a durable web or
GitHub mirror. The UT demo and Unreal Special Edition contain readmes and
publisher credits but no located grant to this project. The current
[Epic EULA index](https://www.epicgames.com/help/c-202300000001639/c-202300000001738/end-user-license-agreements-a202300000010624)
does not provide a legacy-demo redistribution license. Embracer's official
[Crystal Dynamics–Eidos page](https://www.embracer.com/about/operative-groups/crystal-dynamics-eidos-montreal/)
identifies Deus Ex in the current portfolio but gives no download or
redistribution permission.

Community pages can help identify versions, but cannot grant rights. In
particular, the OldUnreal community discussion relies on inference from the
product being a demo/shareware build. Until a rights holder supplies explicit
permission, the website should distribute only the data-free engine and let
each user select a lawfully obtained local folder.

## Integration order

1. Keep the exact hash/version descriptors and browser distribution definitions
   together in this reviewable topic; do not add game files or XR conditionals.
2. Land the generic absent-save-directory guard independently if desired; it is
   useful to fresh full-game installs as well as the Deus Ex demo.
3. Treat UT99 348 and Deus Ex 1002f as experimental package-scan support until
   real map/menu/gameplay tests pass on local user data.
4. Keep each Unreal 200 property-layout rule isolated and covered by a focused
   regression test. The v61 `Parent`/`Outer` reflection rule and legacy
   `StructProperty(DynamicString)` storage are independent of the remaining
   `UPak` content dependency; do not weaken bounds or package lookup globally
   to force a boot.
5. Keep website artifacts data-free. Permission to redistribute a demo must be
   documented per exact artifact/hash; availability on a mirror is insufficient.

## Reproducible checks

- `UE1GameDatabaseTests` checks the three exact executable SHA-1 values, version
  metadata, experimental flags, and the unchanged retail Deus Ex 1002f hash.
- Passing the three audit folder paths to that executable performs the bounded
  engine/package scan used for the matrix above; the ordinary CTest has no
  dependency on local commercial data.
- `node web/test_ue1_demo_imports.mjs` validates synthetic requested/automatic
  imports, `.unr`/`.dx` map manifests, full UT99/Unreal Gold non-regression, and
  the mutable config/save allowlist.
- The existing release-package and browser-launcher suites remain data-free and
  pass. OPFS round-trip checks still require an HTTP origin rather than `file:`.
