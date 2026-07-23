import assert from "node:assert/strict";

globalThis.window = globalThis;
globalThis.surrealWebGPUDeviceXRCompatible = true;
globalThis.GPUTextureUsage = { COPY_SRC: 1, COPY_DST: 2, TEXTURE_BINDING: 4, RENDER_ATTACHMENT: 16 };

const nativeCalls = [];
const loopTransitions = [];
const renderedPackets = [];
const inputPackets = [];
const copies = [];
const submissions = [];
const destroyedTextures = [];
let resetCalls = 0;
let renderDeferred = null;
let textureSequence = 0;

function deferred() {
	let resolve, reject;
	const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
	return { promise, resolve, reject };
}

const device = {
	createTexture(description) {
		const size = Array.isArray(description.size) ? description.size :
			[description.size.width, description.size.height, description.size.depthOrArrayLayers];
		return { id: ++textureSequence, width: size[0], height: size[1], format: description.format,
			description, destroy() { destroyedTextures.push(this.id); } };
	},
	createCommandEncoder() {
		const commands = [];
		return {
			copyTextureToTexture(source, destination, size) { commands.push({ source, destination, size }); },
			finish() { copies.push(...commands); return { commands }; },
		};
	},
	queue: {
		submit(commands) { submissions.push(commands); },
		onSubmittedWorkDone() { return Promise.resolve(); },
	},
};

globalThis.Module = {
	preinitializedWebGPUDevice: device,
	_Surreal_GetWebXRFrameABIVersion: () => 3,
	ccall(name, returnType, argumentTypes, args, options) {
		nativeCalls.push(name);
		if (name === "Surreal_SetXRFrameLoopActive") { loopTransitions.push(args[0]); return 1; }
		if (name === "Surreal_ResetWebXRPose") { resetCalls++; return undefined; }
		if (name === "Surreal_RenderWebXRFrame") {
			assert.deepEqual(options, { async: true });
			const packet = args[0];
			const data = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
			assert.equal(data.getUint32(0, true), 3);
			assert.equal(data.getUint32(8, true), 2);
			renderedPackets.push({ bytes: Uint8Array.from(packet),
				textures: globalThis.surrealWebXRFrameTextures.slice() });
			return renderDeferred ? renderDeferred.promise : 1;
		}
		if (name === "Surreal_SubmitWebXRInputSnapshot") { inputPackets.push(Uint8Array.from(args[0])); return 1; }
		if (name === "Surreal_ApplyWebXRInputSnapshot") return 1;
		if (name === "Surreal_GetWebXRInputLastError" || name === "Surreal_GetWebXRFrameLastError") return 0;
		if (name === "Surreal_ClearWebXRInputSnapshot") return undefined;
		throw new Error("unexpected native call: " + name);
	},
};

class FakeReferenceSpace {
	addEventListener(name, callback) { if (name === "reset") this.resetCallback = callback; }
	reset() { if (this.resetCallback) this.resetCallback(); }
}

class FakeSession {
	constructor() {
		this.listeners = new Map(); this.visibilityState = "visible"; this.inputSources = makeInputSources();
		this.frames = new Map(); this.cancelledFrames = []; this.nextHandle = 1;
		this.referenceSpace = new FakeReferenceSpace();
	}
	addEventListener(name, callback) { this.listeners.set(name, callback); }
	updateRenderState(state) { this.renderState = state; }
	async requestReferenceSpace() { return this.referenceSpace; }
	requestAnimationFrame(callback) { const handle = this.nextHandle++; this.frames.set(handle, callback); return handle; }
	cancelAnimationFrame(handle) { this.cancelledFrames.push(handle); this.frames.delete(handle); }
	fireFrame(time, frame) {
		const pending = this.frames.entries().next().value;
		assert.ok(pending, "an XR animation frame should be pending");
		this.frames.delete(pending[0]); pending[1](time, frame);
	}
	async end() { const callback = this.listeners.get("end"); if (callback) callback(); }
}

function button(value, pressed = false, touched = pressed) { return { value, pressed, touched }; }
function makeInputSource(handedness, x) {
	return { handedness, profiles: ["oculus-touch-v3"], targetRaySpace: { kind: "aim", x },
		gripSpace: { kind: "grip", x }, gamepad: { mapping: "xr-standard",
			buttons: [button(handedness === "right" ? .8 : .2, handedness === "right"), button(.4),
				button(0), button(1, true), button(1, handedness === "left"),
				button(handedness === "right" ? 1 : .5, handedness === "right")],
			axes: [.1, -.2, handedness === "left" ? -.75 : .75, .25] } };
}
function makeInputSources() { return [makeInputSource("left", -.25), makeInputSource("right", .25)]; }

