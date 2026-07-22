"""Verify the worker-owned engine can create SDL's window-only presentation seam."""

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
	page.goto(base_url + "/build-pthread-presentation/PthreadSDLPresentationProbe.html", wait_until="load")
	deadline = time.time() + 20
	while time.time() < deadline and not any(line.startswith("PASS pthread-sdl-presentation") for line in console):
		page.wait_for_timeout(25)
	browser.close()

result = {
	"passed": any(line == "PASS pthread-sdl-presentation worker=1 renderer=0 gl-context=0" for line in console),
	"pageErrors": page_errors,
	"console": [line for line in console if line.startswith(("PASS ", "FAIL ", "worker:"))],
}
print(json.dumps(result, indent=2))
if not result["passed"] or result["pageErrors"]:
	print("FAIL: pthread SDL presentation ownership", file=sys.stderr)
	sys.exit(1)
print("PASS: worker runtime retained SDL input/window ownership without creating worker-inaccessible GL state")
