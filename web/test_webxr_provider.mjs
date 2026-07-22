import assert from "node:assert/strict";

globalThis.window = globalThis;
const loopTransitions = [];
let renderedFrames = 0;
let resetCalls = 0;

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
		if (name === "Surreal_GetWebXRFrameLastError") return 0;
		throw new Error("unexpected native call: " + name);
	}
};

class FakeSession {
	constructor() {
		this.listeners = new Map();
		this.nextFrame = null;
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
	getViewerPose() { return { views: [makeView("left", -0.032), makeView("right", 0.032)] }; }
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
assert.equal(globalThis.surrealXRExit(), true);
await Promise.resolve();
assert.equal(globalThis.surrealXRGetState().active, false);

assert.equal(await globalThis.surrealXREnter(), true);
assert.equal(globalThis.surrealXRGetState().generation, 2);
assert.equal(globalThis.surrealXRExit(), true);
await Promise.resolve();

assert.deepEqual(loopTransitions, [1, 0, 1, 0]);
assert.ok(resetCalls >= 4);
console.log("WebXR lifecycle enter/frame/exit/re-enter test passed");
