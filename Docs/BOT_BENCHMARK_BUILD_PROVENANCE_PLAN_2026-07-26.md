# Bot benchmark build-provenance plan (2026-07-26)

## Decision

Observer and qualification runs need a build identity emitted by the running
engine, rather than trusting only the matrix runner's pre-launch record.  The
smallest useful, fail-closed design is a required `build_identity` object in
both `manifest.json` and `summary.json`, plus a digest of that object in the
first telemetry record.  It identifies both the source state captured when the
binary was built and the exact executable which wrote the evidence.

This is deliberately a plan, not a runtime change.  It does not alter the
currently active warning-observer integration.

## What exists today

`Run-BotBenchmarkMatrix.py` already writes a matrix-level `provenance.json`.
For every variant it records the selected executable's resolved path, byte
size, and uppercase SHA-256.  It also records the runner checkout's Git
commit, tree, dirty state, and a dirty-state digest.  In release mode the
runner hashes untracked contents as well.  This is valuable launch evidence,
but it is external to the child's `manifest.json`/`summary.json`: a copied run
directory can therefore be analysed without carrying an engine-attested build
identity.

The engine currently writes a v2 manifest in
`BotBenchmarkTelemetryProtocol::ManifestJson`, writes telemetry after that in
`BotBenchmarkDriver::OpenTelemetry`, and writes the v3 summary only when the
run finishes.  Neither document carries build provenance.  The existing
browser-only `SurrealBuildProvenance` CMake target is not a desktop benchmark
solution; it is guarded by `EMSCRIPTEN` and produces a separate compliance
file.

## Required record

Use one canonical JSON object, with a fixed member order at emission and a
canonical byte serialization for its digest:

```json
{
  "schema": "surreal-engine-build-identity-v1",
  "source": {
    "commit": "0123456789abcdef0123456789abcdef01234567",
    "tree": "0123456789abcdef0123456789abcdef01234567",
    "dirty": false,
    "state_sha256": "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
  },
  "executable": {
    "name": "SurrealEngine.exe",
    "size_bytes": "12345678",
    "sha256": "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"
  }
}
```

Rules:

- `commit` and `tree` are exactly 40 lowercase hexadecimal Git object IDs.
- `dirty` is a JSON boolean.  `state_sha256` is always present and is a 64
  character uppercase SHA-256: it is the digest of the exact build-time Git
  status, binary diff, and untracked content inputs.  A clean build has the
  same defined digest procedure with empty change inputs; do not use an empty
  string or `null` as a clean marker.
- `name` is the executable filename only, never a machine-specific path.
  `size_bytes` is a decimal string, avoiding JSON-number precision loss; it
  must be positive.  `sha256` is uppercase SHA-256 over the executable bytes.
- There is no `unknown`, `unavailable`, or optional form for a qualifying run.
  If any build-time source field cannot be captured, or the running executable
  cannot be located, read, sized, or hashed, the benchmark must fail before
  opening its normal telemetry stream.  A diagnostic failure summary/log may
  be written, but it is not a completed or comparable run.

`build_identity_id` is `sha256:` followed by the uppercase SHA-256 of the
UTF-8 canonical serialization of the object above.  The manifest and summary
each contain the complete object and its ID.  The first `run_start` telemetry
event contains `build_identity_id`; this anchors the event stream to the
same identity without repeating a large object every tick.

## Capture and ownership

1. Add a desktop build-identity generation step to CMake, shared by the
   engine rather than the benchmark harness.  At build time it captures
   `git rev-parse HEAD`, `HEAD^{tree}`, `git status --porcelain=v1 -z
   --untracked-files=all`, `git diff --binary HEAD --`, and contents of every
   untracked path.  The state digest must use the same unambiguous framed
   inputs already used by the matrix runner, so a dirty build is reproducible.
   The step generates a private C++ header/source compiled into
   `SurrealEngine`; it must fail the build if a configured source checkout
   cannot provide a complete identity.  It must not query the working tree at
   benchmark execution time.
