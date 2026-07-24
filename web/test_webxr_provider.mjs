import assert from "node:assert/strict";

globalThis.window = globalThis;
globalThis.surrealWebGPUDeviceXRCompatible = true;
globalThis.surrealXRExperimentalWebGPU = true;
globalThis.GPUTextureUsage = { COPY_SRC: 1, COPY_DST: 2, TEXTURE_BINDING: 4, RENDER_ATTACHMENT: 16 };
const rootListeners = new Map();
globalThis.addEventListener = (name, callback) => rootListeners.set(name, callback);
globalThis.dispatchEvent = event => { const callback = rootListeners.get(event.type); if (callback) callback(event); return true; };

const nativeCalls = [];
const loopTransitions = [];
const renderedPackets = [];
const synchronousRenders = [];
const inputPackets = [];
const copies = [];
const submissions = [];
const destroyedTextures = [];
let resetCalls = 0;
let renderDeferred = null;
let textureSequence = 0;
let submittedInput = null;
let appliedInput = null;
let xrCallbackActive = false;
let poseSamplingWindow = false;
let rejectLoopResume = false;
const currentPoseOrdering = [];

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
			copyTextureToTexture(source, destination, size) {
				assert.equal(xrCallbackActive, true, "XR projection copies must remain inside the active rAF callback");
				assert.equal(poseSamplingWindow, true,
					"no promise boundary may occur between current-pose sampling and presentation");
				currentPoseOrdering.push("copy");
				commands.push({ source, destination, size });
			},
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
	_Surreal_GetWebXRFrameABIVersion: () => 4,
	ccall(name, returnType, argumentTypes, args, options) {
		nativeCalls.push(name);
		if (name === "Surreal_SetXRFrameLoopActive") {
			loopTransitions.push(args[0]);
			return rejectLoopResume && args[0] === 0 ? 0 : 1;
		}
		if (name === "Surreal_ResetWebXRPose") { resetCalls++; return undefined; }
		if (name === "Surreal_PrepareWebXRFrame") {
			assert.deepEqual(options, { async: true });
			const packet = args[0];
			const data = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
			assert.equal(data.getUint32(0, true), 4);
			assert.equal(data.getUint32(8, true), 2);
			renderedPackets.push({ bytes: Uint8Array.from(packet),
				textures: [],
				input: appliedInput && Uint8Array.from(appliedInput) });
			return renderDeferred ? renderDeferred.promise : 1;
		}
		if (name === "Surreal_RenderWebXRFrame") {
			assert.equal(options, undefined, "current-pose render must be synchronous inside XR rAF");
			assert.equal(xrCallbackActive, true, "current-pose render must run inside the active XR rAF callback");
			assert.equal(poseSamplingWindow, true,
				"no promise boundary may occur after current-pose sampling and before native render");
			currentPoseOrdering.push("render");
			synchronousRenders.push({ bytes: Uint8Array.from(args[0]),
				textures: globalThis.surrealWebXRFrameTextures.slice() });
			return 1;
		}
		if (name === "Surreal_CompleteWebXRFrame") return 1;
		if (name === "Surreal_SubmitWebXRInputSnapshot") {
			submittedInput = Uint8Array.from(args[0]); inputPackets.push(submittedInput); return 1;
		}
		if (name === "Surreal_ApplyWebXRInputSnapshot") { appliedInput = submittedInput; return 1; }
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
		this.referenceSpace = new FakeReferenceSpace(); this.endDeferred = null; this.endError = null;
		this.endThrowsSynchronously = false;
	}
	addEventListener(name, callback) { this.listeners.set(name, callback); }
	removeEventListener(name, callback) { if (this.listeners.get(name) === callback) this.listeners.delete(name); }
	emit(name, event) { const callback = this.listeners.get(name); if (callback) callback(event); }
	updateRenderState(state) { this.renderState = state; }
	async requestReferenceSpace() { return this.referenceSpace; }
	requestAnimationFrame(callback) { const handle = this.nextHandle++; this.frames.set(handle, callback); return handle; }
	cancelAnimationFrame(handle) { this.cancelledFrames.push(handle); this.frames.delete(handle); }
	fireFrame(time, frame) {
		const pending = this.frames.entries().next().value;
		assert.ok(pending, "an XR animation frame should be pending");
		this.frames.delete(pending[0]);
		xrCallbackActive = true;
		try { pending[1](time, frame); }
		finally { xrCallbackActive = false; }
	}
	end() {
		if (this.endThrowsSynchronously)
			throw this.endError || new Error("synthetic synchronous end failure");
		return (async () => {
			if (this.endDeferred) await this.endDeferred.promise;
			if (this.endError) throw this.endError;
			this.emitEnd();
		})();
	}
	emitEnd() { const callback = this.listeners.get("end"); if (callback) callback(); }
}

function button(value, pressed = false, touched = pressed) { return { value, pressed, touched }; }
const leftHapticCalls = [];
const rightHapticCalls = [];
const leftHapticActuator = {
	pulse(amplitude, durationMilliseconds) {
		leftHapticCalls.push({ amplitude, durationMilliseconds });
		return true;
	},
};
const rightHapticActuator = {
	playEffect(type, options) {
		rightHapticCalls.push({ type, options: Object.assign({}, options) });
		return Promise.resolve(true);
	},
};
function makeInputSource(handedness, x) {
	const gamepad = { mapping: "xr-standard", connected: true,
			buttons: [button(handedness === "right" ? 0 : .2, handedness === "right"), button(.4),
				button(0), button(1, true), button(1, handedness === "left"),
				button(handedness === "right" ? 1 : .5, handedness === "right")],
			axes: [.1, -.2, handedness === "left" ? -.75 : .75, .25] };
	if (handedness === "left") gamepad.hapticActuators = [leftHapticActuator];
	else gamepad.vibrationActuator = rightHapticActuator;
	return { handedness, profiles: ["oculus-touch-v3"], targetRaySpace: { kind: "aim", x },
		gripSpace: { kind: "grip", x }, gamepad };
}
function makeInputSources() { return [makeInputSource("left", -.25), makeInputSource("right", .25)]; }

