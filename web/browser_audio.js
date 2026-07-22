/* User-gesture and lifecycle controller for the browser OpenAL device. */
(function (root) {
	"use strict";
	const STATE = Object.freeze({ 0: "unavailable", 1: "suspended", 2: "running", 3: "closed", 4: "interrupted" });
	class BrowserAudioController {
		constructor(element, environment) {
			this.root = element || null; this.environment = environment || root; this.Module = null; this.started = false;
			this.unlock = this.root && this.root.querySelector("[data-audio-unlock]");
			this.mute = this.root && this.root.querySelector("[data-audio-mute]");
			this.volume = this.root && this.root.querySelector("[data-audio-volume]");
			this.state = this.root && this.root.querySelector("[data-audio-state]");
			this.error = this.root && this.root.querySelector("[data-audio-error]");
			this.onUnlock = () => this.resume(); this.onOutput = () => this.setOutput();
			this.onVisibility = () => this.environment.document && this.environment.document.visibilityState === "hidden" ? this.suspend() : this.refresh();
			this.onPageHide = () => this.shutdown(); this.onPresentation = () => this.refresh();
			if (this.unlock) this.unlock.addEventListener("click", this.onUnlock);
			if (this.mute) this.mute.addEventListener("change", this.onOutput);
			if (this.volume) this.volume.addEventListener("input", this.onOutput);
			if (this.environment.document) this.environment.document.addEventListener("visibilitychange", this.onVisibility);
			this.environment.addEventListener("pagehide", this.onPageHide);
			this.environment.addEventListener("surrealwebxrpresentation", this.onPresentation);
			this.refresh();
		}
		attachModule(Module) { this.Module = Module || null; this.refresh(); return this; }
		engineStarted() { this.started = true; this.setOutput(); this.refresh(); }
		call(name, returnType, argumentTypes, args) {
			if (!this.Module || typeof this.Module.ccall !== "function") return null;
			try { return this.Module.ccall(name, returnType || null, argumentTypes || [], args || []); } catch (_) { return null; }
		}
		resume() { const result = this.call("Surreal_ResumeBrowserAudio", "number"); this.refreshSoon(); return result; }
		suspend() { const result = this.call("Surreal_SuspendBrowserAudio", "number"); this.refreshSoon(); return result; }
		shutdown() { return this.call("Surreal_ShutdownBrowserAudio", "number"); }
		setOutput() {
			const volume = this.volume ? Number(this.volume.value) : 1, muted = !!(this.mute && this.mute.checked);
			this.call("Surreal_SetBrowserAudioOutput", null, ["number", "number"], [volume, muted ? 1 : 0]); this.refresh();
		}
		refreshSoon() { this.environment.setTimeout(() => this.refresh(), 50); }
		refresh() {
			const code = this.call("Surreal_GetBrowserAudioState", "number");
			if (this.state) this.state.textContent = code === null ? (this.started ? "initializing" : "waiting for game") : (STATE[code] || "unknown");
			if (this.unlock) this.unlock.hidden = code === 2 || !this.started;
			const native = this.environment.SurrealBrowserAudioNativeDiagnostics || {}, errorCode = typeof native.lastErrorCode === "string" ? native.lastErrorCode : "none";
			if (this.error) { this.error.hidden = errorCode === "none" || errorCode === "context-unavailable"; this.error.textContent = this.error.hidden ? "" : "Audio could not start (" + errorCode + "). Press Enable audio again."; }
			return this.diagnostics();
		}
		diagnostics() {
			const native = this.environment.SurrealBrowserAudioNativeDiagnostics || {}, code = this.call("Surreal_GetBrowserAudioState", "number");
			return Object.freeze({ state: code === null ? "runtime-unavailable" : (STATE[code] || "unknown"), currentTimeMs: Math.max(0, Number(this.call("Surreal_GetBrowserAudioCurrentTimeMs", "number")) || 0),
				resumeAttempts: Math.max(0, Number(native.resumeAttempts) || 0), resumeSuccesses: Math.max(0, Number(native.resumeSuccesses) || 0), suspendAttempts: Math.max(0, Number(native.suspendAttempts) || 0), shutdowns: Math.max(0, Number(native.shutdowns) || 0),
				muted: !!native.muted, volume: Math.max(0, Math.min(1, Number(native.volume) || 0)), lastErrorCode: typeof native.lastErrorCode === "string" ? native.lastErrorCode : "none", lastErrorStage: typeof native.lastErrorStage === "string" ? native.lastErrorStage : "none" });
		}
	}
	root.SurrealBrowserAudio = Object.freeze({ BrowserAudioController, create: (element, environment) => new BrowserAudioController(element, environment) });
})(typeof window !== "undefined" ? window : globalThis);
