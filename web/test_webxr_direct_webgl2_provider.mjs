import assert from "node:assert/strict";

globalThis.window = globalThis;
globalThis.isSecureContext = true;
globalThis.addEventListener = () => {};
globalThis.dispatchEvent = () => true;

const delay = () => new Promise(resolve => setTimeout(resolve, 5));
let xrCallbackActive = false;
let nativeRenderCount = 0;
let prepareCount = 0;
let makeCompatibleCount = 0;
let layerCount = 0;
const loopTransitions = [];
const bindEvents = [];
const activationEvents = [];

const gl = {
	FRAMEBUFFER: 0x8d40,
	FRAMEBUFFER_BINDING: 0x8ca6,
	DRAW_FRAMEBUFFER: 0x8ca9,
	READ_FRAMEBUFFER: 0x8ca8,
	DRAW_FRAMEBUFFER_BINDING: 0x8ca6,
	READ_FRAMEBUFFER_BINDING: 0x8caa,
	boundDrawFramebuffer: { name: "flat-draw" },
	boundReadFramebuffer: { name: "flat-read" },
	async makeXRCompatible() {
		activationEvents.push("make-compatible");
		makeCompatibleCount++;
	},
	getParameter(parameter) {
		if (parameter === this.DRAW_FRAMEBUFFER_BINDING) return this.boundDrawFramebuffer;
		if (parameter === this.READ_FRAMEBUFFER_BINDING) return this.boundReadFramebuffer;
		throw new Error("unexpected framebuffer binding query");
	},
	bindFramebuffer(target, framebuffer) {
		bindEvents.push({ target, framebuffer, xrCallbackActive });
		if (target === this.FRAMEBUFFER || target === this.DRAW_FRAMEBUFFER)
			this.boundDrawFramebuffer = framebuffer;
		if (target === this.FRAMEBUFFER || target === this.READ_FRAMEBUFFER)
			this.boundReadFramebuffer = framebuffer;
	},
};

const canvas = {
	getContext(kind) {
		assert.equal(kind, "webgl2");
		return gl;
	},
};

globalThis.Module = {
	canvas,
	_Surreal_GetWebXRFrameABIVersion: () => 4,
	ccall(name, _returnType, _argumentTypes, args, options) {
		if (name === "Surreal_GetWebGL2ContextState") return 1;
		if (name === "Surreal_GetWebGL2ContextHandle") return 17;
		if (name === "Surreal_SetXRFrameLoopActive") {
			loopTransitions.push(args[0]);
			return 1;
		}
		if (name === "Surreal_PrepareWebXRFrame") {
			assert.deepEqual(options, { async: true });
			prepareCount++;
			return 1;
		}
		if (name === "Surreal_RenderDirectWebGL2XRFrame") {
			assert.equal(options, undefined);
			assert.equal(xrCallbackActive, true,
				"direct native rendering must stay in the XR animation callback");
			assert.equal(gl.boundDrawFramebuffer, sessions.at(-1).renderState.baseLayer.framebuffer,
				"native rendering must see the compositor framebuffer bound");
			const packet = new DataView(args[0].buffer, args[0].byteOffset, args[0].byteLength);
			assert.equal(packet.getUint32(0, true), 4);
			assert.equal(packet.getUint32(8, true), 2);
			assert.equal(packet.getUint32(12, true), 1);
			assert.equal(packet.getUint32(28, true), 2,
				"direct WebGL2 uses one shared compositor atlas with canonical projection depth");
			assert.deepEqual([
				packet.getInt32(32 + 20, true), packet.getInt32(32 + 24, true),
				packet.getInt32(32 + 28, true), packet.getInt32(32 + 32, true),
			], [0, 180, 900, 800], "left WebGL viewport must map to the engine's top-left ViewRect");
			assert.deepEqual([
				packet.getInt32(32 + 128 + 20, true), packet.getInt32(32 + 128 + 24, true),
				packet.getInt32(32 + 128 + 28, true), packet.getInt32(32 + 128 + 32, true),
			], [1000, 50, 900, 850], "right WebGL viewport must map to the engine's top-left ViewRect");
			nativeRenderCount++;
			return 1;
		}
		if (name === "Surreal_CompleteWebXRFrame" ||
			name === "Surreal_SubmitWebXRInputSnapshot" ||
			name === "Surreal_ApplyWebXRInputSnapshot") return 1;
		if (name === "Surreal_ResetWebXRPose" ||
			name === "Surreal_ClearWebXRInputSnapshot") return undefined;
		if (name === "Surreal_GetWebXRFrameLastError" ||
			name === "Surreal_GetWebXRInputLastError") return 0;
		throw new Error("unexpected native call: " + name);
	},
};

globalThis.XRWebGLLayer = class {
	constructor(session, context, options) {
		activationEvents.push("create-layer");
		assert.equal(context, gl, "XRWebGLLayer must reuse the engine's WebGL2 context");
		assert.deepEqual(options, {
			alpha: false, antialias: false, depth: true, stencil: false,
			framebufferScaleFactor: 1,
		});
		this.session = session;
		this.framebuffer = { name: "xr-framebuffer-" + (++layerCount) };
		this.framebufferWidth = 2000;
		this.framebufferHeight = 1000;
	}
	getViewport(view) {
		return view.eye === "left" ?
			{ x: 0, y: 20, width: 900, height: 800 } :
			{ x: 1000, y: 100, width: 900, height: 850 };
	}
};

