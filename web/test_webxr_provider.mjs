import assert from "node:assert/strict";

globalThis.window = globalThis;
const loopTransitions = [];
let renderedFrames = 0;
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
			const view = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
			assert.equal(view.getUint32(0, true), 1);
			assert.equal(view.getUint32(8, true), 2);
			renderedFrames++;
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

class FakeSession {
	constructor() {
		this.listeners = new Map();
		this.nextFrame = null;
		this.visibilityState = "visible";
		this.inputSources = makeInputSources();
	}
	addEventListener(name, callback) { this.listeners.set(name, callback); }
	async updateRenderState() {}
	async requestReferenceSpace() { return { addEventListener() {} }; }
	requestAnimationFrame(callback) { this.nextFrame = callback; }
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
			async requestSession() {
				const result = new FakeSession();
				sessions.push(result);
				return result;
			}
		}
	}
});

globalThis.XRGPUBinding = class {
	getPreferredColorFormat() { return "rgba8unorm"; }
	createProjectionLayer(options) {
		assert.equal(options.colorFormat, "rgba8unorm");
		return {};
	}
	getViewSubImage(layer, view) {
		return {
			colorTexture: sharedTexture,
			getViewDescriptor() { return { baseArrayLayer: view.eye === "left" ? 0 : 1 }; },
			viewport: { x: 0, y: 0, width: 800, height: 600 }
		};
	}
};

const sharedTexture = { width: 800, height: 600 };
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
const frame = {
	getViewerPose() { return { views: [makeView("left", -0.032), makeView("right", 0.032)] }; },
	getPose(space) {
		return { transform: {
			position: { x: space.x, y: space.kind === "aim" ? 1.3 : 1.1, z: -0.4 },
			orientation: { x: 0, y: 0, z: 0, w: 1 },
		} };
	}
};

await import("./webxr_provider.js");
assert.equal(await globalThis.surrealXRIsSupported(), true);
assert.equal(globalThis.surrealXRIsColorFormatSupported("rgba8unorm"), true);
assert.equal(globalThis.surrealXRIsColorFormatSupported("rgb10a2unorm"), false);
assert.equal(await globalThis.surrealXREnter(), true);
assert.equal(await globalThis.surrealXREnter(), false);
assert.equal(globalThis.surrealXRGetState().active, true);
assert.equal(globalThis.surrealXRGetState().projectionFormat, "rgba8unorm");
sessions[0].nextFrame(16.0, frame);
assert.equal(renderedFrames, 1);
assert.equal(globalThis.surrealXRGetState().frames, 1);
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
const defensive = globalThis.surrealXRPackInputSnapshot(17, { visibilityState: "visible", inputSources: [unsafeSource] }, frame, {});
const defensiveView = new DataView(defensive.buffer);
assert.equal(defensiveView.getUint32(24 + 8, true), 0, "non-standard mapping buttons must be neutral");
assert.equal(defensiveView.getFloat32(24 + 40 + 8, true), 0, "non-standard mapping axes must be neutral");

const blurred = globalThis.surrealXRPackInputSnapshot(17, { visibilityState: "visible-blurred", inputSources: [makeInputSource("right", 0.2)] }, frame, {});
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

assert.equal(await globalThis.surrealXREnter(), true);
assert.equal(globalThis.surrealXRGetState().generation, 2);
assert.equal(globalThis.surrealXRExit(), true);
await Promise.resolve();

assert.deepEqual(loopTransitions, [1, 0, 1, 0]);
assert.ok(resetCalls >= 4);
assert.equal(appliedInputPackets, inputPackets.length, "every accepted stored input snapshot must reach the native runtime");
assert.equal(new DataView(inputPackets.at(-1).buffer).getUint32(8, true), 0, "session end must leave neutral input");
assert.equal(new DataView(inputPackets.at(-1).buffer).getUint32(12, true), 0, "session end must mark input inactive");
console.log("WebXR lifecycle and packed controller snapshot tests passed");
