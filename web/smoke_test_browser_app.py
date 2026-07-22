"""Synthetic Playwright runner for the shared browser launcher."""
import json
import os
import sys
import time
from playwright.sync_api import sync_playwright

base_url = next((arg.split("=", 1)[1].rstrip("/") for arg in sys.argv[1:] if arg.startswith("--base-url=")), os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/"))
with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	page = browser.new_page()
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(base_url + "/web/test_browser_app.html", wait_until="load")
	deadline = time.time() + 30
	while time.time() < deadline and not page.evaluate("window.browserAppTestDone === true"):
		time.sleep(0.05)
	results = page.evaluate("window.browserAppTestResults") or []
	print(json.dumps(results, indent=2))
	failures = [result for result in results if not result.get("passed")]
	if not page.evaluate("window.browserAppTestDone === true") or failures or page_errors:
		print("FAIL: browser launcher checks failed", file=sys.stderr)
		print("\n".join(page_errors), file=sys.stderr)
		sys.exit(1)
	print(f"PASS: {len(results)} deterministic browser launcher checks")
	browser.close()
