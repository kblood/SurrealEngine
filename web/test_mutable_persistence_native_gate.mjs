import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";

const listeners = new Map();
const intervals = [];
const documentListeners = new Map();
const environment = {
	surrealXRNativeCallsBlocked: false,
	addEventListener(name, callback) { listeners.set(name, callback); },
	removeEventListener(name, callback) {
		if (listeners.get(name) === callback) listeners.delete(name);
	},
	setInterval(callback) { intervals.push(callback); return intervals.length; },
	clearInterval() {},
	document: {
		hidden: false,
		addEventListener(name, callback) { documentListeners.set(name, callback); },
		removeEventListener(name, callback) {
			if (documentListeners.get(name) === callback) documentListeners.delete(name);
		},
	},
};
const sandbox = { globalThis: environment, console, Blob, TextEncoder, Uint8Array, Date };
vm.runInNewContext(fs.readFileSync(new URL("./mutable_persistence.js", import.meta.url), "utf8"), sandbox);

let fsCalls = 0;
let saves = 0;
const Module = { FS: {
	analyzePath() { fsCalls++; return { exists: false }; },
	stat() { fsCalls++; throw new Error("ENOENT"); },
	readFile() { fsCalls++; throw new Error("ENOENT"); },
} };
const storage = {
	backend: "test",
	async load() { return null; },
	async save(entries) {
		saves++;
		return { fileCount: entries.length, totalBytes: 0 };
	},
	async clear() {},
};

const controller = new environment.SurrealMutableData.MutableDataController(Module, {
	storage, checkpointIntervalMs: 1,
});
await controller.initialize();
const fsCallsAfterInitialize = fsCalls;

environment.surrealXRNativeCallsBlocked = true;
intervals[0]();
intervals[0]();
documentListeners.get("visibilitychange")();
environment.document.hidden = true;
documentListeners.get("visibilitychange")();
listeners.get("pagehide")();
await controller.whenIdle();
assert.equal(fsCalls, fsCallsAfterInitialize,
	"blocked automatic checkpoints must not enter the Emscripten filesystem");
assert.equal(saves, 0, "blocked checkpoint triggers must not persist a partial snapshot");
assert.equal(controller.status().deferredCheckpointReason, "pagehide",
	"multiple triggers coalesce while preserving the strongest lifecycle reason");

environment.surrealXRNativeCallsBlocked = false;
assert.equal(controller._nativeCallsBlocked(), false);
listeners.get("surrealnativecallgatechange")();
await controller.whenIdle();
assert.equal(saves, 1, "unblocking flushes exactly one coalesced checkpoint");
assert.ok(fsCalls > fsCallsAfterInitialize, "the filesystem is read only after the gate reopens");
assert.equal(controller.status().lastCheckpointReason, "pagehide");

controller.dispose();
assert.equal(listeners.has("surrealnativecallgatechange"), false,
	"dispose removes the native-call gate listener");
environment.surrealXRNativeCallsBlocked = false;
assert.equal(saves, 1, "teardown cannot flush a stale deferred checkpoint");
console.log("Mutable persistence native-call gate tests passed");
