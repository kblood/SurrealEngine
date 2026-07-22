"""Pure validation and reporting helpers for the WebXR performance harness."""

from __future__ import annotations

import math
import statistics
from typing import Any, Iterable


REPORT_SCHEMA = "surrealengine-webxr-performance-report"
REPORT_VERSION = 1
PROBE_SCHEMA = "surrealengine-webxr-performance-probe"
PROBE_VERSION = 1
REFRESH_RATES_HZ = (72, 80, 90)

REQUIRED_SAMPLE_COUNTERS = (
	"elapsedMs",
	"engineTicks",
	"xrFrames",
	"webgpuErrors",
	"drawCalls",
	"textureCount",
	"bindGroupsCreated",
	"bindGroupCacheHits",
	"bufferRollovers",
	"weaponExpectedEyes",
	"weaponEyePasses",
	"weaponCalls",
	"weaponVisualDrawScopes",
	"weaponVisualDrawRestores",
	"weaponVisualZeroFallbacks",
	"weaponVisualCalibratedOffsets",
	"weaponVisualRejectedTransforms",
	"hudExpectedEyes",
	"hudStateUpdates",
	"hudEyePresentations",
	"hudCapturedCommands",
	"hudUnsupportedDraws",
	"hudClampedViewports",
	"hudPlayerPostRenderCalls",
	"hudConsolePostRenderCalls",
	"wasmHeapBytes",
)

WEBGPU_LAST_FRAME_COUNTERS = (
	"drawCalls",
	"bindGroupsCreated",
	"bindGroupCacheHits",
	"bufferRollovers",
)

PRESENTATION_LAST_FRAME_COUNTERS = (
	"weaponExpectedEyes",
	"weaponEyePasses",
	"weaponCalls",
	"hudExpectedEyes",
	"hudStateUpdates",
	"hudEyePresentations",
	"hudCapturedCommands",
	"hudUnsupportedDraws",
	"hudClampedViewports",
	"hudPlayerPostRenderCalls",
	"hudConsolePostRenderCalls",
)

WEAPON_CUMULATIVE_COUNTERS = (
	"weaponVisualDrawScopes",
	"weaponVisualDrawRestores",
	"weaponVisualZeroFallbacks",
	"weaponVisualCalibratedOffsets",
	"weaponVisualRejectedTransforms",
)


class ProfileValidationError(ValueError):
	"""Raised when a run cannot produce a trustworthy profile report."""


def _finite(value: Any, label: str) -> float:
	if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
		raise ProfileValidationError(f"{label} is missing or nonfinite: {value!r}")
	return float(value)


def percentile(values: Iterable[float], percent: float) -> float:
	"""Return a linearly interpolated percentile for finite input values."""
	ordered = sorted(_finite(value, "percentile sample") for value in values)
	if not ordered:
		raise ProfileValidationError("cannot calculate a percentile from no samples")
	if not 0 <= percent <= 100:
		raise ProfileValidationError(f"percentile must be in [0, 100], got {percent}")
	position = (len(ordered) - 1) * percent / 100.0
	lower = math.floor(position)
	upper = math.ceil(position)
	if lower == upper:
		return ordered[lower]
	weight = position - lower
	return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def interval_summary(timestamps_ms: Iterable[float], source: str) -> dict[str, Any]:
	timestamps = [_finite(value, f"{source} timestamp") for value in timestamps_ms]
	if len(timestamps) < 2:
		raise ProfileValidationError(f"{source} needs at least two timestamps")
	intervals = [right - left for left, right in zip(timestamps, timestamps[1:])]
	if any(interval <= 0 for interval in intervals):
		raise ProfileValidationError(f"{source} timestamps are not strictly increasing")
	missed = {}
	for refresh_rate in REFRESH_RATES_HZ:
		budget = 1000.0 / refresh_rate
		count = sum(interval > budget for interval in intervals)
		missed[str(refresh_rate)] = {
			"budgetMs": budget,
			"count": count,
			"percent": 100.0 * count / len(intervals),
		}
	return {
		"source": source,
		"intervalCount": len(intervals),
		"meanMs": statistics.fmean(intervals),
		"p50Ms": percentile(intervals, 50),
		"p90Ms": percentile(intervals, 90),
		"p95Ms": percentile(intervals, 95),
		"p99Ms": percentile(intervals, 99),
		"maxMs": max(intervals),
		"missedFrameBudgets": missed,
		"validForDesktopBrowserScheduling": True,
		"validForQuestQualification": False,
	}