2. Add a small `BuildIdentity` provider owned by common engine code.  It reads
   the generated source constants and hashes the actual process executable
   once, before `OpenTelemetry`.  Resolve the process image using the platform
   API, hash bytes in a streaming fashion, and cache the resulting immutable
   object for the process lifetime.  Do not take an executable path, hash, or
   source identity from a command-line option or environment variable.
3. Pass that immutable object to `ManifestJson`, `BotBenchmarkRunSummary`, and
   the initial telemetry event.  Compute it before creating `events.jsonl` so
   an identity failure cannot leave a normal-looking partial run.  Keep it out
   of `config_id`: configuration identity answers “same scenario and policy”,
   while build identity answers “same executable/source”.
4. Bump the benchmark manifest and summary together to v3 (the summary must
   advance from its current v3 to v4 if needed to retain the existing timing
   contract).  New quality/qualification validators require the new pair.
   Historical v1/v2 manifests remain readable only for historical analysis;
   they must not satisfy an observer, candidate, or release qualification
   gate.

The matrix runner should retain its independent `provenance.json` collection.
After a child finishes, it must additionally verify that the child
`build_identity.executable` exactly matches the preflight executable size and
SHA-256, and that the child source fields match the runner source capture when
the runner and engine are built from the same declared checkout.  A mismatch
fails the matrix row.  The runner record is a cross-check, not the source of
truth embedded in child evidence.

## Validator and comparison rules

- Extend `Analyze-BotQuality.py`, `Validate-RealizedBotCapabilities.py`, and
  specialised observer analyzers with one shared strict build-identity
  validator.  It must reject unknown members, wrong casing/length, extra
  fields, invalid decimal sizes, noncanonical IDs, a differing manifest versus
  summary object/ID, or a `run_start` identity mismatch.
- Extend `Compare-BotBenchmarkRuns.py` to accept the new schemas only when
  this cross-artifact check succeeds.  Exact-repeat comparison must require an
  identical `build_identity_id`; an identity mismatch is a failed comparison,
  not an ignorable environmental difference.  Candidate-versus-baseline
  quality reports should display both identities and reject a purported
  repeat where they differ.
- Do not add a permissive fallback for missing provenance.  Legacy run
  evidence can be reported as `historical-unqualified`, never upgraded by a
  surrounding matrix `provenance.json`.

## Focused test requirements

1. **Pure provider/serialization tests:** clean and dirty generated source
   records serialize canonically; malformed object IDs, an empty or lowercase
   SHA-256, zero/invalid size, a changed member, and a bad ID all fail.
2. **Executable hashing tests:** hash a fixed fixture binary and verify exact
   uppercase digest and decimal size.  Inject process-path, open, read, and
   hash failures and verify the run aborts before manifest/events creation.
3. **Protocol tests:** fixture a known build identity and assert that manifest,
   summary, and the first `run_start` carry exact matching identity/ID.  Check
   the manifest/summary schema progression preserves the existing roster and
   v3 timing fields.
4. **Analyzer negative tests:** mutate only the summary identity, only the
   `run_start` ID, an executable digest, or an unknown member; every general
   and observer validator must reject the run.  A v2 fixture must still be
   readable in historical mode but fail qualification mode.
5. **Matrix integration tests:** use the existing preflight file-provenance
   helper with a fixture executable.  Matching child identity passes; a
   different hash, size, source state, or absent v3 child identity marks the
   row failed even if gameplay telemetry is otherwise valid.
6. **Repeat comparison tests:** two byte-identical fixture runs with equal
   identities compare successfully; changing only `build_identity_id` makes
   comparison fail.  This prevents falsely calling results deterministic
   across different engine executables.

## Delivery order

Land the generated source identity and pure tests first, then the runtime
provider/protocol write path, then validators/comparer, then the matrix
cross-check and a no-UCC headless observer smoke run.  Only the final step may
update the qualification matrix to require the new evidence.
