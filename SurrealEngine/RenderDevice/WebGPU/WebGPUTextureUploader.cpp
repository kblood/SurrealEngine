
#include "Precomp.h"
#include "WebGPUTextureUploader.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "RenderDevice/RenderDevice.h"
#include <map>

WebGPUTextureUploader* WebGPUTextureUploader::GetUploader(TextureFormat format)
{
	static std::map<TextureFormat, std::unique_ptr<WebGPUTextureUploader>> Uploaders;
	if (Uploaders.empty())
	{
		Uploaders[TextureFormat::P8].reset(new WebGPUTextureUploader_P8());
		Uploaders[TextureFormat::BGRA8_LM].reset(new WebGPUTextureUploader_BGRA8_LM());
		Uploaders[TextureFormat::R5G6B5].reset(new WebGPUTextureUploader_R5G6B5());
		Uploaders[TextureFormat::BC1].reset(new WebGPUTextureUploader_4x4Block(WGPUTextureFormat_BC1RGBAUnorm, 8));
		Uploaders[TextureFormat::RGB8].reset(new WebGPUTextureUploader_RGB8());
		Uploaders[TextureFormat::BGRA8].reset(new WebGPUTextureUploader_Simple(WGPUTextureFormat_BGRA8Unorm, 4));
		Uploaders[TextureFormat::RGBA32_F].reset(new WebGPUTextureUploader_Simple(WGPUTextureFormat_RGBA32Float, 16));
	}

	auto it = Uploaders.find(format);
	if (it != Uploaders.end())
		return it->second.get();
	else
		return nullptr;
}

/////////////////////////////////////////////////////////////////////////////

int WebGPUTextureUploader_P8::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 4;
}

void WebGPUTextureUploader_P8::UploadRect(void* d, UnrealMipmap* mip, int x, int y, int w, int h, FColor* palette, bool masked)
{
	int pitch = mip->Width;
	uint8_t* src = mip->Data.data() + x + y * pitch;
	FColor* Ptr = (FColor*)d;
	if (masked)
	{
		FColor translucent(0, 0, 0, 0);
		for (int i = 0; i < h; i++)
		{
			for (int j = 0; j < w; j++)
			{
				int idx = src[j];
				*Ptr++ = (idx != 0) ? palette[idx] : translucent;
			}
			src += pitch;
		}
	}
	else
	{
		for (int i = 0; i < h; i++)
		{
			for (int j = 0; j < w; j++)
			{
				int idx = src[j];
				*Ptr++ = palette[idx];
			}
			src += pitch;
		}
	}
}

/////////////////////////////////////////////////////////////////////////////

int WebGPUTextureUploader_RGB8::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 4;
}

void WebGPUTextureUploader_RGB8::UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, FColor* palette, bool masked)
{
	int pitch = mip->Width * 3;
	uint8_t* src = ((uint8_t*)mip->Data.data()) + x + y * pitch;
	auto Ptr = (FColor*)dst;
	for (int i = 0; i < h; i++)
	{
		int k = 0;
		for (int j = 0; j < w; j++)
		{
			Ptr->R = src[k++];
			Ptr->G = src[k++];
			Ptr->B = src[k++];
			Ptr->A = 255;
			Ptr++;
		}
		src += pitch;
	}
}

/////////////////////////////////////////////////////////////////////////////

int WebGPUTextureUploader_BGRA8_LM::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 4;
}

void WebGPUTextureUploader_BGRA8_LM::UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, FColor* palette, bool masked)
{
	int pitch = mip->Width;
	FColor* src = ((FColor*)mip->Data.data()) + x + y * pitch;
	auto Ptr = (FColor*)dst;
	for (int i = 0; i < h; i++)
	{
		for (int j = 0; j < w; j++)
		{
			FColor Src = src[j];
			Ptr->R = Src.B << 1;
			Ptr->G = Src.G << 1;
			Ptr->B = Src.R << 1;
			Ptr->A = Src.A << 1;
			Ptr++;
		}
		src += pitch;
	}
}

/////////////////////////////////////////////////////////////////////////////

int WebGPUTextureUploader_R5G6B5::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * 4;
}

void WebGPUTextureUploader_R5G6B5::UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, FColor* palette, bool masked)
{
	int pitch = mip->Width;
	uint16_t* src = ((uint16_t*)mip->Data.data()) + x + y * pitch;
	auto Ptr = (FColor*)dst;
	for (int i = 0; i < h; i++)
	{
		for (int j = 0; j < w; j++)
		{
			uint16_t c = src[j];
			uint32_t r5 = (c >> 11) & 0x1f;
			uint32_t g6 = (c >> 5) & 0x3f;
			uint32_t b5 = c & 0x1f;
			Ptr->R = (uint8_t)((r5 * 255 + 15) / 31);
			Ptr->G = (uint8_t)((g6 * 255 + 31) / 63);
			Ptr->B = (uint8_t)((b5 * 255 + 15) / 31);
			Ptr->A = 255;
			Ptr++;
		}
		src += pitch;
	}
}

/////////////////////////////////////////////////////////////////////////////

int WebGPUTextureUploader_Simple::GetUploadSize(int x, int y, int w, int h)
{
	return w * h * BytesPerPixel;
}

void WebGPUTextureUploader_Simple::UploadRect(void* d, UnrealMipmap* mip, int x, int y, int w, int h, FColor* palette, bool masked)
{
	int pitch = mip->Width * BytesPerPixel;
	int size = w * BytesPerPixel;
	uint8_t* src = mip->Data.data() + x * BytesPerPixel + y * pitch;
	uint8_t* dst = (uint8_t*)d;
	for (int i = 0; i < h; i++)
	{
		memcpy(dst, src, size);
		dst += size;
		src += pitch;
	}
}

/////////////////////////////////////////////////////////////////////////////

int WebGPUTextureUploader_4x4Block::GetUploadSize(int x, int y, int w, int h)
{
	int x0 = x / 4;
	int y0 = y / 4;
	int x1 = (x + w + 3) / 4;
	int y1 = (y + h + 3) / 4;
	return (x1 - x0) * (y1 - y0) * BytesPerBlock;
}

void WebGPUTextureUploader_4x4Block::UploadRect(void* d, UnrealMipmap* mip, int x, int y, int w, int h, FColor* palette, bool masked)
{
	int x0 = x / 4;
	int y0 = y / 4;
	int x1 = (x + w + 3) / 4;
	int y1 = (y + h + 3) / 4;

	int pitch = (mip->Width + 3) / 4 * BytesPerBlock;
	int size = (x1 - x0) * BytesPerBlock;
	uint8_t* src = mip->Data.data() + x0 * BytesPerBlock + y0 * pitch;
	uint8_t* dst = (uint8_t*)d;
	for (int i = y0; i < y1; i++)
	{
		memcpy(dst, src, size);
		dst += size;
		src += pitch;
	}
}