def value_summary(values: Iterable[float], label: str) -> dict[str, float]:
	numbers = [_finite(value, label) for value in values]
	if not numbers:
		raise ProfileValidationError(f"{label} has no samples")
	return {
		"minimum": min(numbers),
		"maximum": max(numbers),
		"mean": statistics.fmean(numbers),
		"last": numbers[-1],
	}


def _validate_samples(samples: list[dict[str, Any]]) -> None:
	if len(samples) < 2:
		raise ProfileValidationError("at least two runtime samples are required")
	for index, sample in enumerate(samples):
		for counter in REQUIRED_SAMPLE_COUNTERS:
			_finite(sample.get(counter), f"samples[{index}].{counter}")
		if sample["webgpuErrors"] != 0:
			raise ProfileValidationError(
				f"WebGPU reported {sample['webgpuErrors']} uncaptured error(s) at sample {index}")
	if samples[-1]["engineTicks"] <= samples[0]["engineTicks"]:
		raise ProfileValidationError("engine tick counter did not advance during sampling")
	if samples[-1]["xrFrames"] <= samples[0]["xrFrames"]:
		raise ProfileValidationError("IWER XR frame counter did not advance during sampling")
	if max(sample["drawCalls"] for sample in samples) <= 0:
		raise ProfileValidationError("WebGPU draw-call diagnostic never became positive")
	if max(sample["textureCount"] for sample in samples) <= 0:
		raise ProfileValidationError("WebGPU texture-count diagnostic never became positive")


def _validate_probe(probe: dict[str, Any]) -> None:
	if probe.get("schema") != PROBE_SCHEMA or probe.get("version") != PROBE_VERSION:
		raise ProfileValidationError("browser performance probe schema/version mismatch")
	if probe.get("xrRafPatched") is not True:
		raise ProfileValidationError("XRSession.requestAnimationFrame was not instrumented")
	interval_summary(probe.get("windowRafTimestampsMs", []), "desktop-window-raf")
	interval_summary(probe.get("xrRafTimestampsMs", []), "iwer-xr-session-raf")
	event_delays = probe.get("eventLoopDelaysMs", [])
	if not event_delays:
		raise ProfileValidationError("event-loop delay probe produced no samples")
	for value in event_delays:
		_finite(value, "event-loop delay")
	for index, task in enumerate(probe.get("longTasks", [])):
		_finite(task.get("startTimeMs"), f"longTasks[{index}].startTimeMs")
		_finite(task.get("durationMs"), f"longTasks[{index}].durationMs")
	memory = probe.get("memory") or {}
	_finite(memory.get("wasmHeapBytes"), "probe.memory.wasmHeapBytes")
	if memory.get("jsHeapSupported"):
		for name in ("jsHeapUsedBytes", "jsHeapTotalBytes", "jsHeapLimitBytes"):
			_finite(memory.get(name), f"probe.memory.{name}")


