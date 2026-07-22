"""Validation and aggregation for the desktop/IWER multi-map profile matrix."""

from __future__ import annotations

import hashlib
import json
import math
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from webxr_performance import REPORT_SCHEMA, REPORT_VERSION, ProfileValidationError


MATRIX_SCHEMA = "surrealengine-webxr-performance-matrix"
MATRIX_VERSION = 1
MAP_PATTERN = re.compile(r"^(DM|CTF|DOM|AS)-[A-Za-z0-9][A-Za-z0-9_\-\[\]]*$", re.ASCII)


@dataclass(frozen=True)
class MapSpec:
	name: str
	role: str


def validate_map_specs(specs: list[MapSpec], require_acceptance_set: bool = True) -> None:
	if not specs:
		raise ProfileValidationError("performance matrix has no maps")
	seen: set[str] = set()
	for spec in specs:
		if spec.role not in {"representative", "stress"}:
			raise ProfileValidationError(f"invalid map role: {spec.role!r}")
		if not MAP_PATTERN.fullmatch(spec.name):
			raise ProfileValidationError(f"unsafe or unsupported local map name: {spec.name!r}")
		folded = spec.name.casefold()
		if folded in seen:
			raise ProfileValidationError(f"duplicate map in matrix: {spec.name!r}")
		seen.add(folded)
	if require_acceptance_set:
		representatives = sum(spec.role == "representative" for spec in specs)
		stress = sum(spec.role == "stress" for spec in specs)
		if representatives < 5 or stress < 1:
			raise ProfileValidationError(
				"acceptance matrix requires at least five representative maps and one stress map")


def map_slug(name: str) -> str:
	slug = re.sub(r"[^a-z0-9]+", "-", name.casefold()).strip("-")
	if not slug:
		raise ProfileValidationError(f"map has no safe output slug: {name!r}")
	return slug


