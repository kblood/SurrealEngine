"""Probe the non-pthread WasmFS build's Window-owned native ABI boundary."""

import json
import os
import sys

from playwright.sync_api import sync_playwright


base_url = next(
	(arg.split("=", 1)[1].rstrip("/") for arg in sys.argv[1:] if arg.startswith("--base-url=")),
	os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/"),
)

with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	page = browser.new_page()
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(base_url + "/web/index.html", wait_until="load")
	page.wait_for_function(
		"window.surrealDataBootResult && window.surrealDataBootResult.import.state === 'waiting-for-import'",
		timeout=60000,
	)
	result = page.evaluate("""() => {
		const call = (name, returnType = "number", argumentTypes = [], args = []) =>
			Module.ccall(name, returnType, argumentTypes, args);
		const before = performance.now();
		const invalidFrame = call("Surreal_RenderWebXRFrame", "number",
			["number", "number"], [0, 0]);
		const after = performance.now();
		const originalScale = call("Surreal_GetWebXRWorldUnitsPerMeter");
		call("Surreal_SetWebXRWorldUnitsPerMeter", null, ["number"], [48]);
		const changedScale = call("Surreal_GetWebXRWorldUnitsPerMeter");
		call("Surreal_SetWebXRWorldUnitsPerMeter", null, ["number"], [originalScale]);
		return {
			mountABI: call("Surreal_GetBrowserOPFSMountABIVersion"),
			mountMode: call("Surreal_GetBrowserOPFSMountMode"),
			crossOriginIsolated: globalThis.crossOriginIsolated,
			frameABI: call("Surreal_GetWebXRFrameABIVersion"),
			frameHeaderBytes: call("Surreal_GetWebXRFrameHeaderSize"),
			viewBytes: call("Surreal_GetWebXRViewSize"),
			maxViews: call("Surreal_GetWebXRFrameMaxViews"),
			inputABI: call("Surreal_GetWebXRInputABIVersion"),
			inputHeaderBytes: call("Surreal_GetWebXRInputHeaderSize"),
			inputSourceBytes: call("Surreal_GetWebXRInputSourceSize"),
			maxInputSources: call("Surreal_GetWebXRInputMaxSources"),
			invalidFrame,
			synchronousCallMs: after - before,
			originalScale,
			changedScale,
		};
	}""")
	browser.close()

result["pageErrors"] = page_errors
print(json.dumps(result, indent=2))
failed = (
	result["mountABI"] != 2 or result["mountMode"] != 2 or
	result["frameABI"] <= 0 or result["frameHeaderBytes"] <= 0 or result["viewBytes"] <= 0 or
	result["maxViews"] != 2 or result["inputABI"] <= 0 or result["inputHeaderBytes"] <= 0 or
	result["inputSourceBytes"] <= 0 or result["maxInputSources"] != 2 or
	result["invalidFrame"] != 0 or result["changedScale"] != 48 or page_errors
)
if failed:
	print("FAIL: Window-owned Asyncify/WebXR ABI boundary", file=sys.stderr)
	sys.exit(1)
print("PASS: non-shared Window-owned runtime exposes synchronous WebXR frame and input ABIs")
