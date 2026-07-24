import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";

const providerSource = fs.readFileSync(new URL("./webxr_provider.js", import.meta.url), "utf8");

function createScenario(options = {}) {
	const sessions = [];
	const requestOptions = [];
	let bindingCreations = 0;
	let bridgeCreations = 0;
	let webGLLayerCreations = 0;
	let makeCompatibleCalls = 0;
	const gl = options.webGL2 === false ? null : {
		FRAMEBUFFER: 0x8d40,
		makeXRCompatible: async () => { makeCompatibleCalls++; },
		bindFramebuffer() {},
	};
	const canvas = { getContext: kind => kind === "webgl2" ? gl : null };

	class FakeSession {
		constructor() {
			this.listeners = new Map();
			this.inputSources = [];
			this.visibilityState = "visible";
			this.endCalls = 0;
		}
		addEventListener(name, callback) { this.listeners.set(name, callback); }
		removeEventListener() {}
		updateRenderState(state) { this.renderState = state; }
		async requestReferenceSpace() { return { addEventListener() {} }; }
		requestAnimationFrame(callback) { this.frameCallback = callback; return 1; }
		cancelAnimationFrame() { this.frameCallback = null; }
		async end() { this.endCalls++; this.listeners.get("end")?.(); }
	}

	const device = { queue: { onSubmittedWorkDone: () => Promise.resolve() } };
	const sandbox = {
		console,
		setTimeout,
		clearTimeout,
		isSecureContext: true,
		surrealXRExperimentalWebGPU: options.experimentalWebGPU === true,
		surrealWebGPUDeviceXRCompatible: options.webGPU !== false,
		GPUTextureUsage: { COPY_DST: 2, RENDER_ATTACHMENT: 16 },
		Module: {
			canvas,
			preinitializedWebGPUDevice: device,
			_Surreal_GetWebXRFrameABIVersion: () => 4,
			ccall(name) {
				if (name === "Surreal_GetWebGL2ContextState") return gl ? 1 : -1;
				if (name === "Surreal_GetWebGL2ContextHandle") return gl ? 9 : 0;
				if (name === "Surreal_SetXRFrameLoopActive" ||
					name === "Surreal_SubmitWebXRInputSnapshot" ||
					name === "Surreal_ApplyWebXRInputSnapshot") return 1;
				if (name === "Surreal_ResetWebXRPose" ||
					name === "Surreal_ClearWebXRInputSnapshot") return undefined;
				throw new Error("unexpected native call: " + name);
			},
		},
		navigator: { xr: {
			isSessionSupported: async () => true,
			async requestSession(mode, suppliedOptions) {
				assert.equal(mode, "immersive-vr");
				requestOptions.push(JSON.parse(JSON.stringify(suppliedOptions)));
				const session = new FakeSession();
				sessions.push(session);
				return session;
			},
		} },
	};
	sandbox.window = sandbox;
	sandbox.globalThis = sandbox;
	sandbox.addEventListener = () => {};
	sandbox.dispatchEvent = () => true;
	sandbox.XRGPUBinding = options.webGPU === false ? undefined : class {
		constructor() { bindingCreations++; }
		getPreferredColorFormat() { return "rgba8unorm"; }
		createProjectionLayer() { return { textureWidth: 2048, textureHeight: 1024 }; }
	};
	sandbox.XRWebGLLayer = options.webGLLayer === false ? undefined : class {
		constructor(session, suppliedContext) {
			if (options.layerFailure) throw new Error("synthetic WebGL layer failure");
			webGLLayerCreations++;
			assert.equal(suppliedContext, gl);
			this.session = session;
			this.framebuffer = {};
			this.framebufferWidth = 2000;
			this.framebufferHeight = 1000;
		}
	};
	sandbox.SurrealWebXRWebGLBridge = options.bridge === true ? {
		canCreateWebGL2: () => true,
		async create({ session }) {
			bridgeCreations++;
			const baseLayer = { framebuffer: {} };
			session.updateRenderState({ baseLayer });
			return { textureFormat: "rgba8unorm", diagnostics: () => ({}), destroy() {} };
		},
	} : undefined;

	vm.createContext(sandbox);
	vm.runInContext(providerSource, sandbox, { filename: "webxr_provider.js" });
	return { host: sandbox, sessions, requestOptions,
		bindingCreations: () => bindingCreations,
		bridgeCreations: () => bridgeCreations,
		webGLLayerCreations: () => webGLLayerCreations,
		makeCompatibleCalls: () => makeCompatibleCalls };
}

// Production auto mode always prefers the running engine's WebGL2 context,
// even when experimental WebGPU APIs and the diagnostic bridge also exist.
{
	const scenario = createScenario({ experimentalWebGPU: true, bridge: true });
	assert.equal(await scenario.host.surrealXRRequestSession(), true);
	assert.deepEqual(scenario.requestOptions[0], { optionalFeatures: ["local-floor"] });
	assert.equal(scenario.host.surrealXRGetState().presentationMode, "direct-webgl2");
	assert.equal(await scenario.host.surrealXRActivateReservedSession(), true);
	assert.equal(scenario.makeCompatibleCalls(), 1);
	assert.equal(scenario.webGLLayerCreations(), 1);
	assert.equal(scenario.bindingCreations(), 0);
	assert.equal(scenario.bridgeCreations(), 0);
	assert.ok(scenario.sessions[0].renderState.baseLayer);
}

// WebGPU XR remains opt-in and asks for the unstable feature explicitly.
{
	const scenario = createScenario({ webGL2: false, experimentalWebGPU: true });
	assert.equal(await scenario.host.surrealXRRequestSession(), true);
	assert.deepEqual(scenario.requestOptions[0], {
		requiredFeatures: ["webgpu"], optionalFeatures: ["local-floor"],
	});
	assert.equal(scenario.host.surrealXRGetState().presentationMode, "direct-webgpu");
	assert.equal(await scenario.host.surrealXRActivateReservedSession(), true);
	assert.equal(scenario.bindingCreations(), 1);
	assert.equal(scenario.webGLLayerCreations(), 0);
}

// Merely exposing XRGPUBinding must not move production traffic to WebGPU.
{
	const scenario = createScenario({ webGL2: false });
	assert.equal(await scenario.host.surrealXRRequestSession(), false);
	assert.equal(scenario.host.surrealXRGetState().lastErrorCode,
		"no-webxr-presentation-backend");
	assert.equal(scenario.sessions.length, 0);
}

// The cross-API bridge remains available only as an explicit diagnostic mode.
{
	const scenario = createScenario({ webGL2: false, bridge: true });
	assert.equal(scenario.host.surrealXRSetPresentationPreference("webgl-bridge"), "webgl-bridge");
	assert.equal(await scenario.host.surrealXRRequestSession(), true);
	assert.deepEqual(scenario.requestOptions[0], { optionalFeatures: ["local-floor"] });
	assert.equal(await scenario.host.surrealXRActivateReservedSession(), true);
	assert.equal(scenario.bridgeCreations(), 1);
	assert.equal(scenario.bindingCreations(), 0);
}

// A direct-layer setup failure ends that session instead of switching graphics
// APIs underneath a live game/runtime.
{
	const scenario = createScenario({ layerFailure: true, bridge: true });
	assert.equal(await scenario.host.surrealXREnter(), false);
	assert.equal(scenario.host.surrealXRGetState().lastErrorCode,
		"webgl2-layer-creation-failed");
	assert.equal(scenario.sessions[0].endCalls, 1);
	assert.equal(scenario.bridgeCreations(), 0);
}

console.log("WebXR production/experimental backend policy tests passed");