const sessions = [];
let requestSessionError = null;
Object.defineProperty(globalThis, "navigator", { configurable: true, value: { xr: {
	async isSessionSupported(mode) { return mode === "immersive-vr"; },
	async requestSession(mode, options) {
		assert.equal(mode, "immersive-vr"); assert.deepEqual(options.requiredFeatures, ["webgpu"]);
		if (requestSessionError) {
			const error = requestSessionError;
			requestSessionError = null;
			throw error;
		}
		const result = new FakeSession(); sessions.push(result); return result;
	},
} } });

let useSharedTexture = false;
let invalidProjectionMode = null;
const projectionUsage = GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST;
const leftXRTexture = { kind: "xr-left", width: 800, height: 600,
	depthOrArrayLayers: 1, format: "rgba8unorm", usage: projectionUsage };
const rightXRTexture = { kind: "xr-right", width: 1024, height: 768,
	depthOrArrayLayers: 1, format: "rgba8unorm", usage: projectionUsage };
const sharedXRTexture = { kind: "xr-array", width: 1200, height: 900,
	depthOrArrayLayers: 2, format: "rgba8unorm", usage: projectionUsage };
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
		let colorTexture = useSharedTexture ? sharedXRTexture :
			(view.eye === "left" ? leftXRTexture : rightXRTexture);
		if (invalidProjectionMode === "format") colorTexture = Object.assign({}, colorTexture, { format: "bgra8unorm" });
		if (invalidProjectionMode === "usage") colorTexture = Object.assign({}, colorTexture,
			{ usage: GPUTextureUsage.RENDER_ATTACHMENT });
		return { colorTexture,
			getViewDescriptor: () => ({ baseArrayLayer: invalidProjectionMode === "layer" ?
				colorTexture.depthOrArrayLayers : (useSharedTexture ? layerIndex : 0), arrayLayerCount: 1 }),
			viewport: { x: useSharedTexture && view.eye === "right" ? 8 : 0, y: 0,
				width: invalidProjectionMode === "bounds" ? colorTexture.width + 1 :
					colorTexture.width - (useSharedTexture && view.eye === "right" ? 8 : 0),
				height: colorTexture.height } };
	}
};

const webGLProjection = (left, right, down, up, near = .1, far = 1000) =>
	new Float32Array([2 / (right - left),0,0,0, 0,2 / (up - down),0,0,
		(right + left) / (right - left), (up + down) / (up - down),
		-(far + near) / (far - near),-1, 0,0,-2 * far * near / (far - near),0]);
const webGPUProjection = matrix => {
	const result = Float32Array.from(matrix);
	for (let column = 0; column < 4; column++) result[column * 4 + 2] =
		0.5 * (result[column * 4 + 2] + result[column * 4 + 3]);
	return result;
};
const projections = {
	left: webGPUProjection(webGLProjection(-1.17, .97, -1.08, 1.12)),
	right: webGPUProjection(webGLProjection(-.97, 1.17, -1.07, 1.13)),
};
const rotatedOrientation = { x: -.08871944, y: .22108249, z: .08185684, w: .96775557 };
function makeView(eye, x) {
	return { eye, projectionMatrix: projections[eye], transform: { position: { x, y: 1.6, z: 0 },
		orientation: rotatedOrientation } };
}
function makeStereoFrame(headOffset = 0) { return {
	getViewerPose: () => {
		currentPoseOrdering.push("pose");
		poseSamplingWindow = true;
		queueMicrotask(() => { poseSamplingWindow = false; });
		return { views: [makeView("left", headOffset - .032), makeView("right", headOffset + .032)] };
	},
	getPose(space) { return { transform: { position: { x: space.x, y: 1.2, z: -.4 },
		orientation: { x: 0, y: 0, z: 0, w: 1 } } }; },
}; }
const stereoFrame = makeStereoFrame();
const nextTask = () => new Promise(resolve => setTimeout(resolve, 5));
function driveRightTriggerTransitions(session, count, startTime) {
	const right = session.inputSources.find(source => source.handedness === "right");
	for (let index = 0; index < count; index++) {
		const pressed = index % 2 === 0;
		right.gamepad.buttons[0].pressed = pressed;
		right.gamepad.buttons[0].touched = pressed;
		right.gamepad.buttons[0].value = pressed ? 1 : 0;
		session.fireFrame(startTime + index, stereoFrame);
	}
}

await import("./webxr_provider.js");
let audioGestureEvents = 0;
let lastAudioGesture = null;
globalThis.addEventListener("surrealwebxraudiogesture", event => {
	audioGestureEvents++;
	lastAudioGesture = event;
});
assert.throws(() => globalThis.surrealXRSetPresentationPreference("direct-webgpu"), TypeError);
assert.equal(globalThis.surrealXRSetPresentationPreference("webgl-bridge"), "webgl-bridge");
const forcedCapabilities = await globalThis.surrealXRGetCapabilities();
assert.equal(forcedCapabilities.supported, false);
assert.equal(forcedCapabilities.preferredMode, null);
assert.ok(forcedCapabilities.reasons.includes("webgl-bridge-unavailable"));
assert.equal(await globalThis.surrealXRRequestSession(), false);
assert.equal(globalThis.surrealXRGetState().lastErrorCode, "webxr-webgl-bridge-unavailable");
assert.equal(globalThis.surrealXRSetPresentationPreference("auto"), "auto");
const capabilities = await globalThis.surrealXRGetCapabilities();
assert.equal(capabilities.supported, true);
assert.equal(capabilities.presentationPreference, "auto");
assert.equal(capabilities.preferredMode, "direct-webgpu");
assert.equal(globalThis.surrealXRFrameABI.version, 4);

