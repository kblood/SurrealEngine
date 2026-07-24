#include "WebGL2Context.h"

#include <GLES3/gl3.h>
#include <stdexcept>
#include <string_view>

WebGL2Context::WebGL2Context(const char* selector) : canvasSelector(selector ? selector : "#canvas")
{
	EmscriptenWebGLContextAttributes attributes = {};
	emscripten_webgl_init_context_attributes(&attributes);
	attributes.alpha = false;
	attributes.depth = true;
	attributes.stencil = false;
	attributes.antialias = false;
	attributes.premultipliedAlpha = false;
	attributes.preserveDrawingBuffer = false;
	attributes.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
	attributes.failIfMajorPerformanceCaveat = false;
	attributes.majorVersion = 2;
	attributes.minorVersion = 0;
	attributes.enableExtensionsByDefault = true;
	attributes.explicitSwapControl = false;

	handle = emscripten_webgl_create_context(canvasSelector.c_str(), &attributes);
	if (!handle)
	{
		lifecycle.CompleteInitialCreation(false);
		throw std::runtime_error("Could not create a WebGL 2 context on the browser game canvas");
	}

	if (!MakeCurrent() || !ValidateCurrentContext())
	{
		lifecycle.CompleteInitialCreation(false);
		emscripten_webgl_destroy_context(handle);
		handle = 0;
		throw std::runtime_error("The browser did not provide the required WebGL 2 / OpenGL ES 3 context");
	}
	lifecycle.CompleteInitialCreation(true);

	const auto lostResult = emscripten_set_webglcontextlost_callback(
		canvasSelector.c_str(), this, false, ContextLostCallback);
	const auto restoredResult = emscripten_set_webglcontextrestored_callback(
		canvasSelector.c_str(), this, false, ContextRestoredCallback);
	if (lostResult != EMSCRIPTEN_RESULT_SUCCESS || restoredResult != EMSCRIPTEN_RESULT_SUCCESS)
	{
		emscripten_webgl_destroy_context(handle);
		handle = 0;
		throw std::runtime_error("Could not register WebGL context loss and restoration callbacks");
	}
}

WebGL2Context::~WebGL2Context()
{
	if (!handle)
		return;
	emscripten_set_webglcontextlost_callback(canvasSelector.c_str(), nullptr, false, nullptr);
	emscripten_set_webglcontextrestored_callback(canvasSelector.c_str(), nullptr, false, nullptr);
	emscripten_webgl_destroy_context(handle);
}

bool WebGL2Context::MakeCurrent() const
{
	return handle && emscripten_webgl_make_context_current(handle) == EMSCRIPTEN_RESULT_SUCCESS;
}

bool WebGL2Context::GetDrawingBufferSize(int& width, int& height) const
{
	width = 0;
	height = 0;
	return handle && emscripten_webgl_get_drawing_buffer_size(handle, &width, &height) == EMSCRIPTEN_RESULT_SUCCESS;
}

bool WebGL2Context::ValidateCurrentContext() const
{
	const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
	if (!version)
		return false;
	const std::string_view text(version);
	return text.find("WebGL 2") != std::string_view::npos || text.find("OpenGL ES 3") != std::string_view::npos;
}

bool WebGL2Context::ContextLostCallback(int, const void*, void* userData)
{
	auto* context = static_cast<WebGL2Context*>(userData);
	if (context)
		context->lifecycle.MarkLost();
	return true;
}

bool WebGL2Context::ContextRestoredCallback(int, const void*, void* userData)
{
	auto* context = static_cast<WebGL2Context*>(userData);
	if (!context || !context->lifecycle.BeginRestore())
		return false;
	context->lifecycle.CompleteRestore(context->MakeCurrent() && context->ValidateCurrentContext());
	return false;
}
