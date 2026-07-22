(function (global) {
	"use strict";

	const diagnostics = {
		supported: "serviceWorker" in navigator,
		enabled: false,
		state: "not-requested",
		version: null,
		error: null,
		updateFound: false,
	};

	function snapshot() {
		return Object.assign({}, diagnostics);
	}

	function publish(log) {
		global.dispatchEvent(new CustomEvent("surrealpwa", { detail: snapshot() }));
		if (typeof log === "function") log("[pwa] " + diagnostics.state +
			(diagnostics.error ? ": " + diagnostics.error : ""));
	}

	async function queryWorker(worker) {
		if (!worker) return null;
		return new Promise(resolve => {
			const channel = new MessageChannel();
			const timeout = setTimeout(() => resolve(null), 2000);
			channel.port1.onmessage = event => {
				clearTimeout(timeout);
				resolve(event.data || null);
			};
			worker.postMessage({ type: "SURREAL_PWA_STATUS" }, [channel.port2]);
		});
	}

	async function register(options) {
		const settings = options || {};
		diagnostics.enabled = !!settings.enabled;
		if (!diagnostics.enabled) {
			diagnostics.state = settings.refusalReason || "disabled";
			publish(settings.log);
			return snapshot();
		}
		if (!diagnostics.supported) {
			diagnostics.state = "unsupported";
			publish(settings.log);
			return snapshot();
		}

		diagnostics.state = "registering";
		publish(settings.log);
		try {
			const registration = await navigator.serviceWorker.register("service-worker.js", {
				scope: "./",
				updateViaCache: "none",
			});
			registration.addEventListener("updatefound", () => {
				diagnostics.updateFound = true;
				diagnostics.state = "update-found";
				publish(settings.log);
			});
			const ready = await navigator.serviceWorker.ready;
			const worker = ready.active || registration.active || registration.waiting || registration.installing;
			const status = await queryWorker(worker);
			diagnostics.version = status && status.version || null;
			diagnostics.state = navigator.serviceWorker.controller ? "active" : "installed-reload-pending";
			publish(settings.log);
			return snapshot();
		} catch (error) {
			diagnostics.state = "registration-failed";
			diagnostics.error = error && error.name ? error.name + ": " + error.message : String(error);
			publish(settings.log);
			return snapshot();
		}
	}

	navigator.serviceWorker && navigator.serviceWorker.addEventListener("message", event => {
		if (!event.data || event.data.type !== "SURREAL_PWA_ACTIVATED") return;
		diagnostics.version = event.data.version || null;
		diagnostics.state = "active";
		publish();
	});

	global.SurrealPWA = Object.freeze({ register, getDiagnostics: snapshot });
})(window);
