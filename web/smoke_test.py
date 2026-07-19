import sys
import time
from playwright.sync_api import sync_playwright

sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")

URL = "http://localhost:8091/web/index.html"

with sync_playwright() as p:
	browser = p.chromium.launch()
	page = browser.new_page()

	console_lines = []
	page.on("console", lambda msg: console_lines.append(msg.text))
	page_errors = []
	page.on("pageerror", lambda exc: page_errors.append(str(exc)))

	page.goto(URL, wait_until="load")

	# Wait for boot (package scan + map load can take a while with a 629MB
	# preload package to unpack into MEMFS).
	deadline = time.time() + 120
	booted = False
	while time.time() < deadline:
		booted = page.evaluate("window.surrealBooted === true")
		crashed = page.evaluate("window.surrealCrashed")
		if crashed:
			print(f"CRASHED during boot: {crashed}")
			print("--- surrealLog (Module.print/printErr output) ---")
			print("\n".join(page.evaluate("window.surrealLog") or []))
			print("--- console ---")
			print("\n".join(console_lines[-80:]))
			print("--- pageerrors ---")
			print("\n".join(page_errors))
			sys.exit(1)
		if booted:
			break
		time.sleep(1)

	if not booted:
		print("TIMEOUT waiting for boot (onRuntimeInitialized never fired)")
		print("\n".join(console_lines[-50:]))
		sys.exit(1)

	print("[harness] booted, watching tick counter for 10s...")
	samples = []
	for _ in range(10):
		time.sleep(1)
		crashed = page.evaluate("window.surrealCrashed")
		if crashed:
			print(f"CRASHED after boot: {crashed}")
			print("\n".join(console_lines[-80:]))
			sys.exit(1)
		tick = page.evaluate("window.surrealGetTickCount ? window.surrealGetTickCount() : -1")
		samples.append(tick)
		print(f"  tick count: {tick}")

	print("\n".join(console_lines[-80:]))

	if len(set(samples)) <= 1:
		print(f"FAIL: tick counter did not advance, samples={samples}")
		sys.exit(1)

	print("[harness] tick counter is advancing, requesting quit...")
	page.evaluate("window.surrealRequestQuit()")
	time.sleep(2)
	tick_after_quit_1 = page.evaluate("window.surrealGetTickCount()")
	time.sleep(2)
	tick_after_quit_2 = page.evaluate("window.surrealGetTickCount()")

	page_err = page.evaluate("window.surrealCrashed")
	print(f"tick right after quit request: {tick_after_quit_1}")
	print(f"tick 2s later: {tick_after_quit_2}")
	print(f"page-level crash flag: {page_err}")
	print(f"unhandled page errors: {page_errors}")

	if tick_after_quit_2 != tick_after_quit_1:
		print("FAIL: engine kept ticking after quit was requested")
		sys.exit(1)

	print("PASS: booted, ticked live, quit cleanly, no uncaught JS errors" if not page_errors and not page_err else "PASS with warnings (see above)")
	browser.close()
