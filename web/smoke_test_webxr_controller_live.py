"""Prove post-launch WebXR controller transitions preserve one live UE1/WASM runtime.

This deliberately mocks only the browser XR session/layer boundary. It is a
state-continuity test, not evidence that a headset rendered a frame.
"""

import json
import os
import sys
import time
from pathlib import Path
from urllib.parse import quote

from playwright.sync_api import sync_playwright


base_url = os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/")
engine_base = os.environ.get("SURREAL_WEB_ENGINE_BASE", "/build-emscripten/")
harness_path = os.environ.get("SURREAL_WEBGL2_HARNESS_PATH", "/web/index_webgl2.html")
adapter_path = os.environ.get("SURREAL_WEBXR_ADAPTER_PATH", "/web/webxr_browser_app_adapter.js")
result_path = os.environ.get("SURREAL_WEBXR_CONTROLLER_RESULT")
map_name = os.environ.get("SURREAL_WEB_MAP")
url = f"{base_url}{harness_path}?engineBase={quote(engine_base, safe='/')}"
if map_name:
	url += "&map=" + quote(map_name, safe="-")


def wait_until(page, expression, timeout=120):
	deadline = time.time() + timeout
	while time.time() < deadline:
		if page.evaluate(expression):
			return True
		time.sleep(0.2)
	return False


