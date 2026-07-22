#!/usr/bin/env python3
"""Read-only audit for a staged redistributable SurrealEngine WebXR release."""

from __future__ import annotations

import argparse
import hashlib
from html.parser import HTMLParser
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import stat
import sys
from typing import Any
from urllib.parse import parse_qs, unquote, urlsplit


SCHEMA = "surrealengine-web-release-audit-v1"
REQUIRED_SHELL_FILES = (
	"web/index_webxr.html",
	"web/offline.html",
	"web/manifest.webmanifest",
	"web/pwa_register.js",
	"web/ut99_importer.js",
	"web/mutable_persistence.js",
	"web/webxr_settings.js",
	"web/webxr_launcher.js",
	"web/webxr_session.js",
	"web/service-worker.js",
	"Assets/surreal-engine-icon.svg",
	"Resources/surreal-engine-icon-128.png",
	"Resources/surreal-engine-icon-256.png",
)
RUNTIME_DIRECTORIES = ("build-emscripten-nodata", "dist")
RUNTIME_FILES = ("SurrealEngine.js", "SurrealEngine.wasm")
FORBIDDEN_EXTENSIONS = {
	".data", ".u", ".unr", ".utx", ".uax", ".umx", ".uz", ".uz2", ".exe",
}
FORBIDDEN_PATH_COMPONENTS = {
	"gamedata", "ut99-data", "surrealengine-ut99-data-v1",
}
UT_LAYOUT_DIRECTORIES = {"system", "maps", "textures", "sounds", "music"}
EXPECTED_MIME_TYPES = {
	".html": "text/html; charset=utf-8",
	".js": "text/javascript; charset=utf-8",
	".wasm": "application/wasm",
	".webmanifest": "application/manifest+json; charset=utf-8",
	".svg": "image/svg+xml",
	".png": "image/png",
}
PRELOAD_JS_MARKERS = (
	"SurrealEngine.data",
	"remote_package_size",
	"loadPackage(",
	"DataRequest(",
)


class ReleaseHTMLParser(HTMLParser):
	def __init__(self) -> None:
		super().__init__()
		self.references: list[tuple[str, str]] = []

	def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
		values = {name.lower(): value for name, value in attrs if value is not None}
		if tag.lower() == "script" and values.get("src"):
			self.references.append(("script", values["src"]))
		elif tag.lower() == "img" and values.get("src"):
			self.references.append(("image", values["src"]))
		elif tag.lower() == "link" and values.get("href"):
			relations = set(values.get("rel", "").lower().split())
			if relations.intersection({"manifest", "icon", "apple-touch-icon"}):
				kind = "manifest" if "manifest" in relations else "image"
				self.references.append((kind, values["href"]))


