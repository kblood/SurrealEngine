/* XRWebGLLayer presentation bridge for engines which render through WebGPU. */
(function (root) {
	"use strict";

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
		return sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * fraction))];
	}

	function canCreateWebGL2(host) {
		try {
			const canvas = host.document && host.document.createElement("canvas");
			return !!(canvas && canvas.getContext("webgl2"));
		} catch (_) { return false; }
	}

	async function create(options) {
		const host = options.root || root;
		const session = options.session;
		const sourceCanvas = options.canvas;
		if (!session || !sourceCanvas || typeof host.XRWebGLLayer !== "function")
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

		const originalSize = { width: sourceCanvas.width, height: sourceCanvas.height };
		const samples = [];
		let allocatedWidth = 0, allocatedHeight = 0, errors = 0, frames = 0;

		function beginFrame(pose) {
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
			if (sourceCanvas.width !== x) sourceCanvas.width = x;
			if (sourceCanvas.height !== height) sourceCanvas.height = height;
			const gpu = sourceCanvas.getContext("webgpu");
			if (!gpu || typeof gpu.getCurrentTexture !== "function")
				throw new Error("SurrealEngine WebGPU canvas is unavailable");
			return { texture: gpu.getCurrentTexture(), width: x, height, views, destinations, atlasViews };
		}

		function present(frame) {
			const started = host.performance && host.performance.now ? host.performance.now() : 0;
			try {
				gl.bindFramebuffer(gl.FRAMEBUFFER, layer.framebuffer);
				gl.useProgram(program); gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, texture);
				if (allocatedWidth !== frame.width || allocatedHeight !== frame.height) {
					gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, frame.width, frame.height, 0,
						gl.RGBA, gl.UNSIGNED_BYTE, null);
					allocatedWidth = frame.width; allocatedHeight = frame.height;
				}
				// The only WebGPU-to-WebGL handoff. It must remain in the same XR rAF task.
				gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, gl.RGBA, gl.UNSIGNED_BYTE, sourceCanvas);
				frame.destinations.forEach((destination, index) => {
					const source = frame.atlasViews[index];
					gl.viewport(destination.x, destination.y, destination.width, destination.height);
					gl.uniform4f(uvRect, source.x / frame.width, source.y / frame.height,
						(source.x + source.width) / frame.width, (source.y + source.height) / frame.height);
					gl.drawArrays(gl.TRIANGLES, 0, 3);
				});
				if (host.surrealXRBridgeBlockingTiming === true) gl.finish();
				const error = gl.getError();
				if (error !== gl.NO_ERROR) throw new Error("WebGL bridge error " + error);
				frames++;
			} catch (error) { errors++; throw error; }
			finally {
				if (started && host.performance && host.performance.now) {
					samples.push(host.performance.now() - started);
					if (samples.length > 120) samples.shift();
				}
			}
		}

		function diagnostics() {
			return Object.freeze({ frames, errors, samples: samples.length,
				medianMs: percentile(samples, 0.5), p95Ms: percentile(samples, 0.95),
				blockingTiming: host.surrealXRBridgeBlockingTiming === true });
		}

		function destroy() {
			try { gl.deleteTexture(texture); gl.deleteProgram(program); gl.deleteShader(vertex); gl.deleteShader(fragment); } catch (_) {}
			sourceCanvas.width = originalSize.width; sourceCanvas.height = originalSize.height;
		}

		return Object.freeze({ mode: "webgl-bridge", layer, beginFrame, present, diagnostics, destroy });
	}

	root.SurrealWebXRWebGLBridge = Object.freeze({ create, canCreateWebGL2, convertProjectionDepth });
})(typeof window !== "undefined" ? window : globalThis);
