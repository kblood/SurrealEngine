
#include "Precomp.h"
#include "WebGPUUploadManager.h"
#include "WebGPURenderDevice.h"
#include "WebGPUCachedTexture.h"

// Conservative fallback for WebGPU's guaranteed-minimum maxTextureDimension2D
// limit (8192) - anything larger falls back to the null/white texture, same
// graceful-degrade path D3D11RenderDevice::UploadTexture takes for its own
// (larger) D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION bound.
static const uint32_t MaxWebGPUTextureDimension = 8192;

WebGPUUploadManager::WebGPUUploadManager(WebGPURenderDevice* renderer) : renderer(renderer)
{
}

WebGPUUploadManager::~WebGPUUploadManager()
{
}

bool WebGPUUploadManager::SupportsTextureFormat(TextureFormat Format) const
{
	return WebGPUTextureUploader::GetUploader(Format) != nullptr;
}

void WebGPUUploadManager::UploadTexture(WebGPUCachedTexture* tex, const FTextureInfo& Info, bool masked)
{
	int width = Info.USize;
	int height = Info.VSize;
	int mipcount = Info.NumMips;

	WebGPUTextureUploader* uploader = WebGPUTextureUploader::GetUploader(Info.Format);

	if ((uint32_t)Info.USize > MaxWebGPUTextureDimension || (uint32_t)Info.VSize > MaxWebGPUTextureDimension || !uploader)
	{
		width = 1;
		height = 1;
		mipcount = 1;
		uploader = nullptr;
	}

	WGPUTextureFormat format = uploader ? uploader->GetWGPUFormat() : WGPUTextureFormat_RGBA8Unorm;

	// BC-compressed formats need complete 4x4 blocks at every mip level in
	// WebGPU too. Pad with dummy empty mip levels the same way D3D11 does.
	int minSize = Info.Format == TextureFormat::BC1 ? 4 : 0;

	if (!tex->Texture)
	{
		if (width < minSize || height < minSize)
		{
			if (width == 1 || height == 1)
			{
				width *= 4;
				height *= 4;
				mipcount += 2;
				tex->DummyMipmapCount = 2;
			}
			else
			{
				width *= 2;
				height *= 2;
				mipcount += 1;
				tex->DummyMipmapCount = 1;
			}
		}

		WGPUTextureDescriptor texDesc = {};
		texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
		texDesc.dimension = WGPUTextureDimension_2D;
		texDesc.size = { (uint32_t)width, (uint32_t)height, 1 };
		texDesc.format = format;
		texDesc.mipLevelCount = (uint32_t)mipcount;
		texDesc.sampleCount = 1;
		tex->Texture = wgpuDeviceCreateTexture(renderer->Context->Device, &texDesc);

		WGPUTextureViewDescriptor viewDesc = {};
		viewDesc.format = format;
		viewDesc.dimension = WGPUTextureViewDimension_2D;
		viewDesc.baseMipLevel = 0;
		viewDesc.mipLevelCount = (uint32_t)mipcount;
		viewDesc.baseArrayLayer = 0;
		viewDesc.arrayLayerCount = 1;
		viewDesc.aspect = WGPUTextureAspect_All;
		tex->View = wgpuTextureCreateView(tex->Texture, &viewDesc);
	}

	if (uploader)
		UploadData(tex->Texture, Info, masked, uploader, tex->DummyMipmapCount, minSize);
	else
		UploadWhite(tex->Texture);

	renderer->Stats.Uploads++;
}

