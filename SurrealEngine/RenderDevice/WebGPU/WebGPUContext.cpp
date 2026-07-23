
#include "Precomp.h"
#include "WebGPUContext.h"
#include "Utils/Exception.h"

#include <cstring>
#include <emscripten/em_js.h>

namespace
{
	constexpr const char* BrowserCanvasSelector = "[data-surreal-webgpu-canvas]";

	EM_JS(int, surreal_mark_webgpu_canvas, (), {
		const canvas = globalThis.Module && globalThis.Module.canvas;
		if (!(canvas instanceof HTMLCanvasElement)) return 0;
		const previous = document.querySelector("[data-surreal-webgpu-canvas]");
		if (previous && previous !== canvas)
			previous.removeAttribute("data-surreal-webgpu-canvas");
		canvas.setAttribute("data-surreal-webgpu-canvas", "");
		return document.querySelector("[data-surreal-webgpu-canvas]") === canvas ? 1 : 0;
	});
}

WebGPUContext::WebGPUContext()
{
	Device = emscripten_webgpu_get_device();
	if (!Device)
		Exception::Throw("WebGPU device not available (Module.preinitializedWebGPUDevice was not set before callMain)");

	Queue = wgpuDeviceGetQueue(Device);

	Instance = wgpuCreateInstance(nullptr);
	if (!Instance)
		Exception::Throw("wgpuCreateInstance failed");
	if (!surreal_mark_webgpu_canvas())
		Exception::Throw("Module.canvas is not the active browser WebGPU canvas");

	WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc = {};
	canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
	canvasDesc.selector = { BrowserCanvasSelector, std::strlen(BrowserCanvasSelector) };

	WGPUSurfaceDescriptor surfDesc = {};
	surfDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvasDesc);
	Surface = wgpuInstanceCreateSurface(Instance, &surfDesc);
	if (!Surface)
		Exception::Throw("wgpuInstanceCreateSurface failed");
}

WebGPUContext::~WebGPUContext()
{
	if (Surface)
		wgpuSurfaceRelease(Surface);
	if (Instance)
		wgpuInstanceRelease(Instance);
	if (Queue)
		wgpuQueueRelease(Queue);
	if (Device)
		wgpuDeviceRelease(Device);
}

void WebGPUContext::ConfigureSurface(int width, int height)
{
	if (width == ConfiguredWidth && height == ConfiguredHeight)
		return;

	WGPUSurfaceConfiguration config = {};
	config.device = Device;
	config.format = SurfaceFormat;
	config.usage = WGPUTextureUsage_RenderAttachment;
	config.width = (uint32_t)width;
	config.height = (uint32_t)height;
	config.presentMode = WGPUPresentMode_Fifo;
	wgpuSurfaceConfigure(Surface, &config);

	ConfiguredWidth = width;
	ConfiguredHeight = height;
}
