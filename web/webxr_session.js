// web/webxr_session.js — M4 groundwork: WebXR session-lifecycle plumbing.
//
// Ported from the sibling `webxr-port/` (QuakeQuest) project's proven
// session-request pattern (src/web-host/lib/webxr/library_webxr.js's
// onSessionStarted/requestSession flow — see PATCHES.md there for the
// upstream emscripten-webxr fixes it carries), adapted to two differences:
//
//   1. This is plain browser JS, not an Emscripten JS library (`--js-library`)
//      called from C. The default path remains the IWER-testable session
//      lifecycle harness. An explicitly selected native path now owns the XR
//      frame loop, packs XRView data, and calls the versioned C++ render bridge
//      synchronously while the browser-owned GPUTexture is still valid.
//   2. QuakeQuest is WebGL2 (`XRWebGLLayer` via `GLctx.makeXRCompatible()`).
//      SurrealEngine's renderer is WebGPU (RenderDevice/WebGPU/), which per
//      the WebXR spec needs `XRGPUBinding`/`XRGPUProjectionLayer` instead of
//      `XRWebGLLayer`, once the engine is actually rendering into the
//      session. That binding is not implemented by IWER as of iwer@2.3.0
//      (confirmed by inspecting node_modules/iwer/build/iwer.js — no
//      `XRGPUBinding`/`GPUBinding` symbol anywhere in the bundle), so IWER
//      cannot emulate the WebGPU-XR handoff, only the WebGL2 one. This
//      default path therefore drives the session's render state with a throwaway,
//      offscreen WebGL2 `XRWebGLLayer` purely to satisfy
//      `updateRenderState()`/`requestAnimationFrame()` (IWER returns no pose
//      and never re-invokes the frame callback while baseLayer is null —
//      see iwer.js's `onXRFrame` early-return) — it never touches the real
//      WebGPU canvas or SurrealEngine's rendering at all. Separately, this
//      module also probes `navigator.gpu.requestAdapter({xrCompatible:true})`
//      as a capability check for the future `XRGPUBinding` handoff — that
//      probe exercises real Chrome, not IWER (see surrealXRCheckGPUCompatible).
//
// State is exposed as flat `window.surrealXR*` globals for Playwright to
// poll via page.evaluate(), matching this project's existing harness
// convention (see index_webgpu.html's window.surreal* hooks) rather than
// requiring the test driver to wire up event listeners.