// A browser denial leaves the flat loop untouched and the next user-triggered request may retry.
requestSessionError = new Error("synthetic permission denial");
assert.equal(await globalThis.surrealXRRequestSession(), false);
assert.equal(globalThis.surrealXRGetState().lastErrorCode, "session-request-failed");
assert.deepEqual(loopTransitions, []);
assert.equal(sessions.length, 0);
await nextTask();

// Reservation stays native-free until startup has returned and activation is explicit.
assert.equal(await globalThis.surrealXRRequestSession(), true);
assert.equal(globalThis.surrealXRGetState().phase, "session-reserved");
assert.deepEqual(globalThis.surrealXRGetState().inputDiagnostics, {
	actionFocused: null, xrStandardSources: 0,
	leftButtons: null, leftAxes: null, rightButtons: null, rightAxes: null,
	nonzeroThumbstickSamples: 0,
}, "a reserved session starts with bounded diagnostics and no inferred controller data");
assert.equal(bindingCreations, 0); assert.equal(nativeCalls.length, 0); assert.equal(sessions[0].frames.size, 0);
assert.equal(await globalThis.surrealXRActivateReservedSession(), true);
assert.deepEqual(loopTransitions, [1]);
const nativeCallsBeforeAudioGesture = nativeCalls.length;
const inputPacketsBeforeAudioGesture = inputPackets.length;
sessions[0].emit("select", { isTrusted: false, inputSource: { privateProfile: "not-forwarded" } });
assert.equal(audioGestureEvents, 0, "synthetic XR select events must not unlock browser audio");
sessions[0].emit("select", { isTrusted: true, inputSource: { privateProfile: "not-forwarded" } });
assert.equal(audioGestureEvents, 1, "one trusted XR select publishes one bounded audio-unlock notification");
assert.equal(lastAudioGesture.type, "surrealwebxraudiogesture");
assert.equal("detail" in lastAudioGesture, false, "the audio notification must not carry raw XR event data");
assert.equal(nativeCalls.length, nativeCallsBeforeAudioGesture,
	"the audio notification must not call or synthesize native gameplay input");
assert.equal(inputPackets.length, inputPacketsBeforeAudioGesture,
	"the audio notification must not mutate the XR input snapshot queue");

// Haptic requests retain XRCommon hand/amplitude semantics, bound browser pulse
// duration, and resolve the live controller capability for every dispatch.
let haptics = globalThis.surrealXRGetHapticCapabilities();
assert.equal(haptics.left.mode, "pulse");
assert.equal(haptics.right.mode, "playEffect");
assert.equal(globalThis.surrealXRSubmitHaptic(0, .25, .1, 90), true);
assert.deepEqual(leftHapticCalls, [{ amplitude: .25, durationMilliseconds: 1 }]);
assert.equal(globalThis.surrealXRSubmitHaptic(1, .75, 5000, 0), true);
assert.equal(rightHapticCalls[0].type, "dual-rumble");
assert.deepEqual(rightHapticCalls[0].options, { duration: 1000, startDelay: 0,
	strongMagnitude: .75, weakMagnitude: .75 });
assert.equal(globalThis.surrealXRSubmitHaptic(2, .5, 10, 0), false);
assert.equal(globalThis.surrealXRSubmitHaptic(0, 1.1, 10, 0), false);
const rightGamepad = sessions[0].inputSources.find(source => source.handedness === "right").gamepad;
rightGamepad.vibrationActuator = {};
assert.equal(globalThis.surrealXRGetHapticCapabilities().right.status, "unsupported");
assert.equal(globalThis.surrealXRSubmitHaptic(1, .5, 10, 0), false);
rightGamepad.vibrationActuator = { playEffect() { return Promise.reject(new Error("synthetic rejection")); } };
assert.equal(globalThis.surrealXRSubmitHaptic(1, .5, 10, 0), true,
	"an asynchronous actuator result is accepted without blocking the engine frame");
await nextTask();
assert.equal(globalThis.surrealXRGetState().haptics.asyncRejected, 1);
rightGamepad.vibrationActuator = rightHapticActuator;
assert.equal(globalThis.surrealXRGetState().haptics.dispatched, 3);
assert.equal(globalThis.surrealXRGetState().haptics.rejected, 2);
assert.equal(globalThis.surrealXRGetState().haptics.droppedUnsupported, 1);

// First rAF only captures/discovers layout. The later task renders to ordinary persistent textures.
renderDeferred = deferred();
sessions[0].fireFrame(16, stereoFrame);
assert.equal(renderedPackets.length, 0, "XR rAF must not call native rendering");
await nextTask();
assert.equal(renderedPackets.length, 1);
assert.equal(globalThis.surrealXRNativeCallsBlocked, true);
const firstInput = new DataView(renderedPackets[0].input.buffer,
	renderedPackets[0].input.byteOffset, renderedPackets[0].input.byteLength);
const firstRight = 24 + 112;
assert.notEqual(firstInput.getUint32(firstRight + 8, true) & 1, 0,
	"the Quest primary-action pressed edge must survive startup neutralization");
