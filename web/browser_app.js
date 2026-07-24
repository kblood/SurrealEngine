/* Shared, presentation-neutral browser launcher for SurrealEngine. */
(function (global) {
	"use strict";

	const DEFAULT_ENGINE_SCRIPT = "../build-emscripten/SurrealEngine.js";
	const PREFERENCE_KEY = "surrealengine-browser-launcher-v1";
	const LIBRARY_KEY = "surrealengine-browser-library-v1";

	class LauncherError extends Error {
		constructor(code, message) {
			super(message);
			this.name = "SurrealBrowserLauncherError";
			this.code = code;
		}
	}

	function publishRuntimeFailure(label, reason, log, onRuntimeCrash, environment) {
		const host = environment || global;
		const writeLog = typeof log === "function" ? log : () => {};
		const notify = typeof onRuntimeCrash === "function" ? onRuntimeCrash : () => {};
		const reasonText = reason && typeof reason.message === "string" ? reason.message :
			String(reason === undefined || reason === null ? "unknown reason" : reason);
		const detail = label + ": " + reasonText;
		host.surrealCrashed = detail;
		writeLog("[runtime] " + detail);
		notify(detail);
		if (typeof host.dispatchEvent === "function" && typeof host.CustomEvent === "function") {
			host.dispatchEvent(new host.CustomEvent("surrealruntimeabort", {
				detail: Object.freeze({ message: detail }),
			}));
		}
		return detail;
	}

	function createRuntimeAbortHandler(log, onRuntimeCrash, environment) {
		return reason => publishRuntimeFailure("WebAssembly runtime aborted", reason,
			log, onRuntimeCrash, environment);
	}

	function createUnhandledRuntimeErrorHandler(log, onRuntimeCrash, environment) {
		return event => {
			const reason = event && (event.error || event.reason || event.message);
			return publishRuntimeFailure("Uncaught browser runtime error", reason,
				log, onRuntimeCrash, environment);
		};
	}

	class PresentationRegistry {
		constructor() { this._providers = new Map(); }
		register(provider) {
			if (!provider || !/^[a-z][a-z0-9-]{0,31}$/.test(provider.id || "") ||
				typeof provider.label !== "string") {
				throw new LauncherError("PRESENTATION_PROVIDER", "A presentation provider needs a safe id and label.");
			}
			if (this._providers.has(provider.id)) throw new LauncherError("PRESENTATION_DUPLICATE", "That presentation provider is already registered.");
			const stored = Object.freeze({
				id: provider.id,
				label: provider.label,
				isAvailable: typeof provider.isAvailable === "function" ? provider.isAvailable : () => provider.isAvailable !== false,
				requiresXRCompatibleAdapter: provider.requiresXRCompatibleAdapter === true,
				prefersXRCompatibleAdapter: provider.prefersXRCompatibleAdapter === true,
				setXRCompatibleAdapter: typeof provider.setXRCompatibleAdapter === "function" ? provider.setXRCompatibleAdapter : () => {},
				prepareLaunch: typeof provider.prepareLaunch === "function" ? provider.prepareLaunch : async () => {},
				activate: typeof provider.activate === "function" ? provider.activate : async () => {},
			});
			this._providers.set(stored.id, stored);
			return stored;
		}
		available() { return Array.from(this._providers.values()).filter(provider => provider.isAvailable()); }
		get(id) { return this._providers.get(id) || null; }
	}

	class GameLibrary {
		constructor(storage, storageFactory) {
			this.storage = storage || global.localStorage;
			this.storageFactory = storageFactory || (options => global.SurrealGameImporter.createStorage(options));
			this._storageByGame = new Map();
		}
		_state() {
			try {
				const parsed = JSON.parse(this.storage.getItem(LIBRARY_KEY) || "{}");
				const installed = Array.isArray(parsed.installed) ? parsed.installed.filter(id => !!global.SurrealGameImporter.GAME_DEFINITIONS[id]) : [];
				const activeGameId = global.SurrealGameImporter.GAME_DEFINITIONS[parsed.activeGameId] ? parsed.activeGameId : (installed[0] || "ut99");
				return { installed: Array.from(new Set(installed)), activeGameId, importing: parsed.importing === true };
			} catch (_) { return { installed: [], activeGameId: "ut99", importing: false }; }
		}
		_write(state) { this.storage.setItem(LIBRARY_KEY, JSON.stringify(state)); }
		status() { const state = this._state(); return Object.freeze({ installed: Object.freeze(state.installed.slice()), activeGameId: state.activeGameId, importing: state.importing }); }
		setActive(gameId) {
			if (!global.SurrealGameImporter.GAME_DEFINITIONS[gameId]) throw new LauncherError("LIBRARY_GAME", "That game is not supported.");
			const state = this._state(); state.activeGameId = gameId; state.importing = false; this._write(state);
		}
		requestImport() { const state = this._state(); state.importing = true; this._write(state); }
		register(metadata) {
			const gameId = metadata && metadata.gameId || "ut99";
			if (!global.SurrealGameImporter.GAME_DEFINITIONS[gameId]) throw new LauncherError("LIBRARY_GAME", "Imported metadata names an unsupported game.");
			const state = this._state();
			if (!state.installed.includes(gameId)) state.installed.push(gameId);
			state.activeGameId = gameId; state.importing = false; this._write(state);
		}
		remove(gameId) {
			const state = this._state(); state.installed = state.installed.filter(id => id !== gameId);
			if (state.activeGameId === gameId) state.activeGameId = state.installed[0] || "ut99";
			this._write(state);
		}
		_storageNames(gameId) {
			if (gameId === "ut99") return {};
			return { databaseName: "surrealengine-game-" + gameId + "-v1", opfsDirectory: "surrealengine-game-" + gameId + "-v1" };
		}
		mutableOptions(gameId) {
			if (gameId === "ut99") return {};
			return { databaseName: "surrealengine-mutable-" + gameId + "-v1", opfsDirectory: "surrealengine-mutable-" + gameId + "-v1" };
		}
		async _gameStorage(gameId) {
			if (!this._storageByGame.has(gameId)) this._storageByGame.set(gameId, this.storageFactory(this._storageNames(gameId)));
			return this._storageByGame.get(gameId);
		}
		storageProxy() {
			const library = this;
			return Object.freeze({
				backend: "game-library",
				async load() {
					const state = library._state();
					if (state.importing) return null;
					const dataset = await (await library._gameStorage(state.activeGameId)).load();
					if (dataset) library.register(dataset.metadata);
					return dataset;
				},
				async save(validation, onProgress) {
					const storage = await library._gameStorage(validation.gameId);
					const metadata = await storage.save(validation, onProgress);
					library.register(metadata);
					return metadata;
				},
				async clear() {
					const gameId = library._state().activeGameId;
					await (await library._gameStorage(gameId)).clear();
					library.remove(gameId);
				},
			});
		}
	}

	class GameLibraryUI {
		constructor(root, library) {
			this.root = root || null; this.library = library;
			this.select = root && root.querySelector("[data-library-game]");
			this.switchButton = root && root.querySelector("[data-library-switch]");
			this.addButton = root && root.querySelector("[data-library-add]");
			if (this.switchButton) this.switchButton.addEventListener("click", () => {
				if (!this.select || !this.select.value) return;
				this.library.setActive(this.select.value); global.location.reload();
			});
			if (this.addButton) this.addButton.addEventListener("click", () => { this.library.requestImport(); global.location.reload(); });
			this.refresh();
		}
		refresh() {
			if (!this.select) return;
			const status = this.library.status(); this.select.textContent = "";
			for (const id of status.installed) {
				const option = global.document.createElement("option"); option.value = id;
				option.textContent = global.SurrealGameImporter.GAME_DEFINITIONS[id].name; this.select.appendChild(option);
			}
			this.select.value = status.activeGameId;
			if (this.switchButton) this.switchButton.disabled = !status.installed.length || this.select.value === status.activeGameId;
			this.select.onchange = () => { if (this.switchButton) this.switchButton.disabled = this.select.value === status.activeGameId; };
			if (this.root) this.root.hidden = false;
		}
	}

	function normalizeHostEntries(records, bridge) {
		if (!Array.isArray(records) || !records.length) throw new LauncherError("HOST_EMPTY", "No files were returned by the desktop folder picker.");
		return records.map(record => {
			if (!record || typeof record.path !== "string") throw new LauncherError("HOST_ENTRY", "The desktop folder picker returned an invalid file.");
			let blob = record.blob;
			if (!(blob instanceof Blob) && record.data instanceof ArrayBuffer) blob = new Blob([record.data]);
			if (!(blob instanceof Blob) && ArrayBuffer.isView(record.data)) blob = new Blob([record.data]);
			if (blob instanceof Blob) return { path: record.path, size: blob.size, getBlob: async () => blob };
			if (typeof record.token === "string" && Number.isSafeInteger(record.size) && record.size >= 0 && bridge && typeof bridge.readGameFile === "function") {
				return { path: record.path, size: record.size, getBlob: async () => {
					const data = await bridge.readGameFile(record.token);
					const loaded = data instanceof Blob ? data : new Blob([data]);
					if (loaded.size !== record.size) throw new LauncherError("HOST_ENTRY_SIZE", "A desktop-selected file changed while it was being imported.");
					return loaded;
				} };
			}
			throw new LauncherError("HOST_ENTRY", "The desktop folder picker must return file data or a readable opaque token.");
		});
	}

	function createHostPicker(bridge) {
		if (!bridge || typeof bridge.pickGameDirectory !== "function") return null;
		return async onProgress => {
			const records = await bridge.pickGameDirectory();
			if (!records) throw new DOMException("Folder selection was cancelled.", "AbortError");
			const entries = normalizeHostEntries(records, bridge);
			if (onProgress) onProgress({ phase: "scan", filesDone: entries.length });
			return entries;
		};
	}

	function buildNativeArguments(selection) {
		if (!selection) throw new LauncherError("LAUNCH_SELECTION", "Choose how to start before continuing.");
		const skipIntro = selection.skipIntro !== false;
		const gameId = selection.game && selection.game.id || "ut99";
		if (skipIntro && !selection.map) throw new LauncherError("LAUNCH_SELECTION", "Choose a map before starting.");
		if (skipIntro && !global.SurrealGameImporter.isSafeGameMapBasename(selection.map, gameId)) {
			throw new LauncherError("LAUNCH_MAP", "The selected map name is unsafe.");
		}
		const renderer = selection.renderer || "webgpu";
		if (renderer !== "webgl2" && renderer !== "webgpu" && renderer !== "null")
			throw new LauncherError("LAUNCH_RENDERER", "That renderer is not available in this browser build.");
		const args = ["--autoplay"];
		if (skipIntro) args.push("--url=" + selection.map);
		args.push("--render=" + renderer, "/gamedata");
		return Object.freeze(args);
	}

	function callNativeMain(Module, selection) {
		if (!Module || typeof Module.callMain !== "function") {
			throw new LauncherError("ENGINE_RUNTIME", "The SurrealEngine runtime is not ready to start.");
		}
		if (typeof Module._Surreal_StartBrowserGame === "function" && typeof Module.ccall === "function") {
			return Module.ccall("Surreal_StartBrowserGame", "number",
				["string", "string", "number"],
				[selection.skipIntro === false ? "" : selection.map,
					selection.renderer || "webgpu", selection.skipIntro === false ? 0 : 1],
				{ async: true });
		}
		// Emscripten prepends argv[0] in place. Keep the public launch
		// description immutable, but give callMain its own mutable copy.
		return Module.callMain(Array.from(buildNativeArguments(selection)));
	}

	async function activatePresentation(registry, selection, Module) {
		const provider = registry.get(selection && selection.presentationId || "flat");
		if (!provider || !provider.isAvailable()) {
			throw new LauncherError("PRESENTATION_UNAVAILABLE", "That presentation mode is unavailable.");
		}
		return provider.activate(Object.freeze({ Module, selection }));
	}

	const STARTUP_STAGES = Object.freeze(["idle", "native-startup", "native-ready",
		"presentation-activation", "running", "failed"]);

	class LaunchStartupTracker {
		constructor(options) {
			const settings = options || {};
			this.clock = typeof settings.clock === "function" ? settings.clock : () => {
				if (global.performance && typeof global.performance.now === "function") return global.performance.now();
				return Date.now();
			};
			this.onChange = typeof settings.onChange === "function" ? settings.onChange : () => {};
			this.startedAt = null;
			this.nativeStartedAt = null;
			this.nativeReturnedAt = null;
			this.stageChangedAt = this.clock();
			this.stage = "idle";
		}

		transition(stage) {
			if (!STARTUP_STAGES.includes(stage)) throw new LauncherError("STARTUP_STAGE", "Unknown browser startup stage.");
			const now = this.clock();
			if (this.startedAt === null && stage !== "idle") this.startedAt = now;
			this.stage = stage;
			this.stageChangedAt = now;
			const snapshot = this.status();
			this.onChange(snapshot);
			return snapshot;
		}

		markNativeStarted() { this.nativeStartedAt = this.clock(); }
		markNativeReturned() { this.nativeReturnedAt = this.clock(); }

		status() {
			const now = this.clock();
			return Object.freeze({
				stage: this.stage,
				elapsedMs: this.startedAt === null ? 0 : Math.max(0, now - this.startedAt),
				stageElapsedMs: Math.max(0, now - this.stageChangedAt),
				nativeStartupMs: this.nativeStartedAt === null || this.nativeReturnedAt === null ? null :
					Math.max(0, this.nativeReturnedAt - this.nativeStartedAt),
			});
		}
	}

	function waitForBrowserPaint(environment) {
		const host = environment || global;
		return new Promise(resolve => {
			if (typeof host.requestAnimationFrame !== "function") { resolve(); return; }
			host.requestAnimationFrame(() => {
				if (typeof host.setTimeout === "function") host.setTimeout(resolve, 0);
				else resolve();
			});
		});
	}

	class WebGPUDeviceLossController {
		constructor(options) {
			const settings = options || {};
			this.environment = settings.environment || global;
			this.log = typeof settings.log === "function" ? settings.log : () => {};
			this.onRuntimeCrash = typeof settings.onRuntimeCrash === "function" ? settings.onRuntimeCrash : null;
			this.getDataController = typeof settings.getDataController === "function" ? settings.getDataController : () => null;
			this.getActiveRenderer = typeof settings.getActiveRenderer === "function" ? settings.getActiveRenderer : () => null;
			this.onDeviceUnavailable = typeof settings.onDeviceUnavailable === "function" ? settings.onDeviceUnavailable : () => {};
			this.checkpointTimeoutMs = Number.isFinite(settings.checkpointTimeoutMs) ?
				Math.max(0, settings.checkpointTimeoutMs) : 5000;
			this.root = settings.root || null;
			this.message = null;
			this.restartButton = null;
			this.phase = "ready";
			this.checkpoint = "not-attempted";
			this.reason = "unknown";
			this.handling = null;
			this.restartRequested = false;
		}

		_view() {
			if (!this.root) {
				const document = this.environment.document;
				if (!document || typeof document.createElement !== "function") return;
				this.root = document.createElement("section");
				this.root.hidden = true;
				this.root.setAttribute("role", "alert");
				this.root.setAttribute("aria-live", "assertive");
				this.root.dataset.webgpuDeviceLoss = "true";
				const parent = document.querySelector &&
					(document.querySelector("[data-app-error]")?.parentElement || document.querySelector("main"));
				(parent || document.body)?.appendChild(this.root);
			}
			if (!this.message) {
				this.message = this.root.querySelector && this.root.querySelector("[data-webgpu-device-loss-message]");
				if (!this.message && this.environment.document) {
					this.message = this.environment.document.createElement("p");
					this.message.dataset.webgpuDeviceLossMessage = "true";
					this.root.appendChild(this.message);
				}
			}
			if (!this.restartButton) {
				this.restartButton = this.root.querySelector && this.root.querySelector("[data-webgpu-device-loss-restart]");
				if (!this.restartButton && this.environment.document) {
					this.restartButton = this.environment.document.createElement("button");
					this.restartButton.type = "button";
					this.restartButton.dataset.webgpuDeviceLossRestart = "true";
					this.restartButton.textContent = "Restart with WebGL 2";
					this.root.appendChild(this.restartButton);
				}
				if (this.restartButton) this.restartButton.addEventListener("click", () => this.restartWithWebGL2());
			}
		}

		_show(message, restartEnabled) {
			this._view();
			if (this.root) this.root.hidden = false;
			if (this.message) this.message.textContent = message;
			if (this.restartButton) this.restartButton.disabled = !restartEnabled;
		}

		async _checkpointMutableData() {
			const controller = this.getDataController();
			if (!controller || typeof controller.flush !== "function") return "unavailable";
			let timeout = null;
			try {
				const save = Promise.resolve(controller.flush("webgpu-device-lost"));
				if (!this.checkpointTimeoutMs || typeof this.environment.setTimeout !== "function") {
					const result = await save;
					if (result && ["not-started", "unavailable"].includes(result.state)) return "unavailable";
					return result && result.deferredCheckpointReason ? "failed" : "saved";
				}
				const result = await Promise.race([save, new Promise((resolve, reject) => {
					timeout = this.environment.setTimeout(() => reject(new Error("checkpoint timed out")), this.checkpointTimeoutMs);
				})]);
				if (result && ["not-started", "unavailable"].includes(result.state)) return "unavailable";
				return result && result.deferredCheckpointReason ? "failed" : "saved";
			} catch (_) {
				return "failed";
			} finally {
				if (timeout !== null && typeof this.environment.clearTimeout === "function")
					this.environment.clearTimeout(timeout);
			}
		}

		handle(info) {
			if (this.handling) return this.handling;
			this.handling = this._handle(info);
			return this.handling;
		}

		async _handle(info) {
			this.reason = info && typeof info.reason === "string" ? info.reason : "unknown";
			this.onDeviceUnavailable();
			if (this.getActiveRenderer() !== "webgpu") {
				this.phase = "inactive-device-lost";
				this.log("[runtime] WebGPU device lost while WebGPU was not the active renderer.");
				return this.status();
			}
			this.phase = "checkpointing";
			this._show("WebGPU stopped unexpectedly. Preserving local saves before offering a safe WebGL 2 restart…", false);
			this.checkpoint = await this._checkpointMutableData();
			const reason = info && typeof info.message === "string" && info.message ? info.message : this.reason;
			publishRuntimeFailure("WebGPU device lost", reason, this.log, this.onRuntimeCrash, this.environment);
			this.phase = "failed";
			const saveMessage = this.checkpoint === "saved" ? "Local mutable saves were checkpointed." :
				(this.checkpoint === "unavailable" ? "No mutable save controller was available." :
					"Local mutable saves could not be checkpointed; the last completed checkpoint remains intact.");
			this._show("WebGPU stopped unexpectedly. " + saveMessage + " Restart with WebGL 2 to return to a supported graphics path.", true);
			return this.status();
		}

		restartWithWebGL2() {
			if (this.phase !== "failed" || this.restartRequested) return false;
			this.restartRequested = true;
			this.phase = "restarting";
			try {
				const storage = this.environment.localStorage;
				const preferences = JSON.parse(storage && storage.getItem(PREFERENCE_KEY) || "{}");
				preferences.renderer = "webgl2";
				preferences.presentationId = "flat";
				if (storage) storage.setItem(PREFERENCE_KEY, JSON.stringify(preferences));
			} catch (_) { /* The restart still works when optional preferences are unavailable. */ }
			if (this.restartButton) this.restartButton.disabled = true;
			if (this.message) this.message.textContent = "Restarting with WebGL 2…";
			const location = this.environment.location;
			if (location && typeof location.reload === "function") location.reload();
			return true;
		}

		status() {
			return Object.freeze({ phase: this.phase, checkpoint: this.checkpoint,
				reason: this.reason, restartRequested: this.restartRequested });
		}
	}

	class BrowserViewportController {
		constructor(canvas, options) {
			const settings = options || {};
			this.canvas = canvas || null;
			this.environment = settings.environment || global;
			this.log = typeof settings.log === "function" ? settings.log : () => {};
			this.Module = null;
			this.running = false;
			this.pendingFrame = null;
			this.lastWidth = 0;
			this.lastHeight = 0;
			this.resizeCount = 0;
			this.lastError = null;
			this._schedule = () => this.schedule();
			const ResizeObserverType = this.environment.ResizeObserver;
			this.observer = this.canvas && typeof ResizeObserverType === "function" ?
				new ResizeObserverType(this._schedule) : null;
		}

		attachModule(Module) { this.Module = Module || null; }

		engineStarted() {
			if (this.running) return;
			this.running = true;
			if (this.observer) this.observer.observe(this.canvas);
			if (this.environment && typeof this.environment.addEventListener === "function")
				this.environment.addEventListener("resize", this._schedule);
			const document = this.environment && this.environment.document;
			if (document && typeof document.addEventListener === "function")
				document.addEventListener("fullscreenchange", this._schedule);
			this.schedule();
		}

		schedule() {
			if (!this.running || this.pendingFrame !== null) return;
			const requestFrame = this.environment && this.environment.requestAnimationFrame;
			if (typeof requestFrame === "function") {
				this.pendingFrame = requestFrame.call(this.environment, () => {
					this.pendingFrame = null;
					this.synchronize();
				});
			} else {
				this.pendingFrame = 0;
				this.pendingFrame = null;
				this.synchronize();
			}
		}

		synchronize() {
			if (!this.running || !this.canvas || !this.Module || typeof this.Module.ccall !== "function") return false;
			const bounds = this.canvas.getBoundingClientRect();
			const scale = Math.max(1, Number(this.environment.devicePixelRatio) || 1);
			const width = Math.max(320, Math.min(8192, Math.round(bounds.width * scale)));
			const height = Math.max(200, Math.min(8192, Math.round(bounds.height * scale)));
			if (width === this.lastWidth && height === this.lastHeight) return true;
			try {
				if (this.Module.ccall("Surreal_ResizeBrowserViewport", "number",
					["number", "number"], [width, height]) !== 1)
					throw new Error("native viewport rejected " + width + "x" + height);
				this.lastWidth = width;
				this.lastHeight = height;
				this.resizeCount++;
				this.lastError = null;
				return true;
			} catch (error) {
				this.lastError = error && error.message || String(error);
				this.log("[viewport] " + this.lastError);
				return false;
			}
		}

		diagnostics() {
			return Object.freeze({ running: this.running, width: this.lastWidth, height: this.lastHeight,
				resizeCount: this.resizeCount, lastError: this.lastError });
		}
	}

	async function runLaunchBoundary(options) {
		const settings = options || {};
		const tracker = settings.startupTracker || new LaunchStartupTracker({ onChange: settings.onStartupStage });
		try {
			// Publish the native-startup state and yield through a rendering opportunity before
			// entering callMain: some Emscripten builds remain there for a long time.
			tracker.transition("native-startup");
			await (settings.waitForPaint || waitForBrowserPaint)(settings.environment || global);
			tracker.markNativeStarted();
			await callNativeMain(settings.Module, settings.selection);
			tracker.markNativeReturned();
			tracker.transition("native-ready");
			if (settings.selection && settings.selection.xrDominantHand && settings.Module &&
				typeof settings.Module.ccall === "function") {
				const hand = settings.selection.xrDominantHand === "left" ? 0 : 1;
				if (await settings.Module.ccall("Surreal_SetXRDominantHand", "number",
					["number"], [hand], { async: true }) !== 1)
					throw new LauncherError("XR_HAND_SETTING", "The engine rejected the XR dominant-hand setting.");
			}
			if (settings.audioController && typeof settings.audioController.engineStarted === "function")
				settings.audioController.engineStarted();
			if (settings.viewportController && typeof settings.viewportController.engineStarted === "function")
				settings.viewportController.engineStarted();
			if (settings.xrController && typeof settings.xrController.engineStarted === "function")
				settings.xrController.engineStarted();
			tracker.transition("presentation-activation");
			await activatePresentation(settings.registry, settings.selection, settings.Module);
			tracker.transition("running");
			if (typeof settings.onLaunch === "function") settings.onLaunch(settings.selection);
			return tracker.status();
		} catch (error) {
			tracker.transition("failed");
			throw error;
		}
	}

	class LauncherController {
		constructor(root, registry, options) {
			this.root = root || null;
			this.registry = registry;
			this.options = options || {};
			this.context = null;
			this.pending = null;
			this.map = root && root.querySelector("[data-launcher-map]");
			this.presentation = root && root.querySelector("[data-launcher-presentation]");
			this.webXRBackend = root && root.querySelector("[data-launcher-webxr-backend]");
			this.webXRBridgeBlockingTiming = root && root.querySelector("[data-launcher-webxr-bridge-blocking-timing]");
			this.webXRBridgeRotationReprojection = root && root.querySelector("[data-launcher-webxr-bridge-rotation-reprojection]");
			this.renderer = root && root.querySelector("[data-launcher-renderer]");
			this.skipIntro = root && root.querySelector("[data-launcher-skip-intro]");
			this.xrDominantHand = root && root.querySelector("[data-launcher-xr-dominant-hand]");
			this.game = root && root.querySelector("[data-launcher-game]");
			this.error = root && root.querySelector("[data-launcher-error]");
			this.form = root && root.querySelector("[data-launcher-form]");
			this._configureRenderers(null);
			if (this.form) this.form.addEventListener("submit", event => this._submit(event));
			if (this.skipIntro) this.skipIntro.addEventListener("change", () => this._updateMapAvailability());
			if (this.presentation) this.presentation.addEventListener("change", () => this._updatePresentationAvailability());
			if (this.webXRBackend) this.webXRBackend.addEventListener("change", () => this._updatePresentationAvailability());
		}

		_preferences() {
			try { return JSON.parse(global.localStorage.getItem(PREFERENCE_KEY) || "{}"); }
			catch (_) { return {}; }
		}

		_savePreferences(selection) {
			try { global.localStorage.setItem(PREFERENCE_KEY, JSON.stringify({
				mapByGame: Object.assign({}, this._preferences().mapByGame || {}, { [selection.game.id]: selection.map }),
				presentationId: selection.presentationId,
				webXRPresentationPreference: selection.webXRPresentationPreference ||
					(this.webXRBackend && this.webXRBackend.value === "webgl-bridge" ? "webgl-bridge" : "auto"),
				renderer: selection.renderer,
				skipIntro: selection.skipIntro,
				xrDominantHand: selection.xrDominantHand,
			})); } catch (_) { /* Preferences are optional. */ }
		}

		_updateMapAvailability() {
			if (this.map) this.map.disabled = !!this.skipIntro && !this.skipIntro.checked;
		}

		_updatePresentationAvailability() {
			const webXRSelected = !!this.presentation && this.presentation.value === "webxr";
			if (this.webXRBackend) this.webXRBackend.disabled = !webXRSelected;
			const bridgeForced = webXRSelected && this.webXRBackend && this.webXRBackend.value === "webgl-bridge";
			if (this.webXRBridgeBlockingTiming) {
				this.webXRBridgeBlockingTiming.disabled = !bridgeForced;
				if (!bridgeForced) this.webXRBridgeBlockingTiming.checked = false;
			}
			if (this.webXRBridgeRotationReprojection) {
				this.webXRBridgeRotationReprojection.disabled = !bridgeForced;
				if (!bridgeForced) this.webXRBridgeRotationReprojection.checked = false;
			}
		}

		_option(select, value, label) {
			if (!select) return;
			const option = global.document.createElement("option");
			option.value = value;
			option.textContent = label;
			select.appendChild(option);
		}

		_configureRenderers(preferred) {
			if (!this.renderer) return;
			const labels = { webgl2: "WebGL 2 (VR-ready)", webgpu: "WebGPU (flat / experimental XR)",
				null: "No graphics (diagnostics)" };
			const renderers = Array.isArray(this.options.availableRenderers) && this.options.availableRenderers.length ?
				this.options.availableRenderers : ["webgpu", "null"];
			this.renderer.textContent = "";
			for (const renderer of renderers) this._option(this.renderer, renderer, labels[renderer] || renderer);
			this.renderer.value = renderers.includes(preferred) ? preferred : renderers[0];
		}

		setAvailableRenderers(renderers) {
			const current = this.renderer && this.renderer.value;
			this.options.availableRenderers = Array.from(new Set((renderers || []).filter(renderer =>
				["webgl2", "webgpu", "null"].includes(renderer))));
			this._configureRenderers(current);
		}

		async selectLaunch(context) {
			if (this.pending) throw new LauncherError("LAUNCH_PENDING", "A launch choice is already pending.");
			this.context = context;
			const preferences = this._preferences();
			if (this.game) this.game.textContent = context.game.name;
			if (this.map) {
				this.map.textContent = "";
				const maps = context.mapManifest && context.mapManifest.maps.length ? context.mapManifest.maps : [context.game.defaultMap];
				for (const map of maps) this._option(this.map, map, map);
				const preferred = preferences.mapByGame && preferences.mapByGame[context.game.id];
				const selected = (preferred && maps.find(map => map.toLowerCase() === preferred.toLowerCase())) ||
					maps.find(map => map.toLowerCase() === context.game.defaultMap.toLowerCase());
				if (selected) this.map.value = selected;
			}
			if (this.presentation) {
				this.presentation.textContent = "";
				for (const provider of this.registry.available()) this._option(this.presentation, provider.id, provider.label);
				if (this.registry.get(preferences.presentationId) && this.registry.get(preferences.presentationId).isAvailable()) {
					this.presentation.value = preferences.presentationId;
				}
			}
			if (this.webXRBackend) this.webXRBackend.value =
				preferences.webXRPresentationPreference === "webgl-bridge" ? "webgl-bridge" : "auto";
			if (this.webXRBridgeBlockingTiming) this.webXRBridgeBlockingTiming.checked = false;
			if (this.webXRBridgeRotationReprojection) this.webXRBridgeRotationReprojection.checked = false;
			this._configureRenderers(preferences.renderer);
			if (this.skipIntro) this.skipIntro.checked = preferences.skipIntro !== false;
			if (this.xrDominantHand) this.xrDominantHand.value =
				preferences.xrDominantHand === "left" ? "left" : "right";
			this._updateMapAvailability();
			this._updatePresentationAvailability();
			if (this.root) this.root.hidden = false;
			return new Promise((resolve, reject) => { this.pending = { resolve, reject }; });
		}

		async _submit(event) {
			event.preventDefault();
			if (!this.pending || !this.context) return;
			try {
				const provider = this.registry.get(this.presentation && this.presentation.value || "flat");
				if (!provider || !provider.isAvailable()) throw new LauncherError("PRESENTATION_UNAVAILABLE", "That presentation mode is unavailable.");
				const renderer = this.renderer && this.renderer.value || "webgpu";
				const availableRenderers = Array.isArray(this.options.availableRenderers) ? this.options.availableRenderers : ["webgpu", "null"];
				if (!availableRenderers.includes(renderer))
					throw new LauncherError("LAUNCH_RENDERER", "That renderer is unavailable in this browser and build.");
				const selectionData = {
					game: this.context.game,
					map: this.map && this.map.value || this.context.game.defaultMap,
					presentationId: provider.id,
					renderer,
					skipIntro: !this.skipIntro || this.skipIntro.checked,
					xrDominantHand: this.xrDominantHand && this.xrDominantHand.value === "left" ? "left" : "right",
				};
				if (provider.id === "webxr") selectionData.webXRPresentationPreference =
					this.webXRBackend && this.webXRBackend.value === "webgl-bridge" ? "webgl-bridge" : "auto";
				if (provider.id === "webxr" && selectionData.webXRPresentationPreference === "webgl-bridge")
					selectionData.webXRBridgeBlockingTiming = !!this.webXRBridgeBlockingTiming &&
						this.webXRBridgeBlockingTiming.checked;
				if (provider.id === "webxr" && selectionData.webXRPresentationPreference === "webgl-bridge")
					selectionData.webXRBridgeRotationReprojection = !!this.webXRBridgeRotationReprojection &&
						this.webXRBridgeRotationReprojection.checked;
				const selection = Object.freeze(selectionData);
				buildNativeArguments(selection);
				await provider.prepareLaunch(Object.freeze({ context: this.context, selection }));
				this._savePreferences(selection);
				const pending = this.pending;
				this.pending = null;
				if (this.root) this.root.hidden = true;
				pending.resolve(selection);
			} catch (error) {
				if (this.error) { this.error.hidden = false; this.error.textContent = error.message || String(error); }
			}
		}
	}

	async function acquireWebGPUDevice(log, options) {
		const settings = options || {};
		if (!global.navigator.gpu) throw new LauncherError("WEBGPU_UNAVAILABLE", "This browser does not provide WebGPU.");
		let adapter = null;
		let xrFailure = null;
		let xrCompatibleAdapter = false;
		if (settings.xrCompatible) {
			try { adapter = await global.navigator.gpu.requestAdapter({ xrCompatible: true }); }
			catch (error) { xrFailure = error; }
			if (!adapter) {
				const detail = xrFailure && xrFailure.message ? xrFailure.message : "no XR-compatible WebGPU adapter was returned";
				log("[runtime] Immersive WebXR disabled: " + detail + "; retrying WebGPU for flat mode");
				if (typeof settings.onXRCompatibility === "function") settings.onXRCompatibility(false, detail);
				adapter = await global.navigator.gpu.requestAdapter();
			} else {
				xrCompatibleAdapter = true;
				if (typeof settings.onXRCompatibility === "function") settings.onXRCompatibility(true, null);
			}
		} else {
			adapter = await global.navigator.gpu.requestAdapter();
		}
		if (!adapter) throw new LauncherError("WEBGPU_ADAPTER", "No compatible WebGPU adapter was found.");
		const requiredFeatures = ["texture-compression-bc", "float32-filterable"].filter(feature => adapter.features.has(feature));
		const device = await adapter.requestDevice({ requiredFeatures });
		global.surrealWebGPUDeviceXRCompatible = xrCompatibleAdapter;
		device.lost.then(info => {
			log("[runtime] WebGPU device lost: " + (info && info.message || info && info.reason || "unknown reason"));
			if (typeof settings.onDeviceLost === "function") Promise.resolve(settings.onDeviceLost(info)).catch(error =>
				log("[runtime] WebGPU device loss recovery failed: " + (error && error.message || String(error))));
		}, error => {
			log("[runtime] WebGPU device loss monitoring failed: " + (error && error.message || String(error)));
			if (typeof settings.onDeviceLost === "function") Promise.resolve(settings.onDeviceLost(error)).catch(recoveryError =>
				log("[runtime] WebGPU device loss recovery failed: " + (recoveryError && recoveryError.message || String(recoveryError))));
		});
		return device;
	}

	async function start(options) {
		options = options || {};
		const runtimeMilestone = (stage, detail) => {
			if (typeof options.onRuntimeMilestone === "function") options.onRuntimeMilestone(Object.freeze({ stage, detail: detail || null }));
		};
		const log = typeof options.log === "function" ? options.log : () => {};
		const viewportController = options.viewportController || new BrowserViewportController(options.canvas, {
			environment: options.environment || global,
			log,
		});
		const registry = options.presentationRegistry || new PresentationRegistry();
		if (!registry.get("flat")) registry.register({ id: "flat", label: "Desktop window" });
		for (const provider of options.presentationProviders || []) registry.register(provider);
		const library = options.library || new GameLibrary();
		const libraryUI = options.libraryUI || new GameLibraryUI(options.libraryRoot || null, library);
		const xrProviders = registry.available().filter(provider =>
			provider.requiresXRCompatibleAdapter || provider.prefersXRCompatibleAdapter);
		let availableRenderers = Array.isArray(options.availableRenderers) ?
			options.availableRenderers.filter(renderer => ["webgl2", "webgpu", "null"].includes(renderer)) : ["webgpu", "null"];
		availableRenderers = Array.from(new Set(availableRenderers));
		let dataController = null;
		let activeRenderer = null;
		let launcher = null;
		const deviceLossController = options.webGPUDeviceLossController || new WebGPUDeviceLossController({
			environment: options.environment || global,
			root: options.webGPUDeviceLossRoot || null,
			log,
			onRuntimeCrash: options.onRuntimeCrash,
			getDataController: () => dataController,
			getActiveRenderer: () => activeRenderer,
			onDeviceUnavailable: () => {
				availableRenderers = availableRenderers.filter(renderer => renderer !== "webgpu");
				if (launcher) launcher.setAvailableRenderers(availableRenderers);
				for (const provider of xrProviders) provider.setXRCompatibleAdapter(false, "WebGPU device lost");
			},
		});
		let device = null;
		if (availableRenderers.includes("webgpu")) {
			runtimeMilestone("webgpu-device-requested");
			try {
				device = await acquireWebGPUDevice(log, {
					xrCompatible: xrProviders.length > 0,
					onDeviceLost: info => deviceLossController.handle(info),
					onXRCompatibility: (available, detail) => {
						for (const provider of xrProviders) provider.setXRCompatibleAdapter(available, detail);
					},
				});
				runtimeMilestone("webgpu-device-ready", { xrCompatible: global.surrealWebGPUDeviceXRCompatible === true });
			} catch (error) {
				availableRenderers = availableRenderers.filter(renderer => renderer !== "webgpu");
				for (const provider of xrProviders) provider.setXRCompatibleAdapter(false, error.message || String(error));
				log("[runtime] WebGPU unavailable: " + (error.message || String(error)));
				runtimeMilestone("webgpu-device-unavailable", { name: error && error.name || "Error" });
			}
		}
		if (!availableRenderers.length)
			throw new LauncherError("RENDERER_UNAVAILABLE", "No renderer compiled in this build is available in this browser.");
		const launcherOptions = Object.assign({}, options.launcherOptions || {}, { availableRenderers });
		launcher = options.launcher || new LauncherController(options.launcherRoot || null, registry, launcherOptions);
		global.surrealCrashed = null;
		const unhandledRuntimeError = createUnhandledRuntimeErrorHandler(log, options.onRuntimeCrash, global);
		if (typeof global.addEventListener === "function") {
			global.addEventListener("error", unhandledRuntimeError);
			global.addEventListener("unhandledrejection", unhandledRuntimeError);
		}
		return new Promise((resolve, reject) => {
			const engineFiles = options.engineFiles && typeof options.engineFiles === "object" ? options.engineFiles : null;
			const Module = {
				canvas: options.canvas,
				locateFile: path => engineFiles && typeof engineFiles[path] === "string" ?
					engineFiles[path] : (options.engineBase || "../build-emscripten/") + path,
				print: log,
				printErr: log,
				preinitializedWebGPUDevice: device || undefined,
				onAbort: createRuntimeAbortHandler(log, options.onRuntimeCrash, global),
				onRuntimeInitialized: async () => {
					runtimeMilestone("wasm-runtime-initialized");
					try {
						const importerOptions = Object.assign({}, options.importerOptions || {}, {
							storage: library.storageProxy(),
							pickDirectoryEntries: createHostPicker(options.hostBridge || global.SurrealHostBridge),
						});
						const browserDataOptions = {
							uiRoot: options.importerRoot || null,
							log,
							logSnapshot: options.logSnapshot,
							importerOptions,
							mutableOptionsForGame: gameId => library.mutableOptions(gameId),
							selectLaunch: context => { if (context.metadata) { library.register(context.metadata); libraryUI.refresh(); } return launcher.selectLaunch(context); },
							launch: selection => {
								activeRenderer = selection.renderer;
								return runLaunchBoundary({ Module, selection, registry,
									audioController: options.audioController, onLaunch: options.onLaunch,
									viewportController, xrController: options.xrController,
									onStartupStage: options.onStartupStage, environment: options.environment || global,
									waitForPaint: options.waitForPaint });
							},
						};
						let started;
						if (typeof global.SurrealBrowserData.BrowserDataController === "function") {
							// Keep the controller reachable while native main owns the awaited launch.
							// Mutable persistence is initialized before that boundary and can then be
							// checkpointed if WebGPU is lost while the game is still running.
							dataController = new global.SurrealBrowserData.BrowserDataController(Module, browserDataOptions);
							started = { controller: dataController, result: await dataController.start() };
						} else {
							started = await global.SurrealBrowserData.start(Module, browserDataOptions);
							dataController = started.controller;
						}
						runtimeMilestone("persistent-storage-ready", {
							state: started.result && started.result.import && started.result.import.state || null,
						});
						resolve(Object.freeze({ Module, launcher, registry, library, libraryUI, viewportController,
							xrController: options.xrController || null,
							dataController: started.controller, webGPUDeviceLossController, result: started.result }));
					} catch (error) { reject(error); }
				},
			};
			global.Module = Module;
			viewportController.attachModule(Module);
			if (options.audioController && typeof options.audioController.attachModule === "function") options.audioController.attachModule(Module);
			if (options.xrController && typeof options.xrController.attachModule === "function") options.xrController.attachModule(Module);
			const script = global.document.createElement("script");
			script.src = options.engineScript || DEFAULT_ENGINE_SCRIPT;
			script.onload = () => runtimeMilestone("engine-script-loaded");
			script.onerror = () => {
				runtimeMilestone("engine-script-failed");
				reject(new LauncherError("ENGINE_SCRIPT", "The SurrealEngine browser module could not be loaded."));
			};
			runtimeMilestone("engine-script-requested", { url: script.src });
			global.document.body.appendChild(script);
		});
	}

	global.SurrealBrowserApp = Object.freeze({
		LauncherError,
		WebGPUDeviceLossController,
		createRuntimeAbortHandler,
		createUnhandledRuntimeErrorHandler,
		PresentationRegistry,
		GameLibrary,
		GameLibraryUI,
		LauncherController,
		normalizeHostEntries,
		createHostPicker,
		buildNativeArguments,
		callNativeMain,
		activatePresentation,
		LaunchStartupTracker,
		waitForBrowserPaint,
		BrowserViewportController,
		runLaunchBoundary,
		acquireWebGPUDevice,
		start,
	});
})(typeof window !== "undefined" ? window : globalThis);
