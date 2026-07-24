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
map_name = os.environ.get("SURREAL_WEB_MAP")
url = f"{base_url}{harness_path}?engineBase={quote(engine_base, safe='/')}"
if map_name:
	url += "&map=" + quote(map_name, safe="-")


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
	browser = playwright.chromium.launch(channel="chrome", headless=True, args=["--autoplay-policy=user-gesture-required"])
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

	# The running game must use the same production user-gesture controllers
	# that flat-to-XR transitions will later suspend and resume.
	page.evaluate("window.surrealBrowserAudio.suspend()")
	if not wait_until(page, "window.surrealGetAudioDiagnostics().state === 'suspended'", timeout=10):
		raise RuntimeError("the running game's audio graph did not suspend for lifecycle qualification")
	audio_before = page.evaluate("window.surrealGetAudioDiagnostics()")
	page.locator("[data-audio-unlock]").click()
	if not wait_until(page, "window.surrealGetAudioDiagnostics().state === 'running'", timeout=10):
		raise RuntimeError("trusted audio unlock did not start the running game's audio graph")
	time.sleep(0.25)
	audio_running = page.evaluate("window.surrealGetAudioDiagnostics()")

	page.locator("[data-pointer-lock-capture]").click()
	if not wait_until(page, "document.pointerLockElement === document.getElementById('canvas') && window.surrealGetPointerLockDiagnostics().bridgeOwnsMotion", timeout=10):
		raise RuntimeError("trusted pointer-lock capture did not activate the native relative-motion bridge")
	pointer_locked = page.evaluate("window.surrealGetPointerLockDiagnostics()")
	page.mouse.move(570, 340)
	page.mouse.move(640, 390)
	if not wait_until(page, "window.surrealGetPointerLockDiagnostics().forwardedMouseMotionEvents > 0", timeout=10):
		raise RuntimeError("pointer lock did not forward nonzero relative motion to the live game")
	pointer_moved = page.evaluate("window.surrealGetPointerLockDiagnostics()")
	page.evaluate("SurrealBrowserPointerLock.setXRActive(true)")
	if not wait_until(page, "document.pointerLockElement === null", timeout=10):
		raise RuntimeError("XR-style activation did not release flat pointer lock")
	pointer_xr = page.evaluate("window.surrealGetPointerLockDiagnostics()")
	page.evaluate("SurrealBrowserPointerLock.setXRActive(false)")
	pointer_reacquired = None
	for _ in range(3):
		page.locator("#canvas").focus()
		page.locator("[data-pointer-lock-capture]").click()
		if wait_until(page, "window.surrealGetPointerLockDiagnostics().active && window.surrealGetPointerLockDiagnostics().bridgeOwnsMotion", timeout=3):
			time.sleep(0.25)
			candidate = page.evaluate("window.surrealGetPointerLockDiagnostics()")
			if candidate["active"] and candidate["bridgeOwnsMotion"]:
				pointer_reacquired = candidate
				break
	if pointer_reacquired is None:
		raise RuntimeError("flat pointer lock could not be reacquired after XR-style release")
	page.evaluate("SurrealBrowserPointerLock.setRequested(false)")

	# Resize the real Emscripten canvas and require the renderer to observe the
	# new drawing buffer without changing context generation or stopping ticks.
	resize_result = page.evaluate("Module.ccall('Surreal_ResizeBrowserViewport', 'number', ['number', 'number'], [960, 540])")
	if resize_result != 1:
		raise RuntimeError("native browser viewport resize was rejected")
	if not wait_until(page, "window.surrealGetWebGL2Diagnostics().drawingBufferWidth === 960 && window.surrealGetWebGL2Diagnostics().drawingBufferHeight === 540", timeout=10):
		raise RuntimeError("WebGL2 renderer did not adopt the resized drawing buffer")
	if not wait_until(page, "window.surrealGetWebGL2Diagnostics().drawCalls > 0", timeout=10):
		raise RuntimeError("UE1 draws did not continue after canvas resize")
	resized = page.evaluate("window.surrealGetWebGL2Diagnostics()")
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
		"audio": {"before": audio_before, "running": audio_running},
		"pointerLock": {"locked": pointer_locked, "moved": pointer_moved, "xrReleased": pointer_xr, "reacquired": pointer_reacquired},
		"resized": resized,
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
		and before["submissions"] > 0 and before["submissions"] < before["drawCalls"]
		and before["textures"] >= 5
		and before["unsupportedDraws"] == 0
		and before["errors"] == 0
		and audio_running["state"] == "running"
		and audio_running["currentTimeMs"] >= audio_before["currentTimeMs"]
		and pointer_locked["active"] and pointer_locked["bridgeOwnsMotion"]
		and pointer_moved["forwardedMouseMotionEvents"] > pointer_locked["forwardedMouseMotionEvents"]
		and pointer_xr["active"] is False and pointer_xr["bridgeOwnsMotion"] is False
		and pointer_xr["forwardedEscapeIntents"] == pointer_moved["forwardedEscapeIntents"]
		and pointer_reacquired["active"] and pointer_reacquired["bridgeOwnsMotion"]
		and resized["drawingBufferWidth"] == 960 and resized["drawingBufferHeight"] == 540
		and resized["generation"] == before["generation"] and resized["errors"] == 0
		and before_non_blank >= 5.0
		and after["state"] == 1
		and after["generation"] == before["generation"] + 1
		and after["losses"] == before["losses"] + 1
		and after["drawCalls"] > 0
		and after["submissions"] > 0 and after["submissions"] < after["drawCalls"]
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
