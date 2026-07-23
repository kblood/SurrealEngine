import assert from "node:assert/strict";

globalThis.window = globalThis;
globalThis.surrealWebGPUDeviceXRCompatible = false;

const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const deferred = () => {
	let resolve;
	const promise = new Promise(complete => { resolve = complete; });
	return { promise, resolve };
};

const atlasTextures = [];
const destroyedTextures = [];
const renderDeferrals = [];
const renderCalls = [];
const nativeCalls = [];
const presentations = [];
const inputPackets = [];
let destroyedBridges = 0;
let requestOptions = null;
let bridgeCreationGate = null;
let referenceSpaceRequests = 0;
const device = {
	createTexture(description) {
		const texture = {
			name: `atlas-${atlasTextures.length}`,
			description,
			destroy() { destroyedTextures.push(texture); },
		};
		atlasTextures.push(texture);
		return texture;
	},
	queue: { submit() {}, onSubmittedWorkDone: () => Promise.resolve() },
};
globalThis.Module = {
	canvas: {}, preinitializedWebGPUDevice: device,
	_Surreal_GetWebXRFrameABIVersion: () => 3,
	ccall(name, _returnType, _types, args) {
		nativeCalls.push(name);
		if (name === "Surreal_SetXRFrameLoopActive") return 1;
		if (name === "Surreal_SubmitWebXRInputSnapshot") { inputPackets.push(Uint8Array.from(args[0])); return 1; }
		if (name === "Surreal_ApplyWebXRInputSnapshot") return 1;
		if (name === "Surreal_RenderWebXRFrame") {
			const pending = deferred();
			const call = { packet: Uint8Array.from(args[0]), textures: globalThis.surrealWebXRFrameTextures.slice() };
			renderCalls.push(call);
			renderDeferrals.push(pending);
			return pending.promise;
		}
		if (name === "Surreal_ResetWebXRPose" || name === "Surreal_ClearWebXRInputSnapshot") return undefined;
		if (name === "Surreal_GetWebXRFrameLastError" || name === "Surreal_GetWebXRInputLastError") return 0;
		throw new Error("unexpected native call " + name);
	}
};

globalThis.XRGPUBinding = undefined;
globalThis.XRWebGLLayer = function () {};
globalThis.SurrealWebXRWebGLBridge = {
	canCreateWebGL2: () => true,
	convertProjectionDepth(matrix) {
		const result = Array.from(matrix);
		for (let column = 0; column < 4; column++) result[column * 4 + 2] =
			0.5 * (result[column * 4 + 2] + result[column * 4 + 3]);
		return result;
	},
	async create({ session, canvas, device: suppliedDevice, blockingTiming }) {
		assert.ok(sessions.includes(session)); assert.equal(canvas, globalThis.Module.canvas);
		assert.equal(suppliedDevice, device);
		assert.equal(blockingTiming, true);
		let bridgeFrames = 0;
		const bridge = {
			textureFormat: "bgra8unorm",
			describeFrame: () => ({ width: 1600, height: 700, destinations: [],
				atlasViews: [{ x: 0, y: 0, width: 800, height: 700 }, { x: 800, y: 0, width: 800, height: 700 }] }),
			present: (_frame, texture, renderDevice) => {
				assert.ok(atlasTextures.includes(texture)); assert.equal(renderDevice, device);
				presentations.push(texture); bridgeFrames++;
			},
			diagnostics: () => ({ frames: bridgeFrames, errors: 0, samples: bridgeFrames ? 120 : 0,
				medianMs: bridgeFrames ? 0.5 : null, p95Ms: bridgeFrames ? 0.8 : null,
				p99Ms: bridgeFrames ? 1.1 : null, blockingTiming: true,
				layerWidth: 1832, layerHeight: 1920,
				atlasWidth: bridgeFrames ? 1600 : 0, atlasHeight: bridgeFrames ? 700 : 0,
				privatePath: "C:\\Private\\Game\\System\\Core.u" }),
			destroy: () => { destroyedBridges++; },
		};
		if (bridgeCreationGate) await bridgeCreationGate.promise;
		return bridge;
	}
};

const sessions = [];
class FakeSession {
	constructor() {
		this.listeners = new Map(); this.frames = new Map(); this.visibilityState = "visible";
		this.nextHandle = 1;
		this.inputSources = [{ handedness: "right", profiles: ["oculus-touch-v3"],
			targetRaySpace: {}, gripSpace: {}, gamepad: { mapping: "xr-standard",
				buttons: [{ value: 1, pressed: true, touched: true }], axes: [0, 0, 0, 0] } }];
	}
	addEventListener(name, callback) { this.listeners.set(name, callback); }
	updateRenderState() {}
	async requestReferenceSpace() { referenceSpaceRequests++; return { addEventListener() {} }; }
	requestAnimationFrame(callback) { const handle = this.nextHandle++; this.frames.set(handle, callback); return handle; }
	cancelAnimationFrame(handle) { this.frames.delete(handle); }
	fireFrame(time, frame) {
		const pending = this.frames.entries().next().value;
		assert.ok(pending, "a requested XR animation frame must be pending");
		this.frames.delete(pending[0]); pending[1](time, frame);
	}
	async end() { this.listeners.get("end")?.(); }
}
Object.defineProperty(globalThis, "navigator", { configurable: true, value: { xr: {
	isSessionSupported: async () => true,
	async requestSession(_mode, options) { requestOptions = options; const session = new FakeSession(); sessions.push(session); return session; }
} } });

