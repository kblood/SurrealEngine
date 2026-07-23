/* XRWebGLLayer presentation bridge for engines which render through WebGPU. */
(function (root) {
	"use strict";
	const MAX_TIMING_SAMPLES = 120;
	const REPROJECTION_EPSILON = 1e-6;
	const VERTEX_SHADER_SOURCE = `#version 300 es
		const vec2 p[3] = vec2[3](vec2(-1.,-1.),vec2(3.,-1.),vec2(-1.,3.));
		out vec2 localUv;
		void main(){ vec2 q=p[gl_VertexID]; gl_Position=vec4(q,0.,1.);
		localUv=q*.5+.5; }`;
	const FRAGMENT_SHADER_SOURCE = `#version 300 es
		precision highp float; uniform sampler2D atlas; uniform vec4 uvRect;
		uniform vec2 atlasSize; uniform int reprojectEnabled;
		uniform vec4 currentProjection; uniform float currentProjectionW;
		uniform vec4 sourceProjection; uniform float sourceProjectionW;
		uniform mat3 currentToSource; in vec2 localUv; out vec4 color;
		void main(){
			if(reprojectEnabled==0){ color=texture(atlas,mix(uvRect.xy,uvRect.zw,localUv)); return; }
			vec2 currentNdc=localUv*2.-1.;
			vec3 currentRay=vec3((currentProjection.z-currentNdc.x*currentProjectionW)/currentProjection.x,
				(currentProjection.w-currentNdc.y*currentProjectionW)/currentProjection.y,-1.);
			vec3 sourceRay=currentToSource*currentRay;
			float clipW=sourceProjectionW*sourceRay.z;
			if(clipW<=1.e-6){ color=vec4(0.,0.,0.,1.); return; }
			vec2 sourceNdc=vec2(sourceProjection.x*sourceRay.x+sourceProjection.z*sourceRay.z,
				sourceProjection.y*sourceRay.y+sourceProjection.w*sourceRay.z)/clipW;
			if(any(greaterThan(abs(sourceNdc),vec2(1.)))){ color=vec4(0.,0.,0.,1.); return; }
			vec2 halfTexel=.5/atlasSize;
			vec2 safeMin=uvRect.xy+halfTexel, safeMax=uvRect.zw-halfTexel;
			vec2 sourceUv=mix(uvRect.xy,uvRect.zw,sourceNdc*.5+.5);
			color=texture(atlas,clamp(sourceUv,safeMin,safeMax));
		}`;
	const REPROJECTION_SHADER_SOURCES = Object.freeze({
		vertex: VERTEX_SHADER_SOURCE, fragment: FRAGMENT_SHADER_SOURCE,
	});

	function convertProjectionDepth(matrix) {
		const converted = Array.from(matrix);
		if (converted.length !== 16) throw new Error("WebXR projection matrix must contain 16 values");
		// Column-major clip-space conversion: z' = 0.5 * (z + w).
		for (let column = 0; column < 4; column++) {
			const z = column * 4 + 2;
			const w = column * 4 + 3;
			converted[z] = 0.5 * (converted[z] + converted[w]);
		}
		return converted;
	}

	function writeProjectionIntrinsics(matrix, output) {
		if (!matrix || matrix.length !== 16) return false;
		for (let index = 0; index < 16; index++) if (!Number.isFinite(matrix[index])) return false;
		if (Math.abs(matrix[0]) < REPROJECTION_EPSILON ||
			Math.abs(matrix[5]) < REPROJECTION_EPSILON || matrix[11] >= -REPROJECTION_EPSILON ||
			[1, 2, 3, 4, 6, 7, 12, 13, 15].some(index =>
				Math.abs(matrix[index]) > REPROJECTION_EPSILON)) return false;
		output[0] = matrix[0]; output[1] = matrix[5]; output[2] = matrix[8];
		output[3] = matrix[9]; output[4] = matrix[11];
		return true;
	}

	function writeNormalizedQuaternion(value, output) {
		if (!value || !Number.isFinite(value.x) || !Number.isFinite(value.y) ||
			!Number.isFinite(value.z) || !Number.isFinite(value.w)) return false;
		const magnitude = Math.hypot(value.x, value.y, value.z, value.w);
		if (magnitude < REPROJECTION_EPSILON) return false;
		output[0] = value.x / magnitude; output[1] = value.y / magnitude;
		output[2] = value.z / magnitude; output[3] = value.w / magnitude;
		return true;
	}

	function writeCurrentToSourceQuaternion(source, current, output) {
		const x = -source[0], y = -source[1], z = -source[2], w = source[3];
		output[0] = w * current[0] + x * current[3] + y * current[2] - z * current[1];
		output[1] = w * current[1] - x * current[2] + y * current[3] + z * current[0];
		output[2] = w * current[2] + x * current[1] - y * current[0] + z * current[3];
		output[3] = w * current[3] - x * current[0] - y * current[1] - z * current[2];
		const magnitude = Math.hypot(output[0], output[1], output[2], output[3]);
		if (magnitude < REPROJECTION_EPSILON) return false;
		for (let index = 0; index < 4; index++) output[index] /= magnitude;
		return true;
	}

	function writeQuaternionMatrix(q, output) {
		const [x, y, z, w] = q;
		const xx = x * x, yy = y * y, zz = z * z;
		const xy = x * y, xz = x * z, yz = y * z;
		const wx = w * x, wy = w * y, wz = w * z;
		output[0] = 1 - 2 * (yy + zz); output[1] = 2 * (xy + wz); output[2] = 2 * (xz - wy);
		output[3] = 2 * (xy - wz); output[4] = 1 - 2 * (xx + zz); output[5] = 2 * (yz + wx);
		output[6] = 2 * (xz + wy); output[7] = 2 * (yz - wx); output[8] = 1 - 2 * (xx + yy);
	}

	function createReprojectionScratch() {
		return {
			currentProjection: new Float32Array(5), sourceProjection: new Float32Array(5),
			currentToSource: new Float32Array(9), sourceOrientation: new Float32Array(4),
			currentOrientation: new Float32Array(4), delta: new Float32Array(4),
		};
	}

	function writeRotationReprojection(sourceView, currentView, output) {
		if (!sourceView || !currentView || sourceView.eye !== currentView.eye ||
			!writeProjectionIntrinsics(sourceView.projectionMatrix, output.sourceProjection) ||
			!writeProjectionIntrinsics(currentView.projectionMatrix, output.currentProjection) ||
			!writeNormalizedQuaternion(sourceView.transform && sourceView.transform.orientation,
				output.sourceOrientation) ||
			!writeNormalizedQuaternion(currentView.transform && currentView.transform.orientation,
				output.currentOrientation) ||
			!writeCurrentToSourceQuaternion(output.sourceOrientation, output.currentOrientation, output.delta))
			return false;
		writeQuaternionMatrix(output.delta, output.currentToSource);
		return true;
	}

	function createRotationReprojection(sourceView, currentView) {
		const scratch = createReprojectionScratch();
		if (!writeRotationReprojection(sourceView, currentView, scratch)) return null;
		return Object.freeze({
			currentProjection: Object.freeze(Array.from(scratch.currentProjection)),
			sourceProjection: Object.freeze(Array.from(scratch.sourceProjection)),
			currentToSource: Object.freeze(Array.from(scratch.currentToSource)),
		});
	}

	function mapRotationReprojection(reprojection, ndcX, ndcY) {
		if (!reprojection || !Number.isFinite(ndcX) || !Number.isFinite(ndcY)) return null;
		const current = reprojection.currentProjection;
		const source = reprojection.sourceProjection;
		const rotation = reprojection.currentToSource;
		if (!current || !source || !rotation) return null;
		const ray = [(current[2] - ndcX * current[4]) / current[0],
			(current[3] - ndcY * current[4]) / current[1], -1];
		const rotated = [
			rotation[0] * ray[0] + rotation[3] * ray[1] + rotation[6] * ray[2],
			rotation[1] * ray[0] + rotation[4] * ray[1] + rotation[7] * ray[2],
			rotation[2] * ray[0] + rotation[5] * ray[1] + rotation[8] * ray[2],
		];
		const clipW = source[4] * rotated[2];
		if (!Number.isFinite(clipW) || clipW <= REPROJECTION_EPSILON) return null;
		const sourceNdc = [(source[0] * rotated[0] + source[2] * rotated[2]) / clipW,
			(source[1] * rotated[1] + source[3] * rotated[2]) / clipW];
		if (!sourceNdc.every(Number.isFinite)) return null;
		return Object.freeze({ ndc: Object.freeze(sourceNdc),
			uv: Object.freeze(sourceNdc.map(value => value * 0.5 + 0.5)),
			inside: sourceNdc.every(value => Math.abs(value) <= 1) });
	}

	function findEyeViewIndex(views, eye) {
		if (!views) return -1;
		let found = -1;
		for (let index = 0; index < views.length; index++) {
			if (!views[index] || views[index].eye !== eye) continue;
			if (found !== -1) return -1;
			found = index;
		}
		return found;
	}

	function validAtlasView(view, width, height) {
		return view && [view.x, view.y, view.width, view.height].every(Number.isInteger) &&
			view.x >= 0 && view.y >= 0 && view.width > 0 && view.height > 0 &&
			view.x + view.width <= width && view.y + view.height <= height;
	}

	function compile(gl, type, source) {
		const shader = gl.createShader(type);
		gl.shaderSource(shader, source);
		gl.compileShader(shader);
		if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
			throw new Error(gl.getShaderInfoLog(shader) || "WebGL bridge shader compilation failed");
		return shader;
	}

	function percentile(values, fraction) {
		if (!values.length) return null;
		const sorted = values.slice().sort((a, b) => a - b);
		return sorted[Math.max(0, Math.min(sorted.length - 1, Math.ceil(sorted.length * fraction) - 1))];
	}

	function createTimingWindow(capacity) {
		const limit = Number.isInteger(capacity) && capacity > 0 ?
			Math.min(capacity, MAX_TIMING_SAMPLES) : MAX_TIMING_SAMPLES;
		const values = [];
		return Object.freeze({
			add(value) {
				if (!Number.isFinite(value) || value < 0) return;
				values.push(value);
				if (values.length > limit) values.shift();
			},
			summary() {
				return Object.freeze({ samples: values.length, medianMs: percentile(values, 0.5),
					p95Ms: percentile(values, 0.95), p99Ms: percentile(values, 0.99) });
			},
		});
	}

	function canCreateWebGL2(host) {
		try {
			const canvas = host.document && host.document.createElement("canvas");
			return !!(canvas && canvas.getContext("webgl2"));
		} catch (_) { return false; }
	}

	async function create(options) {
		const host = options.root || root;
		const blockingTiming = options.blockingTiming === true ||
			(options.blockingTiming === undefined && host.surrealXRBridgeBlockingTiming === true);
		const rotationReprojection = options.rotationReprojection === true ||
			(options.rotationReprojection === undefined &&
				host.surrealXRBridgeRotationReprojection === true);
		const session = options.session;
		const sourceCanvas = options.canvas;
		const device = options.device;
		if (!session || !sourceCanvas || !device || typeof host.XRWebGLLayer !== "function")
			throw new Error("XRWebGLLayer bridge prerequisites are unavailable");
		const xrCanvas = host.document.createElement("canvas");
		const gl = xrCanvas.getContext("webgl2", {
			xrCompatible: true, alpha: false, antialias: false, depth: false,
			stencil: false, preserveDrawingBuffer: false
		});
		if (!gl) throw new Error("WebGL 2 context creation failed");
		if (typeof gl.makeXRCompatible === "function") await gl.makeXRCompatible();
		const layer = new host.XRWebGLLayer(session, gl, {
			alpha: false, antialias: false, depth: false, stencil: false,
			framebufferScaleFactor: 1
		});
		session.updateRenderState({ baseLayer: layer });
		// Keep the SDL/flat canvas untouched. This transfer canvas exists only for
		// the same-task WebGPU-to-WebGL upload performed at the end of each XR rAF.
		const transferCanvas = host.document.createElement("canvas");
		const transferContext = transferCanvas.getContext("webgpu");
		if (!transferContext || typeof transferContext.configure !== "function" ||
			typeof transferContext.getCurrentTexture !== "function")
			throw new Error("WebGPU transfer canvas creation failed");
		const textureFormat = host.navigator && host.navigator.gpu &&
			typeof host.navigator.gpu.getPreferredCanvasFormat === "function" ?
			host.navigator.gpu.getPreferredCanvasFormat() : "bgra8unorm";
		const usage = host.GPUTextureUsage || {};
		transferContext.configure({ device, format: textureFormat, alphaMode: "opaque",
			usage: (usage.RENDER_ATTACHMENT || 0x10) | (usage.COPY_DST || 0x02) });

		const vertex = compile(gl, gl.VERTEX_SHADER, VERTEX_SHADER_SOURCE);
		const fragment = compile(gl, gl.FRAGMENT_SHADER, FRAGMENT_SHADER_SOURCE);
		const program = gl.createProgram();
		gl.attachShader(program, vertex); gl.attachShader(program, fragment); gl.linkProgram(program);
		if (!gl.getProgramParameter(program, gl.LINK_STATUS))
			throw new Error(gl.getProgramInfoLog(program) || "WebGL bridge program link failed");
		const uvRect = gl.getUniformLocation(program, "uvRect");
		const atlasSize = gl.getUniformLocation(program, "atlasSize");
		const reprojectEnabled = gl.getUniformLocation(program, "reprojectEnabled");
		const currentProjection = gl.getUniformLocation(program, "currentProjection");
		const currentProjectionW = gl.getUniformLocation(program, "currentProjectionW");
		const sourceProjection = gl.getUniformLocation(program, "sourceProjection");
		const sourceProjectionW = gl.getUniformLocation(program, "sourceProjectionW");
		const currentToSource = gl.getUniformLocation(program, "currentToSource");
		const texture = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, texture);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);

		const timings = createTimingWindow();
		let allocatedWidth = 0, allocatedHeight = 0, errors = 0, frames = 0;
		let reprojectedFrames = 0, reprojectionFallbackFrames = 0;
		let reprojectedEyes = 0, reprojectionFallbackEyes = 0;
		const reprojectionScratch = rotationReprojection ?
			[createReprojectionScratch(), createReprojectionScratch()] : null;
		const layerWidth = Number.isInteger(layer.framebufferWidth) && layer.framebufferWidth > 0 ?
			layer.framebufferWidth : 0;
		const layerHeight = Number.isInteger(layer.framebufferHeight) && layer.framebufferHeight > 0 ?
			layer.framebufferHeight : 0;

		let cachedDescription = null;
		function sameViewport(left, right) {
			return left && right && left.x === right.x && left.y === right.y &&
				left.width === right.width && left.height === right.height;
		}

		function describeFrame(pose) {
			const views = pose && pose.views;
			if (!views || views.length !== 2)
				throw new Error("XRWebGLLayer bridge requires exactly two primary views");
			const first = layer.getViewport(views[0]);
			const second = layer.getViewport(views[1]);
			if (!first || !second || first.width <= 0 || first.height <= 0 ||
				second.width <= 0 || second.height <= 0)
				throw new Error("XRWebGLLayer returned an invalid eye viewport");
			if (cachedDescription && cachedDescription.eyes[0] === views[0].eye &&
				cachedDescription.eyes[1] === views[1].eye &&
				sameViewport(cachedDescription.destinations[0], first) &&
				sameViewport(cachedDescription.destinations[1], second)) return cachedDescription;
			const destinations = [
				{ x: first.x, y: first.y, width: first.width, height: first.height },
				{ x: second.x, y: second.y, width: second.width, height: second.height },
			];
			const width = first.width + second.width;
			const height = Math.max(first.height, second.height);
			const atlasViews = [
				{ x: 0, y: 0, width: first.width, height: first.height },
				{ x: first.width, y: 0, width: second.width, height: second.height },
			];
			if (transferCanvas.width !== width) transferCanvas.width = width;
			if (transferCanvas.height !== height) transferCanvas.height = height;
			cachedDescription = { mode: "webgl-bridge", format: textureFormat,
				width, height, destinations, atlasViews, eyes: [views[0].eye, views[1].eye] };
			return cachedDescription;
		}

		function present(frame, sourceTexture, sourceDevice, metadata) {
			const started = host.performance && host.performance.now ? host.performance.now() : null;
			try {
				if (!sourceTexture || sourceDevice !== device)
					throw new Error("WebGL bridge received an invalid persistent atlas");
				const currentTexture = transferContext.getCurrentTexture();
				const encoder = device.createCommandEncoder({ label: "Surreal WebXR WebGL bridge present" });
				encoder.copyTextureToTexture({ texture: sourceTexture }, { texture: currentTexture },
					{ width: frame.width, height: frame.height, depthOrArrayLayers: 1 });
				device.queue.submit([encoder.finish()]);
				gl.bindFramebuffer(gl.FRAMEBUFFER, layer.framebuffer);
				gl.useProgram(program); gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, texture);
				if (allocatedWidth !== frame.width || allocatedHeight !== frame.height) {
					gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, frame.width, frame.height, 0,
						gl.RGBA, gl.UNSIGNED_BYTE, null);
					allocatedWidth = frame.width; allocatedHeight = frame.height;
				}
				// The only WebGPU-to-WebGL handoff. It must remain in the same XR rAF task.
				gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, gl.RGBA, gl.UNSIGNED_BYTE, transferCanvas);
				let frameReprojectedEyes = 0;
				for (let index = 0; index < frame.destinations.length; index++) {
					const destination = frame.destinations[index];
					const fallbackSource = frame.atlasViews[index];
					const eye = frame.eyes && frame.eyes[index];
					const sourceIndex = rotationReprojection ?
						findEyeViewIndex(metadata && metadata.sourceViews, eye) : -1;
					const currentIndex = rotationReprojection ?
						findEyeViewIndex(metadata && metadata.currentViews, eye) : -1;
					const frozenAtlasView = sourceIndex >= 0 && metadata && metadata.sourceAtlasViews &&
						metadata.sourceAtlasViews[sourceIndex];
					const hasFrozenAtlasView = validAtlasView(frozenAtlasView, frame.width, frame.height);
					const scratch = rotationReprojection ?
						(reprojectionScratch[index] || reprojectionScratch[0]) : null;
					const reproject = hasFrozenAtlasView && currentIndex >= 0 &&
						writeRotationReprojection(metadata.sourceViews[sourceIndex],
							metadata.currentViews[currentIndex], scratch);
					const source = reproject ? frozenAtlasView : fallbackSource;
					gl.viewport(destination.x, destination.y, destination.width, destination.height);
					gl.uniform4f(uvRect, source.x / frame.width, source.y / frame.height,
						(source.x + source.width) / frame.width, (source.y + source.height) / frame.height);
					gl.uniform2f(atlasSize, frame.width, frame.height);
					gl.uniform1i(reprojectEnabled, reproject ? 1 : 0);
					if (reproject) {
						const current = scratch.currentProjection;
						const frozen = scratch.sourceProjection;
						gl.uniform4f(currentProjection, current[0], current[1], current[2], current[3]);
						gl.uniform1f(currentProjectionW, current[4]);
						gl.uniform4f(sourceProjection, frozen[0], frozen[1], frozen[2], frozen[3]);
						gl.uniform1f(sourceProjectionW, frozen[4]);
						gl.uniformMatrix3fv(currentToSource, false, scratch.currentToSource);
						frameReprojectedEyes++;
					}
					gl.drawArrays(gl.TRIANGLES, 0, 3);
				}
				if (blockingTiming) gl.finish();
				const error = gl.getError();
				if (error !== gl.NO_ERROR) throw new Error("WebGL bridge error " + error);
				if (rotationReprojection) {
					reprojectedEyes += frameReprojectedEyes;
					reprojectionFallbackEyes += frame.destinations.length - frameReprojectedEyes;
					if (frame.destinations.length > 0 &&
						frameReprojectedEyes === frame.destinations.length) reprojectedFrames++;
					else reprojectionFallbackFrames++;
				}
				frames++;
			} catch (error) { errors++; throw error; }
			finally {
				if (started !== null && host.performance && host.performance.now)
					timings.add(host.performance.now() - started);
			}
		}

		function clear() {
			gl.bindFramebuffer(gl.FRAMEBUFFER, layer.framebuffer);
			gl.clearColor(0, 0, 0, 1);
			gl.clear(gl.COLOR_BUFFER_BIT);
			const error = gl.getError();
			if (error !== gl.NO_ERROR) throw new Error("WebGL bridge clear error " + error);
		}

		function diagnostics() {
			return Object.freeze(Object.assign({ frames, errors,
				blockingTiming,
				reprojectionMode: rotationReprojection ? "rotation-only" : "disabled",
				reprojectedFrames, reprojectionFallbackFrames,
				reprojectedEyes, reprojectionFallbackEyes,
				layerWidth, layerHeight, atlasWidth: allocatedWidth, atlasHeight: allocatedHeight },
				timings.summary()));
		}

		function destroy() {
			try { gl.deleteTexture(texture); gl.deleteProgram(program); gl.deleteShader(vertex); gl.deleteShader(fragment); } catch (_) {}
			try { transferContext.unconfigure(); } catch (_) {}
		}

		return Object.freeze({ mode: "webgl-bridge", layer, textureFormat,
			describeFrame, present, clear, diagnostics, destroy });
	}

	root.SurrealWebXRWebGLBridge = Object.freeze({
		create, canCreateWebGL2, convertProjectionDepth, createTimingWindow,
		createRotationReprojection, mapRotationReprojection,
		reprojectionShaderSources: () => REPROJECTION_SHADER_SOURCES,
	});
})(typeof window !== "undefined" ? window : globalThis);
