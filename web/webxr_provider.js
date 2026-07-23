(function (root) {
	"use strict";

	// Version 3 keeps the packed layout from v2, but changes texture ownership:
	// every texture handed to native code is an ordinary persistent GPUTexture.
	// XR compositor textures are acquired and consumed only inside the XR rAF.
	const ABI = Object.freeze({ version: 3, headerBytes: 32, viewBytes: 128, maxViews: 2 });
	const FRAME_FLAGS = Object.freeze({ projectionDepthZeroToOne: 1, sharedStereoAtlas: 2 });
	const INPUT_ABI = Object.freeze({
		version: 1, headerBytes: 24, sourceBytes: 112, maxSources: 2,
		buttons: Object.freeze({ trigger: 0, squeeze: 1, primary: 2, secondary: 3, menu: 4, stickClick: 5 })
	});
	const INPUT_HEADER_FLAGS = Object.freeze({ sessionActive: 1, actionFocused: 2 });
	const INPUT_SOURCE_FLAGS = Object.freeze({ connected: 1, aimValid: 2, gripValid: 4 });
	const INPUT_QUEUE_LIMIT = 256;
	let generationCounter = 0;
	let activeGeneration = 0;
	let enterPending = false;
	let activationPending = false;
	let session = null;
	let sessionEndTracker = null;
	let sessionEndBlocked = false;
	let referenceSpace = null;
	let binding = null;
	let projectionLayer = null;
	let webGLBridge = null;
	let presentationMode = null;
	let resetGeneration = 0;
	let animationFrameHandle = null;
	let engineLoopOwned = false;
	let persistentTargets = null;
	let targetLayout = null;
	let layoutEpoch = 0;
	let frontTargetIndex = null;
	let latestFrameCapture = null;
	let queuedInputPackets = [];
	let lastQueuedInputSignature = null;
	let inputQueueOverflow = false;
	let renderPumpHandle = null;
	let nativeRenderPromise = null;
	let cleanupPending = Promise.resolve();

	const status = {
		phase: "idle",
		currentStage: "idle",
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
		presentationMode: null,
		layerWidth: null,
		layerHeight: null,
		atlasWidth: null,
		atlasHeight: null,
		bridgeDiagnostics: null,
		capabilities: null,
		enterAttempts: 0,
		successfulEntries: 0,
		exitRequests: 0,
		endedSessions: 0,
		reentries: 0,
		transitions: [],
	};
	let transitionSequence = 0;

	function recordTransition(type, generation, stage) {
		status.transitions.push(Object.freeze({
			sequence: ++transitionSequence,
			type: String(type),
			generation: Number(generation) || 0,
			stage: String(stage || status.currentStage || "unknown"),
		}));
		if (status.transitions.length > 16) status.transitions.shift();
	}

	function setStage(stage) {
		status.currentStage = stage;
	}

	function nonnegativeNumber(value) {
		return Number.isFinite(value) && value >= 0 ? value : null;
	}

	function positiveDimension(value) {
		return Number.isInteger(value) && value > 0 ? value : null;
	}

	function copyBridgeDiagnostics(source) {
		if (!source || typeof source !== "object") return null;
		return Object.freeze({
			frames: nonnegativeNumber(source.frames),
			errors: nonnegativeNumber(source.errors),
			samples: nonnegativeNumber(source.samples),
			medianMs: nonnegativeNumber(source.medianMs),
			p95Ms: nonnegativeNumber(source.p95Ms),
			p99Ms: nonnegativeNumber(source.p99Ms),
			blockingTiming: source.blockingTiming === true,
			layerWidth: positiveDimension(source.layerWidth),
			layerHeight: positiveDimension(source.layerHeight),
			atlasWidth: positiveDimension(source.atlasWidth),
			atlasHeight: positiveDimension(source.atlasHeight),
		});
	}

	function setBridgeDiagnostics(source) {
		status.bridgeDiagnostics = copyBridgeDiagnostics(source);
		if (!status.bridgeDiagnostics) return;
		status.layerWidth = status.bridgeDiagnostics.layerWidth;
		status.layerHeight = status.bridgeDiagnostics.layerHeight;
		status.atlasWidth = status.bridgeDiagnostics.atlasWidth;
		status.atlasHeight = status.bridgeDiagnostics.atlasHeight;
	}

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

	function moduleCall(name, returnType, argumentTypes, args, options) {
		if (!root.Module || typeof root.Module.ccall !== "function")
			throw new Error("SurrealEngine WebAssembly runtime is not ready");
		return root.Module.ccall(name, returnType, argumentTypes || [], args || [], options);
	}

	function setNativeCallsBlocked(blocked) {
		root.surrealXRNativeCallsBlocked = blocked === true;
		if (typeof root.dispatchEvent === "function" && typeof root.Event === "function") {
			try { root.dispatchEvent(new root.Event("surrealnativecallgatechange")); } catch (_) {}
		}
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
		enqueueInputPacket(packInputSnapshot(time, session, null, referenceSpace));
	}

	function requireNativeFrameABI() {
		const query = root.Module && root.Module._Surreal_GetWebXRFrameABIVersion;
		if (typeof query !== "function" || query() !== ABI.version)
			throw providerError("incompatible-native-frame-abi", "activating-session",
				"native WebXR frame ABI does not support persistent two-phase presentation");
	}

	function enqueueInputPacket(packet) {
		if (inputQueueOverflow) return;
		const data = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
		const sourceCount = data.getUint32(8, true);
		const signature = [data.getUint32(12, true), sourceCount];
		for (let index = 0; index < sourceCount; index++) {
			const base = INPUT_ABI.headerBytes + index * INPUT_ABI.sourceBytes;
			signature.push(data.getUint32(base, true), data.getUint32(base + 4, true) & INPUT_SOURCE_FLAGS.connected,
				data.getUint32(base + 8, true), data.getUint32(base + 12, true));
		}
		const discrete = signature.join(":");
		const transition = discrete !== lastQueuedInputSignature;
		lastQueuedInputSignature = discrete;
		// A continuous pose/axis sample immediately before a discrete edge is
		// redundant because the edge packet also contains the newest pose/axes.
		if (transition && queuedInputPackets.length && !queuedInputPackets.at(-1).transition)
			queuedInputPackets.pop();
		if (transition) {
			if (queuedInputPackets.length >= INPUT_QUEUE_LIMIT) { inputQueueOverflow = true; return; }
			queuedInputPackets.push({ packet, transition: true });
		}
		else if (queuedInputPackets.length && !queuedInputPackets.at(-1).transition)
			queuedInputPackets[queuedInputPackets.length - 1] = { packet, transition: false };
		else {
			if (queuedInputPackets.length >= INPUT_QUEUE_LIMIT) { inputQueueOverflow = true; return; }
			queuedInputPackets.push({ packet, transition: false });
		}
		// Never silently discard a button/focus/connect edge. A pathological stall
		// fails closed instead of leaving a logically stuck button in native input.
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

	function validateViews(pose) {
		const views = Array.from(pose.views);
		if (views.length !== ABI.maxViews || views[0].eye === views[1].eye ||
			!views.some(view => view.eye === "left") || !views.some(view => view.eye === "right"))
			throw providerError("unsupported-view-configuration", "frame",
				"WebXR immersive VR requires exactly one left and one right primary view");
		return views;
	}

	function snapshotView(view) {
		const projection = Array.from(view.projectionMatrix || []);
		const transform = view.transform || {};
		const position = transform.position || {};
		const orientation = transform.orientation || {};
		if (projection.length !== 16 || !projection.every(Number.isFinite) ||
			![position.x, position.y, position.z, orientation.x, orientation.y,
				orientation.z, orientation.w].every(Number.isFinite))
			throw providerError("invalid-view-pose", "frame", "WebXR returned an invalid view pose");
		return { eye: view.eye, projectionMatrix: projection, transform: {
			position: { x: position.x, y: position.y, z: position.z },
			orientation: { x: orientation.x, y: orientation.y, z: orientation.z, w: orientation.w },
		} };
	}

	function createFramePacket(time, views, packedViews, textures, currentResetGeneration, flags) {
		const packet = new Uint8Array(ABI.headerBytes + views.length * ABI.viewBytes);
		const data = new DataView(packet.buffer);
		data.setUint32(0, ABI.version, true);
		data.setUint32(4, packet.byteLength, true);
		data.setUint32(8, views.length, true);
		data.setUint32(12, textures.length, true);
		data.setFloat64(16, time, true);
		data.setUint32(24, currentResetGeneration, true);
		data.setUint32(28, flags || 0, true);

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
			writeArray(data, base + 64, packedView.projection || view.projectionMatrix);
		});
		return { packet, textures };
	}

	function directFrameDescription(views) {
		const destinations = views.map(function (view) {
			const image = binding.getViewSubImage(projectionLayer, view);
			if (!image || !image.colorTexture || typeof image.getViewDescriptor !== "function")
				throw providerError("invalid-projection-subimage", "frame", "WebXR returned an incomplete projection subimage");
			const descriptor = image.getViewDescriptor() || {};
			const texture = image.colorTexture;
			const viewport = image.viewport || { x: 0, y: 0,
				width: texture.width, height: texture.height };
			const arrayLayer = descriptor.baseArrayLayer === undefined ? 0 : descriptor.baseArrayLayer;
			const depth = texture.depthOrArrayLayers;
			const usage = root.GPUTextureUsage || {};
			const copyDestination = usage.COPY_DST || 0x02;
			if (![viewport.x, viewport.y, viewport.width, viewport.height, arrayLayer].every(Number.isInteger) ||
				viewport.width <= 0 || viewport.height <= 0 || viewport.x < 0 || viewport.y < 0 || arrayLayer < 0 ||
				!Number.isInteger(texture.width) || !Number.isInteger(texture.height) ||
				viewport.x + viewport.width > texture.width || viewport.y + viewport.height > texture.height ||
				!Number.isInteger(depth) || depth <= arrayLayer || texture.format !== status.projectionFormat ||
				!Number.isInteger(texture.usage) || (texture.usage & copyDestination) === 0 ||
				(descriptor.arrayLayerCount !== undefined && descriptor.arrayLayerCount !== 1))
				throw providerError("invalid-projection-subimage", "frame", "WebXR returned invalid projection texture metadata");
			return { eye: view.eye, texture, arrayLayer,
				viewport: { x: viewport.x, y: viewport.y, width: viewport.width, height: viewport.height } };
		});
		return { mode: "direct-webgpu", format: status.projectionFormat, destinations,
			views: destinations.map(item => ({ eye: item.eye, width: item.viewport.width, height: item.viewport.height })) };
	}

	function frameLayoutSignature(layout) {
		return [layout.mode, layout.format].concat(layout.views.map(view =>
			view.eye + ":" + view.width + "x" + view.height)).join("|");
	}

	function textureUsage() {
		const usage = root.GPUTextureUsage || {};
		return (usage.RENDER_ATTACHMENT || 0x10) | (usage.COPY_SRC || 0x01) |
			(usage.TEXTURE_BINDING || 0x04);
	}

	function createPersistentTargetSets(layout) {
		const device = webGPUDevice();
		if (!device || typeof device.createTexture !== "function")
			throw providerError("webgpu-device-not-ready", "frame", "WebGPU cannot allocate persistent WebXR targets");
		return [0, 1].map(setIndex => Object.freeze({
			textures: layout.mode === "webgl-bridge" ? [device.createTexture({
				label: "Surreal WebXR atlas " + setIndex, size: [layout.width, layout.height, 1],
				format: layout.format, usage: textureUsage(),
			})] : layout.views.map(view => device.createTexture({
				label: "Surreal WebXR " + view.eye + " " + setIndex,
				size: [view.width, view.height, 1], format: layout.format, usage: textureUsage(),
			})),
		}));
	}

	function destroyTargetSets(sets) {
		for (const set of sets || []) for (const texture of set.textures || []) {
			try { if (texture && typeof texture.destroy === "function") texture.destroy(); } catch (_) {}
		}
	}

	function retireTargetSets(sets, pendingRender) {
		if (!sets) return;
		Promise.resolve(pendingRender).catch(function () {}).then(async function () {
			const device = webGPUDevice();
			try {
				if (device && device.queue && typeof device.queue.onSubmittedWorkDone === "function")
					await device.queue.onSubmittedWorkDone();
			} catch (_) {}
			destroyTargetSets(sets);
		});
	}

	function adoptLayout(description) {
		const layout = description.mode === "webgl-bridge" ? {
			mode: description.mode, format: description.format, width: description.width,
			height: description.height, views: description.atlasViews.map((view, index) => ({
				eye: index === 0 ? "left" : "right", width: view.width, height: view.height,
			})),
		} : { mode: description.mode, format: description.format,
			views: description.views.map(view => Object.assign({}, view)) };
		layout.signature = frameLayoutSignature(layout) + (layout.mode === "webgl-bridge" ?
			("|" + layout.width + "x" + layout.height) : "");
		if (targetLayout && targetLayout.signature === layout.signature) return true;
		const oldTargets = persistentTargets;
		const pending = nativeRenderPromise;
		layoutEpoch++;
		targetLayout = layout;
		persistentTargets = createPersistentTargetSets(layout);
		frontTargetIndex = null;
		latestFrameCapture = null;
		retireTargetSets(oldTargets, pending);
		return false;
	}

	function packPersistentFrame(capture, targetSet) {
		const views = capture.views;
		let packedViews;
		let flags = 0;
		if (targetLayout.mode === "webgl-bridge") {
			packedViews = views.map(function (view, index) {
				return { view, textureIndex: 0, arrayLayer: 0, width: targetLayout.width,
					height: targetLayout.height, image: { viewport: capture.atlasViews[index] },
					projection: root.SurrealWebXRWebGLBridge.convertProjectionDepth(view.projectionMatrix) };
			});
			flags = FRAME_FLAGS.projectionDepthZeroToOne | FRAME_FLAGS.sharedStereoAtlas;
		} else {
			packedViews = views.map(function (view, index) {
				const layoutView = targetLayout.views[index];
				return { view, textureIndex: index, arrayLayer: 0,
					width: layoutView.width, height: layoutView.height,
					image: { viewport: { x: 0, y: 0, width: layoutView.width, height: layoutView.height } } };
			});
		}
		return createFramePacket(capture.time, views, packedViews, targetSet.textures,
			capture.resetGeneration, flags);
	}

	function presentDirect(description) {
		if (frontTargetIndex === null || !persistentTargets) return false;
		const encoder = webGPUDevice().createCommandEncoder({ label: "Surreal WebXR late present" });
		const source = persistentTargets[frontTargetIndex].textures;
		description.destinations.forEach(function (destination, index) {
			encoder.copyTextureToTexture({ texture: source[index] }, {
				texture: destination.texture,
				origin: { x: destination.viewport.x, y: destination.viewport.y, z: destination.arrayLayer },
			}, { width: destination.viewport.width, height: destination.viewport.height, depthOrArrayLayers: 1 });
		});
		webGPUDevice().queue.submit([encoder.finish()]);
		return true;
	}

	function scheduleRenderPump(generation) {
		if (renderPumpHandle !== null || nativeRenderPromise || generation !== activeGeneration) return;
		renderPumpHandle = root.setTimeout(function () {
			renderPumpHandle = null;
			pumpNativeRender(generation);
		}, 0);
	}

	function pumpNativeRender(generation) {
		if (generation !== activeGeneration || nativeRenderPromise || !latestFrameCapture ||
			!persistentTargets || !targetLayout) return;
		const capture = latestFrameCapture;
		latestFrameCapture = null;
		if (capture.epoch !== layoutEpoch) { scheduleRenderPump(generation); return; }
		if (inputQueueOverflow) {
			fail(generation, providerError("input-transition-overflow", "frame",
				"WebXR input transition queue overflowed while native rendering was suspended"));
			return;
		}
		const input = queuedInputPackets;
		queuedInputPackets = [];
		try { input.forEach(entry => submitInputPacket(entry.packet)); }
		catch (error) { fail(generation, providerError("input-submit-failed", "frame", error.message || String(error))); return; }
		const backIndex = frontTargetIndex === 0 ? 1 : 0;
		const targets = persistentTargets;
		const packed = packPersistentFrame(capture, targets[backIndex]);
		root.surrealWebXRFrameTextures = packed.textures;
		setNativeCallsBlocked(true);
		let renderResult;
		try {
			renderResult = moduleCall("Surreal_RenderWebXRFrame", "number", ["array", "number"],
				[packed.packet, packed.packet.byteLength], { async: true });
		} catch (error) {
			root.surrealWebXRFrameTextures = null;
			setNativeCallsBlocked(false);
			fail(generation, providerError("frame-failed", "frame", error.message || String(error)));
			return;
		}
		const promise = Promise.resolve(renderResult).then(function (rendered) {
			if (rendered !== 1) {
				const error = moduleCall("Surreal_GetWebXRFrameLastError", "number");
				throw providerError("native-frame-rejected-" + error, "frame",
					"native WebXR frame bridge rejected the frame (error=" + error + ")");
			}
			if (generation === activeGeneration && capture.epoch === layoutEpoch && targets === persistentTargets) {
				frontTargetIndex = backIndex;
				status.frames++;
			}
		}).catch(function (error) {
			if (generation === activeGeneration)
				fail(generation, error instanceof WebXRProviderError ? error :
					providerError("frame-failed", "frame", error.message || String(error)));
		}).finally(function () {
			root.surrealWebXRFrameTextures = null;
			if (nativeRenderPromise === promise) nativeRenderPromise = null;
			setNativeCallsBlocked(false);
			if (generation === activeGeneration) scheduleRenderPump(generation);
		});
		nativeRenderPromise = promise;
	}

	function finish(generation, phase, error) {
		if (generation !== activeGeneration) return false;
		const nativePresentationStarted = status.active || activationPending || engineLoopOwned || referenceSpace !== null;
		const finishedSession = session;
		const pendingFrame = animationFrameHandle;
		const pendingRender = nativeRenderPromise;
		const finishedTargets = persistentTargets;
		const finishedBridge = webGLBridge;
		const ownedEngineLoop = engineLoopOwned;
		const errorStage = error ? (error.stage || status.currentStage || status.phase) : null;
		activeGeneration = 0;
		enterPending = false;
		activationPending = false;
		animationFrameHandle = null;
		if (renderPumpHandle !== null) {
			try { root.clearTimeout(renderPumpHandle); } catch (_) {}
			renderPumpHandle = null;
		}
		if (finishedSession && pendingFrame !== null &&
			typeof finishedSession.cancelAnimationFrame === "function") {
			try { finishedSession.cancelAnimationFrame(pendingFrame); } catch (_) {}
		}
		session = null;
		sessionEndTracker = null;
		if (root.SurrealBrowserPointerLock) root.SurrealBrowserPointerLock.setXRActive(false);
		referenceSpace = null;
		binding = null;
		projectionLayer = null;
		webGLBridge = null;
		presentationMode = null;
		persistentTargets = null;
		targetLayout = null;
		frontTargetIndex = null;
		latestFrameCapture = null;
		queuedInputPackets = [];
		lastQueuedInputSignature = null;
		inputQueueOverflow = false;
		engineLoopOwned = false;
		if (!pendingRender) root.surrealWebXRFrameTextures = null;
		status.phase = phase || "ended";
		setStage(status.phase);
		status.active = false;
		status.presentationMode = null;
		status.layerWidth = null;
		status.layerHeight = null;
		status.atlasWidth = null;
		status.atlasHeight = null;
		status.bridgeDiagnostics = null;
		status.lastError = error ? String(error.message || error) : null;
		status.lastErrorCode = error ? (error.code || "webxr-provider-failed") : null;
		status.lastErrorStage = errorStage;
		status.endedSessions++;
		recordTransition(error ? "entry-or-session-failed" : "session-ended", generation,
			errorStage || status.phase);
		if (error) log("WebXR session failed: " + error);
		cleanupPending = (async function () {
			try { await Promise.resolve(pendingRender); } catch (_) {}
			if (nativePresentationStarted) submitNeutralInput(0, false);
			if (ownedEngineLoop) {
				try { setEngineLoop(false); } catch (_) {}
			}
			if (nativePresentationStarted) resetNativePose();
			const device = webGPUDevice();
			try {
				if (device && device.queue && typeof device.queue.onSubmittedWorkDone === "function")
					await device.queue.onSubmittedWorkDone();
			} catch (_) {}
			destroyTargetSets(finishedTargets);
			if (finishedBridge) { try { finishedBridge.destroy(); } catch (_) {} }
		})();
		return true;
	}

	function createSessionEndTracker(sessionObject, generation) {
		let resolveEnded;
		const tracker = {
			generation,
			ended: false,
			blocked: false,
			promise: new Promise(function (resolve) { resolveEnded = resolve; }),
		};
		sessionObject.addEventListener("end", function () {
			if (!tracker.ended) {
				tracker.ended = true;
				resolveEnded();
			}
			if (tracker.blocked) {
				tracker.blocked = false;
				sessionEndBlocked = false;
				if (activeGeneration === 0) {
					status.phase = "ended";
					setStage("ended");
					recordTransition("session-end-confirmed", generation, status.currentStage);
				}
			}
			finish(generation, "ended", null);
		});
		return tracker;
	}

	function drainSessionEnd(ending, endTracker, failureMessage) {
		const localCleanup = cleanupPending;
		cleanupPending = Promise.all([
			localCleanup,
			Promise.resolve(ending).catch(function (error) {
				log(failureMessage + error);
				if (!endTracker || endTracker.ended) return;
				endTracker.blocked = true;
				sessionEndBlocked = true;
				status.phase = "error";
				setStage("session-shutdown");
				status.lastError = String(error && (error.message || error) || "session end rejected");
				status.lastErrorCode = "session-end-rejected";
				status.lastErrorStage = "session-shutdown";
				recordTransition("session-end-rejected", endTracker.generation, status.currentStage);
				// A rejected end request does not prove that the browser released its
				// immersive session. Admission stays closed until the real end event;
				// otherwise only a full page reset can clear this terminal gate.
				return endTracker.promise;
			}),
		]).then(function () {});
	}

	function fail(generation, error) {
		const failedSession = generation === activeGeneration ? session : null;
		const failedEndTracker = generation === activeGeneration ? sessionEndTracker : null;
		if (!finish(generation, "error", error)) return;
		if (failedSession) {
			let ending;
			try {
				ending = failedSession.end();
			} catch (endError) {
				ending = Promise.reject(endError);
			}
			drainSessionEnd(ending, failedEndTracker,
				"WebXR failed-session end rejected after scheduling stopped: ");
		}
	}

	function onFrame(generation, time, frame) {
		if (generation !== activeGeneration || !session) return;
		try {
			animationFrameHandle = session.requestAnimationFrame(function (nextTime, nextFrame) {
				animationFrameHandle = null;
				onFrame(generation, nextTime, nextFrame);
			});
			const pose = frame.getViewerPose(referenceSpace);
			if (!pose) { status.skippedFrames++; return; }
			const xrViews = validateViews(pose);
			const views = xrViews.map(snapshotView);
			enqueueInputPacket(packInputSnapshot(time, session, frame, referenceSpace));
			let description;
			if (presentationMode === "webgl-bridge") {
				description = webGLBridge.describeFrame(pose);
				description.mode = "webgl-bridge";
				description.format = webGLBridge.textureFormat;
				adoptLayout(description);
				if (frontTargetIndex !== null) {
					webGLBridge.present(description, persistentTargets[frontTargetIndex].textures[0], webGPUDevice());
					setBridgeDiagnostics(webGLBridge.diagnostics());
				}
			} else {
				description = directFrameDescription(xrViews);
				adoptLayout(description);
				presentDirect(description);
			}
			if (latestFrameCapture) status.skippedFrames++;
			latestFrameCapture = { generation, epoch: layoutEpoch, time, views,
				resetGeneration, atlasViews: description.atlasViews || null };
			scheduleRenderPump(generation);
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
			directWebGPU: false,
			webGLBridge: false,
			preferredMode: null,
			supported: false,
			reasons: [],
		};
		if (!result.secureContext) result.reasons.push("secure-context-required");
		if (!result.webXR) result.reasons.push("webxr-unavailable");
		if (!result.webGPUDevice) result.reasons.push("webgpu-device-not-ready");
		if (result.webXR && typeof root.navigator.xr.isSessionSupported === "function") {
			try { result.immersiveVR = await root.navigator.xr.isSessionSupported("immersive-vr"); }
			catch (_) { result.reasons.push("immersive-vr-query-failed"); }
		}
		if (!result.immersiveVR && !result.reasons.includes("immersive-vr-query-failed"))
			result.reasons.push("immersive-vr-unavailable");
		result.directWebGPU = result.webGPUBinding && result.webGPUDevice && result.webGPUDeviceXRCompatible;
		result.webGLBridge = !!(result.webGPUDevice && typeof root.XRWebGLLayer === "function" &&
			root.SurrealWebXRWebGLBridge && root.SurrealWebXRWebGLBridge.canCreateWebGL2(root));
		if (result.webGPUDevice && !result.webGPUDeviceXRCompatible && !result.webGLBridge)
			result.reasons.push("webgpu-device-not-xr-compatible");
		result.preferredMode = root.surrealXRForceWebGLBridge === true && result.webGLBridge ? "webgl-bridge" :
			(result.directWebGPU ? "direct-webgpu" : (result.webGLBridge ? "webgl-bridge" : null));
		if (!result.directWebGPU && !result.webGLBridge)
			result.reasons.push(hasLegacyBinding ? "obsolete-webgpu-binding-api" : "no-webxr-presentation-backend");
		result.supported = result.secureContext && result.webXR && result.immersiveVR &&
			result.webGPUDevice && !!result.preferredMode;
		status.capabilities = Object.assign({}, result, { reasons: result.reasons.slice() });
		return Object.assign({}, result, { reasons: result.reasons.slice() });
	};

	root.surrealXRIsSupported = async function () {
		return (await root.surrealXRGetCapabilities()).supported;
	};

	root.surrealXRIsColorFormatSupported = function (format) {
		return format === "bgra8unorm" || format === "rgba8unorm" || format === "rgba16float";
	};

	root.surrealXRRequestSession = async function () {
		await cleanupPending;
		if (session || enterPending) return false;
		status.enterAttempts++;
		setStage("preflight");
		recordTransition("enter-requested", generationCounter + 1, status.currentStage);
		let preflightError = null;
		if (root.isSecureContext === false)
			preflightError = providerError("secure-context-required", "preflight", "WebXR requires a secure context");
		else if (!root.navigator || !root.navigator.xr)
			preflightError = providerError("webxr-unavailable", "preflight", "WebXR is unavailable");
		else if (!webGPUDevice())
			preflightError = providerError("webgpu-device-not-ready", "preflight",
				"SurrealEngine WebGPU device is not ready");
		const bridgeAvailable = typeof root.XRWebGLLayer === "function" && root.SurrealWebXRWebGLBridge &&
			root.SurrealWebXRWebGLBridge.canCreateWebGL2(root);
		const directAvailable = typeof root.XRGPUBinding === "function" &&
			root.surrealWebGPUDeviceXRCompatible === true;
		presentationMode = root.surrealXRForceWebGLBridge === true && bridgeAvailable ? "webgl-bridge" :
			(directAvailable ? "direct-webgpu" : (bridgeAvailable ? "webgl-bridge" : null));
		if (!preflightError && !presentationMode) {
			const missingBinding = typeof root.XRGPUBinding !== "function";
			preflightError = providerError(missingBinding ? "webxr-webgpu-binding-unavailable" :
				"no-webxr-presentation-backend", "preflight",
				"Neither direct WebGPU WebXR layers nor the XRWebGLLayer compatibility bridge is available");
		}
		if (preflightError) {
			status.phase = "error";
			status.lastError = preflightError.message;
			status.lastErrorCode = preflightError.code;
			status.lastErrorStage = preflightError.stage;
			recordTransition("entry-or-session-failed", generationCounter + 1, status.currentStage);
			log(preflightError.message);
			return false;
		}

		enterPending = true;
		const generation = ++generationCounter;
		activeGeneration = generation;
		status.phase = "requesting-session";
		setStage("request-session");
		status.generation = generation;
		status.frames = 0;
		status.skippedFrames = 0;
		status.inputPackets = 0;
		status.lastError = null;
		status.lastErrorCode = null;
		status.lastErrorStage = null;
		status.referenceSpaceType = null;
		status.projectionFormat = null;
		status.presentationMode = presentationMode;
		status.layerWidth = null;
		status.layerHeight = null;
		status.atlasWidth = null;
		status.atlasHeight = null;
		status.bridgeDiagnostics = null;
		let requestedSession = null;
		try {
			try {
				const sessionOptions = presentationMode === "direct-webgpu" ?
					{ requiredFeatures: ["webgpu"], optionalFeatures: ["local-floor"] } :
					{ optionalFeatures: ["local-floor"] };
				requestedSession = await root.navigator.xr.requestSession("immersive-vr", sessionOptions);
			} catch (error) {
				throw providerError("session-request-failed", "requesting-session", error.message || String(error));
			}
			if (generation !== activeGeneration) {
				await requestedSession.end();
				return false;
			}
			session = requestedSession;
			sessionEndTracker = createSessionEndTracker(session, generation);
			if (root.SurrealBrowserPointerLock) root.SurrealBrowserPointerLock.setXRActive(true);
			enterPending = false;
			status.phase = "session-reserved";
			setStage("session-reserved");
			recordTransition("session-reserved", generation, status.currentStage);
			return true;
		} catch (error) {
			fail(generation, error);
			return false;
		}
	};

	root.surrealXRActivateReservedSession = async function () {
		if (!session || status.active || enterPending || activationPending) return false;
		const generation = activeGeneration;
		activationPending = true;
		try {
			requireNativeFrameABI();
			session.addEventListener("inputsourceschange", function (event) {
				if (generation === activeGeneration && event && event.removed && event.removed.length) submitCurrentInput(0);
			});
			session.addEventListener("visibilitychange", function () {
				if (generation === activeGeneration && !isActionFocused(session)) submitCurrentInput(0);
			});
			if (presentationMode === "direct-webgpu") {
				status.phase = "creating-binding";
				setStage("create-binding");
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
				setStage("create-projection-layer");
				try {
					const usage = root.GPUTextureUsage || {};
					projectionLayer = binding.createProjectionLayer({ colorFormat: projectionFormat, scaleFactor: 1,
						textureUsage: (usage.RENDER_ATTACHMENT || 0x10) | (usage.COPY_DST || 0x02) });
					status.layerWidth = positiveDimension(projectionLayer.textureWidth);
					status.layerHeight = positiveDimension(projectionLayer.textureHeight);
					setStage("update-render-state");
					session.updateRenderState({ layers: [projectionLayer] });
				} catch (error) {
					throw providerError("projection-layer-failed", "creating-projection-layer", error.message || String(error));
				}
			} else {
				status.phase = "creating-webgl-bridge";
				setStage("create-webgl-bridge");
				try {
					webGLBridge = await root.SurrealWebXRWebGLBridge.create({ root, session,
						canvas: root.Module && root.Module.canvas, device: webGPUDevice() });
					status.projectionFormat = "rgba8unorm-webgl-bridge";
					setBridgeDiagnostics(webGLBridge.diagnostics());
				} catch (error) {
					throw providerError("webgl-bridge-creation-failed", "creating-webgl-bridge",
						error.message || String(error));
				}
			}
			status.phase = "requesting-reference-space";
			setStage("request-reference-space");
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
			setStage("start-engine-loop");
			submitNeutralInput(0, true);
			status.phase = "starting-frame-loop";
			if (!setEngineLoop(true))
				throw providerError("engine-loop-rejected", "starting-frame-loop",
					"engine rejected WebXR frame-loop ownership");
			engineLoopOwned = true;
			activationPending = false;
			status.phase = "running";
			setStage("running");
			status.active = true;
			status.successfulEntries++;
			status.reentries = Math.max(0, status.successfulEntries - 1);
			recordTransition(status.reentries ? "session-reentered" : "session-entered", generation, status.currentStage);
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

	root.surrealXREnter = async function () {
		if (!await root.surrealXRRequestSession()) return false;
		return root.surrealXRActivateReservedSession();
	};

	root.surrealXRExit = function () {
		if (!session) return false;
		const generation = activeGeneration;
		const exitingSession = session;
		const exitingEndTracker = sessionEndTracker;
		status.exitRequests++;
		setStage("exit-requested");
		recordTransition("exit-requested", generation, status.currentStage);
		let ending;
		let accepted = true;
		try {
			ending = exitingSession.end();
			// The browser may delay the `end` event or the returned Promise. Invalidate
			// scheduling immediately after it accepts the request, then make re-entry
			// wait for both native/GPU cleanup and the actual session shutdown.
			finish(generation, "ended", null);
		} catch (error) {
			accepted = false;
			ending = Promise.reject(error);
			finish(generation, "ended", null);
		}
		drainSessionEnd(ending, exitingEndTracker, "WebXR session end failed after scheduling stopped: ");
		return accepted;
	};

	root.surrealXRGetState = function () {
		const result = Object.assign({}, status);
		if (status.capabilities)
			result.capabilities = Object.assign({}, status.capabilities,
				{ reasons: status.capabilities.reasons.slice() });
		result.transitions = status.transitions.slice();
		result.bridgeDiagnostics = copyBridgeDiagnostics(status.bridgeDiagnostics);
		result.sessionEndBlocked = sessionEndBlocked;
		result.inputQueueDepth = queuedInputPackets.length;
		result.inputQueueOverflow = inputQueueOverflow;
		return result;
	};
	root.surrealXRFrameABI = ABI;
	root.surrealXRInputABI = INPUT_ABI;
	root.surrealXRPackInputSnapshot = packInputSnapshot;
	setNativeCallsBlocked(false);
})(typeof window !== "undefined" ? window : globalThis);
