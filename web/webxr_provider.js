(function (root) {
	"use strict";

	const ABI = Object.freeze({ version: 2, headerBytes: 32, viewBytes: 128, maxViews: 2 });
	const INPUT_ABI = Object.freeze({
		version: 1, headerBytes: 24, sourceBytes: 112, maxSources: 2,
		buttons: Object.freeze({ trigger: 0, squeeze: 1, primary: 2, secondary: 3, menu: 4, stickClick: 5 })
	});
	const INPUT_HEADER_FLAGS = Object.freeze({ sessionActive: 1, actionFocused: 2 });
	const INPUT_SOURCE_FLAGS = Object.freeze({ connected: 1, aimValid: 2, gripValid: 4 });
	let generationCounter = 0;
	let activeGeneration = 0;
	let enterPending = false;
	let session = null;
	let referenceSpace = null;
	let binding = null;
	let projectionLayer = null;
	let resetGeneration = 0;
	let animationFrameHandle = null;
	let engineLoopOwned = false;

	const status = {
		phase: "idle",
		active: false,
		generation: 0,
		frames: 0,
		skippedFrames: 0,
		inputPackets: 0,
		lastError: null,
		lastErrorCode: null,
		lastErrorStage: null,
		referenceSpaceType: null,
		projectionFormat: null,
		capabilities: null,
	};

	class WebXRProviderError extends Error {
		constructor(code, stage, message) {
			super(message);
			this.name = "WebXRProviderError";
			this.code = code;
			this.stage = stage;
		}
	}

	function providerError(code, stage, message) {
		return new WebXRProviderError(code, stage, message);
	}

	function log(message) {
		if (typeof root.surrealXRLog === "function") root.surrealXRLog(message);
	}

	function moduleCall(name, returnType, argumentTypes, args) {
		if (!root.Module || typeof root.Module.ccall !== "function")
			throw new Error("SurrealEngine WebAssembly runtime is not ready");
		return root.Module.ccall(name, returnType, argumentTypes || [], args || []);
	}

	function webGPUDevice() {
		return root.surrealWebGPUDevice ||
			(root.Module && root.Module.preinitializedWebGPUDevice) || null;
	}

	function setEngineLoop(active) {
		return moduleCall("Surreal_SetXRFrameLoopActive", "number", ["number"], [active ? 1 : 0]) === 1;
	}

	function resetNativePose() {
		try { moduleCall("Surreal_ResetWebXRPose", null); } catch (_) {}
	}

	function writeArray(view, offset, values) {
		for (let index = 0; index < values.length; index++)
			view.setFloat32(offset + index * 4, values[index], true);
	}

	function finiteClamped(value, minimum, maximum) {
		const number = Number(value);
		return Number.isFinite(number) ? Math.max(minimum, Math.min(maximum, number)) : 0;
	}

	function writePose(data, offset, pose) {
		if (!pose || !pose.transform) return false;
		const position = pose.transform.position;
		const orientation = pose.transform.orientation;
		const values = [position.x, position.y, position.z, orientation.x, orientation.y, orientation.z, orientation.w];
		if (!values.every(Number.isFinite)) return false;
		writeArray(data, offset, values.slice(0, 3));
		writeArray(data, offset + 12, values.slice(3));
		return true;
	}

	function isActionFocused(currentSession) {
		return !currentSession || currentSession.visibilityState === undefined || currentSession.visibilityState === "visible";
	}

	function semanticGamepad(inputSource, actionFocused) {
		const neutral = { pressed: 0, touched: 0, values: new Array(6).fill(0), axes: new Array(4).fill(0) };
		const gamepad = inputSource && inputSource.gamepad;
		if (!actionFocused || !gamepad || gamepad.mapping !== "xr-standard") return neutral;
		// xr-standard normatively defines only trigger, squeeze, touchpad, and
		// thumbstick slots. Additional buttons are profile-specific; never infer
		// A/B/menu semantics merely because an array happens to be long enough.
		const semanticIndices = [[0, INPUT_ABI.buttons.trigger], [1, INPUT_ABI.buttons.squeeze],
			[3, INPUT_ABI.buttons.stickClick]];
		const profiles = Array.from(inputSource.profiles || []);
		if (profiles.some(profile => /^oculus-touch(?:-|$)/.test(profile))) {
			semanticIndices.push([4, INPUT_ABI.buttons.primary], [5, INPUT_ABI.buttons.secondary]);
		}
		semanticIndices.forEach(function (mapping) {
			const gamepadIndex = mapping[0];
			const semanticIndex = mapping[1];
			const button = gamepad.buttons && gamepad.buttons[gamepadIndex];
			if (!button) return;
			neutral.values[semanticIndex] = finiteClamped(button.value, 0, 1);
			if (button.pressed === true) neutral.pressed |= 1 << semanticIndex;
			if (button.touched === true) neutral.touched |= 1 << semanticIndex;
		});
		for (let index = 0; index < neutral.axes.length; index++) {
			neutral.axes[index] = finiteClamped(gamepad.axes && gamepad.axes[index], -1, 1);
		}
		return neutral;
	}

	function packInputSnapshot(time, currentSession, frame, currentReferenceSpace) {
		const byHand = new Map();
		for (const source of Array.from(currentSession && currentSession.inputSources || [])) {
			if ((source.handedness === "left" || source.handedness === "right") && !byHand.has(source.handedness))
				byHand.set(source.handedness, source);
		}
		const sources = [byHand.get("left"), byHand.get("right")].filter(Boolean).slice(0, INPUT_ABI.maxSources);
		const packet = new Uint8Array(INPUT_ABI.headerBytes + sources.length * INPUT_ABI.sourceBytes);
		const data = new DataView(packet.buffer);
		const actionFocused = isActionFocused(currentSession);
		data.setUint32(0, INPUT_ABI.version, true);
		data.setUint32(4, packet.byteLength, true);
		data.setUint32(8, sources.length, true);
		data.setUint32(12, INPUT_HEADER_FLAGS.sessionActive | (actionFocused ? INPUT_HEADER_FLAGS.actionFocused : 0), true);
		data.setFloat64(16, Number.isFinite(time) ? time : 0, true);
		sources.forEach(function (source, index) {
			const base = INPUT_ABI.headerBytes + index * INPUT_ABI.sourceBytes;
			let flags = INPUT_SOURCE_FLAGS.connected;
			let aimPose = null;
			let gripPose = null;
			try {
				aimPose = source.targetRaySpace && frame && typeof frame.getPose === "function" ? frame.getPose(source.targetRaySpace, currentReferenceSpace) : null;
				gripPose = source.gripSpace && frame && typeof frame.getPose === "function" ? frame.getPose(source.gripSpace, currentReferenceSpace) : null;
			} catch (_) { /* A lost space produces a connected controller with invalid poses. */ }
			data.setUint32(base, source.handedness === "left" ? 1 : 2, true);
			const gamepad = semanticGamepad(source, actionFocused);
			data.setUint32(base + 8, gamepad.pressed, true);
			data.setUint32(base + 12, gamepad.touched, true);
			writeArray(data, base + 16, gamepad.values);
			writeArray(data, base + 40, gamepad.axes);
			if (writePose(data, base + 56, aimPose)) flags |= INPUT_SOURCE_FLAGS.aimValid;
			if (writePose(data, base + 84, gripPose)) flags |= INPUT_SOURCE_FLAGS.gripValid;
			data.setUint32(base + 4, flags, true);
		});
		return packet;
	}

	function submitInputPacket(packet) {
		const accepted = moduleCall("Surreal_SubmitWebXRInputSnapshot", "number", ["array", "number"],
			[packet, packet.byteLength]);
		if (accepted !== 1) {
			const error = moduleCall("Surreal_GetWebXRInputLastError", "number");
			throw new Error("native WebXR input bridge rejected the snapshot (error=" + error + ")");
		}
		if (moduleCall("Surreal_ApplyWebXRInputSnapshot", "number") !== 1)
			throw new Error("native WebXR input runtime rejected the stored snapshot");
		status.inputPackets++;
	}

	function submitCurrentInput(time) {
		submitInputPacket(packInputSnapshot(time, session, null, referenceSpace));
	}

	function submitNeutralInput(time, sessionActive) {
		const packet = new Uint8Array(INPUT_ABI.headerBytes);
		const data = new DataView(packet.buffer);
		data.setUint32(0, INPUT_ABI.version, true);
		data.setUint32(4, packet.byteLength, true);
		data.setUint32(12, sessionActive ? INPUT_HEADER_FLAGS.sessionActive : 0, true);
		data.setFloat64(16, Number.isFinite(time) ? time : 0, true);
		try { submitInputPacket(packet); } catch (_) {
			try { moduleCall("Surreal_ClearWebXRInputSnapshot", null); } catch (_) {}
			try { moduleCall("Surreal_ApplyWebXRInputSnapshot", "number"); } catch (_) {}
		}
	}

	function frameViewData(view, image, textures) {
		if (!image || !image.colorTexture || typeof image.getViewDescriptor !== "function")
			throw providerError("invalid-projection-subimage", "frame", "WebXR returned an incomplete projection subimage");
		const texture = image.colorTexture;
		const descriptor = image.getViewDescriptor();
		const width = texture.width;
		const height = texture.height;
		const arrayLayer = descriptor && descriptor.baseArrayLayer !== undefined ?
			descriptor.baseArrayLayer : 0;
		if (!Number.isInteger(width) || width <= 0 || !Number.isInteger(height) || height <= 0 ||
			!Number.isInteger(arrayLayer) || arrayLayer < 0 ||
			(descriptor && descriptor.arrayLayerCount !== undefined && descriptor.arrayLayerCount !== 1))
			throw providerError("invalid-projection-subimage", "frame", "WebXR returned invalid projection texture metadata");
		let textureIndex = textures.indexOf(texture);
		if (textureIndex < 0) {
			textureIndex = textures.length;
			textures.push(texture);
		}
		return { view, image, textureIndex, arrayLayer, width, height };
	}

	function packFrame(time, pose, currentBinding, layer, currentResetGeneration) {
		const views = Array.from(pose.views);
		if (views.length !== ABI.maxViews || views[0].eye === views[1].eye ||
			!views.some(view => view.eye === "left") || !views.some(view => view.eye === "right"))
			throw providerError("unsupported-view-configuration", "frame",
				"WebXR immersive VR requires exactly one left and one right primary view");
		const textures = [];
		const packedViews = views.map(function (view) {
			return frameViewData(view, currentBinding.getViewSubImage(layer, view), textures);
		});
		const packet = new Uint8Array(ABI.headerBytes + views.length * ABI.viewBytes);
		const data = new DataView(packet.buffer);
		data.setUint32(0, ABI.version, true);
		data.setUint32(4, packet.byteLength, true);
		data.setUint32(8, views.length, true);
		data.setUint32(12, textures.length, true);
		data.setFloat64(16, time, true);
		data.setUint32(24, currentResetGeneration, true);
		data.setUint32(28, 0, true);

		packedViews.forEach(function (packedView, index) {
			const view = packedView.view;
			const viewport = packedView.image.viewport ||
				{ x: 0, y: 0, width: packedView.width, height: packedView.height };
			const transform = view.transform;
			const base = ABI.headerBytes + index * ABI.viewBytes;
			data.setUint32(base, view.eye === "left" ? 1 : (view.eye === "right" ? 2 : 0), true);
			data.setUint32(base + 4, packedView.textureIndex, true);
			data.setUint32(base + 8, packedView.arrayLayer, true);
			data.setUint32(base + 12, packedView.width, true);
			data.setUint32(base + 16, packedView.height, true);
			data.setInt32(base + 20, viewport.x, true);
			data.setInt32(base + 24, viewport.y, true);
			data.setInt32(base + 28, viewport.width, true);
			data.setInt32(base + 32, viewport.height, true);
			writeArray(data, base + 36, [transform.position.x, transform.position.y, transform.position.z]);
			writeArray(data, base + 48, [transform.orientation.x, transform.orientation.y,
				transform.orientation.z, transform.orientation.w]);
			writeArray(data, base + 64, view.projectionMatrix);
		});
		return { packet, textures };
	}

	function renderFrame(time, frame) {
		submitInputPacket(packInputSnapshot(time, session, frame, referenceSpace));
		const pose = frame.getViewerPose(referenceSpace);
		if (!pose) return false;
		const packed = packFrame(time, pose, binding, projectionLayer, resetGeneration);
		if (!packed) return false;
		root.surrealWebXRFrameTextures = packed.textures;
		try {
			const rendered = moduleCall("Surreal_RenderWebXRFrame", "number", ["array", "number"],
				[packed.packet, packed.packet.byteLength]);
			if (rendered !== 1) {
				const error = moduleCall("Surreal_GetWebXRFrameLastError", "number");
				throw providerError("native-frame-rejected-" + error, "frame",
					"native WebXR frame bridge rejected the frame (error=" + error + ")");
			}
			return true;
		} finally {
			root.surrealWebXRFrameTextures = null;
		}
	}

	function finish(generation, phase, error) {
		if (generation !== activeGeneration) return false;
		submitNeutralInput(0, false);
		const finishedSession = session;
		const pendingFrame = animationFrameHandle;
		const errorStage = error ? (error.stage || status.phase) : null;
		activeGeneration = 0;
		enterPending = false;
		animationFrameHandle = null;
		if (finishedSession && pendingFrame !== null &&
			typeof finishedSession.cancelAnimationFrame === "function") {
			try { finishedSession.cancelAnimationFrame(pendingFrame); } catch (_) {}
		}
		session = null;
		referenceSpace = null;
		binding = null;
		projectionLayer = null;
		root.surrealWebXRFrameTextures = null;
		if (engineLoopOwned) {
			try { setEngineLoop(false); } catch (_) {}
			engineLoopOwned = false;
		}
		resetNativePose();
		status.phase = phase || "ended";
		status.active = false;
		status.lastError = error ? String(error.message || error) : null;
		status.lastErrorCode = error ? (error.code || "webxr-provider-failed") : null;
		status.lastErrorStage = errorStage;
		if (error) log("WebXR session failed: " + error);
		return true;
	}

	function fail(generation, error) {
		const failedSession = generation === activeGeneration ? session : null;
		finish(generation, "error", error);
		if (failedSession) {
			try {
				const ending = failedSession.end();
				if (ending && typeof ending.catch === "function") ending.catch(function () {});
			} catch (_) {}
		}
	}

	function onFrame(generation, time, frame) {
		if (generation !== activeGeneration || !session) return;
		try {
			animationFrameHandle = session.requestAnimationFrame(function (nextTime, nextFrame) {
				animationFrameHandle = null;
				onFrame(generation, nextTime, nextFrame);
			});
			if (renderFrame(time, frame)) status.frames++;
			else status.skippedFrames++;
		} catch (error) {
			fail(generation, error instanceof WebXRProviderError ? error :
				providerError("frame-failed", "frame", error.message || String(error)));
		}
	}

	async function requestReference(sessionObject) {
		try {
			return { space: await sessionObject.requestReferenceSpace("local-floor"), type: "local-floor" };
		} catch (_) {
			return { space: await sessionObject.requestReferenceSpace("local"), type: "local" };
		}
	}

	root.surrealXRGetCapabilities = async function () {
		const hasCurrentBinding = typeof root.XRGPUBinding === "function";
		const hasLegacyBinding = typeof root.XRWebGPUBinding === "function";
		const result = {
			secureContext: root.isSecureContext !== false,
			webXR: !!(root.navigator && root.navigator.xr),
			immersiveVR: false,
			webGPUBinding: hasCurrentBinding,
			webGPUBindingName: hasCurrentBinding ? "XRGPUBinding" :
				(hasLegacyBinding ? "XRWebGPUBinding (obsolete)" : null),
			legacyWebGPUBinding: hasLegacyBinding,
			webGPUDevice: !!webGPUDevice(),
			webGPUDeviceXRCompatible: root.surrealWebGPUDeviceXRCompatible === true,
			supported: false,
			reasons: [],
		};
		if (!result.secureContext) result.reasons.push("secure-context-required");
		if (!result.webXR) result.reasons.push("webxr-unavailable");
		if (!result.webGPUBinding)
			result.reasons.push(hasLegacyBinding ? "obsolete-webgpu-binding-api" : "webxr-webgpu-binding-unavailable");
		if (!result.webGPUDevice) result.reasons.push("webgpu-device-not-ready");
		else if (!result.webGPUDeviceXRCompatible) result.reasons.push("webgpu-device-not-xr-compatible");
		if (result.webXR && typeof root.navigator.xr.isSessionSupported === "function") {
			try { result.immersiveVR = await root.navigator.xr.isSessionSupported("immersive-vr"); }
			catch (_) { result.reasons.push("immersive-vr-query-failed"); }
		}
		if (!result.immersiveVR && !result.reasons.includes("immersive-vr-query-failed"))
			result.reasons.push("immersive-vr-unavailable");
		result.supported = result.secureContext && result.webXR && result.immersiveVR &&
			result.webGPUBinding && result.webGPUDevice && result.webGPUDeviceXRCompatible;
		status.capabilities = Object.assign({}, result, { reasons: result.reasons.slice() });
		return Object.assign({}, result, { reasons: result.reasons.slice() });
	};

	root.surrealXRIsSupported = async function () {
		return (await root.surrealXRGetCapabilities()).supported;
	};

	root.surrealXRIsColorFormatSupported = function (format) {
		return format === "bgra8unorm" || format === "rgba8unorm" || format === "rgba16float";
	};

	root.surrealXREnter = async function () {
		if (session || enterPending) return false;
		let preflightError = null;
		if (root.isSecureContext === false)
			preflightError = providerError("secure-context-required", "preflight", "WebXR requires a secure context");
		else if (!root.navigator || !root.navigator.xr)
			preflightError = providerError("webxr-unavailable", "preflight", "WebXR is unavailable");
		else if (typeof root.XRGPUBinding !== "function")
			preflightError = providerError("webxr-webgpu-binding-unavailable", "preflight",
				"XRGPUBinding is unavailable; this browser does not expose the draft WebXR/WebGPU binding");
		else if (!webGPUDevice())
			preflightError = providerError("webgpu-device-not-ready", "preflight",
				"SurrealEngine WebGPU device is not ready");
		else if (root.surrealWebGPUDeviceXRCompatible !== true)
			preflightError = providerError("webgpu-device-not-xr-compatible", "preflight",
				"SurrealEngine WebGPU device was not acquired from an XR-compatible adapter");
		if (preflightError) {
			status.phase = "error";
			status.lastError = preflightError.message;
			status.lastErrorCode = preflightError.code;
			status.lastErrorStage = preflightError.stage;
			log(preflightError.message);
			return false;
		}

		enterPending = true;
		const generation = ++generationCounter;
		activeGeneration = generation;
		status.phase = "requesting-session";
		status.generation = generation;
		status.frames = 0;
		status.skippedFrames = 0;
		status.inputPackets = 0;
		status.lastError = null;
		status.lastErrorCode = null;
		status.lastErrorStage = null;
		status.referenceSpaceType = null;
		status.projectionFormat = null;
		let requestedSession = null;
		try {
			try {
				requestedSession = await root.navigator.xr.requestSession("immersive-vr", {
					requiredFeatures: ["webgpu"], optionalFeatures: ["local-floor"]
				});
			} catch (error) {
				throw providerError("session-request-failed", "requesting-session", error.message || String(error));
			}
			if (generation !== activeGeneration) {
				await requestedSession.end();
				return false;
			}
			session = requestedSession;
			session.addEventListener("end", function () { finish(generation, "ended", null); });
			session.addEventListener("inputsourceschange", function (event) {
				if (generation === activeGeneration && event && event.removed && event.removed.length) submitCurrentInput(0);
			});
			session.addEventListener("visibilitychange", function () {
				if (generation === activeGeneration && !isActionFocused(session)) submitCurrentInput(0);
			});
			status.phase = "creating-binding";
			try { binding = new root.XRGPUBinding(session, webGPUDevice()); }
			catch (error) {
				throw providerError("binding-creation-failed", "creating-binding", error.message || String(error));
			}
			const projectionFormat = binding.getPreferredColorFormat();
			if (!root.surrealXRIsColorFormatSupported(projectionFormat))
				throw providerError("unsupported-color-format", "creating-projection-layer",
					"unsupported WebXR projection color format: " + projectionFormat);
			status.projectionFormat = projectionFormat;
			status.phase = "creating-projection-layer";
			try {
				projectionLayer = binding.createProjectionLayer({ colorFormat: projectionFormat, scaleFactor: 1 });
				session.updateRenderState({ layers: [projectionLayer] });
			} catch (error) {
				throw providerError("projection-layer-failed", "creating-projection-layer", error.message || String(error));
			}
			status.phase = "requesting-reference-space";
			let reference;
			try { reference = await requestReference(session); }
			catch (error) {
				throw providerError("reference-space-failed", "requesting-reference-space", error.message || String(error));
			}
			if (generation !== activeGeneration) return false;
			referenceSpace = reference.space;
			status.referenceSpaceType = reference.type;
			resetGeneration = 0;
			if (referenceSpace && typeof referenceSpace.addEventListener === "function") {
				referenceSpace.addEventListener("reset", function () {
					if (generation === activeGeneration) resetGeneration++;
				});
			}
			resetNativePose();
			submitNeutralInput(0, true);
			status.phase = "starting-frame-loop";
			if (!setEngineLoop(true))
				throw providerError("engine-loop-rejected", "starting-frame-loop",
					"engine rejected WebXR frame-loop ownership");
			engineLoopOwned = true;
			enterPending = false;
			status.phase = "running";
			status.active = true;
			animationFrameHandle = session.requestAnimationFrame(function (time, frame) {
				animationFrameHandle = null;
				onFrame(generation, time, frame);
			});
			return true;
		} catch (error) {
			fail(generation, error);
			return false;
		}
	};

	root.surrealXRExit = function () {
		if (!session) return false;
		const generation = activeGeneration;
		try {
			const ending = session.end();
			if (ending && typeof ending.catch === "function") {
				ending.catch(function (error) { fail(generation, error); });
			}
			return true;
		} catch (error) {
			fail(generation, error);
			return false;
		}
	};

	root.surrealXRGetState = function () {
		const result = Object.assign({}, status);
		if (status.capabilities)
			result.capabilities = Object.assign({}, status.capabilities,
				{ reasons: status.capabilities.reasons.slice() });
		return result;
	};
	root.surrealXRFrameABI = ABI;
	root.surrealXRInputABI = INPUT_ABI;
	root.surrealXRPackInputSnapshot = packInputSnapshot;
})(typeof window !== "undefined" ? window : globalThis);
