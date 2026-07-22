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

Launch the title shell or training map by passing the game root folder:

```powershell
build/Release/SurrealEngine.exe --url=DX.dx "C:\Games\Deus Ex GOTY"
build/Release/SurrealEngine.exe --url=00_Training.dx "C:\Games\Deus Ex GOTY"
```

Baseline results on 2026-07-22:

- `DX.dx` remained running for a 30-second unattended smoke test.
- `00_Training.dx` remained running and responsive for a 20-second unattended
  smoke test.
- No output was written to stdout or stderr during either test. Behavioral
  validation therefore still requires an interactive run.

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

Initial tokenizer audit findings:

- `EMAIL` is incorrectly returned as the `File` tag, so callers of
  `GetEmailInfo` receive empty metadata.
- `JC` and `JR` are accepted but returned as `None`; `JL` is not recognized.
- Parsed `DC` channel values are passed by value and never reach the output
  color.
- A token ending exactly at end-of-input is rejected by the low-level matching
  helpers.

The first compatibility slice addresses those four findings. It emits distinct
email metadata, preserves center/left/right alignment, copies validated color
channels into the parsed color, and accepts text or delimiters that end exactly
at end-of-input. After the change, the Release build completed and both
`DX.dx` and `00_Training.dx` remained responsive during 15-second unattended
smoke tests. Interactive email, book, and DataCube validation remains pending.

## Next validation targets

1. Verify books, DataCubes, email terminals, and bulletin links interactively.
2. Add support for the remaining text tags encountered through package-object
   parsing rather than raw byte scanning.
3. Capture the next failing native call in training and reduce it to a focused,
   independently reviewable change.
4. Re-test title, training, a fresh game, map travel, save, and load before
   proposing an upstream pull request.
