#pragma once

#include <webgpu/webgpu.h>

// Direct port of D3D11CachedTexture.h.
class WebGPUCachedTexture
{
public:
	WGPUTexture Texture = nullptr;
	WGPUTextureView View = nullptr;
	int RealtimeChangeCount = 0;
	int DummyMipmapCount = 0;

	float UScale = 0.0f;
	float VScale = 0.0f;
	float PanX = 0.0f;
	float PanY = 0.0f;
	float UMult = 0.0f;
	float VMult = 0.0f;

	~WebGPUCachedTexture()
	{
		if (View) wgpuTextureViewRelease(View);
		if (Texture) wgpuTextureRelease(Texture);
	}
};
