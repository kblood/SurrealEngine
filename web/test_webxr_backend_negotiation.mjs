import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";

const providerSource = fs.readFileSync(new URL("./webxr_provider.js", import.meta.url), "utf8");

function createScenario(options = {}) {
	const sessions = [];
	const requestOptions = [];
	let bindingCreations = 0;
	let bridgeCreations = 0;

	class FakeSession {
		constructor(enabledFeatures) {
			this.listeners = new Map();
			this.inputSources = [];
			this.visibilityState = "visible";
			this.endCalls = 0;
			this.bindingCreated = false;
			this.baseLayerAssigned = false;
			if (enabledFeatures === "throw") {
				Object.defineProperty(this, "enabledFeatures", {
					get() { throw new Error("synthetic private feature failure"); },
				});
			} else if (enabledFeatures !== "missing") {
				this.enabledFeatures = new Set(enabledFeatures || []);
			}
		}
		addEventListener(name, callback) { this.listeners.set(name, callback); }
		updateRenderState(state) {
			if (state && state.baseLayer) this.baseLayerAssigned = true;
			this.renderState = state;
		}
		async requestReferenceSpace() { return { addEventListener() {} }; }
		requestAnimationFrame(callback) { this.frameCallback = callback; return 1; }
		cancelAnimationFrame() { this.frameCallback = null; }
		async end() {
			this.endCalls++;
			this.listeners.get("end")?.();
		}
	}

	const device = { queue: { onSubmittedWorkDone: () => Promise.resolve() } };
	const sandbox = {
		console,
		setTimeout,
		clearTimeout,
		isSecureContext: true,
		surrealWebGPUDeviceXRCompatible: options.direct !== false,
		GPUTextureUsage: { COPY_DST: 2, RENDER_ATTACHMENT: 16 },
		Module: {
			preinitializedWebGPUDevice: device,
			_Surreal_GetWebXRFrameABIVersion: () => 4,
			ccall(name) {
				if (name === "Surreal_SetXRFrameLoopActive" || name === "Surreal_SubmitWebXRInputSnapshot" ||
					name === "Surreal_ApplyWebXRInputSnapshot") return 1;
				if (name === "Surreal_ResetWebXRPose" || name === "Surreal_ClearWebXRInputSnapshot") return undefined;
				throw new Error("unexpected native call: " + name);
			},
		},
		navigator: { xr: {
			isSessionSupported: async () => true,
			async requestSession(mode, suppliedOptions) {
				assert.equal(mode, "immersive-vr");
				requestOptions.push(JSON.parse(JSON.stringify(suppliedOptions)));
				const nextFeatures = options.enabledFeatures;
				const result = new FakeSession(nextFeatures === undefined ? [] : nextFeatures);
				sessions.push(result);
				return result;
			},
		} },
	};
	sandbox.window = sandbox;
	sandbox.globalThis = sandbox;
	sandbox.addEventListener = () => {};
	sandbox.dispatchEvent = () => true;
	sandbox.XRGPUBinding = options.direct === false ? undefined : class {
		constructor(session) {
			bindingCreations++;
			session.bindingCreated = true;
			if (options.bindingFailure) throw new Error("synthetic direct binding failure");
		}
		getPreferredColorFormat() { return "rgba8unorm"; }
		createProjectionLayer() { return { textureWidth: 2048, textureHeight: 1024 }; }
	};
	sandbox.XRWebGLLayer = options.bridge === false ? undefined : function (session) {
		this.session = session;
	};
	sandbox.SurrealWebXRWebGLBridge = options.bridge === false ? undefined : {
		canCreateWebGL2: () => true,
		async create({ session }) {
			bridgeCreations++;
			const baseLayer = new sandbox.XRWebGLLayer(session);
			session.updateRenderState({ baseLayer });
			return {
				textureFormat: "rgba8unorm",
				diagnostics: () => ({}),
				destroy() {},
			};
		},
	};

	vm.createContext(sandbox);
	vm.runInContext(providerSource, sandbox, { filename: "webxr_provider.js" });
	return {
		host: sandbox,
		sessions,
		requestOptions,
		bindingCreations: () => bindingCreations,
		bridgeCreations: () => bridgeCreations,
		assertExclusive() {
			for (const session of sessions)
				assert.equal(session.bindingCreated && session.baseLayerAssigned, false,
					"one XRSession must never use both XRGPUBinding and an XRWebGLLayer baseLayer");
		},
	};
}

