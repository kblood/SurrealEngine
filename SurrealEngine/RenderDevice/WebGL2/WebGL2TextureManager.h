#pragma once

#include "RenderDevice/RenderDevice.h"

#include <GLES3/gl3.h>
#include <cstdint>
#include <unordered_map>
#include <vector>

class WebGL2CachedTexture
{
public:
	GLuint Texture = 0;
	uint32_t Generation = 0;
	int MipCount = 0;

	float UScale = 1.0f;
	float VScale = 1.0f;
	float PanX = 0.0f;
	float PanY = 0.0f;
	float UMult = 1.0f;
	float VMult = 1.0f;
};

// Owns the WebGL texture-name cache and converts the legacy UE1 texture
// formats used by the supported games to portable RGBA8 uploads. Keeping the
// conversion here (rather than relying on optional browser compression
// extensions) makes the same package work on desktop and standalone Quest.
class WebGL2TextureManager
{
public:
	explicit WebGL2TextureManager(uint32_t generation);
	~WebGL2TextureManager();

	WebGL2CachedTexture* GetTexture(FTextureInfo* info, bool masked);
	WebGL2CachedTexture* GetNullTexture();
	void UpdateTextureRect(FTextureInfo* info, int x, int y, int width, int height);
	bool SupportsTextureFormat(TextureFormat format) const;

	void ResetForGeneration(uint32_t generation);
	void Clear(bool deleteObjects);
	int TextureCount() const;

private:
	struct CacheEntry
	{
		WebGL2CachedTexture Texture;
	};

	bool UploadTexture(WebGL2CachedTexture& texture, const FTextureInfo& info, bool masked);
	bool UploadMip(const FTextureInfo& info, int level, bool masked, std::vector<uint8_t>& pixels);
	bool ConvertRect(const FTextureInfo& info, const UnrealMipmap& mip, int x, int y, int width, int height, bool masked, std::vector<uint8_t>& pixels) const;
	void SetTextureCoordinates(WebGL2CachedTexture& texture, const FTextureInfo& info) const;
	void DeleteTexture(WebGL2CachedTexture& texture);

	uint32_t generation = 0;
	std::unordered_map<uint64_t, CacheEntry> cache[2];
	WebGL2CachedTexture nullTexture;
};
