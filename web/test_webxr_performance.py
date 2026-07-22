#!/usr/bin/env python3
"""Deterministic tests for the M11 WebXR performance report."""

from __future__ import annotations

import copy
import math
import tempfile
import unittest
from pathlib import Path

from profile_webxr import PROBE_PATH, parse_extra_query, write_outputs
from webxr_performance import (
	ProfileValidationError,
	REPORT_SCHEMA,
	REQUIRED_SAMPLE_COUNTERS,
	build_report,
	human_summary,
	interval_summary,
	percentile,
)


def fake_sample(elapsed: float, ticks: int, frames: int) -> dict:
	sample = {name: 0 for name in REQUIRED_SAMPLE_COUNTERS}
	sample.update({
		"elapsedMs": elapsed,
		"engineTicks": ticks,
		"xrFrames": frames,
		"webgpuErrors": 0,
		"drawCalls": 120,
		"textureCount": 42,
		"bindGroupsCreated": 8,
		"bindGroupCacheHits": 112,
		"bufferRollovers": 1,
		"weaponExpectedEyes": 2,
		"weaponEyePasses": 2,
		"weaponCalls": 1,
		"weaponVisualDrawScopes": ticks,
		"weaponVisualDrawRestores": ticks,
		"weaponVisualCalibratedOffsets": ticks,
		"hudExpectedEyes": 2,
		"hudStateUpdates": 1,
		"hudEyePresentations": 2,
		"hudCapturedCommands": 65,
		"hudPlayerPostRenderCalls": 1,
		"hudConsolePostRenderCalls": 1,
		"wasmHeapBytes": 384 * 1024 * 1024,
		"jsHeapUsedBytes": 50 * 1024 * 1024,
		"jsHeapTotalBytes": 64 * 1024 * 1024,
	})
	return sample


def fake_probe() -> dict:
	return {
		"schema": "surrealengine-webxr-performance-probe",
		"version": 1,
		"active": False,
		"xrRafPatched": True,
		"longTaskSupported": True,
		"windowRafTimestampsMs": [0.0, 10.0, 20.0, 35.0],
		"xrRafTimestampsMs": [0.0, 11.0, 22.5, 34.0],
		"eventLoopDelaysMs": [0.1, 0.5, 2.0],
		"longTasks": [{"startTimeMs": 100.0, "durationMs": 55.0}],
		"memory": {
			"wasmHeapBytes": 384 * 1024 * 1024,
			"jsHeapSupported": True,
			"jsHeapUsedBytes": 50 * 1024 * 1024,
			"jsHeapTotalBytes": 64 * 1024 * 1024,
			"jsHeapLimitBytes": 2048 * 1024 * 1024,
		},
	}


def fake_report_inputs() -> dict:
	return {
		"created_at": "2026-07-22T12:00:00+00:00",
		"metadata": {
			"map": "DM-Deck16][",
			"build": {"name": "build-emscripten"},
			"machine": {},
			"browser": {},
			"query": {"map": "DM-Deck16][", "build": "build-emscripten"},
		},
		"timing": {"warmupSeconds": 2.0, "sampleSeconds": 2.0},
		"samples": [fake_sample(1000.0, 100, 90), fake_sample(2000.0, 190, 180),
			fake_sample(3000.0, 280, 270)],
		"probe": fake_probe(),
		"lifecycle": {"sessionActive": True, "lifecycleOnly": True,
			"state": {"sessionsStarted": 1, "deviceLost": False, "shutdown": False}},
		"errors": {"engineCrash": None, "xrError": None, "pageErrors": [],
			"consoleWebGPUErrorLines": []},
	}