assert.equal(firstInput.getFloat32(firstRight + 16, true), 0,
	"the regression requires a digital trigger edge independent of analog value");
assert.equal(firstInput.getFloat32(firstRight + 40 + 8, true), .75);
assert.equal(firstInput.getFloat32(firstRight + 40 + 12, true), .25,
	"xr-standard Quest thumbstick axes must remain in slots 2 and 3");
const firstInputDiagnostics = globalThis.surrealXRGetState().inputDiagnostics;
assert.deepEqual(firstInputDiagnostics, {
	actionFocused: true, xrStandardSources: 2,
	leftButtons: 6, leftAxes: 4, rightButtons: 6, rightAxes: 4,
	nonzeroThumbstickSamples: 2,
});
assert.deepEqual(Object.keys(firstInputDiagnostics).sort(), ["actionFocused", "leftAxes", "leftButtons",
	"nonzeroThumbstickSamples", "rightAxes", "rightButtons", "xrStandardSources"],
	"provider state must expose only the diagnostics allowlist");
let packet = new DataView(renderedPackets[0].bytes.buffer);
assert.equal(packet.getUint32(12, true), 2);
assert.equal(packet.getUint32(28, true), 1,
	"direct WebGPU packets must identify Chromium's WebGPU zero-to-one depth range");
assert.equal(packet.getUint32(32 + 12, true), 800);
assert.equal(packet.getUint32(32 + 128 + 12, true), 1024);
for (let viewIndex = 0; viewIndex < 2; viewIndex++) {
	const eye = viewIndex === 0 ? "left" : "right";
	const projectionOffset = 32 + viewIndex * 128 + 64;
	for (let element = 0; element < 16; element++)
		assert.ok(Math.abs(packet.getFloat32(projectionOffset + element * 4, true) - projections[eye][element]) < 1e-6,
			`${eye} direct projection element ${element} did not preserve the WebXR matrix`);
}

// A second XR frame and event callbacks make zero Wasm entries while Asyncify is suspended.
const callsWhilePending = nativeCalls.length;
const inputsBeforePending = inputPackets.length;
const rendersBeforePending = renderedPackets.length;
const stalledLeft = sessions[0].inputSources[0];
const stalledRight = sessions[0].inputSources[1];
for (let index = 0; index < 20; index++) {
	const pressed = index % 2 === 1;
	stalledRight.gamepad.buttons[0].pressed = pressed;
	stalledRight.gamepad.buttons[0].touched = pressed;
	stalledRight.gamepad.buttons[0].value = pressed ? 1 : 0;
	sessions[0].fireFrame(32 + index, stereoFrame);
}
for (let index = 0; index < 30; index++) {
	stalledRight.gamepad.axes[2] = index / 30;
	sessions[0].fireFrame(60 + index, stereoFrame);
}
await nextTask();
assert.equal(nativeCalls.length, callsWhilePending);
assert.equal(renderedPackets.length, 1, "there must be at most one producer in flight");

// Completion atomically publishes the back target. The next rAF presents it to current XR textures.
renderDeferred.resolve(1); renderDeferred = null;
await nextTask();
const currentPoseFrame = makeStereoFrame(.5);
const orderingStart = currentPoseOrdering.length;
sessions[0].fireFrame(100, currentPoseFrame);
const orderedBoundary = currentPoseOrdering.slice(orderingStart);
assert.deepEqual(orderedBoundary.slice(0, 4), ["pose", "render", "copy", "copy"],
	"current pose, synchronous native render, and projection copies must be one uninterrupted XR rAF sequence");
assert.equal(synchronousRenders.length, 1);
const currentPosePacket = new DataView(synchronousRenders[0].bytes.buffer);
assert.equal(currentPosePacket.getFloat64(16, true), 100,
	"render must consume this XR callback's timestamp, not the prepared capture timestamp");
assert.ok(Math.abs(currentPosePacket.getFloat32(32 + 36, true) - .468) < 1e-6,
	"render must consume this XR callback's headset pose");
await nextTask();
let drained = inputPackets.slice(inputsBeforePending).map(packet => new DataView(packet.buffer));
assert.equal(drained.length, 1,
	"only one retained input edge may be applied before a simulation tick");
assert.equal(globalThis.surrealXRGetState().inputQueueDepth, 20);
for (let index = 0; index < 19; index++) {
	sessions[0].fireFrame(48 + index, stereoFrame);
	await nextTask();
}
drained = inputPackets.slice(inputsBeforePending).map(packet => new DataView(packet.buffer));
assert.equal(drained.length, 20,
	">16 button edges are retained and delivered across separate simulation ticks while obsolete pose/axis frames coalesce away");
assert.equal(globalThis.surrealXRGetState().inputQueueDepth, 1,
	"the newest continuous sample remains coalesced behind retained edges");
assert.deepEqual(drained.map(packet => packet.getUint32(8, true)),
	new Array(20).fill(2));
assert.deepEqual(drained.map(packet => packet.getUint32(12, true)),
	new Array(20).fill(3));
assert.deepEqual(drained.slice(0, 20).map(packet =>
	(packet.getUint32(24 + 112 + 8, true) & 1) !== 0),
	Array.from({ length: 20 }, (_, index) => index % 2 === 1));
sessions[0].fireFrame(80, stereoFrame);
await nextTask();
assert.equal(inputPackets.slice(inputsBeforePending).length, 21,
	"the coalesced continuous sample is delivered after all retained edges");
