# WebXR desktop/IWER performance harness

## Purpose and qualification boundary

`web/profile_webxr.py` is the repeatable M11 development profiler for a
data-backed SurrealEngine WebGPU build running in desktop Chrome while IWER
provides an emulated Meta Quest 3 WebXR lifecycle. It produces a versioned JSON
report and a short text summary from the same validated observations.

Every output is labeled:

```text
DESKTOP CHROME + IWER; NOT A QUEST PERFORMANCE RESULT
```

This harness measures desktop browser scheduling and engine/render diagnostics.
It does **not** measure Quest GPU time, headset compositor timing, thermal
behavior, power, reprojection, motion-to-photon latency, or comfort. IWER's
reported user agent is emulated; the report keeps Playwright's actual Chrome
version separately and marks the page user agent as IWER-altered.

## Prerequisites

- Build `SurrealEngine.js` and `SurrealEngine.wasm`. The default
  `build-emscripten` also contains the developer-only UT99 preload and is the
  easiest data-backed profiling target. Never distribute that preload.
- Run `npm install` in `web/` so `iwer@2.3.0` is present.
- Install Playwright for Python and a Chrome channel visible to Playwright.
- Start `web/serve.mjs`, which supplies the required isolation and MIME headers:

```powershell
node web/serve.mjs --port 8091
```

Use an otherwise idle machine and record whether the browser was headless or
headed. Do not compare runs from different build hashes, maps, Chrome versions,
machine configurations, warmups, or durations as if they were equivalent.

## Running a profile

The primary command accepts explicit map, build, output, warmup, and duration:

```powershell
python -B -u web/profile_webxr.py `
  --map "DM-Deck16][" `
  --build build-emscripten `
  --warmup 10 `
  --duration 60 `
  --sample-interval 0.25 `
  --output C:\profiles\deck16-webxr-iwer.json
```

The command also writes `C:\profiles\deck16-webxr-iwer.txt`. Omit `--output`
to use a UTC-stamped path below `web/profiles/`. `--headed` shows Chrome;
otherwise it runs headless. Change the server with `--base-url`, the installed
Chromium channel with `--browser-channel`, and append non-reserved launcher
parameters with repeatable `--query KEY=VALUE` arguments. `map`, `build`, and
`native-webgpu-xr` cannot be overridden through `--query`.

This is intentionally a lifecycle-only IWER run. It does not request the native
WebGPU XR presentation path, because IWER is not a WebGPU headset compositor.
Use the physical Quest matrix for production-presentation qualification.

## What is captured

The JSON schema is `surrealengine-webxr-performance-report`, version 1. It
contains:

- UTC creation time and pass/fail result;
- OS, architecture, processor string, logical CPU count, and Python version;
- actual Playwright Chrome version/channel/headless state plus IWER-altered page
  user agent, browser CPU/memory hints, secure-context/isolation, WebGPU, and
  WebXR availability;
- Git commit and tracked-worktree dirty state;
- build-relative path, presence of the developer preload, byte size, and SHA-256
  for each JS/Wasm/data build artifact;
- exact map, launcher URL, decoded query, and importer/data-boot result;
- requested warmup, requested and observed sample duration, and sample count;
- engine tick and IWER XR-frame start/end/delta/rate;
- desktop window RAF and IWER XR-session RAF interval count, mean, p50, p90,
  p95, p99, maximum, and missed 72/80/90 Hz budget counts;
- PerformanceObserver long-task count/duration when Chrome exposes `longtask`,
  and periodic event-loop delay samples;
- Wasm heap size captured from the actual `WebAssembly.Memory` plus Chrome JS
  heap used/total/limit when `performance.memory` is available;
- WebGPU uncaptured errors, last-frame draw calls, bind groups created, bind-group
  cache hits, geometry-buffer rollovers, and resident texture count;
- last-frame HUD and weapon eye/call/capture/error counters, plus cumulative
  controller-relative weapon-transform counters;
- launch readiness, lifecycle state, native diagnostics, XR log, page errors,
  crash/XR errors, and browser-console WebGPU error lines;
- raw native-counter samples and raw probe observations for later analysis.

WebGPU, HUD, and weapon values have different semantics. Draw/bind/rollover and
most HUD/weapon overlay values describe the most recently completed engine
frame. Texture count is resident state. Weapon-transform scope counters are
cumulative. In lifecycle-only mode, stereo HUD and weapon counters may remain
zero because IWER is not presenting engine WebGPU eye textures; retaining those
zeroes makes that limitation visible rather than pretending the stereo path ran.

## Frame-budget interpretation

An interval is counted as missed when it is strictly greater than `1000 / Hz`.
The two timing sources are reported separately:

- `desktop-window-raf` is the companion browser window RAF cadence.
- `iwer-xr-session-raf` is IWER's emulated XR-session callback cadence.