(function () {
	// M8 packed-frame ABI v2. The originally proposed 112-byte view stride is
	// arithmetically impossible: the projection matrix starts at byte 52 and
	// contains 16 float32 values, ending at byte 116. The header has no trailing
	// padding, so the first view starts at byte 44. Keep every size explicit so
	// JS and C++ can share compile-time/runtime offset checks.
	const XR_FRAME_ABI = Object.freeze({
		version: 2,
		headerBytes: 44,
		viewBytes: 116,
		inputBytes: 128,
		maxViews: 2,
		maxInputSources: 2,
		maxPacketBytes: 532,
		eye: Object.freeze({ none: 0, left: 1, right: 2 }),
		handedness: Object.freeze({ none: 0, left: 1, right: 2 }),
		inputFlags: Object.freeze({ aim: 1, grip: 2, connected: 4, xrStandard: 8 }),
	});
	window.surrealXRFrameABI = XR_FRAME_ABI;

	function requireFinite(value, name) {
		value = Number(value);
		if (!Number.isFinite(value)) throw new TypeError(name + " must be finite");
		return value;
	}

	function component(value, name, fallback) {
		if (value == null) return fallback;
		return requireFinite(value, name);
	}

	function eyeCode(eye) {
		if (typeof eye === "number") return eye >>> 0;
		return XR_FRAME_ABI.eye[eye || "none"] ?? XR_FRAME_ABI.eye.none;
	}

	function handednessCode(handedness) {
		if (typeof handedness === "number") return handedness >>> 0;
		return XR_FRAME_ABI.handedness[handedness || "none"] ?? XR_FRAME_ABI.handedness.none;
	}

	function writeView(view, data, base, index) {
		const viewport = view.viewport || {};
		const position = view.position || {};
		const orientation = view.orientation || {};
		const projection = view.projectionMatrix || view.projection;
		if (!projection || projection.length !== 16) {
			throw new TypeError("views[" + index + "].projectionMatrix must contain 16 values");
		}

		data.setUint32(base + 0, eyeCode(view.eye), true);
		data.setUint32(base + 4, (view.arrayLayer ?? index) >>> 0, true);
		data.setInt32(base + 8, component(viewport.x, "viewport.x", 0), true);
		data.setInt32(base + 12, component(viewport.y, "viewport.y", 0), true);
		data.setInt32(base + 16, component(viewport.width, "viewport.width", 0), true);
		data.setInt32(base + 20, component(viewport.height, "viewport.height", 0), true);
		data.setFloat32(base + 24, component(position.x, "position.x", 0), true);
		data.setFloat32(base + 28, component(position.y, "position.y", 0), true);
		data.setFloat32(base + 32, component(position.z, "position.z", 0), true);
		data.setFloat32(base + 36, component(orientation.x, "orientation.x", 0), true);
		data.setFloat32(base + 40, component(orientation.y, "orientation.y", 0), true);
		data.setFloat32(base + 44, component(orientation.z, "orientation.z", 0), true);
		data.setFloat32(base + 48, component(orientation.w, "orientation.w", 1), true);
		for (let n = 0; n < 16; n++) {
			data.setFloat32(base + 52 + n * 4,
				requireFinite(projection[n], "projectionMatrix[" + n + "]"), true);
		}
	}

	function writePose(data, base, pose, name) {
		pose = pose || {};
		const position = pose.position || {};
		const orientation = pose.orientation || {};
		data.setFloat32(base + 0, component(position.x, name + ".position.x", 0), true);
		data.setFloat32(base + 4, component(position.y, name + ".position.y", 0), true);
		data.setFloat32(base + 8, component(position.z, name + ".position.z", 0), true);
		data.setFloat32(base + 12, component(orientation.x, name + ".orientation.x", 0), true);
		data.setFloat32(base + 16, component(orientation.y, name + ".orientation.y", 0), true);
		data.setFloat32(base + 20, component(orientation.z, name + ".orientation.z", 0), true);
		data.setFloat32(base + 24, component(orientation.w, name + ".orientation.w", 1), true);
	}

	function writeInput(input, data, base, index) {
		const axes = Array.from(input.axes || []);
		const values = Array.from(input.values || []);
		data.setUint32(base + 0, (input.sourceId || 0) >>> 0, true);
		data.setUint32(base + 4, handednessCode(input.handedness), true);
		data.setUint32(base + 8, (input.flags || 0) >>> 0, true);
		data.setUint32(base + 12, (input.pressedMask || 0) >>> 0, true);
		data.setUint32(base + 16, (input.touchedMask || 0) >>> 0, true);
		data.setUint32(base + 20, 0, true);
		for (let n = 0; n < 4; n++) {
			data.setFloat32(base + 24 + n * 4,
				component(axes[n], "inputs[" + index + "].axes[" + n + "]", 0), true);
		}
		for (let n = 0; n < 8; n++) {
			data.setFloat32(base + 40 + n * 4,
				component(values[n], "inputs[" + index + "].values[" + n + "]", 0), true);
		}
		writePose(data, base + 72, input.grip, "inputs[" + index + "].grip");
		writePose(data, base + 100, input.aim, "inputs[" + index + "].aim");
	}

	// Packs plain values only. The returned ArrayBuffer owns no XR objects and
	// may safely outlive the XRFrame callback that produced the values.
	window.surrealXRPackFrame = function (frame) {
		frame = frame || {};
		const views = Array.from(frame.views || []);
		const inputs = Array.from(frame.inputs || []);
		if (views.length > XR_FRAME_ABI.maxViews) {
			throw new RangeError("XR frame ABI v2 supports at most " + XR_FRAME_ABI.maxViews + " views");
		}
		if (inputs.length > XR_FRAME_ABI.maxInputSources) {
			throw new RangeError("XR frame ABI v2 supports at most " +
				XR_FRAME_ABI.maxInputSources + " input sources");
		}
		const inputOffset = XR_FRAME_ABI.headerBytes + views.length * XR_FRAME_ABI.viewBytes;
		const byteSize = inputOffset + inputs.length * XR_FRAME_ABI.inputBytes;
		const buffer = new ArrayBuffer(byteSize);
		const data = new DataView(buffer);
		data.setUint32(0, XR_FRAME_ABI.version, true);
		data.setUint32(4, byteSize, true);
		data.setUint32(8, views.length, true);
		data.setUint32(12, (frame.flags || 0) >>> 0, true);
		data.setFloat64(16, requireFinite(frame.timestamp || 0, "timestamp"), true);
		data.setUint32(24, (frame.resetGeneration || 0) >>> 0, true);
		data.setUint32(28, (frame.textureWidth || 0) >>> 0, true);
		data.setUint32(32, (frame.textureHeight || 0) >>> 0, true);
		data.setUint32(36, inputs.length, true);
		data.setUint32(40, inputOffset, true);
		views.forEach((view, index) => writeView(
			view, data, XR_FRAME_ABI.headerBytes + index * XR_FRAME_ABI.viewBytes, index));
		inputs.forEach((input, index) => writeInput(
			input, data, inputOffset + index * XR_FRAME_ABI.inputBytes, index));
		return buffer;
	};

	function subImageAt(subImages, view, index) {
		if (typeof subImages === "function") return subImages(view, index);
		return subImages ? subImages[index] : null;
	}

	function arrayLayerOf(subImage, index) {
		if (!subImage) return index;
		const descriptor = typeof subImage.getViewDescriptor === "function" ?
			subImage.getViewDescriptor() : null;
		// getViewDescriptor().baseArrayLayer is the current WebXR-WebGPU-Binding
		// contract. Older aliases remain diagnostic fallbacks for draft churn.
		return (descriptor && descriptor.baseArrayLayer) ?? subImage.arrayLayer ??
			subImage.textureArrayLayer ?? subImage.imageIndex ?? index;
	}

	// Production-facing conversion helper. Call this synchronously from an
	// XRSession RAF callback. `subImages` is either an array or a function that
	// calls XRGPUBinding.getViewSubImage(layer, view). All native XR objects stay
	// local; only copied numbers leave this function. IWER has no XRGPUBinding,
	// so its lifecycle test deliberately does not exercise this path.
	window.surrealXRPackFrameFromPose = function (time, pose, subImages, options) {
		options = options || {};
		if (!pose || !pose.views) return null;
		const packedViews = [];
		let textureWidth = options.textureWidth || 0;
		let textureHeight = options.textureHeight || 0;
		for (let index = 0; index < pose.views.length; index++) {
			const view = pose.views[index];
			const subImage = subImageAt(subImages, view, index);
			const viewport = (subImage && subImage.viewport) || options.viewport || {};
			textureWidth = textureWidth || (subImage && subImage.colorTextureWidth) ||
				(subImage && subImage.colorTexture && subImage.colorTexture.width) || 0;
			textureHeight = textureHeight || (subImage && subImage.colorTextureHeight) ||
				(subImage && subImage.colorTexture && subImage.colorTexture.height) || 0;
			packedViews.push({
				eye: view.eye,
				arrayLayer: arrayLayerOf(subImage, index),
				viewport: viewport,
				position: view.transform.position,
				orientation: view.transform.orientation,
				projectionMatrix: view.projectionMatrix,
			});
		}
		return window.surrealXRPackFrame({
			timestamp: time,
			resetGeneration: options.resetGeneration || 0,
			textureWidth: textureWidth,
			textureHeight: textureHeight,
			flags: options.flags || 0,
			views: packedViews,
			inputs: options.inputs || [],
		});
	};

	window.surrealXRPackFrameFromXRFrame = function (time, frame, referenceSpace, subImages, options) {
		const pose = frame.getViewerPose(referenceSpace);
		return window.surrealXRPackFrameFromPose(time, pose, subImages, options);
	};

	// Full production handoff helper. It acquires XRGPUSubImages and consumes
	// their shared GPUTexture during this callback only. WebXR-WebGPU-Binding
	// requires all views in a projection layer to reference the same array
	// texture; reject a mismatched set instead of issuing multiple C frames.
	window.surrealXRConsumeWebGPUFrame = function (
		time, frame, referenceSpace, binding, projectionLayer, options, consume) {
		const pose = frame.getViewerPose(referenceSpace);
		if (!pose) return false;
		const subImages = [];
		let colorTexture = null;
		for (const view of pose.views) {
			const subImage = binding.getViewSubImage(projectionLayer, view);
			if (!subImage || !subImage.colorTexture) {
				throw new Error("XRGPUSubImage did not provide a colorTexture");
			}
			if (colorTexture && colorTexture !== subImage.colorTexture) {
				throw new Error("XR views did not share one projection-layer colorTexture");
			}
			colorTexture = subImage.colorTexture;
			subImages.push(subImage);
		}
		const packet = window.surrealXRPackFrameFromPose(time, pose, subImages, options);
		if (!packet) return false;
		return consume ? consume(packet, colorTexture) === 1 : true;
	};

	// Lower-level packet-only hook retained for deterministic tests and future
	// non-WebGPU consumers. The production WebGPU path above uses the versioned
	// C export installed by index_webxr.html.
	window.surrealXRConsumeFramePacket = function (time, frame, referenceSpace, subImages, options, consume) {
		const packet = window.surrealXRPackFrameFromXRFrame(
			time, frame, referenceSpace, subImages, options);
		if (packet && consume) consume(packet);
		return packet !== null;
	};

	window.surrealXRTestFrameABI = function () {
		const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
		const buffer = window.surrealXRPackFrame({
			timestamp: 1234.5,
			resetGeneration: 7,
			textureWidth: 2048,
			textureHeight: 1024,
			flags: 0xa5,
			views: [
				{ eye: "left", arrayLayer: 3, viewport: { x: 11, y: 12, width: 900, height: 901 },
					position: { x: 1.25, y: -2.5, z: 3.75 }, orientation: { x: 0, y: 0.5, z: 0, w: 0.75 },
					projectionMatrix: identity },
				{ eye: "right", arrayLayer: 4, viewport: { x: 21, y: 22, width: 800, height: 801 },
					position: { x: -4, y: 5, z: -6 }, orientation: { x: 0.1, y: 0.2, z: 0.3, w: 0.4 },
					projectionMatrix: identity.map((value, index) => value + index) },
			],
			inputs: [
				{ sourceId: 42, handedness: "left", flags: 15, pressedMask: 5, touchedMask: 3,
					axes: [0.1, -0.2, 0.3, -0.4],
					values: [0.11, 0.22, 0.33, 0.44, 0.55, 0.66, 0.77, 0.88],
					grip: { position: { x: 1, y: 2, z: 3 },
						orientation: { x: 0.1, y: 0.2, z: 0.3, w: 0.9 } },
					aim: { position: { x: 4, y: 5, z: 6 },
						orientation: { x: 0.4, y: 0.5, z: 0.6, w: 0.7 } } },
				{ sourceId: 99, handedness: "right", flags: 4, pressedMask: 0x80,
					touchedMask: 0x40, axes: [-1, 1, -0.5, 0.5], values: [1, 0, 0, 0, 0, 0, 0, 0] },
			],
		});
		const data = new DataView(buffer);
		const near = (actual, expected) => Math.abs(actual - expected) < 0.00001;
		const checks = [
			data.getUint32(0, true) === 2, data.getUint32(4, true) === 532,
			data.getUint32(8, true) === 2, data.getUint32(12, true) === 0xa5,
			data.getFloat64(16, true) === 1234.5, data.getUint32(24, true) === 7,
			data.getUint32(28, true) === 2048, data.getUint32(32, true) === 1024,
			data.getUint32(36, true) === 2, data.getUint32(40, true) === 276,
			data.getUint32(44, true) === 1, data.getUint32(48, true) === 3,
			data.getInt32(52, true) === 11, near(data.getFloat32(68, true), 1.25),
			near(data.getFloat32(96, true), 1), data.getUint32(160, true) === 2,
			data.getUint32(164, true) === 4, data.getInt32(168, true) === 21,
			near(data.getFloat32(208, true), 0.4), near(data.getFloat32(272, true), 16),
			data.getUint32(276, true) === 42, data.getUint32(280, true) === 1,
			data.getUint32(284, true) === 15, data.getUint32(288, true) === 5,
			data.getUint32(292, true) === 3, data.getUint32(296, true) === 0,
			near(data.getFloat32(300, true), 0.1), near(data.getFloat32(312, true), -0.4),
			near(data.getFloat32(316, true), 0.11), near(data.getFloat32(344, true), 0.88),
			near(data.getFloat32(348, true), 1), near(data.getFloat32(372, true), 0.9),
			near(data.getFloat32(376, true), 4), near(data.getFloat32(400, true), 0.7),
			data.getUint32(404, true) === 99, data.getUint32(408, true) === 2,
			data.getUint32(412, true) === 4, data.getUint32(416, true) === 0x80,
			near(data.getFloat32(428, true), -1), near(data.getFloat32(440, true), 0.5),
			near(data.getFloat32(476, true), 0), near(data.getFloat32(500, true), 1),
			near(data.getFloat32(504, true), 0), near(data.getFloat32(528, true), 1),
		];
		return {
			passed: checks.every(Boolean),
			checksPassed: checks.filter(Boolean).length,
			checkCount: checks.length,
			byteSize: buffer.byteLength,
			headerBytes: XR_FRAME_ABI.headerBytes,
			viewBytes: XR_FRAME_ABI.viewBytes,
			inputBytes: XR_FRAME_ABI.inputBytes,
		};
	};

	window.surrealXRSupported = null; // null = not checked yet
	window.surrealXRSessionActive = false;
	window.surrealXRFrameCount = 0;
	window.surrealXRLog = [];
	window.surrealXRError = null;
	window.surrealXRGPUCompatible = null; // tri-state: null/true/false
	window.surrealXRGPUBindingAvailable = typeof globalThis.XRGPUBinding === "function";
	window.surrealXRWebGPUProbe = null;
	window.surrealXRUseNativeWebGPU = window.surrealXRUseNativeWebGPU === true;
	window.surrealXRNativeError = null;
	window.surrealXRNativeDiagnostics = {
		optIn: window.surrealXRUseNativeWebGPU,
		phase: "idle",
		generation: 0,
		frameCount: 0,
		skippedFrames: 0,
		lastRenderSucceeded: null,
		referenceSpaceType: null,
		projectionFormat: null,
		engineLoopOwned: false,
		poseReset: null,
		inputSourceCount: 0,
		error: null,
	};
	const XR_INTERACTION_SCHEMA = "surrealengine-webxr-interaction";
	const XR_INTERACTION_POLICY = Object.freeze({
		confirmDelayMs: 300,
		confirmWindowMs: 5000,
		trackingGraceMs: 1000,
		controllerChordHoldMs: 2500,
		// WebXR's xr-standard mapping exposes grip at 1 and secondary face at
		// 5. The runtime-reserved system/menu control is neither requested nor
		// intercepted. We observe this two-hand chord without masking game input.
		gripButtonMask: 1 << 1,
		secondaryFaceButtonMask: 1 << 5,
	});
	window.surrealXRInteractionPolicy = XR_INTERACTION_POLICY;
	const XR_LAUNCH_READINESS_SCHEMA = "surrealengine-webxr-launch-readiness";

	function launchBlocker(code, message, action) {
		return { code: code, message: message, action: action };
	}

	// Pure evaluator kept separate from the live browser snapshot so automation
	// can cover both launch modes without pretending that IWER is a native
	// WebGPU compositor. `canAttempt` means a trusted click may request the
	// session; only `productionSessionReady` proves that native presentation is
	// actually running.
	window.surrealXREvaluateLaunchReadiness = function (capabilities) {
		capabilities = capabilities || {};
		const nativeRequested = capabilities.nativeRequested === true;
		const blockers = [];
		if (capabilities.secureContext !== true) {
			blockers.push(launchBlocker("insecure-context",
				"WebXR requires a secure context; a Quest cannot use the PC's plain HTTP LAN address.",
				"Serve this page over trusted HTTPS, or use an explicitly trusted localhost development route."));
		}
		if (capabilities.xrSystemAvailable !== true) {
			blockers.push(launchBlocker("webxr-unavailable",
				"navigator.xr is unavailable in this browser context.",
				"Open the page in a WebXR-capable headset browser after fixing secure delivery."));
		} else if (capabilities.immersiveSupported === null) {
			blockers.push(launchBlocker("immersive-check-pending",
				"The immersive-vr capability check has not finished.",
				"Wait for the browser capability check to complete."));
		} else if (capabilities.immersiveSupported !== true) {
			blockers.push(launchBlocker("immersive-vr-unsupported",
				"This browser/device did not report immersive-vr support.",
				"Use a WebXR-capable headset browser and confirm headset access is allowed."));
		}
		if (capabilities.engineBooted !== true) {
			blockers.push(launchBlocker("engine-not-ready",
				"The engine and imported game data are not ready yet.",
				"Finish importing UT99 data and wait for the game to boot."));
		}
		if (nativeRequested) {
			if (capabilities.gpuBindingAvailable !== true) {
				blockers.push(launchBlocker("xrgpu-binding-unavailable",
					"XRGPUBinding is not exposed, so WebGPU game frames cannot be presented to the headset.",
					"A Chromium experimental WebXR-WebGPU flag may expose the API, but does not guarantee compositor support."));
			}
			if (capabilities.gpuCompatible === null) {
				blockers.push(launchBlocker("xr-gpu-check-pending",
					"The XR-compatible WebGPU device check has not finished.",
					"Wait for the WebGPU capability check to complete."));
			} else if (capabilities.gpuCompatible !== true) {
				blockers.push(launchBlocker("xr-gpu-unavailable",
					"An XR-compatible WebGPU device could not be created.",
					"Enable WebGPU and any browser WebXR-WebGPU experiment, then reload."));
			}
			if (capabilities.gpuDeviceReady !== true) {
				blockers.push(launchBlocker("engine-gpu-not-ready",
					"The engine's XR-compatible WebGPU device/frame bridge is not ready.",
					"Wait for engine initialization; reload if this does not clear."));
			}
		}
		const productionSessionReady = nativeRequested && capabilities.sessionActive === true &&
			capabilities.nativePhase === "running";
		return {
			schema: XR_LAUNCH_READINESS_SCHEMA,
			version: 1,
			mode: nativeRequested ? "native-webgpu" : "lifecycle-only",
			lifecycleOnly: !nativeRequested,
			productionPathRequested: nativeRequested,
			productionPresentation: productionSessionReady,
			canAttempt: blockers.length === 0,
			productionSessionReady: productionSessionReady,
			nativePhase: capabilities.nativePhase || "idle",
			blockers: blockers,
		};
	};

	window.surrealXRGetLaunchReadiness = function () {
		return window.surrealXREvaluateLaunchReadiness({
			nativeRequested: window.surrealXRUseNativeWebGPU === true,
			secureContext: globalThis.isSecureContext === true,
			xrSystemAvailable: !!navigator.xr,
			immersiveSupported: window.surrealXRSupported,
			engineBooted: window.surrealBooted === true,
			gpuBindingAvailable: typeof globalThis.XRGPUBinding === "function",
			gpuCompatible: window.surrealXRGPUCompatible,
			gpuDeviceReady: !!window.surrealWebGPUDevice &&
				typeof window.surrealXRRenderWebGPUFrame === "function" &&
				typeof window.surrealSetXRFrameLoopActive === "function",
			sessionActive: window.surrealXRSessionActive === true,
			nativePhase: window.surrealXRNativeDiagnostics.phase,
		});
	};

	function notifyLaunchReadinessChanged() {
		window.dispatchEvent(new CustomEvent("surreal-webxr-readinesschange", {
			detail: window.surrealXRGetLaunchReadiness(),
		}));
	}
	window.surrealXRLifecycle = {
		visibilityHidden: document.hidden,
		visibilitySource: "initial",
		xrVisibilityState: null,
		audioPolicy: "unchanged",
		audioState: null,
		shutdown: false,
		deviceLost: false,
		deviceLostReason: null,
		lastExitReason: null,
		entryAttempts: 0,
		sessionsStarted: 0,
		sessionsEnded: 0,
	};

	let xrSession = null;
	let xrRefSpace = null;
	let xrCanvas = null; // throwaway WebGL2 canvas backing the session's baseLayer
	let xrEnterPending = false;
	let sessionGeneration = 0;
	let activeSessionGeneration = 0;
	let nativeBinding = null;
	let nativeProjectionLayer = null;
	let nativeResetGeneration = 0;
	let nativeOwnsEngineLoop = false;
	let activeSessionKind = null;
	let requestedExitGeneration = 0;
	let requestedExitReason = null;
	let exitEndPendingGeneration = 0;
	let interactionArmTimer = 0;
	let interactionTrackingStartedAt = null;
	let interactionTrackingIncident = false;
	let interactionHadHealthyTracking = false;
	let interactionSeenHands = new Set();
	let interactionChordStartedAt = null;
	let interactionChordProgressStep = -1;
	let audioSuspendedByLifecycle = false;
	let pageShuttingDown = false;
	let xrDeviceLost = false;
	let watchedGPUDevice = null;
	let inputSourceIds = new WeakMap();
	let nextInputSourceId = 1;
	let trackedInputSources = [];
	let inputSourceGeneration = 0;
	let pendingHapticPulses = new Map();
	let lastHapticDispatchTimes = new Map();
	let hapticGeneration = 0;
	const HAPTIC_POLICY = Object.freeze({
		minimumDurationMs: 1,
		maximumDurationMs: 1000,
		minimumIntervalMs: 50,
	});
	window.surrealXRHapticPolicy = HAPTIC_POLICY;
	window.surrealXRHapticsEnabled = window.surrealXRHapticsEnabled !== false;
	window.surrealXRHapticDiagnostics = null;
	window.surrealXRInputDiagnostics = {
		generation: 0,
		changeEvents: 0,
		connectedCount: 0,
		lastSourceIds: [],
		lastPressedMasks: [],
	};
	const interactionStatus = {
		schema: XR_INTERACTION_SCHEMA,
		version: 1,
		active: false,
		generation: 0,
		domOverlayType: null,
		exitPhase: "idle",
		exitSource: null,
		confirmReadyAt: null,
		confirmExpiresAt: null,
		exitRequests: 0,
		exitConfirmations: 0,
		exitCancellations: 0,
		exitRejections: 0,
		chordPhase: "idle",
		chordProgress: 0,
		trackingPhase: "inactive",
		trackingMessage: "XR controls inactive.",
		connectedControllers: 0,
		pointerControllers: 0,
		expectedControllers: 0,
		trackingLosses: 0,
		trackingRecoveries: 0,
		lastExitReason: null,
	};

	function interactionNow(value) {
		value = Number(value);
		return Number.isFinite(value) ? value : performance.now();
	}

	function copyInteractionStatus() {
		return Object.assign({}, interactionStatus);
	}

	function notifyInteractionChanged() {
		window.dispatchEvent(new CustomEvent("surreal-webxr-interactionchange", {
			detail: copyInteractionStatus(),
		}));
	}

	window.surrealXRGetInteractionStatus = copyInteractionStatus;

	function clearInteractionArmTimer() {
		if (interactionArmTimer) clearTimeout(interactionArmTimer);
		interactionArmTimer = 0;
	}

	function resetSafeExitIntent(notify) {
		clearInteractionArmTimer();
		interactionStatus.exitPhase = "idle";
		interactionStatus.exitSource = null;
		interactionStatus.confirmReadyAt = null;
		interactionStatus.confirmExpiresAt = null;
		if (notify) notifyInteractionChanged();
	}

	function beginInteractionSession(generation, session) {
		clearInteractionArmTimer();
		interactionTrackingStartedAt = null;
		interactionTrackingIncident = false;
		interactionHadHealthyTracking = false;
		interactionSeenHands = new Set();
		interactionChordStartedAt = null;
		interactionChordProgressStep = -1;
		interactionStatus.active = true;
		interactionStatus.generation = generation;
		interactionStatus.domOverlayType = session && session.domOverlayState ?
			String(session.domOverlayState.type || "available") : null;
		interactionStatus.exitPhase = "idle";
		interactionStatus.exitSource = null;
		interactionStatus.confirmReadyAt = null;
		interactionStatus.confirmExpiresAt = null;
		interactionStatus.chordPhase = "idle";
		interactionStatus.chordProgress = 0;
		interactionStatus.trackingPhase = "acquiring";
		interactionStatus.trackingMessage = "Acquiring controller pointers...";
		interactionStatus.connectedControllers = 0;
		interactionStatus.pointerControllers = 0;
		interactionStatus.expectedControllers = 0;
		interactionStatus.lastExitReason = null;
		notifyInteractionChanged();
	}

	function finishInteractionSession(generation, reason) {
		if (interactionStatus.generation !== generation) return;
		clearInteractionArmTimer();
		interactionTrackingStartedAt = null;
		interactionTrackingIncident = false;
		interactionHadHealthyTracking = false;
		interactionSeenHands = new Set();
		interactionChordStartedAt = null;
		interactionChordProgressStep = -1;
		interactionStatus.active = false;
		interactionStatus.generation = 0;
		interactionStatus.domOverlayType = null;
		interactionStatus.exitPhase = "idle";
		interactionStatus.exitSource = null;
		interactionStatus.confirmReadyAt = null;
		interactionStatus.confirmExpiresAt = null;
		interactionStatus.chordPhase = "idle";
		interactionStatus.chordProgress = 0;
		interactionStatus.trackingPhase = "inactive";
		interactionStatus.trackingMessage = "XR controls inactive.";
		interactionStatus.connectedControllers = 0;
		interactionStatus.pointerControllers = 0;
		interactionStatus.expectedControllers = 0;
		interactionStatus.lastExitReason = reason || "ended";
		notifyInteractionChanged();
	}

	function setInteractionTracking(phase, message, connected, pointers, expected) {
		const changed = interactionStatus.trackingPhase !== phase ||
			interactionStatus.trackingMessage !== message ||
			interactionStatus.connectedControllers !== connected ||
			interactionStatus.pointerControllers !== pointers ||
			interactionStatus.expectedControllers !== expected;
		const impaired = phase === "degraded" || phase === "lost";
		if (impaired && !interactionTrackingIncident) {
			interactionTrackingIncident = true;
			interactionStatus.trackingLosses++;
		} else if (phase === "healthy" && interactionTrackingIncident) {
			interactionTrackingIncident = false;
			interactionStatus.trackingRecoveries++;
		}
		interactionStatus.trackingPhase = phase;
		interactionStatus.trackingMessage = message;
		interactionStatus.connectedControllers = connected;
		interactionStatus.pointerControllers = pointers;
		interactionStatus.expectedControllers = expected;
		if (phase === "healthy") interactionHadHealthyTracking = true;
		if (changed) notifyInteractionChanged();
	}

	function finishSafeExitFromChord(generation) {
		if (generation !== activeSessionGeneration || !interactionStatus.active ||
			interactionStatus.exitPhase === "ending") return false;
		clearInteractionArmTimer();
		interactionStatus.exitRequests++;
		interactionStatus.exitConfirmations++;
		interactionStatus.exitPhase = "ending";
		interactionStatus.exitSource = "controller-chord";
		interactionStatus.confirmReadyAt = null;
		interactionStatus.confirmExpiresAt = null;
		interactionStatus.chordPhase = "confirmed";
		interactionStatus.chordProgress = 1;
		notifyInteractionChanged();
		return window.surrealXRExit("safe-exit-controller-chord");
	}

	function updateControllerExitChord(snapshot, generation, timestamp) {
		const requiredMask = XR_INTERACTION_POLICY.gripButtonMask |
			XR_INTERACTION_POLICY.secondaryFaceButtonMask;
		let leftHeld = false;
		let rightHeld = false;
		for (const input of snapshot) {
			if (!input || (input.flags & XR_FRAME_ABI.inputFlags.connected) === 0) continue;
			const held = ((input.pressedMask || 0) & requiredMask) === requiredMask;
			if (input.handedness === "left") leftHeld = held;
			if (input.handedness === "right") rightHeld = held;
		}
		if (!leftHeld || !rightHeld) {
			if (interactionStatus.chordPhase === "holding") {
				interactionStatus.chordPhase = "idle";
				interactionStatus.chordProgress = 0;
				interactionChordStartedAt = null;
				interactionChordProgressStep = -1;
				notifyInteractionChanged();
			}
			return;
		}
		if (interactionChordStartedAt === null) {
			interactionChordStartedAt = timestamp;
			interactionChordProgressStep = 0;
			interactionStatus.chordPhase = "holding";
			interactionStatus.chordProgress = 0;
			notifyInteractionChanged();
			return;
		}
		const progress = Math.max(0, Math.min(1,
			(timestamp - interactionChordStartedAt) / XR_INTERACTION_POLICY.controllerChordHoldMs));
		const step = Math.floor(progress * 10);
		interactionStatus.chordProgress = progress;
		if (step !== interactionChordProgressStep) {
			interactionChordProgressStep = step;
			notifyInteractionChanged();
		}
		if (progress >= 1) finishSafeExitFromChord(generation);
	}

	function updateInteractionSnapshot(generation, snapshot, timestamp) {
		if (generation !== activeSessionGeneration || generation !== interactionStatus.generation ||
			!interactionStatus.active) return false;
		timestamp = interactionNow(timestamp);
		if (interactionTrackingStartedAt === null) interactionTrackingStartedAt = timestamp;
		snapshot = Array.isArray(snapshot) ? snapshot : [];
		const connected = snapshot.filter(input => input &&
			(input.flags & XR_FRAME_ABI.inputFlags.connected) !== 0);
		for (const input of connected) {
			if (input.handedness === "left" || input.handedness === "right")
				interactionSeenHands.add(input.handedness);
		}
		const pointers = connected.filter(input => input.aim !== null && input.aim !== undefined);
		const expected = Math.max(connected.length, interactionSeenHands.size);
		const graceActive = !interactionHadHealthyTracking && timestamp - interactionTrackingStartedAt <
			XR_INTERACTION_POLICY.trackingGraceMs;
		if (connected.length > 0 && connected.length === expected &&
			pointers.length === connected.length) {
			setInteractionTracking("healthy", connected.length === 1 ?
				"Controller pointer tracked." : "Controller pointers tracked.",
				connected.length, pointers.length, expected);
		} else if (pointers.length > 0) {
			const message = connected.length < expected ?
				"Controller tracking reduced (" + connected.length + " of " + expected +
				" connected). Wake or reconnect it; recovery is automatic." :
				"Controller pointer tracking reduced (" + pointers.length + " of " + connected.length +
				"). Keep controllers in view; recovery is automatic.";
			setInteractionTracking("degraded", message,
				connected.length, pointers.length, expected);
		} else if (graceActive) {
			setInteractionTracking("acquiring", "Acquiring controller pointers...",
				connected.length, 0, expected);
		} else if (connected.length > 0) {
			setInteractionTracking("lost",
				"Controller pointer tracking lost. Keep controllers in view; recovery is automatic.",
				connected.length, 0, expected);
		} else {
			setInteractionTracking("lost",
				"Controllers unavailable. Wake or reconnect them; recovery is automatic.", 0, 0, expected);
		}
		updateControllerExitChord(snapshot, generation, timestamp);
		return true;
	}

	// Exported for diagnostics and deterministic harnesses. The values are
	// already copied scalars from collectInputSnapshot(); no XR-owned objects
	// cross this boundary.
	window.surrealXRUpdateInteractionState = function (snapshot, timestamp) {
		return updateInteractionSnapshot(activeSessionGeneration, snapshot, timestamp);
	};

	window.surrealXRRequestSafeExit = function (source, timestamp) {
		source = String(source || "visible-button");
		const now = interactionNow(timestamp);
		if (!interactionStatus.active || !xrSession || !activeSessionGeneration) {
			interactionStatus.exitRejections++;
			return { accepted: false, action: "inactive", status: copyInteractionStatus() };
		}
		if (interactionStatus.exitPhase === "ending") {
			interactionStatus.exitRejections++;
			return { accepted: false, action: "already-ending", status: copyInteractionStatus() };
		}
		if (interactionStatus.exitPhase === "armed" &&
			interactionStatus.confirmExpiresAt !== null && now > interactionStatus.confirmExpiresAt) {
			resetSafeExitIntent(false);
		}
		if (interactionStatus.exitPhase === "armed") {
			if (source !== interactionStatus.exitSource) {
				interactionStatus.exitRejections++;
				return { accepted: false, action: "different-control", status: copyInteractionStatus() };
			}
			if (now < interactionStatus.confirmReadyAt) {
				interactionStatus.exitRejections++;
				return { accepted: false, action: "confirmation-too-fast", status: copyInteractionStatus() };
			}
			clearInteractionArmTimer();
			interactionStatus.exitConfirmations++;
			interactionStatus.exitPhase = "ending";
			interactionStatus.confirmReadyAt = null;
			interactionStatus.confirmExpiresAt = null;
			notifyInteractionChanged();
			const ended = window.surrealXRExit("safe-exit-" + source);
			return { accepted: ended, action: ended ? "confirmed" : "end-rejected",
				status: copyInteractionStatus() };
		}
		interactionStatus.exitRequests++;
		interactionStatus.exitPhase = "armed";
		interactionStatus.exitSource = source;
		interactionStatus.confirmReadyAt = now + XR_INTERACTION_POLICY.confirmDelayMs;
		interactionStatus.confirmExpiresAt = now + XR_INTERACTION_POLICY.confirmWindowMs;
		const generation = activeSessionGeneration;
		interactionArmTimer = setTimeout(function () {
			interactionArmTimer = 0;
			if (generation === activeSessionGeneration && interactionStatus.exitPhase === "armed") {
				resetSafeExitIntent(true);
			}
		}, XR_INTERACTION_POLICY.confirmWindowMs + 1);
		notifyInteractionChanged();
		return { accepted: true, action: "armed", status: copyInteractionStatus() };
	};

	window.surrealXRCancelSafeExit = function () {
		if (interactionStatus.exitPhase !== "armed") return false;
		interactionStatus.exitCancellations++;
		resetSafeExitIntent(true);
		return true;
	};

	window.surrealXRBindInteractionUI = function (root) {
		root = root || document.getElementById("webxr-headset-controls");
		if (!root) return false;
		const exitButton = root.querySelector("[data-xr-safe-exit]");
		const cancelButton = root.querySelector("[data-xr-cancel-exit]");
		const tracking = root.querySelector("[data-xr-tracking]");
		const help = root.querySelector("[data-xr-exit-help]");
		function render() {
			const status = copyInteractionStatus();
			root.hidden = !status.active;
			root.dataset.xrActive = String(status.active);
			root.dataset.tracking = status.trackingPhase;
			if (tracking) {
				tracking.textContent = status.chordPhase === "holding" ?
					"Keep holding the exit chord: " + Math.round(status.chordProgress * 100) + "%" :
					status.trackingMessage;
			}
			if (help) {
				help.textContent = status.domOverlayType ?
					"Headset exit overlay active. Select Exit VR twice, or hold both grips and both B/Y buttons for 2.5 seconds." :
					"This runtime did not grant a headset DOM overlay. Hold both grips and both B/Y buttons for 2.5 seconds; the desktop exit button also requires confirmation.";
			}
			if (exitButton) {
				exitButton.disabled = !status.active || status.exitPhase === "ending";
				exitButton.textContent = status.exitPhase === "armed" ? "Confirm Exit VR" :
					status.exitPhase === "ending" ? "Exiting VR..." : "Exit VR";
			}
			if (cancelButton) cancelButton.hidden = status.exitPhase !== "armed";
		}
		if (root.dataset.xrInteractionBound !== "true") {
			root.dataset.xrInteractionBound = "true";
			// DOM Overlay select suppression applies only while pointing at these
			// controls; it avoids firing the weapon behind the UI. No system/menu
			// button is observed, cancelled, or remapped.
			root.addEventListener("beforexrselect", function (event) {
				event.preventDefault();
			});
			if (exitButton) exitButton.addEventListener("click", function () {
				window.surrealXRRequestSafeExit("headset-overlay-button");
			});
			if (cancelButton) cancelButton.addEventListener("click", function () {
				window.surrealXRCancelSafeExit();
			});
			window.addEventListener("surreal-webxr-interactionchange", render);
		}
		render();
		return true;
	};

	function newHapticDiagnostics(generation) {
		return {
			generation: generation || 0,
			enabled: window.surrealXRHapticsEnabled,
			pendingCount: 0,
			queued: 0,
			dispatched: 0,
			coalesced: 0,
			droppedDisabled: 0,
			droppedInactive: 0,
			droppedHidden: 0,
			droppedDisconnected: 0,
			droppedUnsupported: 0,
			droppedStale: 0,
			rejected: 0,
			lastHandedness: null,
			lastIntensity: null,
			lastDurationMs: null,
			lastActuator: null,
		};
	}

	function resetHapticState(generation) {
		pendingHapticPulses = new Map();
		lastHapticDispatchTimes = new Map();
		hapticGeneration = generation || 0;
		window.surrealXRHapticDiagnostics = newHapticDiagnostics(hapticGeneration);
	}

	resetHapticState(0);

	function resetInputSourceTracking(generation) {
		inputSourceIds = new WeakMap();
		nextInputSourceId = 1;
		trackedInputSources = [];
		inputSourceGeneration = generation || 0;
		window.surrealXRInputDiagnostics = {
			generation: inputSourceGeneration,
			changeEvents: 0,
			connectedCount: 0,
			lastSourceIds: [],
			lastPressedMasks: [],
		};
		resetHapticState(inputSourceGeneration);
	}

	function sourceIdFor(source) {
		let id = inputSourceIds.get(source);
		if (id === undefined) {
			id = nextInputSourceId++;
			if (nextInputSourceId === 0) nextInputSourceId = 1;
			inputSourceIds.set(source, id);
		}
		return id;
	}

	function syncInputSources(sources, generation, isChangeEvent) {
		if (generation !== inputSourceGeneration) return;
		trackedInputSources = Array.from(sources || []);
		for (const source of trackedInputSources) sourceIdFor(source);
		if (isChangeEvent) window.surrealXRInputDiagnostics.changeEvents++;
		window.surrealXRInputDiagnostics.connectedCount = Math.min(
			trackedInputSources.length, XR_FRAME_ABI.maxInputSources);
	}

	function beginInputSourceTracking(session, generation) {
		resetInputSourceTracking(generation);
		syncInputSources(session.inputSources, generation, false);
		session.addEventListener("inputsourceschange", function (event) {
			if (generation !== activeSessionGeneration) return;
			syncInputSources(event.session.inputSources, generation, true);
		});
	}

	function hapticActuatorForHand(handedness) {
		let sawConnected = false;
		for (const source of trackedInputSources) {
			if (!source || source.handedness !== handedness) continue;
			const gamepad = source.gamepad;
			if (!gamepad || gamepad.connected === false) continue;
			sawConnected = true;
			try {
				const actuators = Array.from(gamepad.hapticActuators || []);
				if (gamepad.vibrationActuator && !actuators.includes(gamepad.vibrationActuator)) {
					actuators.push(gamepad.vibrationActuator);
				}
				for (const actuator of actuators) {
					if (!actuator) continue;
					if (typeof actuator.pulse === "function") {
						return { status: "supported", actuator: actuator, mode: "pulse" };
					}
					if (typeof actuator.playEffect === "function") {
						return { status: "supported", actuator: actuator, mode: "playEffect" };
					}
				}
			} catch (_) {
				// Treat an accessor failure like an unsupported actuator. A source or
				// actuator is never retained beyond this lookup.
			}
		}
		return { status: sawConnected ? "unsupported" : "disconnected" };
	}

	function hapticsAreHidden() {
		return (xrSession && xrSession.visibilityState === "hidden") ||
			window.surrealXRLifecycle.xrVisibilityState === "hidden";
	}

	function updatePendingHapticCount() {
		window.surrealXRHapticDiagnostics.pendingCount = pendingHapticPulses.size;
	}

	function dropPendingHaptics(field) {
		const count = pendingHapticPulses.size;
		if (count && field) window.surrealXRHapticDiagnostics[field] += count;
		pendingHapticPulses.clear();
		updatePendingHapticCount();
	}

	// Queue only copied scalar values plus the active generation. The XRInputSource,
	// Gamepad and actuator are resolved again at dispatch, so none of the live
	// browser-owned objects can leak into another session generation.
	window.surrealXRQueueHapticPulse = function (handedness, intensity, durationMs) {
		handedness = String(handedness || "").toLowerCase();
		const diagnostics = window.surrealXRHapticDiagnostics;
		if (handedness !== "left" && handedness !== "right") {
			diagnostics.rejected++;
			return false;
		}
		if (!window.surrealXRHapticsEnabled) {
			diagnostics.droppedDisabled++;
			return false;
		}
		if (!xrSession || !window.surrealXRSessionActive || !activeSessionGeneration ||
			activeSessionGeneration !== hapticGeneration) {
			diagnostics.droppedInactive++;
			return false;
		}
		if (hapticsAreHidden()) {
			diagnostics.droppedHidden++;
			return false;
		}
		intensity = Number(intensity);
		durationMs = Number(durationMs);
		if (!Number.isFinite(intensity) || !Number.isFinite(durationMs)) {
			diagnostics.rejected++;
			return false;
		}
		intensity = Math.max(0, Math.min(1, intensity));
		durationMs = Math.max(HAPTIC_POLICY.minimumDurationMs,
			Math.min(HAPTIC_POLICY.maximumDurationMs, durationMs));
		if (intensity === 0) {
			diagnostics.rejected++;
			return false;
		}
		const target = hapticActuatorForHand(handedness);
		if (target.status !== "supported") {
			diagnostics[target.status === "unsupported" ?
				"droppedUnsupported" : "droppedDisconnected"]++;
			return false;
		}

		const prior = pendingHapticPulses.get(handedness);
		if (prior && prior.generation === activeSessionGeneration) {
			prior.intensity = Math.max(prior.intensity, intensity);
			prior.durationMs = Math.max(prior.durationMs, durationMs);
			diagnostics.coalesced++;
		} else {
			pendingHapticPulses.set(handedness, {
				handedness: handedness,
				intensity: intensity,
				durationMs: durationMs,
				generation: activeSessionGeneration,
			});
			diagnostics.queued++;
		}
		updatePendingHapticCount();
		return true;
	};

	window.surrealXRSetHapticsEnabled = function (enabled) {
		window.surrealXRHapticsEnabled = !!enabled;
		window.surrealXRHapticDiagnostics.enabled = window.surrealXRHapticsEnabled;
		if (!window.surrealXRHapticsEnabled) dropPendingHaptics("droppedDisabled");
		return window.surrealXRHapticsEnabled;
	};

	function noteHapticPromise(result, diagnostics) {
		if (!result || typeof result.then !== "function") return;
		result.then(function (accepted) {
			if (accepted === false) diagnostics.rejected++;
		}).catch(function () {
			diagnostics.rejected++;
		});
	}

	function flushHaptics(generation, timestamp) {
		const diagnostics = window.surrealXRHapticDiagnostics;
		if (!pendingHapticPulses.size) return 0;
		if (!xrSession || !window.surrealXRSessionActive || generation !== activeSessionGeneration ||
			generation !== hapticGeneration) {
			dropPendingHaptics(generation !== activeSessionGeneration || generation !== hapticGeneration ?
				"droppedStale" : "droppedInactive");
			return 0;
		}
		if (!window.surrealXRHapticsEnabled) {
			dropPendingHaptics("droppedDisabled");
			return 0;
		}
		if (hapticsAreHidden()) {
			dropPendingHaptics("droppedHidden");
			return 0;
		}
		timestamp = Number(timestamp);
		if (!Number.isFinite(timestamp)) timestamp = performance.now();
		let dispatched = 0;
		for (const [handedness, pulse] of Array.from(pendingHapticPulses.entries())) {
			if (pulse.generation !== generation) {
				pendingHapticPulses.delete(handedness);
				diagnostics.droppedStale++;
				continue;
			}
			const lastTime = lastHapticDispatchTimes.get(handedness);
			if (lastTime !== undefined && timestamp - lastTime < HAPTIC_POLICY.minimumIntervalMs) {
				continue;
			}
			const target = hapticActuatorForHand(handedness);
			if (target.status !== "supported") {
				pendingHapticPulses.delete(handedness);
				diagnostics[target.status === "unsupported" ?
					"droppedUnsupported" : "droppedDisconnected"]++;
				continue;
			}
			pendingHapticPulses.delete(handedness);
			lastHapticDispatchTimes.set(handedness, timestamp);
			try {
				let result;
				if (target.mode === "pulse") {
					result = target.actuator.pulse(pulse.intensity, pulse.durationMs);
				} else {
					result = target.actuator.playEffect("dual-rumble", {
						duration: pulse.durationMs,
						startDelay: 0,
						strongMagnitude: pulse.intensity,
						weakMagnitude: pulse.intensity,
					});
				}
				noteHapticPromise(result, diagnostics);
				diagnostics.dispatched++;
				diagnostics.lastHandedness = handedness;
				diagnostics.lastIntensity = pulse.intensity;
				diagnostics.lastDurationMs = pulse.durationMs;
				diagnostics.lastActuator = target.mode;
				dispatched++;
			} catch (_) {
				diagnostics.rejected++;
			}
		}
		updatePendingHapticCount();
		return dispatched;
	}

	window.surrealXRFlushHaptics = function (timestamp) {
		return flushHaptics(activeSessionGeneration, timestamp);
	};

	function clampFinite(value, minimum, maximum) {
		value = Number(value);
		if (!Number.isFinite(value)) return 0;
		return Math.max(minimum, Math.min(maximum, value));
	}

	// WebXR xr-standard preserves touchpad in slots 0/1 and thumbstick in
	// slots 2/3. Normalize each pair radially, then convert Gamepad's negative-Y
	// forward/up convention to the engine's positive-Y forward/up convention.
	function normalizeAxes(liveAxes) {
		const axes = [0, 0, 0, 0];
		for (let n = 0; n < 4; n++) axes[n] = clampFinite(liveAxes && liveAxes[n], -1, 1);
		const deadzone = 0.15;
		for (let n = 0; n < 4; n += 2) {
			const x = axes[n];
			const y = axes[n + 1];
			const magnitude = Math.hypot(x, y);
			if (magnitude <= deadzone) {
				axes[n] = 0;
				axes[n + 1] = 0;
				continue;
			}
			const normalizedMagnitude = Math.min(1, (magnitude - deadzone) / (1 - deadzone));
			const scale = normalizedMagnitude / magnitude;
			axes[n] = x * scale;
			axes[n + 1] = -y * scale;
		}
		return axes;
	}

	function copyTrackedPose(frame, space, referenceSpace) {
		if (!space || !frame || typeof frame.getPose !== "function") return null;
		let pose;
		try { pose = frame.getPose(space, referenceSpace); }
		catch (_) { return null; }
		if (!pose || !pose.transform) return null;
		const position = pose.transform.position || {};
		const orientation = pose.transform.orientation || {};
		const px = Number(position.x), py = Number(position.y), pz = Number(position.z);
		let qx = Number(orientation.x), qy = Number(orientation.y);
		let qz = Number(orientation.z), qw = Number(orientation.w);
		if (![px, py, pz, qx, qy, qz, qw].every(Number.isFinite)) return null;
		const length = Math.hypot(qx, qy, qz, qw);
		if (!(length > 0.000001)) return null;
		qx /= length; qy /= length; qz /= length; qw /= length;
		return {
			position: { x: px, y: py, z: pz },
			orientation: { x: qx, y: qy, z: qz, w: qw },
		};
	}

	function collectInputSnapshot(frame, referenceSpace) {
		if (xrSession && frame && frame.session === xrSession) {
			syncInputSources(xrSession.inputSources, inputSourceGeneration, false);
		}
		const snapshot = [];
		for (const source of trackedInputSources.slice(0, XR_FRAME_ABI.maxInputSources)) {
			const gamepad = source.gamepad;
			const grip = copyTrackedPose(frame, source.gripSpace, referenceSpace);
			const aim = copyTrackedPose(frame, source.targetRaySpace, referenceSpace);
			let flags = 0;
			if (aim) flags |= XR_FRAME_ABI.inputFlags.aim;
			if (grip) flags |= XR_FRAME_ABI.inputFlags.grip;
			if (!gamepad || gamepad.connected !== false) flags |= XR_FRAME_ABI.inputFlags.connected;
			if (gamepad && gamepad.mapping === "xr-standard") flags |= XR_FRAME_ABI.inputFlags.xrStandard;
			let pressedMask = 0;
			let touchedMask = 0;
			const values = new Array(8).fill(0);
			const buttons = gamepad && gamepad.buttons ? gamepad.buttons : [];
			for (let buttonIndex = 0; buttonIndex < Math.min(16, buttons.length); buttonIndex++) {
				const button = buttons[buttonIndex] || {};
				if (button.pressed) pressedMask |= 1 << buttonIndex;
				if (button.touched) touchedMask |= 1 << buttonIndex;
				if (buttonIndex < values.length)
					values[buttonIndex] = clampFinite(button.value, 0, 1);
			}
			snapshot.push({
				sourceId: sourceIdFor(source),
				handedness: source.handedness || "none",
				flags: flags,
				pressedMask: pressedMask >>> 0,
				touchedMask: touchedMask >>> 0,
				axes: normalizeAxes(gamepad && gamepad.axes),
				values: values,
				grip: grip,
				aim: aim,
			});
		}
		window.surrealXRInputDiagnostics.connectedCount = snapshot.filter(input =>
			(input.flags & XR_FRAME_ABI.inputFlags.connected) !== 0).length;
		window.surrealXRInputDiagnostics.lastSourceIds = snapshot.map(input => input.sourceId);
		window.surrealXRInputDiagnostics.lastPressedMasks = snapshot.map(input => input.pressedMask);
		return snapshot;
	}

	window.surrealXRCollectInputSnapshot = function (frame, referenceSpace, sources) {
		if (sources !== undefined) syncInputSources(sources, inputSourceGeneration, false);
		return collectInputSnapshot(frame, referenceSpace);
	};

	window.surrealXRTestInputCollector = function () {
		if (xrSession) return { passed: false, error: "collector self-test requires no active session" };
		const leftGrip = { kind: "left-grip" };
		const leftAim = { kind: "left-aim" };
		const rightAim = { kind: "right-aim" };
		const leftButtons = [
			{ value: 0.25, pressed: false, touched: true },
			{ value: 1.5, pressed: true, touched: true },
		];
		// Standard Gamepad D-pad Up lives at button index 12. Masks retain it
		// even though the stable ABI deliberately keeps only eight analog values.
		leftButtons[12] = { value: 1, pressed: true, touched: false };
		const leftAxes = [0.1, -0.1, 0.5, -0.5];
		const left = {
			handedness: "left", gripSpace: leftGrip, targetRaySpace: leftAim,
			gamepad: { connected: true, mapping: "xr-standard", buttons: leftButtons, axes: leftAxes },
		};
		const right = {
			handedness: "right", gripSpace: null, targetRaySpace: rightAim,
			gamepad: { connected: true, mapping: "", buttons: [], axes: [NaN, Infinity, -2, 2] },
		};
		const frame = {
			getPose: function (space) {
				const x = space === leftGrip ? 1 : (space === leftAim ? 2 : 3);
				return { transform: {
					position: { x: x, y: x + 0.25, z: -x },
					orientation: { x: 0, y: 0, z: 0, w: 2 },
				} };
			},
		};
		const near = (actual, expected) => Math.abs(actual - expected) < 0.00001;
		try {
			resetInputSourceTracking(0);
			syncInputSources([left, right], 0, true);
			const initial = collectInputSnapshot(frame, {});
			const initialPacket = window.surrealXRPackFrame({ inputs: initial });
			const initialData = new DataView(initialPacket);

			// Mutate the same live arrays/objects in place. A correct collector has
			// already copied the prior frame, so its edge snapshot cannot change.
			leftButtons[0].value = 0.8;
			leftButtons[0].pressed = true;
			leftButtons[1].pressed = false;
			leftAxes[2] = -1;
			leftAxes[3] = 0;
			const changed = collectInputSnapshot(frame, {});

			syncInputSources([right], 0, true);
			const disconnected = collectInputSnapshot(frame, {});
			const rightId = initial[1].sourceId;
			syncInputSources([], 0, true);
			const cleared = collectInputSnapshot(frame, {});
			const checks = [
				initial.length === 2, initial[0].sourceId === 1, initial[1].sourceId === 2,
				initial[0].flags === 15, initial[1].flags === 5,
				initial[0].pressedMask === 4098, initial[0].touchedMask === 3,
				near(initial[0].values[0], 0.25), initial[0].values[1] === 1,
				initial[0].axes[0] === 0, initial[0].axes[1] === 0,
				initial[0].axes[2] > 0.46 && initial[0].axes[2] < 0.47,
				initial[0].axes[3] > 0.46 && initial[0].axes[3] < 0.47,
				initial[1].axes[0] === 0, initial[1].axes[1] === 0,
				initial[1].axes[2] < -0.7, initial[1].axes[3] < -0.7,
				initial[0].grip.position.x === 1, initial[0].aim.position.x === 2,
				initial[1].grip === null, initial[1].aim.position.x === 3,
				initialPacket.byteLength === 300,
				initialData.getUint32(0, true) === 2,
				initialData.getUint32(36, true) === 2,
				initialData.getUint32(40, true) === 44,
				initialData.getUint32(44, true) === 1,
				initialData.getUint32(56, true) === 4098,
				near(initialData.getFloat32(84, true), 0.25),
				// Copied-edge proof: changed live state is new while initial is immutable.
				near(initial[0].values[0], 0.25), near(changed[0].values[0], 0.8),
				initial[0].pressedMask === 4098, changed[0].pressedMask === 4097,
				initial[0].axes[2] > 0, changed[0].axes[2] === -1,
				changed[0].sourceId === initial[0].sourceId,
				disconnected.length === 1, disconnected[0].sourceId === rightId,
				cleared.length === 0,
			];
			return {
				passed: checks.every(Boolean),
				checksPassed: checks.filter(Boolean).length,
				checkCount: checks.length,
				initial: {
					ids: initial.map(input => input.sourceId),
					pressedMasks: initial.map(input => input.pressedMask),
					leftAxes: initial[0].axes.slice(), leftValue0: initial[0].values[0],
				},
				changed: {
					ids: changed.map(input => input.sourceId),
					pressedMasks: changed.map(input => input.pressedMask),
					leftAxes: changed[0].axes.slice(), leftValue0: changed[0].values[0],
				},
				disconnected: { ids: disconnected.map(input => input.sourceId) },
				clearedCount: cleared.length,
			};
		} finally {
			resetInputSourceTracking(0);
		}
	};

	// Deterministic fake-actuator coverage. This intentionally does not emulate
	// physical haptic output; it verifies only routing, policy and API calls.
	window.surrealXRTestHaptics = function () {
		if (xrSession) return { passed: false, error: "haptics self-test requires no active session" };
		const saved = {
			xrSession: xrSession,
			activeSessionGeneration: activeSessionGeneration,
			activeSessionKind: activeSessionKind,
			inputSourceIds: inputSourceIds,
			nextInputSourceId: nextInputSourceId,
			trackedInputSources: trackedInputSources,
			inputSourceGeneration: inputSourceGeneration,
			pendingHapticPulses: pendingHapticPulses,
			lastHapticDispatchTimes: lastHapticDispatchTimes,
			hapticGeneration: hapticGeneration,
			sessionActive: window.surrealXRSessionActive,
			hapticsEnabled: window.surrealXRHapticsEnabled,
			hapticDiagnostics: window.surrealXRHapticDiagnostics,
			inputDiagnostics: window.surrealXRInputDiagnostics,
			xrVisibilityState: window.surrealXRLifecycle.xrVisibilityState,
		};
		const leftCalls = [];
		const rightCalls = [];
		const leftActuator = {
			pulse: function (intensity, durationMs) {
				leftCalls.push({ intensity: intensity, durationMs: durationMs });
				return true;
			},
		};
		const rightActuator = {
			playEffect: function (type, options) {
				rightCalls.push({ type: type, options: Object.assign({}, options) });
				return true;
			},
		};
		const left = {
			handedness: "left",
			gamepad: { connected: true, hapticActuators: [leftActuator] },
		};
		const right = {
			handedness: "right",
			gamepad: { connected: true, vibrationActuator: rightActuator },
		};

		function installFakeSession(generation, sources) {
			xrSession = { visibilityState: "visible" };
			activeSessionGeneration = generation;
			activeSessionKind = "default";
			window.surrealXRSessionActive = true;
			window.surrealXRLifecycle.xrVisibilityState = "visible";
			inputSourceIds = new WeakMap();
			nextInputSourceId = 1;
			trackedInputSources = Array.from(sources || []);
			inputSourceGeneration = generation;
			resetHapticState(generation);
		}

		try {
			installFakeSession(701, [left, right]);
			const queuedLeft = window.surrealXRQueueHapticPulse("left", 5, 5000);
			const queuedRecord = pendingHapticPulses.get("left");
			const retainedLiveSource = !!queuedRecord &&
				Object.values(queuedRecord).some(value => value === left || value === leftActuator);
			const queuedRight = window.surrealXRQueueHapticPulse("right", 2, 2000);
			const initialDispatch = flushHaptics(701, 1000);

			const queuedRateLimited = window.surrealXRQueueHapticPulse("left", 0.2, 100);
			const tooSoonDispatch = flushHaptics(701, 1020);
			const queuedCoalesced = window.surrealXRQueueHapticPulse("left", 0.8, 200);
			const rateLimitedDispatch = flushHaptics(701, 1050);
			const routingDiagnostics = Object.assign({}, window.surrealXRHapticDiagnostics);

			const queuedBeforeDisable = window.surrealXRQueueHapticPulse("right", 0.4, 30);
			window.surrealXRSetHapticsEnabled(false);
			const disabledPendingCount = window.surrealXRHapticDiagnostics.pendingCount;
			const disabledAccepted = window.surrealXRQueueHapticPulse("right", 0.5, 30);
			const disabledDrops = window.surrealXRHapticDiagnostics.droppedDisabled;
			window.surrealXRSetHapticsEnabled(true);

			const queuedBeforeHidden = window.surrealXRQueueHapticPulse("left", 0.5, 30);
			xrSession.visibilityState = "hidden";
			const hiddenDispatch = flushHaptics(701, 1100);
			const hiddenAccepted = window.surrealXRQueueHapticPulse("left", 0.5, 30);
			const hiddenDrops = window.surrealXRHapticDiagnostics.droppedHidden;
			xrSession.visibilityState = "visible";

			window.surrealXRSessionActive = false;
			const inactiveAccepted = window.surrealXRQueueHapticPulse("left", 0.5, 30);
			const inactiveDrops = window.surrealXRHapticDiagnostics.droppedInactive;
			window.surrealXRSessionActive = true;

			installFakeSession(702, [left]);
			const staleQueued = window.surrealXRQueueHapticPulse("left", 0.4, 40);
			activeSessionGeneration = 703;
			const staleDispatch = flushHaptics(703, 2000);
			const staleDiagnostics = Object.assign({}, window.surrealXRHapticDiagnostics);

			installFakeSession(704, [right]);
			const disconnectQueued = window.surrealXRQueueHapticPulse("right", 0.6, 60);
			right.gamepad.connected = false;
			const disconnectDispatch = flushHaptics(704, 3000);
			const disconnectDiagnostics = Object.assign({}, window.surrealXRHapticDiagnostics);
			right.gamepad.connected = true;

			const unsupported = {
				handedness: "right",
				gamepad: { connected: true, hapticActuators: [{}] },
			};
			installFakeSession(705, [unsupported]);
			const unsupportedAccepted = window.surrealXRQueueHapticPulse("right", 0.5, 50);
			const unsupportedDiagnostics = Object.assign({}, window.surrealXRHapticDiagnostics);

			const leftClamped = leftCalls[0] || {};
			const leftCoalesced = leftCalls[1] || {};
			const rightClamped = rightCalls[0] || { options: {} };
			const checks = [
				queuedLeft, queuedRight, initialDispatch === 2,
				leftClamped.intensity === 1, leftClamped.durationMs === 1000,
				rightClamped.type === "dual-rumble",
				rightClamped.options.strongMagnitude === 1,
				rightClamped.options.weakMagnitude === 1,
				rightClamped.options.duration === 1000,
				!retainedLiveSource,
				queuedRateLimited, tooSoonDispatch === 0, queuedCoalesced,
				rateLimitedDispatch === 1, leftCalls.length === 2,
				leftCoalesced.intensity === 0.8, leftCoalesced.durationMs === 200,
				routingDiagnostics.coalesced === 1,
				queuedBeforeDisable, disabledPendingCount === 0,
				disabledAccepted === false, disabledDrops === 2,
				queuedBeforeHidden, hiddenDispatch === 0,
				hiddenAccepted === false, hiddenDrops === 2,
				inactiveAccepted === false, inactiveDrops === 1,
				staleQueued, staleDispatch === 0, staleDiagnostics.droppedStale === 1,
				staleDiagnostics.pendingCount === 0,
				disconnectQueued, disconnectDispatch === 0,
				disconnectDiagnostics.droppedDisconnected === 1,
				unsupportedAccepted === false,
				unsupportedDiagnostics.droppedUnsupported === 1,
				rightCalls.length === 1,
			];
			return {
				passed: checks.every(Boolean),
				checksPassed: checks.filter(Boolean).length,
				checkCount: checks.length,
				fakeActuatorsOnly: true,
				leftCalls: leftCalls,
				rightCalls: rightCalls,
				routing: routingDiagnostics,
				stale: staleDiagnostics,
				disconnect: disconnectDiagnostics,
				unsupported: unsupportedDiagnostics,
			};
		} finally {
			right.gamepad.connected = true;
			xrSession = saved.xrSession;
			activeSessionGeneration = saved.activeSessionGeneration;
			activeSessionKind = saved.activeSessionKind;
			inputSourceIds = saved.inputSourceIds;
			nextInputSourceId = saved.nextInputSourceId;
			trackedInputSources = saved.trackedInputSources;
			inputSourceGeneration = saved.inputSourceGeneration;
			pendingHapticPulses = saved.pendingHapticPulses;
			lastHapticDispatchTimes = saved.lastHapticDispatchTimes;
			hapticGeneration = saved.hapticGeneration;
			window.surrealXRSessionActive = saved.sessionActive;
			window.surrealXRHapticsEnabled = saved.hapticsEnabled;
			window.surrealXRHapticDiagnostics = saved.hapticDiagnostics;
			window.surrealXRInputDiagnostics = saved.inputDiagnostics;
			window.surrealXRLifecycle.xrVisibilityState = saved.xrVisibilityState;
		}
	};

	function xrLog(line) {
		window.surrealXRLog.push(line);
		console.log("[webxr] " + line);
	}

	function getWebAudioContext() {
		if (typeof AL === "undefined" || !AL.currentCtx) return null;
		return AL.currentCtx.audioCtx || null;
	}

	// Immersive presentation can cause the companion DOM page to become
	// hidden. Keep audio alive while XR is active; outside XR, suspend it to
	// avoid a background tab continuing to play. Resume only when this policy
	// performed the suspension, preserving intentional user/browser suspension.
	async function applyVisibilityPolicy(hidden, source) {
		window.surrealXRLifecycle.visibilityHidden = !!hidden;
		window.surrealXRLifecycle.visibilitySource = source || "unknown";
		const context = getWebAudioContext();
		window.surrealXRLifecycle.audioState = context ? context.state : "unavailable";
		// XRSession visibility is authoritative during immersive presentation.
		// A companion DOM visibility event must not resume a hidden XR session
		// or relabel its deliberately suspended audio state.
		if (window.surrealXRSessionActive &&
			window.surrealXRLifecycle.xrVisibilityState === "hidden") {
			window.surrealXRLifecycle.audioPolicy = audioSuspendedByLifecycle ?
				"suspended-xr-hidden" : "unchanged-xr-hidden";
			return window.surrealXRLifecycle;
		}

		if (hidden) {
			if (window.surrealXRSessionActive) {
				window.surrealXRLifecycle.audioPolicy = "kept-running-for-xr";
				return window.surrealXRLifecycle;
			}
			if (context && context.state === "running") {
				audioSuspendedByLifecycle = true;
				window.surrealXRLifecycle.audioPolicy = "suspending-hidden";
				try { await context.suspend(); }
				catch (e) { xrLog("Web Audio suspend failed: " + e); }
			}
			window.surrealXRLifecycle.audioState = context ? context.state : "unavailable";
			window.surrealXRLifecycle.audioPolicy = audioSuspendedByLifecycle ?
				"suspended-hidden" : "unchanged-hidden";
			return window.surrealXRLifecycle;
		}

		if (audioSuspendedByLifecycle && context && context.state !== "closed") {
			window.surrealXRLifecycle.audioPolicy = "resuming-visible";
			try {
				await context.resume();
				audioSuspendedByLifecycle = false;
			} catch (e) {
				xrLog("Web Audio visibility resume failed: " + e);
			}
		}
		window.surrealXRLifecycle.audioState = context ? context.state : "unavailable";
		window.surrealXRLifecycle.audioPolicy = audioSuspendedByLifecycle ?
			"resume-blocked" : "running-visible";
		return window.surrealXRLifecycle;
	}

	async function applySessionVisibilityPolicy(generation, state, source) {
		if (generation !== activeSessionGeneration || !xrSession) {
			return window.surrealXRLifecycle;
		}
		state = state || "visible";
		window.surrealXRLifecycle.xrVisibilityState = state;
		window.surrealXRLifecycle.visibilitySource = source || "xr-visibilitychange";
		const context = getWebAudioContext();

		if (state === "hidden") {
			// A hidden XR session does not receive animation frames. Drop any
			// queued feedback now rather than replaying stale effects on resume.
			dropPendingHaptics("droppedHidden");
			if (context && context.state === "running") {
				audioSuspendedByLifecycle = true;
				window.surrealXRLifecycle.audioPolicy = "suspending-xr-hidden";
				try { await context.suspend(); }
				catch (e) { xrLog("Web Audio XR-hidden suspend failed: " + e); }
			}
			window.surrealXRLifecycle.audioState = context ? context.state : "unavailable";
			window.surrealXRLifecycle.audioPolicy = audioSuspendedByLifecycle ?
				"suspended-xr-hidden" : "unchanged-xr-hidden";
			return window.surrealXRLifecycle;
		}

		// visible-blurred still represents active immersive presentation. Keep
		// audio continuous just as for visible, while input handling can remain
		// independently constrained by the browser/runtime.
		if (audioSuspendedByLifecycle && context && context.state !== "closed") {
			window.surrealXRLifecycle.audioPolicy = "resuming-xr-" + state;
			try {
				await context.resume();
				audioSuspendedByLifecycle = false;
			} catch (e) {
				xrLog("Web Audio XR visibility resume failed: " + e);
			}
		}
		window.surrealXRLifecycle.audioState = context ? context.state : "unavailable";
		window.surrealXRLifecycle.audioPolicy = audioSuspendedByLifecycle ?
			"resume-blocked-xr-" + state : "running-xr-" + state;
		return window.surrealXRLifecycle;
	}

	window.surrealXRApplyVisibilityPolicy = function (hidden, source) {
		return applyVisibilityPolicy(!!hidden, source || "explicit");
	};
	window.surrealXRApplySessionVisibilityPolicy = function (state, source) {
		return applySessionVisibilityPolicy(activeSessionGeneration, state, source || "explicit-xr");
	};

	xrLog("XRGPUBinding available = " + window.surrealXRGPUBindingAvailable);

	// --- Capability checks (no session side effects) ---------------------

	window.surrealXRCheckSupport = async function () {
		if (!navigator.xr) {
			window.surrealXRSupported = false;
			xrLog("navigator.xr is undefined (no WebXR support in this browser)");
			notifyLaunchReadinessChanged();
			return false;
		}
		try {
			const ok = await navigator.xr.isSessionSupported("immersive-vr");
			window.surrealXRSupported = ok;
			xrLog("isSessionSupported('immersive-vr') = " + ok);
			notifyLaunchReadinessChanged();
			return ok;
		} catch (e) {
			window.surrealXRError = String(e);
			xrLog("isSessionSupported threw: " + e);
			notifyLaunchReadinessChanged();
			return false;
		}
	};

	// Probes the XRGPUBinding-shaped "xrCompatible GPUDevice" request path
	// (Immersive Web Editor's Draft — see Docs/VR/WEBXR_PORT_PLAN.md's
	// Precedent section). This is a capability probe only, independent of
	// any session: real M4 will hand a device acquired this way to
	// XRGPUBinding once that path is implemented engine-side. Not exercised
	// by IWER (no XRGPUBinding emulation) — this runs against the real
	// browser's navigator.gpu, so it's meaningful even in the headless test.
	window.surrealXRCheckGPUCompatible = async function () {
		if (!navigator.gpu) {
			window.surrealXRGPUCompatible = false;
			xrLog("navigator.gpu is undefined, cannot probe xrCompatible adapter");
			notifyLaunchReadinessChanged();
			return false;
		}
		try {
			const adapter = await navigator.gpu.requestAdapter({ xrCompatible: true });
			if (!adapter) {
				window.surrealXRGPUCompatible = false;
				xrLog("requestAdapter({xrCompatible:true}) resolved null");
				notifyLaunchReadinessChanged();
				return false;
			}
			const device = await adapter.requestDevice();
			window.surrealXRGPUCompatible = true;
			xrLog("xrCompatible GPUAdapter/GPUDevice acquired OK (adapter info: " +
				JSON.stringify(adapter.info || {}) + ")");
			device.destroy();
			notifyLaunchReadinessChanged();
			return true;
		} catch (e) {
			window.surrealXRGPUCompatible = false;
			window.surrealXRError = String(e);
			xrLog("xrCompatible GPUDevice probe failed: " + e);
			notifyLaunchReadinessChanged();
			return false;
		}
	};

	// Attempts the real WebGPU/WebXR setup sequence through projection-layer
	// creation, but deliberately does not render. This is both an automation
	// probe and a user-gesture-safe button action for testing on a real headset.
	// Expected to reject when the JS-only IWER session cannot satisfy Blink's
	// native XRSession type check; that result is recorded rather than promoted
	// to surrealXRError because it identifies a harness limitation, not an
	// engine crash.
	window.surrealXRProbeWebGPUProjection = async function () {
		const result = {
			supported: window.surrealXRGPUBindingAvailable,
			sessionCreated: false,
			bindingCreated: false,
			layerCreated: false,
			colorFormat: null,
			error: null,
		};
		window.surrealXRWebGPUProbe = result;

		if (!result.supported) {
			result.error = "XRGPUBinding is not exposed";
			xrLog("WebGPU projection probe stopped: " + result.error);
			return result;
		}
		const device = window.surrealWebGPUDevice;
		if (!device) {
			result.error = "engine XR-compatible GPUDevice is not ready";
			xrLog("WebGPU projection probe stopped: " + result.error);
			return result;
		}

		let probeSession = null;
		try {
			probeSession = await navigator.xr.requestSession("immersive-vr", {
				requiredFeatures: ["webgpu"],
			});
			result.sessionCreated = true;
			xrLog("WebGPU-compatible session requested");

			const binding = new XRGPUBinding(probeSession, device);
			result.bindingCreated = true;
			result.colorFormat = binding.getPreferredColorFormat();
			xrLog("XRGPUBinding created (preferred color format=" + result.colorFormat + ")");

			const layer = binding.createProjectionLayer({ colorFormat: result.colorFormat });
			await probeSession.updateRenderState({ layers: [layer] });
			result.layerCreated = true;
			xrLog("WebGPU XR projection layer created and installed");
		} catch (e) {
			result.error = e.name + ": " + e.message;
			xrLog("WebGPU projection probe failed: " + result.error);
		} finally {
			if (probeSession) {
				try { await probeSession.end(); } catch (_) {}
			}
		}
		return result;
	};

	// --- Session lifecycle -------------------------------------------------

	function nativeDiagnostic(phase, values) {
		Object.assign(window.surrealXRNativeDiagnostics, values || {});
		window.surrealXRNativeDiagnostics.phase = phase;
		notifyLaunchReadinessChanged();
	}

	function resetNativePoseState() {
		try {
			if (typeof window.surrealResetWebXRPose === "function") {
				window.surrealResetWebXRPose();
				return true;
			}
			if (typeof Module !== "undefined" && typeof Module.ccall === "function") {
				Module.ccall("Surreal_ResetWebXRPose", null, [], []);
				return true;
			}
		} catch (e) {
			xrLog("native WebXR pose reset unavailable: " + e);
		}
		return false;
	}

	function restoreCanvasFrameLoop(generation) {
		if (generation !== activeSessionGeneration) return;
		nativeOwnsEngineLoop = false;
		window.surrealXRNativeDiagnostics.engineLoopOwned = false;
		try {
			if (typeof window.surrealSetXRFrameLoopActive === "function") {
				window.surrealSetXRFrameLoopActive(false);
			}
		} catch (e) {
			xrLog("failed to restore canvas RAF ownership: " + e);
		}
	}

	function removeXRCanvas() {
		if (xrCanvas) {
			xrCanvas.remove();
			xrCanvas = null;
		}
	}

	function noteSessionEnded(reason) {
		window.surrealXRLifecycle.lastExitReason = reason || "ended";
		window.surrealXRLifecycle.xrVisibilityState = null;
		window.surrealXRLifecycle.sessionsEnded++;
		if (pageShuttingDown) return;
		if (window.surrealXRLifecycle.visibilityHidden) {
			applyVisibilityPolicy(true, "session-ended");
		} else {
			applyVisibilityPolicy(false, "session-ended");
		}
	}

	function finalizedExitReason(generation, fallback) {
		const normalEnd = fallback === "ended" || fallback === "session-end" ||
			fallback === "explicit-exit";
		const reason = requestedExitGeneration === generation && normalEnd ?
			(requestedExitReason || fallback) : fallback;
		if (requestedExitGeneration === generation) {
			requestedExitGeneration = 0;
			requestedExitReason = null;
		}
		if (exitEndPendingGeneration === generation) exitEndPendingGeneration = 0;
		return reason || "ended";
	}

	function finishNativeSession(generation, phase, error) {
		if (generation !== activeSessionGeneration) return;
		const wasActive = window.surrealXRSessionActive;
		const endReason = finalizedExitReason(generation, error ? "native-error" : (phase || "ended"));
		restoreCanvasFrameLoop(generation);
		activeSessionGeneration = 0; // invalidates every queued callback
		activeSessionKind = null;
		xrSession = null;
		xrRefSpace = null;
		nativeBinding = null;
		nativeProjectionLayer = null;
		nativeResetGeneration = 0;
		resetInputSourceTracking(0);
		resetNativePoseState();
		xrEnterPending = false;
		window.surrealXRSessionActive = false;
		finishInteractionSession(generation, endReason);
		if (wasActive) noteSessionEnded(endReason);
		if (error) {
			const message = error instanceof Error ? error.name + ": " + error.message : String(error);
			window.surrealXRNativeError = message;
			window.surrealXRError = message;
			nativeDiagnostic("error", { error: message });
			xrLog("native WebGPU session failed: " + message);
		} else {
			nativeDiagnostic(phase || "ended", { engineLoopOwned: false });
			xrLog("native WebGPU session ended");
		}
	}

	function finishDefaultSession(generation, reason, error) {
		if (generation !== activeSessionGeneration || activeSessionKind !== "default") return;
		const wasActive = window.surrealXRSessionActive;
		const endReason = finalizedExitReason(generation, error ? (reason || "error") : (reason || "ended"));
		activeSessionGeneration = 0;
		activeSessionKind = null;
		xrSession = null;
		xrRefSpace = null;
		resetInputSourceTracking(0);
		xrEnterPending = false;
		window.surrealXRSessionActive = false;
		removeXRCanvas();
		finishInteractionSession(generation, endReason);
		if (error) {
			window.surrealXRError = String(error);
			xrLog("XR session failed: " + error);
		}
		if (wasActive) noteSessionEnded(endReason);
		xrLog("session ended (reason=" + endReason + ")");
		notifyLaunchReadinessChanged();
	}

	function abortActiveSession(reason, error) {
		const generation = activeSessionGeneration;
		const session = xrSession;
		if (!generation) return false;
		if (activeSessionKind === "native") {
			finishNativeSession(generation, reason || "ended", error || null);
		} else {
			finishDefaultSession(generation, reason || "ended", error || null);
		}
		if (session) {
			try {
				const ending = session.end();
				if (ending && typeof ending.catch === "function") ending.catch(function () {});
			} catch (_) {}
		}
		return true;
	}

	function watchGPUDeviceLoss() {
		const device = window.surrealWebGPUDevice;
		if (!device || device === watchedGPUDevice || !device.lost) return;
		watchedGPUDevice = device;
		device.lost.then(function (info) {
			window.surrealXRHandleDeviceLoss(info || {});
		}).catch(function (error) {
			window.surrealXRHandleDeviceLoss({ reason: "unknown", message: String(error) });
		});
	}

	window.surrealXRHandleDeviceLoss = function (info) {
		if (xrDeviceLost) return false;
		xrDeviceLost = true;
		const reason = info && info.reason ? String(info.reason) : "unknown";
		const message = info && info.message ? String(info.message) : "WebGPU device lost";
		window.surrealXRLifecycle.deviceLost = true;
		window.surrealXRLifecycle.deviceLostReason = reason + ": " + message;
		xrLog("WebGPU device lost (" + reason + "): " + message);
		abortActiveSession("device-lost", new Error("WebGPU device lost: " + message));
		return true;
	};

	window.surrealXRShutdown = function (reason) {
		if (pageShuttingDown) return false;
		pageShuttingDown = true;
		window.surrealXRLifecycle.shutdown = true;
		window.surrealXRLifecycle.lastExitReason = reason || "page-shutdown";
		abortActiveSession(reason || "page-shutdown", null);
		applyVisibilityPolicy(true, reason || "page-shutdown");
		return true;
	};

	function failNativeSession(generation, error) {
		const session = generation === activeSessionGeneration ? xrSession : null;
		finishNativeSession(generation, "error", error);
		if (session) {
			try {
				const ending = session.end();
				if (ending && typeof ending.catch === "function") ending.catch(function () {});
			} catch (_) {}
		}
	}

	function onNativeWebGPUFrame(generation, time, frame) {
		if (generation !== activeSessionGeneration || !xrSession) return;
		try {
			// Queue first so a synchronous engine exception cannot silently stall
			// the compositor. The token makes this callback inert after cleanup.
			xrSession.requestAnimationFrame(function (nextTime, nextFrame) {
				onNativeWebGPUFrame(generation, nextTime, nextFrame);
			});
			window.surrealXRFrameCount++;
			window.surrealXRNativeDiagnostics.frameCount++;
			const inputs = collectInputSnapshot(frame, xrRefSpace);
			updateInteractionSnapshot(generation, inputs, time);
			if (generation !== activeSessionGeneration || !xrSession) return;
			window.surrealXRNativeDiagnostics.inputSourceCount = inputs.length;
			const rendered = window.surrealXRRenderWebGPUFrame(
				time, frame, xrRefSpace, nativeBinding, nativeProjectionLayer,
				{ resetGeneration: nativeResetGeneration, inputs: inputs });
			window.surrealXRNativeDiagnostics.lastRenderSucceeded = rendered;
			if (!rendered) {
				const nativeError = typeof window.surrealGetWebXRFrameLastError === "function" ?
					window.surrealGetWebXRFrameLastError() : 0;
				if (nativeError !== 0) {
					throw new Error("native WebXR frame bridge rejected the frame (error=" +
						nativeError + ")");
				}
				// getViewerPose() may legitimately return null for a frame. No engine
				// phase ran in that case, so leave simulation untouched and try again.
				window.surrealXRNativeDiagnostics.skippedFrames++;
			}
			flushHaptics(generation, time);
		} catch (e) {
			failNativeSession(generation, e);
		}
	}

	async function requestPreferredReferenceSpace(session) {
		try {
			return { space: await session.requestReferenceSpace("local-floor"), type: "local-floor" };
		} catch (floorError) {
			xrLog("local-floor unavailable, falling back to local: " + floorError);
			return { space: await session.requestReferenceSpace("local"), type: "local" };
		}
	}

	function makeSessionInit(requiredFeatures, optionalFeatures) {
		const required = Array.from(requiredFeatures || []);
		const optional = Array.from(optionalFeatures || []);
		const root = document.getElementById("webxr-headset-controls");
		const init = { requiredFeatures: required, optionalFeatures: optional };
		if (root) {
			if (!optional.includes("dom-overlay")) optional.push("dom-overlay");
			init.domOverlay = { root: root };
		}
		return init;
	}

	// Explicitly opt-in production path. Unlike the default IWER lifecycle
	// harness below, this requires a browser-native XRGPUBinding and never falls
	// back to WebGL or reports emulated success.
	window.surrealXREnterNativeWebGPU = async function () {
		if (xrSession || xrEnterPending) {
			xrLog("XR session entry already active/pending; ignoring duplicate request");
			return false;
		}
		if (pageShuttingDown || xrDeviceLost) {
			window.surrealXRNativeError = pageShuttingDown ?
				"page shutdown has begun" : "WebGPU device has been lost";
			nativeDiagnostic("error", { error: window.surrealXRNativeError });
			xrLog("native WebGPU XR entry rejected: " + window.surrealXRNativeError);
			return false;
		}
		window.surrealXRLifecycle.entryAttempts++;
		watchGPUDeviceLoss();
		window.surrealXRNativeError = null;
		window.surrealXRError = null;
		if (globalThis.isSecureContext !== true) {
			window.surrealXRNativeError = "WebXR requires trusted HTTPS (a plain HTTP LAN address is not a secure context)";
			nativeDiagnostic("error", { error: window.surrealXRNativeError });
			xrLog(window.surrealXRNativeError);
			return false;
		}
		if (!navigator.xr) {
			window.surrealXRNativeError = "navigator.xr is unavailable";
			nativeDiagnostic("error", { error: window.surrealXRNativeError });
			return false;
		}
		if (window.surrealXRSupported !== true) {
			window.surrealXRNativeError = "immersive-vr is unavailable or its capability check has not completed";
			nativeDiagnostic("error", { error: window.surrealXRNativeError });
			xrLog(window.surrealXRNativeError);
			return false;
		}
		if (typeof globalThis.XRGPUBinding !== "function") {
			window.surrealXRNativeError = "XRGPUBinding is not exposed by this browser";
			nativeDiagnostic("error", { error: window.surrealXRNativeError });
			xrLog(window.surrealXRNativeError);
			return false;
		}
		if (!window.surrealWebGPUDevice || typeof window.surrealXRRenderWebGPUFrame !== "function" ||
			typeof window.surrealSetXRFrameLoopActive !== "function") {
			window.surrealXRNativeError = "engine WebGPU device/frame bridge is not ready";
			nativeDiagnostic("error", { error: window.surrealXRNativeError });
			xrLog(window.surrealXRNativeError);
			return false;
		}
		if (window.surrealXRGPUCompatible !== true) {
			window.surrealXRNativeError = "an XR-compatible WebGPU device is unavailable or its capability check has not completed";
			nativeDiagnostic("error", { error: window.surrealXRNativeError });
			xrLog(window.surrealXRNativeError);
			return false;
		}

		xrEnterPending = true;
		const generation = ++sessionGeneration;
		activeSessionGeneration = generation;
		activeSessionKind = "native";
		resetInputSourceTracking(generation);
		nativeDiagnostic("requesting-session", {
			generation: generation, frameCount: 0, skippedFrames: 0,
			lastRenderSucceeded: null, referenceSpaceType: null,
			projectionFormat: null, engineLoopOwned: false, error: null,
			poseReset: null, inputSourceCount: 0,
		});
		try {
			const session = await navigator.xr.requestSession("immersive-vr",
				makeSessionInit(["webgpu"], ["local-floor"]));
			if (generation !== activeSessionGeneration) {
				await session.end();
				return false;
			}
			xrSession = session;
			beginInputSourceTracking(session, generation);
			session.addEventListener("end", function () {
				finishNativeSession(generation, "ended", null);
			});
			session.addEventListener("visibilitychange", function () {
				applySessionVisibilityPolicy(generation, session.visibilityState,
					"xr-visibilitychange");
			});

			nativeDiagnostic("creating-binding");
			nativeBinding = new XRGPUBinding(session, window.surrealWebGPUDevice);
			const projectionFormat = nativeBinding.getPreferredColorFormat();
			if (projectionFormat !== "rgba8unorm" && projectionFormat !== "bgra8unorm" &&
				projectionFormat !== "rgba16float") {
				throw new Error("unsupported XR projection color format: " + projectionFormat);
			}
			nativeProjectionLayer = nativeBinding.createProjectionLayer({
				colorFormat: projectionFormat,
				scaleFactor: 1,
			});
			await session.updateRenderState({ layers: [nativeProjectionLayer] });
			if (generation !== activeSessionGeneration) return false;
			const reference = await requestPreferredReferenceSpace(session);
			if (generation !== activeSessionGeneration) return false;
			xrRefSpace = reference.space;
			nativeResetGeneration = 0;
			if (xrRefSpace && typeof xrRefSpace.addEventListener === "function") {
				xrRefSpace.addEventListener("reset", function () {
					if (generation === activeSessionGeneration) nativeResetGeneration++;
				});
			}

			// Ownership transfers only after the session, layer and reference space
			// are all ready. A rejection before here leaves the canvas RAF untouched.
			const poseReset = resetNativePoseState();
			nativeDiagnostic("transferring-frame-loop", { poseReset: poseReset });
			if (window.surrealSetXRFrameLoopActive(true) !== 1) {
				throw new Error("engine rejected XR frame-loop ownership");
			}
			nativeOwnsEngineLoop = true;
			window.surrealXRSessionActive = true;
			window.surrealXRLifecycle.sessionsStarted++;
			window.surrealXRFrameCount = 0;
			xrEnterPending = false;
			beginInteractionSession(generation, session);
			nativeDiagnostic("running", {
				referenceSpaceType: reference.type,
				projectionFormat: projectionFormat,
				engineLoopOwned: true,
			});
			xrLog("native WebGPU XR session running (generation=" + generation +
				", referenceSpace=" + reference.type + ")");
			applySessionVisibilityPolicy(generation, session.visibilityState || "visible",
				"xr-session-start");
			session.requestAnimationFrame(function (time, frame) {
				onNativeWebGPUFrame(generation, time, frame);
			});
			return true;
		} catch (e) {
			failNativeSession(generation, e);
			return false;
		}
	};

	function onXRFrame(generation, time, frame) {
		if (generation !== activeSessionGeneration || activeSessionKind !== "default" || !xrSession) return;
		window.surrealXRFrameCount++;
		const inputs = collectInputSnapshot(frame, xrRefSpace);
		updateInteractionSnapshot(generation, inputs, time);
		if (generation !== activeSessionGeneration || !xrSession) return;
		const pose = frame.getViewerPose(xrRefSpace);
		if (pose && window.surrealXRFrameCount % 30 === 1) {
			xrLog("frame " + window.surrealXRFrameCount + ": " + pose.views.length + " view(s)");
		}
		flushHaptics(generation, time);
		xrSession.requestAnimationFrame(function (nextTime, nextFrame) {
			onXRFrame(generation, nextTime, nextFrame);
		});
	}

	window.surrealXREnter = async function (mode) {
		mode = mode || "immersive-vr";
		if (xrSession || xrEnterPending) {
			xrLog("already in a session, ignoring surrealXREnter()");
			return;
		}
		if (pageShuttingDown || xrDeviceLost) {
			window.surrealXRError = pageShuttingDown ?
				"page shutdown has begun" : "WebGPU device has been lost";
			xrLog("XR entry rejected: " + window.surrealXRError);
			return false;
		}
		if (globalThis.isSecureContext !== true) {
			window.surrealXRError = "WebXR requires trusted HTTPS (a plain HTTP LAN address is not a secure context)";
			xrLog(window.surrealXRError);
			return false;
		}
		if (!navigator.xr) {
			window.surrealXRError = "no navigator.xr";
			xrLog(window.surrealXRError);
			return;
		}
		window.surrealXRLifecycle.entryAttempts++;
		watchGPUDeviceLoss();
		xrEnterPending = true;
		const generation = ++sessionGeneration;
		activeSessionGeneration = generation;
		activeSessionKind = "default";
		resetInputSourceTracking(generation);
		try {
			const session = await navigator.xr.requestSession(mode,
				makeSessionInit([], ["local-floor", "bounded-floor"]));
			if (generation !== activeSessionGeneration || pageShuttingDown || xrDeviceLost) {
				try { await session.end(); } catch (_) {}
				return false;
			}
			xrSession = session;
			beginInputSourceTracking(session, generation);

			session.addEventListener("end", function () {
				finishDefaultSession(generation, "session-end", null);
			});
			session.addEventListener("visibilitychange", function () {
				applySessionVisibilityPolicy(generation, session.visibilityState,
					"xr-visibilitychange");
			});

			// Throwaway offscreen WebGL2 layer purely to satisfy
			// updateRenderState()/frame production — see file header. Not the
			// engine's WebGPU canvas; never rendered into beyond the GL clear
			// implied by context creation.
			const sessionCanvas = document.createElement("canvas");
			xrCanvas = sessionCanvas;
			const gl = sessionCanvas.getContext("webgl2", { xrCompatible: true });
			if (gl.makeXRCompatible) {
				await gl.makeXRCompatible();
			}
			const baseLayer = new XRWebGLLayer(session, gl);
			await session.updateRenderState({ baseLayer: baseLayer });
			xrLog("baseLayer attached (" + baseLayer.framebufferWidth + "x" + baseLayer.framebufferHeight + ")");

			xrRefSpace = await session.requestReferenceSpace("local");
			if (generation !== activeSessionGeneration) return false;
			window.surrealXRSessionActive = true;
			window.surrealXRLifecycle.sessionsStarted++;
			window.surrealXRFrameCount = 0;
			beginInteractionSession(generation, session);
			xrLog("session started (mode=" + mode + ")");
			applySessionVisibilityPolicy(generation, session.visibilityState || "visible",
				"xr-session-start");
			session.requestAnimationFrame(function (time, frame) {
				onXRFrame(generation, time, frame);
			});
			return true;
		} catch (e) {
			if (generation === activeSessionGeneration) {
				abortActiveSession("entry-error", e);
			}
			return false;
		} finally {
			if (generation === activeSessionGeneration || !activeSessionGeneration) {
				xrEnterPending = false;
			}
		}
	};

	window.surrealXRExit = function (reason) {
		if (!xrSession || !activeSessionGeneration) return false;
		const generation = activeSessionGeneration;
		if (exitEndPendingGeneration === generation) return true;
		exitEndPendingGeneration = generation;
		requestedExitGeneration = generation;
		requestedExitReason = String(reason || "explicit-exit");
		window.surrealXRLifecycle.lastExitReason = requestedExitReason;
		if (interactionStatus.generation === generation) {
			interactionStatus.exitPhase = "ending";
			if (!interactionStatus.exitSource) interactionStatus.exitSource = "direct-api";
			notifyInteractionChanged();
		}
		try {
			const ending = xrSession.end();
			if (ending && typeof ending.catch === "function") {
				ending.catch(function (error) {
					if (generation === activeSessionGeneration) {
						abortActiveSession("exit-error", error);
					}
				});
			}
			return true;
		} catch (e) {
			abortActiveSession("exit-error", e);
			return false;
		}
	};

	window.surrealXRBindInteractionUI();

	document.addEventListener("visibilitychange", function () {
		applyVisibilityPolicy(document.hidden, "visibilitychange");
	});
	window.addEventListener("pagehide", function () {
		window.surrealXRShutdown("pagehide");
	});
	window.addEventListener("beforeunload", function () {
		window.surrealXRShutdown("beforeunload");
	});
})();
