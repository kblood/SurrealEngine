#include <emscripten.h>
#include <emscripten/threading.h>
#include <webgpu/webgpu.h>

#include <cstdio>

EM_JS(int, worker_has_preinitialized_webgpu_device, (), {
	return Module['preinitializedWebGPUDevice'] ? 1 : 0;
});

EM_JS(int, worker_has_webgpu_entry_point, (), {
	return typeof navigator !== 'undefined' && navigator.gpu ? 1 : 0;
});

int main()
{
	if (emscripten_is_main_browser_thread())
	{
		std::fprintf(stderr, "FAIL WebGPU probe main did not move to a worker\n");
		return 1;
	}
	std::printf("INFO pthread-webgpu-device worker-navigator-gpu=%d\n",
		worker_has_webgpu_entry_point());
	if (!worker_has_preinitialized_webgpu_device())
	{
		std::fprintf(stderr,
			"FAIL browser-thread preinitialized WebGPU device is absent in the proxied main worker\n");
		return 2;
	}

	// The browser harness sets Module.preinitializedWebGPUDevice before callMain,
	// exactly as the product launcher does. This call deliberately measures
	// whether emdawnwebgpu makes that browser-thread JS object visible in the
	// PROXY_TO_PTHREAD worker realm.
	WGPUDevice device = emscripten_webgpu_get_device();
	if (!device)
	{
		std::fprintf(stderr, "FAIL worker could not import the preinitialized WebGPU device\n");
		return 3;
	}

	std::printf("PASS pthread-webgpu-device worker=1 device=1\n");
	wgpuDeviceRelease(device);
	return 0;
}