def build_report(
	*,
	created_at: str,
	metadata: dict[str, Any],
	timing: dict[str, Any],
	samples: list[dict[str, Any]],
	probe: dict[str, Any],
	lifecycle: dict[str, Any],
	errors: dict[str, Any],
) -> dict[str, Any]:
	"""Validate raw observations and construct the stable v1 JSON report."""
	_validate_samples(samples)
	_validate_probe(probe)
	warmup_seconds = _finite(timing.get("warmupSeconds"), "timing.warmupSeconds")
	sample_seconds = _finite(timing.get("sampleSeconds"), "timing.sampleSeconds")
	if warmup_seconds < 0 or sample_seconds <= 0:
		raise ProfileValidationError("warmup must be nonnegative and sample duration positive")

	page_errors = errors.get("pageErrors")
	if not isinstance(page_errors, list):
		raise ProfileValidationError("errors.pageErrors is missing")
	if page_errors:
		raise ProfileValidationError(f"page emitted {len(page_errors)} unhandled error(s)")
	for name in ("engineCrash", "xrError"):
		if errors.get(name) not in (None, ""):
			raise ProfileValidationError(f"{name} was set: {errors[name]}")
	if lifecycle.get("sessionActive") is not True:
		raise ProfileValidationError("IWER immersive session was not active at capture end")
	if lifecycle.get("lifecycleOnly") is not True:
		raise ProfileValidationError("profile was not collected in explicit IWER lifecycle-only mode")
	state = lifecycle.get("state") or {}
	if state.get("deviceLost") is True or state.get("shutdown") is True:
		raise ProfileValidationError("WebXR lifecycle reported device loss or shutdown during capture")
	if _finite(state.get("sessionsStarted"), "lifecycle.state.sessionsStarted") < 1:
		raise ProfileValidationError("WebXR lifecycle did not record a started IWER session")
	console_gpu_errors = errors.get("consoleWebGPUErrorLines", [])
	if not isinstance(console_gpu_errors, list):
		raise ProfileValidationError("errors.consoleWebGPUErrorLines is invalid")
	if console_gpu_errors:
		raise ProfileValidationError(
			f"browser console reported {len(console_gpu_errors)} WebGPU error line(s)")

	first = samples[0]
	last = samples[-1]
	measured_seconds = (last["elapsedMs"] - first["elapsedMs"]) / 1000.0
	if measured_seconds <= 0:
		raise ProfileValidationError("sample elapsed time did not advance")

	long_tasks = probe.get("longTasks", [])
	long_task_durations = [task["durationMs"] for task in long_tasks]
	js_heap = None
	if probe["memory"].get("jsHeapSupported"):
		for index, sample in enumerate(samples):
			_finite(sample.get("jsHeapUsedBytes"), f"samples[{index}].jsHeapUsedBytes")
			_finite(sample.get("jsHeapTotalBytes"), f"samples[{index}].jsHeapTotalBytes")
		js_heap = {
			"usedBytes": value_summary(
				(sample["jsHeapUsedBytes"] for sample in samples), "JS heap used bytes"),
			"totalBytes": value_summary(
				(sample["jsHeapTotalBytes"] for sample in samples), "JS heap total bytes"),
			"limitBytes": probe["memory"]["jsHeapLimitBytes"],
		}
	metrics = {
		"engine": {
			"tickStart": first["engineTicks"],
			"tickEnd": last["engineTicks"],
			"tickDelta": last["engineTicks"] - first["engineTicks"],
			"ticksPerSecond": (last["engineTicks"] - first["engineTicks"]) / measured_seconds,
			"iwerXRFrameStart": first["xrFrames"],
			"iwerXRFrameEnd": last["xrFrames"],
			"iwerXRFrameDelta": last["xrFrames"] - first["xrFrames"],
			"iwerXRFramesPerSecond": (last["xrFrames"] - first["xrFrames"]) / measured_seconds,
		},
		"frameIntervals": {
			"desktopWindowRAF": interval_summary(
				probe["windowRafTimestampsMs"], "desktop-window-raf"),
			"iwerXRSessionRAF": interval_summary(
				probe["xrRafTimestampsMs"], "iwer-xr-session-raf"),
		},
		"eventLoopDelayMs": value_summary(probe["eventLoopDelaysMs"], "event-loop delay"),
		"longTasks": {
			"supported": probe.get("longTaskSupported") is True,
			"count": len(long_tasks),
			"totalDurationMs": sum(long_task_durations),
			"maximumDurationMs": max(long_task_durations) if long_task_durations else 0.0,
		},
		"memory": {
			"wasmHeapBytes": value_summary(
				(sample["wasmHeapBytes"] for sample in samples), "Wasm heap bytes"),
			"browserFinal": probe["memory"],
			"jsHeap": js_heap,
		},
		"webgpu": {
			"uncapturedErrorsMaximum": max(sample["webgpuErrors"] for sample in samples),
			"textureCount": value_summary(
				(sample["textureCount"] for sample in samples), "textureCount"),
			"lastFrameCounters": {
				name: value_summary((sample[name] for sample in samples), name)
				for name in WEBGPU_LAST_FRAME_COUNTERS
			},
		},
		"presentation": {
			"lastFrameCounters": {
				name: value_summary((sample[name] for sample in samples), name)
				for name in PRESENTATION_LAST_FRAME_COUNTERS
			},
			"weaponCumulativeCounters": {
				name: value_summary((sample[name] for sample in samples), name)
				for name in WEAPON_CUMULATIVE_COUNTERS
			},
		},
	}

	return {
		"schema": REPORT_SCHEMA,
		"version": REPORT_VERSION,
		"createdAt": created_at,
		"result": "pass",
		"environment": {
			"kind": "desktop-chrome-iwer",
			"headset": False,
			"questQualified": False,
			"presentationMode": "lifecycle-only",
			"label": "DESKTOP CHROME + IWER; NOT A QUEST PERFORMANCE RESULT",
		},
		"claims": {
			"desktopBrowserSchedulingMeasured": True,
			"gpuTimeMeasured": False,
			"compositorTimingMeasured": False,
			"thermalBehaviorMeasured": False,
			"motionToPhotonMeasured": False,
		},
		"metadata": metadata,
		"timing": {
			"warmupSeconds": warmup_seconds,
			"requestedSampleSeconds": sample_seconds,
			"observedSampleSeconds": measured_seconds,
			"sampleCount": len(samples),
		},
		"metrics": metrics,
		"lifecycle": lifecycle,
		"errors": errors,
		"raw": {
			"samples": samples,
			"probe": probe,
		},
		"limitations": [
			"IWER emulates WebXR lifecycle and frame scheduling; it is not a headset compositor.",
			"WebGPU counters describe the desktop companion game renderer, not Quest GPU execution.",
			"No GPU timestamps, thermal state, reprojection, motion-to-photon latency, or headset power data are measured.",
		],
	}


