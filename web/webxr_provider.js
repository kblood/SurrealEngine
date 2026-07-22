(function (root) {
	"use strict";

	const ABI = Object.freeze({ version: 1, headerBytes: 36, viewBytes: 116, maxViews: 2 });
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

	const status = {
		phase: "idle",
		active: false,
		generation: 0,
		frames: 0,
		skippedFrames: 0,
		inputPackets: 0,
		lastError: null,
		referenceSpaceType: null,
		projectionFormat: null,
	};

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

	function arrayLayerOf(subImage, fallback) {
		const descriptor = subImage && typeof subImage.getViewDescriptor === "function" ?
			subImage.getViewDescriptor() : null;
		return (descriptor && descriptor.baseArrayLayer) ?? subImage.arrayLayer ??
			subImage.textureArrayLayer ?? subImage.imageIndex ?? fallback;
	}

	function packFrame(time, pose, currentBinding, layer, currentResetGeneration) {
		const views = Array.from(pose.views).slice(0, ABI.maxViews);
		if (views.length === 0) return null;
		const subImages = views.map(view => currentBinding.getViewSubImage(layer, view));
		const colorTexture = subImages[0].colorTexture;
		if (!colorTexture || subImages.some(image => image.colorTexture !== colorTexture))
			throw new Error("WebXR views did not share one projection color texture");

		const width = colorTexture.width;
		const height = colorTexture.height;
		const packet = new Uint8Array(ABI.headerBytes + views.length * ABI.viewBytes);
		const data = new DataView(packet.buffer);
		data.setUint32(0, ABI.version, true);
		data.setUint32(4, packet.byteLength, true);
		data.setUint32(8, views.length, true);
		data.setUint32(12, 0, true);
		data.setFloat64(16, time, true);
		data.setUint32(24, currentResetGeneration, true);
		data.setUint32(28, width, true);
		data.setUint32(32, height, true);

		views.forEach(function (view, index) {
			const image = subImages[index];
			const viewport = image.viewport || { x: 0, y: 0, width: width, height: height };
			const transform = view.transform;
			const base = ABI.headerBytes + index * ABI.viewBytes;
			data.setUint32(base, view.eye === "left" ? 1 : (view.eye === "right" ? 2 : 0), true);
			data.setUint32(base + 4, arrayLayerOf(image, index), true);
			data.setInt32(base + 8, viewport.x, true);
			data.setInt32(base + 12, viewport.y, true);
			data.setInt32(base + 16, viewport.width, true);
			data.setInt32(base + 20, viewport.height, true);
			writeArray(data, base + 24, [transform.position.x, transform.position.y, transform.position.z]);
			writeArray(data, base + 36, [transform.orientation.x, transform.orientation.y,
				transform.orientation.z, transform.orientation.w]);
			writeArray(data, base + 52, view.projectionMatrix);
		});
		return { packet: packet, colorTexture: colorTexture };
	}

	function renderFrame(time, frame) {
		submitInputPacket(packInputSnapshot(time, session, frame, referenceSpace));
		const pose = frame.getViewerPose(referenceSpace);
		if (!pose) return false;
		const packed = packFrame(time, pose, binding, projectionLayer, resetGeneration);
		if (!packed) return false;
		root.surrealWebXRFrameTexture = packed.colorTexture;
		try {
			const rendered = moduleCall("Surreal_RenderWebXRFrame", "number", ["array", "number"],
				[packed.packet, packed.packet.byteLength]);
			if (rendered !== 1) {
				const error = moduleCall("Surreal_GetWebXRFrameLastError", "number");
				throw new Error("native WebXR frame bridge rejected the frame (error=" + error + ")");
			}
			return true;
		} finally {
			root.surrealWebXRFrameTexture = null;
		}
	}

	function finish(generation, phase, error) {
		if (generation !== activeGeneration) return false;
		submitNeutralInput(0, false);
		activeGeneration = 0;
		enterPending = false;
		session = null;
		referenceSpace = null;
		binding = null;
		projectionLayer = null;
		try { setEngineLoop(false); } catch (_) {}
		resetNativePose();
		status.phase = phase || "ended";
		status.active = false;
		status.lastError = error ? String(error) : null;
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
			session.requestAnimationFrame(function (nextTime, nextFrame) {
				onFrame(generation, nextTime, nextFrame);
			});
			if (renderFrame(time, frame)) status.frames++;
			else status.skippedFrames++;
		} catch (error) {
			fail(generation, error);
		}
	}

	async function requestReference(sessionObject) {
		try {
			return { space: await sessionObject.requestReferenceSpace("local-floor"), type: "local-floor" };
		} catch (_) {
			return { space: await sessionObject.requestReferenceSpace("local"), type: "local" };
		}
	}

	root.surrealXRIsSupported = async function () {
		if (!root.navigator || !root.navigator.xr || typeof root.navigator.xr.isSessionSupported !== "function")
			return false;
		try { return await root.navigator.xr.isSessionSupported("immersive-vr"); }
		catch (_) { return false; }
	};

	root.surrealXRIsColorFormatSupported = function (format) {
		return format === "bgra8unorm" || format === "rgba8unorm" || format === "rgba16float";
	};

	root.surrealXREnter = async function () {
		if (session || enterPending) return false;
		let preflightError = null;
		if (!root.navigator || !root.navigator.xr) preflightError = "WebXR is unavailable";
		else if (typeof root.XRGPUBinding !== "function") preflightError = "XRGPUBinding is unavailable";
		else if (!webGPUDevice()) preflightError = "SurrealEngine WebGPU device is not ready";
		if (preflightError) {
			status.phase = "error";
			status.lastError = preflightError;
			log(preflightError);
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
		let requestedSession = null;
		try {
			requestedSession = await root.navigator.xr.requestSession("immersive-vr", {
				requiredFeatures: ["webgpu"], optionalFeatures: ["local-floor"]
			});
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
			binding = new root.XRGPUBinding(session, webGPUDevice());
			const projectionFormat = binding.getPreferredColorFormat();
			if (!root.surrealXRIsColorFormatSupported(projectionFormat))
				throw new Error("unsupported WebXR projection color format: " + projectionFormat);
			status.projectionFormat = projectionFormat;
			projectionLayer = binding.createProjectionLayer({ colorFormat: projectionFormat, scaleFactor: 1 });
			await session.updateRenderState({ layers: [projectionLayer] });
			const reference = await requestReference(session);
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
			if (!setEngineLoop(true)) throw new Error("engine rejected WebXR frame-loop ownership");
			enterPending = false;
			status.phase = "running";
			status.active = true;
			session.requestAnimationFrame(function (time, frame) { onFrame(generation, time, frame); });
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

	root.surrealXRGetState = function () { return Object.assign({}, status); };
	root.surrealXRFrameABI = ABI;
	root.surrealXRInputABI = INPUT_ABI;
	root.surrealXRPackInputSnapshot = packInputSnapshot;
})(typeof window !== "undefined" ? window : globalThis);