class ReleaseAuditor:
	def __init__(self, root: Path):
		self.root = root
		self.checks: list[dict[str, Any]] = []
		self.errors: list[dict[str, Any]] = []
		self.warnings: list[dict[str, Any]] = []
		self.files: dict[str, Path] = {}
		self.artifacts: list[dict[str, Any]] = []
		self.runtime_directory: str | None = None

	def check(
		self,
		condition: bool,
		code: str,
		message: str,
		*,
		path: str | None = None,
		details: Any = None,
	) -> bool:
		item: dict[str, Any] = {"id": code, "ok": bool(condition), "message": message}
		if path is not None:
			item["path"] = path
		if details is not None:
			item["details"] = details
		self.checks.append(item)
		if not condition:
			self.errors.append({key: value for key, value in item.items() if key != "ok"})
		return bool(condition)

	def audit(self) -> dict[str, Any]:
		self._scan_stage()
		self._check_required_shell()
		self._select_runtime()
		self._check_file_signatures()
		manifest = self._check_manifest()
		self._check_index_references()
		self._check_service_worker()
		self._check_registration_reference()
		self._check_runtime_preload_markers()
		self._check_manifest_runtime(manifest)
		self._record_artifacts()
		return self.result()

	def result(self) -> dict[str, Any]:
		return {
			"schema": SCHEMA,
			"root": str(self.root),
			"ok": not self.errors,
			"runtimeDirectory": self.runtime_directory,
			"summary": {
				"filesScanned": len(self.files),
				"checks": len(self.checks),
				"errors": len(self.errors),
				"warnings": len(self.warnings),
			},
			"mimeExpectations": dict(sorted(EXPECTED_MIME_TYPES.items())),
			"artifacts": sorted(self.artifacts, key=lambda item: item["path"]),
			"checks": self.checks,
			"errors": self.errors,
			"warnings": self.warnings,
		}

	def _scan_stage(self) -> None:
		directory_children: dict[str, set[str]] = {}
		for directory, dirnames, filenames in os.walk(self.root, topdown=True, followlinks=False):
			dirnames.sort(key=str.casefold)
			filenames.sort(key=str.casefold)
			directory_path = Path(directory)
			parent_rel = directory_path.relative_to(self.root).as_posix()
			children = directory_children.setdefault(parent_rel, set())

			kept_directories: list[str] = []
			for name in dirnames:
				path = directory_path / name
				relative = path.relative_to(self.root).as_posix()
				children.add(name.casefold())
				if path.is_symlink():
					self.check(False, "symlink", "release stages must not contain symlinks", path=relative)
				else:
					kept_directories.append(name)
			dirnames[:] = kept_directories

			for name in filenames:
				path = directory_path / name
				relative = path.relative_to(self.root).as_posix()
				if path.is_symlink():
					self.check(False, "symlink", "release stages must not contain symlinks", path=relative)
					continue
				try:
					mode = path.stat().st_mode
				except OSError as error:
					self.check(False, "unreadable-file", "could not inspect staged file", path=relative,
						details=str(error))
					continue
				if not stat.S_ISREG(mode):
					self.check(False, "non-regular-file", "release stages may contain regular files only",
						path=relative)
					continue
				self.files[relative] = path

				parts = {part.casefold() for part in PurePosixPath(relative).parts}
				if parts.intersection(FORBIDDEN_PATH_COMPONENTS):
					self.check(False, "commercial-data-path",
						"path resembles browser-imported or staged commercial game data", path=relative)
				if path.suffix.casefold() in FORBIDDEN_EXTENSIONS:
					self.check(False, "commercial-data-extension",
						"file extension is forbidden in a redistributable WebXR release", path=relative)

		for parent, children in sorted(directory_children.items()):
			matched = sorted(children.intersection(UT_LAYOUT_DIRECTORIES))
			if len(matched) >= 3:
				self.check(False, "commercial-install-layout",
					"directory resembles an unpacked Unreal Tournament installation",
					path=parent or ".", details=matched)

		self.check(bool(self.files), "nonempty-stage", "release stage contains regular files")

	def _safe_file(self, relative: str) -> Path | None:
		return self.files.get(PurePosixPath(relative).as_posix())

	def _check_required_shell(self) -> None:
		for relative in REQUIRED_SHELL_FILES:
			path = self._safe_file(relative)
			self.check(path is not None, "required-asset", "required release asset exists", path=relative)
			if path is not None:
				try:
					nonempty = path.stat().st_size > 0
				except OSError:
					nonempty = False
				self.check(nonempty, "nonempty-asset", "required release asset is nonempty", path=relative)

	def _select_runtime(self) -> None:
		complete: list[str] = []
		for directory in RUNTIME_DIRECTORIES:
			present = [f"{directory}/{name}" in self.files for name in RUNTIME_FILES]
			if any(present) and not all(present):
				self.check(False, "incomplete-runtime",
					"runtime directory must contain both SurrealEngine.js and SurrealEngine.wasm",
					path=directory, details={name: exists for name, exists in zip(RUNTIME_FILES, present)})
			elif all(present):
				complete.append(directory)
		self.check(len(complete) == 1, "single-runtime",
			"release contains exactly one complete allowlisted no-data runtime",
			details=complete)
		if len(complete) == 1:
			self.runtime_directory = complete[0]

	def _read_bytes(self, relative: str, limit: int | None = None) -> bytes | None:
		path = self._safe_file(relative)
		if path is None:
			return None
		try:
			with path.open("rb") as stream:
				return stream.read() if limit is None else stream.read(limit)
		except OSError as error:
			self.check(False, "unreadable-file", "could not read staged file", path=relative,
				details=str(error))
			return None

	def _read_text(self, relative: str) -> str | None:
		data = self._read_bytes(relative)
		if data is None:
			return None
		try:
			return data.decode("utf-8")
		except UnicodeDecodeError as error:
			self.check(False, "utf8", "text release asset must be valid UTF-8", path=relative,
				details=str(error))
			return None

	def _check_file_signatures(self) -> None:
		wasm_relative = (f"{self.runtime_directory}/SurrealEngine.wasm"
			if self.runtime_directory else None)
		if wasm_relative:
			wasm = self._read_bytes(wasm_relative, 4)
			self.check(wasm == b"\x00asm", "wasm-signature",
				"SurrealEngine.wasm has the WebAssembly magic signature", path=wasm_relative)

		for relative in ("Resources/surreal-engine-icon-128.png",
			"Resources/surreal-engine-icon-256.png"):
			if relative in self.files:
				data = self._read_bytes(relative, 8)
				self.check(data == b"\x89PNG\r\n\x1a\n", "png-signature",
					"PNG icon has the expected signature", path=relative)

		svg_relative = "Assets/surreal-engine-icon.svg"
		if svg_relative in self.files:
			data = self._read_bytes(svg_relative, 4096)
			valid = data is not None and re.search(br"<svg(?:\s|>)", data, re.IGNORECASE) is not None
			self.check(valid, "svg-signature", "SVG icon contains an SVG root element", path=svg_relative)

	def _resolve_reference(self, source: str, reference: str) -> str | None:
		parsed = urlsplit(reference)
		if parsed.scheme or parsed.netloc:
			self.check(False, "external-reference", "release asset reference must be same-origin/local",
				path=source, details=reference)
			return None
		decoded = unquote(parsed.path)
		if not decoded or "\x00" in decoded or "\\" in decoded:
			self.check(False, "invalid-reference", "release asset reference is malformed",
				path=source, details=reference)
			return None
		if decoded.startswith("/"):
			joined = decoded.lstrip("/")
		else:
			joined = posixpath.join(posixpath.dirname(source), decoded)
		normalized = posixpath.normpath(joined)
		if normalized == ".." or normalized.startswith("../") or normalized.startswith("/"):
			self.check(False, "escaping-reference", "release asset reference escapes the explicit stage",
				path=source, details=reference)
			return None
		return PurePosixPath(normalized).as_posix()

	def _check_resolved_reference(self, source: str, reference: str, kind: str) -> str | None:
		resolved = self._resolve_reference(source, reference)
		if resolved is None:
			return None
		self.check(resolved in self.files, "resolved-reference",
			f"{kind} reference resolves to a staged file", path=source,
			details={"reference": reference, "resolved": resolved})
		return resolved

	def _check_manifest(self) -> dict[str, Any] | None:
		relative = "web/manifest.webmanifest"
		text = self._read_text(relative)
		if text is None:
			return None
		try:
			manifest = json.loads(text)
		except (json.JSONDecodeError, UnicodeDecodeError) as error:
			self.check(False, "manifest-json", "manifest is valid JSON", path=relative,
				details=str(error))
			return None
		self.check(isinstance(manifest, dict), "manifest-object", "manifest root is an object", path=relative)
		if not isinstance(manifest, dict):
			return None

		self.check(manifest.get("display") == "standalone", "manifest-display",
			"manifest requests standalone display", path=relative)
		for key in ("id", "start_url"):
			value = manifest.get(key)
			self.check(isinstance(value, str) and bool(value), "manifest-field",
				f"manifest contains nonempty {key}", path=relative)
			if isinstance(value, str) and value:
				self._check_resolved_reference(relative, value, f"manifest {key}")
		scope = manifest.get("scope")
		self.check(isinstance(scope, str) and bool(scope), "manifest-field",
			"manifest contains nonempty scope", path=relative)
		if isinstance(scope, str) and scope:
			resolved_scope = self._resolve_reference(relative, scope)
			scope_path = (self.root.joinpath(*PurePosixPath(resolved_scope).parts)
				if resolved_scope is not None else None)
			self.check(scope_path is not None and scope_path.is_dir(), "manifest-scope",
				"manifest scope resolves to a staged directory", path=relative,
				details={"reference": scope, "resolved": resolved_scope})

		icons = manifest.get("icons")
		self.check(isinstance(icons, list) and bool(icons), "manifest-icons",
			"manifest declares at least one icon", path=relative)
		declared_icons: set[str] = set()
		if isinstance(icons, list):
			for index, icon in enumerate(icons):
				if not isinstance(icon, dict) or not isinstance(icon.get("src"), str):
					self.check(False, "manifest-icon", "manifest icon has a local src",
						path=relative, details=index)
					continue
				resolved = self._check_resolved_reference(relative, icon["src"], "manifest icon")
				if resolved:
					declared_icons.add(resolved)
					expected = EXPECTED_MIME_TYPES.get(PurePosixPath(resolved).suffix.casefold())
					declared = icon.get("type")
					self.check(expected is not None and declared == expected.split(";", 1)[0],
						"manifest-icon-mime", "manifest icon MIME matches its file extension",
						path=resolved, details={"declared": declared, "expected": expected})
		required_icons = {
			"Assets/surreal-engine-icon.svg",
			"Resources/surreal-engine-icon-128.png",
			"Resources/surreal-engine-icon-256.png",
		}
		self.check(required_icons.issubset(declared_icons), "manifest-required-icons",
			"manifest declares every required release icon", path=relative,
			details=sorted(required_icons - declared_icons))
		return manifest

	def _check_index_references(self) -> None:
		relative = "web/index_webxr.html"
		text = self._read_text(relative)
		if text is None:
			return
		parser = ReleaseHTMLParser()
		try:
			parser.feed(text)
		except Exception as error:
			self.check(False, "html-parse", "launcher HTML references can be parsed", path=relative,
				details=str(error))
			return
		manifest_references = 0
		resolved_references: set[str] = set()
		for kind, reference in parser.references:
			resolved = self._check_resolved_reference(relative, reference, f"HTML {kind}")
			if resolved:
				resolved_references.add(resolved)
			if kind == "manifest" and resolved == "web/manifest.webmanifest":
				manifest_references += 1
		self.check(manifest_references == 1, "html-manifest",
			"launcher references the staged manifest exactly once", path=relative,
			details=manifest_references)
		required_references = {
			"web/manifest.webmanifest",
			"web/pwa_register.js",
			"web/ut99_importer.js",
			"web/mutable_persistence.js",
			"web/webxr_settings.js",
			"web/webxr_launcher.js",
			"web/webxr_session.js",
		}
		self.check(required_references.issubset(resolved_references), "html-required-references",
			"launcher references every required manifest/bridge script",
			path=relative, details=sorted(required_references - resolved_references))

	def _extract_js_array(self, text: str, name: str, source: str) -> list[str]:
		match = re.search(
			r"const\s+" + re.escape(name) +
			r"\s*=\s*(?:new\s+Set\s*\(\s*)?\[(.*?)\]\s*\.map",
			text,
			re.DOTALL,
		)
		if not match:
			self.check(False, "service-worker-array", f"service worker declares {name}", path=source)
			return []
		values: list[str] = []
		for literal in re.findall(r'"(?:[^"\\]|\\.)*"', match.group(1)):
			try:
				values.append(json.loads(literal))
			except json.JSONDecodeError:
				self.check(False, "service-worker-string", f"{name} contains valid string literals",
					path=source, details=literal)
		return values

	def _check_service_worker(self) -> None:
		relative = "web/service-worker.js"
		text = self._read_text(relative)
		if text is None:
			return
		version = re.search(r'const\s+APP_VERSION\s*=\s*"([^"\r\n]+)"', text)
		self.check(version is not None, "service-worker-version",
			"service worker declares a nonempty APP_VERSION", path=relative,
			details=version.group(1) if version else None)

		shell_references = self._extract_js_array(text, "SHELL_URLS", relative)
		resolved_shell: set[str] = set()
		for reference in shell_references:
			resolved = self._check_resolved_reference(relative, reference, "service-worker shell")
			if resolved:
				resolved_shell.add(resolved)
		required_precache = set(REQUIRED_SHELL_FILES) - {"web/service-worker.js"}
		self.check(required_precache.issubset(resolved_shell), "service-worker-shell-allowlist",
			"service-worker shell allowlist covers every required cacheable shell asset",
			path=relative, details=sorted(required_precache - resolved_shell))

		runtime_references = self._extract_js_array(text, "IMMUTABLE_RUNTIME_URLS", relative)
		resolved_runtime = {
			resolved for reference in runtime_references
			if (resolved := self._resolve_reference(relative, reference)) is not None
		}
		if self.runtime_directory:
			required_runtime = {
				f"{self.runtime_directory}/SurrealEngine.js",
				f"{self.runtime_directory}/SurrealEngine.wasm",
			}
			self.check(required_runtime.issubset(resolved_runtime), "service-worker-runtime-allowlist",
				"service worker allowlists both selected no-data runtime artifacts",
				path=relative, details=sorted(required_runtime - resolved_runtime))

		policy_markers = ("blocked-commercial-data", "/gamedata", "COMMERCIAL_EXTENSIONS")
		missing_markers = [marker for marker in policy_markers if marker not in text]
		self.check(not missing_markers, "service-worker-commercial-policy",
			"service worker contains explicit commercial-data rejection policy",
			path=relative, details=missing_markers)

	def _check_registration_reference(self) -> None:
		relative = "web/pwa_register.js"
		text = self._read_text(relative)
		if text is None:
			return
		match = re.search(r'navigator\.serviceWorker\.register\(\s*["\']([^"\']+)["\']', text)
		self.check(match is not None, "service-worker-registration",
			"PWA registration script names a service worker", path=relative)
		if match:
			resolved = self._check_resolved_reference(relative, match.group(1), "service-worker registration")
			self.check(resolved == "web/service-worker.js", "service-worker-registration-target",
				"PWA registration targets the audited service worker", path=relative,
				details=resolved)

	def _check_runtime_preload_markers(self) -> None:
		if not self.runtime_directory:
			return
		relative = f"{self.runtime_directory}/SurrealEngine.js"
		text = self._read_text(relative)
		if text is None:
			return
		found = [marker for marker in PRELOAD_JS_MARKERS if marker in text]
		self.check(not found, "runtime-preload-loader",
			"generated runtime JavaScript contains no Emscripten data-package loader",
			path=relative, details=found)

	def _check_manifest_runtime(self, manifest: dict[str, Any] | None) -> None:
		if not manifest or not self.runtime_directory:
			return
		start_url = manifest.get("start_url")
		if not isinstance(start_url, str):
			return
		query = parse_qs(urlsplit(start_url).query)
		self.check(query.get("pwa") == ["1"], "manifest-pwa-entry",
			"manifest start_url explicitly enables PWA mode", path="web/manifest.webmanifest",
			details=query)
		self.check(query.get("build") == [self.runtime_directory], "manifest-runtime-entry",
			"manifest start_url selects the staged no-data runtime directory",
			path="web/manifest.webmanifest", details=query.get("build"))

	def _record_artifacts(self) -> None:
		paths = list(REQUIRED_SHELL_FILES)
		if self.runtime_directory:
			paths.extend(f"{self.runtime_directory}/{name}" for name in RUNTIME_FILES)
		for relative in sorted(set(paths)):
			path = self._safe_file(relative)
			if path is None:
				continue
			expected_mime = EXPECTED_MIME_TYPES.get(path.suffix.casefold())
			self.check(expected_mime is not None, "static-mime-expectation",
				"release asset type has an explicit deployment MIME expectation", path=relative,
				details=expected_mime)
			try:
				digest = hashlib.sha256()
				with path.open("rb") as stream:
					for chunk in iter(lambda: stream.read(1024 * 1024), b""):
						digest.update(chunk)
				self.artifacts.append({
					"path": relative,
					"bytes": path.stat().st_size,
					"sha256": digest.hexdigest(),
					"expectedMime": expected_mime,
				})
			except OSError as error:
				self.check(False, "hash-read", "could not hash staged release asset", path=relative,
					details=str(error))


