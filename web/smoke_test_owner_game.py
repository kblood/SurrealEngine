"""Launch an owner-supplied UE1 installation through the packaged browser UI.

The selected game data remains outside the repository. Chromium grants the
test page access to the directory, and the normal browser importer handles it
exactly as it does for a user-selected folder.
"""

import argparse
import json
import sys
import tempfile
import time
from pathlib import Path

from PIL import Image
from playwright.sync_api import TimeoutError as PlaywrightTimeoutError
from playwright.sync_api import sync_playwright


def parse_args():
	parser = argparse.ArgumentParser()
	parser.add_argument("--game-dir", required=True, type=Path)
	parser.add_argument("--base-url", default="http://localhost:8091")
	parser.add_argument("--expected-game", choices=("ut99", "unreal-gold"))
	parser.add_argument("--renderer", choices=("webgpu", "null"), default="webgpu")
	parser.add_argument("--timeout-minutes", type=float, default=15.0)
	parser.add_argument("--headed", action="store_true")
	parser.add_argument("--profile-dir", type=Path)
	parser.add_argument("--screenshot", type=Path)
	return parser.parse_args()


def directory_summary(root):
	files = [path for path in root.rglob("*") if path.is_file()]
	return {"files": len(files), "bytes": sum(path.stat().st_size for path in files)}


def wait_for_advancing_ticks(page, timeout_ms):
	deadline = time.time() + timeout_ms / 1000
	samples = []
	while time.time() < deadline:
		crash = page.evaluate("window.surrealCrashed || null")
		if crash:
			raise RuntimeError("engine crashed: " + str(crash))
		value = page.evaluate("""() => {
			try { return Module.ccall('Surreal_GetTickCount', 'number', [], []); }
			catch (_) { return null; }
		}""")
		if value is not None:
			samples.append(value)
			if len(samples) >= 3 and len(set(samples[-3:])) > 1:
				return samples
		time.sleep(1)
	raise RuntimeError("engine tick counter did not advance: " + repr(samples))


def optional_counter(page, export_name):
	return page.evaluate("""name => {
		try { return Module.ccall(name, 'number', [], []); }
		catch (_) { return null; }
	}""", export_name)


