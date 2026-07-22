#!/usr/bin/env python3
"""Deterministic tests for the multi-map desktop/IWER performance matrix."""

from __future__ import annotations

import copy
import unittest

from profile_webxr_matrix import (
	DEFAULT_REPRESENTATIVE_MAPS,
	DEFAULT_STRESS_MAPS,
	parse_args,
)
from test_webxr_performance import fake_report_inputs
from webxr_performance import ProfileValidationError, build_report
from webxr_performance_matrix import (
	MapSpec,
	build_matrix,
	map_slug,
	matrix_summary,
	validate_map_specs,
)


SPECS = [
	MapSpec("DM-Deck16][", "representative"),
	MapSpec("CTF-Face", "representative"),
	MapSpec("DOM-Sesmar", "representative"),
	MapSpec("AS-Overlord", "representative"),
	MapSpec("DM-Morpheus", "representative"),
	MapSpec("CTF-Darji16", "stress"),
]


def report_for(spec: MapSpec, draw_calls: int = 120) -> dict:
	inputs = fake_report_inputs()
	inputs["metadata"]["map"] = spec.name
	inputs["metadata"]["git"] = {"commit": "abc", "trackedWorktreeDirty": False}
	inputs["metadata"]["browser"] = {"version": "150", "channel": "chrome", "headless": True}
	inputs["metadata"]["machine"] = {"operatingSystem": "test"}
	inputs["metadata"]["build"] = {"name": "build-emscripten", "assets": {"x": "hash"}}
	for sample in inputs["samples"]:
		sample["drawCalls"] = draw_calls
	return build_report(**inputs)


def profiles(specs: list[MapSpec]) -> list[tuple[dict, str, str]]:
	return [(report_for(spec, 100 + index), f"reports/{index}.json", f"hash-{index}")
		for index, spec in enumerate(specs)]


class PerformanceMatrixTests(unittest.TestCase):
	def test_cli_defaults_to_the_declared_full_acceptance_set(self):
		args = parse_args([])
		self.assertEqual(args.map, DEFAULT_REPRESENTATIVE_MAPS)
		self.assertEqual(args.stress_map, DEFAULT_STRESS_MAPS)
		self.assertEqual(len(args.specs), 6)

	def test_cli_rejects_partial_set_reserved_query_and_invalid_timing(self):
		for arguments in (
				["--map", "DM-Deck16][", "--stress-map", "CTF-Darji16"],
				["--query", "native-webgpu-xr=1"],
				["--sample-interval", "1", "--duration", "1"],
		):
			with self.subTest(arguments=arguments), self.assertRaises(SystemExit):
				parse_args(arguments)

	def test_acceptance_set_requires_five_representative_and_one_stress(self):
		validate_map_specs(SPECS)
		with self.assertRaisesRegex(ProfileValidationError, "five representative"):
			validate_map_specs(SPECS[:-2] + [SPECS[-1]])

	def test_map_names_are_local_allowlisted_and_unique(self):
		for name in ("../DM-Deck16][", "DM-Deck16][.unr", "DM-X?game=Y", "Entry"):
			with self.subTest(name=name), self.assertRaises(ProfileValidationError):
				validate_map_specs([MapSpec(name, "representative")], require_acceptance_set=False)
		with self.assertRaisesRegex(ProfileValidationError, "duplicate"):
			validate_map_specs([MapSpec("CTF-Face", "representative"),
				MapSpec("CTF-FACE", "stress")], require_acceptance_set=False)

	def test_slug_is_safe_and_stable(self):
		self.assertEqual(map_slug("DM-Deck16]["), "dm-deck16")
		self.assertEqual(map_slug("CTF-Darji16"), "ctf-darji16")

	def test_matrix_is_versioned_and_never_claims_quest_qualification(self):
		matrix = build_matrix("2026-07-22T00:00:00Z", SPECS, profiles(SPECS))
		self.assertEqual(matrix["schema"], "surrealengine-webxr-performance-matrix")
		self.assertEqual(matrix["version"], 1)
		self.assertEqual(matrix["result"], "pass")
		self.assertFalse(matrix["environment"]["headset"])
		self.assertFalse(matrix["environment"]["questQualified"])
		self.assertFalse(matrix["claims"]["questPerformanceQualified"])
		self.assertEqual(matrix["coverage"], {
			"representativeMaps": 5, "stressMaps": 1, "totalMaps": 6})

	def test_summary_extracts_comparable_metrics_and_profile_hash(self):
		matrix = build_matrix("now", SPECS, profiles(SPECS))
		first = matrix["maps"][0]
		self.assertEqual(first["map"], "DM-Deck16][")
		self.assertEqual(first["role"], "representative")
		self.assertEqual(first["drawCallsMean"], 100.0)
		self.assertEqual(first["profileSha256"], "hash-0")
		text = matrix_summary(matrix)
		self.assertIn("NOT A QUEST PERFORMANCE RESULT", text)
		self.assertIn("5 representative + 1 stress", text)

	def test_mismatched_map_or_failed_child_is_rejected(self):
		items = profiles(SPECS)
		bad_map = copy.deepcopy(items[0][0])
		bad_map["metadata"]["map"] = "DM-Other"
		with self.assertRaisesRegex(ProfileValidationError, "map mismatch"):
			build_matrix("now", SPECS, [(bad_map, *items[0][1:]), *items[1:]])
		failed = copy.deepcopy(items[0][0])
		failed["result"] = "fail"
		with self.assertRaisesRegex(ProfileValidationError, "did not pass"):
			build_matrix("now", SPECS, [(failed, *items[0][1:]), *items[1:]])

	def test_inconsistent_build_browser_or_machine_identity_is_rejected(self):
		for path, value in (("build", {"name": "other"}),
				("browser", {"version": "different", "channel": "chrome", "headless": True}),
				("machine", {"operatingSystem": "different"})):
			with self.subTest(path=path):
				items = profiles(SPECS)
				items[1][0]["metadata"][path] = value
				with self.assertRaisesRegex(ProfileValidationError, "comparable"):
					build_matrix("now", SPECS, items)

	def test_missing_or_nonfinite_summary_metric_is_rejected(self):
		for value in (None, float("nan")):
			with self.subTest(value=value):
				items = profiles(SPECS)
				items[0][0]["metrics"]["engine"]["ticksPerSecond"] = value
				with self.assertRaisesRegex(ProfileValidationError, "ticksPerSecond"):
					build_matrix("now", SPECS, items)

	def test_profile_count_must_match_map_count(self):
		with self.assertRaisesRegex(ProfileValidationError, "count mismatch"):
			build_matrix("now", SPECS, profiles(SPECS)[:-1])


if __name__ == "__main__":
	unittest.main(verbosity=2)
