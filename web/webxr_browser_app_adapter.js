/* Optional WebXR presentation extension for the shared browser launcher. */
(function (root) {
	"use strict";

	function result(available, code, message) {
		return Object.freeze({ available, code, message });
	}

	async function probe(environment) {
		const host = environment || root;
		if (host.isSecureContext === false)
			return result(false, "insecure-context", "WebXR requires HTTPS or localhost.");
		if (!host.navigator || !host.navigator.xr || typeof host.navigator.xr.isSessionSupported !== "function")
			return result(false, "webxr-unavailable", "This browser does not expose WebXR.");
		const directWebGL2 = typeof host.XRWebGLLayer === "function";
		const directWebGPU = host.surrealXRExperimentalWebGPU === true &&
			typeof host.XRGPUBinding === "function";
		const bridge = typeof host.XRWebGLLayer === "function" && host.SurrealWebXRWebGLBridge &&
			host.SurrealWebXRWebGLBridge.canCreateWebGL2(host);
		if (!directWebGL2 && !directWebGPU && !bridge)
			return result(false, "webxr-presentation-unavailable", "No WebXR presentation path is available.");
		try {
			return await host.navigator.xr.isSessionSupported("immersive-vr") ?
				result(true, "ready", "Immersive WebXR is available.") :
				result(false, "immersive-vr-unsupported", "No immersive WebXR device is available.");
		} catch (error) {
			return result(false, "webxr-probe-failed", "WebXR capability detection failed: " +
				(error && error.message ? error.message : String(error)));
		}
	}

	function publish(host, state, message) {
		if (host && typeof host.dispatchEvent === "function" && typeof host.CustomEvent === "function") {
			host.dispatchEvent(new host.CustomEvent("surrealwebxrpresentation", {
				detail: Object.freeze({ state, message }),
			}));
		}
	}

	const SESSION_STATES = Object.freeze({
		WAITING_FOR_ENGINE: "WaitingForEngine",
		FLAT_RUNNING: "FlatRunning",
		REQUESTING_SESSION: "RequestingSession",
		ACTIVATING_XR: "ActivatingXR",
		IMMERSIVE_RUNNING: "ImmersiveRunning",
		ENDING_XR: "EndingXR",
	});

	function errorMessage(error, fallback) {
		return error && error.message ? error.message : (error ? String(error) : fallback);
	}

	class WebXRSessionController {
		constructor(capability, uiRoot, environment) {
			this.host = environment || root;
			this.capability = capability || result(false, "not-probed", "WebXR has not been checked.");
			this.root = uiRoot || null;
			this.enterButton = this.root && this.root.querySelector("[data-xr-enter]");
			this.exitButton = this.root && this.root.querySelector("[data-xr-exit]");
			this.statusLabel = this.root && this.root.querySelector("[data-xr-session-status]");
			this.errorLabel = this.root && this.root.querySelector("[data-xr-session-error]");
			this.backend = this.root && this.root.querySelector("[data-xr-session-backend]");
			this.blockingTiming = this.root && this.root.querySelector("[data-xr-session-bridge-blocking-timing]");
			this.rotationReprojection = this.root && this.root.querySelector("[data-xr-session-bridge-rotation-reprojection]");
			this.Module = null;
			this.engineRunning = false;
			this.operation = 0;
			this.currentState = SESSION_STATES.WAITING_FOR_ENGINE;
			this.lastError = null;
			this._providerStateChanged = () => this.refreshFromProvider();
			this._gameExitRequested = () => { void this.exit(); };
			if (this.enterButton) this.enterButton.addEventListener("click", () => { void this.enter(); });
			if (this.exitButton) this.exitButton.addEventListener("click", () => { void this.exit(); });
			if (this.backend) this.backend.addEventListener("change", () => this._render());
			if (typeof this.host.addEventListener === "function")
				this.host.addEventListener("surrealwebxrproviderstate", this._providerStateChanged);
			if (typeof this.host.addEventListener === "function")
				this.host.addEventListener("surrealwebxrgameexit", this._gameExitRequested);
			this._render();
		}

		attachModule(Module) {
			this.Module = Module || null;
			return this;
		}

		updateCapability(capability) {
			this.capability = capability || result(false, "not-probed", "WebXR has not been checked.");
			this._render();
			return this.status();
		}

		engineStarted() {
			this.engineRunning = true;
			this._transition(SESSION_STATES.FLAT_RUNNING, null);
			if (this.root) this.root.hidden = false;
			this._refreshRuntimeCapability();
		}

		_refreshRuntimeCapability() {
			if (typeof this.host.surrealXRGetCapabilities !== "function") return;
			Promise.resolve(this.host.surrealXRGetCapabilities()).then(capabilities => {
				if (!this.engineRunning || !capabilities) return;
				this.updateCapability(result(capabilities.supported === true,
					capabilities.supported === true ? "ready" : "runtime-backend-unavailable",
					capabilities.supported === true ?
						"Immersive WebXR is ready through the running WebGL 2 renderer." :
						"Enter VR requires the WebGL 2 renderer in this production build."));
			}).catch(() => {});
		}

		_preference() {
			return this.backend && this.backend.value === "webgl-bridge" ? "webgl-bridge" : "auto";
		}

		_configureProvider() {
			const preference = this._preference();
			if (typeof this.host.surrealXRSetPresentationPreference === "function")
				this.host.surrealXRSetPresentationPreference(preference);
			else if (preference !== "auto")
				throw new Error("This WebXR provider cannot force the WebGL compatibility bridge.");
			const forcedBridge = preference === "webgl-bridge";
			const blocking = forcedBridge && !!this.blockingTiming && this.blockingTiming.checked;
			const reprojection = forcedBridge && !!this.rotationReprojection && this.rotationReprojection.checked;
			if (typeof this.host.surrealXRSetBridgeBlockingTiming === "function")
				this.host.surrealXRSetBridgeBlockingTiming(blocking);
			else if (blocking) throw new Error("This WebXR provider cannot enable QA blocking timing.");
			if (typeof this.host.surrealXRSetBridgeRotationReprojection === "function")
				this.host.surrealXRSetBridgeRotationReprojection(reprojection);
			else if (reprojection) throw new Error("This WebXR provider cannot enable rotation-only late reprojection.");
		}

		_transition(state, message) {
			this.currentState = state;
			this.lastError = message || null;
			this._render();
			publish(this.host, state, message || this._statusMessage());
		}

		_statusMessage() {
			const messages = {
				WaitingForEngine: "Start the game before entering VR.",
				FlatRunning: this.capability.available ? "Game running in the browser window. VR is ready." : this.capability.message,
				RequestingSession: "Waiting for the headset to grant an immersive session…",
				ActivatingXR: "Preparing immersive rendering without restarting the game…",
				ImmersiveRunning: "Immersive WebXR session active.",
				EndingXR: "Leaving VR and returning to the browser window…",
			};
			return messages[this.currentState] || this.currentState;
		}

		_render() {
			const flat = this.currentState === SESSION_STATES.FLAT_RUNNING;
			const immersive = this.currentState === SESSION_STATES.IMMERSIVE_RUNNING;
			const pending = this.currentState === SESSION_STATES.REQUESTING_SESSION ||
				this.currentState === SESSION_STATES.ACTIVATING_XR || this.currentState === SESSION_STATES.ENDING_XR;
			if (this.statusLabel) this.statusLabel.textContent = this._statusMessage();
			if (this.errorLabel) {
				this.errorLabel.hidden = !this.lastError;
				this.errorLabel.textContent = this.lastError || "";
			}
			if (this.enterButton) this.enterButton.disabled = !this.engineRunning || !flat || !this.capability.available;
			if (this.exitButton) {
				this.exitButton.hidden = !(immersive || this.currentState === SESSION_STATES.ENDING_XR);
				this.exitButton.disabled = !immersive;
			}
			if (this.backend) this.backend.disabled = !flat || pending;
			const bridge = flat && this._preference() === "webgl-bridge";
			if (this.blockingTiming) this.blockingTiming.disabled = !bridge || pending;
			if (this.rotationReprojection) this.rotationReprojection.disabled = !bridge || pending;
		}

		async enter() {
			if (!this.engineRunning || this.currentState !== SESSION_STATES.FLAT_RUNNING || !this.capability.available)
				return false;
			const operation = ++this.operation;
			try {
				this._configureProvider();
				this.host.surrealWebGPUDevice = this.Module && this.Module.preinitializedWebGPUDevice;
				this._transition(SESSION_STATES.REQUESTING_SESSION, null);
				// Keep requestSession in the trusted click call stack. Do not await any
				// preparation or cleanup before asking the browser for the session.
				if (typeof this.host.surrealXRRequestSession !== "function")
					throw new Error("The WebXR runtime provider is not loaded.");
				const request = this.host.surrealXRRequestSession();
				const reserved = await request;
				if (operation !== this.operation) return false;
				if (!reserved) throw new Error(this._providerError("The headset declined the immersive session."));
				this._transition(SESSION_STATES.ACTIVATING_XR, null);
				if (typeof this.host.surrealXRActivateReservedSession !== "function" ||
					!await this.host.surrealXRActivateReservedSession())
					throw new Error(this._providerError("Immersive presentation could not be activated."));
				if (operation !== this.operation) return false;
				this._transition(SESSION_STATES.IMMERSIVE_RUNNING, null);
				return true;
			} catch (error) {
				if (operation !== this.operation) return false;
				this._transition(SESSION_STATES.FLAT_RUNNING,
					errorMessage(error, "Immersive entry failed.") + " The game is still running in the browser window; you can retry.");
				return false;
			}
		}

		async exit() {
			if (this.currentState !== SESSION_STATES.IMMERSIVE_RUNNING) return false;
			const operation = ++this.operation;
			this._transition(SESSION_STATES.ENDING_XR, null);
			let accepted = false;
			try { accepted = typeof this.host.surrealXRExit === "function" && this.host.surrealXRExit(); }
			catch (error) {
				this._transition(SESSION_STATES.FLAT_RUNNING,
					errorMessage(error, "Could not end the immersive session.") + " The flat game remains available.");
				return false;
			}
			if (!accepted) {
				this._transition(SESSION_STATES.FLAT_RUNNING,
					this._providerError("The immersive session was already unavailable."));
				return false;
			}
			this._settleFlatWhenProviderReady(operation);
			return true;
		}

		_providerError(fallback) {
			const state = typeof this.host.surrealXRGetState === "function" ? this.host.surrealXRGetState() : null;
			return state && state.lastError || fallback;
		}

		_settleFlatWhenProviderReady(operation) {
			const state = typeof this.host.surrealXRGetState === "function" ? this.host.surrealXRGetState() : null;
			if (operation !== this.operation) return;
			if (!state || (!state.active && !state.cleanupPending && !state.sessionEndBlocked)) {
				this._transition(SESSION_STATES.FLAT_RUNNING, state && state.lastError || null);
				return;
			}
			if (typeof this.host.setTimeout === "function")
				this.host.setTimeout(() => this._settleFlatWhenProviderReady(operation), 25);
		}

		refreshFromProvider() {
			const state = typeof this.host.surrealXRGetState === "function" ? this.host.surrealXRGetState() : null;
			if ((this.currentState === SESSION_STATES.REQUESTING_SESSION ||
				this.currentState === SESSION_STATES.ACTIVATING_XR) && state && !state.active &&
				(state.phase === "error" || state.phase === "ended")) {
				const operation = ++this.operation;
				this._transition(SESSION_STATES.ENDING_XR, null);
				this._settleFlatWhenProviderReady(operation);
				return this.status();
			}
			if (this.currentState === SESSION_STATES.IMMERSIVE_RUNNING || this.currentState === SESSION_STATES.ENDING_XR)
				this._settleFlatWhenProviderReady(this.operation);
			return this.status();
		}

		status() {
			return Object.freeze({ state: this.currentState, engineRunning: this.engineRunning,
				available: this.capability.available === true, lastError: this.lastError });
		}
	}

	function createSessionController(capability, uiRoot, environment) {
		return new WebXRSessionController(capability, uiRoot, environment);
	}

	function publishAdapter(host, available, detail) {
		if (host && typeof host.dispatchEvent === "function" && typeof host.CustomEvent === "function") {
			host.dispatchEvent(new host.CustomEvent("surrealwebxradapter", {
				detail: Object.freeze({
					available: available === true,
					state: available === true ? "xr-compatible" : "flat-fallback",
					detail: detail ? String(detail) : null,
				}),
			}));
		}
	}

	function createProvider(capability, environment) {
		const host = environment || root;
		const detected = capability || result(false, "not-probed", "WebXR has not been checked.");
		let reservationState = "idle";
		let reservationMessage = null;

		function fallbackMessage() {
			const state = typeof host.surrealXRGetState === "function" ? host.surrealXRGetState() : null;
			return state && state.lastError || reservationMessage ||
				"The immersive session was rejected. Continuing in the flat window.";
		}

		function preferenceFromSelection(selection) {
			const preference = selection && selection.webXRPresentationPreference || "auto";
			if (preference !== "auto" && preference !== "webgl-bridge")
				throw new TypeError("WebXR presentation preference must be 'auto' or 'webgl-bridge'");
			return preference;
		}

		async function prepareLaunch(options) {
			reservationState = "requesting";
			reservationMessage = null;
			try {
				const preference = preferenceFromSelection(options && options.selection);
				if (typeof host.surrealXRSetPresentationPreference === "function")
					host.surrealXRSetPresentationPreference(preference);
				else if (preference !== "auto")
					throw new Error("This WebXR provider cannot force the WebGL compatibility bridge.");
				const blockingTiming = preference === "webgl-bridge" &&
					options && options.selection && options.selection.webXRBridgeBlockingTiming === true;
				if (typeof host.surrealXRSetBridgeBlockingTiming === "function")
					host.surrealXRSetBridgeBlockingTiming(blockingTiming);
				else if (blockingTiming)
					throw new Error("This WebXR provider cannot enable QA blocking timing.");
				const rotationReprojection = preference === "webgl-bridge" && options &&
					options.selection && options.selection.webXRBridgeRotationReprojection === true;
				if (typeof host.surrealXRSetBridgeRotationReprojection === "function")
					host.surrealXRSetBridgeRotationReprojection(rotationReprojection);
				else if (rotationReprojection)
					throw new Error("This WebXR provider cannot enable rotation-only late reprojection.");
				// This method is invoked directly by the Play submit event. Reserve the
				// immersive session here, while user activation is still eligible, but do
				// not create layers or transfer frame-loop ownership yet.
				const reserved = typeof host.surrealXRRequestSession === "function" &&
					await host.surrealXRRequestSession();
				if (reserved) {
					reservationState = "reserved";
					publish(host, "reserved", "Immersive WebXR session reserved while the game starts.");
					return Object.freeze({ reserved: true, fallback: null });
				}
				reservationState = "failed";
				reservationMessage = fallbackMessage();
				publish(host, "flat-fallback", reservationMessage);
				return Object.freeze({ reserved: false, fallback: "flat", message: reservationMessage });
			} catch (error) {
				reservationState = "failed";
				reservationMessage = (error && error.message ? error.message : String(error)) +
					" Continuing in the flat window.";
				publish(host, "flat-fallback", reservationMessage);
				return Object.freeze({ reserved: false, fallback: "flat", message: reservationMessage });
			}
		}
		return Object.freeze({
			id: "webxr",
			label: "Immersive WebXR",
			requiresXRCompatibleAdapter: false,
			prefersXRCompatibleAdapter: true,
			isAvailable: () => detected.available === true,
			setXRCompatibleAdapter: (available, detail) => {
				publishAdapter(host, available === true, detail);
			},
			prepareLaunch,
			activate: async ({ Module }) => {
				host.surrealWebGPUDevice = Module && Module.preinitializedWebGPUDevice;
				try {
					let entered = false;
					if (reservationState === "reserved" && typeof host.surrealXRActivateReservedSession === "function")
						entered = await host.surrealXRActivateReservedSession();
					else if (typeof host.surrealXRRequestSession !== "function" && typeof host.surrealXREnter === "function")
						entered = await host.surrealXREnter(); // Compatibility with older provider scripts.
					if (!entered) {
						const message = fallbackMessage();
						publish(host, "flat-fallback", message);
						return Object.freeze({ active: false, fallback: "flat", message });
					}
					publish(host, "active", "Immersive WebXR session active.");
					return Object.freeze({ active: true, fallback: null, message: "Immersive WebXR session active." });
				} catch (error) {
					const message = (error && error.message ? error.message : String(error)) + " Continuing in the flat window.";
					publish(host, "flat-fallback", message);
					return Object.freeze({ active: false, fallback: "flat", message });
				}
			},
		});
	}

	root.SurrealWebXRBrowserProvider = Object.freeze({ probe, createProvider, createSessionController,
		WebXRSessionController, SESSION_STATES });
})(typeof window !== "undefined" ? window : globalThis);
