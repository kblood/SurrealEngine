#!/usr/bin/env python3
"""Deterministic synthetic tests for audit_web_release.py."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


WEB_DIRECTORY = Path(__file__).resolve().parent
AUDIT_SCRIPT = WEB_DIRECTORY / "audit_web_release.py"
sys.path.insert(0, str(WEB_DIRECTORY))

from audit_web_release import audit_release  # noqa: E402


SHELL_REFERENCES = (
	"./index_webxr.html",
	"./offline.html",
	"./manifest.webmanifest",
	"./pwa_register.js",
	"./ut99_importer.js",
	"./mutable_persistence.js",
	"./webxr_settings.js",
	"./webxr_launcher.js",
	"./webxr_session.js",
	"../Assets/surreal-engine-icon.svg",
	"../Resources/surreal-engine-icon-128.png",
	"../Resources/surreal-engine-icon-256.png",
)


def write_text(root: Path, relative: str, text: str) -> None:
	path = root / relative
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_text(text, encoding="utf-8", newline="\n")


def write_bytes(root: Path, relative: str, data: bytes) -> None:
	path = root / relative
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(data)


def build_valid_fixture(root: Path, runtime_directory: str = "build-emscripten-nodata") -> None:
	write_text(root, "web/index_webxr.html", """<!doctype html>
<html><head><link rel="manifest" href="manifest.webmanifest"></head><body>
<script src="webxr_session.js"></script>
<script src="ut99_importer.js"></script>
<script src="mutable_persistence.js"></script>
<script src="webxr_settings.js"></script>
<script src="webxr_launcher.js"></script>
<script src="pwa_register.js"></script>
</body></html>
""")
	write_text(root, "web/offline.html", "<!doctype html><title>Offline</title>")
	manifest = {
		"id": "./index_webxr.html",
		"name": "Synthetic SurrealEngine WebXR",
		"short_name": "Synthetic XR",
		"start_url": f"./index_webxr.html?pwa=1&build={runtime_directory}",
		"scope": "./",
		"display": "standalone",
		"icons": [
			{"src": "../Assets/surreal-engine-icon.svg", "sizes": "any", "type": "image/svg+xml"},
			{"src": "../Resources/surreal-engine-icon-128.png", "sizes": "128x128", "type": "image/png"},
			{"src": "../Resources/surreal-engine-icon-256.png", "sizes": "256x256", "type": "image/png"},
		],
	}
	write_text(root, "web/manifest.webmanifest", json.dumps(manifest, indent=2))
	write_text(root, "web/pwa_register.js",
		'navigator.serviceWorker.register("service-worker.js", { scope: "./" });\n')
	write_text(root, "web/ut99_importer.js", "globalThis.SyntheticImporter = {};\n")
	write_text(root, "web/mutable_persistence.js", "globalThis.SyntheticMutable = {};\n")
	write_text(root, "web/webxr_settings.js", "globalThis.SyntheticSettings = {};\n")
	write_text(root, "web/webxr_launcher.js", "globalThis.SyntheticLauncher = {};\n")
	write_text(root, "web/webxr_session.js", "globalThis.SyntheticXR = {};\n")
	shell_json = ",\n\t".join(json.dumps(value) for value in SHELL_REFERENCES)
	runtime_references = (
		"../build-emscripten-nodata/SurrealEngine.js",
		"../build-emscripten-nodata/SurrealEngine.wasm",
		"../dist/SurrealEngine.js",
		"../dist/SurrealEngine.wasm",
	)
	runtime_json = ",\n\t".join(json.dumps(value) for value in runtime_references)
	write_text(root, "web/service-worker.js", f""""use strict";