class FakeSession {
	constructor() {
		this.listeners = new Map();
		this.frames = new Map();
		this.nextHandle = 1;
		this.visibilityState = "visible";
		this.inputSources = [];
	}
	addEventListener(name, callback) { this.listeners.set(name, callback); }
	removeEventListener(name, callback) {
		if (this.listeners.get(name) === callback) this.listeners.delete(name);
	}
	updateRenderState(state) {
		activationEvents.push("update-render-state");
		this.renderState = state;
	}
	async requestReferenceSpace() { return { addEventListener() {} }; }
	requestAnimationFrame(callback) {
		const handle = this.nextHandle++;
		this.frames.set(handle, callback);
		return handle;
	}
	cancelAnimationFrame(handle) { this.frames.delete(handle); }
	fireFrame(time, frame) {
		const entry = this.frames.entries().next().value;
		assert.ok(entry, "an XR callback must be pending");
		this.frames.delete(entry[0]);
		xrCallbackActive = true;
		try { entry[1](time, frame); }
		finally { xrCallbackActive = false; }
	}
	async end() { this.listeners.get("end")?.(); }
}

const sessions = [];
const requestOptions = [];
Object.defineProperty(globalThis, "navigator", { configurable: true, value: { xr: {
	isSessionSupported: async mode => mode === "immersive-vr",
	async requestSession(mode, options) {
		assert.equal(mode, "immersive-vr");
		requestOptions.push(options);
		const session = new FakeSession();
		sessions.push(session);
		return session;
	},
} } });

const projection = new Float32Array([
	1, 0, 0, 0, 0, 1, 0, 0,
	0, 0, -1.0002, -1, 0, 0, -0.20002, 0,
]);
function view(eye, x) {
	return { eye, projectionMatrix: projection, transform: {
		position: { x, y: 1.6, z: 0 },
		orientation: { x: 0, y: 0, z: 0, w: 1 },
	} };
}
const frame = {
	getViewerPose: () => ({ views: [view("left", -.032), view("right", .032)] }),
	getPose: () => null,
};

await import("./webxr_provider.js");
const capabilities = await globalThis.surrealXRGetCapabilities();
assert.equal(capabilities.directWebGL2, true);
assert.equal(capabilities.directWebGPU, false);
assert.equal(capabilities.preferredMode, "direct-webgl2");
assert.equal(capabilities.supported, true);

assert.equal(await globalThis.surrealXREnter(), true);
assert.deepEqual(requestOptions[0], { optionalFeatures: ["local-floor"] });
assert.deepEqual(activationEvents.slice(0, 3),
	["make-compatible", "create-layer", "update-render-state"]);
assert.equal(globalThis.surrealXRGetState().presentationMode, "direct-webgl2");
assert.deepEqual(loopTransitions, [1]);

const flatDrawFramebuffer = gl.boundDrawFramebuffer;
const flatReadFramebuffer = gl.boundReadFramebuffer;
sessions[0].fireFrame(10, frame);
await delay();
assert.equal(prepareCount, 1);
assert.equal(nativeRenderCount, 0, "the first pose is prepared asynchronously, not rendered twice");
sessions[0].fireFrame(20, frame);
assert.equal(nativeRenderCount, 1);
assert.equal(gl.boundDrawFramebuffer, flatDrawFramebuffer,
	"the provider must restore the pre-XR draw framebuffer before rAF returns");
assert.equal(gl.boundReadFramebuffer, flatReadFramebuffer,
	"the provider must restore the pre-XR read framebuffer before rAF returns");
assert.deepEqual(bindEvents.slice(-3), [
	{ target: gl.FRAMEBUFFER, framebuffer: sessions[0].renderState.baseLayer.framebuffer, xrCallbackActive: true },
	{ target: gl.DRAW_FRAMEBUFFER, framebuffer: flatDrawFramebuffer, xrCallbackActive: true },
	{ target: gl.READ_FRAMEBUFFER, framebuffer: flatReadFramebuffer, xrCallbackActive: true },
]);

assert.equal(globalThis.surrealXRExit(), true);
await delay();
assert.equal(globalThis.surrealXRGetState().phase, "ended");
assert.deepEqual(loopTransitions, [1, 0]);

assert.equal(await globalThis.surrealXREnter(), true);
assert.equal(makeCompatibleCount, 2, "re-entry must revalidate the shared context");
assert.equal(layerCount, 2, "re-entry must create a fresh session-owned layer");
assert.notEqual(sessions[0].renderState.baseLayer.framebuffer,
	sessions[1].renderState.baseLayer.framebuffer);
assert.equal(globalThis.surrealXRExit(), true);
await delay();

console.log("WebXR direct WebGL2 shared-context lifecycle test passed");
