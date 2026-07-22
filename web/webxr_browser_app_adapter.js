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
		if (typeof host.XRGPUBinding !== "function")
			return result(false, "webgpu-webxr-unavailable", "This browser does not expose WebGPU-backed WebXR layers.");
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
		let adapterCompatible = true;
		return Object.freeze({
			id: "webxr",
			label: "Immersive WebXR",
			requiresXRCompatibleAdapter: true,
			isAvailable: () => detected.available === true && adapterCompatible,
			setXRCompatibleAdapter: (available, detail) => {
				adapterCompatible = available === true;
				publishAdapter(host, adapterCompatible, detail);
				if (!adapterCompatible) {
					const message = "Immersive WebXR is disabled because an XR-compatible WebGPU adapter was unavailable" +
						(detail ? ": " + detail : ".");
					if (host && typeof host.dispatchEvent === "function" && typeof host.CustomEvent === "function") {
						host.dispatchEvent(new host.CustomEvent("surrealwebxrcapability", {
							detail: result(false, "xr-compatible-adapter-unavailable", message),
						}));
					}
				}
			},
			activate: async ({ Module }) => {
				host.surrealWebGPUDevice = Module && Module.preinitializedWebGPUDevice;
				try {
					const entered = typeof host.surrealXREnter === "function" && await host.surrealXREnter();
					if (!entered) {
						const state = typeof host.surrealXRGetState === "function" ? host.surrealXRGetState() : null;
						const message = state && state.lastError || "The immersive session was rejected. Continuing in the flat window.";
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
