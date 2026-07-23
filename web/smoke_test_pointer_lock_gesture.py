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
	page.locator("#canvas").click(position={"x": 20, "y": 20})
	locked = page.evaluate("SurrealBrowserPointerLock.status()")
	page.evaluate("SurrealBrowserPointerLock.setRequested(false)")
	unlocked = page.evaluate("({ status: SurrealBrowserPointerLock.status(), exits: pointerLockTest.exits })")
	page.evaluate("SurrealBrowserPointerLock.setXRActive(true); SurrealBrowserPointerLock.setRequested(true)")
	page.locator("#canvas").click(position={"x": 25, "y": 25})
	xr = page.evaluate("SurrealBrowserPointerLock.status()")
	page.evaluate("SurrealBrowserPointerLock.setXRActive(false); pointerLockTest.failNextRequest()")
	page.locator("#canvas").click(position={"x": 30, "y": 30})
	page.wait_for_function("SurrealBrowserPointerLock.status().lastError !== null")
	fallback = page.evaluate("SurrealBrowserPointerLock.status()")
	browser.close()

result = {"initial": initial, "locked": locked, "unlocked": unlocked,
	"xr": xr, "fallback": fallback, "pageErrors": page_errors}
print(json.dumps(result, indent=2))
failed = (
	initial["requestAttempts"] != 0 or initial["active"] or not initial["requested"] or
	locked["requestAttempts"] != 1 or not locked["active"] or
	unlocked["status"]["requested"] or unlocked["status"]["active"] or unlocked["exits"] != 1 or
	xr["requestAttempts"] != 1 or not xr["xrActive"] or
	fallback["requestAttempts"] != 2 or fallback["lastError"] != "NotAllowedError" or page_errors
)
if failed:
	print("FAIL: browser pointer-lock gesture policy", file=sys.stderr)
	sys.exit(1)
print("PASS: startup is inert, trusted canvas click locks, unlock works, XR and rejection are safe")
