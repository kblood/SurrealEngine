(function (root) {
	"use strict";

	// Version 4 keeps the packed layout and persistent targets, but splits the
	// Asyncify-capable simulation from the synchronous current-pose XR render.
	const ABI = Object.freeze({ version: 4, headerBytes: 32, viewBytes: 128, maxViews: 2 });
	const FRAME_FLAGS = Object.freeze({ projectionDepthZeroToOne: 1, sharedStereoAtlas: 2 });
	const INPUT_ABI = Object.freeze({
		version: 1, headerBytes: 24, sourceBytes: 112, maxSources: 2,
		buttons: Object.freeze({ trigger: 0, squeeze: 1, primary: 2, secondary: 3, menu: 4, stickClick: 5 })
	});
	const INPUT_HEADER_FLAGS = Object.freeze({ sessionActive: 1, actionFocused: 2 });
	const INPUT_SOURCE_FLAGS = Object.freeze({ connected: 1, aimValid: 2, gripValid: 4 });
	const INPUT_QUEUE_LIMIT = 256;
	const HAPTIC_POLICY = Object.freeze({ minimumDurationMilliseconds: 1, maximumDurationMilliseconds: 1000 });
	const POSE_AGE_LIMITS = Object.freeze({ frames: 65535, milliseconds: 60000, presentations: 0xffffffff });
	const INPUT_DIAGNOSTIC_LIMITS = Object.freeze({ arrayEntries: 64, sources: 2, samples: 0xffffffff });
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
	let configuredPresentationPreference = null;
	let configuredBridgeBlockingTiming = null;
	let configuredBridgeRotationReprojection = null;
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
	let lastQueuedInputSafety = null;
	let inputQueueOverflow = false;
	let renderPumpHandle = null;
	let nativeRenderPromise = null;
	let nativeFramePrepared = false;
	let nativeFrameRendered = false;
	let preparedFrame = null;
	let cleanupPending = Promise.resolve();
	let cleanupInProgress = false;
	let cleanupSequence = 0;
	let frameClockResetPending = false;
	let hapticStatus = null;
	let audioGestureSession = null;
	let audioGestureListener = null;
	let captureSequence = 0;
	let bridgePoseAge = null;
	let inputDiagnostics = null;

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
		presentationPreference: "auto",
		bridgeBlockingTimingRequested: false,
		bridgeRotationReprojectionRequested: false,
		presentationMode: null,
		layerWidth: null,
		layerHeight: null,
		atlasWidth: null,
		atlasHeight: null,
		bridgeDiagnostics: null,
		inputDiagnostics: null,
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
		publishProviderState();
	}

	function publishProviderState() {
		if (typeof root.dispatchEvent !== "function" || typeof root.CustomEvent !== "function") return;
		try {
			root.dispatchEvent(new root.CustomEvent("surrealwebxrproviderstate", { detail: Object.freeze({
				phase: status.phase, stage: status.currentStage, active: status.active,
				generation: status.generation, cleanupPending: cleanupInProgress,
				sessionEndBlocked,
			}) }));
		} catch (_) {}
	}

	function trackCleanup(promise) {
		const sequence = ++cleanupSequence;
		cleanupInProgress = true;
		cleanupPending = Promise.resolve(promise).finally(function () {
			if (sequence !== cleanupSequence) return;
			cleanupInProgress = false;
			publishProviderState();
		});
		publishProviderState();
		return cleanupPending;
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

	function boundedNonnegative(value, maximum) {
		return Number.isFinite(value) && value >= 0 ? Math.min(value, maximum) : null;
	}

	function boundedCounter(value, maximum) {
		const bounded = boundedNonnegative(value, maximum);
		return bounded === null ? 0 : Math.floor(bounded);
	}

	function boundedIntegerOrNull(value, maximum) {
		const bounded = boundedNonnegative(value, maximum);
		return bounded === null ? null : Math.floor(bounded);
	}

	function inputArrayLength(value) {
		if (!value || !Number.isInteger(value.length) || value.length < 0) return null;
		return Math.min(value.length, INPUT_DIAGNOSTIC_LIMITS.arrayEntries);
	}

	function resetInputDiagnostics() {
		inputDiagnostics = {
			actionFocused: null,
			xrStandardSources: 0,
			leftButtons: null,
			leftAxes: null,
			rightButtons: null,
			rightAxes: null,
			nonzeroThumbstickSamples: 0,
		};
		status.inputDiagnostics = copyInputDiagnostics(inputDiagnostics);
	}

	function copyInputDiagnostics(source) {
		if (!source || typeof source !== "object") return null;
		return Object.freeze({
			actionFocused: source.actionFocused === true ? true :
				source.actionFocused === false ? false : null,
			xrStandardSources: boundedCounter(source.xrStandardSources, INPUT_DIAGNOSTIC_LIMITS.sources),
			leftButtons: boundedIntegerOrNull(source.leftButtons, INPUT_DIAGNOSTIC_LIMITS.arrayEntries),
			leftAxes: boundedIntegerOrNull(source.leftAxes, INPUT_DIAGNOSTIC_LIMITS.arrayEntries),
			rightButtons: boundedIntegerOrNull(source.rightButtons, INPUT_DIAGNOSTIC_LIMITS.arrayEntries),
			rightAxes: boundedIntegerOrNull(source.rightAxes, INPUT_DIAGNOSTIC_LIMITS.arrayEntries),
			nonzeroThumbstickSamples: boundedCounter(source.nonzeroThumbstickSamples,
				INPUT_DIAGNOSTIC_LIMITS.samples),
		});
	}

	function updateInputDiagnostics(currentSession, actionFocused, sources) {
		// Export only observations from the live provider session. The public packet
		// helper remains side-effect free for tests and launcher feature probes.
		if (!inputDiagnostics || !activeGeneration || currentSession !== session) return;
		inputDiagnostics.actionFocused = actionFocused === true;
		inputDiagnostics.xrStandardSources = 0;
		inputDiagnostics.leftButtons = null;
		inputDiagnostics.leftAxes = null;
		inputDiagnostics.rightButtons = null;
		inputDiagnostics.rightAxes = null;
		for (const source of sources) {
			const gamepad = source && source.gamepad;
			const hand = source && source.handedness;
			if (hand === "left" || hand === "right") {
				inputDiagnostics[hand + "Buttons"] = inputArrayLength(gamepad && gamepad.buttons);
				inputDiagnostics[hand + "Axes"] = inputArrayLength(gamepad && gamepad.axes);
			}
			if (!gamepad || gamepad.mapping !== "xr-standard") continue;
			inputDiagnostics.xrStandardSources++;
			if (!actionFocused || !gamepad.axes || gamepad.axes.length < 4) continue;
			const stickX = finiteClamped(gamepad.axes[2], -1, 1);
			const stickY = finiteClamped(gamepad.axes[3], -1, 1);
			if (stickX !== 0 || stickY !== 0)
				inputDiagnostics.nonzeroThumbstickSamples = Math.min(
					inputDiagnostics.nonzeroThumbstickSamples + 1, INPUT_DIAGNOSTIC_LIMITS.samples);
		}
		status.inputDiagnostics = copyInputDiagnostics(inputDiagnostics);
	}

	function resetBridgePoseAge() {
		bridgePoseAge = {
			presentAgeFrames: null,
			maxPresentAgeFrames: null,
			presentAgeMs: null,
			maxPresentAgeMs: null,
			reusedPresents: 0,
		};
	}

	resetBridgePoseAge();

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
			reprojectionMode: source.reprojectionMode === "rotation-only" ? "rotation-only" :
				(source.reprojectionMode === "disabled" ? "disabled" : null),
			reprojectedFrames: boundedCounter(source.reprojectedFrames, POSE_AGE_LIMITS.presentations),
			reprojectionFallbackFrames: boundedCounter(source.reprojectionFallbackFrames, POSE_AGE_LIMITS.presentations),
			reprojectedEyes: boundedCounter(source.reprojectedEyes, POSE_AGE_LIMITS.presentations),
			reprojectionFallbackEyes: boundedCounter(source.reprojectionFallbackEyes, POSE_AGE_LIMITS.presentations),
			presentAgeFrames: boundedIntegerOrNull(source.presentAgeFrames, POSE_AGE_LIMITS.frames),
			maxPresentAgeFrames: boundedIntegerOrNull(source.maxPresentAgeFrames, POSE_AGE_LIMITS.frames),
			presentAgeMs: boundedNonnegative(source.presentAgeMs, POSE_AGE_LIMITS.milliseconds),
			maxPresentAgeMs: boundedNonnegative(source.maxPresentAgeMs, POSE_AGE_LIMITS.milliseconds),
			reusedPresents: boundedCounter(source.reusedPresents, POSE_AGE_LIMITS.presentations),
		});
	}

	function setBridgeDiagnostics(source) {
		status.bridgeDiagnostics = copyBridgeDiagnostics(Object.assign({}, source, bridgePoseAge));
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

	function presentationPreference() {
		if (configuredPresentationPreference) return configuredPresentationPreference;
		return root.surrealXRForceWebGLBridge === true ? "webgl-bridge" : "auto";
	}

	function selectPresentationMode(preference, directAvailable, bridgeAvailable) {
		if (preference === "webgl-bridge") return bridgeAvailable ? "webgl-bridge" : null;
		return directAvailable ? "direct-webgpu" : (bridgeAvailable ? "webgl-bridge" : null);
	}

	function sessionHasWebGPUFeature(sessionObject) {
		try {
			const enabledFeatures = sessionObject && sessionObject.enabledFeatures;
			if (!enabledFeatures || typeof enabledFeatures === "string" ||
				typeof enabledFeatures[Symbol.iterator] !== "function")
				throw new TypeError("enabledFeatures is not iterable");
			return Array.from(enabledFeatures).includes("webgpu");
		} catch (_) {
			throw providerError("feature-negotiation-unobservable", "negotiating-features",
				"WebXR did not expose enabled session features; retry with Force WebGL compatibility bridge");
		}
	}

	function bridgeBlockingTimingPreference() {
		if (configuredBridgeBlockingTiming !== null) return configuredBridgeBlockingTiming;
		return root.surrealXRBridgeBlockingTiming === true;
	}

	function bridgeRotationReprojectionPreference() {
		if (configuredBridgeRotationReprojection !== null)
			return configuredBridgeRotationReprojection;
		return root.surrealXRBridgeRotationReprojection === true;
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

	function detachAudioGestureListener(sessionObject) {
		const attachedSession = audioGestureSession;
		const listener = audioGestureListener;
		audioGestureSession = null;
		audioGestureListener = null;
		if (!attachedSession || attachedSession !== sessionObject || !listener ||
			typeof attachedSession.removeEventListener !== "function") return;
		try { attachedSession.removeEventListener("select", listener); } catch (_) {}
	}

	function attachAudioGestureListener(sessionObject, generation) {
		detachAudioGestureListener(audioGestureSession);
		if (!sessionObject || typeof sessionObject.addEventListener !== "function") return;
		const listener = function (event) {
			if (event && event.isTrusted === true && generation === activeGeneration &&
				status.active && session === sessionObject && typeof root.dispatchEvent === "function" &&
				typeof root.Event === "function") {
				// Forward only the fact that a trusted XR activation occurred. Controller,
				// input-source, pose, button, and profile data never leave the XR provider.
				try { root.dispatchEvent(new root.Event("surrealwebxraudiogesture")); } catch (_) {}
			}
		};
		try {
			sessionObject.addEventListener("select", listener);
			audioGestureSession = sessionObject;
			audioGestureListener = listener;
		} catch (_) {}
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

	function requestNativeFrameClockReset() {
		if (root.surrealXRNativeCallsBlocked === true || nativeRenderPromise) {
			frameClockResetPending = true;
			return false;
		}
		frameClockResetPending = false;
		try { moduleCall("Surreal_ResetBrowserFrameClock", null); return true; }
		catch (_) { return false; }
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

	function resetHapticStatus(generation) {
		hapticStatus = {
			generation: generation || 0,
			submissions: 0,
			dispatched: 0,
			rejected: 0,
			asyncRejected: 0,
			droppedInactive: 0,
			droppedDisconnected: 0,
			droppedUnsupported: 0,
			lastHand: null,
			lastAmplitude: null,
			lastDurationMilliseconds: null,
			lastActuator: null,
		};
	}

	function hapticTarget(handedness) {
		if (!session || !status.active || !activeGeneration || !isActionFocused(session))
			return { status: "inactive" };
		let sawConnected = false;
		for (const source of Array.from(session.inputSources || [])) {
			if (!source || source.handedness !== handedness) continue;
			const gamepad = source.gamepad;
			if (!gamepad || gamepad.connected === false) continue;
			sawConnected = true;
			try {
				const actuators = Array.from(gamepad.hapticActuators || []);
				if (gamepad.vibrationActuator && !actuators.includes(gamepad.vibrationActuator))
					actuators.push(gamepad.vibrationActuator);
				for (const actuator of actuators) {
					if (actuator && typeof actuator.pulse === "function")
						return { status: "supported", actuator, mode: "pulse" };
					if (actuator && typeof actuator.playEffect === "function")
						return { status: "supported", actuator, mode: "playEffect" };
				}
			} catch (_) { /* A capability accessor may disappear with its input source. */ }
		}
		return { status: sawConnected ? "unsupported" : "disconnected" };
	}

	function hapticCapability(handedness) {
		const target = hapticTarget(handedness);
		return Object.freeze({
			connected: target.status === "supported" || target.status === "unsupported",
			supported: target.status === "supported",
			mode: target.status === "supported" ? target.mode : null,
			status: target.status,
		});
	}

	function snapshotHapticStatus() {
		return Object.assign({}, hapticStatus, {
			policy: HAPTIC_POLICY,
			left: hapticCapability("left"),
			right: hapticCapability("right"),
		});
	}

	resetHapticStatus(0);

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
		updateInputDiagnostics(currentSession, actionFocused, sources);
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
		scheduleRenderPump(activeGeneration);
	}

	function requireNativeFrameABI() {
		const query = root.Module && root.Module._Surreal_GetWebXRFrameABIVersion;
		if (typeof query !== "function" || query() !== ABI.version)
			throw providerError("incompatible-native-frame-abi", "activating-session",
				"native WebXR frame ABI does not support current-pose phased presentation");
	}

	function enqueueInputPacket(packet) {
		if (inputQueueOverflow) return;
		const data = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
		const sourceCount = data.getUint32(8, true);
		const headerFlags = data.getUint32(12, true);
		const signature = [headerFlags, sourceCount];
		const connectedHands = [];
		for (let index = 0; index < sourceCount; index++) {
			const base = INPUT_ABI.headerBytes + index * INPUT_ABI.sourceBytes;
			const hand = data.getUint32(base, true);
			const connected = data.getUint32(base + 4, true) & INPUT_SOURCE_FLAGS.connected;
			if (connected) connectedHands.push(hand);
			signature.push(hand, connected,
				data.getUint32(base + 8, true), data.getUint32(base + 12, true));
		}
		connectedHands.sort();
		const safety = {
			sessionActive: (headerFlags & INPUT_HEADER_FLAGS.sessionActive) !== 0,
			actionFocused: (headerFlags & INPUT_HEADER_FLAGS.actionFocused) !== 0,
			connectedHands: connectedHands.join(","),
		};
		const disconnected = lastQueuedInputSafety && lastQueuedInputSafety.connectedHands
			.split(",").filter(Boolean).some(hand => !connectedHands.includes(Number(hand)));
		const safetyBarrier = !safety.sessionActive || !safety.actionFocused || disconnected;
		if (safetyBarrier) {
			queuedInputPackets = [];
			lastQueuedInputSignature = null;
		}
		lastQueuedInputSafety = safety;
		const discrete = signature.join(":");
		const transition = discrete !== lastQueuedInputSignature;
		lastQueuedInputSignature = discrete;
		// A continuous pose/axis sample immediately before a discrete edge is
		// redundant because the edge packet also contains the newest pose/axes.
		if (transition && queuedInputPackets.length && !queuedInputPackets.at(-1).transition)
			queuedInputPackets.pop();
		if (transition) {
			if (queuedInputPackets.length >= INPUT_QUEUE_LIMIT) { inputQueueOverflow = true; return; }
			queuedInputPackets.push({ packet, transition: true, safetyBarrier });
		}
		else if (queuedInputPackets.length && !queuedInputPackets.at(-1).transition)
			queuedInputPackets[queuedInputPackets.length - 1] = { packet, transition: false, safetyBarrier };
		else {
			if (queuedInputPackets.length >= INPUT_QUEUE_LIMIT) { inputQueueOverflow = true; return; }
			queuedInputPackets.push({ packet, transition: false, safetyBarrier });
		}
		// Never silently discard a button/focus/connect edge. A pathological stall
		// fails closed instead of leaving a logically stuck button in native input.
	}

	function takeInputPacketForSimulationFrame() {
		if (!queuedInputPackets.length) return null;
		if (queuedInputPackets[0].transition)
			return queuedInputPackets.shift().packet;
		const latest = queuedInputPackets.at(-1).packet;
		queuedInputPackets = [];
		return latest;
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

	function capturePresentationSource(capture) {
		const source = {
			sequence: capture.sequence,
			time: capture.time,
			resetGeneration: capture.resetGeneration,
		};
		if (!status.bridgeRotationReprojectionRequested) return Object.freeze(source);
		if (capture.atlasViews) {
			const atlasViews = new Array(capture.atlasViews.length);
			for (let index = 0; index < atlasViews.length; index++) {
				const view = capture.atlasViews[index];
				atlasViews[index] = Object.freeze({ x: view.x, y: view.y,
					width: view.width, height: view.height });
			}
			source.atlasViews = Object.freeze(atlasViews);
		} else source.atlasViews = null;
		const views = new Array(capture.views.length);
		for (let index = 0; index < views.length; index++) {
			const view = capture.views[index];
			views[index] = Object.freeze({ eye: view.eye,
				projectionMatrix: Object.freeze(view.projectionMatrix.slice()),
				transform: Object.freeze({
					position: Object.freeze(Object.assign({}, view.transform.position)),
					orientation: Object.freeze(Object.assign({}, view.transform.orientation)),
				}),
			});
		}
		source.views = Object.freeze(views);
		return Object.freeze(source);
	}

	function createPersistentTargetSets(layout) {
		const device = webGPUDevice();
		if (!device || typeof device.createTexture !== "function")
			throw providerError("webgpu-device-not-ready", "frame", "WebGPU cannot allocate persistent WebXR targets");
		return [0, 1].map(setIndex => {
			const textures = layout.mode === "webgl-bridge" ? [device.createTexture({
				label: "Surreal WebXR atlas " + setIndex, size: [layout.width, layout.height, 1],
				format: layout.format, usage: textureUsage(),
			})] : layout.views.map(view => device.createTexture({
				label: "Surreal WebXR " + view.eye + " " + setIndex,
				size: [view.width, view.height, 1], format: layout.format, usage: textureUsage(),
			}));
			return Object.freeze({ textures, presentation: { source: null, count: 0 } });
		});
	}

	function recordBridgePresentation(targetSet, currentSequence, currentTime) {
		const presentation = targetSet && targetSet.presentation;
		const source = presentation && presentation.source;
		if (!source) return;
		const ageFrames = boundedCounter(currentSequence - source.sequence, POSE_AGE_LIMITS.frames);
		const elapsed = Number.isFinite(currentTime) && Number.isFinite(source.time) ?
			Math.max(0, currentTime - source.time) : null;
		const ageMs = boundedNonnegative(elapsed, POSE_AGE_LIMITS.milliseconds);
		if (presentation.count > 0)
			bridgePoseAge.reusedPresents = boundedCounter(
				bridgePoseAge.reusedPresents + 1, POSE_AGE_LIMITS.presentations);
		presentation.count = boundedCounter(presentation.count + 1, POSE_AGE_LIMITS.presentations);
		bridgePoseAge.presentAgeFrames = ageFrames;
		bridgePoseAge.maxPresentAgeFrames = bridgePoseAge.maxPresentAgeFrames === null ? ageFrames :
			Math.max(bridgePoseAge.maxPresentAgeFrames, ageFrames);
		bridgePoseAge.presentAgeMs = ageMs;
		if (ageMs !== null)
			bridgePoseAge.maxPresentAgeMs = bridgePoseAge.maxPresentAgeMs === null ? ageMs :
				Math.max(bridgePoseAge.maxPresentAgeMs, ageMs);
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
			// Chromium supplies XRView projection matrices in WebGPU's [0, 1]
			// depth range when XRGPUBinding is the active graphics API.
			flags = FRAME_FLAGS.projectionDepthZeroToOne;
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
		if (generation !== activeGeneration || nativeRenderPromise ||
			!persistentTargets || !targetLayout) return;
		if (frameClockResetPending) requestNativeFrameClockReset();
		if (inputQueueOverflow) {
			fail(generation, providerError("input-transition-overflow", "frame",
				"WebXR input transition queue overflowed while native rendering was suspended"));
			return;
		}
		const preparedIsCurrent = nativeFramePrepared && preparedFrame &&
			preparedFrame.epoch === layoutEpoch &&
			preparedFrame.resetGeneration === resetGeneration;
		const safetyInputOnly = preparedIsCurrent && queuedInputPackets.length === 1 &&
			queuedInputPackets[0].safetyBarrier;
		if (preparedIsCurrent && !safetyInputOnly) return;
		if (!nativeFramePrepared && !nativeFrameRendered && !latestFrameCapture &&
			!(queuedInputPackets.length === 1 && queuedInputPackets[0].safetyBarrier)) return;
		setNativeCallsBlocked(true);
		const promise = (async function () {
			if (safetyInputOnly) {
				const safetyInput = takeInputPacketForSimulationFrame();
				if (safetyInput) submitInputPacket(safetyInput);
				return;
			}
			if (nativeFramePrepared || nativeFrameRendered) {
				const completed = await moduleCall("Surreal_CompleteWebXRFrame", "number", [], [], { async: true });
				if (completed !== 1) throw new Error("native WebXR frame completion failed");
				nativeFramePrepared = false;
				nativeFrameRendered = false;
				preparedFrame = null;
			}
			const input = takeInputPacketForSimulationFrame();
			if (input) submitInputPacket(input);
			if (!latestFrameCapture) return;
			const capture = latestFrameCapture;
			latestFrameCapture = null;
			const target = persistentTargets[frontTargetIndex === 0 ? 1 : 0];
			const packed = packPersistentFrame(capture, target);
			const prepared = await moduleCall("Surreal_PrepareWebXRFrame", "number",
				["array", "number"], [packed.packet, packed.packet.byteLength], { async: true });
			if (prepared !== 1) {
				const error = moduleCall("Surreal_GetWebXRFrameLastError", "number");
				throw providerError("native-frame-prepare-rejected-" + error, "frame",
					"native WebXR frame preparation failed (error=" + error + ")");
			}
			if (generation === activeGeneration && capture.epoch === layoutEpoch &&
				capture.resetGeneration === resetGeneration) {
				nativeFramePrepared = true;
				preparedFrame = capture;
			}
		})().catch(function (error) {
			if (generation === activeGeneration)
				fail(generation, error instanceof WebXRProviderError ? error :
					providerError("frame-failed", "frame", error.message || String(error)));
		}).finally(function () {
			if (nativeRenderPromise === promise) nativeRenderPromise = null;
			setNativeCallsBlocked(false);
			if (frameClockResetPending) requestNativeFrameClockReset();
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
		detachAudioGestureListener(finishedSession);
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
		nativeFramePrepared = false;
		nativeFrameRendered = false;
		preparedFrame = null;
		queuedInputPackets = [];
		lastQueuedInputSignature = null;
		lastQueuedInputSafety = null;
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
		status.inputDiagnostics = null;
		inputDiagnostics = null;
		status.lastError = error ? String(error.message || error) : null;
		status.lastErrorCode = error ? (error.code || "webxr-provider-failed") : null;
		status.lastErrorStage = errorStage;
		status.endedSessions++;
		if (error) log("WebXR session failed: " + error);
		trackCleanup((async function () {
			try { await Promise.resolve(pendingRender); } catch (_) {}
			if (nativePresentationStarted) {
				try {
					await moduleCall("Surreal_CompleteWebXRFrame", "number", [], [], { async: true });
				} catch (_) {}
			}
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
		})());
		recordTransition(error ? "entry-or-session-failed" : "session-ended", generation,
			errorStage || status.phase);
		return true;
	}

	function createSessionEndTracker(sessionObject, generation) {
		let resolveEnded;
		const tracker = {
			generation,
			ended: false,
			blocked: false,
			applicationEndRequested: false,
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
			let prematureEnd = null;
			if (generation === activeGeneration && !tracker.applicationEndRequested) {
				if (!status.active) {
					prematureEnd = providerError("session-ended-before-activation",
						status.currentStage || "session-reserved",
						"WebXR session ended before presentation activation completed");
				} else if (status.frames === 0) {
					prematureEnd = providerError("session-ended-before-first-frame", "frame",
						"WebXR session ended before its first rendered frame completed");
				}
			}
			finish(generation, prematureEnd ? "error" : "ended", prematureEnd);
		});
		return tracker;
	}

	function drainSessionEnd(ending, endTracker, failureMessage) {
		const localCleanup = cleanupPending;
		trackCleanup(Promise.all([
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
		]).then(function () {}));
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

	function onNativeCallGateOverflow() {
		if (!activeGeneration) return;
		fail(activeGeneration, providerError("native-callback-overflow", "frame",
			"Browser callback transitions overflowed while native rendering was suspended"));
	}
	if (typeof root.addEventListener === "function")
		root.addEventListener("surrealnativecallgateoverflow", onNativeCallGateOverflow);

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
			const views = new Array(xrViews.length);
			for (let index = 0; index < xrViews.length; index++)
				views[index] = snapshotView(xrViews[index]);
			const frameSequence = ++captureSequence;
			enqueueInputPacket(packInputSnapshot(time, session, frame, referenceSpace));
			let description;
			if (presentationMode === "webgl-bridge") {
				description = webGLBridge.describeFrame(pose);
				description.mode = "webgl-bridge";
				description.format = webGLBridge.textureFormat;
			} else {
				description = directFrameDescription(xrViews);
			}
			adoptLayout(description);
			const capture = { generation, epoch: layoutEpoch, sequence: frameSequence, time, views,
				resetGeneration, atlasViews: description.atlasViews || null };
			let renderedCurrentPose = false;
			if (nativeFramePrepared && preparedFrame &&
				preparedFrame.epoch === layoutEpoch &&
				preparedFrame.resetGeneration === resetGeneration) {
				const backIndex = frontTargetIndex === 0 ? 1 : 0;
				const target = persistentTargets[backIndex];
				const packed = packPersistentFrame(capture, target);
				root.surrealWebXRFrameTextures = packed.textures;
				let rendered;
				try {
					rendered = moduleCall("Surreal_RenderWebXRFrame", "number",
						["array", "number"], [packed.packet, packed.packet.byteLength]);
				} finally {
					root.surrealWebXRFrameTextures = null;
				}
				if (rendered !== 1) {
					const error = moduleCall("Surreal_GetWebXRFrameLastError", "number");
					throw providerError("native-frame-render-rejected-" + error, "frame",
						"native synchronous WebXR render failed (error=" + error + ")");
				}
				target.presentation.source = capturePresentationSource(capture);
				target.presentation.count = 0;
				frontTargetIndex = backIndex;
				nativeFramePrepared = false;
				nativeFrameRendered = true;
				preparedFrame = null;
				status.frames++;
				renderedCurrentPose = true;
				if (presentationMode === "webgl-bridge") {
					let metadata;
					if (status.bridgeRotationReprojectionRequested) metadata = {
						sourceViews: views, sourceAtlasViews: capture.atlasViews,
						currentViews: views,
					};
					webGLBridge.present(description, target.textures[0], webGPUDevice(), metadata);
					recordBridgePresentation(target, frameSequence, time);
					setBridgeDiagnostics(webGLBridge.diagnostics());
				} else {
					presentDirect(description);
				}
			}
			if (!renderedCurrentPose) {
				status.skippedFrames++;
				if (presentationMode === "webgl-bridge") {
					webGLBridge.clear();
					setBridgeDiagnostics(webGLBridge.diagnostics());
				}
			}
			if (latestFrameCapture) status.skippedFrames++;
			latestFrameCapture = capture;
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
		const preference = presentationPreference();
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
			presentationPreference: preference,
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
		result.preferredMode = selectPresentationMode(preference, result.directWebGPU, result.webGLBridge);
		if (preference === "webgl-bridge" && !result.webGLBridge)
			result.reasons.push("webgl-bridge-unavailable");
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

	root.surrealXRSetPresentationPreference = function (value) {
		if (value !== "auto" && value !== "webgl-bridge")
			throw new TypeError("WebXR presentation preference must be 'auto' or 'webgl-bridge'");
		if (session || enterPending || activationPending)
			throw providerError("presentation-preference-active", "preflight",
				"WebXR presentation preference cannot change while a session is active or pending");
		configuredPresentationPreference = value;
		status.presentationPreference = value;
		return value;
	};

	root.surrealXRSetBridgeBlockingTiming = function (enabled) {
		if (enabled !== true && enabled !== false)
			throw new TypeError("WebXR bridge blocking timing must be a boolean");
		if (session || enterPending || activationPending)
			throw providerError("bridge-timing-active", "preflight",
				"WebXR bridge timing cannot change while a session is active or pending");
		configuredBridgeBlockingTiming = enabled;
		status.bridgeBlockingTimingRequested = enabled;
		return enabled;
	};

	root.surrealXRSetBridgeRotationReprojection = function (enabled) {
		if (enabled !== true && enabled !== false)
			throw new TypeError("WebXR bridge rotation reprojection must be a boolean");
		if (session || enterPending || activationPending)
			throw providerError("bridge-reprojection-active", "preflight",
				"WebXR bridge rotation reprojection cannot change while a session is active or pending");
		configuredBridgeRotationReprojection = enabled;
		status.bridgeRotationReprojectionRequested = enabled;
		return enabled;
	};

	root.surrealXRIsColorFormatSupported = function (format) {
		return format === "bgra8unorm" || format === "rgba8unorm" || format === "rgba16float";
	};

	root.surrealXRGetHapticCapabilities = function () {
		return snapshotHapticStatus();
	};

	root.surrealXRSubmitHaptic = function (hand, amplitude, durationMilliseconds, frequencyHz) {
		const handedness = hand === 0 ? "left" : (hand === 1 ? "right" : null);
		amplitude = Number(amplitude);
		durationMilliseconds = Number(durationMilliseconds);
		frequencyHz = Number(frequencyHz);
		hapticStatus.submissions++;
		if (!handedness || !Number.isFinite(amplitude) || amplitude <= 0 || amplitude > 1 ||
			!Number.isFinite(durationMilliseconds) || durationMilliseconds <= 0 ||
			!Number.isFinite(frequencyHz) || frequencyHz < 0) {
			hapticStatus.rejected++;
			return false;
		}
		durationMilliseconds = Math.round(Math.max(HAPTIC_POLICY.minimumDurationMilliseconds,
			Math.min(HAPTIC_POLICY.maximumDurationMilliseconds, durationMilliseconds)));
		const target = hapticTarget(handedness);
		if (target.status !== "supported") {
			const field = target.status === "inactive" ? "droppedInactive" :
				(target.status === "unsupported" ? "droppedUnsupported" : "droppedDisconnected");
			hapticStatus[field]++;
			return false;
		}

		const generation = activeGeneration;
		let result;
		try {
			result = target.mode === "pulse" ? target.actuator.pulse(amplitude, durationMilliseconds) :
				target.actuator.playEffect("dual-rumble", {
					duration: durationMilliseconds,
					startDelay: 0,
					strongMagnitude: amplitude,
					weakMagnitude: amplitude,
				});
		} catch (_) {
			hapticStatus.rejected++;
			return false;
		}
		if (result === false) {
			hapticStatus.rejected++;
			return false;
		}
		if (result && typeof result.then === "function") {
			Promise.resolve(result).then(function (accepted) {
				if (accepted === false && generation === activeGeneration && generation === hapticStatus.generation)
					hapticStatus.asyncRejected++;
			}).catch(function () {
				if (generation === activeGeneration && generation === hapticStatus.generation)
					hapticStatus.asyncRejected++;
			});
		}
		hapticStatus.dispatched++;
		hapticStatus.lastHand = handedness;
		hapticStatus.lastAmplitude = amplitude;
		hapticStatus.lastDurationMilliseconds = durationMilliseconds;
		hapticStatus.lastActuator = target.mode;
		return true;
	};

	root.surrealXRRequestSession = async function () {
		// Admission is synchronous so navigator.xr.requestSession remains in the
		// trusted button activation. The UI keeps entry disabled while cleanup is
		// pending instead of consuming activation by awaiting cleanup here.
		if (cleanupInProgress || sessionEndBlocked || session || enterPending) return false;
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
		const preference = presentationPreference();
		status.presentationPreference = preference;
		status.bridgeBlockingTimingRequested = bridgeBlockingTimingPreference();
		status.bridgeRotationReprojectionRequested = bridgeRotationReprojectionPreference();
		const bridgeAvailable = typeof root.XRWebGLLayer === "function" && root.SurrealWebXRWebGLBridge &&
			root.SurrealWebXRWebGLBridge.canCreateWebGL2(root);
		const directAvailable = typeof root.XRGPUBinding === "function" &&
			root.surrealWebGPUDeviceXRCompatible === true;
		const negotiatePresentationMode = preference === "auto" && directAvailable && bridgeAvailable;
		presentationMode = negotiatePresentationMode ? null :
			selectPresentationMode(preference, directAvailable, bridgeAvailable);
		if (!preflightError && !presentationMode && !negotiatePresentationMode) {
			if (preference === "webgl-bridge")
				preflightError = providerError("webxr-webgl-bridge-unavailable", "preflight",
					"WebGL compatibility bridge was requested but is unavailable");
			else {
				const missingBinding = typeof root.XRGPUBinding !== "function";
				preflightError = providerError(missingBinding ? "webxr-webgpu-binding-unavailable" :
					"no-webxr-presentation-backend", "preflight",
					"Neither direct WebGPU WebXR layers nor the XRWebGLLayer compatibility bridge is available");
			}
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
		resetInputDiagnostics();
		captureSequence = 0;
		resetBridgePoseAge();
		resetHapticStatus(generation);
		let requestedSession = null;
		try {
			try {
				const sessionOptions = negotiatePresentationMode ?
					{ optionalFeatures: ["local-floor", "webgpu"] } :
					(presentationMode === "direct-webgpu" ?
						{ requiredFeatures: ["webgpu"], optionalFeatures: ["local-floor"] } :
						{ optionalFeatures: ["local-floor"] });
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
			if (negotiatePresentationMode) {
				setStage("negotiate-features");
				presentationMode = sessionHasWebGPUFeature(session) ? "direct-webgpu" : "webgl-bridge";
				status.presentationMode = presentationMode;
			}
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
		const activatingSession = session;
		activationPending = true;
		try {
			requireNativeFrameABI();
			session.addEventListener("inputsourceschange", function (event) {
				if (generation === activeGeneration && event && event.removed && event.removed.length) submitCurrentInput(0);
			});
			session.addEventListener("visibilitychange", function () {
				if (generation !== activeGeneration) return;
				if (!isActionFocused(session)) submitCurrentInput(0);
				else requestNativeFrameClockReset();
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
					const createdBridge = await root.SurrealWebXRWebGLBridge.create({ root, session: activatingSession,
						canvas: root.Module && root.Module.canvas, device: webGPUDevice(),
						blockingTiming: status.bridgeBlockingTimingRequested,
						rotationReprojection: status.bridgeRotationReprojectionRequested });
					if (generation !== activeGeneration || session !== activatingSession) {
						try { createdBridge.destroy(); } catch (_) {}
						return false;
					}
					webGLBridge = createdBridge;
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
					if (generation === activeGeneration) {
						resetGeneration++;
						frontTargetIndex = null;
						latestFrameCapture = null;
						resetBridgePoseAge();
					}
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
			attachAudioGestureListener(session, generation);
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
		// Compatibility callers may queue programmatic re-entry while teardown is
		// draining. The production trusted-click controller calls RequestSession
		// directly and never takes this await-before-request path.
		if (cleanupInProgress) await cleanupPending;
		if (!await root.surrealXRRequestSession()) return false;
		return root.surrealXRActivateReservedSession();
	};

	root.surrealXRExit = function () {
		if (!session) return false;
		const generation = activeGeneration;
		const exitingSession = session;
		const exitingEndTracker = sessionEndTracker;
		if (exitingEndTracker) exitingEndTracker.applicationEndRequested = true;
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
		result.presentationPreference = presentationPreference();
		if (status.capabilities)
			result.capabilities = Object.assign({}, status.capabilities,
				{ reasons: status.capabilities.reasons.slice() });
		result.transitions = status.transitions.slice();
		result.bridgeDiagnostics = copyBridgeDiagnostics(status.bridgeDiagnostics);
		result.inputDiagnostics = copyInputDiagnostics(status.inputDiagnostics);
		result.sessionEndBlocked = sessionEndBlocked;
		result.cleanupPending = cleanupInProgress;
		result.inputQueueDepth = queuedInputPackets.length;
		result.inputQueueOverflow = inputQueueOverflow;
		result.haptics = snapshotHapticStatus();
		return result;
	};
	root.surrealXRFrameABI = ABI;
	root.surrealXRInputABI = INPUT_ABI;
	root.surrealXRPackInputSnapshot = packInputSnapshot;
	setNativeCallsBlocked(false);
})(typeof window !== "undefined" ? window : globalThis);
