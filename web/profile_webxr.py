#!/usr/bin/env python3
"""Collect a repeatable desktop Chrome/IWER WebXR performance profile."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import parse_qsl, urlencode, urljoin, urlparse

from webxr_performance import (
	ProfileValidationError,
	REPORT_SCHEMA,
	REPORT_VERSION,
	build_report,
	human_summary,
)


HERE = Path(__file__).resolve().parent
SOURCE_ROOT = HERE.parent
IWER_PATH = HERE / "node_modules" / "iwer" / "build" / "iwer.min.js"
PROBE_PATH = HERE / "webxr_performance_probe.js"
DEFAULT_MAP = "DM-Deck16]["
DEFAULT_BUILD = "build-emscripten"
DEFAULT_BASE_URL = "http://localhost:8091/"

IWER_INSTALL = r"""
;(function () {
	const device = new IWER.XRDevice(IWER.metaQuest3);
	device.installRuntime({ forceInstall: true });
	device.stereoEnabled = true;
	window.__surrealProfileIWERDevice = device;
})();
"""

SAMPLE_SCRIPT = r"""
() => {
	function call(name) {
		try { return Module.ccall(name, "number", [], []); }
		catch (_) { return null; }
	}
	const memory = performance.memory || null;
	return {
		elapsedMs: performance.now(),
		engineTicks: window.surrealGetTickCount ? window.surrealGetTickCount() : null,
		xrFrames: window.surrealXRFrameCount,
		webgpuErrors: window.surrealGetWebGPUErrorCount ? window.surrealGetWebGPUErrorCount() : null,
		drawCalls: window.surrealGetWebGPUDrawCalls ? window.surrealGetWebGPUDrawCalls() : null,
		textureCount: window.surrealGetWebGPUTextureCount ? window.surrealGetWebGPUTextureCount() : null,
		bindGroupsCreated: window.surrealGetWebGPUBindGroupsCreated ? window.surrealGetWebGPUBindGroupsCreated() : null,
		bindGroupCacheHits: window.surrealGetWebGPUBindGroupCacheHits ? window.surrealGetWebGPUBindGroupCacheHits() : null,
		bufferRollovers: window.surrealGetWebGPUBufferRollovers ? window.surrealGetWebGPUBufferRollovers() : null,
		weaponExpectedEyes: call("Surreal_GetWebXRWeaponOverlayExpectedEyePasses"),
		weaponEyePasses: call("Surreal_GetWebXRWeaponOverlayEyePasses"),
		weaponCalls: call("Surreal_GetWebXRWeaponOverlayCalls"),
		weaponVisualDrawScopes: call("Surreal_GetWebXRWeaponVisualDrawScopeCount"),
		weaponVisualDrawRestores: call("Surreal_GetWebXRWeaponVisualDrawRestoreCount"),
		weaponVisualZeroFallbacks: call("Surreal_GetWebXRWeaponVisualZeroFallbackCount"),
		weaponVisualCalibratedOffsets: call("Surreal_GetWebXRWeaponVisualCalibratedOffsetCount"),
		weaponVisualRejectedTransforms: call("Surreal_GetWebXRWeaponVisualRejectedTransformCount"),
		hudExpectedEyes: call("Surreal_GetWebXRHudExpectedEyePresentations"),
		hudStateUpdates: call("Surreal_GetWebXRHudStateUpdates"),
		hudEyePresentations: call("Surreal_GetWebXRHudEyePresentations"),
		hudCapturedCommands: call("Surreal_GetWebXRHudCapturedCommands"),
		hudUnsupportedDraws: call("Surreal_GetWebXRHudUnsupportedDraws"),
		hudClampedViewports: call("Surreal_GetWebXRHudClampedViewports"),
		hudPlayerPostRenderCalls: call("Surreal_GetWebXRHudPlayerPostRenderCalls"),
		hudConsolePostRenderCalls: call("Surreal_GetWebXRHudConsolePostRenderCalls"),
		wasmHeapBytes: window.surrealWebXRPerformanceProbe.memory().wasmHeapBytes,
		jsHeapUsedBytes: memory ? memory.usedJSHeapSize : null,
		jsHeapTotalBytes: memory ? memory.totalJSHeapSize : null,
	};
}
"""


def parse_args(argv: list[str]) -> argparse.Namespace:
	parser = argparse.ArgumentParser(
		description=("Profile the data-backed SurrealEngine build in desktop Chrome "
			"with an IWER lifecycle-only XR session (never a Quest qualification)."))
	parser.add_argument("--map", default=DEFAULT_MAP, help="UT99 map name passed to --url")
	parser.add_argument("--build", default=DEFAULT_BUILD, help="Build directory URL/path name")
	parser.add_argument("--output", type=Path, default=None, help="Output JSON path")
	parser.add_argument("--duration", type=float, default=30.0, help="Measured seconds")
	parser.add_argument("--warmup", type=float, default=10.0, help="Warmup seconds before capture")
	parser.add_argument("--sample-interval", type=float, default=0.25, help="Native-counter sample interval")
	parser.add_argument("--base-url", default=DEFAULT_BASE_URL, help="HTTP(S) server root")
	parser.add_argument("--query", action="append", default=[], metavar="KEY=VALUE",
		help="Additional launcher query pair; repeat as needed")
	parser.add_argument("--browser-channel", default="chrome", help="Playwright Chromium channel")
	parser.add_argument("--headed", action="store_true", help="Show the desktop Chrome window")
	parser.add_argument("--boot-timeout", type=float, default=180.0, help="Engine boot timeout seconds")
	args = parser.parse_args(argv)
	for label in ("duration", "warmup", "sample_interval", "boot_timeout"):
		value = getattr(args, label)
		if not isinstance(value, float) or not math_is_finite(value):
			parser.error(f"--{label.replace('_', '-')} must be finite")
	if args.duration <= 0:
		parser.error("--duration must be positive")
	if args.warmup < 0:
		parser.error("--warmup must be nonnegative")
	if args.sample_interval <= 0 or args.sample_interval > args.duration / 2:
		parser.error("--sample-interval must be positive and at most half --duration")
	if args.boot_timeout <= 0:
		parser.error("--boot-timeout must be positive")
	if not args.map.strip() or not args.build.strip():
		parser.error("--map and --build cannot be empty")
	return args


def math_is_finite(value: float) -> bool:
	return value == value and value not in (float("inf"), float("-inf"))


def parse_extra_query(items: list[str]) -> list[tuple[str, str]]:
	result = []
	for item in items:
		if "=" not in item:
			raise ProfileValidationError(f"--query must be KEY=VALUE, got {item!r}")
		key, value = item.split("=", 1)
		if not key or key in {"map", "build", "native-webgpu-xr"}:
			raise ProfileValidationError(f"reserved or empty --query key: {key!r}")
		result.append((key, value))
	return result


def sha256_file(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as stream:
		for block in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(block)
	return digest.hexdigest()


def build_metadata(build_name: str) -> dict:
	build_path = (SOURCE_ROOT / build_name).resolve()
	try:
		build_path.relative_to(SOURCE_ROOT.resolve())
	except ValueError as error:
		raise ProfileValidationError("--build must resolve inside the source tree") from error
	assets = {}
	for suffix in ("js", "wasm", "data"):
		path = build_path / f"SurrealEngine.{suffix}"
		if path.is_file():
			assets[path.name] = {"bytes": path.stat().st_size, "sha256": sha256_file(path)}
	if "SurrealEngine.js" not in assets or "SurrealEngine.wasm" not in assets:
		raise ProfileValidationError(f"build is missing SurrealEngine.js/.wasm: {build_path}")
	return {
		"name": build_name,
		"sourceRelativePath": build_path.relative_to(SOURCE_ROOT).as_posix(),
		"dataPreloadPresent": "SurrealEngine.data" in assets,
		"assets": assets,
	}


def git_metadata() -> dict:
	def command(*parts: str) -> str | None:
		try:
			return subprocess.check_output(
				["git", *parts], cwd=SOURCE_ROOT, text=True, stderr=subprocess.DEVNULL).strip()
		except (OSError, subprocess.CalledProcessError):
			return None
	commit = command("rev-parse", "HEAD")
	status = command("status", "--porcelain", "--untracked-files=no")
	return {"commit": commit, "trackedWorktreeDirty": bool(status) if status is not None else None}


def machine_metadata() -> dict:
	return {
		"operatingSystem": platform.platform(),
		"architecture": platform.machine(),
		"processor": platform.processor() or None,
		"logicalCpuCount": os.cpu_count(),
		"python": platform.python_version(),
	}


def wait_for(page, expression: str, timeout_seconds: float, failure_label: str) -> None:
	deadline = time.monotonic() + timeout_seconds
	while time.monotonic() < deadline:
		if page.evaluate(expression):
			return
		crash = page.evaluate("window.surrealCrashed || null")
		if crash:
			raise ProfileValidationError(f"engine crashed while waiting for {failure_label}: {crash}")
		time.sleep(0.1)
	raise ProfileValidationError(f"timeout waiting for {failure_label}")


def collect(args: argparse.Namespace) -> dict:
	try:
		from playwright.sync_api import sync_playwright
	except ImportError as error:
		raise ProfileValidationError("Playwright is not installed for this Python environment") from error
	if not IWER_PATH.is_file():
		raise ProfileValidationError(f"IWER is missing at {IWER_PATH}; run npm install in web/")
	if not PROBE_PATH.is_file():
		raise ProfileValidationError(f"performance probe is missing at {PROBE_PATH}")

	extra_query = parse_extra_query(args.query)
	query = [("map", args.map), ("build", args.build), *extra_query]
	base = args.base_url.rstrip("/") + "/"
	url = urljoin(base, "web/index_webxr.html") + "?" + urlencode(query)
	parsed_query = dict(parse_qsl(urlparse(url).query, keep_blank_values=True))
	metadata = {
		"machine": machine_metadata(),
		"git": git_metadata(),
		"browser": {},
		"build": build_metadata(args.build),
		"map": args.map,
		"launcherUrl": url,
		"query": parsed_query,
		"dataBoot": None,
	}

	console_lines: list[str] = []
	page_errors: list[str] = []
	with sync_playwright() as playwright:
		browser = playwright.chromium.launch(channel=args.browser_channel, headless=not args.headed)
		metadata["browser"]["version"] = browser.version
		metadata["browser"]["channel"] = args.browser_channel
		metadata["browser"]["headless"] = not args.headed
		page = browser.new_page()
		page.on("console", lambda message: console_lines.append(f"{message.type}: {message.text}"))
		page.on("pageerror", lambda error: page_errors.append(str(error)))
		init_source = (IWER_PATH.read_text(encoding="utf-8") + IWER_INSTALL +
			PROBE_PATH.read_text(encoding="utf-8"))
		page.add_init_script(init_source)
		try:
			page.goto(url, wait_until="load", timeout=int(args.boot_timeout * 1000))
			wait_for(page, "window.surrealBooted === true", args.boot_timeout, "engine boot")
			metadata["browser"].update(page.evaluate(r"""() => ({
				reportedUserAgent: navigator.userAgent,
				reportedUserAgentAlteredByIWER: true,
				platform: navigator.platform,
				hardwareConcurrency: navigator.hardwareConcurrency || null,
				deviceMemoryGiB: navigator.deviceMemory || null,
				crossOriginIsolated: globalThis.crossOriginIsolated === true,
				secureContext: globalThis.isSecureContext === true,
				webgpu: !!navigator.gpu,
				webxr: !!navigator.xr,
			})"""))
			metadata["dataBoot"] = page.evaluate("window.surrealUT99DataBootResult || null")

			readiness = page.evaluate("window.surrealGetWebXRLaunchReadiness()")
			if readiness.get("mode") != "lifecycle-only" or not readiness.get("canAttempt"):
				raise ProfileValidationError(f"IWER lifecycle-only entry is not ready: {readiness}")
			if page.evaluate("window.surrealWebXRPerformanceProbe.patchXRSessionRAF()") is not True:
				raise ProfileValidationError("could not instrument IWER XRSession.requestAnimationFrame")

			page.evaluate("document.exitPointerLock()")
			page.click("#entervr")
			wait_for(page, "window.surrealXRSessionActive === true", 20.0, "IWER immersive session")
			if page.evaluate("window.surrealGetWebAudioState()") != 2:
				raise ProfileValidationError("Web Audio did not reach running state after the XR gesture")

			print(f"Warmup: {args.warmup:.2f}s on {args.map} ({args.build})")
			warmup_deadline = time.monotonic() + args.warmup
			while time.monotonic() < warmup_deadline:
				if page.evaluate("window.surrealCrashed || window.surrealXRError"):
					raise ProfileValidationError("runtime error during warmup")
				time.sleep(min(0.25, max(0.0, warmup_deadline - time.monotonic())))

			page.evaluate("window.surrealWebXRPerformanceProbe.start()")
			samples = [page.evaluate(SAMPLE_SCRIPT)]
			capture_start = time.monotonic()
			capture_deadline = capture_start + args.duration
			next_sample = capture_start + args.sample_interval
			while time.monotonic() < capture_deadline:
				delay = min(next_sample, capture_deadline) - time.monotonic()
				if delay > 0:
					time.sleep(min(delay, 0.25))
					continue
				samples.append(page.evaluate(SAMPLE_SCRIPT))
				next_sample += args.sample_interval
			samples.append(page.evaluate(SAMPLE_SCRIPT))
			print(f"Captured {len(samples)} counter samples over {time.monotonic() - capture_start:.2f}s")
			probe = page.evaluate("window.surrealWebXRPerformanceProbe.stop()")
			lifecycle = page.evaluate(r"""() => ({
				lifecycleOnly: window.surrealGetWebXRLaunchReadiness().lifecycleOnly,
				sessionActive: window.surrealXRSessionActive === true,
				xrFrameCount: window.surrealXRFrameCount,
				readiness: window.surrealGetWebXRLaunchReadiness(),
				nativeDiagnostics: window.surrealXRNativeDiagnostics,
				state: window.surrealXRLifecycle,
				xrLog: window.surrealXRLog,
			})""")
			errors = {
				"engineCrash": page.evaluate("window.surrealCrashed || null"),
				"xrError": page.evaluate("window.surrealXRError || null"),
				"pageErrors": page_errors,
				"consoleWebGPUErrorLines": [
					line for line in console_lines
					if "webgpu" in line.lower() and ("error" in line.lower() or "validation" in line.lower())
				],
			}
			report = build_report(
				created_at=datetime.now(timezone.utc).isoformat(),
				metadata=metadata,
				timing={"warmupSeconds": args.warmup, "sampleSeconds": args.duration},
				samples=samples,
				probe=probe,
				lifecycle=lifecycle,
				errors=errors,
			)
			page.evaluate("window.surrealXRExit()")
			return report
		finally:
			browser.close()


def write_outputs(report: dict, output: Path) -> tuple[Path, Path]:
	output = output.resolve()
	output.parent.mkdir(parents=True, exist_ok=True)
	summary_path = output.with_suffix(".txt")
	output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	summary_path.write_text(human_summary(report), encoding="utf-8")
	return output, summary_path


def main(argv: list[str] | None = None) -> int:
	args = parse_args(argv if argv is not None else sys.argv[1:])
	if args.output is None:
		stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
		args.output = HERE / "profiles" / f"webxr-desktop-iwer-{stamp}.json"
	try:
		report = collect(args)
		json_path, text_path = write_outputs(report, args.output)
		summary = human_summary(report)
		print(summary, end="")
		print(f"JSON: {json_path}")
		print(f"Summary: {text_path}")
		return 0
	except Exception as error:
		failure = {
			"schema": REPORT_SCHEMA,
			"version": REPORT_VERSION,
			"createdAt": datetime.now(timezone.utc).isoformat(),
			"result": "fail",
			"environment": {
				"kind": "desktop-chrome-iwer",
				"headset": False,
				"questQualified": False,
				"label": "DESKTOP CHROME + IWER; NOT A QUEST PERFORMANCE RESULT",
			},
			"error": str(error),
		}
		output = args.output.resolve()
		output.parent.mkdir(parents=True, exist_ok=True)
		output.write_text(json.dumps(failure, indent=2, sort_keys=True) + "\n", encoding="utf-8")
		output.with_suffix(".txt").write_text(
			"SurrealEngine WebXR performance profile v1\n"
			"DESKTOP CHROME + IWER; NOT A QUEST PERFORMANCE RESULT\n"
			f"Result: FAIL\nError: {error}\n", encoding="utf-8")
		print(f"FAIL: {error}", file=sys.stderr)
		print(f"Failure report: {output}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
