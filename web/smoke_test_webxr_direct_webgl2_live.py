"""Exercise direct XRWebGLLayer rendering against the live UE1/WebGL2/WASM engine.

Only the browser XR compositor/session boundary is synthetic. The WebGL2
context, framebuffer attachments, native frame preparation, stereo renderer,
game state, and scheduler handoff are real. This is not physical-headset proof.
"""

import json
import os
import sys
import time
from pathlib import Path
from urllib.parse import quote

from playwright.sync_api import sync_playwright


base_url = os.environ.get("SURREAL_WEB_BASE_URL", "http://localhost:8091").rstrip("/")
engine_base = os.environ.get("SURREAL_WEB_ENGINE_BASE", "/build-emscripten/")
harness_path = os.environ.get("SURREAL_WEBGL2_HARNESS_PATH", "/web/index_webgl2.html")
provider_path = os.environ.get("SURREAL_WEBXR_PROVIDER_PATH", "/web/webxr_provider.js")
result_path = os.environ.get("SURREAL_WEBXR_DIRECT_RESULT")
map_name = os.environ.get("SURREAL_WEB_MAP")
url = f"{base_url}{harness_path}?engineBase={quote(engine_base, safe='/')}"
if map_name:
	url += "&map=" + quote(map_name, safe="-")


def wait_until(page, expression, timeout=120):
	deadline = time.time() + timeout
	while time.time() < deadline:
		if page.evaluate(expression):
			return True
		time.sleep(0.1)
	return False


