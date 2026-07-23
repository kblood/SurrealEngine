import assert from "node:assert/strict";

globalThis.window = globalThis;
globalThis.surrealWebGPUDeviceXRCompatible = false;
const atlasTexture = { width: 1600, height: 700 };
let rendered = null, presented = 0, destroyed = 0, requestOptions = null;
const inputPackets = [];
const device = {
	createTexture(description) { atlasTexture.description = description; atlasTexture.destroy = () => {}; return atlasTexture; },
	queue: { submit() {}, onSubmittedWorkDone: () => Promise.resolve() },
};
globalThis.Module = {
	canvas: {}, preinitializedWebGPUDevice: device,
	_Surreal_GetWebXRFrameABIVersion: () => 3,
	ccall(name, _returnType, _types, args) {
		if (name === "Surreal_SetXRFrameLoopActive") return 1;
		if (name === "Surreal_SubmitWebXRInputSnapshot") { inputPackets.push(Uint8Array.from(args[0])); return 1; }
		if (name === "Surreal_ApplyWebXRInputSnapshot") return 1;
		if (name === "Surreal_RenderWebXRFrame") {
			rendered = { packet: Uint8Array.from(args[0]), textures: globalThis.surrealWebXRFrameTextures.slice() };
			return 1;
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
	async create({ session, canvas, device: suppliedDevice }) {
		assert.ok(sessions.includes(session)); assert.equal(canvas, globalThis.Module.canvas);
		assert.equal(suppliedDevice, device);
		let bridgeFrames = 0;
		return {
			textureFormat: "bgra8unorm",
			describeFrame: () => ({ width: 1600, height: 700, destinations: [],
				atlasViews: [{ x: 0, y: 0, width: 800, height: 700 }, { x: 800, y: 0, width: 800, height: 700 }] }),
			present: (_frame, texture, renderDevice) => {
				assert.equal(texture, atlasTexture); assert.equal(renderDevice, device);
				presented++; bridgeFrames++;
			},
			diagnostics: () => ({ frames: bridgeFrames, errors: 0, samples: bridgeFrames ? 120 : 0,
				medianMs: bridgeFrames ? 0.5 : null, p95Ms: bridgeFrames ? 0.8 : null,
				p99Ms: bridgeFrames ? 1.1 : null, blockingTiming: false,
				layerWidth: 1832, layerHeight: 1920,
				atlasWidth: bridgeFrames ? 1600 : 0, atlasHeight: bridgeFrames ? 700 : 0,
				privatePath: "C:\\Private\\Game\\System\\Core.u" }),
			destroy: () => { destroyed++; },
		};
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
	async requestReferenceSpace() { return { addEventListener() {} }; }
	requestAnimationFrame(callback) { const handle = this.nextHandle++; this.frames.set(handle, callback); return handle; }
	cancelAnimationFrame(handle) { this.frames.delete(handle); }
	fireFrame(time, frame) { const pending = this.frames.entries().next().value; this.frames.delete(pending[0]); pending[1](time, frame); }
	async end() { this.listeners.get("end")?.(); }
}
Object.defineProperty(globalThis, "navigator", { configurable: true, value: { xr: {
	isSessionSupported: async () => true,
	async requestSession(_mode, options) { requestOptions = options; const session = new FakeSession(); sessions.push(session); return session; }
} } });

await import("./webxr_provider.js");
const capabilities = await globalThis.surrealXRGetCapabilities();
assert.equal(capabilities.supported, true);
assert.equal(capabilities.directWebGPU, false);
assert.equal(capabilities.webGLBridge, true);
assert.equal(capabilities.preferredMode, "webgl-bridge");
assert.equal(await globalThis.surrealXREnter(), true);
assert.equal(requestOptions.requiredFeatures, undefined);
assert.deepEqual(requestOptions.optionalFeatures, ["local-floor"]);
const projection = new Float32Array([2,0,0,0, 0,3,0,0, .2,-.3,-1,-1, 0,0,-.2,0]);
const view = (eye, x) => ({ eye, projectionMatrix: projection, transform: {
	position: { x, y: 1.6, z: 0 }, orientation: { x: 0, y: 0, z: 0, w: 1 }
} });
sessions[0].fireFrame(10, {
	getViewerPose: () => ({ views: [view("left", -.032), view("right", .032)] }),
	getPose: () => ({ transform: { position: { x: 0, y: 1.2, z: -.3 },
		orientation: { x: 0, y: 0, z: 0, w: 1 } } })
});
assert.equal(presented, 0);
await new Promise(resolve => setTimeout(resolve, 5));
assert.deepEqual(rendered.textures, [atlasTexture]);
const packet = new DataView(rendered.packet.buffer);
assert.equal(packet.getUint32(0, true), 3);
assert.equal(packet.getUint32(12, true), 1);
assert.equal(packet.getUint32(28, true), 3);
assert.equal(packet.getInt32(32 + 20, true), 0);
assert.equal(packet.getInt32(32 + 128 + 20, true), 800);
assert.ok(Math.abs(packet.getFloat32(32 + 64 + 14 * 4, true) + .1) < 1e-6);
sessions[0].fireFrame(20, {
	getViewerPose: () => ({ views: [view("left", -.032), view("right", .032)] }),
	getPose: () => ({ transform: { position: { x: 0, y: 1.2, z: -.3 },
		orientation: { x: 0, y: 0, z: 0, w: 1 } } })
});
assert.equal(presented, 1);
await new Promise(resolve => setTimeout(resolve, 5));
assert.equal(globalThis.surrealXRGetState().presentationMode, "webgl-bridge");
const bridgeState = globalThis.surrealXRGetState();
assert.equal(bridgeState.bridgeDiagnostics.errors, 0);
assert.equal(bridgeState.bridgeDiagnostics.samples, 120);
assert.equal(bridgeState.bridgeDiagnostics.p99Ms, 1.1);
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
assert.equal(globalThis.surrealXRExit(), true);
await new Promise(resolve => setTimeout(resolve, 5));
assert.equal(destroyed, 1);
assert.equal(sessions[0].frames.size, 0);
assert.equal(globalThis.surrealXRGetState().presentationMode, null);
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
await new Promise(resolve => setTimeout(resolve, 5));
assert.equal(destroyed, 2);
console.log("WebXR XRWebGLLayer fallback provider tests passed");
