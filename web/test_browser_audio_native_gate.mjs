import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";

const listeners = new Map();
const environment = {
	addEventListener(name, callback) { listeners.set(name, callback); },
	document: { visibilityState: "visible", addEventListener(name, callback) { listeners.set(name, callback); } },
	setTimeout(callback) { callback(); },
	surrealXRNativeCallsBlocked: false,
};
const sandbox = { globalThis: environment, console };
vm.runInNewContext(fs.readFileSync(new URL("./browser_audio.js", import.meta.url), "utf8"), sandbox);

const calls = [];
const Module = { ccall(name) {
	calls.push(name);
	if (name === "Surreal_GetBrowserAudioState") return 2;
	if (name === "Surreal_GetBrowserAudioCurrentTimeMs") return 125;
	return 1;
} };
const controller = environment.SurrealBrowserAudio.create(null, environment).attachModule(Module);
controller.engineStarted();
assert.ok(calls.length > 0);
const beforeBlock = calls.length;
environment.surrealXRNativeCallsBlocked = true;
controller.resume(); controller.suspend(); controller.setOutput(); controller.refresh(); controller.shutdown();
const diagnostics = controller.diagnostics();
assert.equal(calls.length, beforeBlock, "audio lifecycle and diagnostics must not enter Wasm during an XR render");
assert.equal(diagnostics.state, "running", "blocked diagnostics use the last safe native state");
assert.equal(diagnostics.currentTimeMs, 125);

environment.surrealXRNativeCallsBlocked = false;
listeners.get("surrealnativecallgatechange")();
assert.ok(calls.slice(beforeBlock).includes("Surreal_SetBrowserAudioOutput"),
	"the desired output state is flushed after native rendering settles");
assert.ok(calls.slice(beforeBlock).includes("Surreal_ShutdownBrowserAudio"),
	"the latest deferred lifecycle state is flushed after native rendering settles");
console.log("Browser audio native-call gate tests passed");
