/* Narrow lifecycle bridge to Emscripten's built-in OpenAL/WebAudio context. */
mergeInto(LibraryManager.library, {
	$SurrealBrowserAudioNative__deps: ["$AL"],
	$SurrealBrowserAudioNative: {
		diagnostics: { resumeAttempts: 0, resumeSuccesses: 0, suspendAttempts: 0, shutdowns: 0,
			lastErrorCode: "none", lastErrorStage: "none", muted: false, volume: 1 },
		context: function () { return AL.currentCtx && AL.currentCtx.audioCtx ? AL.currentCtx : null; },
		fail: function (stage, error) {
			this.diagnostics.lastErrorStage = stage;
			this.diagnostics.lastErrorCode = error && error.name === "NotAllowedError" ? "gesture-required" : "operation-failed";
		},
		expose: function () { globalThis.SurrealBrowserAudioNativeDiagnostics = this.diagnostics; }
	},
	surreal_browser_audio_resume_js__deps: ["$SurrealBrowserAudioNative"],
	surreal_browser_audio_resume_js: function () {
		var bridge = SurrealBrowserAudioNative, ctx = bridge.context();
		bridge.diagnostics.resumeAttempts++; bridge.expose();
		if (!ctx) { bridge.diagnostics.lastErrorCode = "context-unavailable"; bridge.diagnostics.lastErrorStage = "resume"; return -1; }
		if (ctx.audioCtx.state === "running") return 0;
		try {
			var pending = ctx.audioCtx.resume();
			if (pending && pending.then) pending.then(function () {
				bridge.diagnostics.resumeSuccesses++; bridge.diagnostics.lastErrorCode = "none"; bridge.diagnostics.lastErrorStage = "none";
			}, function (error) { bridge.fail("resume", error); });
			return 1;
		} catch (error) { bridge.fail("resume", error); return -2; }
	},
	surreal_browser_audio_suspend_js__deps: ["$SurrealBrowserAudioNative"],
	surreal_browser_audio_suspend_js: function () {
		var bridge = SurrealBrowserAudioNative, ctx = bridge.context();
		bridge.diagnostics.suspendAttempts++; bridge.expose();
		if (!ctx || ctx.audioCtx.state === "closed") return 0;
		try { ctx.audioCtx.suspend().catch(function (error) { bridge.fail("suspend", error); }); return 1; }
		catch (error) { bridge.fail("suspend", error); return -1; }
	},
	surreal_browser_audio_shutdown_js__deps: ["$SurrealBrowserAudioNative"],
	surreal_browser_audio_shutdown_js: function () {
		var bridge = SurrealBrowserAudioNative, ctx = bridge.context();
		bridge.diagnostics.shutdowns++; bridge.expose();
		if (!ctx || ctx.audioCtx.state === "closed") return 0;
		try { ctx.audioCtx.suspend().catch(function (error) { bridge.fail("shutdown", error); }); return 1; }
		catch (error) { bridge.fail("shutdown", error); return -1; }
	},
	surreal_browser_audio_set_output_js__deps: ["$SurrealBrowserAudioNative"],
	surreal_browser_audio_set_output_js: function (volume, muted) {
		var bridge = SurrealBrowserAudioNative, ctx = bridge.context();
		volume = Number.isFinite(volume) ? Math.max(0, Math.min(1, volume)) : 1;
		bridge.diagnostics.volume = volume; bridge.diagnostics.muted = !!muted; bridge.expose();
		if (ctx && ctx.gain && ctx.gain.gain) ctx.gain.gain.value = muted ? 0 : volume;
	},
	surreal_browser_audio_state_js__deps: ["$SurrealBrowserAudioNative"],
	surreal_browser_audio_state_js: function () {
		var ctx = SurrealBrowserAudioNative.context(); if (!ctx) return 0;
		return ctx.audioCtx.state === "running" ? 2 : ctx.audioCtx.state === "closed" ? 3 : ctx.audioCtx.state === "interrupted" ? 4 : 1;
	},
	surreal_browser_audio_current_time_ms_js__deps: ["$SurrealBrowserAudioNative"],
	surreal_browser_audio_current_time_ms_js: function () {
		var ctx = SurrealBrowserAudioNative.context();
		return ctx ? Math.min(2147483647, Math.floor(ctx.audioCtx.currentTime * 1000)) : 0;
	}
});
