import json
import sys
import time

from playwright.sync_api import sync_playwright


URL = "http://localhost:8091/web/test_ut99_importer.html"


with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	context = browser.new_context()
	page = context.new_page()
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(URL, wait_until="load")

	deadline = time.time() + 30
	while time.time() < deadline and not page.evaluate("window.importerTestDone === true"):
		time.sleep(0.05)

	results = page.evaluate("window.importerTestResults")
	failures = [result for result in results if not result.get("passed")]
	print(json.dumps(results, indent=2))
	if not page.evaluate("window.importerTestDone === true"):
		print("FAIL: timed out waiting for importer tests")
		print("\n".join(page_errors))
		sys.exit(1)
	if failures:
		print(f"FAIL: {len(failures)} importer test(s) failed")
		print("\n".join(page_errors))
		sys.exit(1)
	if page_errors:
		print("FAIL: unexpected page error(s)")
		print("\n".join(page_errors))
		sys.exit(1)

	print(f"PASS: {len(results)} deterministic importer checks")
	context.close()
	browser.close()
