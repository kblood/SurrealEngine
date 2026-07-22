(function (global) {
	"use strict";

	const SCHEMA_NAME = "surrealengine-webxr-browser-launcher";
	const SCHEMA_VERSION = 1;
	const DEFAULT_MAP = "DM-Deck16][";
	const MAX_DIAGNOSTIC_LOGS = 40;
	const LOADING_PHASES = Object.freeze({
		"browser-preflight": "Checking browser WebXR and WebGPU support…",
		"webgpu-device": "Acquiring the WebGPU device…",
		"runtime-script": "Loading the SurrealEngine runtime…",
		"runtime-initialized": "SurrealEngine runtime initialized.",
		"import-data": "Waiting for validated local UT99 data…",
		"mutable-data": "Restoring local settings, saves, and diagnostics…",
		ready: "Local data and runtime are ready.",
		"engine-start": "Starting the selected local game…",
		running: "The local game is running.",
		crashed: "Startup or runtime stopped with an error.",
	});
	const GAMEPAD_POLICY = Object.freeze({
		activateButton: 0,
		directionButtons: Object.freeze({ up: 12, down: 13, left: 14, right: 15 }),
		axisThreshold: 0.65,
		neutralThreshold: 0.35,
		initialRepeatMs: 350,
		repeatMs: 120,
	});
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

	function normalizeMapManifest(manifest) {
		if (!manifest) {
			return Object.freeze({ state: "unavailable", maps: Object.freeze([]), rejectedCount: 0 });
		}
		if (typeof manifest !== "object" ||
			manifest.schema !== "surrealengine-ut99-map-manifest" || manifest.version !== 1 ||
			!Array.isArray(manifest.maps) || manifest.maps.length > 8192) {
			throw launcherError("map-manifest-invalid", "The imported map list is unavailable; manual entry remains available.");
		}
		const allowedStates = new Set(["ready", "empty", "unavailable", "cleared", "error"]);
		if (!allowedStates.has(manifest.state)) {
			throw launcherError("map-manifest-state", "The imported map list has an unsupported state.");
		}
		const candidates = [];
		let rejectedCount = Number.isSafeInteger(manifest.rejectedCount) && manifest.rejectedCount > 0 ?
			manifest.rejectedCount : 0;
		for (const candidate of manifest.maps) {
			const checked = validateMap(candidate);
			if (!checked.ok) {
				rejectedCount++;
				continue;
			}
			candidates.push(checked.value);
		}
		candidates.sort((a, b) => {
			const lowerA = a.toLowerCase();
			const lowerB = b.toLowerCase();
			if (lowerA < lowerB) return -1;
			if (lowerA > lowerB) return 1;
			return a < b ? -1 : (a > b ? 1 : 0);
		});
		const seen = new Set();
		const maps = [];
		for (const candidate of candidates) {
			const key = candidate.toLowerCase();
			if (seen.has(key)) continue;
			seen.add(key);
			maps.push(candidate);
		}
		let state = manifest.state;
		if (state === "ready" && !maps.length) state = "empty";
		if (state !== "ready") maps.length = 0;
		return Object.freeze({
			state: state,
			maps: Object.freeze(maps),
			rejectedCount: rejectedCount,
		});
	}

	function restartURL(value) {
		const url = new URL(value, global.location && global.location.href ? global.location.href : "http://localhost/");
		url.searchParams.set("launcher", "1");
		url.searchParams.delete("map");
		url.searchParams.delete("game");
		return url.href;
	}

	function retryURL(value, mapValue) {
		const url = new URL(value, global.location && global.location.href ? global.location.href : "http://localhost/");
		url.searchParams.set("launcher", "1");
		url.searchParams.delete("game");
		const checked = validateMap(mapValue);
		if (checked.ok) url.searchParams.set("map", checked.value);
		else url.searchParams.delete("map");
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
			this.loadingPhase = "browser-preflight";
			this.phaseHistory = ["browser-preflight"];
			this.focusGeneration = 0;
			this.inputGeneration = 0;
			this.actionGeneration = 0;
			this.actionPending = null;
			this.gamepadNeutralObserved = false;
			this.gamepadPreviousActivate = false;
			this.gamepadPreviousDirection = null;
			this.gamepadRepeatAt = 0;
			this.gamepadFrameRequest = 0;
			this.logs = [];
			this.mapRefreshGeneration = 0;
			this.mapPickerState = this.enabled ? "loading" : "inactive";
			this.mapInventory = Object.freeze([]);
			this.mapRejectedCount = 0;
			this.mapPickerRenderKey = null;
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
			this.mapPicker = this.root.querySelector("[data-launcher-map-picker]");
			this.mapRefreshButton = this.root.querySelector("[data-launcher-map-refresh]");
			this.startButton = this.root.querySelector("[data-launcher-start]");
			this.restartButton = this.root.querySelector("[data-launcher-restart]");
			this.copyButton = this.root.querySelector("[data-launcher-copy]");
			this.downloadButton = this.root.querySelector("[data-launcher-download]");
			this.retryButton = this.root.querySelector("[data-launcher-retry]");
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
			if (this.retryButton) this.retryButton.addEventListener("click", () => this.retry());
			if (this.restartButton) this.restartButton.addEventListener("click", () => this.restart());
			if (this.copyButton) this.copyButton.addEventListener("click", () => this.copyDiagnostics());
			if (this.downloadButton) this.downloadButton.addEventListener("click", () => this.downloadDiagnostics());
			if (this.mapRefreshButton) this.mapRefreshButton.addEventListener("click", () => this.refreshMaps());
			if (this.mapPicker) this.mapPicker.addEventListener("change", () => {
				if (!this.mapPicker.value || !this.mapInventory.includes(this.mapPicker.value)) return;
				if (this.mapInput) this.mapInput.value = this.mapPicker.value;
				this.map = this.mapPicker.value;
				this.initialMapError = null;
				this.render();
			});
			if (this.mapInput) this.mapInput.addEventListener("input", () => {
				this.initialMapError = null;
				this.render();
			});
			if (this.mapInput) this.mapInput.addEventListener("keydown", event => {
				if (event.key !== "Enter" || event.altKey || event.ctrlKey || event.metaKey || event.shiftKey ||
					this.state !== "ready") return;
				event.preventDefault();
				this.start();
			});
			if (this.presetInput) this.presetInput.addEventListener("change", () => this.render());
			if (this.enabled && typeof global.addEventListener === "function") {
				this.onGamepadConnected = () => this._syncGamepadPolling();
				global.addEventListener("gamepadconnected", this.onGamepadConnected);
			}
		}

		setLoadingPhase(phase) {
			if (!Object.prototype.hasOwnProperty.call(LOADING_PHASES, phase)) return false;
			if (this.state === "running" || this.state === "crashed") return false;
			this.loadingPhase = phase;
			if (this.phaseHistory[this.phaseHistory.length - 1] !== phase) {
				this.phaseHistory.push(phase);
				if (this.phaseHistory.length > 16) this.phaseHistory.shift();
			}
			this.render();
			return true;
		}

		_isInputInteractive() {
			return this.enabled && !this.actionPending && (this.state === "ready" || this.state === "crashed") &&
				this.root && !this.root.hidden;
		}

		_resetGamepadLatch() {
			this.inputGeneration++;
			this.gamepadNeutralObserved = false;
			this.gamepadPreviousActivate = false;
			this.gamepadPreviousDirection = null;
			this.gamepadRepeatAt = 0;
		}

		_cancelGamepadPolling() {
			if (this.gamepadFrameRequest && typeof global.cancelAnimationFrame === "function")
				global.cancelAnimationFrame(this.gamepadFrameRequest);
			this.gamepadFrameRequest = 0;
		}

		_syncGamepadPolling() {
			if (!this._isInputInteractive()) {
				this._cancelGamepadPolling();
				return;
			}
			if (this.gamepadFrameRequest || typeof global.requestAnimationFrame !== "function") return;
			const pads = safeCall(() => global.navigator && typeof global.navigator.getGamepads === "function" ?
				Array.from(global.navigator.getGamepads() || []) : [], []);
			if (!pads.some(pad => pad && pad.connected !== false && pad.mapping === "standard")) return;
			const generation = this.inputGeneration;
			const frame = timestamp => {
				this.gamepadFrameRequest = 0;
				if (generation !== this.inputGeneration || !this._isInputInteractive()) return;
				const current = safeCall(() => global.navigator && typeof global.navigator.getGamepads === "function" ?
					global.navigator.getGamepads() : [], []);
				this.processGamepads(current, timestamp);
				if (generation === this.inputGeneration && this._isInputInteractive())
					this.gamepadFrameRequest = global.requestAnimationFrame(frame);
			};
			this.gamepadFrameRequest = global.requestAnimationFrame(frame);
		}

		_focusableControls() {
			if (!this.root) return [];
			return Array.from(this.root.querySelectorAll("input, select, button, [href]"))
				.filter(control => !control.disabled && !control.hidden && !control.closest("[hidden]") &&
					control.getAttribute("tabindex") !== "-1");
		}

		_scheduleFocus(control) {
			if (!control || typeof control.focus !== "function") return;
			const generation = ++this.focusGeneration;
			Promise.resolve().then(() => {
				if (generation === this.focusGeneration && this._isInputInteractive() &&
					!control.disabled && !control.hidden) control.focus();
			});
		}

		_transitionState(state, preferredFocus) {
			this.state = state;
			this.actionGeneration++;
			this.actionPending = null;
			this._resetGamepadLatch();
			this._cancelGamepadPolling();
			this.render();
			if (preferredFocus) this._scheduleFocus(preferredFocus);
			this._syncGamepadPolling();
		}

		_gamepadButton(gamepad, index) {
			const button = gamepad && gamepad.buttons && gamepad.buttons[index];
			return !!(button && (button.pressed || Number(button.value) >= 0.5));
		}

		_gamepadDirection(gamepad) {
			const axes = gamepad && gamepad.axes ? gamepad.axes : [];
			if (this._gamepadButton(gamepad, GAMEPAD_POLICY.directionButtons.up) || Number(axes[1]) <= -GAMEPAD_POLICY.axisThreshold) return "up";
			if (this._gamepadButton(gamepad, GAMEPAD_POLICY.directionButtons.down) || Number(axes[1]) >= GAMEPAD_POLICY.axisThreshold) return "down";
			if (this._gamepadButton(gamepad, GAMEPAD_POLICY.directionButtons.left) || Number(axes[0]) <= -GAMEPAD_POLICY.axisThreshold) return "left";
			if (this._gamepadButton(gamepad, GAMEPAD_POLICY.directionButtons.right) || Number(axes[0]) >= GAMEPAD_POLICY.axisThreshold) return "right";
			return null;
		}

		_gamepadIsNeutral(gamepad) {
			const axes = gamepad && gamepad.axes ? gamepad.axes : [];
			return !this._gamepadButton(gamepad, GAMEPAD_POLICY.activateButton) &&
				!Object.values(GAMEPAD_POLICY.directionButtons).some(index => this._gamepadButton(gamepad, index)) &&
				Math.abs(Number(axes[0]) || 0) < GAMEPAD_POLICY.neutralThreshold &&
				Math.abs(Number(axes[1]) || 0) < GAMEPAD_POLICY.neutralThreshold;
		}

		_moveGamepadFocus(direction) {
			const controls = this._focusableControls();
			if (!controls.length) return false;
			let index = controls.indexOf(this.root.ownerDocument.activeElement);
			if (index < 0) index = direction === "up" ? 0 : -1;
			index = (index + (direction === "up" ? -1 : 1) + controls.length) % controls.length;
			controls[index].focus();
			return true;
		}

		_adjustFocusedSelect(direction) {
			const select = this.root && this.root.ownerDocument.activeElement;
			if (!select || select.tagName !== "SELECT" || select.disabled) return false;
			const options = Array.from(select.options).filter(option => !option.disabled);
			if (!options.length) return false;
			let index = options.indexOf(select.selectedOptions[0]);
			if (index < 0) index = direction === "left" ? 0 : -1;
			index = (index + (direction === "left" ? -1 : 1) + options.length) % options.length;
			select.value = options[index].value;
			select.dispatchEvent(new Event("input", { bubbles: true }));
			select.dispatchEvent(new Event("change", { bubbles: true }));
			return true;
		}

		_activateFocusedControl() {
			const control = this.root && this.root.ownerDocument.activeElement;
			if (!control || !this.root.contains(control) || control.disabled || control.tagName !== "BUTTON") return false;
			control.click();
			return true;
		}

		processGamepads(gamepads, timestamp) {
			if (!this._isInputInteractive()) return { handled: false, reason: "inactive" };
			const gamepad = Array.from(gamepads || []).find(candidate =>
				candidate && candidate.connected !== false && candidate.mapping === "standard");
			if (!gamepad) return { handled: false, reason: "no-standard-gamepad" };
			if (!this.gamepadNeutralObserved) {
				if (!this._gamepadIsNeutral(gamepad)) return { handled: false, reason: "awaiting-neutral" };
				this.gamepadNeutralObserved = true;
				return { handled: false, reason: "armed" };
			}

			const now = Number.isFinite(timestamp) ? timestamp : 0;
			const direction = this._gamepadDirection(gamepad);
			let handled = false;
			if (direction && direction !== this.gamepadPreviousDirection) {
				handled = direction === "up" || direction === "down" ?
					this._moveGamepadFocus(direction) : this._adjustFocusedSelect(direction);
				this.gamepadRepeatAt = now + GAMEPAD_POLICY.initialRepeatMs;
			} else if (direction && now >= this.gamepadRepeatAt) {
				handled = direction === "up" || direction === "down" ?
					this._moveGamepadFocus(direction) : this._adjustFocusedSelect(direction);
				this.gamepadRepeatAt = now + GAMEPAD_POLICY.repeatMs;
			}
			this.gamepadPreviousDirection = direction;

			const activate = this._gamepadButton(gamepad, GAMEPAD_POLICY.activateButton);
			if (activate && !this.gamepadPreviousActivate) handled = this._activateFocusedControl() || handled;
			this.gamepadPreviousActivate = activate;
			return { handled: handled, reason: handled ? "launcher-control" : "no-launcher-action" };
		}

		recordLog(line) {
			const event = sanitizedLogEvent(line);
			if (!event) return;
			this.logs.push(event);
			if (this.logs.length > MAX_DIAGNOSTIC_LOGS) this.logs.shift();
		}

		async refreshMaps() {
			if (!this.enabled) return { state: "inactive", maps: [] };
			const generation = ++this.mapRefreshGeneration;
			this.mapPickerState = "refreshing";
			this.render();
			try {
				const manifest = typeof this.options.mapManifest === "function" ?
					await this.options.mapManifest() : null;
				if (generation !== this.mapRefreshGeneration) return null;
				const normalized = normalizeMapManifest(manifest);
				this.mapPickerState = normalized.state;
				this.mapInventory = normalized.maps;
				this.mapRejectedCount = normalized.rejectedCount;
				this.render();
				return normalized;
			} catch (_error) {
				if (generation !== this.mapRefreshGeneration) return null;
				this.mapPickerState = "error";
				this.mapInventory = Object.freeze([]);
				this.mapRejectedCount = 0;
				this.render();
				return { state: "error", maps: [] };
			}
		}

		dataReady(dataMode, launch) {
			if (typeof launch !== "function") throw launcherError("launch-callback", "Launcher callback is missing.");
			this.dataMode = safeToken(dataMode, "ready");
			this.pendingLaunch = launch;
			if (this.enabled) {
				this.loadingPhase = "ready";
				if (this.phaseHistory[this.phaseHistory.length - 1] !== "ready") this.phaseHistory.push("ready");
				this._transitionState("ready", this.mapInput);
				this.refreshMaps();
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
			this.loadingPhase = "engine-start";
			if (this.phaseHistory[this.phaseHistory.length - 1] !== "engine-start") this.phaseHistory.push("engine-start");
			this.launchAttempts++;
			this._transitionState("booting", null);
			return this.pendingLaunch(selection);
		}

		engineStarted() {
			this.loadingPhase = "running";
			if (this.phaseHistory[this.phaseHistory.length - 1] !== "running") this.phaseHistory.push("running");
			this._transitionState("running", null);
		}

		recordCrash(error, phase) {
			this.loadingPhase = "crashed";
			if (this.phaseHistory[this.phaseHistory.length - 1] !== "crashed") this.phaseHistory.push("crashed");
			this.crash = {
				phase: safeToken(phase, "runtime"),
				name: safeToken(error && error.name, "Error"),
				message: sanitizeDiagnosticText(error && error.message ? error.message : error, 300),
			};
			if (typeof this.options.onCrash === "function") this.options.onCrash(Object.assign({}, this.crash));
			this._transitionState("crashed", this.retryButton || this.restartButton);
		}

		async _navigationAction(kind, target) {
			if (this.actionPending) return false;
			const callback = this.options[kind];
			if (typeof callback !== "function" && (!global.location || typeof global.location.assign !== "function"))
				return false;
			const generation = ++this.actionGeneration;
			this.actionPending = kind;
			this._resetGamepadLatch();
			this._cancelGamepadPolling();
			this.render();
			try {
				if (typeof callback === "function") await callback(target);
				else global.location.assign(target);
				if (generation === this.actionGeneration) {
					this.actionPending = null;
					this.render();
					this._syncGamepadPolling();
				}
				return target;
			} catch (_error) {
				if (generation === this.actionGeneration) {
					this.actionPending = null;
					this._setActionStatus("Navigation was refused; diagnostics remain available.", true);
					this.render();
					this._syncGamepadPolling();
				}
				return false;
			}
		}

		retry() {
			if (this.state !== "crashed") return false;
			const current = this.options.location && this.options.location.href ?
				this.options.location.href : global.location.href;
			const map = this.selection ? this.selection.map : (this.mapInput ? this.mapInput.value : this.map);
			return this._navigationAction("retry", retryURL(current, map));
		}

		restart() {
			if (this.state !== "running" && this.state !== "crashed") return false;
			const current = this.options.location && this.options.location.href ?
				this.options.location.href : global.location.href;
			const target = restartURL(current);
			return this._navigationAction("restart", target);
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
					loadingPhase: safeToken(this.loadingPhase, "unknown"),
					actionPending: safeToken(this.actionPending, null),
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
			this.root.setAttribute("aria-busy", String(this.state === "loading" || this.state === "booting"));
			if (!this.enabled) return;
			const status = this.root.querySelector("[data-launcher-status]");
			const error = this.root.querySelector("[data-launcher-error]");
			const mapStatus = this.root.querySelector("[data-launcher-map-status]");
			const loading = this.root.querySelector("[data-launcher-loading]");
			const loadingProgress = this.root.querySelector("[data-launcher-loading-progress]");
			const loadingPhase = this.root.querySelector("[data-launcher-loading-phase]");
			const messages = {
				loading: "Preparing the runtime and your local game data…",
				ready: "Local game data is ready. Choose a map and deliberately start the offline game.",
				booting: "Starting the selected local game…",
				running: "The local game is running.",
				crashed: "The game stopped during startup or runtime. Export diagnostics before restarting.",
			};
			if (status) status.textContent = messages[this.state] || "Launcher is preparing.";
			const busy = this.state === "loading" || this.state === "booting";
			if (loading) loading.hidden = !busy;
			if (loadingProgress) loadingProgress.hidden = !busy;
			if (loadingPhase) loadingPhase.textContent = LOADING_PHASES[this.loadingPhase] || LOADING_PHASES["browser-preflight"];
			const validation = this.initialMapError || validateMap(this.mapInput ? this.mapInput.value : this.map);
			if (error) {
				error.hidden = validation.ok !== false && this.state !== "crashed";
				error.textContent = this.state === "crashed" && this.crash ?
					"Failure (" + this.crash.phase + "): " + this.crash.message :
					(validation.ok === false ? validation.message : "");
			}
			if (this.startButton) this.startButton.disabled = this.state !== "ready" || validation.ok === false || !!this.actionPending;
			if (this.mapInput) this.mapInput.disabled = this.state !== "ready";
			if (this.presetInput) this.presetInput.disabled = this.state !== "ready";
			if (this.mapPicker) {
				const current = this.mapInput ? this.mapInput.value.toLowerCase() : "";
				const renderKey = this.mapRefreshGeneration + ":" + this.mapPickerState;
				if (this.mapPickerRenderKey !== renderKey) {
					this.mapPicker.replaceChildren();
					const placeholder = this.root.ownerDocument.createElement("option");
					placeholder.value = "";
					placeholder.textContent = this.mapPickerState === "refreshing" ? "Refreshing map list…" :
						(this.mapPickerState === "ready" ? "Choose an imported map…" : "No browsable map list");
					this.mapPicker.appendChild(placeholder);
					for (const map of this.mapInventory) {
						const option = this.root.ownerDocument.createElement("option");
						option.value = map;
						option.textContent = map;
						this.mapPicker.appendChild(option);
					}
					this.mapPickerRenderKey = renderKey;
				}
				const selected = this.mapInventory.find(map => map.toLowerCase() === current);
				this.mapPicker.value = selected || "";
				this.mapPicker.disabled = this.state !== "ready" || this.mapPickerState !== "ready" || !this.mapInventory.length;
			}
			if (this.mapRefreshButton) {
				this.mapRefreshButton.disabled = this.state !== "ready" || this.mapPickerState === "refreshing";
			}
			if (mapStatus) {
				const mapMessages = {
					loading: "Map list is waiting for validated import metadata.",
					refreshing: "Refreshing the compatible imported map list…",
					ready: this.mapInventory.length + " compatible local map" +
						(this.mapInventory.length === 1 ? "" : "s") + " found.",
					empty: "No compatible DM/CTF/DOM/AS Maps/*.unr entries were found; manual entry remains available.",
					cleared: "The saved import was cleared; manual entry remains available until new data is imported.",
					unavailable: "Map browsing is unavailable for this data mode; manual entry remains available.",
					error: "The imported map list could not be refreshed; manual entry remains available.",
				};
				mapStatus.textContent = mapMessages[this.mapPickerState] || mapMessages.unavailable;
				mapStatus.dataset.state = this.mapPickerState;
			}
			if (this.retryButton) {
				this.retryButton.hidden = this.state !== "crashed";
				this.retryButton.disabled = !!this.actionPending;
				this.retryButton.textContent = this.actionPending === "retry" ? "Retrying…" : "Retry startup";
			}
			if (this.restartButton) {
				this.restartButton.hidden = this.state !== "running" && this.state !== "crashed";
				this.restartButton.disabled = !!this.actionPending;
				this.restartButton.textContent = this.actionPending === "restart" ? "Restarting…" : "Restart to launcher";
			}
			if (this.copyButton) this.copyButton.disabled = !!this.actionPending;
			if (this.downloadButton) this.downloadButton.disabled = !!this.actionPending;
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
		normalizeMapManifest,
		restartURL,
		retryURL,
		LOADING_PHASES,
		GAMEPAD_POLICY,
		sanitizeDiagnosticText,
		sanitizedLogEvent,
		create,
	});
})(globalThis);
