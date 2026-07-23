import json
import os
import sys
import time

from playwright.sync_api import sync_playwright


base_url = os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/")
url = f"{base_url}/web/probes/webgpu_canvas_async_lifetime_probe.html"

with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	page = browser.new_page()
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(url, wait_until="load")
	deadline = time.time() + 30
	result = None
	while time.time() < deadline:
		result = page.evaluate("window.canvasLifetimeProbeResult ?? null")
		if result is not None:
			break
		time.sleep(0.1)
	browser.close()

print(json.dumps({"result": result, "pageErrors": page_errors}, indent=2))
if result is None:
	print("FAIL: canvas lifetime probe timed out")
	sys.exit(1)
if not result.get("supported"):
	print("FAIL: WebGPU is unavailable in this browser")
	sys.exit(1)
if result.get("sameFrameError") is not None:
	print("FAIL: same-frame canvas submission produced a validation error")
	sys.exit(1)
expired_error = result.get("expiredFrameError") or ""
if "Destroyed texture" not in expired_error or "used in a submit" not in expired_error:
	print("FAIL: crossing the animation-frame boundary did not expire the canvas texture")
	sys.exit(1)
if result.get("offscreenPresentError") is not None:
	print("FAIL: persistent offscreen rendering with same-frame presentation produced a validation error")
	sys.exit(1)
if page_errors:
	print("FAIL: browser page errors occurred")
	sys.exit(1)

print("PASS: persistent offscreen rendering survives a frame boundary and presents synchronously")
