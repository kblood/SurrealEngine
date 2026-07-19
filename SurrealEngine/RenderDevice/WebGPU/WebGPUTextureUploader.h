#pragma once

#include <webgpu/webgpu.h>

struct FTextureInfo;
class UnrealMipmap;
struct FColor;
enum class TextureFormat : uint32_t;

// Direct port of D3D11TextureUploader.h/.cpp - pure byte-shuffling CPU code,
// no D3D11 API calls in the original, so it ports near-verbatim. Scope
// trimmed to the formats that appear in real UT99 assets (see
// WEBXR_IMPLEMENTATION_PLAN.md M2); the rare RGB10A2* HDR variants are
// dropped - GetUploader() returns nullptr for them, same graceful-degrade
// path the engine already takes for oversized textures.
class WebGPUTextureUploader
{
public:
	WebGPUTextureUploader(WGPUTextureFormat format) : Format(format) { }
	virtual ~WebGPUTextureUploader() = default;

	virtual int GetUploadSize(int x, int y, int w, int h) = 0;
	virtual void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, FColor* palette, bool masked) = 0;

	WGPUTextureFormat GetWGPUFormat() const { return Format; }

	static WebGPUTextureUploader* GetUploader(TextureFormat format);

private:
	WGPUTextureFormat Format;
};

class WebGPUTextureUploader_P8 : public WebGPUTextureUploader
{
public:
	WebGPUTextureUploader_P8() : WebGPUTextureUploader(WGPUTextureFormat_RGBA8Unorm) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, FColor* palette, bool masked) override;
};

class WebGPUTextureUploader_RGB8 : public WebGPUTextureUploader
{
public:
	WebGPUTextureUploader_RGB8() : WebGPUTextureUploader(WGPUTextureFormat_RGBA8Unorm) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, FColor* palette, bool masked) override;
};

class WebGPUTextureUploader_BGRA8_LM : public WebGPUTextureUploader
{
public:
	WebGPUTextureUploader_BGRA8_LM() : WebGPUTextureUploader(WGPUTextureFormat_RGBA8Unorm) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, FColor* palette, bool masked) override;
};

// D3D11 keeps this format packed as B5G6R5_UNORM. Core WebGPU has no 16-bit
// packed RGB texture format, so this uploader expands to RGBA8 instead.
class WebGPUTextureUploader_R5G6B5 : public WebGPUTextureUploader
{
public:
	WebGPUTextureUploader_R5G6B5() : WebGPUTextureUploader(WGPUTextureFormat_RGBA8Unorm) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, FColor* palette, bool masked) override;
};

class WebGPUTextureUploader_Simple : public WebGPUTextureUploader
{
public:
	WebGPUTextureUploader_Simple(WGPUTextureFormat format, int bytesPerPixel) : WebGPUTextureUploader(format), BytesPerPixel(bytesPerPixel) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, FColor* palette, bool masked) override;

private:
	int BytesPerPixel;
};

class WebGPUTextureUploader_4x4Block : public WebGPUTextureUploader
{
public:
	WebGPUTextureUploader_4x4Block(WGPUTextureFormat format, int bytesPerBlock) : WebGPUTextureUploader(format), BytesPerBlock(bytesPerBlock) { }

	int GetUploadSize(int x, int y, int w, int h) override;
	void UploadRect(void* dst, UnrealMipmap* mip, int x, int y, int w, int h, FColor* palette, bool masked) override;

private:
	int BytesPerBlock;
};
