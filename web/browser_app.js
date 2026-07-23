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
		if (renderer !== "webgpu" && renderer !== "null") throw new LauncherError("LAUNCH_RENDERER", "That renderer is not available in this browser build.");
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
			if (settings.audioController && typeof settings.audioController.engineStarted === "function")
				settings.audioController.engineStarted();
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
			this.renderer = root && root.querySelector("[data-launcher-renderer]");
			this.skipIntro = root && root.querySelector("[data-launcher-skip-intro]");
			this.game = root && root.querySelector("[data-launcher-game]");
			this.error = root && root.querySelector("[data-launcher-error]");
			this.form = root && root.querySelector("[data-launcher-form]");
			if (this.form) this.form.addEventListener("submit", event => this._submit(event));
			if (this.skipIntro) this.skipIntro.addEventListener("change", () => this._updateMapAvailability());
		}

		_preferences() {
			try { return JSON.parse(global.localStorage.getItem(PREFERENCE_KEY) || "{}"); }
			catch (_) { return {}; }
		}

		_savePreferences(selection) {
			try { global.localStorage.setItem(PREFERENCE_KEY, JSON.stringify({
				mapByGame: Object.assign({}, this._preferences().mapByGame || {}, { [selection.game.id]: selection.map }),
				presentationId: selection.presentationId,
				renderer: selection.renderer,
				skipIntro: selection.skipIntro,
			})); } catch (_) { /* Preferences are optional. */ }
		}

		_updateMapAvailability() {
			if (this.map) this.map.disabled = !!this.skipIntro && !this.skipIntro.checked;
		}

		_option(select, value, label) {
			if (!select) return;
			const option = global.document.createElement("option");
			option.value = value;
			option.textContent = label;
			select.appendChild(option);
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
				if (preferred && maps.some(map => map.toLowerCase() === preferred.toLowerCase())) this.map.value = preferred;
			}
			if (this.presentation) {
				this.presentation.textContent = "";
				for (const provider of this.registry.available()) this._option(this.presentation, provider.id, provider.label);
				if (this.registry.get(preferences.presentationId) && this.registry.get(preferences.presentationId).isAvailable()) {
					this.presentation.value = preferences.presentationId;
				}
			}
			if (this.renderer && preferences.renderer) this.renderer.value = preferences.renderer;
			if (this.skipIntro) this.skipIntro.checked = preferences.skipIntro !== false;
			this._updateMapAvailability();
			if (this.root) this.root.hidden = false;
			return new Promise((resolve, reject) => { this.pending = { resolve, reject }; });
		}

		async _submit(event) {
			event.preventDefault();
			if (!this.pending || !this.context) return;
			try {
				const provider = this.registry.get(this.presentation && this.presentation.value || "flat");
				if (!provider || !provider.isAvailable()) throw new LauncherError("PRESENTATION_UNAVAILABLE", "That presentation mode is unavailable.");
				const selection = Object.freeze({
					game: this.context.game,
					map: this.map && this.map.value || this.context.game.defaultMap,
					presentationId: provider.id,
					renderer: this.renderer && this.renderer.value || "webgpu",
					skipIntro: !this.skipIntro || this.skipIntro.checked,
				});
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
		device.lost.then(info => log("[runtime] WebGPU device lost: " + info.message));
		return device;
	}

	async function start(options) {
		options = options || {};
		const log = typeof options.log === "function" ? options.log : () => {};
		const registry = options.presentationRegistry || new PresentationRegistry();
		if (!registry.get("flat")) registry.register({ id: "flat", label: "Desktop window" });
		for (const provider of options.presentationProviders || []) registry.register(provider);
		const launcher = options.launcher || new LauncherController(options.launcherRoot || null, registry, options.launcherOptions);
		const library = options.library || new GameLibrary();
		const libraryUI = options.libraryUI || new GameLibraryUI(options.libraryRoot || null, library);
		const xrProviders = registry.available().filter(provider =>
			provider.requiresXRCompatibleAdapter || provider.prefersXRCompatibleAdapter);
		const device = await acquireWebGPUDevice(log, {
			xrCompatible: xrProviders.length > 0,
			onXRCompatibility: (available, detail) => {
				for (const provider of xrProviders) provider.setXRCompatibleAdapter(available, detail);
			},
		});
		return new Promise((resolve, reject) => {
			const Module = {
				canvas: options.canvas,
				locateFile: path => (options.engineBase || "../build-emscripten/") + path,
				print: log,
				printErr: log,
				preinitializedWebGPUDevice: device,
				onRuntimeInitialized: async () => {
					try {
						const importerOptions = Object.assign({}, options.importerOptions || {}, {
							storage: library.storageProxy(),
							pickDirectoryEntries: createHostPicker(options.hostBridge || global.SurrealHostBridge),
						});
						const started = await global.SurrealBrowserData.start(Module, {
							uiRoot: options.importerRoot || null,
							log,
							logSnapshot: options.logSnapshot,
							importerOptions,
							mutableOptionsForGame: gameId => library.mutableOptions(gameId),
							selectLaunch: context => { if (context.metadata) { library.register(context.metadata); libraryUI.refresh(); } return launcher.selectLaunch(context); },
							launch: selection => runLaunchBoundary({ Module, selection, registry,
								audioController: options.audioController, onLaunch: options.onLaunch,
								onStartupStage: options.onStartupStage, environment: options.environment || global,
								waitForPaint: options.waitForPaint }),
						});
						resolve(Object.freeze({ Module, launcher, registry, library, libraryUI, dataController: started.controller, result: started.result }));
					} catch (error) { reject(error); }
				},
			};
			global.Module = Module;
			if (options.audioController && typeof options.audioController.attachModule === "function") options.audioController.attachModule(Module);
			const script = global.document.createElement("script");
			script.src = options.engineScript || DEFAULT_ENGINE_SCRIPT;
			script.onerror = () => reject(new LauncherError("ENGINE_SCRIPT", "The SurrealEngine browser module could not be loaded."));
			global.document.body.appendChild(script);
		});
	}

	global.SurrealBrowserApp = Object.freeze({
		LauncherError,
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
		runLaunchBoundary,
		acquireWebGPUDevice,
		start,
	});
})(typeof window !== "undefined" ? window : globalThis);