await import("./webxr_provider.js");
assert.equal(globalThis.surrealXRSetPresentationPreference("webgl-bridge"), "webgl-bridge");
assert.throws(() => globalThis.surrealXRSetBridgeBlockingTiming("yes"), TypeError);
assert.equal(globalThis.surrealXRSetBridgeBlockingTiming(true), true);
const capabilities = await globalThis.surrealXRGetCapabilities();
assert.equal(capabilities.supported, true);
assert.equal(capabilities.directWebGPU, false);
assert.equal(capabilities.webGLBridge, true);
assert.equal(capabilities.presentationPreference, "webgl-bridge");
assert.equal(capabilities.preferredMode, "webgl-bridge");
assert.equal(await globalThis.surrealXREnter(), true);
assert.equal(requestOptions.requiredFeatures, undefined);
assert.deepEqual(requestOptions.optionalFeatures, ["local-floor"]);
assert.equal(atlasTextures.length, 0, "persistent targets are allocated after the first frame describes the atlas");

const projection = new Float32Array([2,0,0,0, 0,3,0,0, .2,-.3,-1,-1, 0,0,-.2,0]);
const view = (eye, x) => ({ eye, projectionMatrix: projection, transform: {
	position: { x, y: 1.6, z: 0 }, orientation: { x: 0, y: 0, z: 0, w: 1 }
} });
const frame = {
	getViewerPose: () => ({ views: [view("left", -.032), view("right", .032)] }),
	getPose: () => ({ transform: { position: { x: 0, y: 1.2, z: -.3 },
		orientation: { x: 0, y: 0, z: 0, w: 1 } } })
};
const session = sessions[0];

// The first producer suspends with no published front. Repeated XR rAF callbacks must
// neither present its incomplete back texture nor re-enter native code.
session.fireFrame(10, frame);
await delay(5);
assert.equal(atlasTextures.length, 2);
assert.notEqual(atlasTextures[0], atlasTextures[1]);
assert.equal(renderCalls.length, 1);
assert.deepEqual(renderCalls[0].textures, [atlasTextures[0]]);
assert.equal(presentations.length, 0);
const firstPendingNativeCount = nativeCalls.length;
session.fireFrame(20, frame);
session.fireFrame(30, frame);
session.fireFrame(40, frame);
assert.equal(renderCalls.length, 1, "an unresolved producer must not be re-entered");
assert.equal(nativeCalls.length, firstPendingNativeCount, "XR rAF must remain a pure-JS consumer while native rendering is suspended");
assert.equal(presentations.length, 0, "there is no front texture until the first producer completes");

const packet = new DataView(renderCalls[0].packet.buffer);
assert.equal(packet.getUint32(0, true), 3);
assert.equal(packet.getUint32(12, true), 1);
assert.equal(packet.getUint32(28, true), 3);
assert.equal(packet.getInt32(32 + 20, true), 0);
assert.equal(packet.getInt32(32 + 128 + 20, true), 800);
assert.ok(Math.abs(packet.getFloat32(32 + 64 + 14 * 4, true) + .1) < 1e-6);

// Publish the first completed target. Before the scheduled producer pump runs again,
// the next rAF must present exactly that immutable front.
renderDeferrals[0].resolve(1);
await Promise.resolve();
await Promise.resolve();
await Promise.resolve();
session.fireFrame(50, frame);
assert.deepEqual(presentations, [atlasTextures[0]]);
await delay(5);
assert.equal(renderCalls.length, 2);
assert.deepEqual(renderCalls[1].textures, [atlasTextures[1]], "the next producer must render into the other persistent target");

// While the second target is unresolved, every rAF re-presents the completed front;
// it must not expose the back target or make another native call.
const secondPendingNativeCount = nativeCalls.length;
session.fireFrame(60, frame);
session.fireFrame(70, frame);
session.fireFrame(80, frame);
assert.equal(renderCalls.length, 2);
assert.equal(nativeCalls.length, secondPendingNativeCount);
assert.equal(presentations.length, 4);
assert.ok(presentations.every(texture => texture === atlasTextures[0]),
	"the immutable front must remain presented while the other atlas is being produced");
assert.ok(!presentations.includes(atlasTextures[1]), "the incomplete back atlas must never be presented");

