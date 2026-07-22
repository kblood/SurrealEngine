(function (root) {
	"use strict";

	const ABI = Object.freeze({ version: 1, headerBytes: 36, viewBytes: 116, maxViews: 2 });
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
})(typeof window !== "undefined" ? window : globalThis);
