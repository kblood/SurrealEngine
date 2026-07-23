import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";

const source = fs.readFileSync(new URL("./webxr_diagnostics.js", import.meta.url), "utf8");
const sandbox = { globalThis: {}, console };
vm.runInNewContext(source, sandbox);
const diagnostics = sandbox.globalThis.SurrealWebXRDiagnostics;

const report = diagnostics.formatReport({
	capabilityCode: "ready", capabilityAvailable: true, adapterState: "xr-compatible",
	phase: "running", currentStage: "running", lastError: "C:\\Private\\Core.u",
	lastErrorStage: "frame", frames: 321, skippedFrames: 2, inputPackets: 44,
	projectionFormat: "rgba8unorm-webgl-bridge", referenceSpaceType: "local-floor",
	presentationMode: "webgl-bridge", presentationPreference: "webgl-bridge", layerWidth: 1832, layerHeight: 1920,
	atlasWidth: 1600, atlasHeight: 700,
	bridgeDiagnostics: { frames: 320, errors: 1, samples: 120, medianMs: 0.12349,
		p95Ms: 3.9996, p99Ms: 5.4996, blockingTiming: true,
		path: "C:\\Private", url: "https://private.invalid", exception: "secret" },
	enterAttempts: 2, successfulEntries: 2, exitRequests: 1, endedSessions: 1, reentries: 1,
	transitions: [{ type: "session-reentered", generation: 2, stage: "running" }],
	url: "https://private.invalid", userAgent: "Private Browser", gameIdentity: "Owned Unreal",
	logs: ["private log"],
});

assert.match(report, /^SurrealEngine WebXR headset report\nschema: surrealengine-webxr-headset-report-v2\n/);
assert.match(report, /\nframes: 321\n/);
assert.match(report, /\npresentation_mode: webgl-bridge\n/);
assert.match(report, /\npresentation_preference: webgl-bridge\n/);
assert.match(report, /\nlayer_width: 1832\nlayer_height: 1920\natlas_width: 1600\natlas_height: 700\n/);
assert.match(report, /\nbridge_frames: 320\nbridge_errors: 1\nbridge_samples: 120\n/);
assert.match(report, /\nbridge_median_ms: 0\.123\nbridge_p95_ms: 4\nbridge_p99_ms: 5\.5\n/);
assert.match(report, /\nbridge_blocking_timing: yes\n/);
for (const secret of ["Private", "Core.u", "private.invalid", "Owned Unreal", "private log", "secret"])
	assert.equal(report.includes(secret), false, "report exposed non-allowlisted value: " + secret);

const direct = diagnostics.normalized({ presentationMode: "direct-webgpu",
	layerWidth: 2048, layerHeight: 1024 });
assert.equal(direct.presentationMode, "direct-webgpu");
assert.equal(direct.layerWidth, 2048);
assert.equal(direct.bridgeSamples, 0);
assert.equal(direct.bridgeP99Ms, "unknown");

const rejected = diagnostics.normalized({ presentationMode: "https://private.invalid/mode",
	bridgeDiagnostics: { p99Ms: -1, samples: "not-a-number" } });
assert.equal(rejected.presentationMode, "unknown");
assert.equal(rejected.bridgeP99Ms, "unknown");
assert.equal(rejected.bridgeSamples, 0);

console.log("WebXR headset diagnostics v2 allowlist tests passed");