const sessions = [];
Object.defineProperty(globalThis, "navigator", { configurable: true, value: { xr: {
	async isSessionSupported(mode) { return mode === "immersive-vr"; },
	async requestSession(mode, options) {
		assert.equal(mode, "immersive-vr"); assert.deepEqual(options.requiredFeatures, ["webgpu"]);
		const result = new FakeSession(); sessions.push(result); return result;
	},
} } });

let useSharedTexture = false;
const leftXRTexture = { kind: "xr-left", width: 800, height: 600 };
const rightXRTexture = { kind: "xr-right", width: 1024, height: 768 };
const sharedXRTexture = { kind: "xr-array", width: 1200, height: 900 };
let bindingCreations = 0;
globalThis.XRGPUBinding = class {
	constructor() { bindingCreations++; }
	getPreferredColorFormat() { return "rgba8unorm"; }
	createProjectionLayer(options) {
		assert.equal(options.colorFormat, "rgba8unorm"); assert.equal(options.scaleFactor, 1);
		assert.equal(options.textureUsage, GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST);
		return { textureWidth: 2048, textureHeight: 1024 };
	}
	getViewSubImage(layer, view) {
		const layerIndex = view.eye === "left" ? 0 : 1;
		const colorTexture = useSharedTexture ? sharedXRTexture :
			(view.eye === "left" ? leftXRTexture : rightXRTexture);
		return { colorTexture,
			getViewDescriptor: () => ({ baseArrayLayer: useSharedTexture ? layerIndex : 0, arrayLayerCount: 1 }),
			viewport: { x: useSharedTexture && view.eye === "right" ? 8 : 0, y: 0,
				width: colorTexture.width - (useSharedTexture && view.eye === "right" ? 8 : 0),
				height: colorTexture.height } };
	}
};

const projection = new Float32Array(16);
projection[0] = projection[5] = projection[10] = projection[15] = 1; projection[11] = -1;
function makeView(eye, x) {
	return { eye, projectionMatrix: projection, transform: { position: { x, y: 1.6, z: 0 },
		orientation: { x: 0, y: 0, z: 0, w: 1 } } };
}
const stereoFrame = {
	getViewerPose: () => ({ views: [makeView("left", -.032), makeView("right", .032)] }),
	getPose(space) { return { transform: { position: { x: space.x, y: 1.2, z: -.4 },
		orientation: { x: 0, y: 0, z: 0, w: 1 } } }; },
};
const nextTask = () => new Promise(resolve => setTimeout(resolve, 5));

await import("./webxr_provider.js");
const capabilities = await globalThis.surrealXRGetCapabilities();
assert.equal(capabilities.supported, true);
assert.equal(globalThis.surrealXRFrameABI.version, 3);

// Reservation stays native-free until startup has returned and activation is explicit.
assert.equal(await globalThis.surrealXRRequestSession(), true);
assert.equal(globalThis.surrealXRGetState().phase, "session-reserved");
assert.equal(bindingCreations, 0); assert.equal(nativeCalls.length, 0); assert.equal(sessions[0].frames.size, 0);
assert.equal(await globalThis.surrealXRActivateReservedSession(), true);
assert.deepEqual(loopTransitions, [1]);

// First rAF only captures/discovers layout. The later task renders to ordinary persistent textures.
renderDeferred = deferred();
sessions[0].fireFrame(16, stereoFrame);
assert.equal(renderedPackets.length, 0, "XR rAF must not call native rendering");
await nextTask();
assert.equal(renderedPackets.length, 1);
assert.equal(globalThis.surrealXRNativeCallsBlocked, true);
assert.equal(renderedPackets[0].textures.length, 2);
assert.ok(renderedPackets[0].textures.every(texture => texture !== leftXRTexture && texture !== rightXRTexture));
let packet = new DataView(renderedPackets[0].bytes.buffer);
assert.equal(packet.getUint32(12, true), 2);
assert.equal(packet.getUint32(32 + 12, true), 800);
assert.equal(packet.getUint32(32 + 128 + 12, true), 1024);

