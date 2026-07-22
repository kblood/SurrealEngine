/* WebXR presentation extension for the shared SurrealEngine browser launcher. */
(function (root) {
	"use strict";

	function createProvider() {
		return Object.freeze({
			id: "webxr",
			label: "WebXR headset",
			isAvailable: () => !!(root.navigator && root.navigator.xr &&
				typeof root.XRGPUBinding === "function" && typeof root.surrealXREnter === "function"),
			activate: async ({ Module }) => {
				// The shared launcher owns device creation. Keep the legacy global
				// populated while the provider migrates to the Module-owned seam.
				root.surrealWebGPUDevice = Module && Module.preinitializedWebGPUDevice;
				const entered = await root.surrealXREnter();
				if (!entered) {
					const state = typeof root.surrealXRGetState === "function" ? root.surrealXRGetState() : null;
					throw new Error(state && state.lastError || "The WebXR session could not be started.");
				}
			},
		});
	}

	root.SurrealWebXRBrowserProvider = Object.freeze({ createProvider });
})(typeof window !== "undefined" ? window : globalThis);