The calculations are valid descriptions of those desktop timestamps only. A
60 Hz desktop/IWER run will normally miss every 72, 80, and 90 Hz budget. That
does not establish that the game misses those budgets on Quest, and a fast
desktop result does not establish that Quest meets them.

## Fail-closed rules

The command exits nonzero and writes a small versioned failure JSON/text pair if
collection or validation fails. A nominal report is rejected when:

- any required renderer, engine, frame, HUD, weapon, memory, or timing value is
  missing or nonfinite;
- fewer than two counter samples or RAF timestamps exist, timestamps do not
  increase, or the event-loop probe has no samples;
- the engine tick or IWER frame counter stalls;
- no WebGPU draw or texture activity is observed;
- any sampled uncaptured WebGPU error is nonzero, or the browser console contains
  a WebGPU error/validation line;
- the engine crash flag, XR error, unhandled page error, lifecycle device-loss,
  or lifecycle shutdown state is present;
- IWER session entry, RAF instrumentation, data-backed engine boot, trusted audio
  resume, or the explicit lifecycle-only readiness contract fails.

The generated report is evidence, not a benchmark verdict. Archive it with the
corresponding build and compare only like-for-like runs.

## Harness validation

Run the deterministic report tests and JavaScript syntax check before relying on
a profile:

```powershell
python -B web/test_webxr_performance.py
node --check web/webxr_performance_probe.js
```

The tests use fake counter/timestamp samples and cover percentile interpolation,
72/80/90 Hz budget counts, schema/scope labels, JSON/text emission, reserved
query handling, stalled counters, missing/nonfinite diagnostics, and WebGPU
counter/console failures.

On 2026-07-22, a short integration run against `DM-Deck16][` and the data-backed
`build-emscripten` completed with 1 second warmup and 3 seconds sampling: 13
counter samples, 59.89 engine ticks/s, 59.89 IWER XR frames/s, 95 WebGPU draws
per sampled frame, 256 MiB Wasm heap, no long tasks, and zero WebGPU errors. Its
~60 Hz IWER scheduling missed the higher 72/80/90 Hz budgets as expected. This
is a harness validation result only, not a Quest performance baseline.

## Multi-map acceptance-set automation

`web/profile_webxr_matrix.py` runs the same fail-closed child profiler across a
declared acceptance set and writes a small schema-v1 aggregate plus the complete
per-map JSON/text reports. Its default local GOTY set is:

| Role | Map | Coverage intent |
|---|---|---|
| representative | `DM-Deck16][` | established indoor baseline |
| representative | `CTF-Face` | outdoor/sky and long sight lines |
| representative | `DOM-Sesmar` | domination zones and indoor complexity |
| representative | `AS-Overlord` | scripted Assault path |
| representative | `DM-Morpheus` | zero-gravity and sky presentation |
| stress candidate | `CTF-Darji16` | largest `.unr` in the qualified local GOTY install |

The stress label is a declared coverage role, not proof that this is the most
expensive map on Quest. Replace or extend it only from measured physical data.
At least five unique representative maps and one stress map are mandatory;
paths, extensions, URL options, unsupported prefixes, and case-fold duplicates
are rejected.

Run the default set with comparable settings:

```powershell
python -B -u web/profile_webxr_matrix.py `
  --build build-emscripten `
  --warmup 10 `
  --duration 60 `
  --sample-interval 0.25 `
  --output C:\profiles\ut99-webxr-matrix.json
```

Use repeatable `--map` and `--stress-map` arguments to supply a different full
set. The runner refuses existing aggregate, summary, or report-directory
targets. Each child receives explicit map/build/timing/browser/query options.
One failed child makes the aggregate fail while retaining its child evidence.
Successful aggregation also requires byte-identical build metadata and the
same Git, browser, headless, and machine identity across every child. Report
paths and SHA-256 hashes bind each compact summary row to its full evidence.

The aggregate schema is `surrealengine-webxr-performance-matrix` version 1. It
extracts engine/IWER rates, draw-call mean/maximum, resident-texture maximum,
Wasm-heap maximum, XR RAF p95, long-task count, and WebGPU error maximum per
map. It repeats the desktop/IWER limitation in the environment, claims, text
summary, and limitations. It cannot satisfy the physical 72 Hz, GPU, thermal,
power, compositor, reprojection, motion-to-photon, or comfort gates.

Deterministic coverage:

```powershell
python -B -u web/test_webxr_performance_matrix.py
```

The tests cover acceptance-set cardinality, local-name validation, duplicate
rejection, stable output slugs, schema/claim boundaries, metric extraction,
report hashes, map/result mismatch, nonfinite metrics, child-count mismatch,
and cross-profile build/browser/machine comparability.
