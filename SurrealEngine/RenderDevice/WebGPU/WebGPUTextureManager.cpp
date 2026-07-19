
#include "Precomp.h"
#include "WebGPUTextureManager.h"
#include "WebGPURenderDevice.h"
#include "WebGPUCachedTexture.h"
#include "WebGPUUploadManager.h"
#include <webgpu/webgpu.h>

WebGPUTextureManager::WebGPUTextureManager(WebGPURenderDevice* renderer) : renderer(renderer)
{
}

WebGPUTextureManager::~WebGPUTextureManager()
{
	ClearCache();
}

WebGPUCachedTexture* WebGPUTextureManager::GetFromCache(int masked, uint64_t cacheID)
{
	if (LastTextureResult[masked].first == cacheID && LastTextureResult[masked].second)
		return LastTextureResult[masked].second;

	LastTextureResult[masked].first = cacheID;
	LastTextureResult[masked].second = TextureCache[masked][cacheID].get();

	return LastTextureResult[masked].second;
}

void WebGPUTextureManager::UpdateTextureRect(FTextureInfo* info, int x, int y, int w, int h)
{
	WebGPUCachedTexture* tex = GetFromCache(0, info->CacheID);
	if (tex)
	{
		renderer->Uploads->UploadTextureRect(tex, *info, x, y, w, h);
		info->bRealtimeChanged = 0;
	}
}

WebGPUCachedTexture* WebGPUTextureManager::GetTexture(FTextureInfo* info, bool masked)
{
	if (!info)
		return GetNullTexture();

	if (info->Texture && (info->Texture->PolyFlags() & PF_Masked))
		masked = true;

	if (info->Format != TextureFormat::P8)
		masked = false;

	WebGPUCachedTexture* tex = GetFromCache((int)masked, info->CacheID);
	if (!tex)
	{
		std::unique_ptr<WebGPUCachedTexture>& tex2 = TextureCache[(int)masked][info->CacheID];
		tex2.reset(new WebGPUCachedTexture());
		tex = tex2.get();

		renderer->Uploads->UploadTexture(tex, *info, masked);
	}
	else
	{
		if (info->bRealtimeChanged)
			UploadTexture(info, masked, tex);
	}

	float uscale = info->UScale;
	float vscale = info->VScale;
	tex->UScale = uscale;
	tex->VScale = vscale;
	tex->PanX = info->Pan.x;
	tex->PanY = info->Pan.y;
	tex->UMult = 1.0f / (uscale * info->USize);
	tex->VMult = 1.0f / (vscale * info->VSize);

	return tex;
}

void WebGPUTextureManager::UploadTexture(FTextureInfo* info, bool masked, WebGPUCachedTexture* tex)
{
	if (info->bRealtimeChanged)
	{
		info->bRealtimeChanged = 0;
		renderer->Uploads->UploadTexture(tex, *info, masked);
	}
}

void WebGPUTextureManager::ClearCache()
{
	for (auto& cache : TextureCache)
	{
		cache.clear();
	}
	for (auto& texture : LastTextureResult)
		texture = {};
}

WebGPUCachedTexture* WebGPUTextureManager::CreateNullTexture()
{
	NullTexture.reset(new WebGPUCachedTexture());

	WGPUTextureDescriptor texDesc = {};
	texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
	texDesc.dimension = WGPUTextureDimension_2D;
	texDesc.size = { 1, 1, 1 };
	texDesc.format = WGPUTextureFormat_RGBA8Unorm;
	texDesc.mipLevelCount = 1;
	texDesc.sampleCount = 1;
	NullTexture->Texture = wgpuDeviceCreateTexture(renderer->Context->Device, &texDesc);

	WGPUTextureViewDescriptor viewDesc = {};
	viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
	viewDesc.dimension = WGPUTextureViewDimension_2D;
	viewDesc.baseMipLevel = 0;
	viewDesc.mipLevelCount = 1;
	viewDesc.baseArrayLayer = 0;
	viewDesc.arrayLayerCount = 1;
	viewDesc.aspect = WGPUTextureAspect_All;
	NullTexture->View = wgpuTextureCreateView(NullTexture->Texture, &viewDesc);

	renderer->Uploads->UploadWhite(NullTexture->Texture);

	return NullTexture.get();
}
