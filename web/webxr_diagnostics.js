/* Optional, data-free WebXR headset diagnostics for the packaged launcher. */
(function (root) {
	"use strict";

	const UNKNOWN = "unknown";
	const PROVIDER_ERROR_CODES = new Set([
		"binding-creation-failed", "bridge-timing-active", "engine-loop-rejected",
		"feature-negotiation-unobservable", "frame-failed", "incompatible-native-frame-abi",
		"input-submit-failed", "input-transition-overflow", "invalid-projection-subimage",
		"invalid-view-pose", "native-callback-overflow", "no-webxr-presentation-backend",
		"presentation-preference-active", "projection-layer-failed", "reference-space-failed",
		"secure-context-required", "session-end-rejected", "session-ended-before-activation",
		"session-ended-before-first-frame", "session-request-failed",
		"unsupported-color-format", "unsupported-view-configuration", "webgl-bridge-creation-failed",
		"webgpu-device-not-ready", "webxr-provider-failed", "webxr-unavailable",
		"webxr-webgl-bridge-unavailable", "webxr-webgpu-binding-unavailable",
		"native-frame-rejected-0", "native-frame-rejected-1", "native-frame-rejected-2",
		"native-frame-rejected-3", "native-frame-rejected-4", "native-frame-rejected-5",
		"native-frame-rejected-6",
	]);
	function token(value) {
		if (value === null || value === undefined || value === "") return UNKNOWN;
		const cleaned = String(value).toLowerCase().replace(/[^a-z0-9_.-]+/g, "-").replace(/^-+|-+$/g, "");
		return cleaned.slice(0, 64) || UNKNOWN;
	}

	function number(value) {
		const parsed = Number(value);
		return Number.isFinite(parsed) && parsed >= 0 ? Math.floor(parsed) : 0;
	}

	function measurement(value) {
		if (value === null || value === undefined || value === "") return UNKNOWN;
		const parsed = Number(value);
		return Number.isFinite(parsed) && parsed >= 0 ? Math.round(parsed * 1000) / 1000 : UNKNOWN;
	}

	function dimension(value) {
		const parsed = Number(value);
		return Number.isInteger(parsed) && parsed > 0 ? parsed : UNKNOWN;
	}

	function knownPresentationMode(value) {
		return value === "direct-webgpu" || value === "webgl-bridge" ? value : UNKNOWN;
	}

	function knownPresentationPreference(value) {
		return value === "auto" || value === "webgl-bridge" ? value : UNKNOWN;
	}

	function knownProviderErrorCode(value) {
		return PROVIDER_ERROR_CODES.has(value) ? value : UNKNOWN;
	}

	function dimensions(width, height) {
		return width === UNKNOWN || height === UNKNOWN ? UNKNOWN : width + "x" + height;
	}

	function normalized(snapshot) {
		const source = snapshot || {};
		const bridge = source.bridgeDiagnostics && typeof source.bridgeDiagnostics === "object" ?
			source.bridgeDiagnostics : {};
		return Object.freeze({
			capabilityCode: token(source.capabilityCode),
			capabilityAvailable: source.capabilityAvailable === true ? "yes" :
				source.capabilityAvailable === false ? "no" : UNKNOWN,
			adapterState: token(source.adapterState),
			phase: token(source.phase),
			stage: token(source.currentStage),
			errorStage: token(source.lastErrorStage),
			errorCode: knownProviderErrorCode(source.lastErrorCode),
			lastError: source.lastError ? "present" : "none",
			frames: number(source.frames),
			skippedFrames: number(source.skippedFrames),
			inputPackets: number(source.inputPackets),
			projectionFormat: token(source.projectionFormat),
			referenceSpaceType: token(source.referenceSpaceType),
			presentationMode: knownPresentationMode(source.presentationMode),
			presentationPreference: knownPresentationPreference(source.presentationPreference),
			layerWidth: dimension(source.layerWidth ?? bridge.layerWidth),
			layerHeight: dimension(source.layerHeight ?? bridge.layerHeight),
			atlasWidth: dimension(source.atlasWidth ?? bridge.atlasWidth),
			atlasHeight: dimension(source.atlasHeight ?? bridge.atlasHeight),
			bridgeFrames: number(bridge.frames),
			bridgeErrors: number(bridge.errors),
			bridgeSamples: number(bridge.samples),
			bridgeMedianMs: measurement(bridge.medianMs),
			bridgeP95Ms: measurement(bridge.p95Ms),
			bridgeP99Ms: measurement(bridge.p99Ms),
			bridgeBlockingTiming: bridge.blockingTiming === true ? "yes" :
				bridge.blockingTiming === false ? "no" : UNKNOWN,
			enterAttempts: number(source.enterAttempts),
			successfulEntries: number(source.successfulEntries),
			exitRequests: number(source.exitRequests),
			endedSessions: number(source.endedSessions),
			reentries: number(source.reentries),
			transitions: Array.from(source.transitions || []).slice(-16).map(item =>
				token(item && item.type) + "@" + number(item && item.generation) + "(" + token(item && item.stage) + ")"),
		});
	}

	function formatReport(snapshot) {
		const state = normalized(snapshot);
		return [
			"SurrealEngine WebXR headset report",
			"schema: surrealengine-webxr-headset-report-v2",
			"capability_code: " + state.capabilityCode,
			"capability_available: " + state.capabilityAvailable,
			"xr_compatible_adapter: " + state.adapterState,
			"provider_phase: " + state.phase,
			"provider_stage: " + state.stage,
			"provider_error_stage: " + state.errorStage,
			"provider_error_code: " + state.errorCode,
			"provider_error: " + state.lastError,
			"frames: " + state.frames,
			"skipped_frames: " + state.skippedFrames,
			"input_packets: " + state.inputPackets,
			"projection_format: " + state.projectionFormat,
			"reference_space: " + state.referenceSpaceType,
			"enter_attempts: " + state.enterAttempts,
			"successful_entries: " + state.successfulEntries,
			"exit_requests: " + state.exitRequests,
			"ended_sessions: " + state.endedSessions,
			"reentries: " + state.reentries,
			"transitions: " + (state.transitions.join(",") || "none"),
			"presentation_mode: " + state.presentationMode,
			"presentation_preference: " + state.presentationPreference,
			"layer_width: " + state.layerWidth,
			"layer_height: " + state.layerHeight,
			"atlas_width: " + state.atlasWidth,
			"atlas_height: " + state.atlasHeight,
			"bridge_frames: " + state.bridgeFrames,
			"bridge_errors: " + state.bridgeErrors,
			"bridge_samples: " + state.bridgeSamples,
			"bridge_median_ms: " + state.bridgeMedianMs,
			"bridge_p95_ms: " + state.bridgeP95Ms,
			"bridge_p99_ms: " + state.bridgeP99Ms,
			"bridge_blocking_timing: " + state.bridgeBlockingTiming,
			"privacy: no-game-data,no-paths,no-logs",
		].join("\n") + "\n";
	}

	class DiagnosticsPanel {
		constructor(element, options) {
			this.root = element || null;
			this.options = options || {};
			this.capability = { available: null, code: UNKNOWN };
			this.adapterState = UNKNOWN;
			this.pollTimer = null;
			this.boundCapability = event => { this.setCapability(event && event.detail); };
			this.boundAdapter = event => { this.setAdapter(event && event.detail); };
			if (root && typeof root.addEventListener === "function") {
				root.addEventListener("surrealwebxrcapability", this.boundCapability);
				root.addEventListener("surrealwebxradapter", this.boundAdapter);
			}
			if (this.root) {
				const copy = this.root.querySelector("[data-xr-diagnostics-copy]");
				const download = this.root.querySelector("[data-xr-diagnostics-download]");
				if (copy) copy.addEventListener("click", () => this.copy());
				if (download) download.addEventListener("click", () => this.download());
				this.root.addEventListener("toggle", () => this.updatePolling());
			}
			this.refresh();
			this.updatePolling();
		}

		updatePolling() {
			if (this.pollTimer !== null && typeof root.clearInterval === "function") root.clearInterval(this.pollTimer);
			this.pollTimer = null;
			this.refresh();
			if (this.root && this.root.open && typeof root.setInterval === "function") {
				this.pollTimer = root.setInterval(() => this.refresh(), 500);
			}
		}

		setCapability(detail) {
			if (detail) this.capability = { available: detail.available, code: detail.code || UNKNOWN };
			this.refresh();
		}

		setAdapter(detail) {
			if (detail) this.adapterState = detail.state || (detail.available ? "xr-compatible" : "flat-fallback");
			this.refresh();
		}

		snapshot() {
			let provider = {};
			try {
				provider = typeof root.surrealXRGetState === "function" ? root.surrealXRGetState() : {};
			} catch (_) { provider = {}; }
			return Object.assign({}, provider, {
				capabilityCode: this.capability.code,
				capabilityAvailable: this.capability.available,
				adapterState: this.adapterState,
			});
		}

		refresh() {
			const state = normalized(this.snapshot());
			if (!this.root) return state;
			const values = {
				capability: state.capabilityCode,
				adapter: state.adapterState,
				phase: state.phase + " / " + state.stage,
				error: state.errorCode + " / " + state.errorStage,
				frames: state.frames + " rendered, " + state.skippedFrames + " skipped",
				input: String(state.inputPackets),
				projection: state.projectionFormat,
				reference: state.referenceSpaceType,
				mode: state.presentationMode,
				preference: state.presentationPreference,
				dimensions: dimensions(state.layerWidth, state.layerHeight) + " layer, " +
					dimensions(state.atlasWidth, state.atlasHeight) + " atlas",
				bridge: state.bridgeSamples + " samples, median " + state.bridgeMedianMs +
					" ms, p95 " + state.bridgeP95Ms + " ms, p99 " + state.bridgeP99Ms +
					" ms, " + state.bridgeErrors + " errors, blocking " + state.bridgeBlockingTiming,
				lifecycle: state.enterAttempts + " enter, " + state.exitRequests + " exit, " + state.reentries + " re-entry",
			};
			Object.entries(values).forEach(([name, value]) => {
				const node = this.root.querySelector("[data-xr-diagnostics-" + name + "]");
				if (node) node.textContent = value;
			});
			return state;
		}

		report() { return formatReport(this.snapshot()); }

		async copy() {
			const contents = this.report();
			if (typeof this.options.copyText === "function") await this.options.copyText(contents);
			else if (root.navigator && root.navigator.clipboard) await root.navigator.clipboard.writeText(contents);
			return contents;
		}

		download() {
			const contents = this.report();
			if (typeof this.options.downloadText === "function") this.options.downloadText(contents);
			else if (root.document && root.URL && root.Blob) {
				const url = root.URL.createObjectURL(new root.Blob([contents], { type: "text/plain;charset=utf-8" }));
				const link = root.document.createElement("a");
				link.href = url;
				link.download = "surrealengine-webxr-headset-report.txt";
				link.click();
				root.URL.revokeObjectURL(url);
			}
			return contents;
		}
	}

	function create(element, options) { return new DiagnosticsPanel(element, options); }
	root.SurrealWebXRDiagnostics = Object.freeze({ create, formatReport, normalized });
})(typeof window !== "undefined" ? window : globalThis);
