(function (root) {
	"use strict";

	const state = {
		requested: false,
		interactive: true,
		xrActive: false,
		canvas: null,
		control: null,
		captureButton: null,
		message: null,
		installed: false,
		wasActive: false,
		programmaticExit: false,
		pageFocused: true,
		everCaptured: false,
		requestAttempts: 0,
		lossCount: 0,
		forwardedEscapeIntents: 0,
		bridgeOwnsMotion: false,
		pendingBridgeActive: null,
		pendingMouseReset: false,
		lastError: null,
	};

	function currentCanvas() {
		return (root.Module && root.Module.canvas) || root.document.getElementById("canvas") ||
			root.document.getElementById("game");
	}

	function recordFailure(error) {
		state.lastError = error && error.name ? error.name : "pointer-lock-failed";
		state.programmaticExit = false;
		updateControl();
	}

	function bindControl() {
		const control = root.document.querySelector("[data-pointer-lock-control]");
		const captureButton = root.document.querySelector("[data-pointer-lock-capture]");
		const message = root.document.querySelector("[data-pointer-lock-message]");
		if (captureButton !== state.captureButton) {
			if (state.captureButton) state.captureButton.removeEventListener("click", onCaptureClick);
			if (captureButton) captureButton.addEventListener("click", onCaptureClick);
		}
		state.control = control;
		state.captureButton = captureButton;
		state.message = message;
	}

	function updateControl() {
		const canvas = state.canvas || currentCanvas();
		const active = !!canvas && root.document.pointerLockElement === canvas;
		if (state.control) state.control.hidden = !(state.requested && state.interactive && !state.xrActive && !active);
		if (state.captureButton) {
			state.captureButton.textContent = state.everCaptured ? "Resume mouse look" : "Capture mouse";
			state.captureButton.disabled = false;
		}
		if (state.message) {
			state.message.textContent = state.lastError ?
				"Mouse capture was blocked. You can keep playing without capture or try again." :
				"Click to capture the mouse for looking around. Press Escape to release it and send Escape to the game.";
		}
	}

	function forwardEscapeIntent() {
		const Module = root.Module;
		try {
			if (Module && typeof Module._Surreal_ForwardBrowserEscape === "function") {
				Module._Surreal_ForwardBrowserEscape();
			} else if (Module && typeof Module.ccall === "function") {
				Module.ccall("Surreal_ForwardBrowserEscape", null, [], []);
			} else {
				state.lastError = "native-escape-unavailable";
				return false;
			}
			state.forwardedEscapeIntents++;
			return true;
		} catch (error) {
			recordFailure(error);
			return false;
		}
	}

	function nativeCallsAllowed() {
		return root.surrealXRNativeCallsBlocked !== true;
	}

	function relativeMotionBridgeReady() {
		const Module = root.Module;
		return !!Module && typeof Module._Surreal_ForwardBrowserMouseMotion === "function" &&
			typeof Module._Surreal_ResetBrowserMouseMotion === "function" &&
			typeof Module._Surreal_SetBrowserMouseMotionActive === "function";
	}

	function desiredBridgeActive() {
		const canvas = state.canvas || currentCanvas();
		return state.requested && !state.xrActive && !!canvas &&
			root.document.pointerLockElement === canvas;
	}

	function applyBridgeActive(active) {
		const desired = active === true;
		if (!relativeMotionBridgeReady()) {
			state.bridgeOwnsMotion = false;
			state.pendingBridgeActive = null;
			state.pendingMouseReset = false;
			return false;
		}
		if (!nativeCallsAllowed()) {
			state.pendingBridgeActive = desired;
			state.pendingMouseReset = true;
			return false;
		}
		try {
			root.Module._Surreal_SetBrowserMouseMotionActive(desired ? 1 : 0);
			state.bridgeOwnsMotion = desired;
			state.pendingBridgeActive = null;
			state.pendingMouseReset = false;
			return true;
		} catch (error) {
			state.pendingBridgeActive = desired;
			state.pendingMouseReset = true;
			recordFailure(error);
			return false;
		}
	}

	function synchronizeBridgeActive() {
		const desired = desiredBridgeActive();
		if (state.pendingBridgeActive !== null || state.bridgeOwnsMotion !== desired)
			return applyBridgeActive(desired);
		if (state.pendingMouseReset)
			return resetMouseMotion();
		return true;
	}

	function forwardMouseMotion(event) {
		synchronizeBridgeActive();
		const canvas = state.canvas || currentCanvas();
		if (!state.requested || state.xrActive || !nativeCallsAllowed() ||
			!relativeMotionBridgeReady() || !state.bridgeOwnsMotion || !canvas ||
			root.document.pointerLockElement !== canvas) return false;
		const dx = Number.isFinite(event.movementX) ? Math.trunc(event.movementX) : 0;
		const dy = Number.isFinite(event.movementY) ? Math.trunc(event.movementY) : 0;
		if (dx === 0 && dy === 0) return false;
		try {
			root.Module._Surreal_ForwardBrowserMouseMotion(dx, dy);
			return true;
		} catch (error) {
			recordFailure(error);
			return false;
		}
	}

	function resetMouseMotion() {
		if (!relativeMotionBridgeReady()) {
			state.pendingMouseReset = false;
			return false;
		}
		if (!nativeCallsAllowed()) {
			state.pendingMouseReset = true;
			return false;
		}
		try {
			root.Module._Surreal_ResetBrowserMouseMotion();
			state.pendingMouseReset = false;
			return true;
		} catch (error) {
			state.pendingMouseReset = true;
			recordFailure(error);
			return false;
		}
	}

	function pageHasInputFocus() {
		const visible = !root.document.visibilityState || root.document.visibilityState === "visible";
		const focused = typeof root.document.hasFocus !== "function" || root.document.hasFocus();
		return state.pageFocused && visible && focused;
	}

	function onPageBlur() {
		state.pageFocused = false;
	}

	function onPageFocus() {
		state.pageFocused = true;
	}

	function onNativeCallGateChange() {
		if (nativeCallsAllowed()) synchronizeBridgeActive();
	}

	function onPointerLockChange() {
		const canvas = state.canvas || currentCanvas();
		const active = !!canvas && root.document.pointerLockElement === canvas;
		const lost = state.wasActive && !active;
		state.wasActive = active;
		synchronizeBridgeActive();
		if (active) {
			state.everCaptured = true;
			state.programmaticExit = false;
			state.lastError = null;
		} else if (lost) {
			state.lossCount++;
			const programmatic = state.programmaticExit;
			state.programmaticExit = false;
			if (!programmatic && state.requested && !state.xrActive && pageHasInputFocus())
				forwardEscapeIntent();
		}
		updateControl();
	}

	function exitPointerLock() {
		if (!root.document.pointerLockElement || typeof root.document.exitPointerLock !== "function") return;
		state.programmaticExit = true;
		try {
			const pending = root.document.exitPointerLock();
			if (pending && typeof pending.catch === "function") pending.catch(recordFailure);
		} catch (error) {
			recordFailure(error);
		}
	}

	function requestFromGesture(event) {
		const canvas = state.canvas || currentCanvas();
		if (!event.isTrusted || !state.requested || !state.interactive || state.xrActive || !canvas ||
			root.document.pointerLockElement === canvas ||
			typeof canvas.requestPointerLock !== "function") return;
		state.requestAttempts++;
		state.lastError = null;
		synchronizeBridgeActive();
		if (typeof canvas.focus === "function") canvas.focus({ preventScroll: true });
		try {
			const pending = canvas.requestPointerLock();
			if (pending && typeof pending.catch === "function") pending.catch(recordFailure);
		} catch (error) {
			recordFailure(error);
		}
	}

	function onMouseDown(event) {
		const canvas = state.canvas || currentCanvas();
		if (event.target === canvas) requestFromGesture(event);
	}

	function onCaptureClick(event) {
		requestFromGesture(event);
	}

	function install() {
		const canvas = currentCanvas();
		if (!canvas) return false;
		state.canvas = canvas;
		bindControl();
		if (!state.installed) {
			root.document.addEventListener("mousedown", onMouseDown, true);
			canvas.addEventListener("mousemove", forwardMouseMotion, true);
			root.document.addEventListener("pointerlockchange", onPointerLockChange);
			root.document.addEventListener("pointerlockerror", function () {
				state.lastError = "pointerlockerror";
				updateControl();
			});
			root.addEventListener("blur", onPageBlur);
			root.addEventListener("focus", onPageFocus);
			root.addEventListener("surrealnativecallgatechange", onNativeCallGateChange);
			state.installed = true;
		}
		state.wasActive = root.document.pointerLockElement === canvas;
		synchronizeBridgeActive();
		updateControl();
		return true;
	}

	function setRequested(requested) {
		state.requested = requested === true;
		install();
		if (!state.requested) {
			synchronizeBridgeActive();
			exitPointerLock();
		} else synchronizeBridgeActive();
		updateControl();
		return state.requested;
	}

	function setInteractive(interactive) {
		state.interactive = interactive === true;
		install();
		updateControl();
		return state.interactive;
	}

	function setXRActive(active) {
		state.xrActive = active === true;
		if (state.xrActive) {
			synchronizeBridgeActive();
			exitPointerLock();
		} else synchronizeBridgeActive();
		updateControl();
	}

	function status() {
		const canvas = state.canvas || currentCanvas();
		return Object.freeze({
			requested: state.requested,
			interactive: state.interactive,
			xrActive: state.xrActive,
			active: !!canvas && root.document.pointerLockElement === canvas,
			pageFocused: state.pageFocused,
			promptVisible: !!state.control && !state.control.hidden,
			requestAttempts: state.requestAttempts,
			lossCount: state.lossCount,
			forwardedEscapeIntents: state.forwardedEscapeIntents,
			bridgeOwnsMotion: state.bridgeOwnsMotion,
			pendingBridgeActive: state.pendingBridgeActive,
			pendingMouseReset: state.pendingMouseReset,
			lastError: state.lastError,
		});
	}

	root.SurrealBrowserPointerLock = Object.freeze({
		setRequested, setInteractive, setXRActive, relativeMotionBridgeReady, status
	});
})(typeof window !== "undefined" ? window : globalThis);
