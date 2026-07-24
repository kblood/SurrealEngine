/* Static-host release shell for the shared browser launcher. */
(function (root) {
	"use strict";

	function frozenCapability(available, code, message) {
		return Object.freeze({ available, code, message });
	}

	function detectWebGL2(environment) {
		try {
			const document = environment && environment.document;
			if (!document || typeof document.createElement !== "function")
				return typeof environment.WebGL2RenderingContext === "function";
			const canvas = document.createElement("canvas");
			const context = canvas.getContext && canvas.getContext("webgl2", {
				alpha: false, antialias: false, depth: true, failIfMajorPerformanceCaveat: true,
			});
			if (!context) return false;
			const loss = context.getExtension && context.getExtension("WEBGL_lose_context");
			if (loss) loss.loseContext();
			return true;
		} catch (_) {
			return false;
		}
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
		const webGL2 = detectWebGL2(host);
		return Object.freeze({
			secureContext: frozenCapability(host.isSecureContext !== false,
				host.isSecureContext === false ? "insecure-context" : "ready",
				host.isSecureContext === false ? "HTTPS or localhost is required for immersive mode." : "Secure context ready."),
			webAssembly: frozenCapability(typeof host.WebAssembly === "object", "webassembly",
				typeof host.WebAssembly === "object" ? "WebAssembly ready." : "WebAssembly is unavailable."),
			webGPU: frozenCapability(!!navigator.gpu, "webgpu",
				navigator.gpu ? "WebGPU available as an optional renderer." : "WebGPU is unavailable; WebGL 2 or diagnostics can still run."),
			webGL2: frozenCapability(webGL2, "webgl2",
				webGL2 ? "WebGL 2 ready." : "WebGL 2 is unavailable."),
			webAudio: frozenCapability(!!(host.AudioContext || host.webkitAudioContext), "webaudio",
				(host.AudioContext || host.webkitAudioContext) ? "Web Audio ready; playback unlocks from an explicit button." : "Web Audio is unavailable."),
			storage: frozenCapability(!!storageBackend, storageBackend ? storageBackend.toLowerCase() : "storage-unavailable",
				storageBackend ? storageBackend + (persisted === true ? " persistent storage granted." : " storage available; browser eviction may remain possible.") :
					"OPFS and IndexedDB are unavailable; local game import cannot be saved."),
			folderImport: frozenCapability(!!folderMode, folderMode ? "ready" : "folder-import-unavailable",
				folderMode ? "Local game import uses the " + folderMode + "." : "This browser cannot select a local game folder."),
		});
	}

	function canStart(capabilities) {
		return !!(capabilities && capabilities.secureContext.available && capabilities.webAssembly.available && capabilities.webAudio.available &&
			capabilities.storage.available && capabilities.folderImport.available);
	}

	class ReleaseDiagnostics {
		constructor(environment, rootElement) {
			this.environment = environment || root;
			this.rootElement = rootElement || null;
			this.startedAt = this.now();
			this.manifest = null;
			this.manifestFetch = null;
			this.platform = null;
			this.webxr = null;
			this.milestones = [];
			this.mode = "development";
			this.record("shell");
			this.downloadButton = this.rootElement && this.rootElement.querySelector("[data-release-diagnostics-download]");
			this.buildLabel = this.rootElement && this.rootElement.querySelector("[data-release-build-id]");
			if (this.downloadButton) {
				this.downloadButton.disabled = false;
				this.downloadButton.addEventListener("click", () => this.download());
			}
		}

		now() {
			const performance = this.environment.performance;
			return performance && typeof performance.now === "function" ? performance.now() : Date.now();
		}

		record(stage, detail) {
			this.milestones.push(Object.freeze({ stage: String(stage), elapsedMs: Math.max(0, this.now() - this.startedAt),
				detail: detail || null }));
			if (this.milestones.length > 64) this.milestones.shift();
		}

		async loadIdentity() {
			const document = this.environment.document;
			const dataset = document && document.documentElement && document.documentElement.dataset || {};
			if (!dataset.engineScript && !dataset.engineWasm) {
				this.record("development-assets");
				if (this.buildLabel) this.buildLabel.textContent = "development (unpackaged)";
				return null;
			}
			if (!dataset.engineScript || !dataset.engineWasm || typeof this.environment.fetch !== "function")
				throw new Error("Packaged release identity markers are incomplete.");
			this.mode = "packaged";
			this.record("manifest-requested");
			const response = await this.environment.fetch("release-manifest.json", { cache: "no-cache", credentials: "same-origin" });
			this.manifestFetch = Object.freeze({ status: response.status, redirected: response.redirected === true,
				contentType: response.headers && response.headers.get("content-type") || null,
				cacheControl: response.headers && response.headers.get("cache-control") || null });
			if (!response.ok || response.redirected) throw new Error("Release manifest request failed or redirected.");
			const manifest = await response.json();
			const hashedEngine = /^engine\/SurrealEngine\.[0-9a-f]{64}\.(?:js|wasm)$/;
			if (manifest.schema !== "surrealengine-browser-release-v2" || manifest.version !== 2 ||
				!manifest.buildId || !manifest.entrypoints ||
				!hashedEngine.test(manifest.entrypoints.javascript || "") ||
				!hashedEngine.test(manifest.entrypoints.wasm || "") ||
				dataset.engineScript !== "./" + manifest.entrypoints.javascript ||
				dataset.engineWasm !== "./" + manifest.entrypoints.wasm)
				throw new Error("Entry HTML and release manifest identify different engine assets.");
			this.manifest = manifest;
			this.record("manifest-verified", { buildId: manifest.buildId, profile: manifest.build && manifest.build.profile || null });
			if (this.buildLabel) this.buildLabel.textContent = manifest.buildId + " (" + (manifest.build && manifest.build.profile || "unknown") + ")";
			return manifest;
		}

		setCapabilities(platform, webxr) {
			this.platform = platform;
			this.webxr = webxr;
			this.record("capabilities-ready", { webGPU: !!(platform && platform.webGPU && platform.webGPU.available),
				webGL2: !!(platform && platform.webGL2 && platform.webGL2.available),
				immersiveVR: !!(webxr && webxr.available) });
		}

		criticalAssetRecords() {
			if (!this.manifest || !Array.isArray(this.manifest.files)) return [];
			const paths = new Set([this.manifest.entrypoints.javascript, this.manifest.entrypoints.wasm]);
			return this.manifest.files.filter(file => paths.has(file.path)).map(file => ({
				path: file.path, bytes: file.bytes, sha256: file.sha256, expectedMime: file.expectedMime,
				cacheControl: file.cacheControl,
			}));
		}

		resourceTimings() {
			const performance = this.environment.performance;
			if (!this.manifest || !performance || typeof performance.getEntriesByType !== "function") return [];
			const paths = [this.manifest.entrypoints.javascript, this.manifest.entrypoints.wasm];
			return performance.getEntriesByType("resource").filter(entry =>
				paths.some(path => String(entry.name).endsWith(path))).map(entry => ({
				name: paths.find(path => String(entry.name).endsWith(path)), durationMs: entry.duration,
				transferBytes: entry.transferSize || null, encodedBytes: entry.encodedBodySize || null,
				decodedBytes: entry.decodedBodySize || null,
			}));
		}

		report() {
			const host = this.environment;
			const navigator = host.navigator || {};
			return {
				schema: "surrealengine-browser-startup-diagnostics-v1",
				generatedAt: new Date().toISOString(),
				mode: this.mode,
				build: this.manifest ? { id: this.manifest.buildId, source: this.manifest.sourceCompliance,
					configuration: this.manifest.build, entrypoints: this.manifest.entrypoints } : null,
				manifestFetch: this.manifestFetch,
				environment: {
					origin: host.location && host.location.origin || null,
					secureContext: host.isSecureContext !== false,
					crossOriginIsolated: host.crossOriginIsolated === true,
					sharedArrayBuffer: typeof host.SharedArrayBuffer === "function",
					logicalProcessors: Number.isSafeInteger(navigator.hardwareConcurrency) ? navigator.hardwareConcurrency : null,
					userAgent: navigator.userAgent || null,
					serviceWorkerController: navigator.serviceWorker && navigator.serviceWorker.controller ?
						String(navigator.serviceWorker.controller.scriptURL || "active") : null,
				},
				capabilities: this.platform ? Object.fromEntries(Object.entries(this.platform).map(([name, value]) =>
					[name, { available: value.available, code: value.code }])) : null,
				webxr: this.webxr ? { available: this.webxr.available, code: this.webxr.code } : null,
				criticalAssets: this.criticalAssetRecords(),
				resourceTimings: this.resourceTimings(),
				milestones: this.milestones.slice(),
			};
		}

		download() {
			const document = this.environment.document;
			if (!document || typeof this.environment.Blob !== "function" || !this.environment.URL) return false;
			const blob = new this.environment.Blob([JSON.stringify(this.report(), null, 2) + "\n"], { type: "application/json" });
			const url = this.environment.URL.createObjectURL(blob);
			const anchor = document.createElement("a");
			anchor.href = url;
			anchor.download = `surrealengine-startup-${this.manifest && this.manifest.buildId || "development"}.json`;
			anchor.click();
			this.environment.setTimeout(() => this.environment.URL.revokeObjectURL(url), 0);
			return true;
		}
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
				["WebAssembly", platform.webAssembly], ["WebGL 2", platform.webGL2], ["WebGPU", platform.webGPU], ["Web Audio", platform.webAudio],
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
		const environment = settings.environment || root;
		const diagnostics = settings.releaseDiagnostics || new ReleaseDiagnostics(environment, settings.statusRoot || null);
		try { await diagnostics.loadIdentity(); }
		catch (error) {
			diagnostics.record("manifest-failed", { name: error && error.name || "Error" });
			view.setError(error && error.message ? error.message : String(error));
			view.setPhase("Startup stopped before loading the engine.");
			throw error;
		}
		view.setPhase("Checking browser capabilities…");
		const platform = await detectPlatformCapabilities(environment);
		let webxr = await root.SurrealWebXRBrowserProvider.probe(environment);
		diagnostics.setCapabilities(platform, webxr);
		if (typeof root.dispatchEvent === "function" && typeof root.CustomEvent === "function") {
			root.dispatchEvent(new root.CustomEvent("surrealwebxrcapability", { detail: webxr }));
		}
		view.setCapabilities(platform, webxr);
		if (!canStart(platform)) {
			const message = "This browser cannot start the SurrealEngine web runtime. See the capability details above.";
			view.setError(message);
			throw new Error(message);
		}

		view.setPhase("Loading the data-free SurrealEngine runtime…");
		root.addEventListener("surrealwebxrcapability", event => {
			webxr = event.detail || webxr;
			view.setCapabilities(platform, webxr);
			view.setPhase(webxr.message || "Immersive WebXR capability changed.");
		});
		const suppliedAppOptions = settings.appOptions || {};
		const packagedBuild = diagnostics.manifest && diagnostics.manifest.build || null;
		const webGL2Compiled = packagedBuild ? packagedBuild.webgl2Renderer === true : settings.webgl2Renderer === true;
		let availableRenderers = [];
		if (webGL2Compiled && platform.webGL2.available) availableRenderers.push("webgl2");
		if (platform.webGPU.available) availableRenderers.push("webgpu");
		if (settings.allowNullRenderer !== false) availableRenderers.push("null");
		if (Array.isArray(suppliedAppOptions.availableRenderers))
			availableRenderers = availableRenderers.filter(renderer => suppliedAppOptions.availableRenderers.includes(renderer));
		const appOptions = Object.assign({}, suppliedAppOptions, {
			availableRenderers,
			presentationProviders: [root.SurrealWebXRBrowserProvider.createProvider(webxr)],
			onRuntimeMilestone: milestone => {
				diagnostics.record(milestone.stage, milestone.detail);
				if (typeof suppliedAppOptions.onRuntimeMilestone === "function") suppliedAppOptions.onRuntimeMilestone(milestone);
			},
		});
		root.addEventListener("surrealwebxrpresentation", event => {
			const detail = event.detail || {};
			view.setPhase(detail.message || detail.state || "Presentation changed.");
			view.setError(detail.state === "flat-fallback" ? detail.message : null);
		});
		try {
			const app = await root.SurrealBrowserApp.start(appOptions);
			diagnostics.record("launcher-ready");
			view.setPhase(app.result && app.result.import && app.result.import.state === "waiting-for-import" ?
				"Choose a supported local game folder to continue." : "Choose launch options and press Play.");
			return Object.freeze({ app, platform, webxr, diagnostics });
		} catch (error) {
			diagnostics.record("startup-failed", { name: error && error.name || "Error" });
			view.setError(error && error.message ? error.message : String(error));
			view.setPhase("Startup stopped.");
			throw error;
		}
	}

	root.SurrealBrowserRelease = Object.freeze({ detectPlatformCapabilities, canStart, ReleaseDiagnostics, StatusView, start });
})(typeof window !== "undefined" ? window : globalThis);