with sync_playwright() as playwright:
	browser = playwright.chromium.launch(channel="chrome", headless=True)
	page = browser.new_page(viewport={"width": 960, "height": 640})
	page_errors = []
	page.on("pageerror", lambda error: page_errors.append(str(error)))
	page.goto(url, wait_until="load")
	if not wait_until(page, "window.surrealBooted || window.surrealCrashed"):
		raise RuntimeError("timed out waiting for the live WebGL2 game")
	if page.evaluate("window.surrealCrashed"):
		raise RuntimeError(page.evaluate("window.surrealCrashed"))
	if not wait_until(page,
		"window.surrealGetTickCount() >= 10 && window.surrealGetWebGL2Diagnostics().textures >= 164",
		timeout=30):
		raise RuntimeError("live scene resources did not reach the stable baseline")

	page.add_script_tag(url=base_url + provider_path)
	setup = page.evaluate("""() => {
		const gl = Module.canvas.getContext('webgl2');
		if (!gl) throw new Error('the live engine WebGL2 context is unavailable');
		const sessions = [];
		// Headless Chrome has no real XR device, so its native compatibility
		// promise rejects before our synthetic layer can be installed. This one
		// method is part of the mocked compositor boundary, not the renderer.
		Object.defineProperty(gl, 'makeXRCompatible', {
			configurable: true, value: async () => {},
		});
		const projection = new Float32Array([
			1, 0, 0, 0, 0, 1, 0, 0,
			0, 0, -1.0002, -1, 0, 0, -0.20002, 0,
		]);
		const makeView = (eye, x) => ({ eye, projectionMatrix: projection,
			transform: { position: { x, y: 1.6, z: 0 },
				orientation: { x: 0, y: 0, z: 0, w: 1 } } });

		class SyntheticSession {
			constructor() {
				this.listeners = new Map(); this.frames = new Map(); this.nextHandle = 1;
				this.visibilityState = 'visible'; this.inputSources = [];
			}
			addEventListener(name, callback) { this.listeners.set(name, callback); }
			removeEventListener(name, callback) {
				if (this.listeners.get(name) === callback) this.listeners.delete(name);
			}
			updateRenderState(state) { this.renderState = state; }
			async requestReferenceSpace() { return { addEventListener() {} }; }
			requestAnimationFrame(callback) {
				const handle = this.nextHandle++; this.frames.set(handle, callback); return handle;
			}
			cancelAnimationFrame(handle) { this.frames.delete(handle); }
			fire(time) {
				const entry = this.frames.entries().next().value;
				if (!entry) throw new Error('no synthetic XR frame is pending');
				this.frames.delete(entry[0]);
				const frame = {
					getViewerPose: () => ({ views: [makeView('left', -.032), makeView('right', .032)] }),
					getPose: () => null,
				};
				entry[1](time, frame);
			}
			async end() { this.listeners.get('end')?.(); }
		}

		class SyntheticXRWebGLLayer {
			constructor(session, suppliedGL, options) {
				if (suppliedGL !== gl) throw new Error('provider did not reuse the engine context');
				if (!options || options.depth !== true) throw new Error('direct layer requires depth');
				this.framebufferWidth = 1024; this.framebufferHeight = 512;
				const savedFramebuffer = gl.getParameter(gl.FRAMEBUFFER_BINDING);
				const savedRenderbuffer = gl.getParameter(gl.RENDERBUFFER_BINDING);
				const savedActiveTexture = gl.getParameter(gl.ACTIVE_TEXTURE);
				gl.activeTexture(gl.TEXTURE0);
				const savedTexture = gl.getParameter(gl.TEXTURE_BINDING_2D);
				this.framebuffer = gl.createFramebuffer();
				this.color = gl.createTexture();
				this.depth = gl.createRenderbuffer();
				gl.bindFramebuffer(gl.FRAMEBUFFER, this.framebuffer);
				gl.bindTexture(gl.TEXTURE_2D, this.color);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
				gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
				gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, this.framebufferWidth,
					this.framebufferHeight, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
				gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0,
					gl.TEXTURE_2D, this.color, 0);
				gl.bindRenderbuffer(gl.RENDERBUFFER, this.depth);
				gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT16,
					this.framebufferWidth, this.framebufferHeight);
				gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT,
					gl.RENDERBUFFER, this.depth);
				this.complete = gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE;
				gl.bindTexture(gl.TEXTURE_2D, savedTexture);
				gl.activeTexture(savedActiveTexture);
				gl.bindRenderbuffer(gl.RENDERBUFFER, savedRenderbuffer);
				gl.bindFramebuffer(gl.FRAMEBUFFER, savedFramebuffer);
			}
			getViewport(view) {
				return view.eye === 'left' ? { x: 0, y: 0, width: 512, height: 512 } :
					{ x: 512, y: 0, width: 512, height: 512 };
			}
			checksum() {
				const saved = gl.getParameter(gl.FRAMEBUFFER_BINDING);
				gl.bindFramebuffer(gl.FRAMEBUFFER, this.framebuffer);
				const pixels = new Uint8Array(this.framebufferWidth * this.framebufferHeight * 4);
				gl.readPixels(0, 0, this.framebufferWidth, this.framebufferHeight,
					gl.RGBA, gl.UNSIGNED_BYTE, pixels);
				gl.bindFramebuffer(gl.FRAMEBUFFER, saved);
				let checksum = 0, nonzero = 0, leftNonzero = 0, rightNonzero = 0;
				for (let index = 0; index < pixels.length; index++) {
					checksum = (checksum + pixels[index]) >>> 0;
					if (pixels[index]) {
						nonzero++;
						const pixel = Math.floor(index / 4);
						const x = pixel % this.framebufferWidth;
						if (x < this.framebufferWidth / 2) leftNonzero++;
						else rightNonzero++;
					}
				}
				return { checksum, nonzero, leftNonzero, rightNonzero,
					error: gl.getError() };
			}
		}

		Object.defineProperty(navigator, 'xr', { configurable: true, value: {
			isSessionSupported: async mode => mode === 'immersive-vr',
			requestSession: async () => { const session = new SyntheticSession(); sessions.push(session); return session; },
		} });
		window.XRWebGLLayer = SyntheticXRWebGLLayer;
		window.__directXR = { gl, sessions };
		return { contextHandle: Module.ccall('Surreal_GetWebGL2ContextHandle', 'number', [], []),
			engine: Module.ccall('Surreal_GetBrowserEngineIdentity', 'number', [], []),
			renderer: Module.ccall('Surreal_GetBrowserRendererIdentity', 'number', [], []),
			tick: surrealGetTickCount(), frames: surrealGetWebGL2Diagnostics().frames };
	}""")
	if not setup["contextHandle"]:
		raise RuntimeError("live WebGL2 context handle is zero")

	capabilities = page.evaluate("surrealXRGetCapabilities()")
	if capabilities["preferredMode"] != "direct-webgl2":
		raise RuntimeError(f"direct WebGL2 was not selected: {capabilities}")
	if not page.evaluate("surrealXREnter()"):
		raise RuntimeError(page.evaluate("surrealXRGetState()"))
	if not page.evaluate("__directXR.sessions[0].renderState.baseLayer.complete"):
		raise RuntimeError("synthetic compositor framebuffer is incomplete")

	entry = page.evaluate("""() => ({ tick: surrealGetTickCount(),
		frames: surrealGetWebGL2Diagnostics().frames })""")
	first_boundary = page.evaluate("""() => {
		const before = surrealGetTickCount();
		__directXR.sessions[0].fire(16);
		return { before, after: surrealGetTickCount() };
	}""")
	if not wait_until(page, "!window.surrealXRNativeCallsBlocked", timeout=30):
		raise RuntimeError("native XR preparation did not complete")
	prepared_tick = page.evaluate("surrealGetTickCount()")
	render_boundary = page.evaluate("""() => {
		const beforeTick = surrealGetTickCount();
		const beforeFrames = surrealGetWebGL2Diagnostics().frames;
		__directXR.sessions[0].fire(32);
		return { beforeTick, afterTick: surrealGetTickCount(), beforeFrames,
			afterFrames: surrealGetWebGL2Diagnostics().frames };
	}""")
	state_during = page.evaluate("surrealXRGetState()")
	gl_during = page.evaluate("surrealGetWebGL2Diagnostics()")
	pixels = page.evaluate("__directXR.sessions[0].renderState.baseLayer.checksum()")
	identity_during = page.evaluate("""() => ({
		engine: Module.ccall('Surreal_GetBrowserEngineIdentity', 'number', [], []),
		renderer: Module.ccall('Surreal_GetBrowserRendererIdentity', 'number', [], []),
		tick: surrealGetTickCount()
	})""")

	if not page.evaluate("surrealXRExit()"):
		raise RuntimeError("direct WebGL2 exit was rejected: " +
			json.dumps(page.evaluate("surrealXRGetState()"), indent=2))
	if not wait_until(page, "!surrealXRGetState().cleanupPending", timeout=30):
		raise RuntimeError("direct WebGL2 cleanup did not drain")
	if not wait_until(page, f"surrealGetTickCount() > {identity_during['tick']}", timeout=10):
		raise RuntimeError("flat rendering did not resume after direct XR")
	identity_after = page.evaluate("""() => ({
		engine: Module.ccall('Surreal_GetBrowserEngineIdentity', 'number', [], []),
		renderer: Module.ccall('Surreal_GetBrowserRendererIdentity', 'number', [], []),
		tick: surrealGetTickCount(), gl: surrealGetWebGL2Diagnostics()
	})""")

	result = {
		"schema": "surrealengine-webxr-direct-webgl2-live-v1",
		"scope": "live UE1/WebGL2/WASM stereo framebuffer render with synthetic browser XR session/layer/compatibility boundary; not physical headset qualification",
		"url": url,
		"capabilities": capabilities,
		"before": setup,
		"entry": entry,
		"firstBoundary": first_boundary,
		"preparedTick": prepared_tick,
		"renderBoundary": render_boundary,
		"during": identity_during,
		"after": identity_after,
		"providerDuring": state_during,
		"webGLDuring": gl_during,
		"pixels": pixels,
		"pageErrors": page_errors,
	}
	print(json.dumps(result, indent=2))
	if result_path:
		Path(result_path).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

	passed = (
		capabilities["preferredMode"] == "direct-webgl2"
		and state_during["presentationMode"] == "direct-webgl2"
		and state_during["frames"] == 1
		and first_boundary["before"] == first_boundary["after"] == entry["tick"]
		and prepared_tick == entry["tick"] + 1
		and render_boundary["beforeTick"] == render_boundary["afterTick"] == prepared_tick
		and render_boundary["afterFrames"] == render_boundary["beforeFrames"] + 1
		and setup["engine"] == identity_during["engine"] == identity_after["engine"]
		and setup["renderer"] == identity_during["renderer"] == identity_after["renderer"]
		and gl_during["frames"] == render_boundary["afterFrames"]
		and gl_during["drawCalls"] > 0
		and pixels["error"] == 0 and pixels["nonzero"] > 0 and pixels["checksum"] > 0
		and pixels["leftNonzero"] > 0 and pixels["rightNonzero"] > 0
		and identity_after["tick"] > identity_during["tick"]
		and not page_errors
	)
	page.evaluate("window.surrealRequestQuit()")
	browser.close()

if not passed:
	print("FAIL: live direct WebGL2 XR render did not preserve the runtime or draw stereo pixels", file=sys.stderr)
	raise SystemExit(1)
print("PASS: live direct WebGL2 XR render preserved one UE1/WASM runtime")
