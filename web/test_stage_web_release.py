#!/usr/bin/env python3
"""Deterministic and adversarial tests for stage_web_release.py."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


WEB_DIRECTORY = Path(__file__).resolve().parent
STAGE_SCRIPT = WEB_DIRECTORY / "stage_web_release.py"
sys.path.insert(0, str(WEB_DIRECTORY))

from stage_web_release import (  # noqa: E402
	RELEASE_MANIFEST,
	SHELL_ALLOWLIST,
	STAGE_SCHEMA,
	stage_release,
)


def write_text(root: Path, relative: str, text: str) -> None:
	path = root / relative
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(text, encoding="utf-8", newline="\n")


def write_bytes(root: Path, relative: str, data: bytes) -> None:
	path = root / relative
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(data)


def shell_reference(relative: str) -> str:
	return "./" + PurePosixPath(relative).name if relative.startswith("web/") else "../" + relative


def build_valid_inputs(base: Path, runtime_name: str = "build-emscripten-nodata") -> tuple[Path, Path]:
	source = base / "source"
	runtime = base / runtime_name
	source.mkdir()
	runtime.mkdir()

	index_scripts = (
		"webxr_session.js",
		"ut99_importer.js",
		"mutable_persistence.js",
		"webxr_settings.js",
		"webxr_launcher.js",
		"pwa_register.js",
	)
	write_text(source, "web/index_webxr.html", "<!doctype html><html><head>\n" +
		'<link rel="manifest" href="manifest.webmanifest">\n</head><body>\n' +
		"".join(f'<script src="{name}"></script>\n' for name in index_scripts) +
		"</body></html>\n")
	write_text(source, "web/offline.html", "<!doctype html><title>Offline</title>\n")
	manifest = {
		"id": "./index_webxr.html",
		"name": "Synthetic SurrealEngine WebXR",
		"short_name": "Synthetic XR",
		"start_url": f"./index_webxr.html?pwa=1&build={runtime_name}",
		"scope": "./",
		"display": "standalone",
		"icons": [
			{"src": "../Assets/surreal-engine-icon.svg", "sizes": "any", "type": "image/svg+xml"},
			{"src": "../Resources/surreal-engine-icon-128.png", "sizes": "128x128", "type": "image/png"},
			{"src": "../Resources/surreal-engine-icon-256.png", "sizes": "256x256", "type": "image/png"},
		],
	}
	write_text(source, "web/manifest.webmanifest", json.dumps(manifest, indent=2))
	write_text(source, "web/pwa_register.js",
		'navigator.serviceWorker.register("service-worker.js", { scope: "./" });\n')
	for relative in (
		"web/ut99_importer.js",
		"web/mutable_persistence.js",
		"web/webxr_settings.js",
		"web/webxr_launcher.js",
		"web/webxr_session.js",
	):
		write_text(source, relative, f"// synthetic {relative}\n")

	shell_references = [
		shell_reference(relative) for relative in SHELL_ALLOWLIST
		if relative != "web/service-worker.js"
	]
	runtime_references = (
		"../build-emscripten-nodata/SurrealEngine.js",
		"../build-emscripten-nodata/SurrealEngine.wasm",
		"../dist/SurrealEngine.js",
		"../dist/SurrealEngine.wasm",
	)
	write_text(source, "web/service-worker.js", """"use strict";
