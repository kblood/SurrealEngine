"""Verify browser pointer lock is gesture-gated, reversible, and XR-safe."""

import json
import os
import sys

from playwright.sync_api import sync_playwright


base_url = next(
	(arg.split("=", 1)[1].rstrip("/") for arg in sys.argv[1:] if arg.startswith("--base-url=")),
	os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/"),
)

with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	page = browser.new_page()
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(base_url + "/web/test_pointer_lock_gesture.html", wait_until="load")
	page.wait_for_function("window.pointerLockTestReady === true")
	initial = page.evaluate("SurrealBrowserPointerLock.status()")
	page.evaluate("SurrealBrowserPointerLock.setInteractive(true)")
	prompt = page.evaluate("({ status: SurrealBrowserPointerLock.status(), label: document.querySelector('[data-pointer-lock-capture]').textContent })")
	page.locator("[data-pointer-lock-capture]").click()
	locked = page.evaluate("SurrealBrowserPointerLock.status()")
	page.evaluate("pointerLockTest.loseAsBrowserEscape()")
	escape_loss = page.evaluate("({ status: SurrealBrowserPointerLock.status(), nativeEscapes: pointerLockTest.nativeEscapes, label: document.querySelector('[data-pointer-lock-capture]').textContent })")
	page.locator("[data-pointer-lock-capture]").click()
	resumed = page.evaluate("SurrealBrowserPointerLock.status()")
	page.evaluate("window.dispatchEvent(new Event('blur')); pointerLockTest.loseAsBrowserEscape()")
	background_loss = page.evaluate("({ status: SurrealBrowserPointerLock.status(), nativeEscapes: pointerLockTest.nativeEscapes })")
	page.evaluate("window.dispatchEvent(new Event('focus'))")
	page.locator("[data-pointer-lock-capture]").click()
	background_resumed = page.evaluate("SurrealBrowserPointerLock.status()")
	page.evaluate("SurrealBrowserPointerLock.setRequested(false)")
	unlocked = page.evaluate("({ status: SurrealBrowserPointerLock.status(), exits: pointerLockTest.exits, nativeEscapes: pointerLockTest.nativeEscapes })")
	page.evaluate("SurrealBrowserPointerLock.setRequested(true)")
	page.locator("[data-pointer-lock-capture]").click()
	page.evaluate("SurrealBrowserPointerLock.setXRActive(true)")
	xr = page.evaluate("({ status: SurrealBrowserPointerLock.status(), exits: pointerLockTest.exits, nativeEscapes: pointerLockTest.nativeEscapes })")
	page.evaluate("SurrealBrowserPointerLock.setXRActive(false); pointerLockTest.failNextRequest()")
	page.locator("#canvas").click(position={"x": 30, "y": 30})
	page.wait_for_function("SurrealBrowserPointerLock.status().lastError !== null")
	fallback = page.evaluate("SurrealBrowserPointerLock.status()")
	browser.close()

result = {"initial": initial, "prompt": prompt, "locked": locked,
	"escapeLoss": escape_loss, "resumed": resumed, "backgroundLoss": background_loss,
	"backgroundResumed": background_resumed, "unlocked": unlocked,
	"xr": xr, "fallback": fallback, "pageErrors": page_errors}
print(json.dumps(result, indent=2))
failed = (
	initial["requestAttempts"] != 0 or initial["active"] or not initial["requested"] or
	initial["interactive"] or initial["promptVisible"] or
	not prompt["status"]["promptVisible"] or prompt["label"] != "Capture mouse" or
	locked["requestAttempts"] != 1 or not locked["active"] or locked["promptVisible"] or
	escape_loss["status"]["active"] or not escape_loss["status"]["promptVisible"] or
	escape_loss["status"]["forwardedEscapeIntents"] != 1 or escape_loss["nativeEscapes"] != 1 or
	escape_loss["label"] != "Resume mouse look" or not resumed["active"] or resumed["requestAttempts"] != 2 or
	background_loss["status"]["active"] or background_loss["status"]["pageFocused"] or
	background_loss["status"]["forwardedEscapeIntents"] != 1 or background_loss["nativeEscapes"] != 1 or
	not background_resumed["active"] or not background_resumed["pageFocused"] or
	background_resumed["requestAttempts"] != 3 or
	unlocked["status"]["requested"] or unlocked["status"]["active"] or unlocked["exits"] != 1 or
	unlocked["nativeEscapes"] != 1 or not xr["status"]["xrActive"] or xr["status"]["active"] or
	xr["status"]["promptVisible"] or xr["exits"] != 2 or xr["nativeEscapes"] != 1 or
	fallback["requestAttempts"] != 5 or fallback["lastError"] != "NotAllowedError" or
	not fallback["promptVisible"] or fallback["forwardedEscapeIntents"] != 1 or page_errors
)
if failed:
	print("FAIL: browser pointer-lock gesture policy", file=sys.stderr)
	sys.exit(1)
print("PASS: post-start capture/resume is trusted, Escape intent forwards once, and programmatic/XR unlocks stay inert")
