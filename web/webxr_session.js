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
	// M6 packed-frame ABI v1. The originally proposed 112-byte view stride is
	// arithmetically impossible: the projection matrix starts at byte 52 and
	// contains 16 float32 values, ending at byte 116. The header has no trailing
	// padding, so the first view starts at byte 36. Keep every size explicit so
	// JS and C++ can share compile-time/runtime offset checks.
	const XR_FRAME_ABI = Object.freeze({
		version: 1,
		headerBytes: 36,
		viewBytes: 116,
		maxViews: 2,
		maxPacketBytes: 268,
		eye: Object.freeze({ none: 0, left: 1, right: 2 }),
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

	// Packs plain values only. The returned ArrayBuffer owns no XR objects and
	// may safely outlive the XRFrame callback that produced the values.
	window.surrealXRPackFrame = function (frame) {
		frame = frame || {};
		const views = Array.from(frame.views || []);
		if (views.length > XR_FRAME_ABI.maxViews) {
			throw new RangeError("XR frame ABI v1 supports at most " + XR_FRAME_ABI.maxViews + " views");
		}
		const byteSize = XR_FRAME_ABI.headerBytes + views.length * XR_FRAME_ABI.viewBytes;
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
		views.forEach((view, index) => writeView(
			view, data, XR_FRAME_ABI.headerBytes + index * XR_FRAME_ABI.viewBytes, index));
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
		});
		const data = new DataView(buffer);
		const near = (actual, expected) => Math.abs(actual - expected) < 0.00001;
		const checks = [
			data.getUint32(0, true) === 1, data.getUint32(4, true) === 268,
			data.getUint32(8, true) === 2, data.getUint32(12, true) === 0xa5,
			data.getFloat64(16, true) === 1234.5, data.getUint32(24, true) === 7,
			data.getUint32(28, true) === 2048, data.getUint32(32, true) === 1024,
			data.getUint32(36, true) === 1, data.getUint32(40, true) === 3,
			data.getInt32(44, true) === 11, near(data.getFloat32(60, true), 1.25),
			near(data.getFloat32(88, true), 1), data.getUint32(152, true) === 2,
			data.getUint32(156, true) === 4, data.getInt32(160, true) === 21,
			near(data.getFloat32(200, true), 0.4), near(data.getFloat32(264, true), 16),
		];
		return {
			passed: checks.every(Boolean),
			checksPassed: checks.filter(Boolean).length,
			checkCount: checks.length,
			byteSize: buffer.byteLength,
			headerBytes: XR_FRAME_ABI.headerBytes,
			viewBytes: XR_FRAME_ABI.viewBytes,
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
		error: null,
	};
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
	let audioSuspendedByLifecycle = false;
	let pageShuttingDown = false;
	let xrDeviceLost = false;
	let watchedGPUDevice = null;

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
			return false;
		}
		try {
			const ok = await navigator.xr.isSessionSupported("immersive-vr");
			window.surrealXRSupported = ok;
			xrLog("isSessionSupported('immersive-vr') = " + ok);
			return ok;
		} catch (e) {
			window.surrealXRError = String(e);
			xrLog("isSessionSupported threw: " + e);
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
			return false;
		}
		try {
			const adapter = await navigator.gpu.requestAdapter({ xrCompatible: true });
			if (!adapter) {
				window.surrealXRGPUCompatible = false;
				xrLog("requestAdapter({xrCompatible:true}) resolved null");
				return false;
			}
			const device = await adapter.requestDevice();
			window.surrealXRGPUCompatible = true;
			xrLog("xrCompatible GPUAdapter/GPUDevice acquired OK (adapter info: " +
				JSON.stringify(adapter.info || {}) + ")");
			device.destroy();
			return true;
		} catch (e) {
			window.surrealXRGPUCompatible = false;
			window.surrealXRError = String(e);
			xrLog("xrCompatible GPUDevice probe failed: " + e);
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

	function finishNativeSession(generation, phase, error) {
		if (generation !== activeSessionGeneration) return;
		const wasActive = window.surrealXRSessionActive;
		restoreCanvasFrameLoop(generation);
		activeSessionGeneration = 0; // invalidates every queued callback
		activeSessionKind = null;
		xrSession = null;
		xrRefSpace = null;
		nativeBinding = null;
		nativeProjectionLayer = null;
		nativeResetGeneration = 0;
		resetNativePoseState();
		xrEnterPending = false;
		window.surrealXRSessionActive = false;
		if (wasActive) noteSessionEnded(error ? "native-error" : (phase || "ended"));
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
		activeSessionGeneration = 0;
		activeSessionKind = null;
		xrSession = null;
		xrRefSpace = null;
		xrEnterPending = false;
		window.surrealXRSessionActive = false;
		removeXRCanvas();
		if (error) {
			window.surrealXRError = String(error);
			xrLog("XR session failed: " + error);
		}
		if (wasActive) noteSessionEnded(reason || "ended");
		xrLog("session ended (reason=" + (reason || "ended") + ")");
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
			const rendered = window.surrealXRRenderWebGPUFrame(
				time, frame, xrRefSpace, nativeBinding, nativeProjectionLayer,
				{ resetGeneration: nativeResetGeneration });
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
		if (!navigator.xr) {
			window.surrealXRNativeError = "navigator.xr is unavailable";
			nativeDiagnostic("error", { error: window.surrealXRNativeError });
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

		xrEnterPending = true;
		const generation = ++sessionGeneration;
		activeSessionGeneration = generation;
		activeSessionKind = "native";
		nativeDiagnostic("requesting-session", {
			generation: generation, frameCount: 0, skippedFrames: 0,
			lastRenderSucceeded: null, referenceSpaceType: null,
			projectionFormat: null, engineLoopOwned: false, error: null,
			poseReset: null,
		});
		try {
			const session = await navigator.xr.requestSession("immersive-vr", {
				requiredFeatures: ["webgpu"],
				optionalFeatures: ["local-floor"],
			});
			if (generation !== activeSessionGeneration) {
				await session.end();
				return false;
			}
			xrSession = session;
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
		const pose = frame.getViewerPose(xrRefSpace);
		if (pose && window.surrealXRFrameCount % 30 === 1) {
			xrLog("frame " + window.surrealXRFrameCount + ": " + pose.views.length + " view(s)");
		}
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
		try {
			const session = await navigator.xr.requestSession(mode, {
				optionalFeatures: ["local-floor", "bounded-floor"],
			});
			if (generation !== activeSessionGeneration || pageShuttingDown || xrDeviceLost) {
				try { await session.end(); } catch (_) {}
				return false;
			}
			xrSession = session;

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

	window.surrealXRExit = function () {
		if (!xrSession) return false;
		const generation = activeSessionGeneration;
		window.surrealXRLifecycle.lastExitReason = "explicit-exit";
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