const APP_VERSION = "synthetic-stage-v1";
const SHELL_URLS = %s.map(path => new URL(path, self.location.href).href);
const IMMUTABLE_RUNTIME_URLS = new Set(%s.map(path => new URL(path, self.location.href).pathname));
const COMMERCIAL_EXTENSIONS = /\\.(?:data|u|unr|utx|uax|umx|uz|uz2)$/i;
const policy = "blocked-commercial-data";
const forbiddenRoot = "/gamedata";
""" % (json.dumps(shell_references, indent=2), json.dumps(runtime_references, indent=2)))
	write_text(source, "Assets/surreal-engine-icon.svg",
		'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1 1"></svg>\n')
	write_bytes(source, "Resources/surreal-engine-icon-128.png", b"\x89PNG\r\n\x1a\nsynthetic-128")
	write_bytes(source, "Resources/surreal-engine-icon-256.png", b"\x89PNG\r\n\x1a\nsynthetic-256")
	write_text(runtime, "SurrealEngine.js", "globalThis.Module = {};\n")
	write_bytes(runtime, "SurrealEngine.wasm", b"\x00asm\x01\x00\x00\x00")
	return source, runtime


def snapshot(root: Path) -> dict[str, tuple[int, int, str]]:
	result: dict[str, tuple[int, int, str]] = {}
	for path in sorted((item for item in root.rglob("*") if item.is_file()),
		key=lambda item: item.relative_to(root).as_posix()):
		data = path.read_bytes()
		result[path.relative_to(root).as_posix()] = (
			path.stat().st_size,
			path.stat().st_mtime_ns,
			hashlib.sha256(data).hexdigest(),
		)
	return result


def staged_bytes(root: Path) -> dict[str, bytes]:
	return {
		path.relative_to(root).as_posix(): path.read_bytes()
		for path in sorted((item for item in root.rglob("*") if item.is_file()),
			key=lambda item: item.relative_to(root).as_posix())
	}


class WebReleaseStagerTests(unittest.TestCase):
	def test_valid_output_is_exact_reproducible_audited_and_source_immutable(self) -> None:
		with tempfile.TemporaryDirectory() as temp_directory:
			base = Path(temp_directory)
			source, runtime = build_valid_inputs(base)
			destination_a = base / "release-a"
			destination_b = base / "release-b"
			destination_b.mkdir()
			source_before = snapshot(source)
			runtime_before = snapshot(runtime)

			result_a = stage_release(source, runtime, destination_a)
			result_b = stage_release(source, runtime, destination_b)

			self.assertTrue(result_a["ok"], result_a["errors"])
			self.assertTrue(result_b["ok"], result_b["errors"])
			self.assertTrue(result_a["destinationCreated"])
			self.assertFalse(result_b["destinationCreated"])
			self.assertTrue(result_a["audit"]["ok"])
			self.assertEqual(snapshot(source), source_before)
			self.assertEqual(snapshot(runtime), runtime_before)
			self.assertEqual(staged_bytes(destination_a), staged_bytes(destination_b))

			expected_files = set(SHELL_ALLOWLIST) | {
				"build-emscripten-nodata/SurrealEngine.js",
				"build-emscripten-nodata/SurrealEngine.wasm",
				RELEASE_MANIFEST,
			}
			self.assertEqual(set(staged_bytes(destination_a)), expected_files)
			manifest_bytes = (destination_a / RELEASE_MANIFEST).read_bytes()
			manifest = json.loads(manifest_bytes)
			self.assertEqual(manifest["schema"], STAGE_SCHEMA)
			self.assertEqual([entry["path"] for entry in manifest["files"]],
				sorted(expected_files - {RELEASE_MANIFEST}))
			self.assertNotIn(str(source).encode(), manifest_bytes)
			self.assertNotIn(str(runtime).encode(), manifest_bytes)
			self.assertNotIn(b"timestamp", manifest_bytes.lower())

	def test_cli_emits_machine_readable_success(self) -> None:
		with tempfile.TemporaryDirectory() as temp_directory:
			base = Path(temp_directory)
			source, runtime = build_valid_inputs(base, runtime_name="dist")
			destination = base / "release"
			completed = subprocess.run([
				sys.executable, "-B", str(STAGE_SCRIPT),
				"--source-root", str(source),
				"--runtime-directory", str(runtime),
				"--destination", str(destination),
				"--pretty",
			], capture_output=True, text=True, check=False)
			self.assertEqual(completed.returncode, 0, completed.stderr)
			diagnostics = json.loads(completed.stdout)
			self.assertTrue(diagnostics["ok"])
			self.assertEqual(diagnostics["audit"]["runtimeDirectory"], "dist")

	def test_nonempty_destination_is_rejected_without_overwrite(self) -> None:
		with tempfile.TemporaryDirectory() as temp_directory:
			base = Path(temp_directory)
			source, runtime = build_valid_inputs(base)
			destination = base / "release"
			destination.mkdir()
			sentinel = destination / "keep.txt"
			sentinel.write_text("do not replace", encoding="utf-8")

			result = stage_release(source, runtime, destination)
			self.assertFalse(result["ok"])
			self.assertEqual(result["errors"][0]["id"], "nonempty-destination")
			self.assertEqual(sentinel.read_text(encoding="utf-8"), "do not replace")
			self.assertEqual(list(destination.iterdir()), [sentinel])

	def test_symlinked_selected_source_is_rejected_before_destination_creation(self) -> None:
		with tempfile.TemporaryDirectory() as temp_directory:
			base = Path(temp_directory)
			source, runtime = build_valid_inputs(base)
			destination = base / "release"
			selected = source / "web/offline.html"
			target = base / "outside.html"
			target.write_text("outside", encoding="utf-8")
			try:
				selected.unlink()
				os.symlink(target, selected)
			except OSError:
				selected.write_text("placeholder", encoding="utf-8")
				original = Path.is_symlink
				with mock.patch.object(Path, "is_symlink",
					new=lambda path: path == selected or original(path)):
					result = stage_release(source, runtime, destination)
			else:
				result = stage_release(source, runtime, destination)

			self.assertFalse(result["ok"])
			self.assertEqual(result["errors"][0]["id"], "symlink-source")
			self.assertFalse(destination.exists())

	def test_preload_loader_is_rejected_and_inputs_are_unchanged(self) -> None:
		with tempfile.TemporaryDirectory() as temp_directory:
			base = Path(temp_directory)
			source, runtime = build_valid_inputs(base)
			destination = base / "release"
			write_text(runtime, "SurrealEngine.js",
				'const packageName = "SurrealEngine.data"; loadPackage(packageName);\n')
			before = snapshot(runtime)

			result = stage_release(source, runtime, destination)
			self.assertFalse(result["ok"])
			self.assertEqual(result["errors"][0]["id"], "runtime-preload-loader")
			self.assertEqual(snapshot(runtime), before)
			self.assertFalse(destination.exists())

	def test_commercial_runtime_extension_and_path_are_rejected(self) -> None:
		cases = (
			("SurrealEngine.data", "commercial-runtime-extension"),
			("Maps/DM-Synthetic.unr", "commercial-runtime-extension"),
			("gamedata/System/readme.txt", "commercial-runtime-path"),
		)
		for relative, expected_error in cases:
			with self.subTest(relative=relative), tempfile.TemporaryDirectory() as temp_directory:
				base = Path(temp_directory)
				source, runtime = build_valid_inputs(base)
				write_text(runtime, relative, "synthetic forbidden payload")
				destination = base / "release"
				result = stage_release(source, runtime, destination)
				self.assertFalse(result["ok"])
				self.assertEqual(result["errors"][0]["id"], expected_error)
				self.assertFalse(destination.exists())

	def test_service_worker_allowlist_drift_is_rejected(self) -> None:
		with tempfile.TemporaryDirectory() as temp_directory:
			base = Path(temp_directory)
			source, runtime = build_valid_inputs(base)
			worker = source / "web/service-worker.js"
			text = worker.read_text(encoding="utf-8")
			worker.write_text(text.replace('  "./webxr_settings.js",\n', ""),
				encoding="utf-8", newline="\n")
			destination = base / "release"
			result = stage_release(source, runtime, destination)
			self.assertFalse(result["ok"])
			self.assertEqual(result["errors"][0]["id"], "shell-allowlist")
			self.assertFalse(destination.exists())

	def test_missing_and_signature_invalid_runtime_are_rejected(self) -> None:
		for mode, expected_error in (("missing", "missing-source"), ("signature", "wasm-signature")):
			with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temp_directory:
				base = Path(temp_directory)
				source, runtime = build_valid_inputs(base)
				wasm = runtime / "SurrealEngine.wasm"
				if mode == "missing":
					wasm.unlink()
				else:
					wasm.write_bytes(b"NOPE")
				destination = base / "release"
				result = stage_release(source, runtime, destination)
				self.assertFalse(result["ok"])
				self.assertEqual(result["errors"][0]["id"], expected_error)
				self.assertFalse(destination.exists())

	def test_destination_inside_source_is_rejected(self) -> None:
		with tempfile.TemporaryDirectory() as temp_directory:
			base = Path(temp_directory)
			source, runtime = build_valid_inputs(base)
			destination = source / "release"
			result = stage_release(source, runtime, destination)
			self.assertFalse(result["ok"])
			self.assertEqual(result["errors"][0]["id"], "overlapping-destination")
			self.assertFalse(destination.exists())


if __name__ == "__main__":
	unittest.main(verbosity=2)
