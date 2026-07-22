import assert from "node:assert/strict";

globalThis.window = globalThis;
globalThis.surrealWebGPUDeviceXRCompatible = true;
const loopTransitions = [];
const renderedPackets = [];
let resetCalls = 0;
let appliedInputPackets = 0;
const inputPackets = [];

globalThis.Module = {
	preinitializedWebGPUDevice: {},
	ccall(name, returnType, argumentTypes, args) {
		if (name === "Surreal_SetXRFrameLoopActive") {
			loopTransitions.push(args[0]);
			return 1;
		}
		if (name === "Surreal_ResetWebXRPose") { resetCalls++; return undefined; }
		if (name === "Surreal_RenderWebXRFrame") {
			const packet = args[0];
			const data = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
			assert.equal(data.getUint32(0, true), 2);
			assert.equal(data.getUint32(8, true), 2);
			renderedPackets.push({
				bytes: new Uint8Array(packet),
				textures: globalThis.surrealWebXRFrameTextures.slice(),
			});
			return 1;
		}
		if (name === "Surreal_SubmitWebXRInputSnapshot") {
			inputPackets.push(Uint8Array.from(args[0]));
			return 1;
		}
		if (name === "Surreal_ApplyWebXRInputSnapshot") { appliedInputPackets++; return 1; }
		if (name === "Surreal_GetWebXRInputLastError") return 0;
		if (name === "Surreal_ClearWebXRInputSnapshot") return undefined;
		if (name === "Surreal_GetWebXRFrameLastError") return 0;
		throw new Error("unexpected native call: " + name);
	}
};

class FakeReferenceSpace {
	addEventListener(name, callback) {
		if (name === "reset") this.resetCallback = callback;
	}
	reset() { if (this.resetCallback) this.resetCallback(); }
}

class FakeSession {
	constructor() {
		this.listeners = new Map();
		this.visibilityState = "visible";
		this.inputSources = makeInputSources();
		this.frames = new Map();
		this.cancelledFrames = [];
		this.nextHandle = 1;
		this.referenceSpace = new FakeReferenceSpace();
	}
	addEventListener(name, callback) { this.listeners.set(name, callback); }
	updateRenderState(state) { this.renderState = state; }
	async requestReferenceSpace() { return this.referenceSpace; }
	requestAnimationFrame(callback) {
		const handle = this.nextHandle++;
		this.frames.set(handle, callback);
		return handle;
	}
	cancelAnimationFrame(handle) {
		this.cancelledFrames.push(handle);
		this.frames.delete(handle);
	}
	fireFrame(time, frame) {
		const pending = this.frames.entries().next().value;
		assert.ok(pending, "an XR animation frame should be pending");
		this.frames.delete(pending[0]);
		pending[1](time, frame);
	}
	async end() {
		const callback = this.listeners.get("end");
		if (callback) callback();
	}
}

function button(value, pressed = false, touched = pressed) { return { value, pressed, touched }; }
function makeInputSource(handedness, x) {
	return {
		handedness,
		profiles: ["oculus-touch-v3"],
		targetRaySpace: { kind: "aim", x },
		gripSpace: { kind: "grip", x },
		gamepad: {
			mapping: "xr-standard",
			buttons: [button(handedness === "right" ? 0.8 : 0.2, handedness === "right"), button(0.4),
				button(0), button(1, true), button(1, handedness === "left"),
				button(handedness === "right" ? 1 : 0.5, handedness === "right"), button(1, handedness === "right")],
			axes: [0.1, -0.2, handedness === "left" ? -0.75 : 0.75, 0.25],
		},
	};
}
function makeInputSources() { return [makeInputSource("left", -0.25), makeInputSource("right", 0.25)]; }

const sessions = [];
Object.defineProperty(globalThis, "navigator", {
	configurable: true,
	value: {
		xr: {
			async isSessionSupported(mode) { return mode === "immersive-vr"; },
			async requestSession(mode, options) {
				assert.equal(mode, "immersive-vr");
				assert.deepEqual(options.requiredFeatures, ["webgpu"]);
				const result = new FakeSession();
				sessions.push(result);
				return result;
			}
		}
	}
});

