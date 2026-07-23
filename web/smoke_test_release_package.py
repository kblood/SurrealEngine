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
		pointerLockReady: typeof SurrealBrowserPointerLock !== "undefined" &&
			SurrealBrowserPointerLock.status().requested === false,
		releaseNotice: document.querySelector('[data-release-notice]') &&
			document.querySelector('[data-release-notice]').textContent,
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
		result["registeredPresentations"] != ["flat"] or not result["pointerLockReady"] or
		not result["sourceUI"]["visible"] or
		"experimental preview" not in (result["releaseNotice"] or "") or
		"unverified on physical Quest hardware" not in (result["releaseNotice"] or "") or
		"No game or demo data is bundled or downloaded" not in (result["releaseNotice"] or "") or
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

	prelaunch = page.evaluate("""() => {
		const startup = document.getElementById('browser-startup');
		const canvas = document.getElementById('canvas');
		const picker = document.querySelector('[data-game-pick]');
		const pickerBounds = picker.getBoundingClientRect();
		return { startupHidden: startup.hidden, startupDisplay: getComputedStyle(startup).display,
			canvasRects: canvas.getClientRects().length, pickerTop: pickerBounds.top,
			pickerBottom: pickerBounds.bottom, viewportHeight: innerHeight, scrollY };
	}""")
	if (not prelaunch["startupHidden"] or prelaunch["startupDisplay"] != "none" or
		prelaunch["canvasRects"] != 0 or prelaunch["pickerTop"] < 0 or
		prelaunch["pickerBottom"] > prelaunch["viewportHeight"] or prelaunch["scrollY"] != 0):
		print(json.dumps({"prelaunch": prelaunch}, indent=2), file=sys.stderr)
		print("FAIL: inactive canvas obscured the pre-launch controls", file=sys.stderr)
		sys.exit(1)
	page.evaluate("""asyncifyEntry => {
		window.syntheticNativeEntry = null;
		window.syntheticNativeArgs = null;
		if (asyncifyEntry) {
			Module._Surreal_StartBrowserGame = () => 0;
			const originalCcall = Module.ccall.bind(Module);
			Module.ccall = (name, returnType, argumentTypes, args, options) => {
				if (name !== 'Surreal_StartBrowserGame')
					return originalCcall(name, returnType, argumentTypes, args, options);
				window.syntheticNativeEntry = name;
				window.syntheticNativeArgs = ['--autoplay'];
				if (args[2]) window.syntheticNativeArgs.push('--url=' + args[0]);
				window.syntheticNativeArgs.push('--render=' + args[1], '/gamedata');
				return Promise.resolve(0);
			};
		} else {
			Module.callMain = args => {
				window.syntheticNativeEntry = 'callMain';
				window.syntheticNativeArgs = Array.from(args);
			};
		}
		const files = [
			['System/Core.u','core'], ['System/Engine.u','engine'], ['System/UnrealShare.u','share'],
			['System/UnrealI.u','unreali'], ['System/Unreal.ini','ini'], ['System/Unreal.exe','exe'],
			['Maps/Vortex2.unr','map'], ['Textures/Test.utx','texture'], ['Sounds/Test.uax','sound'], ['Music/Test.umx','music'],
		];
		const entries = files.map(([path, value]) => { const blob = new Blob([value]); return { path, size: blob.size, getBlob: async () => blob }; });
		window.syntheticImportPromise = window.surrealApp.dataController.importController.importEntries(entries);
	}""", compliance.get("buildProvenance", {}).get("browserEntryPoint") == "asyncify-opfs")
	page.wait_for_selector("#game-launcher:not([hidden])", timeout=30000)
	presentations = page.locator("[data-launcher-presentation] option").all_text_contents()
	pre_launch_scroll = page.evaluate("""() => {
		const launcher = document.getElementById('game-launcher');
		launcher.scrollIntoView({ behavior: 'auto', block: 'start' });
		const startup = document.getElementById('browser-startup');
		const canvas = document.getElementById('canvas');
		const launcherBounds = launcher.getBoundingClientRect();
		return { scrollY, startupHidden: startup.hidden, canvasRects: canvas.getClientRects().length,
			launcherTop: launcherBounds.top, launcherBottom: launcherBounds.bottom, viewportHeight: innerHeight };
	}""")
	page.click("#game-launcher button[type=submit]")
	page.evaluate("window.syntheticImportPromise")
	page.wait_for_function("window.surrealBooted === true")
	integration = page.evaluate("""() => ({
		entry: window.syntheticNativeEntry,
		args: window.syntheticNativeArgs,
		selection: window.surrealLaunchSelection && {
			gameId: window.surrealLaunchSelection.game.id,
			map: window.surrealLaunchSelection.map,
			presentationId: window.surrealLaunchSelection.presentationId,
		},
		layout: (() => {
			const canvas = document.getElementById('canvas');
			const bounds = canvas.getBoundingClientRect();
			return { top: bounds.top, bottom: bounds.bottom, width: bounds.width, height: bounds.height,
				viewportWidth: innerWidth, viewportHeight: innerHeight, scrollY,
				hidden: canvas.hidden, display: getComputedStyle(canvas).display,
				focused: document.activeElement === canvas, label: canvas.getAttribute('aria-label') };
		})(),
	})""")
	expected_entry = "Surreal_StartBrowserGame" if compliance.get("buildProvenance", {}).get("browserEntryPoint") == "asyncify-opfs" else "callMain"
	if (presentations != ["Desktop window"] or integration["entry"] != expected_entry or
		integration["args"] != ["--autoplay", "--url=Vortex2", "--render=webgpu", "/gamedata"] or
		integration["selection"] != {"gameId": "unreal-gold", "map": "Vortex2", "presentationId": "flat"}):
		print("FAIL: staged package game detection or flat launch", file=sys.stderr)
		sys.exit(1)
	layout = integration["layout"]
	if (pre_launch_scroll["scrollY"] <= 0 or not pre_launch_scroll["startupHidden"] or
		pre_launch_scroll["canvasRects"] != 0 or pre_launch_scroll["launcherTop"] < -1 or
		pre_launch_scroll["launcherTop"] >= pre_launch_scroll["viewportHeight"] or
		layout["top"] < -1 or layout["top"] >= layout["viewportHeight"] or layout["bottom"] <= 0 or
		layout["height"] > layout["viewportHeight"] + 1 or layout["scrollY"] > 1 or
		layout["hidden"] or layout["display"] == "none" or
		not layout["focused"] or layout["label"] != "SurrealEngine game view"):
		print("FAIL: launcher submission did not restore the bounded game canvas to the viewport", file=sys.stderr)
		sys.exit(1)

	page.set_viewport_size({"width": 1100, "height": 760})
	wide_canvas = page.locator("#canvas").bounding_box()
	page.set_viewport_size({"width": 700, "height": 520})
	narrow_canvas = page.locator("#canvas").bounding_box()
	page.set_viewport_size({"width": 390, "height": 844})
	portrait_canvas = page.locator("#canvas").bounding_box()
	page.set_viewport_size({"width": 844, "height": 390})
	landscape_canvas = page.locator("#canvas").bounding_box()
	page.set_viewport_size({"width": 700, "height": 520})
	if (not wide_canvas or not narrow_canvas or wide_canvas["width"] < 1090 or
		narrow_canvas["width"] < 690 or narrow_canvas["width"] >= wide_canvas["width"] or
		wide_canvas["width"] > 1101 or narrow_canvas["width"] > 701 or
		wide_canvas["height"] > 761 or narrow_canvas["height"] > 521 or
		not portrait_canvas or portrait_canvas["x"] < -1 or portrait_canvas["width"] < 380 or
		portrait_canvas["width"] > 391 or portrait_canvas["height"] > 845 or
		not landscape_canvas or landscape_canvas["x"] < -1 or landscape_canvas["width"] >= 844 or
		landscape_canvas["x"] + landscape_canvas["width"] > 845 or landscape_canvas["height"] > 391 or
		abs(wide_canvas["width"] / wide_canvas["height"] - 16 / 9) > 0.02 or
		abs(narrow_canvas["width"] / narrow_canvas["height"] - 16 / 9) > 0.02 or
		abs(portrait_canvas["width"] / portrait_canvas["height"] - 16 / 9) > 0.02 or
		abs(landscape_canvas["width"] / landscape_canvas["height"] - 16 / 9) > 0.02):
		print(json.dumps({"viewport": {"wide": wide_canvas, "narrow": narrow_canvas,
			"mobilePortrait": portrait_canvas, "mobileLandscape": landscape_canvas}}, indent=2), file=sys.stderr)
		print("FAIL: flat canvas did not retain a viewport-bounded 16:9 layout", file=sys.stderr)
		sys.exit(1)

	fullscreen = page.evaluate("""async () => {
		const canvas = document.getElementById('canvas');
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
			page.wait_for_function("document.fullscreenElement === document.getElementById('canvas')", timeout=5000)
		except Exception:
			pass
		fullscreen["entered"] = page.evaluate("document.fullscreenElement === document.getElementById('canvas')")
		if fullscreen["entered"]:
			page.evaluate("document.exitFullscreen()")
			page.wait_for_function("document.fullscreenElement === null", timeout=5000)
			fullscreen["exited"] = True
	if fullscreen["supported"] and not fullscreen["entered"]:
		print("FAIL: Chrome exposed fullscreen but the trusted probe could not enter it", file=sys.stderr)
		sys.exit(1)

	page.evaluate("""() => {
		const canvas = document.getElementById('canvas');
		window.__flatInputEvents = { keydown: 0, keyup: 0, mousedown: 0, mouseup: 0, mousemove: 0, wheel: 0 };
		for (const name of Object.keys(window.__flatInputEvents))
			canvas.addEventListener(name, () => window.__flatInputEvents[name]++);
	}""")
	canvas = page.locator("#canvas")
	canvas.click(position={"x": 40, "y": 40})
	page.keyboard.press("w")
	box = canvas.bounding_box()
	page.mouse.move(box["x"] + 60, box["y"] + 60)
	page.mouse.wheel(0, 100)
	input_events = page.evaluate("window.__flatInputEvents")
	if any(input_events[name] < 1 for name in ["keydown", "keyup", "mousedown", "mouseup", "mousemove", "wheel"]):
		print("FAIL: ordinary DOM keyboard/mouse delivery to the flat canvas was interrupted", file=sys.stderr)
		sys.exit(1)

	print(json.dumps({"presentations": presentations, "prelaunch": prelaunch,
		"preLaunchScroll": pre_launch_scroll, "syntheticLaunch": integration,
		"viewport": {"wide": wide_canvas, "narrow": narrow_canvas,
			"mobilePortrait": portrait_canvas, "mobileLandscape": landscape_canvas},
		"fullscreen": fullscreen, "inputEvents": input_events}, indent=2))
	print("PASS: staged browser release package import gate and flat launch")
	context.close()
	browser.close()
