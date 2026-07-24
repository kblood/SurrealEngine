"""Qualify WebGL2 reloads, mutable persistence, demo maps, and sustained flat play."""

import json
import os
import re
import time
from pathlib import Path
from urllib.parse import quote

from PIL import Image
from playwright.sync_api import sync_playwright


BASE_URL = os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/")
ENGINE_BASE = os.environ.get("SURREAL_WEB_ENGINE_BASE", "/build-emscripten/")
HARNESS_PATH = os.environ.get("SURREAL_WEBGL2_HARNESS_PATH", "/web/index_webgl2.html")
RESULT_PATH = os.environ.get("SURREAL_WEBGL2_MATRIX_RESULT")
SCREENSHOT_DIRECTORY = Path(os.environ.get("SURREAL_WEBGL2_MATRIX_SCREENSHOTS", "web/webgl2-matrix"))
RELOAD_COUNT = int(os.environ.get("SURREAL_WEBGL2_RELOAD_COUNT", "10"))
SUSTAINED_SECONDS = int(os.environ.get("SURREAL_WEBGL2_SUSTAINED_SECONDS", "30"))
MAPS = [value.strip() for value in os.environ.get("SURREAL_WEBGL2_MAPS",
	"DM-TurbineDEMO,DM-MorpheusDEMO,DM-PhobosDEMO,DM-TempestDEMO,CTF-CoretDEMO,DOM-SesmarDEMO").split(",") if value.strip()]


def game_url(map_name):
	return (f"{BASE_URL}{HARNESS_PATH}?engineBase={quote(ENGINE_BASE, safe='/')}"
		f"&map={quote(map_name, safe='-')}")


def wait_until(page, expression, timeout=120):
	deadline = time.time() + timeout
	while time.time() < deadline:
		if page.evaluate(expression):
			return True
		time.sleep(0.2)
	return False


def wait_for_game(page, page_errors, map_name):
	started = time.perf_counter()
	if not wait_until(page, "window.surrealBooted || window.surrealCrashed"):
		raise RuntimeError(f"timed out booting {map_name}")
	crashed = page.evaluate("window.surrealCrashed")
	if crashed:
		raise RuntimeError(f"{map_name} crashed: {crashed}")
	if page_errors:
		raise RuntimeError(f"{map_name} raised page errors: {page_errors}")
	if not wait_until(page, "window.surrealGetWebGL2Diagnostics().drawCalls > 0 || (window.surrealLog || []).some(line => String(line).includes('Fatal error:'))", timeout=30) or \
			page.evaluate("window.surrealGetWebGL2Diagnostics().drawCalls") <= 0:
		diagnostic = page.evaluate("""() => ({
			renderer: window.surrealGetWebGL2Diagnostics(),
			data: window.surrealBrowserDataController && window.surrealBrowserDataController.status(),
			logTail: (window.surrealLog || []).slice(-80),
		})""")
		raise RuntimeError(f"{map_name} produced no WebGL2 draws: {json.dumps(diagnostic)}")
	first_tick = page.evaluate("window.surrealGetTickCount()")
	time.sleep(0.5)
	second_tick = page.evaluate("window.surrealGetTickCount()")
	if second_tick <= first_tick:
		raise RuntimeError(f"{map_name} did not advance simulation ticks")
	return round((time.perf_counter() - started) * 1000, 1)


def non_blank_percent(path):
	image = Image.open(path).convert("RGB")
	pixels = list(image.getdata())
	background = pixels[0]
	return round(100.0 * sum(pixel != background for pixel in pixels) / len(pixels), 2)


def snapshot(page):
	return page.evaluate("""() => ({
		tick: window.surrealGetTickCount(),
		renderer: window.surrealGetWebGL2Diagnostics(),
		wasmHeapBytes: Module.ccall('Surreal_GetBrowserWasmHeapSize', 'number', [], []),
		mutable: window.surrealBrowserDataController.status().mutable,
	})""")


