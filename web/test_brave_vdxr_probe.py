#!/usr/bin/env python3
"""Deterministic tests for the physical Brave/VDXR evidence collector."""

from __future__ import annotations

import argparse
import unittest

import run_brave_vdxr_probe as probe


class BraveVDXRProbeTests(unittest.TestCase):
	def test_accepts_loopback_http_and_https(self) -> None:
		self.assertEqual(probe.validate_base_url("http://localhost:8091/"), "http://localhost:8091")
		self.assertEqual(probe.validate_base_url("http://127.0.0.1:8091"), "http://127.0.0.1:8091")
		self.assertEqual(probe.validate_base_url("https://quest.example"), "https://quest.example")

	def test_rejects_insecure_lan_and_malformed_urls(self) -> None:
		for value in ("http://192.168.50.8:8091", "file:///tmp/game", "localhost:8091"):
			with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
				probe.validate_base_url(value)

	def test_build_url_forces_native_route_and_cache_marker(self) -> None:
		url = probe.build_url("http://localhost:8091", "build-emscripten", "DM-Deck16][")
		self.assertIn("native-webgpu-xr=1", url)
		self.assertIn("orientation-fix=1", url)
		self.assertIn("build=build-emscripten", url)
		self.assertIn("map=DM-Deck16%5D%5B", url)

	def test_success_requires_running_rendered_frame_budget(self) -> None:
		good = {"phase": "running", "frameCount": 10, "lastRenderSucceeded": True}
		self.assertTrue(probe.native_success(good))
		for mutation in (
			{"phase": "error"},
			{"frameCount": 9},
			{"lastRenderSucceeded": False},
		):
			candidate = dict(good)
			candidate.update(mutation)
			with self.subTest(candidate=candidate):
				self.assertFalse(probe.native_success(candidate))

	def test_success_rejects_missing_or_wrong_types(self) -> None:
		self.assertFalse(probe.native_success(None))
		self.assertFalse(probe.native_success({}))
		self.assertFalse(probe.native_success({
			"phase": "running", "frameCount": "10", "lastRenderSucceeded": True,
		}))

	def test_parser_exposes_safe_preflight_mode(self) -> None:
		args = probe.parser().parse_args(["--preflight-only"])
		self.assertTrue(args.preflight_only)
		self.assertFalse(args.click_without_prompt)


if __name__ == "__main__":
	unittest.main(verbosity=2)
