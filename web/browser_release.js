/* Static-host release shell for the shared browser launcher. */
(function (root) {
	"use strict";

	function frozenCapability(available, code, message) {
		return Object.freeze({ available, code, message });
	}

	async function detectPlatformCapabilities(environment) {
		const host = environment || root;
		const navigator = host.navigator || {};
		const storage = navigator.storage || null;
		let persisted = null;
		if (storage && typeof storage.persisted === "function") {
			try { persisted = await storage.persisted(); } catch (_) { /* Diagnostic only. */ }
		}
		const storageBackend = storage && typeof storage.getDirectory === "function" ? "OPFS" :
			(host.indexedDB ? "IndexedDB" : null);
		const folderMode = typeof host.showDirectoryPicker === "function" ? "directory picker" :
			(host.File && host.Blob ? "folder upload fallback" : null);
		return Object.freeze({
			secureContext: frozenCapability(host.isSecureContext !== false,
				host.isSecureContext === false ? "insecure-context" : "ready",
				host.isSecureContext === false ? "HTTPS or localhost is required for immersive mode." : "Secure context ready."),
			webAssembly: frozenCapability(typeof host.WebAssembly === "object", "webassembly",
				typeof host.WebAssembly === "object" ? "WebAssembly ready." : "WebAssembly is unavailable."),
			webGPU: frozenCapability(!!navigator.gpu, "webgpu",
				navigator.gpu ? "WebGPU ready." : "WebGPU is required by this build."),
			storage: frozenCapability(!!storageBackend, storageBackend ? storageBackend.toLowerCase() : "storage-unavailable",
				storageBackend ? storageBackend + (persisted === true ? " persistent storage granted." : " storage available; browser eviction may remain possible.") :
					"OPFS and IndexedDB are unavailable; local game import cannot be saved."),
			folderImport: frozenCapability(!!folderMode, folderMode ? "ready" : "folder-import-unavailable",
				folderMode ? "Local game import uses the " + folderMode + "." : "This browser cannot select a local game folder."),
		});
	}

	function canStart(capabilities) {
		return !!(capabilities && capabilities.secureContext.available && capabilities.webAssembly.available && capabilities.webGPU.available &&
			capabilities.storage.available && capabilities.folderImport.available);
	}

	class StatusView {
		constructor(rootElement) {
			this.root = rootElement || null;
			this.list = this.root && this.root.querySelector("[data-capability-list]");
			this.phase = this.root && this.root.querySelector("[data-app-phase]");
			this.error = this.root && this.root.querySelector("[data-app-error]");
		}

		setCapabilities(platform, webxr) {
			if (!this.list) return;
			this.list.textContent = "";
			const rows = [
				["Secure hosting", platform.secureContext],
				["WebAssembly", platform.webAssembly], ["WebGPU", platform.webGPU],
				["Game storage", platform.storage], ["Folder import", platform.folderImport],
				["Immersive WebXR", webxr],
			];
			for (const [label, capability] of rows) {
				const item = root.document.createElement("li");
				item.dataset.available = capability.available ? "true" : "false";
				const title = root.document.createElement("strong");
				title.textContent = label + ": ";
				item.appendChild(title);
				item.appendChild(root.document.createTextNode(capability.message));
				this.list.appendChild(item);
			}
		}

		setPhase(message) {
			if (this.phase) this.phase.textContent = message;
		}

		setError(message) {
			if (!this.error) return;
			this.error.hidden = !message;
			this.error.textContent = message || "";
		}
	}

	async function start(options) {
		const settings = options || {};
		const view = settings.statusView || new StatusView(settings.statusRoot || null);
		view.setPhase("Checking browser capabilities…");
		const platform = await detectPlatformCapabilities(settings.environment || root);
		let webxr = await root.SurrealWebXRBrowserProvider.probe(settings.environment || root);
		view.setCapabilities(platform, webxr);
		if (!canStart(platform)) {
			const message = "This browser cannot start the SurrealEngine WebGPU build. See the capability details above.";
			view.setError(message);
			throw new Error(message);
		}

		view.setPhase("Loading the data-free SurrealEngine runtime…");
		root.addEventListener("surrealwebxrcapability", event => {
			webxr = event.detail || webxr;
			view.setCapabilities(platform, webxr);
			view.setPhase(webxr.message || "Immersive WebXR capability changed.");
		});
		const appOptions = Object.assign({}, settings.appOptions || {}, {
			presentationProviders: [root.SurrealWebXRBrowserProvider.createProvider(webxr)],
		});
		root.addEventListener("surrealwebxrpresentation", event => {
			const detail = event.detail || {};
			view.setPhase(detail.message || detail.state || "Presentation changed.");
			view.setError(detail.state === "flat-fallback" ? detail.message : null);
		});
		try {
			const app = await root.SurrealBrowserApp.start(appOptions);
			view.setPhase(app.result && app.result.import && app.result.import.state === "waiting-for-import" ?
				"Choose a supported local game folder to continue." : "Choose launch options and press Play.");
			return Object.freeze({ app, platform, webxr });
		} catch (error) {
			view.setError(error && error.message ? error.message : String(error));
			view.setPhase("Startup stopped.");
			throw error;
		}
	}

	root.SurrealBrowserRelease = Object.freeze({ detectPlatformCapabilities, canStart, StatusView, start });
})(typeof window !== "undefined" ? window : globalThis);
