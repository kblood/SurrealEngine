"""Measure browser-main to PROXY_TO_PTHREAD dispatch and async completion."""

import json
import os
import sys
import time

from playwright.sync_api import sync_playwright


base_url = next(
	(arg.split("=", 1)[1].rstrip("/") for arg in sys.argv[1:] if arg.startswith("--base-url=")),
	os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/"),
)
probe_url = base_url + "/build-proxy-thread-probe/index.html"

with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	page = browser.new_page()
	messages = []
	page_errors = []
	page.on("console", lambda message: messages.append(message.text))
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(probe_url, wait_until="load")
	deadline = time.time() + 30
	while time.time() < deadline and not any(message.startswith("READY proxy-to-pthread") for message in messages):
		page.wait_for_timeout(50)
	if not any("engine-thread-kind=2" in message for message in messages):
		raise RuntimeError("engine worker did not become ready: " + repr(messages))

	result = page.evaluate("""() => new Promise((resolve, reject) => {
		const count = 32;
		const submittedAt = new Map();
		const completions = [];
		let insideSubmit = false;
		let synchronousCompletion = false;
		let rafBodyFinished = false;
		const timeout = setTimeout(() => reject(new Error("worker completion timed out")), 10000);
		globalThis.surrealProxyProbeMarker = 0x51a7c0de;
		globalThis.surrealProxyProbeComplete = (sequence, threadKind, sawBrowserGlobal, completedAt) => {
			if (insideSubmit) synchronousCompletion = true;
			completions.push({ sequence, threadKind, sawBrowserGlobal,
				latencyMs: completedAt - submittedAt.get(sequence),
				afterRAFBody: rafBodyFinished });
			if (completions.length === count) {
				clearTimeout(timeout);
				resolve({
					callingThreadKind: Module.ccall("Probe_CallingThreadKind", "number", [], []),
					synchronousCompletion,
					completions,
				});
			}
		};
		requestAnimationFrame(() => {
			for (let sequence = 0; sequence < count; sequence++) {
				submittedAt.set(sequence, performance.now());
				insideSubmit = true;
				const accepted = Module.ccall("Probe_SubmitToEngineThread", "number", ["number"], [sequence]);
				insideSubmit = false;
				if (accepted !== 1) reject(new Error("worker dispatch was rejected"));
			}
			rafBodyFinished = true;
		});
	})""")
	browser.close()

latencies = sorted(completion["latencyMs"] for completion in result["completions"])
summary = {
	"callingThreadKind": result["callingThreadKind"],
	"engineThreadKinds": sorted(set(completion["threadKind"] for completion in result["completions"])),
	"completionCount": len(result["completions"]),
	"synchronousCompletion": result["synchronousCompletion"],
	"allAfterRAFBody": all(completion["afterRAFBody"] for completion in result["completions"]),
	"workerSawBrowserGlobal": any(completion["sawBrowserGlobal"] for completion in result["completions"]),
	"medianLatencyMs": latencies[len(latencies) // 2],
	"maximumLatencyMs": latencies[-1],
	"pageErrors": page_errors,
}
print(json.dumps(summary, indent=2))
if (summary["callingThreadKind"] != 1 or summary["engineThreadKinds"] != [2] or
		summary["completionCount"] != 32 or summary["synchronousCompletion"] or
		not summary["allAfterRAFBody"] or summary["workerSawBrowserGlobal"] or summary["pageErrors"]):
	print("FAIL: PROXY_TO_PTHREAD dispatch boundary", file=sys.stderr)
	sys.exit(1)
print("PASS: browser exports run on the browser thread; queued work and completion are asynchronous")
