import assert from "node:assert/strict";

globalThis.window = globalThis;
Object.defineProperty(globalThis, "navigator", {
	configurable: true,
	value: { xr: { isSessionSupported: async mode => mode === "immersive-vr" } },
});
globalThis.XRGPUBinding = function () {};
const device = { label: "shared-device" };
const lifecycle = [];
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
const reservation = await provider.prepareLaunch({ selection: {} });
assert.equal(reservation.reserved, true);
assert.deepEqual(lifecycle, ["request-session"]);
await provider.activate({ Module: { preinitializedWebGPUDevice: device }, selection: {} });
assert.deepEqual(lifecycle, ["request-session", "activate-reserved"]);

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
