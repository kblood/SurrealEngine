// Development-only WebXR scheduling probe used by profile_webxr.py.
//
// This deliberately measures browser scheduling timestamps, not GPU execution,
// compositor timing, headset thermals, or motion-to-photon latency. The probe is
// injected by Playwright and is not part of the deployable WebXR shell.
(function (global) {
	"use strict";

	const SCHEMA = "surrealengine-webxr-performance-probe";
	const VERSION = 1;
	const EVENT_LOOP_PERIOD_MS = 50;
	let active = false;
	let windowRafTimestamps = [];
	let xrRafTimestamps = [];
	let eventLoopDelays = [];
	let longTasks = [];
	let longTaskObserver = null;
	let xrPatched = false;
	let capturedWasmMemory = null;

	function finite(value) {
		return typeof value === "number" && Number.isFinite(value);
	}

	function captureMemory(candidate) {
		if (candidate instanceof WebAssembly.Memory) capturedWasmMemory = candidate;
	}

	function captureImportMemory(imports) {
		if (!imports || typeof imports !== "object") return;
		for (const namespace of Object.values(imports)) {
			if (!namespace || typeof namespace !== "object") continue;
			for (const candidate of Object.values(namespace)) captureMemory(candidate);
		}
	}

	// Emscripten does not export Module.HEAP8 in release builds. Observe the
	// WebAssembly instantiation boundary instead, preserving the native Promise
	// contract and retaining only the Memory object needed for byteLength.
	const originalInstantiate = WebAssembly.instantiate.bind(WebAssembly);
	WebAssembly.instantiate = async function (source, imports) {
		captureImportMemory(imports);
		const result = await originalInstantiate(source, imports);
		const instance = result && result.instance ? result.instance : result;
		if (instance && instance.exports) captureMemory(instance.exports.memory);
		return result;
	};
	if (typeof WebAssembly.instantiateStreaming === "function") {
		const originalInstantiateStreaming = WebAssembly.instantiateStreaming.bind(WebAssembly);
		WebAssembly.instantiateStreaming = async function (source, imports) {
			captureImportMemory(imports);
			const result = await originalInstantiateStreaming(source, imports);
			if (result && result.instance && result.instance.exports) {
				captureMemory(result.instance.exports.memory);
			}
			return result;
		};
	}

	function reset() {
		windowRafTimestamps = [];
		xrRafTimestamps = [];
		eventLoopDelays = [];
		longTasks = [];
	}

	function patchXRSessionRAF() {
		if (xrPatched || typeof global.XRSession !== "function") return xrPatched;
		const prototype = global.XRSession.prototype;
		const original = prototype && prototype.requestAnimationFrame;
		if (typeof original !== "function") return false;
		prototype.requestAnimationFrame = function (callback) {
			return original.call(this, function (time, frame) {
				if (active && finite(time)) xrRafTimestamps.push(time);
				return callback(time, frame);
			});
		};
		xrPatched = true;
		return true;
	}

	function onWindowRAF(time) {
		if (active && finite(time)) windowRafTimestamps.push(time);
		global.requestAnimationFrame(onWindowRAF);
	}
	global.requestAnimationFrame(onWindowRAF);

	function scheduleEventLoopProbe() {
		const expected = performance.now() + EVENT_LOOP_PERIOD_MS;
		setTimeout(function () {
			const delay = Math.max(0, performance.now() - expected);
			if (active && finite(delay)) eventLoopDelays.push(delay);
			scheduleEventLoopProbe();
		}, EVENT_LOOP_PERIOD_MS);
	}
	scheduleEventLoopProbe();

	let longTaskSupported = false;
	if (typeof global.PerformanceObserver === "function") {
		longTaskSupported = Array.isArray(PerformanceObserver.supportedEntryTypes) &&
			PerformanceObserver.supportedEntryTypes.includes("longtask");
		if (longTaskSupported) {
			longTaskObserver = new PerformanceObserver(function (list) {
				if (!active) return;
				for (const entry of list.getEntries()) {
					if (finite(entry.startTime) && finite(entry.duration)) {
						longTasks.push({ startTimeMs: entry.startTime, durationMs: entry.duration });
					}
				}
			});
			try {
				longTaskObserver.observe({ type: "longtask", buffered: false });
			} catch (_) {
				longTaskSupported = false;
				longTaskObserver = null;
			}
		}
	}

	function memorySnapshot() {
		const wasmBytes = capturedWasmMemory ? capturedWasmMemory.buffer.byteLength : null;
		const memory = performance.memory || null;
		return {
			wasmHeapBytes: finite(wasmBytes) ? wasmBytes : null,
			jsHeapSupported: !!memory,
			jsHeapUsedBytes: memory && finite(memory.usedJSHeapSize) ? memory.usedJSHeapSize : null,
			jsHeapTotalBytes: memory && finite(memory.totalJSHeapSize) ? memory.totalJSHeapSize : null,
			jsHeapLimitBytes: memory && finite(memory.jsHeapSizeLimit) ? memory.jsHeapSizeLimit : null,
		};
	}

	function snapshot() {
		return {
			schema: SCHEMA,
			version: VERSION,
			active: active,
			xrRafPatched: xrPatched,
			longTaskSupported: longTaskSupported,
			windowRafTimestampsMs: windowRafTimestamps.slice(),
			xrRafTimestampsMs: xrRafTimestamps.slice(),
			eventLoopDelaysMs: eventLoopDelays.slice(),
			longTasks: longTasks.slice(),
			memory: memorySnapshot(),
		};
	}

	global.surrealWebXRPerformanceProbe = {
		schema: SCHEMA,
		version: VERSION,
		start: function () {
			reset();
			patchXRSessionRAF();
			active = true;
			return snapshot();
		},
		stop: function () {
			active = false;
			return snapshot();
		},
		snapshot: snapshot,
		memory: memorySnapshot,
		patchXRSessionRAF: patchXRSessionRAF,
	};

	patchXRSessionRAF();
})(globalThis);
