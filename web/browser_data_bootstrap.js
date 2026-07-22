/*
 * Provider-neutral coordination for legal game-data import and mutable data.
 *
 * The immutable imported dataset is materialized first. The mutable overlay is
 * then restored before native main() observes configuration or save files.
 * Rendering and presentation providers are deliberately outside this module.
 */
(function (global) {
	"use strict";

	const DEFAULT_MAP = "DM-Deck16][";

	class BrowserDataError extends Error {
		constructor(code, message) {
			super(message);
			this.name = "SurrealBrowserDataError";
			this.code = code;
		}
	}

	class BrowserDataController {
		constructor(Module, options) {
			this.Module = Module;
			this.options = options || {};
			this.importController = null;
			this.mutableController = null;
			this.importResult = null;
			this.mutableResult = { state: "not-started" };
			this.selectedMap = null;
			this.currentManifest = null;
			this.currentSelection = null;
			this.started = false;
		}

		_log(message) {
			if (typeof this.options.log === "function") this.options.log("[browser-data] " + message);
		}

		async _startMutable(gameId) {
			if (this.mutableController) return this.mutableResult;
			try {
				const gameOptions = typeof this.options.mutableOptionsForGame === "function" ?
					this.options.mutableOptionsForGame(gameId || "ut99") : {};
				const mutableOptions = Object.assign({}, this.options.mutableOptions || {}, gameOptions || {}, {
					log: this.options.log,
					logSnapshot: this.options.logSnapshot,
				});
				const started = await global.SurrealMutableData.start(this.Module, mutableOptions);
				this.mutableController = started.controller;
				this.mutableResult = started.result;
				this._log("mutable overlay " + started.result.state + " via " + started.result.backend);
			} catch (error) {
				this.mutableResult = Object.freeze({
					state: "unavailable",
					error: error && error.message ? error.message : String(error),
				});
				this._log("mutable persistence unavailable; continuing with imported baseline");
				if (this.options.requireMutablePersistence) throw error;
			}
			return this.mutableResult;
		}

		async start() {
			if (this.started) throw new BrowserDataError("ALREADY_STARTED", "Browser data bootstrap has already started.");
			if (!this.Module || !this.Module.FS) throw new BrowserDataError("NO_MODULE", "The runtime filesystem is unavailable.");
			if (!global.SurrealUT99Importer || !global.SurrealMutableData) {
				throw new BrowserDataError("MISSING_MODULE", "Browser data modules were not loaded.");
			}
			this.started = true;
			const importerOptions = Object.assign({}, this.options.importerOptions || {}, {
				uiRoot: this.options.uiRoot || null,
				log: this.options.log,
				beforeLaunch: async (mode, context) => {
					const gameId = context && context.metadata && context.metadata.gameId ||
						context && context.mapManifest && context.mapManifest.gameId || "ut99";
					await this._startMutable(gameId);
					if (typeof this.options.beforeLaunch === "function") await this.options.beforeLaunch(mode);
				},
				launch: async (mode, context) => {
					this.currentManifest = context && context.mapManifest;
					const metadata = context && context.metadata || null;
					const gameId = metadata && metadata.gameId || this.currentManifest && this.currentManifest.gameId || "ut99";
					const definitions = global.SurrealGameImporter && global.SurrealGameImporter.GAME_DEFINITIONS || {};
					const game = definitions[gameId] || definitions.ut99 || Object.freeze({ id: gameId, name: gameId, defaultMap: DEFAULT_MAP });
					const launchContext = Object.freeze({ mode, game, metadata, mapManifest: this.currentManifest });
					const requested = typeof this.options.selectLaunch === "function" ?
						(await this.options.selectLaunch(launchContext) || {}) : {};
					const preferredMap = requested.map || (typeof this.options.preferredMap === "function" ?
						await this.options.preferredMap(this.currentManifest) :
						(this.options.preferredMap || game.defaultMap || DEFAULT_MAP));
					this.selectedMap = global.SurrealUT99Importer.selectLaunchMap(this.currentManifest, preferredMap);
					this.currentSelection = Object.freeze(Object.assign({}, requested, {
						mode,
						game,
						metadata,
						map: this.selectedMap,
						mapManifest: this.currentManifest,
					}));
					if (typeof this.options.launch === "function") {
						await this.options.launch(this.currentSelection);
					}
				},
			});
			const started = await global.SurrealUT99Importer.start(this.Module, importerOptions);
			this.importController = started.controller;
			this.importResult = started.result;
			return this.status();
		}

		mapManifest() {
			const manifest = this.importController && typeof this.importController.mapManifest === "function" ?
				this.importController.mapManifest() : this.currentManifest;
			return manifest || null;
		}

		status() {
			return Object.freeze({
				import: this.importResult,
				mutable: this.mutableController && typeof this.mutableController.status === "function" ?
					this.mutableController.status() : this.mutableResult,
				selectedMap: this.selectedMap,
				selection: this.currentSelection,
				mapManifest: this.mapManifest(),
			});
		}

		flush(reason) {
			if (!this.mutableController) return Promise.resolve(this.mutableResult);
			return this.mutableController.flush(reason || "explicit-api");
		}

		clearMutableData() {
			if (!this.mutableController) throw new BrowserDataError("MUTABLE_NOT_READY", "Mutable persistence is not ready.");
			return this.mutableController.clear();
		}

		clearImportedData() {
			if (!this.importController) throw new BrowserDataError("IMPORT_NOT_READY", "Imported-data storage is not ready.");
			return this.importController.clearSavedImport();
		}
	}

	async function start(Module, options) {
		const controller = new BrowserDataController(Module, options);
		const result = await controller.start();
		return { controller, result };
	}

	global.SurrealBrowserData = Object.freeze({
		DEFAULT_MAP,
		BrowserDataError,
		BrowserDataController,
		start,
	});
})(typeof window !== "undefined" ? window : globalThis);
