/* XRWebGLLayer presentation bridge for engines which render through WebGPU. */
(function (root) {
	"use strict";
	const MAX_TIMING_SAMPLES = 120;

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

		const vertex = compile(gl, gl.VERTEX_SHADER, `#version 300 es
			const vec2 p[3] = vec2[3](vec2(-1.,-1.),vec2(3.,-1.),vec2(-1.,3.));
			uniform vec4 uvRect; out vec2 uv;
			void main(){ vec2 q=p[gl_VertexID]; gl_Position=vec4(q,0.,1.);
			uv=mix(uvRect.xy,uvRect.zw,q*.5+.5); }`);
		const fragment = compile(gl, gl.FRAGMENT_SHADER, `#version 300 es
			precision highp float; uniform sampler2D atlas; in vec2 uv; out vec4 color;
			void main(){ color=texture(atlas,uv); }`);
		const program = gl.createProgram();
		gl.attachShader(program, vertex); gl.attachShader(program, fragment); gl.linkProgram(program);
		if (!gl.getProgramParameter(program, gl.LINK_STATUS))
			throw new Error(gl.getProgramInfoLog(program) || "WebGL bridge program link failed");
		const uvRect = gl.getUniformLocation(program, "uvRect");
		const texture = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, texture);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);

		const timings = createTimingWindow();
		let allocatedWidth = 0, allocatedHeight = 0, errors = 0, frames = 0;
		const layerWidth = Number.isInteger(layer.framebufferWidth) && layer.framebufferWidth > 0 ?
			layer.framebufferWidth : 0;
		const layerHeight = Number.isInteger(layer.framebufferHeight) && layer.framebufferHeight > 0 ?
			layer.framebufferHeight : 0;

		function describeFrame(pose) {
			const views = Array.from(pose.views);
			const destinations = views.map(view => layer.getViewport(view));
			if (destinations.some(viewport => !viewport || viewport.width <= 0 || viewport.height <= 0))
				throw new Error("XRWebGLLayer returned an invalid eye viewport");
			const height = Math.max.apply(null, destinations.map(viewport => viewport.height));
			let x = 0;
			const atlasViews = destinations.map(viewport => {
				const result = { x, y: 0, width: viewport.width, height: viewport.height };
				x += viewport.width;
				return result;
			});
			if (transferCanvas.width !== x) transferCanvas.width = x;
			if (transferCanvas.height !== height) transferCanvas.height = height;
			return { width: x, height, destinations, atlasViews };
		}

		function present(frame, sourceTexture, sourceDevice) {
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
				frame.destinations.forEach((destination, index) => {
					const source = frame.atlasViews[index];
					gl.viewport(destination.x, destination.y, destination.width, destination.height);
					gl.uniform4f(uvRect, source.x / frame.width, source.y / frame.height,
						(source.x + source.width) / frame.width, (source.y + source.height) / frame.height);
					gl.drawArrays(gl.TRIANGLES, 0, 3);
				});
				if (blockingTiming) gl.finish();
				const error = gl.getError();
				if (error !== gl.NO_ERROR) throw new Error("WebGL bridge error " + error);
				frames++;
			} catch (error) { errors++; throw error; }
			finally {
				if (started !== null && host.performance && host.performance.now)
					timings.add(host.performance.now() - started);
			}
		}

		function diagnostics() {
			return Object.freeze(Object.assign({ frames, errors,
				blockingTiming,
				layerWidth, layerHeight, atlasWidth: allocatedWidth, atlasHeight: allocatedHeight },
				timings.summary()));
		}

		function destroy() {
			try { gl.deleteTexture(texture); gl.deleteProgram(program); gl.deleteShader(vertex); gl.deleteShader(fragment); } catch (_) {}
			try { transferContext.unconfigure(); } catch (_) {}
		}

		return Object.freeze({ mode: "webgl-bridge", layer, textureFormat,
			describeFrame, present, diagnostics, destroy });
	}

	root.SurrealWebXRWebGLBridge = Object.freeze({
		create, canCreateWebGL2, convertProjectionDepth, createTimingWindow,
	});
})(typeof window !== "undefined" ? window : globalThis);
