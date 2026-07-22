import fs from "node:fs";
import vm from "node:vm";

const source = fs.readFileSync(new URL("./webxr_webgl_bridge.js", import.meta.url), "utf8");
const sandbox = { globalThis: {}, console };
vm.runInNewContext(source, sandbox);
const bridge = sandbox.globalThis.SurrealWebXRWebGLBridge;
const asymmetric = [2,0,0,0, 0,3,0,0, .2,-.3,-1,-1, 0,0,-.2,0];
const converted = bridge.convertProjectionDepth(asymmetric);
if (converted[2] !== 0 || converted[6] !== 0 || converted[10] !== -1 || converted[11] !== -1 ||
	Math.abs(converted[14] + .1) > 1e-7 || converted[15] !== 0)
	throw new Error("projection depth conversion does not match the shared native transform");
if (converted[8] !== .2 || converted[9] !== -.3)
	throw new Error("asymmetric projection terms were changed");

const percentileWindow = bridge.createTimingWindow(120);
for (let value = 1; value <= 100; value++) percentileWindow.add(value);
let timing = percentileWindow.summary();
if (timing.samples !== 100 || timing.medianMs !== 50 || timing.p95Ms !== 95 || timing.p99Ms !== 99)
	throw new Error("bridge timing percentiles do not use deterministic nearest ranks");

const boundedWindow = bridge.createTimingWindow(3);
[1, 2, 3, 4, Number.NaN, -1].forEach(value => boundedWindow.add(value));
timing = boundedWindow.summary();
if (timing.samples !== 3 || timing.medianMs !== 3 || timing.p95Ms !== 4 || timing.p99Ms !== 4)
	throw new Error("bridge timing window did not retain only bounded valid samples");

const gl = {
	COMPILE_STATUS: 1, LINK_STATUS: 2, TEXTURE_2D: 3, TEXTURE_MIN_FILTER: 4,
	TEXTURE_MAG_FILTER: 5, TEXTURE_WRAP_S: 6, TEXTURE_WRAP_T: 7, LINEAR: 8,
	CLAMP_TO_EDGE: 9, UNPACK_FLIP_Y_WEBGL: 10, FRAMEBUFFER: 11, TEXTURE0: 12,
	RGBA: 13, UNSIGNED_BYTE: 14, TRIANGLES: 15, NO_ERROR: 0,
	createShader: () => ({}), shaderSource() {}, compileShader() {}, getShaderParameter: () => true,
	getShaderInfoLog: () => "", createProgram: () => ({}), attachShader() {}, linkProgram() {},
	getProgramParameter: () => true, getProgramInfoLog: () => "", getUniformLocation: () => ({}),
	createTexture: () => ({}), bindTexture() {}, texParameteri() {}, pixelStorei() {},
	async makeXRCompatible() {}, bindFramebuffer() {}, useProgram() {}, activeTexture() {},
	texImage2D() {}, texSubImage2D() {}, viewport() {}, uniform4f() {}, drawArrays() {}, finish() {},
	getError: () => 0, deleteTexture() {}, deleteProgram() {}, deleteShader() {},
};
const sourceCanvas = { width: 1280, height: 720,
	getContext: kind => kind === "webgpu" ? { getCurrentTexture: () => ({ width: 1600, height: 700 }) } : null };
const times = [0, 2];
const host = {
	document: { createElement: () => ({ getContext: kind => kind === "webgl2" ? gl : null }) },
	performance: { now: () => times.shift() },
	XRWebGLLayer: function () { return {
		framebuffer: {}, framebufferWidth: 1832, framebufferHeight: 1920,
		getViewport: view => ({ x: view.eye === "left" ? 0 : 800, y: 0, width: 800, height: 700 }),
	}; },
};
const session = { updateRenderState() {} };
const liveBridge = await bridge.create({ root: host, session, canvas: sourceCanvas });
const frame = liveBridge.beginFrame({ views: [{ eye: "left" }, { eye: "right" }] });
liveBridge.present(frame);
const liveDiagnostics = liveBridge.diagnostics();
if (liveDiagnostics.samples !== 1 || liveDiagnostics.medianMs !== 2 || liveDiagnostics.p99Ms !== 2 ||
	liveDiagnostics.frames !== 1 || liveDiagnostics.errors !== 0 ||
	liveDiagnostics.layerWidth !== 1832 || liveDiagnostics.layerHeight !== 1920 ||
	liveDiagnostics.atlasWidth !== 1600 || liveDiagnostics.atlasHeight !== 700)
	throw new Error("live bridge diagnostics omitted timing or known surface dimensions");
console.log("WebXR WebGL bridge helper tests passed");
