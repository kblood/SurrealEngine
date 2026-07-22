import assert from "node:assert/strict";

globalThis.window = globalThis;
Object.defineProperty(globalThis, "navigator", {
	configurable: true,
	value: { xr: {} },
});
globalThis.XRGPUBinding = function () {};
const device = { label: "shared-device" };
let enterCalls = 0;
globalThis.surrealXREnter = async () => {
	enterCalls++;
	assert.equal(globalThis.surrealWebGPUDevice, device);
	return true;
};

await import("./webxr_browser_app_adapter.js");
const provider = globalThis.SurrealWebXRBrowserProvider.createProvider();
assert.equal(provider.id, "webxr");
assert.equal(provider.isAvailable(), true);
await provider.activate({ Module: { preinitializedWebGPUDevice: device }, selection: {} });
assert.equal(enterCalls, 1);
console.log("WebXR shared browser launcher adapter test passed");
