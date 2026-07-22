#!/usr/bin/env python3
"""Run and record the physical Brave -> VDXR WebXR/WebGPU compatibility gate."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import platform
import sys
import time
import urllib.parse
from pathlib import Path
from typing import Any


HERE = Path(__file__).resolve().parent
SOURCE_ROOT = HERE.parent
DEFAULT_BRAVE = Path(r"C:\Program Files\BraveSoftware\Brave-Browser\Application\brave.exe")
DEFAULT_PROFILE = Path(os.environ.get("TEMP", str(SOURCE_ROOT.parent))) / "SurrealWebXR-Brave-Probe"
DEFAULT_VDXR_LOG = Path(os.environ.get("ProgramData", r"C:\ProgramData")) / "Virtual Desktop" / "OpenXR.log"
SCHEMA = "surrealengine-brave-vdxr-probe"
SCHEMA_VERSION = 1


def utc_now() -> str:
	return dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z")


def validate_base_url(value: str) -> str:
	parsed = urllib.parse.urlparse(value)
	if parsed.scheme not in {"http", "https"} or not parsed.hostname:
		raise argparse.ArgumentTypeError("base URL must be an absolute http(s) URL")
	loopback = parsed.hostname.lower() in {"localhost", "127.0.0.1", "::1"}
	if parsed.scheme != "https" and not loopback:
		raise argparse.ArgumentTypeError(
			"non-loopback WebXR must use trusted HTTPS; plain LAN HTTP is not a secure context")
	return value.rstrip("/")


def build_url(base_url: str, build: str, map_name: str) -> str:
	query = urllib.parse.urlencode({
		"build": build,
		"map": map_name,
		"native-webgpu-xr": "1",
		"orientation-fix": "1",
	})
	return f"{base_url}/web/index_webxr.html?{query}"


def native_success(native: Any, minimum_frames: int = 10) -> bool:
	return bool(
		isinstance(native, dict)
		and native.get("phase") == "running"
		and isinstance(native.get("frameCount"), (int, float))
		and native["frameCount"] >= minimum_frames
		and native.get("lastRenderSucceeded") is True
	)


def tail_text(path: Path, maximum_lines: int = 100) -> list[str]:
	try:
		with path.open("r", encoding="utf-8", errors="replace") as stream:
			return stream.read().splitlines()[-maximum_lines:]
	except OSError:
		return []


def active_openxr_runtime() -> str | None:
	if platform.system() != "Windows":
		return None
	try:
		import winreg

		with winreg.OpenKey(
			winreg.HKEY_LOCAL_MACHINE,
			r"SOFTWARE\Khronos\OpenXR\1",
			0,
			winreg.KEY_READ | getattr(winreg, "KEY_WOW64_64KEY", 0),
		) as key:
			value, _ = winreg.QueryValueEx(key, "ActiveRuntime")
			return str(value)
	except (OSError, ImportError):
		return None


def default_output() -> Path:
	stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%SZ")
	return SOURCE_ROOT.parent.parent / f"webxr-brave-vdxr-{stamp}.json"


def parser() -> argparse.ArgumentParser:
	result = argparse.ArgumentParser(
		description="Launch headed Brave with experimental WebXR/WebGPU flags and record native VDXR evidence.")
	result.add_argument("--base-url", type=validate_base_url, default="http://localhost:8091")
	result.add_argument("--build", default="build-emscripten")
	result.add_argument("--map", default="DM-Deck16][")
	result.add_argument("--brave", type=Path, default=DEFAULT_BRAVE)
	result.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
	result.add_argument("--output", type=Path, default=None)
	result.add_argument("--boot-timeout", type=float, default=180.0)
	result.add_argument("--entry-timeout", type=float, default=45.0)
	result.add_argument("--sample-seconds", type=float, default=10.0)
	result.add_argument("--minimum-frames", type=int, default=10)
	result.add_argument(
		"--preflight-only",
		action="store_true",
		help="verify Brave/API/engine readiness without requesting an immersive session")
	result.add_argument(
		"--click-without-prompt",
		action="store_true",
		help="click immediately after readiness; default waits for the operator to press Enter")
	return result


def page_snapshot(page: Any) -> dict[str, Any]:
	return page.evaluate(r"""() => ({
		url: location.href,
		userAgent: navigator.userAgent,
		secureContext: window.isSecureContext,
		xrBindingType: typeof XRGPUBinding,
		readiness: window.surrealGetWebXRLaunchReadiness?.() || null,
		probe: window.surrealXRWebGPUProbe || null,
		native: window.surrealXRNativeDiagnostics || null,
		nativeError: window.surrealXRNativeError || null,
		lifecycle: window.surrealXRLifecycle || null,
		booted: window.surrealBooted === true,
		crashed: window.surrealCrashed || null,
		log: Array.isArray(window.surrealXRLog) ? window.surrealXRLog.slice(-100) : [],
	})""")


def run(args: argparse.Namespace) -> tuple[dict[str, Any], bool]:
	try:
		from playwright.sync_api import sync_playwright
	except ImportError as error:
		raise RuntimeError("Playwright for Python is required: pip install playwright") from error

	if not args.brave.is_file():
		raise RuntimeError(f"Brave executable not found: {args.brave}")
	if args.boot_timeout <= 0 or args.entry_timeout <= 0 or args.sample_seconds < 0:
		raise RuntimeError("timeouts must be positive and sample-seconds cannot be negative")
	if args.minimum_frames < 1:
		raise RuntimeError("minimum-frames must be at least one")

	url = build_url(args.base_url, args.build, args.map)
	started = utc_now()
	openxr_runtime = active_openxr_runtime()
	vdxr_before = tail_text(DEFAULT_VDXR_LOG)
	console_lines: list[dict[str, str]] = []
	page_errors: list[str] = []
	final_snapshot: dict[str, Any] = {}
	operator_clicked = False

	with sync_playwright() as playwright:
		context = playwright.chromium.launch_persistent_context(
			user_data_dir=str(args.profile.resolve()),
			executable_path=str(args.brave.resolve()),
			headless=False,
			no_viewport=True,
			args=[
				"--enable-features=WebXRWebGPUBinding,WebXRLayers",
				"--start-maximized",
			],
		)
		try:
			page = context.pages[0] if context.pages else context.new_page()
			page.on("console", lambda message: console_lines.append({
				"type": message.type,
				"text": message.text,
			}))
			page.on("pageerror", lambda error: page_errors.append(str(error)))
			page.goto(url, wait_until="domcontentloaded", timeout=int(args.boot_timeout * 1000))
			# Early readiness intentionally reports transient GPU/engine blockers.
			# Wait for the real data-backed engine outcome before judging preflight.
			page.wait_for_function(
				"() => window.surrealBooted === true || !!window.surrealCrashed",
				timeout=int(args.boot_timeout * 1000),
			)
			initial = page_snapshot(page)
			print(json.dumps(initial.get("readiness"), indent=2))
			if args.preflight_only or not initial.get("readiness", {}).get("canAttempt"):
				final_snapshot = initial
			else:
				if not args.click_without_prompt:
					input(
						"Put on the Quest, confirm Virtual Desktop shows Runtime: VDXR, "
						"then press Enter to perform the trusted Enter Native WebGPU VR click... ")
				operator_clicked = True
				page.locator("#entervr").click(timeout=10_000)
				deadline = time.monotonic() + args.entry_timeout
				while time.monotonic() < deadline:
					final_snapshot = page_snapshot(page)
					native = final_snapshot.get("native")
					if native_success(native, args.minimum_frames):
						break
					if isinstance(native, dict) and native.get("phase") == "error":
						break
					time.sleep(0.25)
				if native_success(final_snapshot.get("native"), args.minimum_frames):
					print(f"Native WebGPU XR is running; sampling for {args.sample_seconds:.1f}s...")
					time.sleep(args.sample_seconds)
					final_snapshot = page_snapshot(page)
				if final_snapshot.get("native", {}).get("phase") == "running":
					page.evaluate("window.surrealXRExit?.()")
					time.sleep(0.5)
		finally:
			context.close()

	passed = bool(final_snapshot.get("readiness", {}).get("canAttempt")) if args.preflight_only else \
		native_success(final_snapshot.get("native"), args.minimum_frames)
	report = {
		"schema": SCHEMA,
		"version": SCHEMA_VERSION,
		"createdUTC": utc_now(),
		"startedUTC": started,
		"scope": "BRAVE NATIVE PREFLIGHT" if args.preflight_only else
			"PHYSICAL BRAVE + VDXR COMPATIBILITY EVIDENCE",
		"passed": passed,
		"launch": {
			"url": url,
			"braveExecutable": str(args.brave.resolve()),
			"profile": str(args.profile.resolve()),
			"features": ["WebXRWebGPUBinding", "WebXRLayers"],
			"operatorPrompted": not args.click_without_prompt,
			"operatorAcknowledged": operator_clicked,
		},
		"system": {
			"platform": platform.platform(),
			"activeOpenXRRuntime": openxr_runtime,
			"vdxrLogPath": str(DEFAULT_VDXR_LOG),
		},
		"acceptance": {
			"mode": "preflight-only" if args.preflight_only else "native-presentation",
			"minimumFrames": args.minimum_frames,
			"requiresPhase": "running",
			"requiresLastRenderSucceeded": True,
		},
		"page": final_snapshot,
		"browserConsole": console_lines[-200:],
		"pageErrors": page_errors,
		"vdxrLogBefore": vdxr_before,
		"vdxrLogAfter": tail_text(DEFAULT_VDXR_LOG),
	}
	return report, passed


def main(argv: list[str] | None = None) -> int:
	args = parser().parse_args(argv)
	args.output = (args.output or default_output()).resolve()
	try:
		report, passed = run(args)
	except Exception as error:
		report = {
			"schema": SCHEMA,
			"version": SCHEMA_VERSION,
			"createdUTC": utc_now(),
			"scope": "BRAVE NATIVE PREFLIGHT" if args.preflight_only else
				"PHYSICAL BRAVE + VDXR COMPATIBILITY EVIDENCE",
			"passed": False,
			"collectorError": f"{type(error).__name__}: {error}",
		}
		passed = False
	args.output.parent.mkdir(parents=True, exist_ok=True)
	args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
	print(f"{'PASS' if passed else 'FAIL'}: report written to {args.output}")
	if not passed:
		page = report.get("page", {})
		print(json.dumps({
			"readiness": page.get("readiness"),
			"probe": page.get("probe"),
			"native": page.get("native"),
			"nativeError": page.get("nativeError"),
			"collectorError": report.get("collectorError"),
		}, indent=2))
	return 0 if passed else 1


if __name__ == "__main__":
	raise SystemExit(main())
