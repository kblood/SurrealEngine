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

	function onPointerLockChange() {
		const canvas = state.canvas || currentCanvas();
		const active = !!canvas && root.document.pointerLockElement === canvas;
		const lost = state.wasActive && !active;
		state.wasActive = active;
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
			root.document.addEventListener("pointerlockchange", onPointerLockChange);
			root.document.addEventListener("pointerlockerror", function () {
				state.lastError = "pointerlockerror";
				updateControl();
			});
			root.addEventListener("blur", onPageBlur);
			root.addEventListener("focus", onPageFocus);
			state.installed = true;
		}
		state.wasActive = root.document.pointerLockElement === canvas;
		updateControl();
		return true;
	}

	function setRequested(requested) {
		state.requested = requested === true;
		install();
		if (!state.requested) exitPointerLock();
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
		if (state.xrActive) exitPointerLock();
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
			lastError: state.lastError,
		});
	}

	root.SurrealBrowserPointerLock = Object.freeze({ setRequested, setInteractive, setXRActive, status });
})(typeof window !== "undefined" ? window : globalThis);