SCREENSHOT_DIRECTORY.mkdir(parents=True, exist_ok=True)
result = {
	"schema": "surrealengine-webgl2-flat-matrix-v1",
	"reloads": [],
	"persistence": {},
	"maps": [],
	"mapExclusions": [],
	"sustained": {},
}

with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True,
		args=["--autoplay-policy=user-gesture-required"])
	context = browser.new_context(viewport={"width": 1280, "height": 800})
	page = context.new_page()
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))

	# A single origin/context exercises warm-cache reload while creating a fresh
	# WASM runtime on every navigation. Every generation must independently boot,
	# render, and tick without inheriting JS state from the previous page.
	for index in range(RELOAD_COUNT):
		page_errors.clear()
		started = time.perf_counter()
		page.goto(game_url("DM-TurbineDEMO"), wait_until="load")
		boot_ms = wait_for_game(page, page_errors, "DM-TurbineDEMO")
		diagnostic = snapshot(page)
		result["reloads"].append({
			"iteration": index + 1,
			"navigationAndBootMs": round((time.perf_counter() - started) * 1000, 1),
			"engineReadyMs": boot_ms,
			"tick": diagnostic["tick"],
			"drawCalls": diagnostic["renderer"]["drawCalls"],
			"textures": diagnostic["renderer"]["textures"],
			"errors": diagnostic["renderer"]["errors"],
		})
		if diagnostic["renderer"]["errors"] != 0 or diagnostic["renderer"]["unsupportedDraws"] != 0:
			raise RuntimeError(f"reload {index + 1} reported renderer errors")

	# Checkpoint an allowlisted save-like file, rebuild the entire page/WASM
	# runtime, and require the mutable overlay to restore the exact bytes before
	# the new game continues. The test uses a dedicated localhost origin.
	sentinel_path = "/home/web_user/.config/SurrealEngine/Settings.json"
	sentinel_text = "surreal-webgl2-persistence-425f7873"
	page.evaluate("window.surrealBrowserDataController.clearMutableData()")
	page.evaluate("([path, text]) => FS.writeFile(path, new TextEncoder().encode(text))",
		[sentinel_path, sentinel_text])
	flush = page.evaluate("window.surrealBrowserDataController.flush('wp2-matrix')")
	page_errors.clear()
	page.goto(game_url("DM-TurbineDEMO"), wait_until="load")
	wait_for_game(page, page_errors, "DM-TurbineDEMO persistence reload")
	restored = page.evaluate("""path => {
		try { return new TextDecoder().decode(FS.readFile(path)); }
		catch (_) { return null; }
	}""", sentinel_path)
	mutable = page.evaluate("window.surrealBrowserDataController.status().mutable")
	result["persistence"] = {
		"path": sentinel_path,
		"flushState": flush["state"],
		"flushFileCount": flush["fileCount"],
		"restoreState": mutable["state"],
		"restoreFileCount": mutable["fileCount"],
		"exactBytesRestored": restored == sentinel_text,
	}
	if restored != sentinel_text or mutable["state"] != "ready":
		raise RuntimeError("mutable save sentinel did not survive a full page/WASM reload")
	page.evaluate("window.surrealBrowserDataController.clearMutableData()")
	page.evaluate("path => { try { FS.unlink(path); } catch (_) {} }", sentinel_path)

	# Qualify each representative demo map independently. This catches missing
	# texture/blend/geometry paths that a single DM-Turbine first frame cannot.
	for map_name in MAPS:
		page_errors.clear()
		page.goto(game_url(map_name), wait_until="load")
		try:
			boot_ms = wait_for_game(page, page_errors, map_name)
		except RuntimeError as error:
			message = str(error)
			missing = re.search(r"Could not find package ([A-Za-z0-9_.-]+)", message)
			if not missing:
				raise
			result["mapExclusions"].append({
				"map": map_name,
				"classification": "fixture-missing-package",
				"package": missing.group(1),
			})
			continue
		diagnostic = snapshot(page)
		screenshot = SCREENSHOT_DIRECTORY / f"{map_name}.png"
		page.locator("#canvas").screenshot(path=str(screenshot))
		non_blank = non_blank_percent(screenshot)
		log = "\n".join(page.evaluate("window.surrealLog"))
		entry = {
			"map": map_name,
			"bootMs": boot_ms,
			"tick": diagnostic["tick"],
			"drawCalls": diagnostic["renderer"]["drawCalls"],
			"textures": diagnostic["renderer"]["textures"],
			"unsupportedDraws": diagnostic["renderer"]["unsupportedDraws"],
			"errors": diagnostic["renderer"]["errors"],
			"nonBlankPercent": non_blank,
			"requestedMapObserved": map_name.lower() in log.lower(),
			"screenshot": str(screenshot),
		}
		result["maps"].append(entry)
		if (entry["drawCalls"] <= 0 or entry["textures"] < 5 or entry["unsupportedDraws"] != 0 or
				entry["errors"] != 0 or entry["nonBlankPercent"] < 5 or not entry["requestedMapObserved"]):
			raise RuntimeError(f"map qualification failed: {entry}")
	if len(result["maps"]) < 2:
		raise RuntimeError(f"fewer than two fixture-complete maps rendered: {result['mapExclusions']}")

	# Sample a live map for long enough to expose main-loop stalls, runaway
	# texture growth, or WASM heap growth. This is a regression baseline, not a
	# device-independent frame-rate benchmark.
	page_errors.clear()
	page.goto(game_url("DM-TurbineDEMO"), wait_until="load")
	wait_for_game(page, page_errors, "DM-TurbineDEMO sustained run")
	time.sleep(3)
	samples = [snapshot(page)]
	started = time.perf_counter()
	for _ in range(SUSTAINED_SECONDS):
		time.sleep(1)
		samples.append(snapshot(page))
	elapsed = time.perf_counter() - started
	frame_deltas = [samples[index]["renderer"]["frames"] - samples[index - 1]["renderer"]["frames"]
		for index in range(1, len(samples))]
	tick_deltas = [samples[index]["tick"] - samples[index - 1]["tick"]
		for index in range(1, len(samples))]
	result["sustained"] = {
		"requestedSeconds": SUSTAINED_SECONDS,
		"elapsedSeconds": round(elapsed, 3),
		"frameCount": sum(frame_deltas),
		"averageFramesPerSecond": round(sum(frame_deltas) / elapsed, 2),
		"minimumFramesPerSample": min(frame_deltas),
		"tickCount": sum(tick_deltas),
		"minimumTicksPerSample": min(tick_deltas),
		"wasmHeapBytesBefore": samples[0]["wasmHeapBytes"],
		"wasmHeapBytesAfter": samples[-1]["wasmHeapBytes"],
		"textureCountBefore": samples[0]["renderer"]["textures"],
		"textureCountAfter": samples[-1]["renderer"]["textures"],
		"errors": samples[-1]["renderer"]["errors"],
		"unsupportedDraws": samples[-1]["renderer"]["unsupportedDraws"],
	}
	sustained = result["sustained"]
	if (sustained["minimumFramesPerSample"] <= 0 or sustained["minimumTicksPerSample"] <= 0 or
			sustained["averageFramesPerSecond"] < 20 or sustained["errors"] != 0 or
			sustained["unsupportedDraws"] != 0 or
			sustained["wasmHeapBytesAfter"] - sustained["wasmHeapBytesBefore"] > 128 * 1024 * 1024 or
			sustained["textureCountAfter"] - sustained["textureCountBefore"] > 100):
		raise RuntimeError(f"sustained flat qualification failed: {sustained}")
	if page_errors:
		raise RuntimeError(f"sustained run raised page errors: {page_errors}")

	page.evaluate("window.surrealRequestQuit()")
	context.close()
	browser.close()

result["status"] = "passed"
print(json.dumps(result, indent=2))
if RESULT_PATH:
	Path(RESULT_PATH).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
print(f"PASS: {RELOAD_COUNT} reloads, mutable restoration, {len(result['maps'])} rendered maps "
	 f"({len(result['mapExclusions'])} fixture exclusions), and {SUSTAINED_SECONDS}s sustained WebGL2 play")
