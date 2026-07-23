(function (root) {
	"use strict";

	const state = {
		requested: false,
		xrActive: false,
		canvas: null,
		installed: false,
		requestAttempts: 0,
		lastError: null,
	};

	function currentCanvas() {
		return (root.Module && root.Module.canvas) || root.document.getElementById("canvas") ||
			root.document.getElementById("game");
	}

	function recordFailure(error) {
		state.lastError = error && error.name ? error.name : "pointer-lock-failed";
	}

	function exitPointerLock() {
		if (!root.document.pointerLockElement || typeof root.document.exitPointerLock !== "function") return;
		try {
			const pending = root.document.exitPointerLock();
			if (pending && typeof pending.catch === "function") pending.catch(recordFailure);
		} catch (error) {
			recordFailure(error);
		}
	}

	function onMouseDown(event) {
		const canvas = state.canvas || currentCanvas();
		if (!event.isTrusted || !state.requested || state.xrActive || !canvas ||
			event.target !== canvas || root.document.pointerLockElement === canvas ||
			typeof canvas.requestPointerLock !== "function") return;
		state.requestAttempts++;
		state.lastError = null;
		try {
			const pending = canvas.requestPointerLock();
			if (pending && typeof pending.catch === "function") pending.catch(recordFailure);
		} catch (error) {
			recordFailure(error);
		}
	}

	function install() {
		const canvas = currentCanvas();
		if (!canvas) return false;
		state.canvas = canvas;
		if (!state.installed) {
			root.document.addEventListener("mousedown", onMouseDown, true);
			root.document.addEventListener("pointerlockerror", function () {
				state.lastError = "pointerlockerror";
			});
			state.installed = true;
		}
		return true;
	}

	function setRequested(requested) {
		state.requested = requested === true;
		install();
		if (!state.requested) exitPointerLock();
		return state.requested;
	}

	function setXRActive(active) {
		state.xrActive = active === true;
		if (state.xrActive) exitPointerLock();
	}

	function status() {
		const canvas = state.canvas || currentCanvas();
		return Object.freeze({
			requested: state.requested,
			xrActive: state.xrActive,
			active: !!canvas && root.document.pointerLockElement === canvas,
			requestAttempts: state.requestAttempts,
			lastError: state.lastError,
		});
	}

	root.SurrealBrowserPointerLock = Object.freeze({ setRequested, setXRActive, status });
})(typeof window !== "undefined" ? window : globalThis);
