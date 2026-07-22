import sys
import time
from playwright.sync_api import sync_playwright

URL = "http://localhost:8091/web/index.html"

with sync_playwright() as p:
	browser = p.chromium.launch()
	page = browser.new_page()

	page.on("console", lambda msg: print("[console]", msg.text))
	page.on("pageerror", lambda exc: print("[pageerror]", repr(exc), "\nstack:", getattr(exc, "stack", None)))
	page.on("crash", lambda: print("[crash] page crashed"))

	page.goto(URL, wait_until="load")

	deadline = time.time() + 60
	while time.time() < deadline:
		crashed = page.evaluate("window.surrealCrashed")
		if crashed:
			time.sleep(1)
			print("CRASHED:", crashed)
			print("--- surrealLog tail ---")
			print("\n".join((page.evaluate("window.surrealLog") or [])[-60:]))
			break
		booted = page.evaluate("window.surrealBooted === true")
		if booted:
			print("booted ok")
			break
		time.sleep(1)

	browser.close()
