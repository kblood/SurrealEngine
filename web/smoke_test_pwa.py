"""Deterministic browser checks for the redistributable WebXR PWA shell."""

import json
import os
import sys
import time

from playwright.sync_api import sync_playwright


DEFAULT_BASE_URL = "http://localhost:8091"
BASE_URL = next(
	(arg.split("=", 1)[1].rstrip("/") for arg in sys.argv[1:] if arg.startswith("--base-url=")),
	os.environ.get("SURREAL_WEB_BASE_URL", DEFAULT_BASE_URL).rstrip("/"),
)
TEST_URL = BASE_URL + "/web/test_pwa.html"
INDEX_URL = BASE_URL + "/web/index_webxr.html"


def wait_until(predicate, timeout=15.0, message="condition"):
	deadline = time.time() + timeout
	while time.time() < deadline:
		if predicate():
			return
		time.sleep(0.05)
	raise AssertionError("timed out waiting for " + message)


def check(condition, message, details=None):
	if not condition:
		raise AssertionError(message + (": " + repr(details) if details is not None else ""))
	print("PASS: " + message)


def cache_inventory(page):
	return page.evaluate("""async () => {
		const result = {};
		for (const name of await caches.keys()) {
			result[name] = (await (await caches.open(name)).keys()).map(request => request.url).sort();
		}
		return result;
	}""")


def worker_status(page):
	return page.evaluate("""async () => {
		const registration = await navigator.serviceWorker.ready;
		const worker = registration.active || registration.waiting || registration.installing;
		return await new Promise((resolve, reject) => {
			const channel = new MessageChannel();
			const timeout = setTimeout(() => reject(new Error('worker status timeout')), 3000);
			channel.port1.onmessage = event => { clearTimeout(timeout); resolve(event.data); };
			worker.postMessage({ type: 'SURREAL_PWA_STATUS' }, [channel.port2]);
		});
	}""")


