(function (global) {
	"use strict";

	const SCHEMA_NAME = "surrealengine-webxr-settings";
	const SCHEMA_VERSION = 1;
	const STORAGE_KEY = "surrealengine-webxr-settings-v1";

	const DEFINITIONS = Object.freeze([
		{ key: "turnMode", label: "Turn mode", type: "enum", getter: "Surreal_GetWebXRTurnMode", setter: "Surreal_SetWebXRTurnMode",
			options: [[0, "Snap"], [1, "Smooth"], [2, "Existing binding"], [3, "Disabled"]] },
		{ key: "movementReference", label: "Movement direction", type: "enum", getter: "Surreal_GetWebXRMovementReference", setter: "Surreal_SetWebXRMovementReference",
			options: [[0, "Body"], [1, "Head"], [2, "Dominant hand"]] },
		{ key: "dominantHand", label: "Dominant hand", type: "enum", getter: "Surreal_GetWebXRDominantHand", setter: "Surreal_SetWebXRDominantHand",
			options: [[1, "Left"], [2, "Right"]] },
		{ key: "recenterButton", label: "Recenter button", type: "button", getter: "Surreal_GetWebXRRecenterButton", setter: "Surreal_SetWebXRRecenterButton", min: 0, max: 12 },
		{ key: "menuButton", label: "Menu button", type: "button", getter: "Surreal_GetWebXRMenuButton", setter: "Surreal_SetWebXRMenuButton", min: 0, max: 12 },
		{ key: "snapTurnDegrees", label: "Snap angle (degrees)", type: "number", getter: "Surreal_GetWebXRSnapTurnDegrees", setter: "Surreal_SetWebXRSnapTurnDegrees", min: 15, max: 90, step: 1 },
		{ key: "smoothTurnRate", label: "Smooth turn (degrees/second)", type: "number", getter: "Surreal_GetWebXRSmoothTurnDegreesPerSecond", setter: "Surreal_SetWebXRSmoothTurnDegreesPerSecond", min: 15, max: 360, step: 5 },
		{ key: "hapticsEnabled", label: "Controller haptics", type: "boolean", getter: "Surreal_GetWebXRHapticsEnabled", setter: "Surreal_SetWebXRHapticsEnabled" },
		{ key: "hudEnabled", label: "Stereo HUD/menu plane", type: "boolean", getter: "Surreal_GetWebXRHudEnabled", setter: "Surreal_SetWebXRHudEnabled" },
		{ key: "hudDistanceUU", label: "HUD distance (UE units)", type: "number", getter: "Surreal_GetWebXRHudDistanceUU", setter: "Surreal_SetWebXRHudDistanceUU", min: 19.685, max: 157.4804, step: 0.1 },
		{ key: "hudFovDegrees", label: "HUD horizontal FOV", type: "number", getter: "Surreal_GetWebXRHudHorizontalFovDegrees", setter: "Surreal_SetWebXRHudHorizontalFovDegrees", min: 20, max: 75, step: 0.5 },
		{ key: "hudAspectRatio", label: "HUD aspect ratio", type: "number", getter: "Surreal_GetWebXRHudAspectRatio", setter: "Surreal_SetWebXRHudAspectRatio", min: 0.75, max: 2, step: 0.01 },
		{ key: "hudSafeArea", label: "HUD safe-area fraction", type: "number", getter: "Surreal_GetWebXRHudSafeAreaFraction", setter: "Surreal_SetWebXRHudSafeAreaFraction", min: 0.5, max: 1, step: 0.01 },
	]);

	function settingsError(code, message) {
		const error = new Error(message);
		error.code = code;
		return error;
	}

	function isPlainObject(value) {
		return value !== null && typeof value === "object" && !Array.isArray(value);
	}

	function validateValue(definition, value) {
		if (definition.type === "boolean") {
			if (typeof value !== "boolean") throw settingsError("invalid-value", definition.key + " must be boolean");
			return value;
		}
		if (typeof value !== "number" || !Number.isFinite(value))
			throw settingsError("invalid-value", definition.key + " must be finite");
		if (definition.type === "enum") {
			if (!Number.isInteger(value) || !definition.options.some(option => option[0] === value))
				throw settingsError("invalid-value", definition.key + " is not an allowed option");
			return value;
		}
		if (definition.type === "button" && !Number.isInteger(value))
			throw settingsError("invalid-value", definition.key + " must be an integer");
		if (value < definition.min || value > definition.max)
			throw settingsError("invalid-value", definition.key + " is outside its accepted range");
		return value;
	}

	function validateValues(candidate) {
		if (!isPlainObject(candidate)) throw settingsError("invalid-profile", "settings values must be an object");
		const result = {};
		for (const definition of DEFINITIONS) {
			if (!Object.prototype.hasOwnProperty.call(candidate, definition.key))
				throw settingsError("invalid-profile", "missing setting " + definition.key);
			result[definition.key] = validateValue(definition, candidate[definition.key]);
		}
		for (const key of Object.keys(candidate)) {
			if (!DEFINITIONS.some(definition => definition.key === key))
				throw settingsError("invalid-profile", "unknown setting " + key);
		}
		return result;
	}

	function parseStoredProfile(text) {
		let profile;
		try { profile = JSON.parse(text); }
		catch (_) { throw settingsError("invalid-json", "stored WebXR settings are not valid JSON"); }
		if (!isPlainObject(profile) || profile.schema !== SCHEMA_NAME || profile.version !== SCHEMA_VERSION)
			throw settingsError("unsupported-schema", "stored WebXR settings use an unsupported schema");
		return validateValues(profile.values);
	}

	function serializeProfile(values) {
		return JSON.stringify({ schema: SCHEMA_NAME, version: SCHEMA_VERSION, values: validateValues(values) });
	}

	function buttonOptions() {
		const result = [[0, "Disabled"]];
		for (let index = 1; index <= 6; index++) result.push([index, "Left button " + index]);
		for (let index = 1; index <= 6; index++) result.push([index + 6, "Right button " + index]);
		return result;
	}

	function createInput(documentObject, definition) {
		let input;
		if (definition.type === "enum" || definition.type === "button") {
			input = documentObject.createElement("select");
			for (const [value, label] of definition.type === "button" ? buttonOptions() : definition.options) {
				const option = documentObject.createElement("option");
				option.value = String(value);
				option.textContent = label;
				input.appendChild(option);
			}
		} else {
			input = documentObject.createElement("input");
			input.type = definition.type === "boolean" ? "checkbox" : "number";
			if (definition.type === "number") {
				input.min = String(definition.min);
				input.max = String(definition.max);
				input.step = String(definition.step);
			}
		}
		input.name = definition.key;
		input.id = "webxr-setting-" + definition.key;
		return input;
	}

	class SettingsController {
		constructor(options) {
			this.root = options.root;
			this.storage = options.storage || global.localStorage;
			this.log = typeof options.log === "function" ? options.log : function () {};
			this.module = options.module || null;
			this.values = null;
			this.storedValues = null;
			this.ready = false;
			this.lastError = null;
			this.render();
			this.loadStored();
		}

		loadStored() {
			const text = this.storage && this.storage.getItem(STORAGE_KEY);
			if (!text) return null;
			try {
				this.storedValues = parseStoredProfile(text);
				this.values = Object.assign({}, this.storedValues);
				this.writeForm(this.values);
				this.setStatus("Saved browser preferences are ready; waiting for the engine.", false);
				return this.storedValues;
			} catch (error) {
				this.lastError = error;
				this.setStatus("Stored settings were ignored: " + error.message, true);
				return null;
			}
		}

		ccall(name, returnType, argumentTypes, argumentsList) {
			if (!this.module || typeof this.module.ccall !== "function")
				throw settingsError("engine-not-ready", "the engine is not ready");
			return this.module.ccall(name, returnType, argumentTypes || [], argumentsList || []);
		}

		readEngine() {
			const values = {};
			for (const definition of DEFINITIONS) {
				const raw = this.ccall(definition.getter,
					definition.type === "number" ? "number" : "number");
				values[definition.key] = definition.type === "boolean" ? raw !== 0 : raw;
			}
			return validateValues(values);
		}

		apply(values, persist) {
			const accepted = validateValues(values);
			const previous = this.readEngine();
			const applied = [];
			try {
				for (const definition of DEFINITIONS) {
					const value = definition.type === "boolean" ? (accepted[definition.key] ? 1 : 0) : accepted[definition.key];
					const ok = this.ccall(definition.setter, "number", ["number"], [value]);
					if (ok !== 1) throw settingsError("native-rejected", "engine rejected " + definition.key);
					applied.push(definition);
				}
			} catch (error) {
				for (let index = applied.length - 1; index >= 0; index--) {
					const definition = applied[index];
					const value = definition.type === "boolean" ? (previous[definition.key] ? 1 : 0) : previous[definition.key];
					try { this.ccall(definition.setter, "number", ["number"], [value]); }
					catch (_) { /* Preserve the original rejection for diagnostics. */ }
				}
				throw error;
			}
			this.values = Object.assign({}, accepted);
			if (persist !== false) {
				this.storage.setItem(STORAGE_KEY, serializeProfile(accepted));
				this.storedValues = Object.assign({}, accepted);
			}
			this.writeForm(this.values);
			this.setStatus("WebXR settings applied and saved locally.", false);
			return this.values;
		}

		onEngineReady(moduleObject) {
			this.module = moduleObject;
			this.ready = true;
			this.setDisabled(false);
			try {
				if (this.storedValues) return this.apply(this.storedValues, false);
				this.values = this.readEngine();
				this.writeForm(this.values);
				this.setStatus("Showing current engine WebXR settings.", false);
				return this.values;
			} catch (error) {
				this.lastError = error;
				this.setStatus("Could not initialize WebXR settings: " + error.message, true);
				throw error;
			}
		}

		readForm() {
			const values = {};
			for (const definition of DEFINITIONS) {
				const input = this.form.elements.namedItem(definition.key);
				values[definition.key] = definition.type === "boolean" ? input.checked : Number(input.value);
			}
			return validateValues(values);
		}

		writeForm(values) {
			if (!values || !this.form) return;
			for (const definition of DEFINITIONS) {
				const input = this.form.elements.namedItem(definition.key);
				if (definition.type === "boolean") input.checked = values[definition.key];
				else input.value = String(values[definition.key]);
			}
		}

		setDisabled(disabled) {
			for (const element of this.form.elements) element.disabled = disabled;
		}

		setStatus(message, error) {
			this.status.textContent = message;
			this.status.dataset.error = error ? "true" : "false";
			this.log("[webxr-settings] " + message);
		}

		clearSaved() {
			this.storage.removeItem(STORAGE_KEY);
			this.storedValues = null;
			if (this.ready) {
				this.values = this.readEngine();
				this.writeForm(this.values);
			}
			this.setStatus("Saved browser preferences cleared; current engine values are unchanged.", false);
		}

		statusSnapshot() {
			return {
				schema: SCHEMA_NAME,
				version: SCHEMA_VERSION,
				ready: this.ready,
				hasStoredProfile: this.storedValues !== null,
				values: this.values ? Object.assign({}, this.values) : null,
				error: this.lastError ? { code: this.lastError.code || "error", message: this.lastError.message } : null,
			};
		}

		render() {
			if (!this.root || !this.root.ownerDocument)
				throw settingsError("missing-root", "a settings UI root is required");
			const documentObject = this.root.ownerDocument;
			this.root.hidden = false;
			this.root.replaceChildren();
			const heading = documentObject.createElement("h2");
			heading.textContent = "WebXR settings";
			this.root.appendChild(heading);
			const scope = documentObject.createElement("p");
			scope.className = "scope";
			scope.textContent = "Browser MVP: offline single-player and local bots. Multiplayer is unsupported.";
			this.root.appendChild(scope);
			this.form = documentObject.createElement("form");
			this.form.noValidate = true;
			const grid = documentObject.createElement("div");
			grid.className = "settings-grid";
			for (const definition of DEFINITIONS) {
				const label = documentObject.createElement("label");
				label.htmlFor = "webxr-setting-" + definition.key;
				label.append(documentObject.createTextNode(definition.label + " "));
				label.appendChild(createInput(documentObject, definition));
				grid.appendChild(label);
			}
			this.form.appendChild(grid);
			const actions = documentObject.createElement("div");
			actions.className = "actions";
			const apply = documentObject.createElement("button");
			apply.type = "submit";
			apply.textContent = "Apply and save";
			const reload = documentObject.createElement("button");
			reload.type = "button";
			reload.textContent = "Reload engine values";
			const clear = documentObject.createElement("button");
			clear.type = "button";
			clear.textContent = "Clear saved preferences";
			actions.append(apply, reload, clear);
			this.form.appendChild(actions);
			this.root.appendChild(this.form);
			this.status = documentObject.createElement("p");
			this.status.className = "status";
			this.status.setAttribute("aria-live", "polite");
			this.root.appendChild(this.status);
			this.form.addEventListener("submit", event => {
				event.preventDefault();
				try { this.apply(this.readForm(), true); }
				catch (error) { this.lastError = error; this.setStatus("Settings were not applied: " + error.message, true); }
			});
			reload.addEventListener("click", () => {
				try { this.values = this.readEngine(); this.writeForm(this.values); this.setStatus("Reloaded current engine values.", false); }
				catch (error) { this.lastError = error; this.setStatus("Could not reload settings: " + error.message, true); }
			});
			clear.addEventListener("click", () => this.clearSaved());
			this.setDisabled(true);
			this.setStatus("Waiting for the engine.", false);
		}
	}

	global.SurrealWebXRSettings = Object.freeze({
		SCHEMA_NAME,
		SCHEMA_VERSION,
		STORAGE_KEY,
		DEFINITIONS,
		validateValues,
		parseStoredProfile,
		serializeProfile,
		create: options => new SettingsController(options),
	});
})(typeof window !== "undefined" ? window : globalThis);
