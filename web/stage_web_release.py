#!/usr/bin/env python3
"""Reproducibly stage an audited, redistributable SurrealEngine WebXR release."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
from typing import Any


WEB_DIRECTORY = Path(__file__).resolve().parent
if str(WEB_DIRECTORY) not in sys.path:
	sys.path.insert(0, str(WEB_DIRECTORY))

from audit_web_release import (  # noqa: E402
	EXPECTED_MIME_TYPES,
	FORBIDDEN_EXTENSIONS,
	FORBIDDEN_PATH_COMPONENTS,
	PRELOAD_JS_MARKERS,
	RUNTIME_DIRECTORIES,
	RUNTIME_FILES,
	UT_LAYOUT_DIRECTORIES,
	audit_release,
)


STAGE_SCHEMA = "surrealengine-web-release-stage-v1"
STAGE_VERSION = 1
RELEASE_MANIFEST = "web-release-manifest.json"
SHELL_ALLOWLIST = (
	"web/index_webxr.html",
	"web/offline.html",
	"web/manifest.webmanifest",
	"web/pwa_register.js",
	"web/ut99_importer.js",
	"web/mutable_persistence.js",
	"web/webxr_settings.js",
	"web/webxr_session.js",
	"web/service-worker.js",
	"Assets/surreal-engine-icon.svg",
	"Resources/surreal-engine-icon-128.png",
	"Resources/surreal-engine-icon-256.png",
)


class StageError(Exception):
	def __init__(self, code: str, message: str, *, path: str | None = None, details: Any = None):
		super().__init__(message)
		self.code = code
		self.message = message
		self.path = path
		self.details = details

	def diagnostic(self) -> dict[str, Any]:
		result: dict[str, Any] = {"id": self.code, "message": self.message}
		if self.path is not None:
			result["path"] = self.path
		if self.details is not None:
			result["details"] = self.details
		return result


def _failure(
	message: StageError,
	*,
	source_root: str | None,
	runtime_directory: str | None,
	destination: str | None,
	destination_created: bool = False,
) -> dict[str, Any]:
	return {
		"schema": STAGE_SCHEMA,
		"version": STAGE_VERSION,
		"ok": False,
		"sourceRoot": source_root,
		"runtimeSource": runtime_directory,
		"destination": destination,
		"destinationCreated": destination_created,
		"releaseManifest": None,
		"audit": None,
		"errors": [message.diagnostic()],
	}


def _explicit_directory(value: str | os.PathLike[str], label: str) -> Path:
	requested = Path(value)
	if requested.is_symlink():
		raise StageError("symlink-input", f"{label} must not be a symlink", path=str(requested))
	try:
		resolved = requested.resolve(strict=True)
	except OSError as error:
		raise StageError("missing-input", f"{label} is unavailable", path=str(requested),
			details=str(error)) from error
	if not resolved.is_dir():
		raise StageError("invalid-input", f"{label} must be an explicit directory", path=str(resolved))
	if resolved.parent == resolved:
		raise StageError("unsafe-input", f"refusing filesystem root as {label}", path=str(resolved))
	return resolved


def _destination_path(value: str | os.PathLike[str]) -> tuple[Path, bool]:
	requested = Path(os.path.abspath(os.fspath(value)))
	if requested.exists() or requested.is_symlink():
		if requested.is_symlink():
			raise StageError("symlink-destination", "destination must not be a symlink",
				path=str(requested))
		if not requested.is_dir():
			raise StageError("invalid-destination", "destination must be a directory",
				path=str(requested))
		if any(requested.iterdir()):
			raise StageError("nonempty-destination",
				"destination must be nonexistent or completely empty", path=str(requested))
		return requested.resolve(strict=True), False

	parent = requested.parent
	if not parent.exists() or not parent.is_dir():
		raise StageError("missing-destination-parent",
			"destination parent must already exist", path=str(parent))
	if parent.is_symlink():
		raise StageError("symlink-destination-parent",
			"destination parent must not be a symlink", path=str(parent))
	resolved_parent = parent.resolve(strict=True)
	if resolved_parent.parent == resolved_parent:
		# A child of a filesystem root is fine; the destination itself may not be
		# the root, which is already guaranteed because it does not yet exist.
		pass
	return resolved_parent / requested.name, True


def _ensure_separate_paths(source_root: Path, runtime: Path, destination: Path) -> None:
	for label, source in (("source root", source_root), ("runtime directory", runtime)):
		if destination == source:
			raise StageError("overlapping-destination", f"destination equals {label}", path=str(destination))
		if destination.is_relative_to(source):
			raise StageError("overlapping-destination", f"destination is inside {label}",
				path=str(destination))
		if source.is_relative_to(destination):
			raise StageError("overlapping-destination", f"destination is an ancestor of {label}",
				path=str(destination))


def _resolve_selected_file(root: Path, relative: str, label: str) -> Path:
	parts = PurePosixPath(relative).parts
	current = root
	for part in parts:
		current = current / part
		if current.is_symlink():
			raise StageError("symlink-source", f"{label} path contains a symlink",
				path=relative)
	try:
		resolved = current.resolve(strict=True)
	except OSError as error:
		raise StageError("missing-source", f"required {label} is unavailable", path=relative,
			details=str(error)) from error
	if not resolved.is_relative_to(root):
		raise StageError("escaping-source", f"required {label} escapes its explicit input root",
			path=relative)
	try:
		mode = resolved.stat().st_mode
	except OSError as error:
		raise StageError("unreadable-source", f"required {label} cannot be inspected", path=relative,
			details=str(error)) from error
	if not stat.S_ISREG(mode):
		raise StageError("non-regular-source", f"required {label} must be a regular file",
			path=relative)
	if resolved.stat().st_size == 0:
		raise StageError("empty-source", f"required {label} must be nonempty", path=relative)
	return resolved


def _scan_runtime(runtime: Path) -> None:
	directory_children: dict[str, set[str]] = {}
	for directory, dirnames, filenames in os.walk(runtime, topdown=True, followlinks=False):
		dirnames.sort(key=str.casefold)
		filenames.sort(key=str.casefold)
		directory_path = Path(directory)
		parent = directory_path.relative_to(runtime).as_posix()
		children = directory_children.setdefault(parent, set())
		kept: list[str] = []
		for name in dirnames:
			path = directory_path / name
			relative = path.relative_to(runtime).as_posix()
			children.add(name.casefold())
			if path.is_symlink():
				raise StageError("symlink-runtime", "runtime directory contains a symlink",
					path=relative)
			kept.append(name)
		dirnames[:] = kept
		for name in filenames:
			path = directory_path / name
			relative = path.relative_to(runtime).as_posix()
			if path.is_symlink():
				raise StageError("symlink-runtime", "runtime directory contains a symlink",
					path=relative)
			parts = {part.casefold() for part in PurePosixPath(relative).parts}
			if parts.intersection(FORBIDDEN_PATH_COMPONENTS):
				raise StageError("commercial-runtime-path",
					"runtime input contains an imported/commercial-data path", path=relative)
			if path.suffix.casefold() in FORBIDDEN_EXTENSIONS:
				raise StageError("commercial-runtime-extension",
					"runtime input contains a forbidden release extension", path=relative)
	for parent, children in sorted(directory_children.items()):
		matched = sorted(children.intersection(UT_LAYOUT_DIRECTORIES))
		if len(matched) >= 3:
			raise StageError("commercial-runtime-layout",
				"runtime input resembles an unpacked Unreal Tournament installation",
				path=parent or ".", details=matched)


def _read_bytes(path: Path, relative: str) -> bytes:
	try:
		return path.read_bytes()
	except OSError as error:
		raise StageError("unreadable-source", "source file cannot be read", path=relative,
			details=str(error)) from error


def _validate_runtime(runtime_files: dict[str, Path]) -> None:
	javascript_path = runtime_files["SurrealEngine.js"]
	wasm_path = runtime_files["SurrealEngine.wasm"]
	javascript_bytes = _read_bytes(javascript_path, "SurrealEngine.js")
	try:
		javascript = javascript_bytes.decode("utf-8")
	except UnicodeDecodeError as error:
		raise StageError("runtime-utf8", "SurrealEngine.js must be valid UTF-8",
			path="SurrealEngine.js", details=str(error)) from error
	markers = [marker for marker in PRELOAD_JS_MARKERS if marker in javascript]
	if markers:
		raise StageError("runtime-preload-loader",
			"runtime JavaScript contains an Emscripten data-package loader",
			path="SurrealEngine.js", details=markers)
	if _read_bytes(wasm_path, "SurrealEngine.wasm")[:4] != b"\x00asm":
		raise StageError("wasm-signature", "SurrealEngine.wasm has an invalid signature",
			path="SurrealEngine.wasm")


def _validate_shell_graph(sources: dict[str, Path]) -> None:
	service_worker_relative = "web/service-worker.js"
	service_worker_bytes = _read_bytes(sources[service_worker_relative], service_worker_relative)
	try:
		service_worker = service_worker_bytes.decode("utf-8")
	except UnicodeDecodeError as error:
		raise StageError("shell-utf8", "service worker must be valid UTF-8",
			path=service_worker_relative, details=str(error)) from error
	match = re.search(r"const\s+SHELL_URLS\s*=\s*\[(.*?)\]\s*\.map",
		service_worker, re.DOTALL)
	if not match:
		raise StageError("shell-allowlist", "service worker must declare SHELL_URLS",
			path=service_worker_relative)
	references: list[str] = []
	for literal in re.findall(r'"(?:[^"\\]|\\.)*"', match.group(1)):
		try:
			references.append(json.loads(literal))
		except json.JSONDecodeError as error:
			raise StageError("shell-allowlist", "service-worker allowlist has invalid JSON strings",
				path=service_worker_relative, details=literal) from error
	expected = []
	for relative in SHELL_ALLOWLIST:
		if relative == service_worker_relative:
			continue
		expected.append("./" + PurePosixPath(relative).name
			if relative.startswith("web/") else "../" + relative)
	if len(references) != len(set(references)) or set(references) != set(expected):
		raise StageError("shell-allowlist",
			"service-worker shell allowlist must exactly match the staged shell assets",
			path=service_worker_relative,
			details={
				"missing": sorted(set(expected) - set(references)),
				"unexpected": sorted(set(references) - set(expected)),
				"duplicates": len(references) - len(set(references)),
			})

	index_relative = "web/index_webxr.html"
	index_bytes = _read_bytes(sources[index_relative], index_relative)
	try:
		index = index_bytes.decode("utf-8")
	except UnicodeDecodeError as error:
		raise StageError("shell-utf8", "launcher HTML must be valid UTF-8",
			path=index_relative, details=str(error)) from error
	scripts = set(re.findall(r'<script\s+[^>]*src=["\']([^"\']+)["\']', index, re.IGNORECASE))
	required_scripts = {
		"webxr_session.js",
		"ut99_importer.js",
		"mutable_persistence.js",
		"webxr_settings.js",
		"pwa_register.js",
	}
	if not required_scripts.issubset(scripts):
		raise StageError("launcher-scripts",
			"launcher must reference every staged runtime bridge/settings script",
			path=index_relative, details=sorted(required_scripts - scripts))


def _sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def _entry(relative: str, source: Path) -> dict[str, Any]:
	return {
		"path": PurePosixPath(relative).as_posix(),
		"bytes": source.stat().st_size,
		"sha256": _sha256(source),
		"expectedMime": EXPECTED_MIME_TYPES.get(source.suffix.casefold()),
	}


def _exclusive_copy(source: Path, destination: Path) -> None:
	destination.parent.mkdir(parents=True, exist_ok=True)
	with source.open("rb") as input_stream, destination.open("xb") as output_stream:
		for chunk in iter(lambda: input_stream.read(1024 * 1024), b""):
			output_stream.write(chunk)


def _exclusive_json(destination: Path, value: dict[str, Any]) -> None:
	content = json.dumps(value, indent=2, sort_keys=True, separators=(",", ": ")) + "\n"
	with destination.open("x", encoding="utf-8", newline="\n") as stream:
		stream.write(content)


def _verify_staged_files(destination: Path, entries: list[dict[str, Any]]) -> None:
	expected_paths = {entry["path"] for entry in entries} | {RELEASE_MANIFEST}
	actual_paths = {
		path.relative_to(destination).as_posix()
		for path in destination.rglob("*") if path.is_file()
	}
	if actual_paths != expected_paths:
		raise StageError("staged-file-set", "staged release does not match the exact declarative file set",
			path=str(destination), details={
				"missing": sorted(expected_paths - actual_paths),
				"unexpected": sorted(actual_paths - expected_paths),
			})
	for entry in entries:
		path = destination.joinpath(*PurePosixPath(entry["path"]).parts)
		if path.stat().st_size != entry["bytes"] or _sha256(path) != entry["sha256"]:
			raise StageError("staged-file-integrity",
				"staged file does not match its deterministic size/hash manifest",
				path=entry["path"])


def stage_release(
	source_root_value: str | os.PathLike[str],
	runtime_directory_value: str | os.PathLike[str],
	destination_value: str | os.PathLike[str],
) -> dict[str, Any]:
	source_text = os.fspath(source_root_value)
	runtime_text = os.fspath(runtime_directory_value)
	destination_text = os.fspath(destination_value)
	destination_created = False
	try:
		source_root = _explicit_directory(source_root_value, "source root")
		runtime = _explicit_directory(runtime_directory_value, "runtime directory")
		if runtime.name not in RUNTIME_DIRECTORIES:
			raise StageError("runtime-layout",
				"runtime directory name must be build-emscripten-nodata or dist", path=str(runtime))
		destination, destination_was_missing = _destination_path(destination_value)
		_ensure_separate_paths(source_root, runtime, destination)

		sources: dict[str, Path] = {}
		for relative in SHELL_ALLOWLIST:
			sources[relative] = _resolve_selected_file(source_root, relative, "shell asset")
		_validate_shell_graph(sources)
		_scan_runtime(runtime)
		runtime_sources = {
			name: _resolve_selected_file(runtime, name, "runtime asset") for name in RUNTIME_FILES
		}
		_validate_runtime(runtime_sources)
		for name, path in runtime_sources.items():
			sources[f"{runtime.name}/{name}"] = path

		entries = [_entry(relative, path) for relative, path in sorted(sources.items())]
		release_manifest = {
			"schema": STAGE_SCHEMA,
			"version": STAGE_VERSION,
			"runtimeDirectory": runtime.name,
			"files": entries,
		}

		if destination_was_missing:
			destination.mkdir()
			destination_created = True
		for relative, source in sorted(sources.items()):
			target = destination.joinpath(*PurePosixPath(relative).parts)
			_exclusive_copy(source, target)
		_exclusive_json(destination / RELEASE_MANIFEST, release_manifest)
		_verify_staged_files(destination, entries)

		audit = audit_release(destination)
		if not audit["ok"]:
			error = StageError("post-stage-audit",
				"staged release failed the independent release audit",
				path=str(destination), details=audit["errors"])
			return {
				"schema": STAGE_SCHEMA,
				"version": STAGE_VERSION,
				"ok": False,
				"sourceRoot": str(source_root),
				"runtimeSource": str(runtime),
				"destination": str(destination),
				"destinationCreated": destination_created,
				"releaseManifest": release_manifest,
				"audit": audit,
				"errors": [error.diagnostic()],
			}
		return {
			"schema": STAGE_SCHEMA,
			"version": STAGE_VERSION,
			"ok": True,
			"sourceRoot": str(source_root),
			"runtimeSource": str(runtime),
			"destination": str(destination),
			"destinationCreated": destination_created,
			"releaseManifest": release_manifest,
			"audit": {
				"schema": audit["schema"],
				"ok": audit["ok"],
				"runtimeDirectory": audit["runtimeDirectory"],
				"summary": audit["summary"],
			},
			"errors": [],
		}
	except StageError as error:
		return _failure(error, source_root=source_text, runtime_directory=runtime_text,
			destination=destination_text, destination_created=destination_created)
	except OSError as error:
		return _failure(StageError("stage-write", "could not create staged release",
			path=destination_text, details=str(error)), source_root=source_text,
			runtime_directory=runtime_text, destination=destination_text,
			destination_created=destination_created)


def main(argv: list[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source-root", required=True,
		help="explicit source tree containing the allowlisted web shell")
	parser.add_argument("--runtime-directory", required=True,
		help="explicit build-emscripten-nodata or dist directory containing JS/Wasm")
	parser.add_argument("--destination", required=True,
		help="nonexistent or completely empty release directory")
	parser.add_argument("--pretty", action="store_true", help="indent JSON diagnostics")
	args = parser.parse_args(argv)
	result = stage_release(args.source_root, args.runtime_directory, args.destination)
	json.dump(result, sys.stdout, indent=2 if args.pretty else None, sort_keys=True)
	sys.stdout.write("\n")
	return 0 if result["ok"] else 1


if __name__ == "__main__":
	raise SystemExit(main())