assert.equal(globalThis.surrealXRGetState().inputQueueDepth, 0);
const sequencedRenders = renderedPackets.slice(rendersBeforePending);
assert.equal(sequencedRenders.length, 21);
assert.ok(sequencedRenders.every(render => render.input),
	"every simulation render must observe its one applied XR input packet");
assert.deepEqual(sequencedRenders.slice(0, 20).map(render => {
	const input = new DataView(render.input.buffer, render.input.byteOffset, render.input.byteLength);
	return (input.getUint32(24 + 112 + 8, true) & 1) !== 0;
}), Array.from({ length: 20 }, (_, index) => index % 2 === 1),
	"press and release edges must reach distinct simulation frames in order");
assert.ok(copies.length >= 2);
assert.equal(copies.at(-2).destination.texture, leftXRTexture);
assert.equal(copies.at(-1).destination.texture, rightXRTexture);
assert.equal(globalThis.surrealXRGetState().frames, synchronousRenders.length,
	"the presented-frame counter follows synchronous current-pose renders, not async preparation");
assert.equal(globalThis.surrealWebXRFrameTextures, null);

// A focus-loss safety packet must release native XR input even if the browser supplies no
// subsequent viewer frame on which to run simulation.
const idleSafetyInputs = inputPackets.length;
const idleSafetyRenders = renderedPackets.length;
sessions[0].visibilityState = "visible-blurred";
sessions[0].listeners.get("visibilitychange")();
assert.equal(globalThis.surrealXRGetState().inputDiagnostics.actionFocused, false);
assert.equal(globalThis.surrealXRSubmitHaptic(0, .5, 20, 0), false,
	"an unfocused session must not dispatch haptics");
assert.equal(leftHapticCalls.length, 1);
await nextTask();
assert.equal(inputPackets.length, idleSafetyInputs + 1);
assert.equal(renderedPackets.length, idleSafetyRenders,
	"input-only safety release must not invent a simulation/render frame");
let idleSafetyPacket = new DataView(inputPackets.at(-1).buffer);
assert.equal(idleSafetyPacket.getUint32(12, true), 1);
const idleClockResets = nativeCalls.filter(name => name === "Surreal_ResetBrowserFrameClock").length;
sessions[0].visibilityState = "visible";
sessions[0].listeners.get("visibilitychange")();
assert.equal(nativeCalls.filter(name => name === "Surreal_ResetBrowserFrameClock").length,
	idleClockResets + 1, "focus recovery did not reset the native frame clock");
sessions[0].fireFrame(81, stereoFrame);
await nextTask();

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

// Focus loss or a disconnected hand is a safety barrier: retained gameplay edges are discarded
// and the neutral/latest connection state is the next state observed by simulation and UI.
renderDeferred = deferred();
sessions[0].fireFrame(90, stereoFrame);
await nextTask();
const safetyInputsBefore = inputPackets.length;
for (let index = 0; index < 8; index++) {
	const pressed = index % 2 === 1;
	stalledRight.gamepad.buttons[0].pressed = pressed;
	stalledRight.gamepad.buttons[0].touched = pressed;
	stalledRight.gamepad.buttons[0].value = pressed ? 1 : 0;
	sessions[0].fireFrame(91 + index, stereoFrame);
}
assert.equal(globalThis.surrealXRGetState().inputQueueDepth, 8);
sessions[0].inputSources = [stalledRight];
sessions[0].listeners.get("inputsourceschange")({ removed: [stalledLeft], added: [] });
assert.equal(globalThis.surrealXRSubmitHaptic(0, .5, 20, 0), false,
	"a removed controller must not retain an actuator target");
assert.equal(globalThis.surrealXRGetHapticCapabilities().left.status, "disconnected");
assert.equal(globalThis.surrealXRGetState().inputQueueDepth, 1,
	"controller disconnect must purge older retained gameplay edges");
sessions[0].visibilityState = "visible-blurred";
sessions[0].listeners.get("visibilitychange")();
assert.equal(globalThis.surrealXRGetState().inputQueueDepth, 1,
	"focus loss must retain only the neutral safety state");
sessions[0].visibilityState = "visible";
const blockedClockResets = nativeCalls.filter(name => name === "Surreal_ResetBrowserFrameClock").length;
sessions[0].listeners.get("visibilitychange")();
assert.equal(nativeCalls.filter(name => name === "Surreal_ResetBrowserFrameClock").length,
	blockedClockResets, "focus recovery re-entered native code during suspended rendering");
renderDeferred.resolve(1); renderDeferred = null;
await nextTask();
assert.equal(nativeCalls.filter(name => name === "Surreal_ResetBrowserFrameClock").length,
	blockedClockResets + 1, "deferred focus recovery did not reset the clock after native work drained");
const safetyPacket = new DataView(inputPackets.at(-1).buffer);
assert.equal(inputPackets.length, safetyInputsBefore + 1);
assert.equal(safetyPacket.getUint32(8, true), 1);
assert.equal(safetyPacket.getUint32(12, true), 1);
assert.equal(safetyPacket.getUint32(24 + 8, true), 0);
sessions[0].inputSources = [stalledLeft, stalledRight];
sessions[0].listeners.get("inputsourceschange")({ removed: [], added: [stalledLeft] });

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
assert.equal(globalThis.surrealXRGetState().frames, framesBeforeLayoutChange,
	"an async preparation completion cannot present outside an XR animation callback");
sessions[0].fireFrame(104, stereoFrame);
assert.equal(globalThis.surrealXRGetState().frames, framesBeforeLayoutChange + 1,
	"only the replacement-layout render may publish");