let useSharedTexture = false;
const leftTexture = { width: 800, height: 600 };
const rightTexture = { width: 1024, height: 768 };
const sharedTexture = { width: 1200, height: 900 };

globalThis.XRGPUBinding = class {
	getPreferredColorFormat() { return "rgba8unorm"; }
	createProjectionLayer(options) {
		assert.equal(options.colorFormat, "rgba8unorm");
		assert.equal(options.scaleFactor, 1);
		return {};
	}
	getViewSubImage(layer, view) {
		const layerIndex = view.eye === "left" ? 0 : 1;
		const colorTexture = useSharedTexture ? sharedTexture :
			(view.eye === "left" ? leftTexture : rightTexture);
		return {
			colorTexture,
			getViewDescriptor() {
				return { baseArrayLayer: useSharedTexture ? layerIndex : 0, arrayLayerCount: 1 };
			},
			viewport: {
				x: useSharedTexture && view.eye === "right" ? 8 : 0,
				y: 0,
				width: colorTexture.width - (useSharedTexture && view.eye === "right" ? 8 : 0),
				height: colorTexture.height,
			}
		};
	}
};

const projection = new Float32Array(16);
projection[0] = projection[5] = projection[10] = projection[15] = 1;
projection[11] = -1;
function makeView(eye, x) {
	return {
		eye,
		projectionMatrix: projection,
		transform: {
			position: { x, y: 1.6, z: 0 },
			orientation: { x: 0, y: 0, z: 0, w: 1 }
		}
	};
}
const stereoFrame = {
	getViewerPose() { return { views: [makeView("left", -0.032), makeView("right", 0.032)] }; },
	getPose(space) {
		return { transform: {
			position: { x: space.x, y: space.kind === "aim" ? 1.3 : 1.1, z: -0.4 },
			orientation: { x: 0, y: 0, z: 0, w: 1 },
		} };
	}
};

await import("./webxr_provider.js");
const capabilities = await globalThis.surrealXRGetCapabilities();
assert.equal(capabilities.supported, true);
assert.deepEqual(capabilities.reasons, []);
assert.equal(await globalThis.surrealXRIsSupported(), true);
assert.equal(globalThis.surrealXRIsColorFormatSupported("rgba8unorm"), true);
assert.equal(globalThis.surrealXRIsColorFormatSupported("rgb10a2unorm"), false);

// Distinct per-eye textures are legal and must remain distinct through native handoff.
assert.equal(await globalThis.surrealXREnter(), true);
assert.equal(await globalThis.surrealXREnter(), false);
assert.equal(globalThis.surrealXRGetState().active, true);
assert.equal(globalThis.surrealXRGetState().currentStage, "running");
assert.equal(globalThis.surrealXRGetState().enterAttempts, 1, "duplicate entry is not a new headset request");
assert.equal(globalThis.surrealXRGetState().successfulEntries, 1);
assert.equal(globalThis.surrealXRGetState().projectionFormat, "rgba8unorm");
sessions[0].fireFrame(16.0, stereoFrame);
assert.equal(renderedPackets.length, 1);
assert.equal(globalThis.surrealXRGetState().frames, 1);
let data = new DataView(renderedPackets[0].bytes.buffer);
assert.equal(data.getUint32(12, true), 2);
assert.deepEqual(renderedPackets[0].textures, [leftTexture, rightTexture]);
assert.equal(data.getUint32(32 + 4, true), 0);
assert.equal(data.getUint32(32 + 128 + 4, true), 1);
assert.equal(data.getUint32(32 + 12, true), 800);
assert.equal(data.getUint32(32 + 128 + 12, true), 1024);
assert.equal(globalThis.surrealWebXRFrameTextures, null);
assert.equal(globalThis.surrealXRInputABI.version, 1);
assert.equal(inputPackets.length, 2, "entry neutral plus first controller snapshot");
const controllerPacket = new DataView(inputPackets[1].buffer);
assert.equal(controllerPacket.getUint32(0, true), 1);
assert.equal(controllerPacket.getUint32(8, true), 2);
assert.equal(controllerPacket.getUint32(12, true), 3, "active session with action focus");
const leftBase = 24;
const rightBase = 24 + 112;
assert.equal(controllerPacket.getUint32(leftBase, true), 1);
assert.equal(controllerPacket.getUint32(rightBase, true), 2);
assert.equal(controllerPacket.getUint32(leftBase + 4, true), 7, "left aim and grip valid");
assert.equal(controllerPacket.getUint32(rightBase + 8, true) & 1, 1, "right trigger pressed");
assert.equal(controllerPacket.getUint32(rightBase + 8, true) & (1 << 3), 1 << 3, "right secondary pressed");
assert.equal(controllerPacket.getUint32(rightBase + 8, true) & (1 << 4), 0, "UA-reserved menu must not be guessed");
assert.equal(controllerPacket.getFloat32(leftBase + 40 + 8, true), -0.75);
assert.equal(controllerPacket.getFloat32(rightBase + 56, true), 0.25);