// A second XR frame and event callbacks make zero Wasm entries while Asyncify is suspended.
const callsWhilePending = nativeCalls.length;
const inputsBeforePending = inputPackets.length;
sessions[0].fireFrame(32, stereoFrame);
sessions[0].inputSources = [sessions[0].inputSources[1]];
sessions[0].listeners.get("inputsourceschange")({ removed: [{}], added: [] });
sessions[0].visibilityState = "visible-blurred";
sessions[0].listeners.get("visibilitychange")();
await nextTask();
assert.equal(nativeCalls.length, callsWhilePending);
assert.equal(renderedPackets.length, 1, "there must be at most one producer in flight");

// Completion atomically publishes the back target. The next rAF presents it to current XR textures.
renderDeferred.resolve(1); renderDeferred = null;
await nextTask();
const drained = inputPackets.slice(inputsBeforePending).map(packet => new DataView(packet.buffer));
assert.equal(drained.length, 3, "frame, disconnect, and blur snapshots are drained without collapsing transitions");
assert.deepEqual(drained.map(packet => packet.getUint32(8, true)), [2, 1, 1]);
assert.deepEqual(drained.map(packet => packet.getUint32(12, true)), [3, 3, 1]);
sessions[0].fireFrame(48, stereoFrame);
assert.equal(copies.length, 2);
assert.equal(copies[0].destination.texture, leftXRTexture);
assert.equal(copies[1].destination.texture, rightXRTexture);
assert.equal(submissions.length, 1);
await nextTask();
assert.equal(renderedPackets.length, 3, "each completed producer starts at most one queued replacement");
assert.equal(globalThis.surrealXRGetState().frames, 3);
assert.equal(globalThis.surrealWebXRFrameTextures, null);

// Shared XR arrays affect only the late copy destinations; native still receives two safe 2D targets.
useSharedTexture = true;
sessions[0].fireFrame(64, stereoFrame);
await nextTask();
sessions[0].fireFrame(72, stereoFrame);
assert.equal(copies.at(-2).destination.texture, sharedXRTexture);
assert.equal(copies.at(-2).destination.origin.z, 0);
assert.equal(copies.at(-1).destination.texture, sharedXRTexture);
assert.equal(copies.at(-1).destination.origin.z, 1);
await nextTask();

// A layout epoch change while rendering suppresses the old completion and waits for a correctly
// sized target before presenting again.
renderDeferred = deferred();
sessions[0].fireFrame(80, stereoFrame);
await nextTask();
const framesBeforeLayoutChange = globalThis.surrealXRGetState().frames;
const layoutPendingCalls = nativeCalls.length;
useSharedTexture = false;
sessions[0].fireFrame(88, stereoFrame);
assert.equal(nativeCalls.length, layoutPendingCalls);
renderDeferred.resolve(1); renderDeferred = null;
await nextTask(); await nextTask();
assert.equal(globalThis.surrealXRGetState().frames, framesBeforeLayoutChange + 1,
	"only the replacement-layout render may publish");

// Exit invalidates the session immediately but defers native cleanup and texture destruction until
// an unresolved render and submitted GPU work have drained. Re-entry waits for that cleanup.
renderDeferred = deferred();
sessions[0].fireFrame(96, stereoFrame);
await nextTask();
assert.equal(globalThis.surrealXRNativeCallsBlocked, true);
const cleanupCallCount = nativeCalls.length;
assert.equal(globalThis.surrealXRExit(), true);
const reentry = globalThis.surrealXREnter();
await nextTask();
assert.equal(nativeCalls.length, cleanupCallCount, "exit/re-entry must not call Wasm during the suspended render");
assert.equal(sessions.length, 1, "a new session must wait for cleanup");
renderDeferred.resolve(1); renderDeferred = null;
await nextTask(); await nextTask();
assert.equal(await reentry, true);
assert.equal(sessions.length, 2);
assert.deepEqual(loopTransitions, [1, 0, 1]);
assert.ok(destroyedTextures.length >= 4);
assert.equal(globalThis.surrealXRExit(), true);
await nextTask();

// Input packing remains defensive and semantic.
const unsafe = makeInputSource("left", 0); unsafe.gamepad.mapping = "";
const defensive = new DataView(globalThis.surrealXRPackInputSnapshot(1,
	{ visibilityState: "visible", inputSources: [unsafe] }, stereoFrame, {}).buffer);
assert.equal(defensive.getUint32(24 + 8, true), 0);
assert.equal(defensive.getFloat32(24 + 40 + 8, true), 0);

assert.ok(inputPackets.length >= 4);
assert.ok(resetCalls >= 4);
console.log("WebXR persistent-target, deferred-render, no-reentry, and lifecycle tests passed");
