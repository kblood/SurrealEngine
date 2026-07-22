
#include "Precomp.h"
#include "WebGPUContext.h"
#include "Utils/Exception.h"

WebGPUContext::WebGPUContext()
{
	Device = emscripten_webgpu_get_device();
	if (!Device)
		Exception::Throw("WebGPU device not available (Module.preinitializedWebGPUDevice was not set before callMain)");

	Queue = wgpuDeviceGetQueue(Device);

	Instance = wgpuCreateInstance(nullptr);
	if (!Instance)
		Exception::Throw("wgpuCreateInstance failed");

	WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc = {};
	canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
	canvasDesc.selector = { "#canvas", 7 };

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