// Exit invalidates the session immediately but defers native cleanup and texture destruction until
// an unresolved render and submitted GPU work have drained. Re-entry waits for that cleanup.
renderDeferred = deferred();
sessions[0].fireFrame(96, stereoFrame);
await nextTask();
assert.equal(globalThis.surrealXRNativeCallsBlocked, true);
const cleanupCallCount = nativeCalls.length;
const delayedEnd = deferred();
const staleHapticResult = deferred();
rightGamepad.vibrationActuator = { playEffect() { return staleHapticResult.promise; } };
assert.equal(globalThis.surrealXRSubmitHaptic(1, .5, 20, 0), true);
sessions[0].endDeferred = delayedEnd;
assert.equal(globalThis.surrealXRExit(), true);
assert.equal(sessions[0].listeners.has("select"), false,
	"explicit exit removes the old session's audio gesture listener immediately");
sessions[0].emit("select", { isTrusted: true });
assert.equal(audioGestureEvents, 1, "an exited session cannot retry browser audio");
assert.equal(globalThis.surrealXRSubmitHaptic(1, .5, 20, 0), false,
	"session exit must make browser haptics inactive immediately");
assert.equal(globalThis.surrealXRGetState().active, false,
	"successful exit request invalidates XR scheduling before the delayed end event");
assert.equal(sessions[0].frames.size, 0, "successful exit request immediately cancels the XR callback");
const reentry = globalThis.surrealXREnter();
await nextTask();
assert.equal(nativeCalls.length, cleanupCallCount, "exit/re-entry must not call Wasm during the suspended render");
assert.equal(sessions.length, 1, "a new session must wait for cleanup");
renderDeferred.resolve(1); renderDeferred = null;
await nextTask(); await nextTask();
assert.equal(sessions.length, 1, "re-entry also waits for the browser's delayed session end");
delayedEnd.resolve();
assert.equal(await reentry, true);
assert.equal(sessions.length, 2);
assert.deepEqual(globalThis.surrealXRGetState().inputDiagnostics, {
	actionFocused: null, xrStandardSources: 0,
	leftButtons: null, leftAxes: null, rightButtons: null, rightAxes: null,
	nonzeroThumbstickSamples: 0,
}, "input observations do not leak across XR session generations");
assert.equal(sessions[1].listeners.has("select"), true,
	"re-entry attaches one audio gesture listener to the new session");
sessions[0].emit("select", { isTrusted: true });
assert.equal(audioGestureEvents, 1, "the old session remains detached after re-entry");
const reentryNativeCallsBeforeAudioGesture = nativeCalls.length;
const reentryInputPacketsBeforeAudioGesture = inputPackets.length;
sessions[1].emit("select", { isTrusted: true });
assert.equal(audioGestureEvents, 2, "the new session publishes its own trusted audio gesture");
assert.equal(nativeCalls.length, reentryNativeCallsBeforeAudioGesture);
assert.equal(inputPackets.length, reentryInputPacketsBeforeAudioGesture);
assert.deepEqual(loopTransitions, [1, 0, 1]);
assert.ok(destroyedTextures.length >= 4);
staleHapticResult.resolve(false);
await nextTask();
assert.equal(globalThis.surrealXRGetState().haptics.asyncRejected, 0,
	"an old session's actuator result must not contaminate the next session generation");

// A rejected explicit end with no end event is terminal for session admission. Native/GPU cleanup
// may finish, but only the browser's eventual real end event (or a page reset) can release the gate.
sessions[1].endError = new Error("synthetic end rejection");
assert.equal(globalThis.surrealXRExit(), true);
await nextTask();
assert.equal(globalThis.surrealXRGetState().active, false);
assert.equal(globalThis.surrealXRGetState().sessionEndBlocked, true);
assert.equal(globalThis.surrealXRGetState().phase, "error");
assert.equal(globalThis.surrealXRGetState().lastErrorCode, "session-end-rejected");
assert.equal(globalThis.surrealXRGetState().lastErrorStage, "session-shutdown");
const rejectedEndRetry = globalThis.surrealXREnter();
await nextTask(); await nextTask();
assert.equal(sessions.length, 2,
	"cleanup completion must not admit a retry while rejected end has no browser end event");
sessions[1].emitEnd();
assert.equal(await rejectedEndRetry, true);
assert.equal(sessions.length, 3);
assert.equal(globalThis.surrealXRGetState().sessionEndBlocked, false);
assert.equal(globalThis.surrealXRGetState().phase, "running");
assert.equal(globalThis.surrealXRExit(), true);
await nextTask();

// A provider failure invalidates immediately, but a retry must wait until the browser confirms
// that its failed session has ended as well as waiting for native/GPU cleanup.
invalidProjectionMode = "bounds";
assert.equal(await globalThis.surrealXREnter(), true);
const delayedFailureSession = sessions.at(-1);
const sessionsBeforeFailureRetry = sessions.length;
const delayedFailureEnd = deferred();
delayedFailureSession.endDeferred = delayedFailureEnd;
delayedFailureSession.fireFrame(112, stereoFrame);
await nextTask();
assert.equal(globalThis.surrealXRGetState().phase, "error");
assert.equal(globalThis.surrealXRGetState().active, false);
assert.equal(delayedFailureSession.listeners.has("select"), false,
	"provider failure removes the session's audio gesture listener");
assert.equal(delayedFailureSession.frames.size, 0,
	"a failed session must cancel XR scheduling before its delayed end settles");
const failureRetry = globalThis.surrealXREnter();
await nextTask();
assert.equal(sessions.length, sessionsBeforeFailureRetry,
	"an immediate retry must not request a browser session while failed-session end is pending");
