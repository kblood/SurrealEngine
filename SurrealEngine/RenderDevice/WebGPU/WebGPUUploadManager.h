#pragma once

#include "WebGPUTextureUploader.h"
#include <webgpu/webgpu.h>
#include <vector>

class WebGPURenderDevice;
class WebGPUCachedTexture;
struct FTextureInfo;

// Direct port of D3D11UploadManager, but simpler: WebGPU's wgpuQueueWriteTexture
// takes CPU data straight to a queue call - no explicit staging texture object
// or Map/Unmap dance is needed (see WEBXR_IMPLEMENTATION_PLAN.md M2).
class WebGPUUploadManager
{
public:
	WebGPUUploadManager(WebGPURenderDevice* renderer);
	~WebGPUUploadManager();

	bool SupportsTextureFormat(TextureFormat Format) const;

	void UploadTexture(WebGPUCachedTexture* tex, const FTextureInfo& Info, bool masked);
	void UploadTextureRect(WebGPUCachedTexture* tex, const FTextureInfo& Info, int x, int y, int w, int h);

	void UploadWhite(WGPUTexture image);

private:
	void UploadData(WGPUTexture image, const FTextureInfo& Info, bool masked, WebGPUTextureUploader* uploader, int dummyMipmapCount, int minSize);

	uint8_t* GetUploadBuffer(size_t size);

	WebGPURenderDevice* renderer = nullptr;
	std::vector<uint32_t> UploadBuffer;
};