def main():
	args = parse_args()
	game_dir = args.game_dir.resolve()
	if not game_dir.is_dir():
		raise SystemExit("Game directory does not exist: " + str(game_dir))
	summary = directory_summary(game_dir)
	if not summary["files"]:
		raise SystemExit("Game directory contains no files: " + str(game_dir))

	base_url = args.base_url.rstrip("/")
	timeout_ms = int(args.timeout_minutes * 60_000)
	with sync_playwright() as playwright:
		browser = None
		if args.profile_dir:
			args.profile_dir.mkdir(parents=True, exist_ok=True)
			context = playwright.chromium.launch_persistent_context(
				str(args.profile_dir.resolve()), channel="chrome", headless=not args.headed,
				service_workers="block", viewport={"width": 1280, "height": 800})
		else:
			browser = playwright.chromium.launch(channel="chrome", headless=not args.headed)
			context = browser.new_context(service_workers="block", viewport={"width": 1280, "height": 800})
		page = context.new_page()
		console_lines = []
		page_errors = []
		page.on("console", lambda message: console_lines.append(message.text))
		page.on("pageerror", lambda error: page_errors.append(str(error)))

		page.goto(base_url + "/", wait_until="load", timeout=60_000)
		page.wait_for_function("""() => window.surrealApp !== undefined ||
			!document.getElementById('game-launcher').hidden ||
			(!document.querySelector('[data-game-error]').hidden && document.querySelector('[data-game-error]').textContent)""",
			timeout=timeout_ms)
		restored = page.locator("#game-launcher").is_visible()
		initial = page.evaluate("""() => ({
			crossOriginIsolated,
			state: window.surrealApp && window.surrealApp.result.import.state,
			canvasHidden: document.getElementById('canvas').hidden,
		})""")
		if (not initial["crossOriginIsolated"] or
			(not restored and initial["state"] != "waiting-for-import") or not initial["canvasHidden"]):
			raise RuntimeError("unexpected pre-import state: " + json.dumps(initial))

		if not restored:
			page.locator("[data-game-files]").set_input_files(str(game_dir), timeout=60_000)
			try:
				page.wait_for_function("""() => {
					const launcher = document.getElementById('game-launcher');
					const error = document.querySelector('[data-game-error]');
					return (launcher && !launcher.hidden) || (error && !error.hidden && error.textContent);
				}""", timeout=timeout_ms)
			except PlaywrightTimeoutError as error:
				state = page.evaluate("""() => ({
					status: document.querySelector('[data-game-status]').textContent,
					error: document.querySelector('[data-game-error]').textContent,
					log: document.getElementById('log').textContent,
				})""")
				raise RuntimeError("import did not reach the launcher: " + json.dumps(state)) from error
		failure = page.evaluate("""() => {
			const visible = document.querySelector('[data-game-error]');
			const controller = window.surrealApp && window.surrealApp.dataController.importController;
			const error = controller && controller.lastError;
			return visible && !visible.hidden && visible.textContent ? {
				visible: visible.textContent,
				status: document.querySelector('[data-game-status]').textContent,
				name: error && error.name,
				code: error && error.code,
				message: error && error.message,
				stack: error && error.stack,
				progress: document.querySelector('[data-game-progress-text]').textContent,
				storage: document.querySelector('[data-game-storage]').textContent,
				log: document.getElementById('log').textContent,
			} : null;
		}""")
		if failure:
			raise RuntimeError("browser import failed: " + json.dumps(failure))

		imported = page.evaluate("""() => ({
			gameId: window.surrealApp && window.surrealApp.library.status().activeGameId,
			gameName: document.querySelector('[data-launcher-game]').textContent,
			map: document.querySelector('[data-launcher-map]').value,
			status: document.querySelector('[data-game-status]').textContent,
			log: document.getElementById('log').textContent,
		})""")
		if args.expected_game and imported["gameId"] and imported["gameId"] != args.expected_game:
			raise RuntimeError("detected %s instead of %s" % (imported["gameId"], args.expected_game))

		page.select_option("[data-launcher-presentation]", "flat")
		page.select_option("[data-launcher-renderer]", args.renderer)
		page.check("[data-launcher-skip-intro]")
		page.locator("#game-launcher").scroll_into_view_if_needed()
		page.click("#game-launcher button[type=submit]")
		try:
			page.wait_for_function("window.surrealBooted === true", timeout=timeout_ms)
		except PlaywrightTimeoutError as error:
			startup = page.evaluate("""() => ({
				booted: window.surrealBooted === true,
				crashed: window.surrealCrashed || null,
				ticks: (() => { try { return Module.ccall('Surreal_GetTickCount', 'number', [], []); } catch (_) { return null; } })(),
				selection: window.surrealLaunchSelection || null,
				phase: document.querySelector('[data-app-phase]').textContent,
				status: document.querySelector('[data-game-status]').textContent,
				error: document.querySelector('[data-game-error]').textContent,
				log: document.getElementById('log').textContent,
			})""")
			raise RuntimeError("native startup did not complete: " + json.dumps(startup)) from error
		ticks = wait_for_advancing_ticks(page, min(timeout_ms, 120_000))
		time.sleep(10)

		layout = page.evaluate("""() => {
			const canvas = document.getElementById('canvas');
			const bounds = canvas.getBoundingClientRect();
			return { hidden: canvas.hidden, focused: document.activeElement === canvas,
				top: bounds.top, bottom: bounds.bottom, width: bounds.width, height: bounds.height,
				viewportWidth: innerWidth, viewportHeight: innerHeight, scrollY,
				webgpuSurfaceOwnsModuleCanvas:
					document.querySelector('[data-surreal-webgpu-canvas]') === Module.canvas };
		}""")
		if (layout["hidden"] or layout["top"] < -1 or layout["top"] >= layout["viewportHeight"] or
			layout["bottom"] <= 0 or layout["width"] <= 0 or layout["height"] <= 0 or
			not layout["webgpuSurfaceOwnsModuleCanvas"]):
			raise RuntimeError("launched canvas is outside the viewport: " + json.dumps(layout))

		screenshot = args.screenshot or Path(tempfile.gettempdir()) / "surrealengine-owner-game-smoke.png"
		screenshot.parent.mkdir(parents=True, exist_ok=True)
		page.locator("#canvas").screenshot(path=str(screenshot))
		image = Image.open(screenshot).convert("RGB")
		pixels = list(image.getdata())
		reference = pixels[0]
		nonuniform_percent = 100.0 * sum(pixel != reference for pixel in pixels) / len(pixels)
		if nonuniform_percent < 1.0:
			raise RuntimeError("canvas appears blank (%.2f%% nonuniform pixels)" % nonuniform_percent)

		result = {
			"gameDirectory": str(game_dir),
			"source": summary,
			"renderer": args.renderer,
			"restoredFromProfile": restored,
			"detected": imported,
			"ticks": ticks,
			"layout": layout,
			"nonuniformPercent": round(nonuniform_percent, 2),
			"webgpuErrors": optional_counter(page, "Surreal_GetWebGPUErrorCount"),
			"webgpuDrawCalls": optional_counter(page, "Surreal_GetWebGPUDrawCalls"),
			"webgpuTextures": optional_counter(page, "Surreal_GetWebGPUTextureCount"),
			"pageErrors": page_errors,
			"screenshot": str(screenshot.resolve()),
		}
		print(json.dumps(result, indent=2))
		if page_errors or (result["webgpuErrors"] not in (None, 0)):
			print("Recent browser console output:", file=sys.stderr)
			print("\n".join(console_lines[-80:]), file=sys.stderr)
			raise RuntimeError("browser errors occurred during owner-data launch")
		print("PASS: owner-supplied game imported recursively and launched in flat %s mode" % args.renderer)
		context.close()
		if browser:
			browser.close()


if __name__ == "__main__":
	main()
