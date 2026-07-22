# Deus Ex support

Deus Ex support is experimental. Version 1112fm reaches the title screen and
the early training maps, but significant native game and UI behavior is still
missing. This document records a reproducible baseline and the evidence used to
prioritize compatibility work.

## Tested game data

The current baseline uses the GOG Game of the Year release with the original
1112fm executable:

| File | SHA-1 |
| --- | --- |
| `System/DeusEx.exe` | `2a933e26aa9cfb33b37f78afe21434caa031f14a` |

This hash is already recognized as `DEUS_EX_1112fm` in
`SurrealEngine/UE1GameDatabase.h`. Surreal Engine maps Deus Ex to its post-UT,
pre-UE2 compatibility path (`ue1Version = 500`).

The stock installation supplies the paths Surreal Engine expects:

- `System/*.u`
- `Maps/*.dx`
- `Textures/*.utx`
- `Sounds/*.uax`
- `Music/*.umx`

Do not copy or commit proprietary game data to this repository.

## Windows build and smoke test

From a clean checkout with Visual Studio 2022 and CMake installed:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmake --build build --config Release --parallel 8
```

The policy override is required with CMake 4.0 because the bundled OpenAL
project still declares compatibility with CMake older than 3.5. Quote the whole
argument in PowerShell so `3.5` is not split into separate arguments.

Launch the title shell or a map by passing the game root folder. For unattended
compatibility work, use the non-interfering launch profile documented in
[`DeusExValidation.md`](DeusExValidation.md):

```powershell
build/Release/SurrealEngine.exe --autostart --nosound --noactivate `
  --logfile=build/deusex-smoke.log --url=DX `
  "C:\Program Files (x86)\GOG Galaxy\Games\Deus Ex GOTY"
build/Release/SurrealEngine.exe --autostart --nosound --noactivate `
  --logfile=build/deusex-training.log --url=00_Training `
  "C:\Program Files (x86)\GOG Galaxy\Games\Deus Ex GOTY"
