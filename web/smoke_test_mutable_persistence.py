"""Playwright runner for the local-only mutable-data persistence overlay."""

import json
import os
import sys
import time

from playwright.sync_api import sync_playwright


base_url = next(
	(arg.split("=", 1)[1].rstrip("/") for arg in sys.argv[1:] if arg.startswith("--base-url=")),
	os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/"),
)
url = base_url + "/web/test_mutable_persistence.html"


with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	context = browser.new_context(service_workers="block")
	page = context.new_page()
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(url, wait_until="load")

	deadline = time.time() + 30
	while time.time() < deadline and not page.evaluate("window.mutablePersistenceTestDone === true"):
		time.sleep(0.05)

	results = page.evaluate("window.mutablePersistenceTestResults") or []
	print(json.dumps(results, indent=2))
	failures = [result for result in results if not result.get("passed")]
	if not page.evaluate("window.mutablePersistenceTestDone === true"):
		print("FAIL: timed out waiting for mutable persistence tests", file=sys.stderr)
		print("\n".join(page_errors), file=sys.stderr)
		sys.exit(1)
	if failures:
		print(f"FAIL: {len(failures)} mutable persistence test(s) failed", file=sys.stderr)
		print("\n".join(page_errors), file=sys.stderr)
		sys.exit(1)
	unexpected_errors = [error for error in page_errors if "mutable persistence test(s) failed" not in error]
	if unexpected_errors:
		print("FAIL: unexpected page error(s)", file=sys.stderr)
		print("\n".join(unexpected_errors), file=sys.stderr)
		sys.exit(1)

	print(f"PASS: {len(results)} deterministic mutable persistence checks")
	context.close()
	browser.close()
