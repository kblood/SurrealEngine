import assert from "node:assert/strict";

globalThis.window = globalThis;
Object.defineProperty(globalThis, "navigator", {
	configurable: true,
	value: { xr: { isSessionSupported: async mode => mode === "immersive-vr" } },
});
globalThis.XRGPUBinding = function () {};

await import("./webxr_browser_app_adapter.js");
const api = globalThis.SurrealWebXRBrowserProvider;
const capability = await api.probe();
assert.equal(capability.available, true);

function deferred() {
	let resolve;
	const promise = new Promise(done => { resolve = done; });
	return { promise, resolve };
}

function makeHost() {
	const host = new EventTarget();
	host.CustomEvent = globalThis.CustomEvent;
	host.navigator = globalThis.navigator;
	host.XRGPUBinding = globalThis.XRGPUBinding;
	host.setTimeout = callback => { host.timer = callback; };
	host.lifecycle = [];
	host.providerState = { active: false, cleanupPending: false, sessionEndBlocked: false, lastError: null };
	host.surrealXRGetState = () => ({ ...host.providerState });
	host.surrealXRSetPresentationPreference = value => host.lifecycle.push("preference:" + value);
	host.surrealXRSetBridgeBlockingTiming = value => host.lifecycle.push("blocking:" + value);
	host.surrealXRSetBridgeRotationReprojection = value => host.lifecycle.push("reprojection:" + value);
	return host;
}

{
	const host = makeHost();
	const request = deferred();
	const activation = deferred();
	host.surrealXRRequestSession = () => {
		host.lifecycle.push("request-session");
		return request.promise;
	};
	host.surrealXRActivateReservedSession = () => {
		host.lifecycle.push("activate-reserved");
		return activation.promise;
	};
	host.surrealXRExit = () => {
		host.lifecycle.push("exit");
		host.providerState = { ...host.providerState, active: false, cleanupPending: true };
		return true;
	};
	let mainCalls = 0;
	const identities = {
		heap: new ArrayBuffer(64), map: { name: "DM-Turbine" }, player: { id: 7 },
		renderer: { backend: "webgl2" }, audio: { context: "same" }, mounts: new Map([["/gamedata", true]]),
	};
	const module = { preinitializedWebGPUDevice: { label: "shared-device" }, identities,
		callMain() { mainCalls++; } };
	const controller = api.createSessionController(capability, null, host);
	controller.attachModule(module);
	assert.equal(controller.status().state, api.SESSION_STATES.WAITING_FOR_ENGINE);
	assert.equal(await controller.enter(), false, "entry was admitted before native startup");
	controller.engineStarted();
	assert.equal(controller.status().state, api.SESSION_STATES.FLAT_RUNNING);

	const entering = controller.enter();
	assert.deepEqual(host.lifecycle, ["preference:auto", "blocking:false", "reprojection:false", "request-session"],
		"requestSession was not invoked synchronously from the entry call");
	assert.equal(controller.status().state, api.SESSION_STATES.REQUESTING_SESSION);
	request.resolve(true);
	await Promise.resolve();
	assert.equal(controller.status().state, api.SESSION_STATES.ACTIVATING_XR);
	assert.equal(host.surrealWebGPUDevice, module.preinitializedWebGPUDevice);
	activation.resolve(true);
	assert.equal(await entering, true);
	host.providerState = { ...host.providerState, active: true };
	assert.equal(controller.status().state, api.SESSION_STATES.IMMERSIVE_RUNNING);

	assert.equal(await controller.exit(), true);
	assert.equal(controller.status().state, api.SESSION_STATES.ENDING_XR);
	host.providerState = { ...host.providerState, cleanupPending: false };
	controller.refreshFromProvider();
	assert.equal(controller.status().state, api.SESSION_STATES.FLAT_RUNNING);

	const secondRequest = deferred();
	host.surrealXRRequestSession = () => { host.lifecycle.push("request-session-2"); return secondRequest.promise; };
	host.surrealXRActivateReservedSession = async () => { host.lifecycle.push("activate-reserved-2"); return true; };
	const reentering = controller.enter();
	secondRequest.resolve(true);
	assert.equal(await reentering, true);
	assert.equal(controller.status().state, api.SESSION_STATES.IMMERSIVE_RUNNING,
		"the same controller could not re-enter without restarting the engine");
	host.providerState = { ...host.providerState, active: true, cleanupPending: false, phase: "running" };
	host.dispatchEvent(new Event("surrealwebxrgameexit"));
	assert.equal(controller.status().state, api.SESSION_STATES.ENDING_XR,
		"the in-headset game menu exit did not request a return to flat play");
	host.providerState = { ...host.providerState, active: false, cleanupPending: false, phase: "ended" };
	controller.refreshFromProvider();
	assert.equal(controller.status().state, api.SESSION_STATES.FLAT_RUNNING);
	assert.equal(await controller.enter(), true, "game-menu exit prevented later re-entry");
	host.providerState = { ...host.providerState, active: false, cleanupPending: false, phase: "ended" };
	controller.refreshFromProvider();
	assert.equal(controller.status().state, api.SESSION_STATES.FLAT_RUNNING,
		"a headset/system session end did not restore flat play");
	assert.equal(mainCalls, 0, "an XR transition called native main a second time");
	assert.equal(module.identities, identities);
	for (const [name, value] of Object.entries(identities))
		assert.equal(module.identities[name], value, name + " identity changed across flat/XR transitions");
}