def sha256_file(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for block in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(block)
	return digest.hexdigest()


def _finite_number(value: Any, label: str) -> float:
	if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
		raise ProfileValidationError(f"{label} must be finite")
	return float(value)


def _identity(report: dict[str, Any]) -> dict[str, Any]:
	metadata = report.get("metadata") or {}
	return {
		"build": metadata.get("build"),
		"git": metadata.get("git"),
		"browser": {
			key: (metadata.get("browser") or {}).get(key)
			for key in ("version", "channel", "headless")
		},
		"machine": metadata.get("machine"),
	}


def _summary(spec: MapSpec, report: dict[str, Any], relative_path: str,
		report_sha256: str) -> dict[str, Any]:
	if report.get("schema") != REPORT_SCHEMA or report.get("version") != REPORT_VERSION:
		raise ProfileValidationError(f"{spec.name}: profile schema/version mismatch")
	if report.get("result") != "pass":
		raise ProfileValidationError(f"{spec.name}: child profile did not pass")
	environment = report.get("environment") or {}
	if environment.get("kind") != "desktop-chrome-iwer" or environment.get("headset") is not False or \
			environment.get("questQualified") is not False:
		raise ProfileValidationError(f"{spec.name}: child profile overstates its environment")
	metadata = report.get("metadata") or {}
	if metadata.get("map") != spec.name:
		raise ProfileValidationError(
			f"{spec.name}: child report map mismatch: {metadata.get('map')!r}")
	metrics = report.get("metrics") or {}
	engine = metrics.get("engine") or {}
	webgpu = metrics.get("webgpu") or {}
	last_frame = webgpu.get("lastFrameCounters") or {}
	memory = metrics.get("memory") or {}
	long_tasks = metrics.get("longTasks") or {}
	xr_intervals = ((metrics.get("frameIntervals") or {}).get("iwerXRSessionRAF") or {})
	return {
		"map": spec.name,
		"role": spec.role,
		"profile": relative_path,
		"profileSha256": report_sha256,
		"ticksPerSecond": _finite_number(engine.get("ticksPerSecond"),
			f"{spec.name}.ticksPerSecond"),
		"iwerXRFramesPerSecond": _finite_number(engine.get("iwerXRFramesPerSecond"),
			f"{spec.name}.iwerXRFramesPerSecond"),
		"drawCallsMean": _finite_number((last_frame.get("drawCalls") or {}).get("mean"),
			f"{spec.name}.drawCallsMean"),
		"drawCallsMaximum": _finite_number((last_frame.get("drawCalls") or {}).get("maximum"),
			f"{spec.name}.drawCallsMaximum"),
		"residentTexturesMaximum": _finite_number((webgpu.get("textureCount") or {}).get("maximum"),
			f"{spec.name}.residentTexturesMaximum"),
		"wasmHeapBytesMaximum": _finite_number((memory.get("wasmHeapBytes") or {}).get("maximum"),
			f"{spec.name}.wasmHeapBytesMaximum"),
		"iwerRafP95Ms": _finite_number(xr_intervals.get("p95Ms"),
			f"{spec.name}.iwerRafP95Ms"),
		"longTaskCount": int(_finite_number(long_tasks.get("count"),
			f"{spec.name}.longTaskCount")),
		"webgpuErrorsMaximum": _finite_number(webgpu.get("uncapturedErrorsMaximum"),
			f"{spec.name}.webgpuErrorsMaximum"),
	}


def build_matrix(created_at: str, specs: list[MapSpec], profiles: list[tuple[dict[str, Any],
		str, str]], require_acceptance_set: bool = True) -> dict[str, Any]:
	validate_map_specs(specs, require_acceptance_set=require_acceptance_set)
	if len(profiles) != len(specs):
		raise ProfileValidationError("map/profile count mismatch")
	summaries = [
		_summary(spec, report, path, digest)
		for spec, (report, path, digest) in zip(specs, profiles)
	]
	identities = [_identity(report) for report, _, _ in profiles]
	if any(identity != identities[0] for identity in identities[1:]):
		raise ProfileValidationError("child profiles were not collected from one comparable build/browser/machine identity")
	return {
		"schema": MATRIX_SCHEMA,
		"version": MATRIX_VERSION,
		"createdAt": created_at,
		"result": "pass",
		"environment": {
			"kind": "desktop-chrome-iwer-multi-map",
			"headset": False,
			"questQualified": False,
			"label": "DESKTOP CHROME + IWER MULTI-MAP; NOT A QUEST PERFORMANCE RESULT",
		},
		"claims": {
			"acceptanceMapCoverageAutomated": require_acceptance_set,
			"questPerformanceQualified": False,
			"gpuTimeMeasured": False,
			"thermalBehaviorMeasured": False,
			"compositorTimingMeasured": False,
		},
		"identity": identities[0],
		"coverage": {
			"representativeMaps": sum(spec.role == "representative" for spec in specs),
			"stressMaps": sum(spec.role == "stress" for spec in specs),
			"totalMaps": len(specs),
		},
		"maps": summaries,
		"limitations": [
			"Every child is a desktop Chrome/IWER lifecycle profile, not a Quest compositor run.",
			"The matrix measures neither headset GPU time nor thermal, reprojection, power, or comfort behavior.",
			"Passing this matrix does not satisfy the physical 72 Hz acceptance gate.",
		],
	}


def matrix_summary(matrix: dict[str, Any]) -> str:
	lines = [
		"SurrealEngine WebXR multi-map performance matrix v1",
		"DESKTOP CHROME + IWER MULTI-MAP; NOT A QUEST PERFORMANCE RESULT",
		f"Result: {matrix['result'].upper()}",
		("Coverage: {representativeMaps} representative + {stressMaps} stress = "
			"{totalMaps} maps").format(**matrix["coverage"]),
	]
	for item in matrix["maps"]:
		lines.append(
			f"{item['role']:14} {item['map']:20} "
			f"ticks {item['ticksPerSecond']:.2f}/s, IWER {item['iwerXRFramesPerSecond']:.2f}/s, "
			f"draws mean/max {item['drawCallsMean']:.1f}/{item['drawCallsMaximum']:.0f}, "
			f"textures {item['residentTexturesMaximum']:.0f}")
	lines.append("Physical Quest GPU/thermal/compositor/comfort qualification remains required.")
	return "\n".join(lines) + "\n"


def read_json(path: Path) -> dict[str, Any]:
	try:
		value = json.loads(path.read_text(encoding="utf-8"))
	except (OSError, json.JSONDecodeError) as error:
		raise ProfileValidationError(f"could not read child profile {path}: {error}") from error
	if not isinstance(value, dict):
		raise ProfileValidationError(f"child profile is not an object: {path}")
	return value
