#pragma once

#include <unordered_map>
#include <memory>

struct FTextureInfo;
class WebGPURenderDevice;
class WebGPUCachedTexture;

// Direct port of D3D11TextureManager.
class WebGPUTextureManager
{
public:
	WebGPUTextureManager(WebGPURenderDevice* renderer);
	~WebGPUTextureManager();

	void UpdateTextureRect(FTextureInfo* info, int x, int y, int w, int h);
	WebGPUCachedTexture* GetTexture(FTextureInfo* info, bool masked);

	void ClearCache();
	int GetTexturesInCache() { return (int)(TextureCache[0].size() + TextureCache[1].size()); }

	WebGPUCachedTexture* GetNullTexture()
	{
		if (NullTexture)
			return NullTexture.get();
		return CreateNullTexture();
	}

private:
	void UploadTexture(FTextureInfo* info, bool masked, WebGPUCachedTexture* tex);
	WebGPUCachedTexture* CreateNullTexture();

	WebGPUCachedTexture* GetFromCache(int masked, uint64_t cacheID);

	WebGPURenderDevice* renderer = nullptr;
	std::unordered_map<uint64_t, std::unique_ptr<WebGPUCachedTexture>> TextureCache[2];
	std::unique_ptr<WebGPUCachedTexture> NullTexture;

	std::pair<uint64_t, WebGPUCachedTexture*> LastTextureResult[2];
};
