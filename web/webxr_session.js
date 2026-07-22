// web/webxr_session.js — M4 groundwork: WebXR session-lifecycle plumbing.
//
// Ported from the sibling `webxr-port/` (QuakeQuest) project's proven
// session-request pattern (src/web-host/lib/webxr/library_webxr.js's
// onSessionStarted/requestSession flow — see PATCHES.md there for the
// upstream emscripten-webxr fixes it carries), adapted to two differences:
//
//   1. This is plain browser JS, not an Emscripten JS library (`--js-library`)
//      called from C. SurrealEngine has no WebXR-aware C++ yet — that's the
//      real M4 render-path work (WEBXR_IMPLEMENTATION_PLAN.md), out of scope
//      here. This module only proves the session can be requested and its
//      lifecycle observed/driven from JS, so IWER-based headless testing of
//      that plumbing is possible before the real stereo path exists.
//   2. QuakeQuest is WebGL2 (`XRWebGLLayer` via `GLctx.makeXRCompatible()`).
//      SurrealEngine's renderer is WebGPU (RenderDevice/WebGPU/), which per
//      the WebXR spec needs `XRGPUBinding`/`XRGPUProjectionLayer` instead of
//      `XRWebGLLayer`, once the engine is actually rendering into the
//      session. That binding is not implemented by IWER as of iwer@2.3.0
//      (confirmed by inspecting node_modules/iwer/build/iwer.js — no
//      `XRGPUBinding`/`GPUBinding` symbol anywhere in the bundle), so IWER
//      cannot emulate the WebGPU-XR handoff, only the WebGL2 one. This
//      module therefore drives the session's render state with a throwaway,
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

	let xrSession = null;
	let xrRefSpace = null;
	let xrCanvas = null; // throwaway WebGL2 canvas backing the session's baseLayer

	function xrLog(line) {
		window.surrealXRLog.push(line);
		console.log("[webxr] " + line);
	}

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

	function onXRFrame(time, frame) {
		if (!xrSession) return;
		window.surrealXRFrameCount++;
		const pose = frame.getViewerPose(xrRefSpace);
		if (pose && window.surrealXRFrameCount % 30 === 1) {
			xrLog("frame " + window.surrealXRFrameCount + ": " + pose.views.length + " view(s)");
		}
		xrSession.requestAnimationFrame(onXRFrame);
	}

	window.surrealXREnter = async function (mode) {
		mode = mode || "immersive-vr";
		if (xrSession) {
			xrLog("already in a session, ignoring surrealXREnter()");
			return;
		}
		if (!navigator.xr) {
			window.surrealXRError = "no navigator.xr";
			xrLog(window.surrealXRError);
			return;
		}
		try {
			const session = await navigator.xr.requestSession(mode, {
				optionalFeatures: ["local-floor", "bounded-floor"],
			});
			xrSession = session;
			window.surrealXRSessionActive = true;
			window.surrealXRFrameCount = 0;
			xrLog("session started (mode=" + mode + ")");

			session.addEventListener("end", function () {
				xrLog("session ended");
				xrSession = null;
				xrRefSpace = null;
				window.surrealXRSessionActive = false;
				if (xrCanvas) { xrCanvas.remove(); xrCanvas = null; }
			});

			// Throwaway offscreen WebGL2 layer purely to satisfy
			// updateRenderState()/frame production — see file header. Not the
			// engine's WebGPU canvas; never rendered into beyond the GL clear
			// implied by context creation.
			xrCanvas = document.createElement("canvas");
			const gl = xrCanvas.getContext("webgl2", { xrCompatible: true });
			if (gl.makeXRCompatible) {
				await gl.makeXRCompatible();
			}
			const baseLayer = new XRWebGLLayer(session, gl);
			await session.updateRenderState({ baseLayer: baseLayer });
			xrLog("baseLayer attached (" + baseLayer.framebufferWidth + "x" + baseLayer.framebufferHeight + ")");

			xrRefSpace = await session.requestReferenceSpace("local");
			session.requestAnimationFrame(onXRFrame);
		} catch (e) {
			window.surrealXRError = String(e);
			xrLog("requestSession failed: " + e);
		}
	};

	window.surrealXRExit = function () {
		if (xrSession) xrSession.end();
	};
})();
