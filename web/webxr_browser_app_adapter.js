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
		const direct = typeof host.XRGPUBinding === "function";
		const bridge = typeof host.XRWebGLLayer === "function" && host.SurrealWebXRWebGLBridge &&
			host.SurrealWebXRWebGLBridge.canCreateWebGL2(host);
		if (!direct && !bridge)
			return result(false, "webxr-presentation-unavailable", "No direct WebGPU or WebGL compatibility presentation path is available.");
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

		async function prepareLaunch() {
			reservationState = "requesting";
			reservationMessage = null;
			try {
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

	root.SurrealWebXRBrowserProvider = Object.freeze({ probe, createProvider });
})(typeof window !== "undefined" ? window : globalThis);