delayedFailureEnd.resolve();
assert.equal(await failureRetry, true);
assert.equal(sessions.length, sessionsBeforeFailureRetry + 1);
assert.equal(globalThis.surrealXRGetState().phase, "running");
assert.equal(globalThis.surrealXRExit(), true);
await nextTask();
invalidProjectionMode = null;

// Invalid compositor metadata fails before a GPU copy can become an asynchronous validation error.
for (const mode of ["layer", "format", "usage"]) {
	invalidProjectionMode = mode;
	assert.equal(await globalThis.surrealXREnter(), true);
	const invalidSession = sessions.at(-1);
	const copiesBeforeInvalidFrame = copies.length;
	invalidSession.fireFrame(120, stereoFrame);
	await nextTask();
	assert.equal(globalThis.surrealXRGetState().phase, "error", mode + " metadata must fail closed");
	assert.equal(globalThis.surrealXRGetState().lastErrorCode, "invalid-projection-subimage");
	assert.equal(copies.length, copiesBeforeInvalidFrame, "invalid " + mode + " metadata must not submit a copy");
	invalidProjectionMode = null;
}

// Overflow is terminal and capped even if the scheduled producer has not started. Exit must cancel
// that queued render pump and discard the bounded queue without making a late native render call.
assert.equal(await globalThis.surrealXREnter(), true);
const queuedPumpSession = sessions.at(-1);
const rendersBeforeQueuedExit = renderedPackets.length;
driveRightTriggerTransitions(queuedPumpSession, 256, 200);
assert.equal(globalThis.surrealXRGetState().inputQueueOverflow, false);
assert.equal(globalThis.surrealXRGetState().inputQueueDepth, 256);
driveRightTriggerTransitions(queuedPumpSession, 1, 456);
assert.equal(globalThis.surrealXRGetState().inputQueueOverflow, true);
assert.equal(globalThis.surrealXRGetState().inputQueueDepth, 256);
driveRightTriggerTransitions(queuedPumpSession, 443, 457);
assert.equal(globalThis.surrealXRExit(), true);
assert.equal(globalThis.surrealXRGetState().inputQueueOverflow, false);
assert.equal(globalThis.surrealXRGetState().inputQueueDepth, 0);
await nextTask();
assert.equal(renderedPackets.length, rendersBeforeQueuedExit,
	"exit must cancel a queued overflow render pump before native entry");

// During an already-hung native render, the 257th retained edge makes overflow terminal. Later
// frames cannot grow the queue; producer drain then fails closed and clears the bounded storage.
assert.equal(await globalThis.surrealXREnter(), true);
const overflowSession = sessions.at(-1);
renderDeferred = deferred();
overflowSession.fireFrame(1000, stereoFrame);
await nextTask();
const rendersAtOverflowStart = renderedPackets.length;
driveRightTriggerTransitions(overflowSession, 700, 1100);
assert.equal(globalThis.surrealXRGetState().inputQueueOverflow, true);
assert.equal(globalThis.surrealXRGetState().inputQueueDepth, 256);
driveRightTriggerTransitions(overflowSession, 700, 1900);
assert.equal(globalThis.surrealXRGetState().inputQueueDepth, 256,
	"terminal overflow must reject all later transitions without growing memory");
assert.equal(renderedPackets.length, rendersAtOverflowStart,
	"a hung producer must not be re-entered while overflow frames arrive");
renderDeferred.resolve(1); renderDeferred = null;
await nextTask(); await nextTask();
assert.equal(globalThis.surrealXRGetState().phase, "error");
assert.equal(globalThis.surrealXRGetState().lastErrorCode, "input-transition-overflow");
assert.equal(globalThis.surrealXRGetState().inputQueueOverflow, false);
assert.equal(globalThis.surrealXRGetState().inputQueueDepth, 0);
assert.equal(overflowSession.frames.size, 0);

// The generated callback gate reports a terminal pure-JS overflow. The provider invalidates the
// session immediately but makes no cleanup calls until the unresolved native producer drains.
assert.equal(await globalThis.surrealXREnter(), true);
const callbackOverflowSession = sessions.at(-1);
renderDeferred = deferred();
callbackOverflowSession.fireFrame(3000, stereoFrame);
await nextTask();
const callsAtCallbackOverflow = nativeCalls.length;
globalThis.dispatchEvent(new Event("surrealnativecallgateoverflow"));
assert.equal(globalThis.surrealXRGetState().active, false);
assert.equal(globalThis.surrealXRGetState().phase, "error");
assert.equal(globalThis.surrealXRGetState().lastErrorCode, "native-callback-overflow");
assert.equal(nativeCalls.length, callsAtCallbackOverflow,
	"callback overflow must defer native cleanup until the producer drains");
renderDeferred.resolve(1); renderDeferred = null;
await nextTask(); await nextTask();
assert.equal(await globalThis.surrealXREnter(), true,
	"a fresh session must be admitted after overflow cleanup completes");
assert.equal(globalThis.surrealXRGetState().phase, "running");
assert.equal(globalThis.surrealXRExit(), true);
await nextTask();
assert.equal(globalThis.surrealXRGetState().phase, "ended",
	"an explicit exit before the first frame remains a normal end");
assert.equal(globalThis.surrealXRGetState().lastErrorCode, null);

// A runtime-driven end during the reserved/activation window must retain the exact safe stage
// instead of looking like a successful exit after the browser has already returned to flat mode.
assert.equal(await globalThis.surrealXRRequestSession(), true);
const endedWhileReserved = sessions.at(-1);
endedWhileReserved.emitEnd();
assert.equal(globalThis.surrealXRGetState().phase, "error");
assert.equal(globalThis.surrealXRGetState().lastErrorCode, "session-ended-before-activation");
assert.equal(globalThis.surrealXRGetState().lastErrorStage, "session-reserved");
await nextTask();

