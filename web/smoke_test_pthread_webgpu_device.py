"""Measure whether a browser-thread WebGPU device reaches the proxied main worker."""

import json
import os
import sys
import time

from playwright.sync_api import sync_playwright


base_url = next(
	(arg.split("=", 1)[1].rstrip("/") for arg in sys.argv[1:] if arg.startswith("--base-url=")),
	os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/"),
)

with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	page = browser.new_page()
	console = []
	page_errors = []
	page.on("console", lambda message: console.append(message.text))
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(base_url + "/web/pthread_webgpu_device_probe.html", wait_until="load")
	deadline = time.time() + 30
	while time.time() < deadline and not page_errors and not any(
		line.startswith(("PASS pthread-webgpu-device", "FAIL ")) for line in console
	):
		page.wait_for_timeout(25)
	browser.close()

result = {
	"workerImportedDevice": any(line == "PASS pthread-webgpu-device worker=1 device=1" for line in console),
	"pageErrors": page_errors,
	"console": [line for line in console if line.startswith(("PASS ", "FAIL ", "INFO ", "worker:"))],
}
print(json.dumps(result, indent=2))
if result["workerImportedDevice"]:
	print("PASS: browser-thread WebGPU device was available to the proxied main worker")
else:
	print("BLOCKED: emdawnwebgpu did not expose the browser-thread device in the proxied main worker")
	sys.exit(2)
