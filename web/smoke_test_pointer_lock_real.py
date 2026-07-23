"""Exercise Chrome's real Pointer Lock API and native relative-motion bridge."""

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
	page = browser.new_page(viewport={"width": 900, "height": 700})
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(base_url + "/web/test_pointer_lock_real.html", wait_until="load")
	page.wait_for_function("window.realPointerLockTestReady === true")
	page.locator("[data-pointer-lock-capture]").click()
	page.wait_for_function("document.pointerLockElement === document.getElementById('canvas') && "
		"SurrealBrowserPointerLock.status().bridgeOwnsMotion")
	before = page.evaluate("({ status: SurrealBrowserPointerLock.status(), native: realPointerLockTest.nativeMouse })")
	page.mouse.move(470, 340)
	page.mouse.move(520, 375)
	page.wait_for_function("SurrealBrowserPointerLock.status().forwardedMouseMotionEvents > 0")
	after = page.evaluate("({ status: SurrealBrowserPointerLock.status(), native: realPointerLockTest.nativeMouse })")
	page.evaluate("document.exitPointerLock()")
	page.wait_for_function("document.pointerLockElement === null")
	released = page.evaluate("SurrealBrowserPointerLock.status()")
	browser.close()

result = {"before": before, "after": after, "released": released, "pageErrors": page_errors}
print(json.dumps(result, indent=2))
failed = (
	not before["status"]["active"] or not before["status"]["bridgeOwnsMotion"] or
	not before["status"]["nativeCallsAllowed"] or not before["status"]["bridgeReady"] or
	after["status"]["lockedMouseMotionEvents"] <= before["status"]["lockedMouseMotionEvents"] or
	after["status"]["nonzeroMouseMotionEvents"] <= before["status"]["nonzeroMouseMotionEvents"] or
	after["status"]["forwardedMouseMotionEvents"] <= before["status"]["forwardedMouseMotionEvents"] or
	after["native"]["events"] == 0 or not after["native"]["active"] or
	after["status"]["blockedMouseMotionEvents"] != 0 or released["active"] or
	released["bridgeOwnsMotion"] or page_errors
)
if failed:
	print("FAIL: real Chrome pointer lock did not deliver relative motion", file=sys.stderr)
	sys.exit(1)
print("PASS: real Chrome lock delivered nonzero relative motion and released cleanly")
