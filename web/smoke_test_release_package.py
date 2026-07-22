"""Exercise a staged data-free flat/WebXR browser package."""
import json
import hashlib
import os
import sys
import time
from urllib.parse import urljoin
from playwright.sync_api import sync_playwright

base_url = next((arg.split("=", 1)[1].rstrip("/") for arg in sys.argv[1:] if arg.startswith("--base-url=")), os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/"))
with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	context = browser.new_context(service_workers="block")
	context.add_init_script("""
		Object.defineProperty(navigator, "xr", { configurable: true, value: undefined });
		for (const name of ["XRGPUBinding", "XRWebGPUBinding", "XRWebGLLayer"])
			Object.defineProperty(globalThis, name, { configurable: true, value: undefined });
		window.__flatAdapterRequests = [];
		const gpu = navigator.gpu;
		if (gpu && typeof gpu.requestAdapter === "function") {
			const requestAdapter = gpu.requestAdapter.bind(gpu);
			gpu.requestAdapter = options => {
				window.__flatAdapterRequests.push(options === undefined ? null : JSON.parse(JSON.stringify(options)));
				return requestAdapter(options);
			};
		}
	""")
	page = context.new_page()
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(base_url + "/", wait_until="load")
	deadline = time.time() + 60
	while time.time() < deadline:
		if page.evaluate("window.surrealApp || document.getElementById('log').textContent.includes('[launcher]')"):
			break
		time.sleep(0.1)
	manifest = page.evaluate("async () => (await fetch('release-manifest.json')).json()")
	compliance = page.evaluate("async () => (await fetch('source-compliance.json')).json()")
	result = page.evaluate("""() => ({
		ready: !!window.surrealApp,
		state: window.surrealApp && window.surrealApp.result.import.state,
		crossOriginIsolated,
		booted: window.surrealBooted === true,
		engineBase: document.documentElement.dataset.engineBase,
		registeredPresentations: window.surrealApp && window.surrealApp.registry.available().map(provider => provider.id),
		webXRAbsent: typeof navigator.xr === "undefined" && typeof XRGPUBinding === "undefined" &&
			typeof XRWebGPUBinding === "undefined" && typeof XRWebGLLayer === "undefined",
		adapterRequests: window.__flatAdapterRequests,
		presentations: Array.from(document.querySelectorAll('[data-launcher-presentation] option')).map(option => option.value),
		capabilities: Array.from(document.querySelectorAll('[data-capability-list] li')).map(item => item.textContent),
		phase: document.querySelector('[data-app-phase]').textContent,
		sourceUI: (() => {
			const footer = document.querySelector('[data-source-compliance]');
			const link = footer && footer.querySelector('a');
			return { visible: !!footer && !!(footer.offsetWidth || footer.offsetHeight || footer.getClientRects().length),
				href: link && link.getAttribute('href'), text: footer && footer.textContent };
		})(),
		pageErrors: [],
	})""")
	result["pageErrors"] = page_errors
	result["manifestSchema"] = manifest.get("schema")
	result["manifestGames"] = manifest.get("games")
	result["sourceArchiveSha256"] = compliance.get("archiveSha256")
	print(json.dumps(result, indent=2))
	failure = (not result["ready"] or result["state"] != "waiting-for-import" or result["booted"] or
		not result["crossOriginIsolated"] or result["engineBase"] != "./engine/" or
		not result["webXRAbsent"] or result["adapterRequests"] != [None] or
		result["registeredPresentations"] != ["flat"] or not result["sourceUI"]["visible"] or
		result["sourceUI"]["href"] != compliance.get("sourceUrl") or
		compliance.get("archiveSha256") not in (result["sourceUI"]["text"] or "") or
		result["manifestSchema"] != "surrealengine-browser-release-v1" or
		result["manifestGames"] != ["ut99", "unreal-gold"] or result["pageErrors"])
	if failure:
		print("FAIL: staged browser release package", file=sys.stderr)
		sys.exit(1)
	source_response = context.request.get(urljoin(base_url + "/", compliance["sourceUrl"]))
	if (not source_response.ok or len(source_response.body()) != compliance["archiveBytes"] or
		hashlib.sha256(source_response.body()).hexdigest() != compliance["archiveSha256"]):
		print("FAIL: visible corresponding-source link does not return the recorded archive", file=sys.stderr)
		sys.exit(1)

	page.set_viewport_size({"width": 1100, "height": 760})
	wide_canvas = page.locator("#game").bounding_box()
	page.set_viewport_size({"width": 700, "height": 520})
	narrow_canvas = page.locator("#game").bounding_box()
	if (not wide_canvas or not narrow_canvas or wide_canvas["width"] < 1090 or
		narrow_canvas["width"] < 690 or narrow_canvas["width"] >= wide_canvas["width"]):
		print("FAIL: flat canvas did not follow the browser viewport width", file=sys.stderr)
		sys.exit(1)

	fullscreen = page.evaluate("""async () => {
		const canvas = document.getElementById('game');
		if (!document.fullscreenEnabled || typeof canvas.requestFullscreen !== 'function')
			return { supported: false, entered: false };
		const button = document.createElement('button');
		button.id = 'flat-fullscreen-probe';
		button.textContent = 'Fullscreen probe';
		button.style.position = 'fixed'; button.style.inset = '0 auto auto 0'; button.style.zIndex = '10000';
		button.onclick = async () => { try { await canvas.requestFullscreen(); } catch (_) {} };
		document.body.appendChild(button);
		return { supported: true, entered: false };
	}""")
	if fullscreen["supported"]:
		page.click("#flat-fullscreen-probe")
		try:
			page.wait_for_function("document.fullscreenElement === document.getElementById('game')", timeout=5000)
		except Exception:
			pass
		fullscreen["entered"] = page.evaluate("document.fullscreenElement === document.getElementById('game')")
		if fullscreen["entered"]:
			page.evaluate("document.exitFullscreen()")
			page.wait_for_function("document.fullscreenElement === null", timeout=5000)
			fullscreen["exited"] = True
	if fullscreen["supported"] and not fullscreen["entered"]:
		print("FAIL: Chrome exposed fullscreen but the trusted probe could not enter it", file=sys.stderr)
		sys.exit(1)
	page.evaluate("""() => {
		Module.callMain = args => { window.syntheticCallMainArgs = Array.from(args); };
		const files = [
			['System/Core.u','core'], ['System/Engine.u','engine'], ['System/UnrealShare.u','share'],
			['System/UnrealI.u','unreali'], ['System/Unreal.ini','ini'], ['System/Unreal.exe','exe'],
			['Maps/Vortex2.unr','map'], ['Textures/Test.utx','texture'], ['Sounds/Test.uax','sound'], ['Music/Test.umx','music'],
		];
		const entries = files.map(([path, value]) => { const blob = new Blob([value]); return { path, size: blob.size, getBlob: async () => blob }; });
		window.syntheticImportPromise = window.surrealApp.dataController.importController.importEntries(entries);
	}""")
	page.wait_for_selector("#game-launcher:not([hidden])", timeout=30000)
	presentations = page.locator("[data-launcher-presentation] option").all_text_contents()
	page.evaluate("""() => {
		const canvas = document.getElementById('game');
		canvas.tabIndex = 0;
		window.__flatInputEvents = { keydown: 0, keyup: 0, mousedown: 0, mouseup: 0, mousemove: 0, wheel: 0 };
		for (const name of Object.keys(window.__flatInputEvents))
			canvas.addEventListener(name, () => window.__flatInputEvents[name]++);
	}""")
	canvas = page.locator("#game")
	canvas.click(position={"x": 40, "y": 40})
	page.keyboard.press("w")
	box = canvas.bounding_box()
	page.mouse.move(box["x"] + 60, box["y"] + 60)
	page.mouse.wheel(0, 100)
	input_events = page.evaluate("window.__flatInputEvents")
	if any(input_events[name] < 1 for name in ["keydown", "keyup", "mousedown", "mouseup", "mousemove", "wheel"]):
		print("FAIL: ordinary DOM keyboard/mouse delivery to the flat canvas was interrupted", file=sys.stderr)
		sys.exit(1)
	page.click("#game-launcher button[type=submit]")
	page.evaluate("window.syntheticImportPromise")
	integration = page.evaluate("""() => ({
		args: window.syntheticCallMainArgs,
		selection: window.surrealLaunchSelection && {
			gameId: window.surrealLaunchSelection.game.id,
			map: window.surrealLaunchSelection.map,
			presentationId: window.surrealLaunchSelection.presentationId,
		},
	})""")
	print(json.dumps({"presentations": presentations, "syntheticLaunch": integration,
		"viewport": {"wide": wide_canvas, "narrow": narrow_canvas}, "fullscreen": fullscreen,
		"inputEvents": input_events}, indent=2))
	if presentations != ["Desktop window"] or integration["args"] != ["--autoplay", "--url=Vortex2", "--render=webgpu", "/gamedata"] or integration["selection"] != {"gameId": "unreal-gold", "map": "Vortex2", "presentationId": "flat"}:
		print("FAIL: staged package game detection or flat launch", file=sys.stderr)
		sys.exit(1)
	print("PASS: staged browser release package import gate and flat launch")
	context.close()
	browser.close()
