"""Exercise a staged data-free flat/WebXR browser package."""
import json
import os
import sys
import time
from playwright.sync_api import sync_playwright

base_url = next((arg.split("=", 1)[1].rstrip("/") for arg in sys.argv[1:] if arg.startswith("--base-url=")), os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/"))
with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	context = browser.new_context(service_workers="block")
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
	result = page.evaluate("""() => ({
		ready: !!window.surrealApp,
		state: window.surrealApp && window.surrealApp.result.import.state,
		crossOriginIsolated,
		booted: window.surrealBooted === true,
		engineBase: document.documentElement.dataset.engineBase,
		registeredPresentations: window.surrealApp && window.surrealApp.registry.available().map(provider => provider.id),
		presentations: Array.from(document.querySelectorAll('[data-launcher-presentation] option')).map(option => option.value),
		capabilities: Array.from(document.querySelectorAll('[data-capability-list] li')).map(item => item.textContent),
		phase: document.querySelector('[data-app-phase]').textContent,
		pageErrors: [],
	})""")
	result["pageErrors"] = page_errors
	result["manifestSchema"] = manifest.get("schema")
	result["manifestGames"] = manifest.get("games")
	print(json.dumps(result, indent=2))
	failure = (not result["ready"] or result["state"] != "waiting-for-import" or result["booted"] or
		not result["crossOriginIsolated"] or result["engineBase"] != "./engine/" or
		result["registeredPresentations"] != ["flat"] or
		result["manifestSchema"] != "surrealengine-browser-release-v1" or
		result["manifestGames"] != ["ut99", "unreal-gold"] or result["pageErrors"])
	if failure:
		print("FAIL: staged browser release package", file=sys.stderr)
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
	print(json.dumps({"presentations": presentations, "syntheticLaunch": integration}, indent=2))
	if presentations != ["Desktop window"] or integration["args"] != ["--autoplay", "--url=Vortex2", "--render=webgpu", "/gamedata"] or integration["selection"] != {"gameId": "unreal-gold", "map": "Vortex2", "presentationId": "flat"}:
		print("FAIL: staged package game detection or flat launch", file=sys.stderr)
		sys.exit(1)
	print("PASS: staged browser release package import gate and flat launch")
	context.close()
	browser.close()
