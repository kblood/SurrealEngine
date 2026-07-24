"""Exercise the actual UE1 WebGL2 render device, including context restore."""

import json
import os
import sys
import time
from pathlib import Path
from urllib.parse import quote

from PIL import Image
from playwright.sync_api import sync_playwright


base_url = os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/")
engine_base = os.environ.get("SURREAL_WEB_ENGINE_BASE", "/build-emscripten/")
harness_path = os.environ.get("SURREAL_WEBGL2_HARNESS_PATH", "/web/index_webgl2.html")
screenshot_path = os.environ.get("SURREAL_WEBGL2_SCREENSHOT", "web/webgl2_smoke_screenshot.png")
result_path = os.environ.get("SURREAL_WEBGL2_RESULT")
url = f"{base_url}{harness_path}?engineBase={quote(engine_base, safe='/')}"


def wait_until(page, expression, timeout=120):
	deadline = time.time() + timeout
	while time.time() < deadline:
		if page.evaluate(expression):
			return True
		time.sleep(0.25)
	return False


def non_blank_percent(path):
	image = Image.open(path).convert("RGB")
	pixels = list(image.getdata())
	background = pixels[0]
	return 100.0 * sum(pixel != background for pixel in pixels) / len(pixels)


with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	page = browser.new_page(viewport={"width": 1280, "height": 800})
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(url, wait_until="load")
	if not wait_until(page, "window.surrealBooted || window.surrealCrashed"):
		diagnostic = page.evaluate("""() => ({
			runtimeReady: typeof Module !== 'undefined' && !!Module.calledRun,
			dataResult: window.surrealDataBootResult || null,
			crashed: window.surrealCrashed,
			logTail: (window.surrealLog || []).slice(-80),
		})""")
		print(json.dumps(diagnostic, indent=2))
		raise RuntimeError("timed out waiting for the WebGL2 game boot")
	if page.evaluate("window.surrealCrashed"):
		raise RuntimeError(page.evaluate("window.surrealCrashed"))

	ticks = []
	for _ in range(5):
		time.sleep(1)
		ticks.append(page.evaluate("window.surrealGetTickCount()"))
	before = page.evaluate("window.surrealGetWebGL2Diagnostics()")
	page.locator("#canvas").screenshot(path=screenshot_path)
	before_non_blank = non_blank_percent(screenshot_path)

	page.evaluate("window.surrealLoseWebGL2Context()")
	if not wait_until(page, "window.surrealGetWebGL2Diagnostics().generation > " + str(before["generation"]), timeout=30):
		raise RuntimeError("WebGL2 context did not restore with a new generation")
	if not wait_until(page, "window.surrealGetWebGL2Diagnostics().drawCalls > 0", timeout=30):
		raise RuntimeError("UE1 draws did not resume after WebGL2 context restoration")
	after = page.evaluate("window.surrealGetWebGL2Diagnostics()")
	tick_after_restore = page.evaluate("window.surrealGetTickCount()")
	restored_path = str(Path(screenshot_path).with_name(Path(screenshot_path).stem + "-restored" + Path(screenshot_path).suffix))
	page.locator("#canvas").screenshot(path=restored_path)
	after_non_blank = non_blank_percent(restored_path)

	result = {
		"url": url,
		"ticks": ticks,
		"before": before,
		"after": after,
		"tickAfterRestore": tick_after_restore,
		"nonBlankPercent": round(before_non_blank, 2),
		"restoredNonBlankPercent": round(after_non_blank, 2),
		"screenshots": [screenshot_path, restored_path],
		"pageErrors": page_errors,
		"logTail": page.evaluate("window.surrealLog.slice(-40)"),
	}
	print(json.dumps(result, indent=2))
	if result_path:
		Path(result_path).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

	passed = (
		len(set(ticks)) > 1
		and before["state"] == 1
		and before["drawCalls"] > 0
		and before["textures"] >= 5
		and before["unsupportedDraws"] == 0
		and before["errors"] == 0
		and before_non_blank >= 5.0
		and after["state"] == 1
		and after["generation"] == before["generation"] + 1
		and after["losses"] == before["losses"] + 1
		and after["contextLossStatuses"] >= before["contextLossStatuses"] + 1
		and after["drawCalls"] > 0
		and after["textures"] >= 5
		and after["unsupportedDraws"] == 0
		and after["errors"] == 0
		and after_non_blank >= 5.0
		and tick_after_restore > ticks[-1]
		and not page_errors
	)
	page.evaluate("window.surrealRequestQuit()")
	browser.close()

if not passed:
	print("FAIL: actual UE1 WebGL2 render-device smoke", file=sys.stderr)
	raise SystemExit(1)
print("PASS: UE1 rendered through WebGL2 and recovered without restarting the WASM runtime")