def input_error(root: str | None, message: str) -> dict[str, Any]:
	return {
		"schema": SCHEMA,
		"root": root,
		"ok": False,
		"runtimeDirectory": None,
		"summary": {"filesScanned": 0, "checks": 0, "errors": 1, "warnings": 0},
		"mimeExpectations": dict(sorted(EXPECTED_MIME_TYPES.items())),
		"artifacts": [],
		"checks": [],
		"errors": [{"id": "invalid-root", "message": message}],
		"warnings": [],
	}


def audit_release(release_directory: str | os.PathLike[str]) -> dict[str, Any]:
	requested = Path(release_directory)
	if requested.is_symlink():
		return input_error(str(requested), "release directory itself must not be a symlink")
	try:
		root = requested.resolve(strict=True)
	except OSError as error:
		return input_error(str(requested), f"release directory is unavailable: {error}")
	if not root.is_dir():
		return input_error(str(root), "release path must be an explicit directory")
	if root.parent == root:
		return input_error(str(root), "refusing to audit a filesystem root")
	return ReleaseAuditor(root).audit()


def main(argv: list[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("release_directory", nargs="?",
		help="explicit root of the staged no-data WebXR release")
	parser.add_argument("--pretty", action="store_true", help="indent JSON diagnostics")
	args = parser.parse_args(argv)
	if not args.release_directory:
		result = input_error(None, "an explicit release directory argument is required")
	else:
		result = audit_release(args.release_directory)
	json.dump(result, sys.stdout, indent=2 if args.pretty else None, sort_keys=True)
	sys.stdout.write("\n")
	return 0 if result["ok"] else 1


if __name__ == "__main__":
	raise SystemExit(main())
