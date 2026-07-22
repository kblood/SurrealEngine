"""Verify redistributable no-data builds stop at the legal local-import gate."""

import json
import os
import sys
import time

from playwright.sync_api import sync_playwright


base_url = next(
	(arg.split("=", 1)[1].rstrip("/") for arg in sys.argv[1:] if arg.startswith("--base-url=")),
	os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/"),
)
pages = ["index.html", "index_webgpu.html"]
results = []

with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	for page_name in pages:
		context = browser.new_context(service_workers="block")
		page = context.new_page()
		page_errors = []
		page.on("pageerror", lambda error: page_errors.append(str(error)))
		page.goto(f"{base_url}/web/{page_name}", wait_until="load")
		deadline = time.time() + 60
		while time.time() < deadline:
			state = page.evaluate("window.surrealDataBootResult && window.surrealDataBootResult.import && window.surrealDataBootResult.import.state")
			if state == "waiting-for-import" or page.evaluate("window.surrealCrashed"):
				break
			time.sleep(0.1)
		result = page.evaluate("""() => ({
			state: window.surrealDataBootResult && window.surrealDataBootResult.import && window.surrealDataBootResult.import.state,
			booted: window.surrealBooted,
			crashed: window.surrealCrashed,
			importerVisible: !document.getElementById('game-data-importer').hidden,
			status: document.querySelector('[data-ut99-status]').textContent,
		})""")
		result["page"] = page_name
		result["pageErrors"] = page_errors
		results.append(result)
		context.close()
	browser.close()

print(json.dumps(results, indent=2))
failures = [result for result in results if
	result["state"] != "waiting-for-import" or result["booted"] or result["crashed"] or
	not result["importerVisible"] or result["pageErrors"]]
if failures:
	print("FAIL: no-data browser import gate", file=sys.stderr)
	sys.exit(1)
print("PASS: flat and WebGPU no-data builds wait at the local import gate")
