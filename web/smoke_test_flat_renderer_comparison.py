"""Compare the supported flat WebGL2 baseline with the existing WebGPU profile."""

import json
import os
import time
from pathlib import Path
from urllib.parse import quote

from PIL import Image
from playwright.sync_api import sync_playwright


BASE_URL = os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/")
ENGINE_BASE = os.environ.get("SURREAL_WEB_ENGINE_BASE", "/build-emscripten/")
WEBGL2_HARNESS = os.environ.get("SURREAL_WEBGL2_HARNESS_PATH", "/web/index_webgl2.html")
WEBGPU_HARNESS = os.environ.get("SURREAL_WEBGPU_HARNESS_PATH", "/web/index_webgpu.html")
MAP_NAME = os.environ.get("SURREAL_WEB_MAP", "DM-TurbineDEMO")
SAMPLE_SECONDS = int(os.environ.get("SURREAL_RENDERER_COMPARISON_SECONDS", "15"))
RESULT_PATH = os.environ.get("SURREAL_RENDERER_COMPARISON_RESULT")
SCREENSHOT_DIRECTORY = Path(os.environ.get("SURREAL_RENDERER_COMPARISON_SCREENSHOTS", "web/flat-renderer-comparison"))
HEADLESS = os.environ.get("SURREAL_WEBGPU_HEADLESS", "0") == "1"


def url(harness):
	return (f"{BASE_URL}{harness}?engineBase={quote(ENGINE_BASE, safe='/')}"
		f"&map={quote(MAP_NAME, safe='-')}")


def wait_until(page, expression, timeout=120):
	deadline = time.time() + timeout
	while time.time() < deadline:
		if page.evaluate(expression):
			return True
		time.sleep(0.2)
	return False


def wait_for_game(page, errors, renderer):
	started = time.perf_counter()
	if not wait_until(page, "window.surrealBooted || window.surrealCrashed"):
		raise RuntimeError(f"{renderer} timed out during startup")
	crashed = page.evaluate("window.surrealCrashed")
	if crashed:
		raise RuntimeError(f"{renderer} crashed: {crashed}")
	if errors:
		raise RuntimeError(f"{renderer} raised page errors: {errors}")
	return round((time.perf_counter() - started) * 1000, 1)


def non_blank_percent(path):
	image = Image.open(path).convert("RGB")
	pixels = list(image.getdata())
	background = pixels[0]
	return round(100.0 * sum(pixel != background for pixel in pixels) / len(pixels), 2)


def image_difference(first_path, second_path):
	first = Image.open(first_path).convert("RGB")
	second = Image.open(second_path).convert("RGB")
	if first.size != second.size:
		return {"sameDimensions": False, "firstSize": list(first.size), "secondSize": list(second.size)}
	first_pixels = list(first.getdata())
	second_pixels = list(second.getdata())
	total_difference = 0
	different_pixels = 0
	maximum_channel_difference = 0
	for first_pixel, second_pixel in zip(first_pixels, second_pixels):
		channels = [abs(first_pixel[index] - second_pixel[index]) for index in range(3)]
		total_difference += sum(channels)
		maximum_channel_difference = max(maximum_channel_difference, *channels)
		if channels != [0, 0, 0]:
			different_pixels += 1
	return {
		"sameDimensions": True,
		"width": first.width,
		"height": first.height,
		"meanAbsoluteChannelDifference": round(total_difference / (len(first_pixels) * 3), 4),
		"differentPixelPercent": round(100.0 * different_pixels / len(first_pixels), 4),
		"maximumChannelDifference": maximum_channel_difference,
	}


def sample_ticks(page, seconds):
	start_tick = page.evaluate("window.surrealGetTickCount()")
	started = time.perf_counter()
	minimum_delta = None
	previous = start_tick
	for _ in range(seconds):
		time.sleep(1)
		current = page.evaluate("window.surrealGetTickCount()")
		delta = current - previous
		minimum_delta = delta if minimum_delta is None else min(minimum_delta, delta)
		previous = current
	elapsed = time.perf_counter() - started
	return {
		"seconds": round(elapsed, 3),
		"ticks": previous - start_tick,
		"ticksPerSecond": round((previous - start_tick) / elapsed, 2),
		"minimumTicksPerSample": minimum_delta,
	}


SCREENSHOT_DIRECTORY.mkdir(parents=True, exist_ok=True)
result = {
	"schema": "surrealengine-flat-renderer-comparison-v1",
	"map": MAP_NAME,
	"sampleSeconds": SAMPLE_SECONDS,
	"headless": HEADLESS,
	"renderers": {},
}