```

Validation results on 2026-07-22:

- The game selector correctly starts Deus Ex, and the title menu, New Game,
  difficulty, character creation, and intro sequence have been exercised.
- `00_Training` and `01_NYC_UNATCOIsland` load directly. Liberty Island renders
  the world, HUD, inventory belt, player, and live actors and remains responsive
  during unattended checks.
- QuickSave (`-1`) and a menu-created numbered save (`Save0001`) both write a
  map package and `SaveInfo.dxs`. Direct startup through `?loadgame=-1` and
  `?loadgame=1` restores the Liberty Island world. Existing-slot overwrite and
  deletion now work through the stock menu, and the 160-by-120 save snapshot
  survives a metadata-package round trip. See
  [`DeusExSaveSystem.md`](DeusExSaveSystem.md) for the exact lifecycle and open
  issues.
- Every final validation launch uses a null audio backend and a non-activating
  window. Foreground-window handles were polled during the final save checks;
  the game window was never foreground.

These checks do not demonstrate that training or the campaign can be completed,
that scripted map travel is correct, or that saves remain compatible across the
whole campaign. Earlier notes claiming a successful all-map scan were based on
an invalid launcher flow and have been removed.

## Text package findings

A byte-level inventory of tag-shaped strings in the stock `DeusExText.u`
package found the following tags. Counts are useful for prioritization, but are
not a replacement for parsing the package objects.

| Tag | Count | Purpose |
| --- | ---: | --- |
| `P` | 3955 | Paragraph |
| `B`, `/B` | 642, 536 | Bold text |
| `DC` | 399 | Default color |
| `JC`, `JL`, `JR` | 215, 3, 2 | Center, left, and right alignment |
| `COMMENT`, `/COMMENT` | 173, 171 | Comment section |
| `EMAIL` | 137 | Email metadata |
| `I`, `/I` | 98, 98 | Italic text |
| `FILE` | 35 | Linked document metadata |
| `PLAYERNAME` | 12 | Player name substitution |
| `PLAYERFIRSTNAME` | 10 | Player first-name substitution |

The original tokenizer audit found these concrete defects:

- `EMAIL` is incorrectly returned as the `File` tag, so callers of
  `GetEmailInfo` receive empty metadata.
- `JC` and `JR` are accepted but returned as `None`; `JL` is not recognized.
- Parsed `DC` channel values are passed by value and never reach the output
  color.
- A token ending exactly at end-of-input is rejected by the low-level matching
  helpers.

The text implementation now handles the complete original 30-value token table,
including player-name substitutions, formatting, escaped angle brackets,
graphic and font names, and comment, note, and goal blocks. Metadata parsing is
deliberately forgiving because the stock package is not uniform: its 137
`EMAIL` tags include 15 records without a CC field, one empty record, and one
record with an extra trailing field. `FILE` and `EMAIL` fields are trimmed, and
missing fields remain empty.

The wrapper behavior also matches the reference DLL where it differs from the
old implementation:

- `SetPlayerName` derives the first name at the first space.
- `GetName` only exposes names for note, graphic, font, and label tokens.
- `GetColor` exposes parsed colors for `DC` and `C`, the configured default for
  `/C`, and a zero color for other tokens.
- `GotoLabel` remains a no-op because that is the behavior of the original
  implementation.
- `ExtString.GetFirstTextPart` and `GetNextTextPart` return the number of
  characters copied and split speech text into consecutive 239-character
  pages. The previous implementation omitted the final character of the first
  page and did not implement subsequent pages.

### Original parser reference

The stock 1112fm `DeusExText.dll` retains named exports for
`DDeusExTextParser`, including `ParseTag`, `ParseColor`, `ParseEmail`,
`ParseFile`, and `GotoLabel`. Its internal token table maps values 0 through 29
to the same order used by `DeusExTextTags`:

```text
TEXT FILE EMAIL NOTE /NOTE GOAL /GOAL COMMENT /COMMENT
PLAYERNAME PLAYERFIRSTNAME NP JC JL JR DC C /C P B /B U /U I /I
G F L /< />
```

This independently confirms that `JC`, `JL`, and `JR` are the center, left,
and right alignment tokens and that `EMAIL` is distinct from `FILE`. Future
tokenizer changes should compare observable behavior with these exported
reference functions and must not require or redistribute the proprietary DLL.

Disassembly was also used to establish narrowly scoped observable behavior for
block consumption, metadata field handling, color values, player first-name
derivation, `GetName`, `GetColor`, `GotoLabel`, and `ExtString` paging. The
replacement code is an independent implementation and neither links to nor
redistributes the original binaries.

## AI perception findings

The public 1112fm SDK can export the stock UnrealScript and supplies the native
headers used by that script. The SDK installer used for this investigation has
MD5 `1d7560c513f945b607ee96cd2f9aec57`; its files and exported proprietary game
scripts are reference material and are not part of this repository.

`ScriptedPawn` calls `AICanSee` throughout target acquisition, alarm, tracking,
and combat paths. Surreal Engine previously returned zero from `AICanSee`,
`AICanHear`, and `AIVisibility`, which prevented those paths from detecting an
actor. The 1112fm `Engine.dll` behavior establishes the following:

- Actors without `bDetectable` are ignored.
- Hearing uses linear distance attenuation, treats vertical separation as
  twice horizontal separation, defaults a non-positive radius to 800 units,
  subtracts `HearingThreshold`, and clamps the result to zero through one.
- Sight factors in the target collision cylinder's apparent angular size,
  view direction, visibility, `VisibilityThreshold`, and line of sight.
- `AIVisibility` caches light visibility at quarter-second intervals and can
  increase visibility by up to 50 percent based on velocity from 30 through
  200 units per second.
- `AICanSmell` always returns zero in the original 1112fm implementation.

The current implementation restores those major stages and the exact hearing,
motion, threshold, and smell behavior. Sight uses the original angular-size
scale and native options, with Surreal Engine collision traces for the
line-of-sight test. Two fidelity gaps remain: light-mesh sampling currently
falls back to full light, and the original smooth falloff at the edges of the
view cone is currently a pass/fail cone check. These need interactive stealth
comparisons before the AI work is considered complete.

## Automated compatibility coverage

The platform-independent text and AI calculations have CTest targets so they
can be validated without loading proprietary game packages:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

The tests cover text at exact end-of-input, every token-table entry, all three
alignment modes, RGB parsing, case-insensitive tags, player substitutions,
block consumption, escaped angle brackets, file metadata, the stock email
record shapes, and 239-character paging boundaries. The Release test run passed
on 2026-07-22. AI tests cover horizontal and vertically weighted hearing,
default radius and thresholds, apparent-size sight scoring, light visibility,
minimum angular size, motion interpolation, and clamping. Save-path tests cover
the QuickSave and numbered-slot directory naming contract. Property-
serialization coverage verifies that nested booleans emit their required
zero/one payload bytes while tagged booleans do not. New-slot allocation,
metadata round trips, and rejected indices still need focused coverage.

## Next validation targets

The maintained goals and pull-request gates are in
[`DeusExRoadmap.md`](DeusExRoadmap.md). Immediate priorities are completing the
non-proprietary save regression coverage, exercising training and scripted
travel, and comparing AI behavior against the original game.
