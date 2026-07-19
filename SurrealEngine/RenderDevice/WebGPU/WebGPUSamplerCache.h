#pragma once

#include <webgpu/webgpu.h>

class WebGPUContext;

// Direct port of D3D11RenderDevice::CreateSceneSamplers's 16-entry sampler
// array, keyed the same way: bit 0 = point filtering, bit 1 = wrap mode,
// bits 2-3 = dummy mipmap count (see WebGPUUploadManager's minSize padding).
//
// Deviation from D3D11: D3D11 uses MIRROR_ONCE address mode (mirror, then
// clamp-to-border past the first mirror). Core WebGPU has no clamp-to-border
// or mirror-once mode, so this uses plain mirror-repeat instead - a cosmetic
// difference only visible on UV coordinates that wrap more than once, which
// is rare in UT99 content.
class WebGPUSamplerCache
{
public:
	WebGPUSamplerCache(WebGPUContext* context);
	~WebGPUSamplerCache();

	WGPUSampler Get(uint32_t samplerMode) const { return Samplers[samplerMode & 15]; }

private:
	WGPUSampler Samplers[16] = {};
};