with sync_playwright() as playwright:
	# The pinned WebGPU stack has historically required headed Chrome on this
	# Windows qualification host. SURREAL_WEBGPU_HEADLESS=1 remains available
	# for machines whose Chrome/driver tuple exposes WebGPU under headless mode.
	browser = playwright.chromium.launch(channel="chrome", headless=HEADLESS,
		args=["--autoplay-policy=no-user-gesture-required"])
	context = browser.new_context(viewport={"width": 1280, "height": 800})
	page = context.new_page()
	errors = []
	page.on("pageerror", lambda error: errors.append(str(error)))

	page.goto(url(WEBGL2_HARNESS), wait_until="load")
	webgl2_boot_ms = wait_for_game(page, errors, "webgl2")
	if not wait_until(page, "window.surrealGetWebGL2Diagnostics().drawCalls > 0", timeout=30):
		raise RuntimeError("WebGL2 comparison run produced no draws")
	webgl2_sample = sample_ticks(page, SAMPLE_SECONDS)
	webgl2_diagnostics = page.evaluate("window.surrealGetWebGL2Diagnostics()")
	webgl2_screenshot = SCREENSHOT_DIRECTORY / "webgl2.png"
	page.locator("#canvas").evaluate("canvas => { canvas.style.width = '640px'; canvas.style.height = '480px'; }")
	page.locator("#canvas").screenshot(path=str(webgl2_screenshot))
	result["renderers"]["webgl2"] = {
		"bootMs": webgl2_boot_ms,
		"sample": webgl2_sample,
		"drawCalls": webgl2_diagnostics["drawCalls"],
		"submissions": webgl2_diagnostics["submissions"],
		"textures": webgl2_diagnostics["textures"],
		"errors": webgl2_diagnostics["errors"],
		"unsupportedDraws": webgl2_diagnostics["unsupportedDraws"],
		"nonBlankPercent": non_blank_percent(webgl2_screenshot),
		"screenshot": str(webgl2_screenshot),
	}
	page.evaluate("window.surrealRequestQuit()")

	errors.clear()
	page.goto(url(WEBGPU_HARNESS), wait_until="load")
	webgpu_boot_ms = wait_for_game(page, errors, "webgpu")
	if not wait_until(page, "window.surrealGetWebGPUDrawCalls() > 0", timeout=30):
		raise RuntimeError("WebGPU comparison run produced no draws")
	webgpu_sample = sample_ticks(page, SAMPLE_SECONDS)
	webgpu_diagnostics = page.evaluate("""() => ({
		drawCalls: window.surrealGetWebGPUDrawCalls(),
		textures: window.surrealGetWebGPUTextureCount(),
		errors: window.surrealGetWebGPUErrorCount(),
	})""")
	webgpu_screenshot = SCREENSHOT_DIRECTORY / "webgpu.png"
	page.locator("#canvas").evaluate("canvas => { canvas.style.width = '640px'; canvas.style.height = '480px'; }")
	page.locator("#canvas").screenshot(path=str(webgpu_screenshot))
	result["renderers"]["webgpu"] = {
		"bootMs": webgpu_boot_ms,
		"sample": webgpu_sample,
		"drawCalls": webgpu_diagnostics["drawCalls"],
		"textures": webgpu_diagnostics["textures"],
		"errors": webgpu_diagnostics["errors"],
		"nonBlankPercent": non_blank_percent(webgpu_screenshot),
		"screenshot": str(webgpu_screenshot),
	}
	page.evaluate("window.surrealRequestQuit()")
	context.close()
	browser.close()

webgl2 = result["renderers"]["webgl2"]
webgpu = result["renderers"]["webgpu"]
result["webgl2ToWebgpuTickRateRatio"] = round(
	webgl2["sample"]["ticksPerSecond"] / webgpu["sample"]["ticksPerSecond"], 3)
result["visualComparison"] = image_difference(
	webgl2["screenshot"], webgpu["screenshot"])
if (webgl2["sample"]["ticksPerSecond"] < 20 or webgpu["sample"]["ticksPerSecond"] < 20 or
		webgl2["sample"]["minimumTicksPerSample"] <= 0 or webgpu["sample"]["minimumTicksPerSample"] <= 0 or
		webgl2["errors"] != 0 or webgl2["unsupportedDraws"] != 0 or webgpu["errors"] != 0 or
		webgl2["drawCalls"] <= 0 or webgl2["submissions"] <= 0 or webgl2["submissions"] >= webgl2["drawCalls"] or
		webgpu["drawCalls"] <= 0 or
		webgl2["textures"] < 5 or webgpu["textures"] < 5 or
		webgl2["nonBlankPercent"] < 5 or webgpu["nonBlankPercent"] < 5 or
		result["webgl2ToWebgpuTickRateRatio"] < 0.5 or
		not result["visualComparison"]["sameDimensions"] or
		result["visualComparison"]["meanAbsoluteChannelDifference"] > 15):
	raise RuntimeError("flat renderer comparison gate failed: " + json.dumps(result))

result["status"] = "passed"
print(json.dumps(result, indent=2))
if RESULT_PATH:
	Path(RESULT_PATH).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
print("PASS: WebGL2 and WebGPU both rendered and ticked without backend errors")
