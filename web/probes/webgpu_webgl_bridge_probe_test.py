import json
import os
import sys
import time

from playwright.sync_api import sync_playwright


base_url = os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/")
url = f"{base_url}/web/probes/webgpu_webgl_bridge_probe.html"

with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome")
	page = browser.new_page()
	console_lines = []
	page_errors = []
	page.on("console", lambda message: console_lines.append(message.text))
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(url, wait_until="load")
	deadline = time.time() + 30
	result = None
	while time.time() < deadline:
		result = page.evaluate("window.bridgeProbeResult ?? null")
		if result is not None:
			break
		time.sleep(0.1)
	browser.close()

if result is None:
	print("FAIL: bridge probe timed out")
	print("console:", json.dumps(console_lines, indent=2))
	print("page errors:", json.dumps(page_errors, indent=2))
	sys.exit(1)

print(json.dumps(result, indent=2))
if not result.get("supported"):
	print("FAIL: cross-API bridge is unsupported in this browser")
	sys.exit(1)
if not result.get("valid") or result.get("glError") != 0 or not result.get("projectionConversionValid"):
	print("FAIL: uploaded atlas did not retain the expected eye colors")
	sys.exit(1)