def human_summary(report: dict[str, Any]) -> str:
	metrics = report["metrics"]
	xr = metrics["frameIntervals"]["iwerXRSessionRAF"]
	window = metrics["frameIntervals"]["desktopWindowRAF"]
	webgpu = metrics["webgpu"]
	memory = metrics["memory"]["wasmHeapBytes"]
	lines = [
		"SurrealEngine WebXR performance profile v1",
		"DESKTOP CHROME + IWER; NOT A QUEST PERFORMANCE RESULT",
		f"Result: {report['result'].upper()}",
		f"Map/build: {report['metadata']['map']} / {report['metadata']['build']['name']}",
		f"Warmup/sample: {report['timing']['warmupSeconds']:.2f}s / "
		f"{report['timing']['observedSampleSeconds']:.2f}s ({report['timing']['sampleCount']} samples)",
		f"Engine: {metrics['engine']['ticksPerSecond']:.2f} ticks/s; "
		f"IWER XR: {metrics['engine']['iwerXRFramesPerSecond']:.2f} frames/s",
		f"Desktop RAF interval: p50 {window['p50Ms']:.3f} ms, "
		f"p95 {window['p95Ms']:.3f} ms, p99 {window['p99Ms']:.3f} ms",
		f"IWER XR RAF interval: p50 {xr['p50Ms']:.3f} ms, "
		f"p95 {xr['p95Ms']:.3f} ms, p99 {xr['p99Ms']:.3f} ms",
		"IWER missed budgets: " + ", ".join(
			f"{hz} Hz={xr['missedFrameBudgets'][str(hz)]['count']}"
			for hz in REFRESH_RATES_HZ),
		f"WebGPU last-frame draws: mean "
		f"{webgpu['lastFrameCounters']['drawCalls']['mean']:.1f}, "
		f"max {webgpu['lastFrameCounters']['drawCalls']['maximum']:.0f}; errors 0",
		f"Wasm heap: {memory['last'] / (1024 * 1024):.1f} MiB "
		f"(max {memory['maximum'] / (1024 * 1024):.1f} MiB)",
		f"Long tasks: {metrics['longTasks']['count']}; event-loop delay p95 "
		f"{percentile(report['raw']['probe']['eventLoopDelaysMs'], 95):.3f} ms",
		"Not measured: GPU time, headset compositor timing, thermal behavior, motion-to-photon, or Quest power.",
	]
	return "\n".join(lines) + "\n"
