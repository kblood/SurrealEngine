"""Exercise the experimental engine mount with synthetic UE1-shaped data."""

import json
import os
import sys
import time

from playwright.sync_api import sync_playwright


base_url = next(
	(arg.split("=", 1)[1].rstrip("/") for arg in sys.argv[1:] if arg.startswith("--base-url=")),
	os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/"),
)
page_url = base_url + "/web/index.html"


def wait_for_log(page, text, timeout=60):
	deadline = time.time() + timeout
	while time.time() < deadline:
		log = page.evaluate("window.surrealLog || []")
		if any(text in line for line in log):
			return log
		page.wait_for_timeout(50)
	raise RuntimeError("timed out waiting for log text: " + text)


with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	context = browser.new_context(service_workers="block")
	page = context.new_page()
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(page_url, wait_until="load")
	page.wait_for_function(
		"window.surrealDataBootResult && window.surrealDataBootResult.import.state === 'waiting-for-import'",
		timeout=60000,
	)
	page.evaluate("""async () => {
		const files = [
			["System/Core.u", "fake-core"], ["System/Engine.u", "fake-engine"],
			["System/Botpack.u", "fake-botpack"], ["System/UnrealTournament.ini", "[fake]"],
			["System/UnrealTournament.exe", "fake-exe"], ["Maps/DM-Deck16][.unr", "fake-map"],
			["Textures/Synthetic.utx", "fake-texture"], ["Sounds/Synthetic.uax", "fake-sound"],
			["Music/Synthetic.umx", "fake-music"],
		];
		const entries = files.map(([path, text]) => {
			const blob = new Blob([text]);
			return { path, size: blob.size, getBlob: async () => blob };
		});
		await window.surrealBrowserDataController.importController.importEntries(entries);
	}""")
	first_log = wait_for_log(page, "[data] mounted 9 OPFS file(s)")
	first_heap = page.evaluate("Module.ccall('Surreal_GetBrowserWasmHeapSize', 'number', [], [])")
	mount_mode = page.evaluate("Module.ccall('Surreal_GetBrowserOPFSMountMode', 'number', [], [])")

	page.goto(page_url, wait_until="load")
	second_log = wait_for_log(page, "[data] mounted 9 OPFS file(s)")
	second_heap = page.evaluate("Module.ccall('Surreal_GetBrowserWasmHeapSize', 'number', [], [])")
	crashed = page.evaluate("window.surrealCrashed")
	context.close()
	browser.close()

summary = {
	"firstRuntimeMode": next((line for line in first_log if "runtime=" in line), None),
	"restoredRuntimeMode": next((line for line in second_log if "via opfs-mount" in line), None),
	"firstHeapBytes": first_heap,
	"restoredHeapBytes": second_heap,
	"mountMode": mount_mode,
	"crashed": crashed,
	"pageErrors": page_errors,
}
print(json.dumps(summary, indent=2))
if ("runtime=opfs-mount" not in (summary["firstRuntimeMode"] or "") or
		"via opfs-mount" not in (summary["restoredRuntimeMode"] or "") or
		first_heap != 256 * 1024 * 1024 or second_heap != first_heap or mount_mode != 2 or crashed or page_errors):
	print("FAIL: experimental engine OPFS mount", file=sys.stderr)
	sys.exit(1)
print("PASS: synthetic import and reload use the Window-owned Asyncify OPFS mount at its 256 MiB initial heap")
