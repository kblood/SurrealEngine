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
	bridge_ready = page.evaluate("SurrealBrowserPointerLock.relativeMotionBridgeReady()")
	page.evaluate("SurrealBrowserPointerLock.setInteractive(true)")
	prompt = page.evaluate("({ status: SurrealBrowserPointerLock.status(), label: document.querySelector('[data-pointer-lock-capture]').textContent })")
	page.locator("[data-pointer-lock-capture]").click()
	locked = page.evaluate("SurrealBrowserPointerLock.status()")
	page.evaluate("pointerLockTest.move(8, -5); pointerLockTest.move(-3, 9)")
	motion = page.evaluate("pointerLockTest.nativeMouse")
	page.evaluate("globalThis.surrealXRNativeCallsBlocked = true; pointerLockTest.move(100, 100)")
	blocked_motion = page.evaluate("pointerLockTest.nativeMouse")
	page.evaluate("globalThis.surrealXRNativeCallsBlocked = false")
	page.evaluate("pointerLockTest.loseAsBrowserEscape()")
	escape_loss = page.evaluate("({ status: SurrealBrowserPointerLock.status(), nativeEscapes: pointerLockTest.nativeEscapes, nativeMouse: pointerLockTest.nativeMouse, label: document.querySelector('[data-pointer-lock-capture]').textContent })")
	page.evaluate("pointerLockTest.move(50, 50)")
	unlocked_motion = page.evaluate("pointerLockTest.nativeMouse")
	page.locator("[data-pointer-lock-capture]").click()
	resumed = page.evaluate("SurrealBrowserPointerLock.status()")
	page.evaluate("window.dispatchEvent(new Event('blur')); pointerLockTest.loseAsBrowserEscape()")
	background_loss = page.evaluate("({ status: SurrealBrowserPointerLock.status(), nativeEscapes: pointerLockTest.nativeEscapes })")
	page.evaluate("window.dispatchEvent(new Event('focus'))")
	page.locator("[data-pointer-lock-capture]").click()
	background_resumed = page.evaluate("SurrealBrowserPointerLock.status()")
	page.evaluate("pointerLockTest.move(7, -6)")
	pre_blocked_release = page.evaluate("pointerLockTest.nativeMouse")
	page.evaluate("globalThis.surrealXRNativeCallsBlocked = true; SurrealBrowserPointerLock.setRequested(false)")
	pending_release = page.evaluate("({ status: SurrealBrowserPointerLock.status(), nativeMouse: pointerLockTest.nativeMouse })")
	page.evaluate("globalThis.surrealXRNativeCallsBlocked = false; window.dispatchEvent(new Event('surrealnativecallgatechange'))")
	unlocked = page.evaluate("({ status: SurrealBrowserPointerLock.status(), exits: pointerLockTest.exits, nativeEscapes: pointerLockTest.nativeEscapes, nativeMouse: pointerLockTest.nativeMouse })")
	page.evaluate("pointerLockTest.setSyntheticLock(pointerLockTest.canvas); pointerLockTest.move(40, 40)")
	not_requested_motion = page.evaluate("pointerLockTest.nativeMouse")
	page.evaluate("pointerLockTest.setSyntheticLock(document.getElementById('other')); SurrealBrowserPointerLock.setRequested(true); pointerLockTest.move(30, 30)")
	wrong_canvas_motion = page.evaluate("pointerLockTest.nativeMouse")
	page.evaluate("pointerLockTest.setSyntheticLock(null)")
	page.evaluate("SurrealBrowserPointerLock.setRequested(true)")
	page.locator("[data-pointer-lock-capture]").click()
	page.evaluate("SurrealBrowserPointerLock.setXRActive(true)")
	page.evaluate("pointerLockTest.setSyntheticLock(pointerLockTest.canvas); pointerLockTest.move(20, 20)")
	xr_motion = page.evaluate("pointerLockTest.nativeMouse")
	page.evaluate("pointerLockTest.setSyntheticLock(null)")
	xr = page.evaluate("({ status: SurrealBrowserPointerLock.status(), exits: pointerLockTest.exits, nativeEscapes: pointerLockTest.nativeEscapes })")
	page.evaluate("SurrealBrowserPointerLock.setXRActive(false); pointerLockTest.failNextRequest()")
	page.locator("#canvas").click(position={"x": 30, "y": 30})
	page.wait_for_function("SurrealBrowserPointerLock.status().lastError !== null")
	fallback = page.evaluate("SurrealBrowserPointerLock.status()")
	browser.close()

