#include "RenderDevice/WebGL2/WebGL2Context.h"

#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <memory>

namespace
{
	std::unique_ptr<WebGL2Context> Context;

	int ReadCenterPixel()
	{
		int width = 0;
		int height = 0;
		if (!Context || !Context->MakeCurrent() || !Context->GetDrawingBufferSize(width, height))
			return -1;
		unsigned char pixel[4] = {};
		glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
		return static_cast<int>(pixel[0]) | (static_cast<int>(pixel[1]) << 8) |
			(static_cast<int>(pixel[2]) << 16) | (static_cast<int>(pixel[3]) << 24);
	}

	void Clear(float red, float green, float blue)
	{
		Context->MakeCurrent();
		glClearColor(red, green, blue, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glFinish();
	}

	EM_JS(void, StartBrowserProbe, (int initialPixel), {
		const result = globalThis.webgl2ContextProbeResult = {
			complete: false,
			initialPixel,
			initialState: Module.ccall("Surreal_WebGL2ProbeState", "number"),
			initialGeneration: Module.ccall("Surreal_WebGL2ProbeGeneration", "number"),
			lossObserved: false,
			restored: false,
			error: null
		};
		try {
			const gl = GL.currentContext && GL.currentContext.GLctx;
			const extension = gl && gl.getExtension("WEBGL_lose_context");
			if (!extension) throw new Error("WEBGL_lose_context is unavailable");
			extension.loseContext();
			setTimeout(() => {
				result.lossObserved = Module.ccall("Surreal_WebGL2ProbeState", "number") === 2 &&
					Module.ccall("Surreal_WebGL2ProbeLossCount", "number") === 1;
				extension.restoreContext();
			}, 50);
			const deadline = performance.now() + 5000;
			const poll = () => {
				const generation = Module.ccall("Surreal_WebGL2ProbeGeneration", "number");
				if (generation >= 2) {
					result.restoredPixel = Module.ccall("Surreal_WebGL2ProbeClearAfterRestore", "number");
					result.finalState = Module.ccall("Surreal_WebGL2ProbeState", "number");
					result.finalGeneration = generation;
					result.lossCount = Module.ccall("Surreal_WebGL2ProbeLossCount", "number");
					result.restored = true;
					result.complete = true;
					return;
				}
				if (performance.now() >= deadline) {
					result.error = "context restoration timed out";
					result.complete = true;
					return;
				}
				setTimeout(poll, 20);
			};
			setTimeout(poll, 20);
		} catch (error) {
			result.error = error && error.message || String(error);
			result.complete = true;
		}
	});
}

extern "C"
{
	EMSCRIPTEN_KEEPALIVE int Surreal_WebGL2ProbeState()
	{
		return Context ? static_cast<int>(Context->State()) : -1;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_WebGL2ProbeGeneration()
	{
		return Context ? static_cast<int>(Context->Generation()) : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_WebGL2ProbeLossCount()
	{
		return Context ? static_cast<int>(Context->LossCount()) : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_WebGL2ProbeClearAfterRestore()
	{
		if (!Context || !Context->IsReady())
			return -1;
		Clear(0.1f, 0.8f, 0.2f);
		return ReadCenterPixel();
	}
}

int main()
{
	Context = std::make_unique<WebGL2Context>();
	Clear(0.2f, 0.4f, 0.6f);
	StartBrowserProbe(ReadCenterPixel());
	return 0;
}
