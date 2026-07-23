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
const resumeCount = () => calls.filter(name => name === "Surreal_ResumeBrowserAudio").length;
const resumesBeforeStart = resumeCount();
listeners.get("visibilitychange")();
listeners.get("surrealwebxrpresentation")({ detail: { state: "flat-fallback" } });
listeners.get("surrealwebxraudiogesture")();
assert.equal(resumeCount(), resumesBeforeStart,
	"visibility, presentation, and XR gesture notifications do not resume audio before engine start");
controller.engineStarted();
assert.ok(calls.length > 0);
assert.ok(calls.includes("Surreal_ResumeBrowserAudio"),
	"engine startup makes a best-effort resume after the OpenAL context exists");

environment.document.visibilityState = "hidden";
const beforeHidden = calls.length;
listeners.get("visibilitychange")();
assert.equal(calls.slice(beforeHidden).filter(name => name === "Surreal_SuspendBrowserAudio").length, 1,
	"a hidden document still suspends browser audio");
assert.equal(calls.slice(beforeHidden).includes("Surreal_ResumeBrowserAudio"), false);
environment.document.visibilityState = "visible";
const beforeVisible = calls.length;
listeners.get("visibilitychange")();
assert.equal(calls.slice(beforeVisible).filter(name => name === "Surreal_ResumeBrowserAudio").length, 1,
	"returning to a visible started game best-effort resumes browser audio");

for (const state of ["flat-fallback", "active"]) {
	const beforePresentation = calls.length;
	listeners.get("surrealwebxrpresentation")({ detail: { state } });
	assert.equal(calls.slice(beforePresentation).filter(name => name === "Surreal_ResumeBrowserAudio").length, 1,
		state + " presentation transition best-effort resumes browser audio");
}
const beforeXRGesture = calls.length;
listeners.get("surrealwebxraudiogesture")();
assert.equal(calls.slice(beforeXRGesture).filter(name => name === "Surreal_ResumeBrowserAudio").length, 1,
	"a forwarded trusted XR select retries the same bounded audio resume path");
const beforeBlock = calls.length;
environment.surrealXRNativeCallsBlocked = true;
environment.document.visibilityState = "hidden";
listeners.get("visibilitychange")();
environment.document.visibilityState = "visible";
listeners.get("visibilitychange")();
listeners.get("surrealwebxraudiogesture")();
controller.setOutput(); controller.refresh();
const diagnostics = controller.diagnostics();
assert.equal(calls.length, beforeBlock, "audio lifecycle and diagnostics must not enter Wasm during an XR render");
assert.equal(diagnostics.state, "running", "blocked diagnostics use the last safe native state");
assert.equal(diagnostics.currentTimeMs, 125);

environment.surrealXRNativeCallsBlocked = false;
listeners.get("surrealnativecallgatechange")();
assert.ok(calls.slice(beforeBlock).includes("Surreal_SetBrowserAudioOutput"),
	"the desired output state is flushed after native rendering settles");
assert.ok(calls.slice(beforeBlock).includes("Surreal_ResumeBrowserAudio"),
	"a trusted XR retry blocked by native rendering is deferred through the existing lifecycle gate");

const beforeShutdown = calls.length;
environment.surrealXRNativeCallsBlocked = true;
controller.resume(); controller.suspend(); listeners.get("pagehide")();
assert.equal(calls.length, beforeShutdown);
environment.surrealXRNativeCallsBlocked = false;
listeners.get("surrealnativecallgatechange")();
assert.ok(calls.slice(beforeShutdown).includes("Surreal_ShutdownBrowserAudio"),
	"the latest deferred lifecycle state is still flushed after native rendering settles");
console.log("Browser audio native-call gate tests passed");
