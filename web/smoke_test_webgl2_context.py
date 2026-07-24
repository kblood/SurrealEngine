"""Verify WebGL2 context creation, pixels, forced loss, and restoration."""

import json
import os
import sys

from playwright.sync_api import sync_playwright


base_url = next(
	(argument.split("=", 1)[1].rstrip("/") for argument in sys.argv[1:] if argument.startswith("--base-url=")),
	os.environ.get("SURREAL_WEBGL2_PROBE_URL", "http://localhost:8091").rstrip("/"),
)

with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	context = browser.new_context(service_workers="block")
	page = context.new_page()
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(base_url + "/WebGL2ContextProbe.html", wait_until="load")
	page.wait_for_function("window.webgl2ContextProbeResult && window.webgl2ContextProbeResult.complete", timeout=15000)
	result = page.evaluate("window.webgl2ContextProbeResult")
	result["pageErrors"] = page_errors
	browser.close()

print(json.dumps(result, indent=2))


def channels(pixel):
	return tuple((pixel >> shift) & 0xFF for shift in (0, 8, 16, 24))


initial = channels(result.get("initialPixel", 0))
restored = channels(result.get("restoredPixel", 0))
passed = (
	result.get("error") is None
	and result.get("initialState") == 1
	and result.get("initialGeneration") == 1
	and result.get("lossObserved") is True
	and result.get("restored") is True
	and result.get("finalState") == 1
	and result.get("finalGeneration") == 2
	and result.get("lossCount") == 1
	and all(abs(actual - expected) <= 2 for actual, expected in zip(initial, (51, 102, 153, 255)))
	and all(abs(actual - expected) <= 2 for actual, expected in zip(restored, (26, 204, 51, 255)))
	and not page_errors
)
if not passed:
	print("FAIL: WebGL2 context lifecycle probe", file=sys.stderr)
	raise SystemExit(1)
print("PASS: WebGL2 context created, rendered exact pixels, recovered from forced loss")