void WebGPUUploadManager::UploadTextureRect(WebGPUCachedTexture* tex, const FTextureInfo& Info, int x, int y, int w, int h)
{
	WebGPUTextureUploader* uploader = WebGPUTextureUploader::GetUploader(Info.Format);
	if (!uploader || Info.NumMips < 1 || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > Info.Mips[0].Width || y + h > Info.Mips[0].Height || Info.Mips[0].Data.empty())
		return;

	size_t pixelsSize = uploader->GetUploadSize(x, y, w, h);
	uint8_t* data = GetUploadBuffer(pixelsSize);
	uploader->UploadRect(data, &Info.Mips[0], x, y, w, h, Info.Palette, false);

	uint32_t bytesPerRow = (uint32_t)uploader->GetUploadSize(0, 0, w, 1);

	WGPUTexelCopyTextureInfo destination = {};
	destination.texture = tex->Texture;
	destination.mipLevel = 0;
	destination.origin = { (uint32_t)x, (uint32_t)y, 0 };
	destination.aspect = WGPUTextureAspect_All;

	WGPUTexelCopyBufferLayout dataLayout = {};
	dataLayout.offset = 0;
	dataLayout.bytesPerRow = bytesPerRow;
	dataLayout.rowsPerImage = (uint32_t)h;

	WGPUExtent3D writeSize = { (uint32_t)w, (uint32_t)h, 1 };

	wgpuQueueWriteTexture(renderer->Context->Queue, &destination, data, pixelsSize, &dataLayout, &writeSize);

	renderer->Stats.RectUploads++;
}

void WebGPUUploadManager::UploadData(WGPUTexture image, const FTextureInfo& Info, bool masked, WebGPUTextureUploader* uploader, int dummyMipmapCount, int minSize)
{
	for (int level = 0; level < Info.NumMips; level++)
	{
		UnrealMipmap* Mip = &Info.Mips[level];
		if (!Mip->Data.empty())
		{
			uint32_t mipwidth = std::max((uint32_t)Mip->Width, (uint32_t)minSize);
			uint32_t mipheight = std::max((uint32_t)Mip->Height, (uint32_t)minSize);

			size_t mipsize = uploader->GetUploadSize(0, 0, mipwidth, mipheight);
			auto data = GetUploadBuffer(mipsize);
			uploader->UploadRect(data, Mip, 0, 0, mipwidth, mipheight, Info.Palette, masked);

			uint32_t bytesPerRow = (uint32_t)uploader->GetUploadSize(0, 0, mipwidth, 1);

			WGPUTexelCopyTextureInfo destination = {};
			destination.texture = image;
			destination.mipLevel = (uint32_t)(level + dummyMipmapCount);
			destination.origin = { 0, 0, 0 };
			destination.aspect = WGPUTextureAspect_All;

			WGPUTexelCopyBufferLayout dataLayout = {};
			dataLayout.offset = 0;
			dataLayout.bytesPerRow = bytesPerRow;
			dataLayout.rowsPerImage = mipheight;

			WGPUExtent3D writeSize = { mipwidth, mipheight, 1 };

			wgpuQueueWriteTexture(renderer->Context->Queue, &destination, data, mipsize, &dataLayout, &writeSize);
		}
	}
}

void WebGPUUploadManager::UploadWhite(WGPUTexture image)
{
	auto data = (uint32_t*)GetUploadBuffer(sizeof(uint32_t));
	data[0] = 0xffffffff;

	WGPUTexelCopyTextureInfo destination = {};
	destination.texture = image;
	destination.mipLevel = 0;
	destination.origin = { 0, 0, 0 };
	destination.aspect = WGPUTextureAspect_All;

	WGPUTexelCopyBufferLayout dataLayout = {};
	dataLayout.offset = 0;
	dataLayout.bytesPerRow = 4;
	dataLayout.rowsPerImage = 1;

	WGPUExtent3D writeSize = { 1, 1, 1 };

	wgpuQueueWriteTexture(renderer->Context->Queue, &destination, data, sizeof(uint32_t), &dataLayout, &writeSize);
}

uint8_t* WebGPUUploadManager::GetUploadBuffer(size_t size)
{
	size = (size + 3) / 4;
	if (UploadBuffer.size() < size)
		UploadBuffer.resize(size);
	return (uint8_t*)UploadBuffer.data();
}
