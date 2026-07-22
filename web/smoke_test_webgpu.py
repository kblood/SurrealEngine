import sys
import time
from urllib.parse import urlencode
from playwright.sync_api import sync_playwright
from PIL import Image

sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")

POSITIONAL_ARGS = [arg for arg in sys.argv[1:] if not arg.startswith("--")]
MAP_NAME = POSITIONAL_ARGS[0] if POSITIONAL_ARGS else "DM-Deck16]["
BUILD_DIR = next((arg.split("=", 1)[1] for arg in sys.argv[1:] if arg.startswith("--build=")), "build-emscripten")
URL = "http://localhost:8091/web/index_webgpu.html?" + urlencode({"map": MAP_NAME, "build": BUILD_DIR})

with sync_playwright() as p:
	# Playwright's bundled Chromium cannot acquire a WebGPU adapter headlessly
	# under any launch-flag combination (confirmed during the M2 spike). A
	# real Chrome/Edge install works with zero extra flags.
	browser = p.chromium.launch(channel="chrome")
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

	initial_audio_state = page.evaluate("window.surrealGetWebAudioState()")
	print(f"[harness] Web Audio state before trusted click: {initial_audio_state}")
	if initial_audio_state == 0 or initial_audio_state == 3:
		print("FAIL: Emscripten OpenAL did not create a live browser AudioContext")
		sys.exit(1)
	# UT's canvas captures the pointer after boot. Release it before a normal
	# Playwright mouse click so the button is the actual trusted-event target.
	page.evaluate("document.exitPointerLock()")
	page.click("#enableaudio")
	deadline = time.time() + 5
	while time.time() < deadline:
		audio_state = page.evaluate("window.surrealGetWebAudioState()")
		if audio_state == 2:
			break
		time.sleep(0.1)
	else:
		audio_error = page.evaluate("Module.surrealWebAudioLastError || ''")
		print(f"FAIL: Web Audio did not reach running state after trusted click; state={audio_state}, error={audio_error}")
		sys.exit(1)
	print(f"[harness] Web Audio running after click; resume completions={page.evaluate('window.surrealGetWebAudioResumeCount()')}")

	print(f"[harness] booted {MAP_NAME}, watching tick counter for 10s...")
	samples = []
	initial_tick = page.evaluate("window.surrealGetTickCount ? window.surrealGetTickCount() : -1")
	sample_start = time.perf_counter()
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
	sample_seconds = time.perf_counter() - sample_start
	ticks_per_second = (samples[-1] - initial_tick) / sample_seconds
	print(f"[harness] observed engine tick rate: {ticks_per_second:.1f}/s")

	print("\n".join(console_lines[-80:]))

	if len(set(samples)) <= 1:
		print(f"FAIL: tick counter did not advance, samples={samples}")
		sys.exit(1)

	# M2 "definition of done" checklist (WEBXR_IMPLEMENTATION_PLAN.md / the
	# approved M2 plan, section 8): error count, draw-call count, and
	# texture-cache count are read via the Surreal_GetWebGPU*() exports
	# (WebGPURenderDevice.cpp). Non-blank output is checked here in Python
	# against the canvas screenshot instead of a synchronous C++ ReadPixels
	# export - WebGPU buffer mapping is async-only and this build doesn't use
	# Asyncify, so an in-engine readback isn't straightforwardly available.
	error_count = page.evaluate("window.surrealGetWebGPUErrorCount()")
	draw_calls = page.evaluate("window.surrealGetWebGPUDrawCalls()")
	texture_count = page.evaluate("window.surrealGetWebGPUTextureCount()")
	bind_groups_created = page.evaluate("window.surrealGetWebGPUBindGroupsCreated()")
	bind_group_cache_hits = page.evaluate("window.surrealGetWebGPUBindGroupCacheHits()")
	buffer_rollovers = page.evaluate("window.surrealGetWebGPUBufferRollovers()")
	print(f"[harness] WebGPU error count: {error_count}")
	print(f"[harness] WebGPU draw calls (last frame): {draw_calls}")
	print(f"[harness] WebGPU textures cached: {texture_count}")
	print(f"[harness] WebGPU bind groups created (last frame): {bind_groups_created}")
	print(f"[harness] WebGPU bind-group cache hits (last frame): {bind_group_cache_hits}")
	print(f"[harness] WebGPU geometry-buffer rollovers (last frame): {buffer_rollovers}")

	if error_count != 0:
		print(f"FAIL: {error_count} WebGPU uncaptured error(s) during the run")
		sys.exit(1)
	if draw_calls <= 0:
		print(f"FAIL: draw call count is {draw_calls}, expected > 0")
		sys.exit(1)
	if texture_count < 5:
		print(f"FAIL: only {texture_count} textures cached, expected several (real P8/BGRA8_LM conversion path)")
		sys.exit(1)
	if bind_group_cache_hits <= 0:
		print("FAIL: bind-group cache had no hits in the last frame")
		sys.exit(1)
	if bind_groups_created >= draw_calls:
		print(f"FAIL: created {bind_groups_created} bind groups for {draw_calls} draws; cache did not reduce per-draw allocation")
		sys.exit(1)

	screenshot_path = "web/webgpu_smoke_screenshot.png"
	page.locator("#canvas").screenshot(path=screenshot_path)
	print(f"[harness] saved screenshot to {screenshot_path}")

	img = Image.open(screenshot_path).convert("RGB")
	pixels = list(img.getdata())
	reference = pixels[0]
	non_blank_percent = 100.0 * sum(1 for p in pixels if p != reference) / len(pixels)
	print(f"[harness] non-blank pixel percent: {non_blank_percent:.1f}%")
	if non_blank_percent < 5.0:
		print(f"FAIL: screenshot looks blank ({non_blank_percent:.1f}% non-background pixels) - rendering may not be reaching the canvas")
		sys.exit(1)

	print("[harness] requesting quit...")
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

	print("PASS: booted with --render=webgpu, ticked live, quit cleanly, no uncaught JS errors" if not page_errors and not page_err else "PASS with warnings (see above)")
	browser.close()
