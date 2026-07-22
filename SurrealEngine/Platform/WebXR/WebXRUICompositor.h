#pragma once

#ifdef __EMSCRIPTEN__

#include "Platform/WebXR/WebXRUIProvider.h"
#include "RenderDevice/WebGPU/WebGPURenderDevice.h"

namespace WebXR
{
	bool BindUISurfaceTargets(WebGPURenderDevice* device, WGPUTextureFormat format);
	void UnbindUISurfaceTargets(WebGPURenderDevice* device);
	bool CompositeUISurfaces(WebGPURenderDevice* device, WGPUTextureFormat format,
		const ViewFamily& family, const XRUICanvasReplayFrame& replayFrame,
		const WGPUTextureView* projectionViews, uint32_t projectionViewCount);
	void ResetUICompositor(WebGPURenderDevice* device);
}

#endif
