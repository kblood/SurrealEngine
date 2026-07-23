"""M6 verification: the full-body VR avatar pipeline (auto-rig -> IK -> CPU
skin -> DrawGouraudPolygon) against real UT99 data on the Emscripten/WebGPU
build. See Docs/FULLBODY_VR_AVATAR_PLAN.md, milestone M6.

Requires a build configured with -DSURREAL_GAMEDATA_DIR=<UT99 GOTY install>
served at build-emscripten-m6/ next to this repo's web/ directory, and the
dev server (web/serve.mjs) already running.
"""

import os
import re
import sys
import time
from playwright.sync_api import sync_playwright

sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")

BASE_URL = os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/")
URL = f"{BASE_URL}/web/index_webgpu_avatar_m6.html"

with sync_playwright() as p:
	browser = p.chromium.launch(channel="chrome")
	page = browser.new_page()

	console_lines = []
	page.on("console", lambda msg: console_lines.append(msg.text))
	page_errors = []
	page.on("pageerror", lambda exc: page_errors.append(str(exc)))

	page.goto(URL, wait_until="load")

	deadline = time.time() + 180
	booted = False
	while time.time() < deadline:
		booted = page.evaluate("window.surrealBooted === true")
		crashed = page.evaluate("window.surrealCrashed")
		if crashed:
			print(f"CRASHED during boot: {crashed}")
			print("--- surrealLog ---")
			print("\n".join(page.evaluate("window.surrealLog") or []))
			sys.exit(1)
		if booted:
			break
		time.sleep(1)

	if not booted:
		print("TIMEOUT waiting for boot (onRuntimeInitialized never fired)")
		print("\n".join(console_lines[-50:]))
		sys.exit(1)

	print("[harness] booted, waiting for map load + avatar diagnostics...")
	deadline = time.time() + 60
	log_text = ""
	while time.time() < deadline:
		crashed = page.evaluate("window.surrealCrashed")
		if crashed:
			print(f"CRASHED after boot: {crashed}")
			print("\n".join(page.evaluate("window.surrealLog") or []))
			sys.exit(1)
		log_text = "\n".join(page.evaluate("window.surrealLog") or [])
		if "RunMapLoadDiagnostics checked" in log_text:
			break
		time.sleep(1)

	print("[harness] letting the avatar draw run for 15s to collect IK/skin evidence...")
	time.sleep(15)
	log_text = "\n".join(page.evaluate("window.surrealLog") or [])

	print("--- last 120 log lines ---")
	print("\n".join(log_text.splitlines()[-120:]))

	failures = []

	if "AvatarAutoRig: mesh=" not in log_text:
		failures.append("no AvatarAutoRig diagnostic line found - auto-rig never ran")
	if "AvatarAutoRig: mesh=" in log_text and " valid=1" not in log_text:
		failures.append("no rig came back valid=1 - auto-rig never produced a usable rig")
	if "joint[" not in log_text:
		failures.append("no joint[] lines found - rig has no joints logged")
	if "bind-pose self-check" not in log_text:
		failures.append("bind-pose self-check never ran")
	match = re.search(r"bind-pose self-check: verticesChecked=(\d+) maxError=([\d.eE+-]+)", log_text)
	if match:
		checked, max_error = int(match.group(1)), float(match.group(2))
		if checked <= 0:
			failures.append("bind-pose self-check checked 0 vertices")
		if max_error > 0.01:
			failures.append(f"bind-pose self-check maxError={max_error} exceeds tolerance")
	else:
		failures.append("could not parse bind-pose self-check line")

	if "AvatarIK: frame" not in log_text:
		failures.append("no AvatarIK frame lines found - IK solver never logged solved joints")

	gouraud_count = page.evaluate("window.surrealGetWebGPUGouraudPolygons ? window.surrealGetWebGPUGouraudPolygons() : -1")
	print(f"[harness] WebGPU DrawGouraudPolygon call count: {gouraud_count}")
	if gouraud_count <= 0:
		failures.append(f"DrawGouraudPolygon call count is {gouraud_count}, expected > 0 (avatar mesh never submitted a triangle)")

	error_count = page.evaluate("window.surrealGetWebGPUErrorCount()")
	print(f"[harness] WebGPU error count: {error_count}")
	if error_count != 0:
		failures.append(f"{error_count} WebGPU uncaptured error(s) during the run")

	if page_errors:
		failures.append(f"unhandled page error(s): {page_errors}")

	tick = page.evaluate("window.surrealGetTickCount()")
	print(f"[harness] tick count: {tick}")
	if tick <= 0:
		failures.append("tick counter did not advance")

	page.evaluate("window.surrealRequestQuit()")
	time.sleep(1)

	if failures:
		print("FAIL:")
		for f in failures:
			print(f"  - {f}")
		sys.exit(1)

	print("PASS: auto-rig produced a valid rig, IK solved real joints, "
		f"{gouraud_count} Gouraud polygons drawn, zero WebGPU/page errors")
	browser.close()