def main():
	with sync_playwright() as playwright:
		browser = playwright.chromium.launch(channel="chrome", headless=True)
		context = browser.new_context(service_workers="allow")
		page = context.new_page()
		page_errors = []
		page.on("pageerror", lambda error: page_errors.append(str(error)))

		try:
			page.goto(TEST_URL, wait_until="load")
			page.evaluate("""async () => {
				await (await caches.open('surrealengine-webxr-obsolete-shell')).put(
					'/obsolete', new Response('obsolete'));
				await (await caches.open('unrelated-keep')).put('/unrelated', new Response('keep'));
			}""")

			diagnostics = page.evaluate("SurrealPWA.register({ enabled: true })")
			check(diagnostics["state"] in ("active", "installed-reload-pending"),
				"registration seam installs without blocking", diagnostics)
			wait_until(
				lambda: page.evaluate("navigator.serviceWorker.controller !== null"),
				message="the activated worker to control the test page",
			)

			status = worker_status(page)
			check(status["type"] == "SURREAL_PWA_STATUS", "worker exposes deployment diagnostics")
			check(all(status["policySelfTest"].values()),
				"worker's cache-policy self-test rejects unsafe classifications",
				status["policySelfTest"])
			check(status["shellCache"].startswith("surrealengine-webxr-"),
				"shell cache name is versioned", status["shellCache"])
			check(status["runtimeCache"].startswith("surrealengine-webxr-"),
				"runtime cache name is versioned", status["runtimeCache"])

			inventory = cache_inventory(page)
			check("surrealengine-webxr-obsolete-shell" not in inventory,
				"activation atomically removes an obsolete application cache")
			check("unrelated-keep" in inventory,
				"activation preserves unrelated origin caches")
			shell_urls = inventory.get(status["shellCache"], [])
			check(set(shell_urls) == set(status["shellURLs"]),
				"install cache contains exactly the shell allowlist",
				{"actual": shell_urls, "expected": status["shellURLs"]})
			check(not any("/gamedata/" in url.lower() or url.lower().endswith(".data")
				for url in shell_urls), "precache excludes game data and preload bundles")

			manifest = page.evaluate("""async () => {
				const response = await fetch('/web/manifest.webmanifest');
				return { contentType: response.headers.get('content-type'), body: await response.json() };
			}""")
			check(manifest["contentType"].startswith("application/manifest+json"),
				"manifest uses the installable web-manifest MIME type", manifest["contentType"])
			check(manifest["body"]["display"] == "standalone" and manifest["body"]["icons"],
				"manifest declares standalone display and project icons")
			isolation = page.evaluate("""async () => {
				const result = {};
				for (const path of ['/web/index_webxr.html', '/web/service-worker.js', '/web/manifest.webmanifest']) {
					const response = await fetch(path);
					result[path] = {
						coop: response.headers.get('cross-origin-opener-policy'),
						coep: response.headers.get('cross-origin-embedder-policy'),
						corp: response.headers.get('cross-origin-resource-policy'),
						cacheControl: response.headers.get('cache-control'),
					};
				}
				return result;
			}""")
			check(all(headers["coop"] == "same-origin" and
				headers["coep"] == "require-corp" and headers["corp"] == "same-origin"
				for headers in isolation.values()),
				"launcher, worker, and manifest retain COOP/COEP/CORP isolation", isolation)
			check(isolation["/web/service-worker.js"]["cacheControl"] == "no-cache",
				"service-worker script is revalidated for updates",
				isolation["/web/service-worker.js"])

			no_data_result = page.evaluate("""async () => {
				const response = await fetch('/build-emscripten-nodata/SurrealEngine.js');
				return { status: response.status, bytes: (await response.arrayBuffer()).byteLength };
			}""")
			check(no_data_result["status"] == 200 and no_data_result["bytes"] > 0,
				"redistributable runtime asset is fetchable", no_data_result)
			inventory = cache_inventory(page)
			check(any(url.endswith("/build-emscripten-nodata/SurrealEngine.js")
				for url in inventory.get(status["runtimeCache"], [])),
				"allowlisted no-data JavaScript enters the runtime cache")

			dev_result = page.evaluate("""async () => {
				const response = await fetch('/build-emscripten/SurrealEngine.js');
				return { status: response.status, bytes: (await response.arrayBuffer()).byteLength };
			}""")
			check(dev_result["status"] == 200 and dev_result["bytes"] > 0,
				"development JavaScript remains network-accessible for local work", dev_result)
			inventory = cache_inventory(page)
			check(not any(url.endswith("/build-emscripten/SurrealEngine.js")
				for urls in inventory.values() for url in urls),
				"development preload build is never cached")

			blocked = page.evaluate("""async () => {
				const paths = [
					'/gamedata/System/Core.u',
					'/build-emscripten/SurrealEngine.data',
					'/user-content/DM-Test.unr',
				];
				return await Promise.all(paths.map(async path => {
					const response = await fetch(path);
					return { path, status: response.status, policy: response.headers.get('X-Surreal-PWA') };
				}));
			}""")
			check(all(item["status"] == 451 and item["policy"] == "blocked-commercial-data"
				for item in blocked), "commercial/imported-data URLs are explicitly rejected", blocked)
			inventory = cache_inventory(page)
			blocked_paths = [item["path"].lower() for item in blocked]
			check(not any(any(url.lower().endswith(path) for path in blocked_paths)
				for urls in inventory.values() for url in urls),
				"rejected commercial/imported-data URLs never enter Cache Storage")

			page.evaluate("""async cacheName => {
				await (await caches.open(cacheName)).delete('/web/index_webxr.html');
			}""", status["shellCache"])
			html_update = page.evaluate("""async () => {
				const response = await fetch('/web/index_webxr.html?update-probe=1');
				return { status: response.status, text: await response.text() };
			}""")
			check(html_update["status"] == 200 and "SurrealEngine WebXR" in html_update["text"],
				"launcher HTML uses the network while online")
			inventory = cache_inventory(page)
			check(any(url.endswith("/web/index_webxr.html") for url in inventory[status["shellCache"]]),
				"online HTML update refreshes the canonical offline entry")

			context.set_offline(True)
			offline_shell = page.evaluate("""async () => {
				const response = await fetch('/web/index_webxr.html?offline-probe=1');
				return { status: response.status, text: await response.text() };
			}""")
			check(offline_shell["status"] == 200 and "SurrealEngine WebXR" in offline_shell["text"],
				"cached launcher remains available offline")
			offline_response = page.goto(BASE_URL + "/web/not-in-shell", wait_until="load")
			check(offline_response is not None and offline_response.status == 503,
				"uncached offline navigation returns an explicit error status")
			check("offline-shell" in page.locator("body").inner_text(),
				"uncached offline navigation renders diagnostic guidance")
			context.set_offline(False)

			check(not page_errors, "PWA test host has no unexpected page errors", page_errors)

			# A fresh profile proves that even an explicit pwa=1 cannot register the
			# worker while the commercial development preload route is selected.
			dev_context = browser.new_context(service_workers="allow")
			dev_page = dev_context.new_page()
			dev_page.goto(INDEX_URL + "?pwa=1&build=build-emscripten", wait_until="domcontentloaded")
			dev_diagnostics = dev_page.evaluate("window.surrealPWAPromise")
			dev_registrations = dev_page.evaluate("navigator.serviceWorker.getRegistrations().then(r => r.length)")
			check(dev_diagnostics["state"] == "refused-development-preload" and dev_registrations == 0,
				"development preload route refuses service-worker registration", dev_diagnostics)
			dev_context.close()

			print(json.dumps({"version": status["version"], "baseURL": BASE_URL}, indent=2))
		finally:
			context.set_offline(False)
			try:
				page.goto(TEST_URL, wait_until="domcontentloaded", timeout=5000)
				page.evaluate("""async () => {
					for (const registration of await navigator.serviceWorker.getRegistrations()) {
						await registration.unregister();
					}
					for (const name of await caches.keys()) await caches.delete(name);
				}""")
			except Exception as error:
				print("WARN: PWA test cleanup failed: " + str(error), file=sys.stderr)
			context.close()
			browser.close()


if __name__ == "__main__":
	try:
		main()
	except Exception as error:
		print("FAIL: " + str(error), file=sys.stderr)
		raise
