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
	lastErrorCode: "feature-negotiation-unobservable", lastErrorStage: "frame",
	frames: 321, skippedFrames: 2, inputPackets: 44,
	inputDiagnostics: { actionFocused: true, xrStandardSources: 2,
		leftButtons: 6, leftAxes: 4, rightButtons: 6, rightAxes: 4,
		nonzeroThumbstickSamples: 123,
		profiles: ["private-controller-profile"], axes: [987654321], path: "C:\\Private\\input" },
	projectionFormat: "rgba8unorm-webgl-bridge", referenceSpaceType: "local-floor",
	presentationMode: "webgl-bridge", presentationPreference: "webgl-bridge", layerWidth: 1832, layerHeight: 1920,
	atlasWidth: 1600, atlasHeight: 700,
	bridgeDiagnostics: { frames: 320, errors: 1, samples: 120, medianMs: 0.12349,
		p95Ms: 3.9996, p99Ms: 5.4996, blockingTiming: true,
		presentAgeFrames: 7, maxPresentAgeFrames: 12, presentAgeMs: 11.23456,
		maxPresentAgeMs: 42.9996, reusedPresents: 99,
		sourceViews: [{ eye: "private-eye-secret", projectionMatrix: [123456789] }],
		path: "C:\\Private", url: "https://private.invalid", exception: "secret" },
	enterAttempts: 2, successfulEntries: 2, exitRequests: 1, endedSessions: 1, reentries: 1,
	transitions: [{ type: "session-reentered", generation: 2, stage: "running" }],
	url: "https://private.invalid", userAgent: "Private Browser", gameIdentity: "Owned Unreal",
	logs: ["private log"],
});

assert.match(report, /^SurrealEngine WebXR headset report\nschema: surrealengine-webxr-headset-report-v2\n/);
assert.match(report, /\nframes: 321\n/);
assert.match(report, /\nprovider_error_code: feature-negotiation-unobservable\n/);
assert.match(report, /\ninput_action_focus: focused\ninput_xr_standard_sources: 2\n/);
assert.match(report, /\ninput_left_buttons: 6\ninput_left_axes: 4\ninput_right_buttons: 6\ninput_right_axes: 4\n/);
assert.match(report, /\ninput_nonzero_thumbstick_samples: 123\n/);
assert.match(report, /\npresentation_mode: webgl-bridge\n/);
assert.match(report, /\npresentation_preference: webgl-bridge\n/);
assert.match(report, /\nlayer_width: 1832\nlayer_height: 1920\natlas_width: 1600\natlas_height: 700\n/);
assert.match(report, /\nbridge_frames: 320\nbridge_errors: 1\nbridge_samples: 120\n/);
assert.match(report, /\nbridge_median_ms: 0\.123\nbridge_p95_ms: 4\nbridge_p99_ms: 5\.5\n/);
assert.match(report, /\nbridge_present_age_frames: 7\nbridge_max_present_age_frames: 12\n/);
assert.match(report, /\nbridge_present_age_ms: 11\.235\nbridge_max_present_age_ms: 43\nbridge_reused_presents: 99\n/);
assert.match(report, /\nbridge_blocking_timing: yes\n/);
for (const secret of ["Private", "Core.u", "private.invalid", "Owned Unreal", "private log",
	"secret", "private-eye-secret", "private-controller-profile", "123456789", "987654321"])
	assert.equal(report.includes(secret), false, "report exposed non-allowlisted value: " + secret);

const direct = diagnostics.normalized({ presentationMode: "direct-webgpu",
	layerWidth: 2048, layerHeight: 1024 });
assert.equal(direct.presentationMode, "direct-webgpu");
assert.equal(direct.layerWidth, 2048);
assert.equal(direct.bridgeSamples, 0);
assert.equal(direct.bridgeP99Ms, "unknown");

for (const code of ["session-ended-before-activation", "session-ended-before-first-frame"]) {
	const prematureEnd = diagnostics.formatReport({ lastError: "C:\\Private\\runtime failure",
		lastErrorCode: code, lastErrorStage: "session-reserved" });
	assert.match(prematureEnd, new RegExp("\\nprovider_error_code: " + code + "\\n"));
	assert.equal(prematureEnd.includes("Private"), false);
	assert.equal(prematureEnd.includes("runtime failure"), false);
}

const rejected = diagnostics.normalized({ presentationMode: "https://private.invalid/mode",
	lastErrorCode: "C:\\Private\\Core.u",
	inputDiagnostics: { actionFocused: "yes", xrStandardSources: "not-a-number",
		leftButtons: -1, leftAxes: 3.5, rightButtons: "private", rightAxes: Number.POSITIVE_INFINITY,
		nonzeroThumbstickSamples: -4 },
	bridgeDiagnostics: { p99Ms: -1, samples: "not-a-number", presentAgeFrames: -2,
		maxPresentAgeFrames: "private", presentAgeMs: Number.POSITIVE_INFINITY,
		maxPresentAgeMs: -1, reusedPresents: "not-a-number" } });
assert.equal(rejected.presentationMode, "unknown");
assert.equal(rejected.errorCode, "unknown");
assert.equal(rejected.inputActionFocus, "unknown");
assert.equal(rejected.inputXRStandardSources, 0);
assert.equal(rejected.inputLeftButtons, "unknown");
assert.equal(rejected.inputLeftAxes, "unknown");
assert.equal(rejected.inputRightButtons, "unknown");
assert.equal(rejected.inputRightAxes, "unknown");
assert.equal(rejected.inputNonzeroThumbstickSamples, 0);
assert.equal(rejected.bridgeP99Ms, "unknown");
assert.equal(rejected.bridgeSamples, 0);
assert.equal(rejected.bridgePresentAgeFrames, "unknown");
assert.equal(rejected.bridgeMaxPresentAgeFrames, "unknown");
assert.equal(rejected.bridgePresentAgeMs, "unknown");
assert.equal(rejected.bridgeMaxPresentAgeMs, "unknown");
assert.equal(rejected.bridgeReusedPresents, 0);
const rejectedReport = diagnostics.formatReport({ lastError: "present",
	lastErrorCode: "C:\\Private\\Core.u" });
assert.match(rejectedReport, /\nprovider_error_code: unknown\n/);
assert.equal(rejectedReport.includes("Private"), false);
assert.equal(rejectedReport.includes("Core.u"), false);

console.log("WebXR headset diagnostics v2 allowlist tests passed");
