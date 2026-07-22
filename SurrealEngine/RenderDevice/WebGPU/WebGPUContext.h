#pragma once

#include <webgpu/webgpu.h>

// Owns the process-wide WebGPU device/queue/instance/surface acquisition.
//
// The WGPUDevice itself is not created here - it is handed in from JS before
// main() ever runs (Module.preinitializedWebGPUDevice, set after an async
// navigator.gpu.requestAdapter()/requestDevice() in web/index.html) since
// Engine::Setup() -> GameWindow::Create() -> RenderDevice::Create() is fully
// synchronous C++ with no Asyncify in this build. See
// The browser canvas owns presentation; this object owns WebGPU lifetime.
class WebGPUContext
{
public:
	WebGPUContext();
	~WebGPUContext();

	WGPUDevice Device = nullptr;
	WGPUQueue Queue = nullptr;
	WGPUInstance Instance = nullptr;
	WGPUSurface Surface = nullptr;
	WGPUTextureFormat SurfaceFormat = WGPUTextureFormat_BGRA8Unorm;

	// (Re)configures the surface to the given pixel size. Cheap to call every
	// frame - only reconfigures the underlying swap chain when the size
	// actually changed.
	void ConfigureSurface(int width, int height);

private:
	int ConfiguredWidth = 0;
	int ConfiguredHeight = 0;
};
