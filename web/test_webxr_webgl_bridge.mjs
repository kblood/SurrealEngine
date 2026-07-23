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

const projection = (xScale, yScale, xOffset, yOffset) =>
	[xScale,0,0,0, 0,yScale,0,0, xOffset,yOffset,-1,-1, 0,0,-.2,0];
const orientation = (x, y, z, w) => ({ x, y, z, w });
const view = (eye, matrix, rotation) => ({ eye, projectionMatrix: matrix,
	transform: { orientation: rotation, position: { x: 0, y: 0, z: 0 } } });
const identityView = view("left", projection(2, 3, .2, -.3), orientation(0, 0, 0, 1));
const identityReprojection = bridge.createRotationReprojection(identityView, identityView);
const identityCenter = bridge.mapRotationReprojection(identityReprojection, 0, 0);
if (!identityCenter || Math.abs(identityCenter.ndc[0]) > 1e-7 ||
	Math.abs(identityCenter.ndc[1]) > 1e-7 || !identityCenter.inside)
	throw new Error("identity rotation reprojection changed the asymmetric eye center");

const asymmetricSource = view("left", projection(2, 3, -.3, .15), orientation(0, 0, 0, 1));
const asymmetricCurrent = view("left", projection(2, 3, .2, -.3), orientation(0, 0, 0, 1));
const asymmetricCenter = bridge.mapRotationReprojection(
	bridge.createRotationReprojection(asymmetricSource, asymmetricCurrent), 0, 0);
if (!asymmetricCenter || Math.abs(asymmetricCenter.ndc[0] - .5) > 1e-7 ||
	Math.abs(asymmetricCenter.ndc[1] + .45) > 1e-7)
	throw new Error("per-eye asymmetric projection offsets were not preserved");

const yaw = 10 * Math.PI / 180;
const yawCurrent = view("left", projection(1, 1, 0, 0),
	orientation(0, Math.sin(yaw / 2), 0, Math.cos(yaw / 2)));
const yawSource = view("left", projection(1, 1, 0, 0), orientation(0, 0, 0, 1));
const yawCenter = bridge.mapRotationReprojection(
	bridge.createRotationReprojection(yawSource, yawCurrent), 0, 0);
if (!yawCenter || Math.abs(yawCenter.ndc[0] + Math.tan(yaw)) > 1e-7 ||
	Math.abs(yawCenter.ndc[1]) > 1e-7)
	throw new Error("rotation-only yaw reprojection mapped the center ray incorrectly");
for (const degrees of [90, 120, 180]) {
	const radians = degrees * Math.PI / 180;
	const result = bridge.mapRotationReprojection(bridge.createRotationReprojection(yawSource,
		view("left", projection(1, 1, 0, 0),
			orientation(0, Math.sin(radians / 2), 0, Math.cos(radians / 2)))), 0, 0);
	if (result !== null)
		throw new Error(degrees + " degree yaw sampled a ray on or behind the source image plane");
}
const compound = orientation(.17, -.31, .23, .89);
const compoundNegated = orientation(-.17, .31, -.23, -.89);
const compoundMap = bridge.mapRotationReprojection(bridge.createRotationReprojection(yawSource,
	view("left", projection(1.3, .9, -.15, .11), compound)), .23, -.19);
const negatedMap = bridge.mapRotationReprojection(bridge.createRotationReprojection(yawSource,
	view("left", projection(1.3, .9, -.15, .11), compoundNegated)), .23, -.19);
if (!compoundMap || !negatedMap || compoundMap.ndc.some((value, index) =>
	Math.abs(value - negatedMap.ndc[index]) > 1e-6))
	throw new Error("equivalent q/-q orientations produced different compound reprojection");
for (const [axis, rotation] of [["pitch", orientation(.1, 0, 0, .9949874371)],
	["roll", orientation(0, 0, .1, .9949874371)]]) {
	const mapped = bridge.mapRotationReprojection(bridge.createRotationReprojection(yawSource,
		view("left", projection(1, 1, 0, 0), rotation)), .2, -.1);
	if (!mapped || !mapped.ndc.every(Number.isFinite))
		throw new Error(axis + " reprojection did not produce a finite front-facing sample");
}
const wideYaw = 60 * Math.PI / 180;
const outside = bridge.mapRotationReprojection(bridge.createRotationReprojection(yawSource,
	view("left", projection(1, 1, 0, 0),
		orientation(0, Math.sin(wideYaw / 2), 0, Math.cos(wideYaw / 2)))), 0, 0);
if (!outside || outside.inside)
	throw new Error("out-of-bounds reprojection was not rejected before atlas sampling");
const invalidProjection = projection(1, 1, 0, 0); invalidProjection[15] = 1;
if (bridge.createRotationReprojection(yawSource,
	view("left", invalidProjection, orientation(0, 0, 0, 1))) !== null)
	throw new Error("unsupported projection metadata did not select the fallback path");

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