{
	const host = makeHost();
	host.providerState.lastError = "headset permission denied";
	host.surrealXRRequestSession = () => Promise.resolve(false);
	const controller = api.createSessionController(capability, null, host);
	controller.attachModule({});
	controller.engineStarted();
	assert.equal(await controller.enter(), false);
	assert.equal(controller.status().state, api.SESSION_STATES.FLAT_RUNNING);
	assert.match(controller.status().lastError, /permission denied.*still running.*retry/i);
	assert.equal(await controller.enter(), false, "a denied session made retry impossible");
}

{
	const host = makeHost();
	host.surrealXRRequestSession = async () => true;
	host.surrealXRActivateReservedSession = async () => {
		host.providerState = { ...host.providerState, phase: "error", lastError: "projection layer setup failed" };
		return false;
	};
	const controller = api.createSessionController(capability, null, host);
	controller.attachModule({});
	controller.engineStarted();
	assert.equal(await controller.enter(), false);
	assert.equal(controller.status().state, api.SESSION_STATES.FLAT_RUNNING);
	assert.match(controller.status().lastError, /projection layer setup failed.*still running/i);
}

{
	const host = makeHost();
	const delayedRequest = deferred();
	let activations = 0;
	host.surrealXRRequestSession = () => delayedRequest.promise;
	host.surrealXRActivateReservedSession = async () => { activations++; return true; };
	const controller = api.createSessionController(capability, null, host);
	controller.attachModule({});
	controller.engineStarted();
	const staleEntry = controller.enter();
	host.providerState = { ...host.providerState, phase: "error", lastError: "session ended during request" };
	controller.refreshFromProvider();
	delayedRequest.resolve(true);
	assert.equal(await staleEntry, false);
	assert.equal(activations, 0, "a stale request completion activated a superseded session");
	assert.equal(controller.status().state, api.SESSION_STATES.FLAT_RUNNING);
}

{
	const unavailable = { available: false, code: "immersive-vr-unsupported", message: "No headset found." };
	const host = makeHost();
	host.surrealXRRequestSession = () => { throw new Error("unavailable controller requested a session"); };
	const controller = api.createSessionController(unavailable, null, host);
	controller.engineStarted();
	assert.equal(await controller.enter(), false);
	assert.equal(controller.status().state, api.SESSION_STATES.FLAT_RUNNING);
}

console.log("WebXR post-launch session controller lifecycle test passed");