assert.equal(globalThis.surrealXRGetState().presentationMode, "webgl-bridge");
assert.equal(globalThis.surrealXRGetState().presentationPreference, "webgl-bridge");
const bridgeState = globalThis.surrealXRGetState();
assert.equal(bridgeState.bridgeDiagnostics.errors, 0);
assert.equal(bridgeState.bridgeDiagnostics.samples, 120);
assert.equal(bridgeState.bridgeDiagnostics.p99Ms, 1.1);
assert.equal(bridgeState.bridgeDiagnostics.blockingTiming, true);
assert.equal(bridgeState.bridgeDiagnostics.layerWidth, 1832);
assert.equal(bridgeState.bridgeDiagnostics.atlasWidth, 1600);
assert.equal(bridgeState.layerWidth, 1832);
assert.equal(bridgeState.atlasWidth, 1600);
assert.equal("privatePath" in bridgeState.bridgeDiagnostics, false,
	"provider state must copy only allowlisted bridge diagnostics");
const selectingInput = new DataView(inputPackets.at(-1).buffer);
assert.equal(selectingInput.getUint32(8, true), 1);
assert.equal(selectingInput.getUint32(24, true), 2);
assert.equal(selectingInput.getUint32(24 + 8, true) & 1, 1,
	"right select state must reach the same native input packet in bridge mode");

// Exit invalidates the generation and cancels rAF immediately, but resource teardown
// waits for the outstanding producer. Completing it must not publish or present stale work.
const staleFrameCallback = Array.from(session.frames.values())[0];
const presentationCountAtExit = presentations.length;
const nativeCountAtExit = nativeCalls.length;
assert.equal(globalThis.surrealXRExit(), true);
assert.equal(globalThis.surrealXRGetState().active, false);
assert.equal(globalThis.surrealXRGetState().presentationMode, null);
assert.equal(session.frames.size, 0);
assert.equal(destroyedBridges, 0, "bridge teardown must wait for the unresolved producer");
assert.equal(destroyedTextures.length, 0, "persistent targets must outlive their unresolved producer");
assert.equal(nativeCalls.length, nativeCountAtExit, "exit must not re-enter native code while its producer is suspended");

renderDeferrals[1].resolve(1);
await delay(5);
assert.equal(destroyedBridges, 1);
assert.deepEqual(destroyedTextures, atlasTextures.slice(0, 2));
assert.equal(presentations.length, presentationCountAtExit);
assert.equal(globalThis.surrealXRGetState().frames, 1,
	"completion from an invalidated generation must not publish the second target");
staleFrameCallback(90, frame);
assert.equal(presentations.length, presentationCountAtExit, "a stale XR callback must not present after exit");
assert.equal(renderCalls.length, 2, "a stale XR callback must not restart native production");
assert.equal(globalThis.surrealXRGetState().bridgeDiagnostics, null);
assert.equal(globalThis.surrealXRGetState().layerWidth, null);
assert.equal(globalThis.surrealXRGetState().atlasWidth, null);
const neutralInput = new DataView(inputPackets.at(-1).buffer);
assert.equal(neutralInput.getUint32(8, true), 0);
assert.equal(neutralInput.getUint32(12, true), 0);

assert.equal(await globalThis.surrealXREnter(), true);
assert.equal(globalThis.surrealXRGetState().reentries, 1);
assert.equal(globalThis.surrealXRGetState().presentationMode, "webgl-bridge");
assert.equal(globalThis.surrealXRGetState().bridgeDiagnostics.samples, 0,
	"bridge re-entry must start with a fresh timing window");
assert.equal(globalThis.surrealXRGetState().atlasWidth, null,
	"bridge re-entry must not retain the previous atlas dimensions before its first frame");
assert.equal(globalThis.surrealXRExit(), true);
await delay(5);
assert.equal(destroyedBridges, 2);

// If the runtime ends while asynchronous bridge setup is suspended, the completed bridge belongs
// to an invalid generation. It must be destroyed without requesting a reference space, taking the
// engine loop, or contaminating the next session's provider state.
bridgeCreationGate = deferred();
const loopTransitionsBeforeStaleBridge = nativeCalls.filter(name =>
	name === "Surreal_SetXRFrameLoopActive").length;
const referenceSpaceRequestsBeforeStaleBridge = referenceSpaceRequests;
const staleBridgeEntry = globalThis.surrealXREnter();
await delay(0);
const staleBridgeSession = sessions.at(-1);
assert.equal(globalThis.surrealXRGetState().phase, "creating-webgl-bridge");
staleBridgeSession.listeners.get("end")();
assert.equal(globalThis.surrealXRGetState().lastErrorCode, "session-ended-before-activation");
bridgeCreationGate.resolve();
assert.equal(await staleBridgeEntry, false);
await delay(5);
assert.equal(destroyedBridges, 3, "the bridge created after invalidation must be destroyed exactly once");
assert.equal(nativeCalls.filter(name => name === "Surreal_SetXRFrameLoopActive").length,
	loopTransitionsBeforeStaleBridge, "stale bridge completion must not transfer engine-loop ownership");
assert.equal(referenceSpaceRequests, referenceSpaceRequestsBeforeStaleBridge,
	"stale bridge completion must not continue session activation");
assert.equal(globalThis.surrealXRGetState().active, false);
assert.equal(globalThis.surrealXRGetState().presentationMode, null);
assert.equal(globalThis.surrealXRGetState().projectionFormat, null);
console.log("WebXR XRWebGLLayer async double-buffer and exit tests passed");