const shaderSources = [];
const uniformCalls = [];
let clearCalls = 0;
const gl = {
	COMPILE_STATUS: 1, LINK_STATUS: 2, TEXTURE_2D: 3, TEXTURE_MIN_FILTER: 4,
	TEXTURE_MAG_FILTER: 5, TEXTURE_WRAP_S: 6, TEXTURE_WRAP_T: 7, LINEAR: 8,
	CLAMP_TO_EDGE: 9, UNPACK_FLIP_Y_WEBGL: 10, FRAMEBUFFER: 11, TEXTURE0: 12,
	RGBA: 13, UNSIGNED_BYTE: 14, TRIANGLES: 15, NO_ERROR: 0,
	COLOR_BUFFER_BIT: 16,
	createShader: () => ({}), shaderSource(shader, text) { shader.source = text; shaderSources.push(text); }, compileShader() {}, getShaderParameter: () => true,
	getShaderInfoLog: () => "", createProgram: () => ({}), attachShader() {}, linkProgram() {},
	getProgramParameter: () => true, getProgramInfoLog: () => "", getUniformLocation: (_program, name) => name,
	createTexture: () => ({}), bindTexture() {}, texParameteri() {}, pixelStorei() {},
	async makeXRCompatible() {}, bindFramebuffer() {}, useProgram() {}, activeTexture() {},
	texImage2D() {}, texSubImage2D() {}, viewport() {},
	uniform4f(...args) { uniformCalls.push(["uniform4f", ...args]); },
	uniform2f(...args) { uniformCalls.push(["uniform2f", ...args]); },
	uniform1i(...args) { uniformCalls.push(["uniform1i", ...args]); },
	uniform1f(...args) { uniformCalls.push(["uniform1f", ...args]); },
	uniformMatrix3fv(...args) { uniformCalls.push(["uniformMatrix3fv", ...args]); },
	drawArrays() {}, finish() {}, clearColor() {}, clear(mask) {
		if (mask !== this.COLOR_BUFFER_BIT) throw new Error("bridge used an unexpected clear mask");
		clearCalls++;
	},
	getError: () => 0, deleteTexture() {}, deleteProgram() {}, deleteShader() {},
};
const sourceCanvas = { width: 1280, height: 720 };
const times = [0, 2];
const order = [];
const currentTexture = { kind: "transfer-current" };
const transferContext = {
	configure(options) { this.options = options; },
	getCurrentTexture() { order.push("acquire-current-texture"); return currentTexture; },
	unconfigure() { order.push("unconfigure"); },
};
const xrCanvas = { getContext: kind => kind === "webgl2" ? gl : null };
const transferCanvas = { width: 0, height: 0,
	getContext: kind => kind === "webgpu" ? transferContext : null };
let canvasCount = 0;
const host = {
	document: { createElement: () => canvasCount++ === 0 ? xrCanvas : transferCanvas },
	performance: { now: () => times.shift() },
	navigator: { gpu: { getPreferredCanvasFormat: () => "bgra8unorm" } },
	XRWebGLLayer: function () { return {
		framebuffer: {}, framebufferWidth: 1832, framebufferHeight: 1920,
		getViewport: view => ({ x: view.eye === "left" ? 0 : 800, y: 0, width: 800, height: 700 }),
	}; },
};
const session = { updateRenderState() {} };
const sourceTexture = { kind: "persistent-atlas" };
const device = {
	createCommandEncoder() { return {
		copyTextureToTexture(source, destination, size) {
			order.push("copy-persistent-to-current");
			if (source.texture !== sourceTexture || destination.texture !== currentTexture ||
				size.width !== 1600 || size.height !== 700) throw new Error("incorrect bridge copy");
		},
		finish() { return {}; },
	}; },
	queue: { submit() { order.push("submit-webgpu-copy"); } },
};
const originalTexSubImage = gl.texSubImage2D;
gl.texSubImage2D = function (...args) {
	order.push("upload-transfer-canvas");
	if (args.at(-1) !== transferCanvas) throw new Error("bridge uploaded the SDL canvas");
	return originalTexSubImage.apply(this, args);
};
const originalDraw = gl.drawArrays;
gl.drawArrays = function (...args) { order.push("draw-webgl-eye"); return originalDraw.apply(this, args); };
const liveBridge = await bridge.create({ root: host, session, canvas: sourceCanvas, device,
	rotationReprojection: true });
if (liveBridge.textureFormat !== "bgra8unorm") throw new Error("bridge did not expose its transfer format");
const frame = liveBridge.describeFrame({ views: [{ eye: "left" }, { eye: "right" }] });
if (order.length !== 0) throw new Error("describing a frame acquired or submitted a current texture");
if (liveBridge.describeFrame({ views: [{ eye: "left" }, { eye: "right" }] }) !== frame)
	throw new Error("stable eye layout rebuilt per-eye rectangle arrays in the hot XR frame path");
