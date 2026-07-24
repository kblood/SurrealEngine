#pragma once

#include "WebGL2ContextLifecycle.h"

#include <emscripten/html5_webgl.h>
#include <string>

class WebGL2Context
{
public:
	explicit WebGL2Context(const char* canvasSelector = "#canvas");
	~WebGL2Context();

	WebGL2Context(const WebGL2Context&) = delete;
	WebGL2Context& operator=(const WebGL2Context&) = delete;

	bool MakeCurrent() const;
	bool GetDrawingBufferSize(int& width, int& height) const;
	bool IsReady() const { return lifecycle.State() == WebGL2ContextState::Ready; }
	WebGL2ContextState State() const { return lifecycle.State(); }
	uint32_t Generation() const { return lifecycle.Generation(); }
	uint32_t LossCount() const { return lifecycle.LossCount(); }
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE Handle() const { return handle; }

private:
	static bool ContextLostCallback(int eventType, const void* reserved, void* userData);
	static bool ContextRestoredCallback(int eventType, const void* reserved, void* userData);
	bool ValidateCurrentContext() const;

	std::string canvasSelector;
	EMSCRIPTEN_WEBGL_CONTEXT_HANDLE handle = 0;
	WebGL2ContextLifecycle lifecycle;
};