const APP_VERSION = "synthetic-v1";
const SHELL_URLS = [
	{shell_json}
].map(path => new URL(path, self.location.href).href);
const IMMUTABLE_RUNTIME_URLS = new Set([
	{runtime_json}
].map(path => new URL(path, self.location.href).pathname));
const COMMERCIAL_EXTENSIONS = /\\.(?:data|u|unr|utx|uax|umx|uz|uz2)$/i;
const policy = "blocked-commercial-data";
const forbiddenRoot = "/gamedata";
""")
	write_text(root, "Assets/surreal-engine-icon.svg",
		'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1 1"></svg>\n')
	write_bytes(root, "Resources/surreal-engine-icon-128.png", b"\x89PNG\r\n\x1a\nsynthetic-128")
	write_bytes(root, "Resources/surreal-engine-icon-256.png", b"\x89PNG\r\n\x1a\nsynthetic-256")
	write_text(root, f"{runtime_directory}/SurrealEngine.js", "globalThis.Module = {};\n")
	write_bytes(root, f"{runtime_directory}/SurrealEngine.wasm", b"\x00asm\x01\x00\x00\x00")


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


def error_ids(result: dict) -> set[str]:
	return {error["id"] for error in result["errors"]}


class ReleaseAuditTests(unittest.TestCase):
	def test_valid_synthetic_stage_passes_without_mutation(self) -> None:
		with tempfile.TemporaryDirectory() as temp_directory:
			root = Path(temp_directory)
			build_valid_fixture(root)
			before = snapshot(root)
			result = audit_release(root)
			after = snapshot(root)

			self.assertTrue(result["ok"], result["errors"])
			self.assertEqual(result["runtimeDirectory"], "build-emscripten-nodata")
			self.assertEqual(result["summary"]["errors"], 0)
			self.assertEqual(before, after, "the read-only audit changed the release stage")
			artifact_paths = {artifact["path"] for artifact in result["artifacts"]}
			self.assertIn("build-emscripten-nodata/SurrealEngine.wasm", artifact_paths)
			self.assertEqual(result["mimeExpectations"][".wasm"], "application/wasm")

	def test_dist_runtime_is_supported_when_manifest_selects_it(self) -> None:
		with tempfile.TemporaryDirectory() as temp_directory:
			root = Path(temp_directory)
			build_valid_fixture(root, runtime_directory="dist")
			result = audit_release(root)
			self.assertTrue(result["ok"], result["errors"])
			self.assertEqual(result["runtimeDirectory"], "dist")

	def test_commercial_payload_names_and_layout_are_rejected(self) -> None:
		with tempfile.TemporaryDirectory() as temp_directory:
			root = Path(temp_directory)
			build_valid_fixture(root)
			write_bytes(root, "SurrealEngine.data", b"not commercial, but forbidden")
			write_bytes(root, "gamedata/System/Botpack.u", b"synthetic")
			write_bytes(root, "Install/Setup.exe", b"synthetic")
			for directory in ("Maps", "Textures", "Sounds"):
				write_text(root, f"Imported/{directory}/placeholder.txt", "synthetic")

			result = audit_release(root)
			self.assertFalse(result["ok"])
			self.assertTrue({
				"commercial-data-extension", "commercial-data-path", "commercial-install-layout",
			}.issubset(error_ids(result)), result["errors"])

	def test_missing_and_escaping_references_are_rejected(self) -> None:
		with tempfile.TemporaryDirectory() as temp_directory:
			root = Path(temp_directory)
			build_valid_fixture(root)
			(root / "Resources/surreal-engine-icon-256.png").unlink()
			manifest = json.loads((root / "web/manifest.webmanifest").read_text(encoding="utf-8"))
			manifest["icons"][0]["src"] = "../../../outside.svg"
			write_text(root, "web/manifest.webmanifest", json.dumps(manifest))
			launcher = (root / "web/index_webxr.html").read_text(encoding="utf-8")
			write_text(root, "web/index_webxr.html",
				launcher.replace('<script src="pwa_register.js"></script>\n', ""))
			service_worker = (root / "web/service-worker.js").read_text(encoding="utf-8")
			service_worker = service_worker.replace(
				'"./offline.html"', '"./offline.html", "./missing-shell.js"', 1)
			write_text(root, "web/service-worker.js", service_worker)

			result = audit_release(root)
			self.assertFalse(result["ok"])
			self.assertTrue({
				"required-asset", "escaping-reference", "resolved-reference",
				"html-required-references", "manifest-required-icons",
			}.issubset(error_ids(result)), result["errors"])

	def test_invalid_signatures_and_preload_loader_are_rejected(self) -> None:
		with tempfile.TemporaryDirectory() as temp_directory:
			root = Path(temp_directory)
			build_valid_fixture(root)
			write_bytes(root, "build-emscripten-nodata/SurrealEngine.wasm", b"NOPE")
			write_text(root, "build-emscripten-nodata/SurrealEngine.js",
				'const packageName = "SurrealEngine.data"; loadPackage(packageName);\n')
			write_bytes(root, "Resources/surreal-engine-icon-128.png", b"not-png")

			result = audit_release(root)
			self.assertFalse(result["ok"])
			self.assertTrue({
				"wasm-signature", "png-signature", "runtime-preload-loader",
			}.issubset(error_ids(result)), result["errors"])

	def test_duplicate_complete_runtimes_are_rejected(self) -> None:
		with tempfile.TemporaryDirectory() as temp_directory:
			root = Path(temp_directory)
			build_valid_fixture(root)
			write_text(root, "dist/SurrealEngine.js", "globalThis.Module = {};\n")
			write_bytes(root, "dist/SurrealEngine.wasm", b"\x00asm\x01\x00\x00\x00")

			result = audit_release(root)
			self.assertFalse(result["ok"])
			self.assertIn("single-runtime", error_ids(result))

	def test_cli_emits_json_and_nonzero_for_invalid_stage(self) -> None:
		with tempfile.TemporaryDirectory() as temp_directory:
			root = Path(temp_directory)
			build_valid_fixture(root)
			valid = subprocess.run(
				[sys.executable, str(AUDIT_SCRIPT), str(root)],
				capture_output=True, text=True, check=False,
			)
			self.assertEqual(valid.returncode, 0, valid.stderr)
			self.assertTrue(json.loads(valid.stdout)["ok"])

			(root / "web/offline.html").unlink()
			invalid = subprocess.run(
				[sys.executable, str(AUDIT_SCRIPT), str(root), "--pretty"],
				capture_output=True, text=True, check=False,
			)
			self.assertNotEqual(invalid.returncode, 0)
			diagnostics = json.loads(invalid.stdout)
			self.assertFalse(diagnostics["ok"])
			self.assertEqual(diagnostics["schema"], "surrealengine-web-release-audit-v1")

	def test_cli_requires_an_explicit_directory(self) -> None:
		completed = subprocess.run(
			[sys.executable, str(AUDIT_SCRIPT)],
			capture_output=True, text=True, check=False,
		)
		self.assertNotEqual(completed.returncode, 0)
		diagnostics = json.loads(completed.stdout)
		self.assertEqual(diagnostics["errors"][0]["id"], "invalid-root")


if __name__ == "__main__":
	unittest.main(verbosity=2)
