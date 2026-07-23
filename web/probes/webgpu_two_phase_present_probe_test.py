import json
import os
import sys
import time

from playwright.sync_api import sync_playwright


base_url = os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/")
url = f"{base_url}/web/probes/webgpu_two_phase_present_probe.html"

with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	page = browser.new_page()
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(url, wait_until="load")
	deadline = time.time() + 30
	result = None
	while time.time() < deadline:
		result = page.evaluate("window.twoPhaseProbeResult ?? null")
		if result is not None:
			break
		time.sleep(0.1)
	progress = page.evaluate("window.twoPhaseProbeProgress ?? null")
	browser.close()

print(json.dumps({"result": result, "progress": progress, "pageErrors": page_errors}, indent=2))
if result is None:
	print("FAIL: two-phase WebGPU probe timed out")
	sys.exit(1)
if not result.get("supported"):
	print("FAIL: WebGPU two-phase probe could not run")
	sys.exit(1)
if not result.get("valid"):
	print("FAIL: persistent front/back presentation invariants did not hold")
	sys.exit(1)
if page_errors:
	print("FAIL: browser page errors occurred")
	sys.exit(1)

print("PASS: suspended producer kept an immutable front and late-presented without Wasm reentry")
