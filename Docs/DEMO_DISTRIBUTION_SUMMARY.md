# Unreal demo distribution summary

Date: 2026-07-24

This is a conservative release decision, not legal advice.

## Decision

The website may link users to the Epic-sanctioned Unreal Gold and Unreal
Tournament distributions maintained by OldUnreal. It must not host or repackage
the local Unreal Tournament 348 or Unreal Special Edition 200 demo archives.
The Deus Ex 1002f demo remains local-import-only.

| Title | What the public evidence permits | Website policy |
| --- | --- | --- |
| Unreal / Unreal Gold | Epic's current Unreal page links to OldUnreal's installer. Epic also confirmed that the specific Internet Archive versions may be linked to and played. | Link to the sanctioned OldUnreal download; the user installs it and imports the local folder. Do not mirror the audited Unreal Special Edition demo repack. |
| Unreal Tournament | Epic's page likewise links to OldUnreal's installer, whose approved source is the sanctioned full-game image. | Link to the sanctioned OldUnreal download; the user imports the installed folder. Do not mirror the audited demo 348 executable. |
| Deus Ex demo | No comparable public rightsholder authorization was found. Its bundled Eidos licence prohibits further copying and permits only a one-copy transfer in which the sender retains no copy. | Local user import only; no download link, mirror, or bundled files. |

## Why this is narrower than “the demos are free”

Epic's statement covers the specific full-game versions available through
Internet Archive and OldUnreal. It is good evidence for linking to those
versions, but it is not a general redistribution licence for every historical
demo, repack, or modified package. The two local Epic demo artifacts are
different files and contain no located redistribution grant:

- `ut99-demo-v348.exe` — SHA-256
  `59e65276c67af7859935d79001b93934928b3e1c3a8cc300807cdfa95208f708`;
- `UnrealSpecialEdition.7z` — SHA-256
  `c158b030b39987aebbcbcd766b281b0b6020263a17b1989f8fd2c63a25a63855`.

The WASM release therefore remains data-free. Users obtain Unreal Gold or
Unreal Tournament from the externally hosted, sanctioned pages and select the
installed folder. No game file is sent to or copied from this website.

## Sources

- [Epic's current Unreal and Unreal Tournament page](https://www.epicgames.com/unrealtournament/)
- [OldUnreal: Unreal Gold installer](https://www.oldunreal.com/downloads/unreal/full-game-installers/)
- [OldUnreal: Unreal Tournament GOTY installer](https://www.oldunreal.com/downloads/unrealtournament/full-game-installers/)
- [Epic's statement reported by PC Gamer](https://www.pcgamer.com/games/fps/unreal-gold-and-unreal-tournament-are-now-free-on-the-internet-archive-and-epic-says-thats-a-okay/)
- [Sanctioned Unreal Tournament GOTY archive item](https://archive.org/details/ut-goty)
