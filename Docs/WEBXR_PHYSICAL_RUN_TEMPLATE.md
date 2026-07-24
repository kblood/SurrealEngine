# WebXR physical-run evidence template

Use this scaffold for the physical matrix defined by the candidate's own
qualification card. It records observations without game files, saves, private
paths, folder-picker captures, or raw logs. Mock WebXR and capability-only
results do not satisfy it.

## Case layout

Keep the client/runtime row separate from the game case. A full candidate has
seven records:

| Matrix row | Game case |
| --- | --- |
| `Q1` | `ut99` |
| `Q1` | `unreal-gold` |
| `D1` | `official-sample` |
| `E1` | `ut99` |
| `E1` | `unreal-gold` |
| `E2` | `ut99` |
| `E2` | `unreal-gold` |

Store each record and its relative attachments under:

```text
SurrealEngine/qa/runs/<date>/<candidate-commit>/wp7-physical/<matrix-row>/<game-case>/
```

The QA run directory is outside Git. Do not copy commercial game data into it.

## Pin the candidate

Create a reviewed candidate JSON file from the new qualification card and
release/package manifests. The file may contain the candidate object directly,
or a prior physical-run object with a top-level `candidate` field:

```json
{
  "qualificationCard": "Docs/WEBXR_PHYSICAL_QUALIFICATION_<COMMIT>.md",
  "browser": {
    "buildId": "<12-hex-commit>-<16-hex-wasm-prefix>",
    "sourceCommit": "<40-hex>",
    "sourceTree": "<40-hex>",
    "manifestSha256": "<64-hex>",
    "javascriptSha256": "<64-hex>",
    "wasmSha256": "<64-hex>",
    "correspondingSourceSha256": "<64-hex>",
    "immutableGenerationUrl": "https://<host>/<path>/releases/<manifest-sha256>/"
  },
  "electron": {
    "electron": "<exact version>",
    "chrome": "<exact four-part version>",
    "zipName": "<filename only>.zip",
    "zipSha256": "<64-hex>",
    "status": "unsigned-internal-diagnostic"
  }
}
```

Both generation and validation require an explicit candidate. This prevents a
superseding build from accidentally reusing old hashes. The
`--frozen-9aa65824` flag exists only to reproduce the historical example in
`WEBXR_PHYSICAL_QUALIFICATION_9AA65824.md`; do not use it for a rebuilt
candidate.

## Create a record

Create the directory first, put any already-sanitized evidence below it, then
generate the record. Repeated `--attachment` arguments hash existing relative
files into the initial inventory.

```powershell
python web/qualification/new_physical_run.py `
  --candidate C:\path\to\expected-candidate.json `
  --row Q1 `
  --game ut99 `
  --tester lab-a `
  --output C:\Devstuff\QuestGames\SurrealEngine\qa\runs\<date>\<commit>\wp7-physical\Q1\ut99\run.json `
  --attachment before-diagnostics=before.json `
  --attachment running-diagnostics=running.txt `
  --attachment after-diagnostics=after.json `
  --attachment video=headset.webm
```

Use `--game official-sample` for D1. The generator deliberately writes null and
`not-run` placeholders. It never creates passing evidence; fill the record from
the physical run and validate it afterward.

For a later attachment, add an inventory entry containing a unique ID, role,
relative forward-slash path, MIME type, byte count, SHA-256, and UTC capture
time. Re-running the generator is not an update mechanism because it refuses to
overwrite an existing record.

## Game-case requirements

Q1, E1, and E2 require exactly ten indexed cycles. Preserve failed attempts as
`result: fail`; do not delete and renumber them. Across the cycles, include at
least one `game-ui` exit and one `headset-system` exit. Every passing cycle must
record:

- session request, activation, first rendered frame, exit, and flat-restored UTC
  timestamps;
- two immersive views with positive viewports and positive XR/frame-time sample
  counts;
- entry, first-frame, and exit latency; skipped frames; WebXR visibility changes;
  p50/p95/p99 frame times; and maximum simulation delta;
- pre/post WASM heap, WebGL texture count, and WebGL context generation;
- stable engine, level, player, renderer, and audio identities as equality
  results rather than raw pointer-like values;
- monotonic tick counters and human continuity checks for the same map,
  position, health, weapon, and audio;
- no duplicate update, large time step, black/stale eye, or stuck input/haptics;
  plus recovered pointer lock.

Declare resource limits before interpreting the run. Validation compares the
first pre-cycle and final post-cycle heap/texture values with those limits and
checks each passing cycle against `maxFrameDeltaUs`.

Record each required recovery scenario exactly once:

1. `denied-entry`
2. `headset-sleep-obscured`
3. `controller-disconnect-reconnect`
4. `forced-session-end`

A passing scenario requires both recoverable flat play and a later successful
entry.

D1 has no game cycles. Pin the exact official sample URL and captured content
SHA-256, then record capability, granted session, an actual two-view first
frame, controller observation, and successful exit. `isSessionSupported()` by
itself is not a passing baseline.

## Attachment requirements

Game cases require `before-diagnostics`, `running-diagnostics`, and
`after-diagnostics`. E1/E2 additionally require `electron-diagnostics`. D1
requires `sample-diagnostics`. Every case also requires at least one sanitized
`screenshot` or `video`.

Allowed attachment types are JSON, UTF-8 text/Markdown, PNG/JPEG, MP4, and WebM.
The validator resolves every path below the record directory and verifies byte
count and SHA-256. It rejects symlinks, traversal, unsupported archive/game/save
extensions, invalid JSON, private absolute paths, and commercial/save/executable
filenames found in text evidence. Text evidence is bounded to 5 MiB.

Do not attach:

- `.u`, `.unr`, `.utx`, `.uax`, `.umx`, `.usa`, save, archive, executable, or
  DLL payloads;
- imported filenames, local game/library paths, AppData paths, saves, folder
  pickers, or raw browser/engine/runtime logs;
- screenshots or video that expose private paths or account information.

## Validate

Validate against the independently supplied candidate file, not only the
identity embedded in the run:

```powershell
python web/qualification/validate_physical_run.py `
  C:\Devstuff\QuestGames\SurrealEngine\qa\runs\<date>\<commit>\wp7-physical\Q1\ut99\run.json `
  --expected-candidate C:\path\to\expected-candidate.json
```

Exit codes are:

- `0`: complete passing evidence;
- `1`: invalid, incomplete, `not-run`, unsafe, missing, or hash-mismatched
  evidence;
- `2`: structurally complete failure evidence. Preserve it, but never count it
  as a passing promotion row.

Run the scaffold's focused tests with:

```powershell
python -m unittest discover -s web/qualification -p "test_*.py" -v
```

The JSON Schema is `web/qualification/physical-run-v1.schema.json`. Semantic
validation is deliberately stricter than the schema: it verifies candidate
equality, attachment contents and hashes, timestamp ordering, matrix/game
pairing, ten-cycle completeness, recovery categories, pass consistency, and
privacy bounds.
