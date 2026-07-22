(function (global) {
	"use strict";

	const SCHEMA_NAME = "surrealengine-webxr-browser-launcher";
	const SCHEMA_VERSION = 1;
	const DEFAULT_MAP = "DM-Deck16][";
	const MAX_DIAGNOSTIC_LOGS = 40;
	const PRESETS = Object.freeze({
		default: Object.freeze({
			label: "Map default (offline)",
			prefix: null,
			gameClass: null,
		}),
		deathmatch: Object.freeze({
			label: "Deathmatch practice (offline)",
			prefix: "DM-",
			gameClass: "Botpack.DeathMatchPlus",
		}),
		ctf: Object.freeze({
			label: "Capture the Flag practice (offline)",
			prefix: "CTF-",
			gameClass: "Botpack.CTFGame",
		}),
		domination: Object.freeze({
			label: "Domination practice (offline)",
			prefix: "DOM-",
			gameClass: "Botpack.Domination",
		}),
		assault: Object.freeze({
			label: "Assault practice (offline)",
			prefix: "AS-",
			gameClass: "Botpack.Assault",
		}),
	});

	function launcherError(code, message) {
		const error = new Error(message);
		error.code = code;
		return error;
	}

	function validateMap(value) {
		if (typeof value !== "string")
			return { ok: false, code: "map-type", message: "Enter a local UT99 map package name." };
		const candidate = value.trim();
		if (!candidate)
			return { ok: false, code: "map-empty", message: "Enter a local UT99 map package name." };
		if (candidate.length > 72)
			return { ok: false, code: "map-too-long", message: "The map name is too long." };
		// A map is a package basename, not an Unreal/network URL. Deliberately
		// exclude dots, slashes, colons, query delimiters, percent escapes and
		// whitespace so user text can never create a second engine URL option.
		if (!/^(?:DM|CTF|DOM|AS)-[A-Za-z0-9][A-Za-z0-9_\-\[\]']{0,63}$/i.test(candidate)) {
			return {
				ok: false,
				code: "map-invalid",
				message: "Use a DM-, CTF-, DOM-, or AS- map package name without paths, .unr, spaces, or URL options.",
			};
		}
		return { ok: true, value: candidate };
	}

	function buildLocalSelection(mapValue, presetName) {
		const checked = validateMap(mapValue);
		if (!checked.ok) throw launcherError(checked.code, checked.message);
		const preset = PRESETS[presetName];
		if (!preset) throw launcherError("preset-invalid", "Choose one of the provided offline game presets.");
		if (preset.prefix && !checked.value.toUpperCase().startsWith(preset.prefix)) {
			throw launcherError("preset-map-mismatch",
				preset.label + " requires a " + preset.prefix + " map.");
		}
		const engineURL = preset.gameClass ? checked.value + "?game=" + preset.gameClass : checked.value;
		return Object.freeze({
			map: checked.value,
			preset: presetName,
			presetLabel: preset.label,
			engineURL: engineURL,
			networking: "offline-only",
		});
	}

	function restartURL(value) {
		const url = new URL(value, global.location && global.location.href ? global.location.href : "http://localhost/");
		url.searchParams.set("launcher", "1");
		url.searchParams.delete("map");
		url.searchParams.delete("game");
		return url.href;
	}

	function finiteNumber(value) {
		return typeof value === "number" && Number.isFinite(value) ? value : null;
	}

	function safeToken(value, fallback) {
		return typeof value === "string" && /^[A-Za-z0-9._-]{1,80}$/.test(value) ? value : fallback;
	}

	function sanitizeDiagnosticText(value, maximumLength) {
		let text = String(value == null ? "" : value).replace(/[\u0000-\u001f\u007f]+/g, " ");
		text = text.replace(/file:\/\/\/?[^\s"']+/gi, "[path]");
		// Fail closed once an absolute local path begins. Spaces and arbitrary
		// package names make guessing its end unsafe, and the tail is not needed
		// for a useful phase/code report.
		text = text.replace(/\b[A-Za-z]:[\\/].*$/g, "[path details omitted]");
		text = text.replace(/(?:^|\s)\/(?:gamedata|home|Users?|mnt|tmp|var|opt)(?:\/.*)?$/gi,
			match => (match.startsWith(" ") ? " " : "") + "[path details omitted]");
		text = text.replace(/(?:^|\s)(?:\.\.?[\\/])+[^\s"']+/g,
			match => (match.startsWith(" ") ? " " : "") + "[path]");
		text = text.replace(/\b[^\s"'\\/]+\.(?:u|unr|utx|uax|umx|uz2?|data)\b/gi, "[commercial-file]");
		text = text.trim();
		const limit = maximumLength || 240;
		return text.length > limit ? text.slice(0, limit) + "…" : text;
	}

	function safeCall(callback, fallback) {
		try {
			return typeof callback === "function" ? callback() : fallback;
		} catch (_error) {
			return fallback;
		}
	}

	function readinessDiagnostic(value) {
		if (!value || typeof value !== "object") return null;
		return {
			schema: safeToken(value.schema, null),
			version: finiteNumber(value.version),
			mode: safeToken(value.mode, "unknown"),
			lifecycleOnly: value.lifecycleOnly === true,
			productionPathRequested: value.productionPathRequested === true,
			productionPresentation: value.productionPresentation === true,
			canAttempt: value.canAttempt === true,
			productionSessionReady: value.productionSessionReady === true,
			nativePhase: safeToken(value.nativePhase, "unknown"),
			blockerCodes: Array.isArray(value.blockers) ? value.blockers.slice(0, 16)
				.map(item => safeToken(item && item.code, "unknown")) : [],
		};
	}

	function lifecycleDiagnostic(value) {
		value = value && typeof value === "object" ? value : {};
		return {
			visibilityHidden: value.visibilityHidden === true,
			visibilitySource: safeToken(value.visibilitySource, "unknown"),
			xrVisibilityState: safeToken(value.xrVisibilityState, null),
			audioPolicy: safeToken(value.audioPolicy, "unknown"),
			audioState: safeToken(value.audioState, null),
			shutdown: value.shutdown === true,
			deviceLost: value.deviceLost === true,
			lastExitReason: safeToken(value.lastExitReason, null),
			entryAttempts: finiteNumber(value.entryAttempts),
			sessionsStarted: finiteNumber(value.sessionsStarted),
			sessionsEnded: finiteNumber(value.sessionsEnded),
		};
	}

	function interactionDiagnostic(value) {
		value = value && typeof value === "object" ? value : {};
		return {
			schema: safeToken(value.schema, null),
			version: finiteNumber(value.version),
			active: value.active === true,
			generation: finiteNumber(value.generation),
			domOverlayType: safeToken(value.domOverlayType, null),
			exitPhase: safeToken(value.exitPhase, "unknown"),
			trackingPhase: safeToken(value.trackingPhase, "unknown"),
			connectedControllers: finiteNumber(value.connectedControllers),
			expectedControllers: finiteNumber(value.expectedControllers),
			trackingRecoveries: finiteNumber(value.trackingRecoveries),
		};
	}

	function storageDiagnostic(value) {
		value = value && typeof value === "object" ? value : {};
		const metadata = value.metadata && typeof value.metadata === "object" ? value.metadata : {};
		return {
			state: safeToken(value.state, "unknown"),
			mode: safeToken(value.mode, null),
			backend: safeToken(value.backend, null),
			fileCount: finiteNumber(value.fileCount) == null ?
				finiteNumber(metadata.fileCount) : finiteNumber(value.fileCount),
			totalBytes: finiteNumber(value.totalBytes) == null ?
				finiteNumber(metadata.totalBytes) : finiteNumber(value.totalBytes),
			requiresClear: value.requiresClear === true,
			automaticCheckpoints: value.automaticCheckpoints === true,
			errorCode: safeToken(value.error && value.error.code ? value.error.code : value.error, null),
		};
	}

	function rendererDiagnostic(value) {
		value = value && typeof value === "object" ? value : {};
		return {
			webGPUDeviceReady: value.webGPUDeviceReady === true,
			errorCount: finiteNumber(value.errorCount),
			drawCalls: finiteNumber(value.drawCalls),
			textureCount: finiteNumber(value.textureCount),
			bindGroupsCreated: finiteNumber(value.bindGroupsCreated),
			bindGroupCacheHits: finiteNumber(value.bindGroupCacheHits),
			bufferRollovers: finiteNumber(value.bufferRollovers),
		};
	}

	function sanitizedLogEvent(line) {
		if (typeof line !== "string" || !line.startsWith("[harness]")) return null;
		const safeFixed = [
			"runtime initialized, preparing local game data",
			"native WebGPU XR session path explicitly enabled",
			"WebGPU device acquired, loading wasm module",
		];
		for (const message of safeFixed) {
			if (line === "[harness] " + message) return line;
		}
		if (line.startsWith("[harness] game data ready ("))
			return "[harness] game data ready; launch gate reached";
		if (line.startsWith("[harness] launcher "))
			return sanitizeDiagnosticText(line, 180);
		if (/failed|threw|error|unavailable|rejection|lost/i.test(line))
			return "[harness] runtime failure; details omitted from export";
		return null;
	}

	class Controller {
		constructor(options) {
			this.options = options || {};
			this.root = this.options.root || null;
			this.query = this.options.query instanceof URLSearchParams ?
				this.options.query : new URLSearchParams(this.options.query || "");
			this.enabled = this.query.get("launcher") === "1";
			this.state = this.enabled ? "loading" : "automatic";
			this.dataMode = null;
			this.pendingLaunch = null;
			this.selection = null;
			this.crash = null;
			this.launchAttempts = 0;
			this.logs = [];
			this.initialMapError = null;
			const initialMap = this.query.has("map") ? this.query.get("map") : DEFAULT_MAP;
			const checked = validateMap(initialMap);
			this.map = checked.ok ? checked.value : DEFAULT_MAP;
			if (!checked.ok && this.query.has("map")) this.initialMapError = checked;
			this.preset = "default";
			this._bind();
			this.render();
		}

		_bind() {
			if (!this.root) return;
			this.mapInput = this.root.querySelector("[data-launcher-map]");
			this.presetInput = this.root.querySelector("[data-launcher-preset]");
			this.startButton = this.root.querySelector("[data-launcher-start]");
			this.restartButton = this.root.querySelector("[data-launcher-restart]");
			this.copyButton = this.root.querySelector("[data-launcher-copy]");
			this.downloadButton = this.root.querySelector("[data-launcher-download]");
			if (this.mapInput) this.mapInput.value = this.map;
			if (this.presetInput) {
				this.presetInput.replaceChildren();
				for (const [name, preset] of Object.entries(PRESETS)) {
					const option = this.root.ownerDocument.createElement("option");
					option.value = name;
					option.textContent = preset.label;
					this.presetInput.appendChild(option);
				}
			}
			if (this.startButton) this.startButton.addEventListener("click", () => this.start());
			if (this.restartButton) this.restartButton.addEventListener("click", () => this.restart());
			if (this.copyButton) this.copyButton.addEventListener("click", () => this.copyDiagnostics());
			if (this.downloadButton) this.downloadButton.addEventListener("click", () => this.downloadDiagnostics());
			if (this.mapInput) this.mapInput.addEventListener("input", () => {
				this.initialMapError = null;
				this.render();
			});
			if (this.presetInput) this.presetInput.addEventListener("change", () => this.render());
		}

		recordLog(line) {
			const event = sanitizedLogEvent(line);
			if (!event) return;
			this.logs.push(event);
			if (this.logs.length > MAX_DIAGNOSTIC_LOGS) this.logs.shift();
		}

		dataReady(dataMode, launch) {
			if (typeof launch !== "function") throw launcherError("launch-callback", "Launcher callback is missing.");
			this.dataMode = safeToken(dataMode, "ready");
			this.pendingLaunch = launch;
			if (this.enabled) {
				this.state = "ready";
				this.render();
				return { waitingForUser: true };
			}
			const requested = this.query.has("map") ? this.query.get("map") : DEFAULT_MAP;
			try {
				return this._launch(buildLocalSelection(requested, "default"));
			} catch (error) {
				this.recordCrash(error, "automatic-map-validation");
				return { waitingForUser: false, rejected: true, error: error.code || "launch-error" };
			}
		}

		start() {
			if (!this.enabled || this.state !== "ready" || !this.pendingLaunch) return false;
			const map = this.mapInput ? this.mapInput.value : this.map;
			const preset = this.presetInput ? this.presetInput.value : this.preset;
			try {
				return this._launch(buildLocalSelection(map, preset));
			} catch (error) {
				this.initialMapError = { code: error.code || "launch-error", message: error.message };
				this.render();
				return false;
			}
		}

		_launch(selection) {
			this.selection = selection;
			this.initialMapError = null;
			this.state = "booting";
			this.launchAttempts++;
			this.render();
			return this.pendingLaunch(selection);
		}

		engineStarted() {
			this.state = "running";
			this.render();
		}

		recordCrash(error, phase) {
			this.state = "crashed";
			this.crash = {
				phase: safeToken(phase, "runtime"),
				name: safeToken(error && error.name, "Error"),
				message: sanitizeDiagnosticText(error && error.message ? error.message : error, 300),
			};
			if (typeof this.options.onCrash === "function") this.options.onCrash(Object.assign({}, this.crash));
			this.render();
		}

		restart() {
			const current = this.options.location && this.options.location.href ?
				this.options.location.href : global.location.href;
			const target = restartURL(current);
			if (typeof this.options.restart === "function") this.options.restart(target);
			else global.location.assign(target);
			return target;
		}

		diagnostics() {
			const source = safeCall(this.options.diagnosticSources, {}) || {};
			const map = this.selection ? this.selection.map :
				(validateMap(this.mapInput ? this.mapInput.value : this.map).ok ?
					validateMap(this.mapInput ? this.mapInput.value : this.map).value : null);
			return {
				schema: "surrealengine-webxr-diagnostics",
				version: 1,
				launcher: {
					schema: SCHEMA_NAME,
					version: SCHEMA_VERSION,
					enabled: this.enabled,
					state: this.state,
					dataMode: this.dataMode,
					launchAttempts: this.launchAttempts,
					map: map,
					preset: this.selection ? this.selection.preset :
						safeToken(this.presetInput ? this.presetInput.value : this.preset, "default"),
					networking: "offline-only",
				},
				build: safeToken(source.build, "unknown"),
				browser: {
					userAgent: sanitizeDiagnosticText(source.userAgent || "unknown", 240),
					language: safeToken(source.language, "unknown"),
					secureContext: source.secureContext === true,
				},
				crash: this.crash ? Object.assign({}, this.crash) : null,
				readiness: readinessDiagnostic(source.readiness),
				lifecycle: lifecycleDiagnostic(source.lifecycle),
				interaction: interactionDiagnostic(source.interaction),
				renderer: rendererDiagnostic(source.renderer),
				storage: {
					importer: storageDiagnostic(source.importer),
					mutable: storageDiagnostic(source.mutable),
				},
				recentLogs: this.logs.slice(-MAX_DIAGNOSTIC_LOGS),
				notIncluded: ["file contents", "local file paths", "commercial game data", "stack traces"],
			};
		}

		async copyDiagnostics() {
			const payload = JSON.stringify(this.diagnostics(), null, 2);
			if (!global.navigator.clipboard || typeof global.navigator.clipboard.writeText !== "function") {
				this._setActionStatus("Clipboard access is unavailable; use Download diagnostics.", true);
				return false;
			}
			try {
				await global.navigator.clipboard.writeText(payload);
				this._setActionStatus("Diagnostics copied.", false);
				return true;
			} catch (_error) {
				this._setActionStatus("The browser refused clipboard access; use Download diagnostics.", true);
				return false;
			}
		}

		downloadDiagnostics() {
			const blob = new Blob([JSON.stringify(this.diagnostics(), null, 2) + "\n"],
				{ type: "application/json" });
			const href = URL.createObjectURL(blob);
			const anchor = this.root.ownerDocument.createElement("a");
			anchor.href = href;
			anchor.download = "surrealengine-webxr-diagnostics.json";
			anchor.click();
			setTimeout(() => URL.revokeObjectURL(href), 0);
			this._setActionStatus("Diagnostics download created.", false);
			return true;
		}

		_setActionStatus(message, error) {
			if (!this.root) return;
			const target = this.root.querySelector("[data-launcher-action-status]");
			if (target) {
				target.textContent = message;
				target.dataset.error = String(!!error);
			}
		}

		render() {
			if (!this.root) return;
			this.root.hidden = !this.enabled;
			this.root.dataset.state = this.state;
			if (!this.enabled) return;
			const status = this.root.querySelector("[data-launcher-status]");
			const error = this.root.querySelector("[data-launcher-error]");
			const messages = {
				loading: "Preparing the runtime and your local game data…",
				ready: "Local game data is ready. Choose a map and deliberately start the offline game.",
				booting: "Starting the selected local game…",
				running: "The local game is running.",
				crashed: "The game stopped during startup or runtime. Export diagnostics before restarting.",
			};
			if (status) status.textContent = messages[this.state] || "Launcher is preparing.";
			const validation = this.initialMapError || validateMap(this.mapInput ? this.mapInput.value : this.map);
			if (error) {
				error.hidden = validation.ok !== false && this.state !== "crashed";
				error.textContent = this.state === "crashed" && this.crash ?
					"Failure (" + this.crash.phase + "): " + this.crash.message :
					(validation.ok === false ? validation.message : "");
			}
			if (this.startButton) this.startButton.disabled = this.state !== "ready" || validation.ok === false;
			if (this.mapInput) this.mapInput.disabled = this.state !== "ready";
			if (this.presetInput) this.presetInput.disabled = this.state !== "ready";
			if (this.restartButton) this.restartButton.hidden = this.state !== "running" && this.state !== "crashed";
		}
	}

	function create(options) {
		return new Controller(options);
	}

	global.SurrealWebXRLauncher = Object.freeze({
		SCHEMA_NAME,
		SCHEMA_VERSION,
		DEFAULT_MAP,
		PRESETS,
		validateMap,
		buildLocalSelection,
		restartURL,
		sanitizeDiagnosticText,
		sanitizedLogEvent,
		create,
	});
})(globalThis);