{
	const scenario = createScenario({ enabledFeatures: ["local-floor", "webgpu"] });
	assert.equal(await scenario.host.surrealXRRequestSession(), true);
	assert.deepEqual(scenario.requestOptions[0], { optionalFeatures: ["local-floor", "webgpu"] });
	assert.equal(scenario.host.surrealXRGetState().presentationMode, "direct-webgpu");
	assert.equal(await scenario.host.surrealXRActivateReservedSession(), true);
	assert.equal(scenario.bindingCreations(), 1);
	assert.equal(scenario.bridgeCreations(), 0);
	assert.ok(Array.isArray(scenario.sessions[0].renderState.layers));
	scenario.assertExclusive();
}

{
	const scenario = createScenario({ enabledFeatures: ["local-floor"] });
	assert.equal(await scenario.host.surrealXRRequestSession(), true);
	assert.deepEqual(scenario.requestOptions[0], { optionalFeatures: ["local-floor", "webgpu"] });
	assert.equal(scenario.host.surrealXRGetState().presentationMode, "webgl-bridge");
	assert.equal(await scenario.host.surrealXRActivateReservedSession(), true);
	assert.equal(scenario.bindingCreations(), 0);
	assert.equal(scenario.bridgeCreations(), 1);
	assert.ok(scenario.sessions[0].renderState.baseLayer);
	scenario.assertExclusive();
}

for (const enabledFeatures of ["missing", "throw"]) {
	const scenario = createScenario({ enabledFeatures });
	assert.equal(await scenario.host.surrealXRRequestSession(), false);
	const state = scenario.host.surrealXRGetState();
	assert.equal(state.phase, "error");
	assert.equal(state.lastErrorCode, "feature-negotiation-unobservable");
	assert.equal(state.lastErrorStage, "negotiating-features");
	assert.match(state.lastError, /Force WebGL compatibility bridge/);
	assert.equal(scenario.sessions[0].endCalls, 1);
	assert.equal(scenario.bindingCreations(), 0);
	assert.equal(scenario.bridgeCreations(), 0);
	scenario.assertExclusive();
}

{
	const scenario = createScenario({ bridge: false, enabledFeatures: "missing" });
	assert.equal(await scenario.host.surrealXRRequestSession(), true);
	assert.deepEqual(scenario.requestOptions[0], {
		requiredFeatures: ["webgpu"], optionalFeatures: ["local-floor"],
	});
	assert.equal(scenario.host.surrealXRGetState().presentationMode, "direct-webgpu");
	assert.equal(await scenario.host.surrealXRExit(), true);
	scenario.assertExclusive();
}

{
	const scenario = createScenario({ direct: false, enabledFeatures: "missing" });
	assert.equal(await scenario.host.surrealXRRequestSession(), true);
	assert.deepEqual(scenario.requestOptions[0], { optionalFeatures: ["local-floor"] });
	assert.equal(scenario.requestOptions[0].requiredFeatures, undefined);
	assert.equal(scenario.requestOptions[0].optionalFeatures.includes("webgpu"), false);
	assert.equal(scenario.host.surrealXRGetState().presentationMode, "webgl-bridge");
	assert.equal(await scenario.host.surrealXRExit(), true);
	scenario.assertExclusive();
}

{
	const scenario = createScenario({ enabledFeatures: "missing" });
	assert.equal(scenario.host.surrealXRSetPresentationPreference("webgl-bridge"), "webgl-bridge");
	assert.equal(await scenario.host.surrealXRRequestSession(), true);
	assert.deepEqual(scenario.requestOptions[0], { optionalFeatures: ["local-floor"] });
	assert.equal(scenario.requestOptions[0].requiredFeatures, undefined);
	assert.equal(scenario.requestOptions[0].optionalFeatures.includes("webgpu"), false);
	assert.equal(await scenario.host.surrealXRActivateReservedSession(), true);
	assert.equal(scenario.bindingCreations(), 0);
	assert.equal(scenario.bridgeCreations(), 1);
	scenario.assertExclusive();
}

{
	const scenario = createScenario({ enabledFeatures: ["webgpu"], bindingFailure: true });
	assert.equal(await scenario.host.surrealXREnter(), false);
	assert.equal(scenario.host.surrealXRGetState().lastErrorCode, "binding-creation-failed");
	assert.equal(scenario.sessions[0].endCalls, 1);
	assert.equal(scenario.bindingCreations(), 1);
	assert.equal(scenario.bridgeCreations(), 0,
		"a direct setup failure must not fall back to the bridge in the same session");
	scenario.assertExclusive();
}

console.log("WebXR enabled-feature backend negotiation tests passed");
