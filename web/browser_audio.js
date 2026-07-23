/* User-gesture and lifecycle controller for the browser OpenAL device. */
(function (root) {
	"use strict";
	const STATE = Object.freeze({ 0: "unavailable", 1: "suspended", 2: "running", 3: "closed", 4: "interrupted" });
	class BrowserAudioController {
		constructor(element, environment) {
			this.root = element || null; this.environment = environment || root; this.Module = null; this.started = false;
			this.cachedStateCode = null; this.cachedCurrentTimeMs = 0;
			this.pendingLifecycle = null;
			this.unlock = this.root && this.root.querySelector("[data-audio-unlock]");
			this.mute = this.root && this.root.querySelector("[data-audio-mute]");
			this.volume = this.root && this.root.querySelector("[data-audio-volume]");
			this.state = this.root && this.root.querySelector("[data-audio-state]");
			this.error = this.root && this.root.querySelector("[data-audio-error]");
			this.onUnlock = () => this.resume(); this.onOutput = () => this.setOutput();
			this.onXRUserActivation = () => { if (this.started) this.resume(); };
			this.onVisibility = () => {
				if (this.environment.document && this.environment.document.visibilityState === "hidden") this.suspend();
				else if (this.started) this.resume();
				else this.refresh();
			};
			this.onPageHide = () => this.shutdown();
			this.onPresentation = () => { if (this.started) this.resume(); else this.refresh(); };
			this.onNativeCallGate = () => {
				if (this.environment.surrealXRNativeCallsBlocked !== true) {
					const pending = this.pendingLifecycle; this.pendingLifecycle = null;
					if (pending === "resume") this.call("Surreal_ResumeBrowserAudio", "number");
					else if (pending === "suspend") this.call("Surreal_SuspendBrowserAudio", "number");
					else if (pending === "shutdown") this.call("Surreal_ShutdownBrowserAudio", "number");
					if (this.started) this.setOutput(); else this.refresh();
				}
			};
			if (this.unlock) this.unlock.addEventListener("click", this.onUnlock);
			if (this.mute) this.mute.addEventListener("change", this.onOutput);
			if (this.volume) this.volume.addEventListener("input", this.onOutput);
			if (this.environment.document) this.environment.document.addEventListener("visibilitychange", this.onVisibility);
			this.environment.addEventListener("pagehide", this.onPageHide);
			this.environment.addEventListener("surrealwebxrpresentation", this.onPresentation);
			this.environment.addEventListener("surrealwebxraudiogesture", this.onXRUserActivation);
			this.environment.addEventListener("surrealnativecallgatechange", this.onNativeCallGate);
			this.refresh();
		}
		attachModule(Module) { this.Module = Module || null; this.refresh(); return this; }
		engineStarted() { this.started = true; this.setOutput(); return this.resume(); }
		call(name, returnType, argumentTypes, args) {
			if (!this.Module || typeof this.Module.ccall !== "function") return null;
			if (this.environment.surrealXRNativeCallsBlocked === true) return null;
			try {
				const result = this.Module.ccall(name, returnType || null, argumentTypes || [], args || []);
				if (name === "Surreal_GetBrowserAudioState" && Number.isFinite(result)) this.cachedStateCode = result;
				if (name === "Surreal_GetBrowserAudioCurrentTimeMs" && Number.isFinite(result)) this.cachedCurrentTimeMs = result;
				return result;
			} catch (_) { return null; }
		}
		resume() {
			if (this.environment.surrealXRNativeCallsBlocked === true) this.pendingLifecycle = "resume";
			const result = this.call("Surreal_ResumeBrowserAudio", "number"); this.refreshSoon(); return result;
		}
		suspend() {
			if (this.environment.surrealXRNativeCallsBlocked === true) this.pendingLifecycle = "suspend";
			const result = this.call("Surreal_SuspendBrowserAudio", "number"); this.refreshSoon(); return result;
		}
		shutdown() {
			if (this.environment.surrealXRNativeCallsBlocked === true) this.pendingLifecycle = "shutdown";
			return this.call("Surreal_ShutdownBrowserAudio", "number");
		}
		setOutput() {
			const volume = this.volume ? Number(this.volume.value) : 1, muted = !!(this.mute && this.mute.checked);
			this.call("Surreal_SetBrowserAudioOutput", null, ["number", "number"], [volume, muted ? 1 : 0]); this.refresh();
		}
		refreshSoon() { this.environment.setTimeout(() => this.refresh(), 50); }
		refresh() {
			const queried = this.call("Surreal_GetBrowserAudioState", "number");
			const code = queried === null && this.environment.surrealXRNativeCallsBlocked === true ? this.cachedStateCode : queried;
			if (this.state) this.state.textContent = code === null ? (this.started ? "initializing" : "waiting for game") : (STATE[code] || "unknown");
			if (this.unlock) this.unlock.hidden = code === 2 || !this.started;
			const native = this.environment.SurrealBrowserAudioNativeDiagnostics || {}, errorCode = typeof native.lastErrorCode === "string" ? native.lastErrorCode : "none";
			if (this.error) { this.error.hidden = errorCode === "none" || errorCode === "context-unavailable"; this.error.textContent = this.error.hidden ? "" : "Audio could not start (" + errorCode + "). Press Enable audio again."; }
			return this.diagnostics();
		}
		diagnostics() {
			const native = this.environment.SurrealBrowserAudioNativeDiagnostics || {};
			const queriedCode = this.call("Surreal_GetBrowserAudioState", "number");
			const queriedTime = this.call("Surreal_GetBrowserAudioCurrentTimeMs", "number");
			const code = queriedCode === null && this.environment.surrealXRNativeCallsBlocked === true ? this.cachedStateCode : queriedCode;
			const currentTime = queriedTime === null && this.environment.surrealXRNativeCallsBlocked === true ? this.cachedCurrentTimeMs : queriedTime;
			return Object.freeze({ state: code === null ? "runtime-unavailable" : (STATE[code] || "unknown"), currentTimeMs: Math.max(0, Number(currentTime) || 0),
				resumeAttempts: Math.max(0, Number(native.resumeAttempts) || 0), resumeSuccesses: Math.max(0, Number(native.resumeSuccesses) || 0), suspendAttempts: Math.max(0, Number(native.suspendAttempts) || 0), shutdowns: Math.max(0, Number(native.shutdowns) || 0),
				muted: !!native.muted, volume: Math.max(0, Math.min(1, Number(native.volume) || 0)), lastErrorCode: typeof native.lastErrorCode === "string" ? native.lastErrorCode : "none", lastErrorStage: typeof native.lastErrorStage === "string" ? native.lastErrorStage : "none" });
		}
	}
	root.SurrealBrowserAudio = Object.freeze({ BrowserAudioController, create: (element, environment) => new BrowserAudioController(element, environment) });
})(typeof window !== "undefined" ? window : globalThis);
