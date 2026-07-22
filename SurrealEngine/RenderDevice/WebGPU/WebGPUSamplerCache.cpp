
#include "Precomp.h"
#include "WebGPUSamplerCache.h"
#include "WebGPUContext.h"

WebGPUSamplerCache::WebGPUSamplerCache(WebGPUContext* context)
{
	for (int i = 0; i < 16; i++)
	{
		int dummyMipmapCount = (i >> 2) & 3;
		bool point = (i & 1) != 0;
		bool mirror = (i & 2) != 0;

		WGPUSamplerDescriptor samplerDesc = {};
		samplerDesc.addressModeU = mirror ? WGPUAddressMode_MirrorRepeat : WGPUAddressMode_Repeat;
		samplerDesc.addressModeV = mirror ? WGPUAddressMode_MirrorRepeat : WGPUAddressMode_Repeat;
		samplerDesc.addressModeW = mirror ? WGPUAddressMode_MirrorRepeat : WGPUAddressMode_Repeat;
		samplerDesc.magFilter = point ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
		samplerDesc.minFilter = point ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
		samplerDesc.mipmapFilter = point ? WGPUMipmapFilterMode_Nearest : WGPUMipmapFilterMode_Linear;
		samplerDesc.lodMinClamp = (float)dummyMipmapCount;
		samplerDesc.lodMaxClamp = 32.0f;
		samplerDesc.compare = WGPUCompareFunction_Undefined;
		samplerDesc.maxAnisotropy = point ? 1 : 8;

		Samplers[i] = wgpuDeviceCreateSampler(context->Device, &samplerDesc);
	}
}

WebGPUSamplerCache::~WebGPUSamplerCache()
{
	for (WGPUSampler sampler : Samplers)
	{
		if (sampler)
			wgpuSamplerRelease(sampler);
	}
}