const unsafeSource = makeInputSource("left", 0);
unsafeSource.gamepad.mapping = "";
const defensive = globalThis.surrealXRPackInputSnapshot(17, { visibilityState: "visible", inputSources: [unsafeSource] }, stereoFrame, {});
const defensiveView = new DataView(defensive.buffer);
assert.equal(defensiveView.getUint32(24 + 8, true), 0, "non-standard mapping buttons must be neutral");
assert.equal(defensiveView.getFloat32(24 + 40 + 8, true), 0, "non-standard mapping axes must be neutral");

const blurred = globalThis.surrealXRPackInputSnapshot(17, { visibilityState: "visible-blurred", inputSources: [makeInputSource("right", 0.2)] }, stereoFrame, {});
const blurredView = new DataView(blurred.buffer);
assert.equal(blurredView.getUint32(12, true), 1, "blurred session remains active but loses action focus");
assert.equal(blurredView.getUint32(24 + 8, true), 0, "blurred session buttons must be neutral");
assert.equal(blurredView.getFloat32(24 + 40 + 8, true), 0, "blurred session axes must be neutral");

sessions[0].inputSources = [sessions[0].inputSources[1]];
sessions[0].listeners.get("inputsourceschange")({ removed: [{}], added: [] });
const disconnected = new DataView(inputPackets.at(-1).buffer);
assert.equal(disconnected.getUint32(8, true), 1, "disconnect must retain the remaining hand");
assert.equal(disconnected.getUint32(24, true), 2, "disconnect replacement must identify the remaining right hand");
assert.equal(disconnected.getUint32(24 + 4, true), 1, "remaining hand stays connected while frame poses are unavailable");

sessions[0].visibilityState = "visible-blurred";
sessions[0].listeners.get("visibilitychange")();
const blurPacket = new DataView(inputPackets.at(-1).buffer);
assert.equal(blurPacket.getUint32(8, true), 1, "blur must retain connected input sources");
assert.equal(blurPacket.getUint32(12, true), 1, "blur replacement must remove action focus only");
assert.equal(blurPacket.getUint32(24 + 8, true), 0, "blur replacement buttons must be neutral");
assert.equal(globalThis.surrealXRExit(), true);
await Promise.resolve();
assert.equal(globalThis.surrealXRGetState().active, false);
assert.equal(sessions[0].frames.size, 0);
assert.equal(sessions[0].cancelledFrames.length, 1);
assert.equal(globalThis.surrealXRGetState().exitRequests, 1);
assert.equal(globalThis.surrealXRGetState().endedSessions, 1);