with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	page = browser.new_page(viewport={"width": 960, "height": 640})
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(url, wait_until="load")
	if not wait_until(page, "window.surrealBooted || window.surrealCrashed"):
		raise RuntimeError("timed out waiting for the live WebGL2 game")
	if page.evaluate("window.surrealCrashed"):
		raise RuntimeError(page.evaluate("window.surrealCrashed"))
	if not wait_until(page,
		"window.surrealGetTickCount() >= 10 && window.surrealGetWebGL2Diagnostics().textures >= 164",
		timeout=30):
		raise RuntimeError("live scene resources did not reach the stable continuity baseline")

	page.add_script_tag(url=base_url + adapter_path)
	page.evaluate("""() => {
		const provider = { active: false, cleanupPending: false, sessionEndBlocked: false,
			phase: 'ended', lastError: null };
		window.__xrControllerProbe = {
			module: Module,
			dataController: window.surrealBrowserDataController,
			audioController: window.surrealBrowserAudio,
			mainCalls: 0,
			requests: 0,
			activations: 0,
			exits: 0,
			provider,
		};
		const originalMain = Module.callMain;
		Module.callMain = function (...args) {
			window.__xrControllerProbe.mainCalls++;
			return originalMain.apply(this, args);
		};
		window.surrealXRSetPresentationPreference = () => {};
		window.surrealXRSetBridgeBlockingTiming = () => {};
		window.surrealXRSetBridgeRotationReprojection = () => {};
		window.surrealXRGetState = () => ({ ...provider });
		window.surrealXRRequestSession = () => {
			window.__xrControllerProbe.requests++;
			provider.phase = 'session-reserved';
			return Promise.resolve(true);
		};
		window.surrealXRActivateReservedSession = () => {
			window.__xrControllerProbe.activations++;
			provider.active = true;
			provider.phase = 'running';
			return Promise.resolve(true);
		};
		window.surrealXRExit = () => {
			window.__xrControllerProbe.exits++;
			provider.active = false;
			provider.phase = 'ended';
			return true;
		};
		const capability = { available: true, code: 'mock-live-runtime',
			message: 'Mock session boundary for live runtime continuity only.' };
		const controller = SurrealWebXRBrowserProvider.createSessionController(capability);
		controller.attachModule(Module);
		controller.engineStarted();
		window.__xrControllerProbe.controller = controller;
		window.__xrControllerProbe.identity = () => ({
			engine: Module.ccall('Surreal_GetBrowserEngineIdentity', 'number', [], []),
			level: Module.ccall('Surreal_GetBrowserLevelIdentity', 'number', [], []),
			player: Module.ccall('Surreal_GetBrowserPlayerIdentity', 'number', [], []),
			renderer: Module.ccall('Surreal_GetBrowserRendererIdentity', 'number', [], []),
			audio: Module.ccall('Surreal_GetBrowserAudioIdentity', 'number', [], []),
			heapBytes: Module.ccall('Surreal_GetBrowserHeapSize', 'number', [], []),
			tick: window.surrealGetTickCount(),
			gl: window.surrealGetWebGL2Diagnostics(),
			moduleSame: Module === window.__xrControllerProbe.module,
			dataControllerSame: window.surrealBrowserDataController === window.__xrControllerProbe.dataController,
			audioControllerSame: window.surrealBrowserAudio === window.__xrControllerProbe.audioController,
		});
	}""")

	before = page.evaluate("window.__xrControllerProbe.identity()")
	states = [page.evaluate("window.__xrControllerProbe.controller.status().state")]
	if not page.evaluate("window.__xrControllerProbe.controller.enter()"):
		raise RuntimeError("mock session entry failed")
	states.append(page.evaluate("window.__xrControllerProbe.controller.status().state"))
	time.sleep(1)
	during = page.evaluate("window.__xrControllerProbe.identity()")
	if not page.evaluate("window.__xrControllerProbe.controller.exit()"):
		raise RuntimeError("mock session exit failed")
	states.append(page.evaluate("window.__xrControllerProbe.controller.status().state"))
	if not page.evaluate("window.__xrControllerProbe.controller.enter()"):
		raise RuntimeError("mock session re-entry failed")
	states.append(page.evaluate("window.__xrControllerProbe.controller.status().state"))
	page.evaluate("window.dispatchEvent(new Event('surrealwebxrgameexit'))")
	states.append(page.evaluate("window.__xrControllerProbe.controller.status().state"))
	time.sleep(1)
	after = page.evaluate("window.__xrControllerProbe.identity()")
	counts = page.evaluate("""() => ({ mainCalls: __xrControllerProbe.mainCalls,
		requests: __xrControllerProbe.requests, activations: __xrControllerProbe.activations,
		exits: __xrControllerProbe.exits })""")

	# Exercise the real Emscripten scheduler handoff independently of the mocked
	# headset layer. A one-second ownership pause must not become a one-second
	# simulation delta when the ordinary browser RAF resumes.
	scheduler_before = page.evaluate("""() => {
		Module.ccall('Surreal_ResetBrowserFrameDeltaDiagnostics', null, [], []);
		const tick = surrealGetTickCount();
		const accepted = Module.ccall('Surreal_SetXRFrameLoopActive', 'number', ['number'], [1]);
		return { tick, accepted, active: Module.ccall('Surreal_GetXRFrameLoopActive', 'number', [], []) };
	}""")
	time.sleep(1)
	scheduler_paused = page.evaluate("""() => ({ tick: surrealGetTickCount(),
		active: Module.ccall('Surreal_GetXRFrameLoopActive', 'number', [], []) })""")
	page.evaluate("Module.ccall('Surreal_SetXRFrameLoopActive', 'number', ['number'], [0])")
	if not wait_until(page, f"window.surrealGetTickCount() > {scheduler_paused['tick']}", timeout=10):
		raise RuntimeError("flat RAF did not resume after scheduler handoff")
	time.sleep(0.25)
	scheduler_after = page.evaluate("""() => ({ tick: surrealGetTickCount(),
		active: Module.ccall('Surreal_GetXRFrameLoopActive', 'number', [], []),
		maximumDeltaMicroseconds: Module.ccall('Surreal_GetBrowserMaximumFrameDeltaMicroseconds', 'number', [], []) })""")

	result = {
		"schema": "surrealengine-webxr-controller-live-v1",
		"scope": "live UE1/WASM state continuity with mocked browser XR session boundary; not headset rendering qualification",
		"url": url,
		"states": states,
		"before": before,
		"during": during,
		"after": after,
		"counts": counts,
		"schedulerHandoff": {"before": scheduler_before, "paused": scheduler_paused, "after": scheduler_after},
		"pageErrors": page_errors,
	}
	print(json.dumps(result, indent=2))
	if result_path:
		Path(result_path).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

	identities = ["engine", "level", "player", "renderer", "audio"]
	passed = (
		states == ["FlatRunning", "ImmersiveRunning", "FlatRunning", "ImmersiveRunning", "FlatRunning"]
		and all(before[name] != 0 and before[name] == during[name] == after[name] for name in identities)
		and before["heapBytes"] == during["heapBytes"] == after["heapBytes"]
		and all(snapshot[name] for snapshot in [before, during, after]
			for name in ["moduleSame", "dataControllerSame", "audioControllerSame"])
		and before["tick"] < during["tick"] < after["tick"]
		and before["gl"]["generation"] == during["gl"]["generation"] == after["gl"]["generation"]
		and before["gl"]["textures"] == during["gl"]["textures"] == after["gl"]["textures"]
		and counts == {"mainCalls": 0, "requests": 2, "activations": 2, "exits": 2}
		and scheduler_before["accepted"] == 1 and scheduler_before["active"] == 1
		and scheduler_paused["active"] == 1 and scheduler_paused["tick"] == scheduler_before["tick"]
		and scheduler_after["active"] == 0 and scheduler_after["tick"] > scheduler_paused["tick"]
		and scheduler_after["maximumDeltaMicroseconds"] < 100000
		and not page_errors
	)
	page.evaluate("window.surrealRequestQuit()")
	browser.close()

if not passed:
	print("FAIL: post-launch controller changed live runtime identity", file=sys.stderr)
	raise SystemExit(1)
print("PASS: post-launch controller preserved one live UE1/WASM runtime")