result = {"initial": initial, "bridgeReady": bridge_ready, "prompt": prompt, "locked": locked,
	"motion": motion, "blockedMotion": blocked_motion, "unlockedMotion": unlocked_motion,
	"notRequestedMotion": not_requested_motion, "wrongCanvasMotion": wrong_canvas_motion,
	"xrMotion": xr_motion,
	"escapeLoss": escape_loss, "resumed": resumed, "backgroundLoss": background_loss,
	"backgroundResumed": background_resumed, "preBlockedRelease": pre_blocked_release,
	"pendingRelease": pending_release, "unlocked": unlocked,
	"xr": xr, "fallback": fallback, "pageErrors": page_errors}
print(json.dumps(result, indent=2))
failed = (
	not bridge_ready or initial["requestAttempts"] != 0 or initial["active"] or not initial["requested"] or
	initial["interactive"] or initial["promptVisible"] or initial["bridgeOwnsMotion"] or
	initial["pendingBridgeActive"] is not None or initial["pendingMouseReset"] or
	not prompt["status"]["promptVisible"] or prompt["status"]["bridgeOwnsMotion"] or
	prompt["label"] != "Capture mouse" or
	locked["requestAttempts"] != 1 or not locked["active"] or locked["promptVisible"] or
	not locked["bridgeOwnsMotion"] or
	motion["x"] != 5 or motion["y"] != 4 or motion["events"] != 2 or not motion["active"] or
	blocked_motion != motion or escape_loss["nativeMouse"]["x"] != 0 or
	escape_loss["nativeMouse"]["y"] != 0 or escape_loss["nativeMouse"]["resets"] <= motion["resets"] or
	escape_loss["nativeMouse"]["active"] or unlocked_motion != escape_loss["nativeMouse"] or
	escape_loss["status"]["active"] or not escape_loss["status"]["promptVisible"] or
	escape_loss["status"]["bridgeOwnsMotion"] or
	escape_loss["status"]["forwardedEscapeIntents"] != 1 or escape_loss["nativeEscapes"] != 1 or
	escape_loss["label"] != "Resume mouse look" or not resumed["active"] or
	resumed["requestAttempts"] != 2 or not resumed["bridgeOwnsMotion"] or
	background_loss["status"]["active"] or background_loss["status"]["pageFocused"] or
	background_loss["status"]["bridgeOwnsMotion"] or
	background_loss["status"]["forwardedEscapeIntents"] != 1 or background_loss["nativeEscapes"] != 1 or
	not background_resumed["active"] or not background_resumed["pageFocused"] or
	background_resumed["requestAttempts"] != 3 or not background_resumed["bridgeOwnsMotion"] or
	pre_blocked_release["x"] != 7 or pre_blocked_release["y"] != -6 or
	pre_blocked_release["events"] != 3 or not pre_blocked_release["active"] or
	not pending_release["status"]["bridgeOwnsMotion"] or
	pending_release["status"]["pendingBridgeActive"] is not False or
	not pending_release["status"]["pendingMouseReset"] or
	pending_release["nativeMouse"] != pre_blocked_release or
	unlocked["status"]["requested"] or unlocked["status"]["active"] or unlocked["exits"] != 1 or
	unlocked["status"]["bridgeOwnsMotion"] or unlocked["status"]["pendingBridgeActive"] is not None or
	unlocked["status"]["pendingMouseReset"] or unlocked["nativeEscapes"] != 1 or
	unlocked["nativeMouse"]["x"] != 0 or unlocked["nativeMouse"]["y"] != 0 or
	unlocked["nativeMouse"]["events"] != 3 or unlocked["nativeMouse"]["active"] or
	unlocked["nativeMouse"]["resets"] <= pre_blocked_release["resets"] or
	not_requested_motion != unlocked["nativeMouse"] or wrong_canvas_motion != not_requested_motion or
	not xr["status"]["xrActive"] or xr["status"]["active"] or xr["status"]["bridgeOwnsMotion"] or
	xr_motion["x"] != 0 or xr_motion["y"] != 0 or xr_motion["events"] != 3 or xr_motion["active"] or
	xr["status"]["promptVisible"] or xr["exits"] != 2 or xr["nativeEscapes"] != 1 or
	fallback["requestAttempts"] != 5 or fallback["lastError"] != "NotAllowedError" or
	not fallback["promptVisible"] or fallback["bridgeOwnsMotion"] or
	fallback["forwardedEscapeIntents"] != 1 or page_errors
)
if failed:
	print("FAIL: browser pointer-lock gesture policy", file=sys.stderr)
	sys.exit(1)
print("PASS: trusted capture forwards gated relative motion, Escape once, and recaptures without stale deltas")
