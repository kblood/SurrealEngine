import assert from "node:assert/strict";

globalThis.window = globalThis;
Object.defineProperty(globalThis, "navigator", {
	configurable: true,
	value: { xr: { isSessionSupported: async mode => mode === "immersive-vr" } },
});
globalThis.XRGPUBinding = function () {};
const device = { label: "shared-device" };
const lifecycle = [];
globalThis.surrealXRSetPresentationPreference = preference => {
	lifecycle.push("preference:" + preference);
};
globalThis.surrealXRSetBridgeBlockingTiming = enabled => {
	lifecycle.push("blocking-timing:" + enabled);
};
globalThis.surrealXRSetBridgeRotationReprojection = enabled => {
	lifecycle.push("rotation-reprojection:" + enabled);
};
globalThis.surrealXRRequestSession = async () => {
	lifecycle.push("request-session");
	assert.notEqual(globalThis.surrealWebGPUDevice, device, "prepare must not depend on activation wiring");
	return true;
};
globalThis.surrealXRActivateReservedSession = async () => {
	lifecycle.push("activate-reserved");
	assert.equal(globalThis.surrealWebGPUDevice, device);
	return true;
};

await import("./webxr_browser_app_adapter.js");
const capability = await globalThis.SurrealWebXRBrowserProvider.probe();
const provider = globalThis.SurrealWebXRBrowserProvider.createProvider(capability);
assert.equal(provider.id, "webxr");
assert.equal(provider.isAvailable(), true);
const reservation = await provider.prepareLaunch({ selection: {
	webXRPresentationPreference: "webgl-bridge", webXRBridgeBlockingTiming: true,
	webXRBridgeRotationReprojection: true,
} });
assert.equal(reservation.reserved, true);
assert.deepEqual(lifecycle, ["preference:webgl-bridge", "blocking-timing:true",
	"rotation-reprojection:true", "request-session"]);
await provider.activate({ Module: { preinitializedWebGPUDevice: device }, selection: {} });
assert.deepEqual(lifecycle, ["preference:webgl-bridge", "blocking-timing:true",
	"rotation-reprojection:true", "request-session", "activate-reserved"]);

const unavailableHost = {
	navigator: globalThis.navigator,
	XRGPUBinding: globalThis.XRGPUBinding,
	surrealXRSetPresentationPreference(preference) { assert.equal(preference, "webgl-bridge"); },
	surrealXRSetBridgeBlockingTiming(enabled) { assert.equal(enabled, false); },
	surrealXRSetBridgeRotationReprojection(enabled) { assert.equal(enabled, false); },
	async surrealXRRequestSession() { return false; },
	surrealXRGetState() { return { lastError: "WebGL compatibility bridge was requested but is unavailable" }; },
};
const unavailable = await globalThis.SurrealWebXRBrowserProvider.createProvider(capability, unavailableHost)
	.prepareLaunch({ selection: { webXRPresentationPreference: "webgl-bridge" } });
assert.equal(unavailable.reserved, false);
assert.equal(unavailable.fallback, "flat");
assert.match(unavailable.message, /requested but is unavailable/);

const oldForcedProvider = globalThis.SurrealWebXRBrowserProvider.createProvider(capability, {
	navigator: globalThis.navigator, XRGPUBinding: globalThis.XRGPUBinding,
	async surrealXRRequestSession() { throw new Error("must not reserve with an unsupported forced preference"); },
});
const oldForced = await oldForcedProvider.prepareLaunch({ selection: { webXRPresentationPreference: "webgl-bridge" } });
assert.equal(oldForced.fallback, "flat");
assert.match(oldForced.message, /cannot force the WebGL compatibility bridge/);

const automaticLifecycle = [];
const automaticHost = {
	navigator: globalThis.navigator, XRGPUBinding: globalThis.XRGPUBinding,
	surrealXRSetPresentationPreference(value) { automaticLifecycle.push("preference:" + value); },
	surrealXRSetBridgeBlockingTiming(value) { automaticLifecycle.push("blocking-timing:" + value); },
	surrealXRSetBridgeRotationReprojection(value) { automaticLifecycle.push("rotation-reprojection:" + value); },
	async surrealXRRequestSession() { automaticLifecycle.push("request-session"); return true; },
};
const automatic = await globalThis.SurrealWebXRBrowserProvider.createProvider(capability, automaticHost)
	.prepareLaunch({ selection: { webXRPresentationPreference: "auto", webXRBridgeBlockingTiming: true } });
assert.equal(automatic.reserved, true);
assert.deepEqual(automaticLifecycle, ["preference:auto", "blocking-timing:false",
	"rotation-reprojection:false", "request-session"]);

let legacyEntries = 0;
const legacyHost = {
	navigator: globalThis.navigator,
	XRGPUBinding: globalThis.XRGPUBinding,
	async surrealXREnter() { legacyEntries++; return true; },
};
const legacyProvider = globalThis.SurrealWebXRBrowserProvider.createProvider(capability, legacyHost);
await legacyProvider.activate({ Module: { preinitializedWebGPUDevice: device }, selection: {} });
assert.equal(legacyEntries, 1, "older provider scripts retain their flat/immersive activation path");
console.log("WebXR browser launcher reservation and activation test passed");