assert.equal(await globalThis.surrealXREnter(), true);
const endedBeforeFirstFrame = sessions.at(-1);
endedBeforeFirstFrame.emitEnd();
assert.equal(globalThis.surrealXRGetState().phase, "error");
assert.equal(globalThis.surrealXRGetState().lastErrorCode, "session-ended-before-first-frame");
assert.equal(globalThis.surrealXRGetState().lastErrorStage, "frame");
assert.equal(endedBeforeFirstFrame.listeners.has("select"), false,
	"a premature browser end removes the session's audio gesture listener");
await nextTask();

assert.equal(await globalThis.surrealXREnter(), true);
const normallyEndedSession = sessions.at(-1);
normallyEndedSession.fireFrame(4000, stereoFrame);
await nextTask();
normallyEndedSession.fireFrame(4016, stereoFrame);
assert.equal(globalThis.surrealXRGetState().frames, 1);
normallyEndedSession.emitEnd();
assert.equal(globalThis.surrealXRGetState().phase, "ended",
	"a runtime end after presentation started remains a normal end");
assert.equal(globalThis.surrealXRGetState().lastErrorCode, null);
assert.equal(normallyEndedSession.listeners.has("select"), false,
	"a normal browser end removes the session's audio gesture listener");
await nextTask();

// A synchronous end throw follows the same terminal admission gate as a rejected end Promise.
assert.equal(await globalThis.surrealXREnter(), true);
const synchronouslyRejectedEnd = sessions.at(-1);
const sessionsBeforeSynchronousEndRetry = sessions.length;
synchronouslyRejectedEnd.endError = new Error("synthetic synchronous end rejection");
synchronouslyRejectedEnd.endThrowsSynchronously = true;
assert.equal(globalThis.surrealXRExit(), false);
assert.equal(globalThis.surrealXRGetState().cleanupPending, true);
const synchronousEndRetry = globalThis.surrealXREnter();
await nextTask();
assert.equal(globalThis.surrealXRGetState().sessionEndBlocked, true);
assert.equal(sessions.length, sessionsBeforeSynchronousEndRetry,
	"a synchronous end throw must keep session admission closed");
synchronouslyRejectedEnd.emitEnd();
assert.equal(await synchronousEndRetry, true);
assert.equal(sessions.length, sessionsBeforeSynchronousEndRetry + 1);
assert.equal(globalThis.surrealXRExit(), true);
await nextTask();

// Input packing remains defensive and semantic.
for (const profile of ["oculus-touch-v3", "meta-quest-touch-plus",
	"generic-trigger-squeeze-thumbstick"]) {
	const quest = makeInputSource("right", 0);
	quest.profiles = [profile];
	quest.gamepad.buttons[0] = button(0, true);
	quest.gamepad.axes = [0, 0, -.625, .375];
	const questPacket = new DataView(globalThis.surrealXRPackInputSnapshot(1,
		{ visibilityState: "visible", inputSources: [quest] }, stereoFrame, {}).buffer);
	assert.notEqual(questPacket.getUint32(24 + 8, true) & 1, 0,
		`${profile} lost the xr-standard primary-action pressed bit`);
	assert.equal(questPacket.getFloat32(24 + 16, true), 0);
	assert.equal(questPacket.getFloat32(24 + 40 + 8, true), -.625);
	assert.equal(questPacket.getFloat32(24 + 40 + 12, true), .375,
		`${profile} lost the xr-standard thumbstick layout`);
}
const unsafe = makeInputSource("left", 0); unsafe.gamepad.mapping = "";
const defensive = new DataView(globalThis.surrealXRPackInputSnapshot(1,
	{ visibilityState: "visible", inputSources: [unsafe] }, stereoFrame, {}).buffer);
assert.equal(defensive.getUint32(24 + 8, true), 0);
assert.equal(defensive.getFloat32(24 + 40 + 8, true), 0);
const compact = makeInputSource("left", 0); compact.gamepad.axes = [-.5, .75];
const compactPacket = new DataView(globalThis.surrealXRPackInputSnapshot(1,
	{ visibilityState: "visible", inputSources: [compact] }, stereoFrame, {}).buffer);
assert.equal(compactPacket.getFloat32(24 + 40 + 8, true), 0);
assert.equal(compactPacket.getFloat32(24 + 40 + 12, true), 0,
	"xr-standard axes 0/1 are touchpad slots and must not be guessed to be a thumbstick");

assert.ok(inputPackets.length >= 4);
assert.ok(resetCalls >= 4);

// Failure to return scheduler ownership is terminal: do not claim flat mode or admit another session.
assert.equal(await globalThis.surrealXREnter(), true);
const sessionsBeforeResumeFailure = sessions.length;
rejectLoopResume = true;
assert.equal(globalThis.surrealXRExit(), true);
await nextTask();
const resumeFailure = globalThis.surrealXRGetState();
assert.equal(resumeFailure.phase, "error");
assert.equal(resumeFailure.frameLoopBlocked, true);
assert.equal(resumeFailure.lastErrorCode, "engine-loop-rejected");
assert.equal(resumeFailure.lastErrorStage, "stopping-frame-loop");
assert.equal(await globalThis.surrealXRRequestSession(), false);
assert.equal(sessions.length, sessionsBeforeResumeFailure,
	"a paused flat scheduler must not admit a misleading XR retry");
console.log("WebXR current-pose render, async preparation, no-reentry, and lifecycle tests passed");
