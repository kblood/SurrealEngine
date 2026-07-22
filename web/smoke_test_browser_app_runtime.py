"""Verify the shared product page reaches the legal no-data import gate."""
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
	page.goto(base_url + "/web/surreal_app.html", wait_until="load")
	deadline = time.time() + 60
	while time.time() < deadline:
		if page.evaluate("window.surrealApp || document.getElementById('log').textContent.includes('[launcher]')"):
			break
		time.sleep(0.1)
	result = page.evaluate("""() => ({
		ready: !!window.surrealApp,
		state: window.surrealApp && window.surrealApp.result.import.state,
		booted: window.surrealBooted === true,
		libraryVisible: !document.getElementById('game-library').hidden,
		importerVisible: !document.getElementById('game-data-importer').hidden,
		status: document.querySelector('[data-game-status]').textContent,
		log: document.getElementById('log').textContent,
	})""")
	result["pageErrors"] = page_errors
	print(json.dumps(result, indent=2))
	failure = (not result["ready"] or result["state"] != "waiting-for-import" or result["booted"] or
		not result["libraryVisible"] or not result["importerVisible"] or result["pageErrors"])
	if failure:
		print("FAIL: shared browser app no-data gate", file=sys.stderr)
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
	page.click("#game-launcher button[type=submit]")
	page.evaluate("window.syntheticImportPromise")
	integration = page.evaluate("""() => ({
		args: window.syntheticCallMainArgs,
		selection: window.surrealLaunchSelection && { gameId: window.surrealLaunchSelection.game.id, map: window.surrealLaunchSelection.map },
		library: window.surrealApp.library.status(),
	})""")
	print(json.dumps(integration, indent=2))
	if integration["args"] != ["--autoplay", "--url=Vortex2", "--render=webgpu", "/gamedata"] or integration["selection"] != {"gameId": "unreal-gold", "map": "Vortex2"} or integration["library"]["activeGameId"] != "unreal-gold":
		print("FAIL: shared launcher integration", file=sys.stderr)
		sys.exit(1)
	print("PASS: shared browser app import gate and Unreal Gold launch selection")
	context.close()
	browser.close()