const leftSource = view("left", projection(2, 3, -.2, -.1), orientation(0, 0, 0, 1));
const rightSource = view("right", projection(2, 3, .2, -.1), orientation(0, 0, 0, 1));
const leftCurrent = view("left", projection(2, 3, -.2, -.1), orientation(0, .05, 0, .9987492178));
const rightCurrent = view("right", projection(2, 3, .2, -.1), orientation(0, .05, 0, .9987492178));
liveBridge.present(frame, sourceTexture, device,
	{ sourceViews: [rightSource, leftSource],
		sourceAtlasViews: [{ x: 800, y: 0, width: 800, height: 700 },
			{ x: 0, y: 0, width: 800, height: 700 }],
		currentViews: [leftCurrent, rightCurrent] });
if (order.join(",") !== "acquire-current-texture,copy-persistent-to-current,submit-webgpu-copy,upload-transfer-canvas,draw-webgl-eye,draw-webgl-eye")
	throw new Error("bridge late-present ordering is incorrect: " + order.join(","));
if (sourceCanvas.width !== 1280 || sourceCanvas.height !== 720)
	throw new Error("bridge changed the SDL/flat canvas dimensions");
if (!shaderSources.some(text => text.includes("greaterThan(abs(sourceNdc)") &&
	text.includes("if(clipW<=1.e-6)") &&
	text.includes("sourceUv=mix(uvRect.xy,uvRect.zw") &&
	text.includes("texture(atlas,clamp(sourceUv,safeMin,safeMax))")))
	throw new Error("bridge shader omitted behind-plane rejection, exact mapping, or atlas isolation");
if (uniformCalls.filter(call => call[0] === "uniform1i" && call[1] === "reprojectEnabled" && call[2] === 1).length !== 2 ||
	uniformCalls.filter(call => call[0] === "uniformMatrix3fv" && call[1] === "currentToSource").length !== 2)
	throw new Error("bridge did not upload independent rotation reprojection uniforms for both eyes");
const sourceProjectionCalls = uniformCalls.filter(call =>
	call[0] === "uniform4f" && call[1] === "sourceProjection");
if (sourceProjectionCalls.length !== 2 || Math.abs(sourceProjectionCalls[0][4] + .2) > 1e-6 ||
	Math.abs(sourceProjectionCalls[1][4] - .2) > 1e-6)
	throw new Error("bridge did not match frozen asymmetric projections by eye");
const sourceRectCalls = uniformCalls.filter(call => call[0] === "uniform4f" && call[1] === "uvRect");
if (sourceRectCalls.length !== 2 || sourceRectCalls[0][2] !== 0 || sourceRectCalls[1][2] !== .5)
	throw new Error("bridge did not match frozen atlas subrectangles by eye");
const liveDiagnostics = liveBridge.diagnostics();
if (liveDiagnostics.samples !== 1 || liveDiagnostics.medianMs !== 2 || liveDiagnostics.p99Ms !== 2 ||
	liveDiagnostics.frames !== 1 || liveDiagnostics.errors !== 0 ||
	liveDiagnostics.layerWidth !== 1832 || liveDiagnostics.layerHeight !== 1920 ||
	liveDiagnostics.atlasWidth !== 1600 || liveDiagnostics.atlasHeight !== 700 ||
	liveDiagnostics.reprojectionMode !== "rotation-only" || liveDiagnostics.reprojectedFrames !== 1 ||
	liveDiagnostics.reprojectedEyes !== 2 || liveDiagnostics.reprojectionFallbackFrames !== 0 ||
	liveDiagnostics.reprojectionFallbackEyes !== 0)
	throw new Error("live bridge diagnostics omitted timing or known surface dimensions");
liveBridge.present(frame, sourceTexture, device);
const fallbackDiagnostics = liveBridge.diagnostics();
if (fallbackDiagnostics.reprojectedFrames !== 1 || fallbackDiagnostics.reprojectedEyes !== 2 ||
	fallbackDiagnostics.reprojectionFallbackFrames !== 1 || fallbackDiagnostics.reprojectionFallbackEyes !== 2)
	throw new Error("missing pose metadata did not cleanly use the identity-blit fallback");
liveBridge.present(frame, sourceTexture, device,
	{ sourceViews: [leftSource], sourceAtlasViews: [{ x: 0, y: 0, width: 800, height: 700 }],
		currentViews: [leftCurrent, rightCurrent] });
const partialDiagnostics = liveBridge.diagnostics();
if (partialDiagnostics.reprojectedFrames !== 1 || partialDiagnostics.reprojectedEyes !== 3 ||
	partialDiagnostics.reprojectionFallbackFrames !== 2 || partialDiagnostics.reprojectionFallbackEyes !== 3)
	throw new Error("partial per-eye fallback counters did not distinguish the successful eye");
liveBridge.clear();
if (clearCalls !== 1) throw new Error("bridge did not clear the XR framebuffer");
canvasCount = 0;
const defaultBridge = await bridge.create({ root: host, session, canvas: sourceCanvas, device });
const defaultDiagnostics = defaultBridge.diagnostics();
if (defaultDiagnostics.reprojectionMode !== "disabled" ||
	defaultDiagnostics.reprojectedFrames !== 0 || defaultDiagnostics.reprojectionFallbackFrames !== 0)
	throw new Error("rotation reprojection was not explicit opt-in with zero default counters");
console.log("WebXR WebGL bridge helper tests passed");
