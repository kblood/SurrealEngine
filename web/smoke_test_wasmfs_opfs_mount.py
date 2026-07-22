"""Verify the synthetic WasmFS OPFS mount across a browser reload."""

import os
import re
import sys
import time

from playwright.sync_api import sync_playwright


base_url = next(
	(arg.split("=", 1)[1].rstrip("/") for arg in sys.argv[1:] if arg.startswith("--base-url=")),
	os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/"),
)
probe_url = base_url + "/build-wasmfs-probe/index.html"
host_url = base_url + "/web/wasmfs_opfs_probe_host.html"


def wait_for(page, messages, pattern, timeout=30):
	deadline = time.time() + timeout
	compiled = re.compile(pattern)
	while time.time() < deadline:
		for message in messages:
			if compiled.search(message):
				return message
		page.wait_for_timeout(50)
	raise RuntimeError("timed out waiting for " + pattern + "; console=" + repr(messages))


with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	context = browser.new_context(service_workers="block")
	page = context.new_page()
	messages = []
	page.on("console", lambda message: messages.append(message.text))

	page.goto(host_url, wait_until="load")
	page.evaluate("""async () => {
		const root = await navigator.storage.getDirectory();
		try { await root.removeEntry("surreal-probe", { recursive: true }); }
		catch (error) { if (!error || error.name !== "NotFoundError") throw error; }
	}""")
	messages.clear()

	page.goto(probe_url, wait_until="load")
	created = wait_for(page, messages,
		r"PASS wasmfs-opfs-symlink existing=0 .*heap-growth=0 write-denied=1")
	messages.clear()

	page.reload(wait_until="load")
	reloaded = wait_for(page, messages,
		r"PASS wasmfs-opfs-symlink existing=1 .*heap-growth=0 write-denied=1")

	page.goto(host_url, wait_until="load")
	page.evaluate("""async () => {
		const root = await navigator.storage.getDirectory();
		await root.removeEntry("surreal-probe", { recursive: true });
	}""")
	context.close()
	browser.close()

print(created)
print(reloaded)
print("PASS: WasmFS OPFS stat/seek/read persists across reload without heap growth; writes are denied")