class PerformanceReportTests(unittest.TestCase):
	def test_percentile_uses_linear_interpolation(self):
		self.assertEqual(percentile([10.0, 20.0, 30.0], 50), 20.0)
		self.assertEqual(percentile([0.0, 10.0], 95), 9.5)

	def test_interval_budget_counts_are_deterministic(self):
		summary = interval_summary([0.0, 10.0, 22.0, 37.0], "fake-raf")
		self.assertEqual(summary["intervalCount"], 3)
		self.assertEqual(summary["missedFrameBudgets"]["72"]["count"], 1)
		self.assertEqual(summary["missedFrameBudgets"]["80"]["count"], 1)
		self.assertEqual(summary["missedFrameBudgets"]["90"]["count"], 2)
		self.assertFalse(summary["validForQuestQualification"])

	def test_report_is_versioned_and_never_claims_quest_qualification(self):
		report = build_report(**fake_report_inputs())
		self.assertEqual(report["schema"], REPORT_SCHEMA)
		self.assertEqual(report["version"], 1)
		self.assertEqual(report["result"], "pass")
		self.assertFalse(report["environment"]["headset"])
		self.assertFalse(report["environment"]["questQualified"])
		self.assertFalse(report["claims"]["gpuTimeMeasured"])
		self.assertFalse(report["claims"]["compositorTimingMeasured"])
		self.assertFalse(report["claims"]["thermalBehaviorMeasured"])

	def test_human_summary_has_prominent_scope_and_core_metrics(self):
		summary = human_summary(build_report(**fake_report_inputs()))
		self.assertIn("NOT A QUEST PERFORMANCE RESULT", summary)
		self.assertIn("IWER missed budgets", summary)
		self.assertIn("Not measured: GPU time", summary)

	def test_missing_and_nonfinite_diagnostics_fail_closed(self):
		for key, bad_value in (("hudCapturedCommands", None), ("drawCalls", math.nan)):
			with self.subTest(key=key):
				inputs = fake_report_inputs()
				inputs["samples"][1][key] = bad_value
				with self.assertRaisesRegex(ProfileValidationError, key):
					build_report(**inputs)

	def test_webgpu_error_fails_closed(self):
		inputs = fake_report_inputs()
		inputs["samples"][1]["webgpuErrors"] = 1
		with self.assertRaisesRegex(ProfileValidationError, "WebGPU reported"):
			build_report(**inputs)

	def test_console_webgpu_error_fails_closed(self):
		inputs = fake_report_inputs()
		inputs["errors"]["consoleWebGPUErrorLines"] = ["WebGPU validation error"]
		with self.assertRaisesRegex(ProfileValidationError, "browser console"):
			build_report(**inputs)

	def test_stalled_engine_or_iwer_frames_fail_closed(self):
		for key in ("engineTicks", "xrFrames"):
			with self.subTest(key=key):
				inputs = fake_report_inputs()
				for sample in inputs["samples"]:
					sample[key] = 5
				with self.assertRaises(ProfileValidationError):
					build_report(**inputs)

	def test_query_parser_rejects_native_or_malformed_overrides(self):
		self.assertEqual(parse_extra_query(["quality=high", "tag=a=b"]),
			[("quality", "high"), ("tag", "a=b")])
		for value in ("broken", "map=DM-Z", "build=other", "native-webgpu-xr=1"):
			with self.subTest(value=value), self.assertRaises(ProfileValidationError):
				parse_extra_query([value])

	def test_json_and_text_outputs_are_both_written(self):
		report = build_report(**fake_report_inputs())
		with tempfile.TemporaryDirectory() as directory:
			json_path, text_path = write_outputs(report, Path(directory) / "profile.json")
			self.assertTrue(json_path.is_file())
			self.assertTrue(text_path.is_file())
			self.assertIn(REPORT_SCHEMA, json_path.read_text(encoding="utf-8"))
			self.assertIn("NOT A QUEST", text_path.read_text(encoding="utf-8"))

	def test_browser_probe_contains_all_required_instrumentation_hooks(self):
		source = PROBE_PATH.read_text(encoding="utf-8")
		for marker in ("PerformanceObserver", "longtask", "eventLoopDelays",
				"XRSession", "requestAnimationFrame", "wasmHeapBytes", "performance.memory"):
			with self.subTest(marker=marker):
				self.assertIn(marker, source)


if __name__ == "__main__":
	unittest.main(verbosity=2)
