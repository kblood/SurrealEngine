"""Verify the import shell and WASM runtime load when navigator.gpu is absent."""

import json
import os
import sys
import time

from playwright.sync_api import sync_playwright


base_url = next(
	(argument.split("=", 1)[1].rstrip("/") for argument in sys.argv[1:] if argument.startswith("--base-url=")),
	os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/"),
)

with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	context = browser.new_context(service_workers="block")
	context.add_init_script("""
		Object.defineProperty(navigator, "gpu", { configurable: true, value: undefined });
		Object.defineProperty(navigator, "xr", { configurable: true, value: undefined });
	""")
	page = context.new_page()
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(base_url + "/web/surreal_app.html", wait_until="load")
	deadline = time.time() + 60
	while time.time() < deadline:
		if page.evaluate("window.surrealApp || window.surrealCrashed"):
			break
		time.sleep(0.1)
	result = page.evaluate("""() => ({
		ready: !!window.surrealApp,
		state: window.surrealApp?.result?.import?.state || null,
		crashed: window.surrealCrashed || null,
		webGPUAbsent: typeof navigator.gpu === "undefined",
		renderers: Array.from(document.querySelectorAll("[data-launcher-renderer] option"), option => option.value),
		capabilities: Array.from(document.querySelectorAll("[data-capability-list] li"), item => ({
			label: item.textContent,
			available: item.dataset.available,
		})),
		log: document.getElementById("log").textContent,
	})""")
	result["pageErrors"] = page_errors
	browser.close()

print(json.dumps(result, indent=2))
passed = (
	result["ready"]
	and result["state"] == "waiting-for-import"
	and result["crashed"] is None
	and result["webGPUAbsent"]
	and result["renderers"] == ["null"]
	and not result["pageErrors"]
	and "WebGPU unavailable" not in result["log"]
)
if not passed:
	print("FAIL: browser shell still depends on navigator.gpu", file=sys.stderr)
	raise SystemExit(1)
print("PASS: data/import shell and WASM runtime loaded without navigator.gpu")
