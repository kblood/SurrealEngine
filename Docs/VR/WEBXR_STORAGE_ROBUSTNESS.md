# WebXR browser-storage robustness harness

## Purpose

`web/qualify_storage_robustness.py` qualifies the recovery properties that can
be reproduced safely on a desktop browser. It drives the real
`SurrealMutableData` OPFS and IndexedDB implementations and the real
`SurrealUT99Importer` OPFS implementation. The browser fixture stores only
explicit synthetic markers in run-unique test namespaces.

This harness does **not** import, inspect, copy, or clear a user's UT99 files.
It launches a new disposable browser profile for every run and never opens the
production `surrealengine-mutable-data-v1` or `surrealengine-ut99-data-v1`
namespace. Whole-origin clearing is performed only in that newly created
profile.

The emitted evidence schema is
`surrealengine-storage-robustness-report`, version 1. Each result names its
scope and limitation so an automated desktop result cannot be mistaken for a
Quest hardware qualification.

## Running it

Start the repository's ordinary local HTTP server, then run the deterministic
contract checks and the browser qualification:

```powershell
python -B -m unittest web/test_storage_robustness.py
python -B -u web/qualify_storage_robustness.py `
  --base-url=http://localhost:8091 `
  --output="$env:TEMP\surreal-storage-robustness-report.json"
```

The driver automatically chooses Chrome, Chromium, Edge, or Brave. To qualify
the user's current Brave build explicitly:

```powershell
python -B -u web/qualify_storage_robustness.py `
  --base-url=http://localhost:8091 `
  --browser-executable="C:\Program Files\BraveSoftware\Brave-Browser\Application\brave.exe" `
  --output="$env:TEMP\surreal-storage-robustness-brave.json"
```

The driver owns the browser it launches. During the abrupt-termination case it
forcibly terminates that exact process tree. Do not point the tool at an
already-running browser or an existing browser profile; the tool has no option
to do either.

## Test sequence and interpretation

| Case | Mechanism | What a pass proves | What it does not prove |
|---|---|---|---|
| Clean browser restart | Chromium `Browser.close`, process exit, reopen the same disposable profile | Complete OPFS, IndexedDB, and synthetic importer generations survive an orderly full-browser restart | Quest Browser restart, device reboot, or an abrupt exit |
| Mutable/importer separation | Clear both run-unique mutable stores and reload the run-unique importer store | The mutable clear path does not clear the separate importer namespace | Protection from whole-origin site-data clearing or eviction |
| Renderer crash | Navigate the fixture renderer to `chrome://crash`, keep the browser alive, open a new renderer | Committed OPFS, IndexedDB, and importer generations survive a renderer-process crash | Browser-process kill or OS/device kill |
| Forced browser-process-tree kill | Start mutable and importer replacement saves, reach known partial OPFS snapshot/dataset cut points, start an IndexedDB save, then force-kill the owned browser tree | Both OPFS `current.json` pointers retain their old committed generations; IndexedDB exposes one complete old-or-new transaction | Quest OS kill, battery loss, filesystem/controller failure, or every possible transaction cut point |
| Corruption and schema policy | Inject future version metadata and missing committed payloads into mutable OPFS, mutable IndexedDB, and importer OPFS | Initialization fails closed, native main does not launch, mutable automatic checkpoints pause, explicit mutable flush returns `CLEAR_REQUIRED`, future metadata is not overwritten, and the synthetic immutable MEMFS sentinel is unchanged | Migration from a future format; the current implementation has no migration path |
| Whole-origin eviction simulation | CDP `Storage.clearDataForOrigin(all)` after closing live handles | A deterministic site-data clear removes both mutable backends and importer data, and the next load reports no dataset | Browser storage-pressure heuristics, selective eviction ordering, or Quest behavior |

Both OPFS abrupt cut points are deterministic. In each production save loop,
the second entry's blob provider is not called until the first entry has reached
the new snapshot/dataset. That provider then intentionally never resolves.
Consequently neither new `current.json` pointer can have committed before
termination.

The IndexedDB cut point is deliberately reported as indeterminate. Its
production implementation collects blobs and then writes files plus metadata
in one transaction. A valid outcome after termination is either the old complete
generation or the new complete generation. Missing data or a mixture of tags is
a failure.

## Evidence from 2026-07-22

A localhost run on Windows 11 with headless Chrome 150.0.7871.181 passed all six
browser cases. The forced process-tree termination occurred after a partial
mutable OPFS snapshot, a partial importer OPFS dataset, and an initiated
IndexedDB replacement. On restart, both OPFS pointers retained their old
generation and IndexedDB exposed the complete new generation. All six
corruption scenarios (future and missing-data states in mutable OPFS, mutable
IndexedDB, and importer OPFS) failed closed. Explicit whole-origin clearing
removed every test dataset.

The report is intentionally written outside the repository and contains the
exact browser product/revision, host, run id, dataset ids, termination method,
observed generation, fixture hashes, case scopes, and remaining limitations.
Generated reports are evidence artifacts, not redistributable runtime assets.

The same six cases also passed in the installed Brave executable. Its DevTools
protocol identified the Chromium product as `Chrome/150.0.7871.128`; this is the
engine version reported by the browser, not Brave's marketing/product version.
The final Brave run again retained both old OPFS generations, exposed one
complete new IndexedDB generation, failed closed for all six corrupt-store
scenarios, and removed all synthetic data during explicit origin clearing. This
is useful Windows Brave evidence, but it does not qualify VDXR, Quest Browser,
or a headset OS interruption because the run was headless on the PC.

## Remaining M10 storage gates

The following still require other implementation or physical qualification:

- define and implement an actual schema migration before a migration can pass;
- run the same evidence workflow against Brave and Quest Browser versions in
  the release compatibility matrix;
- perform a real Quest Browser close/reopen, headset reboot, and OS/browser
  process reclamation test;
- exercise browser-driven storage-pressure eviction rather than explicit site
  data clearing;
- import a complete user-owned installation in a clean profile and qualify
  full-size copy time, quota behavior, persistence grant behavior, and recovery;
- validate long-play checkpoints, custom save-path policy, storage exhaustion,
  and device thermal behavior.

No desktop automation result should close those hardware and full-install
gates. Attach the versioned JSON from each physical/manual run and record the
browser build, headset software, storage free space, persistence-grant result,
and exact interruption procedure.