// A shared texture array is deduplicated while preserving each eye's array layer and viewport.
useSharedTexture = true;
assert.equal(await globalThis.surrealXREnter(), true);
sessions[1].referenceSpace.reset();
sessions[1].fireFrame(32.0, stereoFrame);
data = new DataView(renderedPackets[1].bytes.buffer);
assert.equal(data.getUint32(12, true), 1);
assert.deepEqual(renderedPackets[1].textures, [sharedTexture]);
assert.equal(data.getUint32(32 + 4, true), 0);
assert.equal(data.getUint32(32 + 128 + 4, true), 0);
assert.equal(data.getUint32(32 + 8, true), 0);
assert.equal(data.getUint32(32 + 128 + 8, true), 1);
assert.equal(data.getInt32(32 + 128 + 20, true), 8);
assert.equal(data.getUint32(24, true), 1);
assert.equal(globalThis.surrealXRGetState().generation, 2);
assert.equal(globalThis.surrealXRGetState().reentries, 1);
assert.ok(globalThis.surrealXRGetState().transitions.some(item => item.type === "session-reentered"));
assert.equal(globalThis.surrealXRExit(), true);
await Promise.resolve();

// Unsupported view layouts fail with a stable code and relinquish frame-loop ownership.
assert.equal(await globalThis.surrealXREnter(), true);
sessions[2].fireFrame(48.0, {
	getViewerPose() { return { views: [makeView("left", 0)] }; }
});
await Promise.resolve();
const failedState = globalThis.surrealXRGetState();
assert.equal(failedState.active, false);
assert.equal(failedState.phase, "error");
assert.equal(failedState.lastErrorCode, "unsupported-view-configuration");
assert.equal(failedState.lastErrorStage, "frame");
assert.equal(sessions[2].frames.size, 0);

// An entry failure before frame-loop ownership retains its stable provider code and diagnostic transition.
globalThis.XRGPUBinding.prototype.getPreferredColorFormat = function () { return "unsupported-test-format"; };
assert.equal(await globalThis.surrealXREnter(), false);
const entryFailure = globalThis.surrealXRGetState();
assert.equal(entryFailure.phase, "error");
assert.equal(entryFailure.lastErrorCode, "unsupported-color-format");
assert.equal(entryFailure.lastErrorStage, "creating-projection-layer");
assert.ok(entryFailure.transitions.some(item => item.type === "entry-or-session-failed"));

assert.deepEqual(loopTransitions, [1, 0, 1, 0, 1, 0]);
assert.ok(resetCalls >= 6);
assert.equal(appliedInputPackets, inputPackets.length, "every accepted stored input snapshot must reach the native runtime");
assert.equal(new DataView(inputPackets.at(-1).buffer).getUint32(8, true), 0, "session end must leave neutral input");
assert.equal(new DataView(inputPackets.at(-1).buffer).getUint32(12, true), 0, "session end must mark input inactive");

// Capability reporting distinguishes the obsolete binding spelling and a non-XR device.
const currentBinding = globalThis.XRGPUBinding;
globalThis.XRGPUBinding = undefined;
globalThis.XRWebGPUBinding = class {};
globalThis.surrealWebGPUDeviceXRCompatible = false;
const unsupportedCapabilities = await globalThis.surrealXRGetCapabilities();
assert.equal(unsupportedCapabilities.supported, false);
assert.equal(unsupportedCapabilities.webGPUBindingName, "XRWebGPUBinding (obsolete)");
assert.ok(unsupportedCapabilities.reasons.includes("obsolete-webgpu-binding-api"));
assert.ok(unsupportedCapabilities.reasons.includes("webgpu-device-not-xr-compatible"));
assert.equal(await globalThis.surrealXREnter(), false);
assert.equal(globalThis.surrealXRGetState().lastErrorCode, "webxr-webgpu-binding-unavailable");
globalThis.XRGPUBinding = currentBinding;
console.log("WebXR capabilities, eye-texture handoff, and lifecycle tests passed");
