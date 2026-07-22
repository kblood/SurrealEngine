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
| `UnrealSpecialEdition.7z` | 128,609,961 | `c158b030b39987aebbcbcd766b281b0b6020263a17b1989f8fd2c63a25a63855` | 7z repack of an InstallShield-style Unreal distribution. The readme names Epic MegaGames, Digital Extremes, and GT Interactive. Community version references identify this as the Unreal 205 OEM/shareware “Special Edition”; no bundled redistribution license was found. |
| `Unreal_S3TC_MapPack.7z` | 134,768,847 | `a2143a0a52216226aa3b1cc60a2a707ca9872b2effe7ff9bf4c9547fd4b3ffa0` | 7z add-on only: 11 maps, 22 texture packages, and `Help/S3TC.txt`; not a complete game. |
| `UnrealDemoFixes.7z` | 22,803,813 | `2886b6b2cb7f9aacffb568d955ea2aeebcbc0f1650b07b794ed3175f72d402f4` | 7z third-party patch only. `delacroix.txt` describes a no-CD executable, UnrealEd executable, and patched proprietary `UnrealI.u`; no license grant was found. |

The repository-local `demos/README.md` says the two self-extracting demos came
from a Data Dungeon preservation mirror. A preservation mirror establishes
provenance of the download, not permission for this project to redistribute it.

## Detection and launch matrix

| Distribution | Browser folder validation | Native `UE1GameDatabase` | Boots now | Missing support | Redistributable by this project |
|---|---|---|---|---|---|
| UT99 demo 348 | Passes when explicitly selected as UT99. Automatic detection previously chose Unreal because the demo legitimately includes `UnrealShare.u`; this branch fixes that generic precedence bug. | No. `System/UnrealTournament.exe` SHA-1 is `4bb5e71f78cf4806d9240df01f72236134af4a31`, absent from the database. | No: native selection stops before engine startup. | Add a distinct 348-demo version/hash, verify old package/VM behavior, choose one of the included `*DEMO` maps, and perform real play/menu/input tests before advertising it. | **Unclear.** The archive has no game redistribution license, and no current authoritative Epic grant for mirroring this legacy binary was located. Use local user import unless written permission is obtained. |
| Deus Ex demo 1002f | Rejected as UT99; the browser has no Deus Ex definition and only accepts `.unr` maps. | No. Demo executable SHA-1 is `4be582d4194400e87f64894c92b3f2119e012251`, while the database's 1002f hash is a different build. | No: native selection stops before engine startup. | Add a demo-specific native hash only after testing; add a browser Deus Ex descriptor, `.dx` maps, required packages/config, launch defaults, persistence namespace, release manifest entry, and gameplay tests. | **No for a hosted project copy.** The bundled Eidos license limits use to private/domestic use and describes transfer only as giving away the entire product while retaining no copy, which is incompatible with maintaining a public mirror. Obtain permission or require local import. |
| Unreal Special Edition / OEM demo 205 | Rejected as Unreal Gold because it lacks `UnrealShare.u` and a generated `Unreal.ini`; raw media has `Default.ini`. | No. `System/Unreal.exe` SHA-1 is `b851dcc69c4f773252c0498bd12756d90bcb59c2`, absent from the database. | No: native selection stops before engine startup. | Add Unreal 205 as a distinct version; define its smaller package contract; safely seed mutable `Unreal.ini` from defaults; test maps through the known incomplete OEM ending; do not depend on the third-party no-CD/fix archive. | **Unclear.** “Shareware” is historical evidence, not an explicit license in this archive. No authoritative current publisher grant for this exact repack was located. Require local import unless permission and an authoritative original artifact are obtained. |
| S3TC map pack | Not a complete selectable folder. | Not applicable. | Not standalone. | If useful, design a separate optional add-on importer with collision/override policy and verify ownership of every package. | **Unclear.** The bundled note explains use but contains no redistribution grant. Do not package it. |
| Unreal Demo Fixes | Not a complete selectable folder. | Patched executable/package are not in the database. | Not standalone. | None recommended for release. Any needed engine compatibility should be implemented in SurrealEngine without distributing patched proprietary files or a no-CD binary. | **No.** No license grant was found, and the archive contains modified proprietary executable/package content. |

“Boots now” records the safe local result: all three full distributions fail the
native hash gate, so no gameplay process was started. Browser validation was
run against file names/sizes with dummy blob providers; it did not read or copy
game contents. Passing browser validation alone is not an engine-support claim.

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

1. Land the generic browser auto-detection precedence regression in this topic;
   it improves full UT installations too and makes no demo support claim.
2. Add native demo hashes/versions in a separate detection-only topic, with
   synthetic hash-registry tests and explicit “experimental” capability state.
3. Prove package listing and a controlled boot locally for each version before
   adding it to browser `GAME_DEFINITIONS` or the public launcher.
4. Add Deus Ex `.dx` and per-title package/config rules through the shared game
   support registry, not demo-specific conditionals scattered through VR/WebXR.
5. Keep website artifacts data-free. Permission to redistribute a demo must be
   documented per exact artifact/hash; availability on a mirror is insufficient.
