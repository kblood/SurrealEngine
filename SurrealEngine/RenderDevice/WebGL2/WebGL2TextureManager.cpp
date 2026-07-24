#include "Precomp.h"
#include "WebGL2TextureManager.h"

#include "UObject/ULevel.h"
#include "UObject/UTexture.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
	constexpr int MaxTextureDimension = 8192;

	uint8_t Expand5(uint16_t value) { return static_cast<uint8_t>((value * 255 + 15) / 31); }
	uint8_t Expand6(uint16_t value) { return static_cast<uint8_t>((value * 255 + 31) / 63); }

	void Decode565(uint16_t value, uint8_t* rgba)
	{
		rgba[0] = Expand5((value >> 11) & 31);
		rgba[1] = Expand6((value >> 5) & 63);
		rgba[2] = Expand5(value & 31);
		rgba[3] = 255;
	}

	void DecodeBC1(const UnrealMipmap& mip, std::vector<uint8_t>& pixels)
	{
		pixels.assign(static_cast<size_t>(mip.Width) * mip.Height * 4, 0);
		const int blocksWide = (mip.Width + 3) / 4;
		const int blocksHigh = (mip.Height + 3) / 4;
		const uint8_t* source = mip.Data.data();
		const size_t available = mip.Data.size();

		for (int by = 0; by < blocksHigh; by++)
		{
			for (int bx = 0; bx < blocksWide; bx++)
			{
				const size_t offset = static_cast<size_t>(by * blocksWide + bx) * 8;
				if (offset + 8 > available)
					return;
				const uint8_t* block = source + offset;
				const uint16_t c0 = static_cast<uint16_t>(block[0] | (block[1] << 8));
				const uint16_t c1 = static_cast<uint16_t>(block[2] | (block[3] << 8));
				uint8_t colors[4][4] = {};
				Decode565(c0, colors[0]);
				Decode565(c1, colors[1]);
				if (c0 > c1)
				{
					for (int channel = 0; channel < 3; channel++)
					{
						colors[2][channel] = static_cast<uint8_t>((2 * colors[0][channel] + colors[1][channel]) / 3);
						colors[3][channel] = static_cast<uint8_t>((colors[0][channel] + 2 * colors[1][channel]) / 3);
					}
					colors[2][3] = colors[3][3] = 255;
				}
				else
				{
					for (int channel = 0; channel < 3; channel++)
						colors[2][channel] = static_cast<uint8_t>((colors[0][channel] + colors[1][channel]) / 2);
					colors[2][3] = 255;
					colors[3][3] = 0;
				}

				uint32_t selectors = static_cast<uint32_t>(block[4]) |
					(static_cast<uint32_t>(block[5]) << 8) |
					(static_cast<uint32_t>(block[6]) << 16) |
					(static_cast<uint32_t>(block[7]) << 24);
				for (int py = 0; py < 4; py++)
				{
					for (int px = 0; px < 4; px++)
					{
						const int x = bx * 4 + px;
						const int y = by * 4 + py;
						const uint32_t selector = selectors & 3;
						selectors >>= 2;
						if (x >= mip.Width || y >= mip.Height)
							continue;
						std::memcpy(pixels.data() + (static_cast<size_t>(y) * mip.Width + x) * 4, colors[selector], 4);
					}
				}
			}
		}
	}

	uint8_t FloatToByte(float value)
	{
		return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
	}
}

WebGL2TextureManager::WebGL2TextureManager(uint32_t initialGeneration) : generation(initialGeneration)
{
}

WebGL2TextureManager::~WebGL2TextureManager()
{
	Clear(true);
}

bool WebGL2TextureManager::SupportsTextureFormat(TextureFormat format) const
{
	switch (format)
	{
	case TextureFormat::P8:
	case TextureFormat::BGRA8_LM:
	case TextureFormat::R5G6B5:
	case TextureFormat::BC1:
	case TextureFormat::RGB8:
	case TextureFormat::BGRA8:
	case TextureFormat::RGBA32_F:
		return true;
	default:
		return false;
	}
}

void WebGL2TextureManager::DeleteTexture(WebGL2CachedTexture& texture)
{
	if (texture.Texture)
		glDeleteTextures(1, &texture.Texture);
	texture = {};
}

void WebGL2TextureManager::Clear(bool deleteObjects)
{
	if (deleteObjects)
	{
		for (auto& maskedCache : cache)
			for (auto& item : maskedCache)
				DeleteTexture(item.second.Texture);
		DeleteTexture(nullTexture);
	}
	for (auto& maskedCache : cache)
		maskedCache.clear();
	if (!deleteObjects)
		nullTexture = {};
}

void WebGL2TextureManager::ResetForGeneration(uint32_t newGeneration)
{
	// Names from a lost WebGL context are no longer valid in the restored
	// context and must not be passed to glDeleteTextures there.
	Clear(false);
	generation = newGeneration;
}

int WebGL2TextureManager::TextureCount() const
{
	return static_cast<int>(cache[0].size() + cache[1].size() + (nullTexture.Texture ? 1 : 0));
}

void WebGL2TextureManager::SetTextureCoordinates(WebGL2CachedTexture& texture, const FTextureInfo& info) const
{
	texture.UScale = info.UScale;
	texture.VScale = info.VScale;
	texture.PanX = info.Pan.x;
	texture.PanY = info.Pan.y;
	texture.UMult = info.USize > 0 && info.UScale != 0.0f ? 1.0f / (info.UScale * info.USize) : 1.0f;
	texture.VMult = info.VSize > 0 && info.VScale != 0.0f ? 1.0f / (info.VScale * info.VSize) : 1.0f;
}

WebGL2CachedTexture* WebGL2TextureManager::GetNullTexture()
{
	if (!nullTexture.Texture)
	{
		const uint8_t white[] = { 255, 255, 255, 255 };
		glGenTextures(1, &nullTexture.Texture);
		glBindTexture(GL_TEXTURE_2D, nullTexture.Texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		nullTexture.Generation = generation;
		nullTexture.MipCount = 1;
	}
	return &nullTexture;
}

WebGL2CachedTexture* WebGL2TextureManager::GetTexture(FTextureInfo* info, bool masked)
{
	if (!info)
		return GetNullTexture();
	if (info->Texture && (info->Texture->PolyFlags() & PF_Masked))
		masked = true;
	if (info->Format != TextureFormat::P8)
		masked = false;

	auto& item = cache[masked ? 1 : 0][info->CacheID];
	if (!item.Texture.Texture || item.Texture.Generation != generation || info->bRealtimeChanged)
	{
		if (item.Texture.Texture && item.Texture.Generation == generation)
			DeleteTexture(item.Texture);
		if (!UploadTexture(item.Texture, *info, masked))
		{
			cache[masked ? 1 : 0].erase(info->CacheID);
			return GetNullTexture();
		}
		info->bRealtimeChanged = false;
	}
	SetTextureCoordinates(item.Texture, *info);
	return &item.Texture;
}

bool WebGL2TextureManager::UploadMip(const FTextureInfo& info, int level, bool masked, std::vector<uint8_t>& pixels)
{
	if (level < 0 || level >= info.NumMips || !info.Mips || info.Mips[level].Data.empty())
		return false;
	const UnrealMipmap& mip = info.Mips[level];
	return ConvertRect(info, mip, 0, 0, mip.Width, mip.Height, masked, pixels);
}

bool WebGL2TextureManager::UploadTexture(WebGL2CachedTexture& texture, const FTextureInfo& info, bool masked)
{
	if (!SupportsTextureFormat(info.Format) || info.USize <= 0 || info.VSize <= 0 ||
		info.USize > MaxTextureDimension || info.VSize > MaxTextureDimension || info.NumMips <= 0 || !info.Mips)
		return false;

	glGenTextures(1, &texture.Texture);
	glBindTexture(GL_TEXTURE_2D, texture.Texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	std::vector<uint8_t> pixels;
	int uploadedLevels = 0;
	for (int level = 0; level < info.NumMips; level++)
	{
		if (!UploadMip(info, level, masked, pixels))
			break;
		const UnrealMipmap& mip = info.Mips[level];
		glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, mip.Width, mip.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		uploadedLevels++;
	}
	if (uploadedLevels == 0)
	{
		DeleteTexture(texture);
		return false;
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, uploadedLevels - 1);
	texture.Generation = generation;
	texture.MipCount = uploadedLevels;
	SetTextureCoordinates(texture, info);
	return true;
}

void WebGL2TextureManager::UpdateTextureRect(FTextureInfo* info, int x, int y, int width, int height)
{
	if (!info || width <= 0 || height <= 0 || x < 0 || y < 0 || !info->Mips || info->NumMips < 1)
		return;
	auto found = cache[0].find(info->CacheID);
	if (found == cache[0].end() || found->second.Texture.Generation != generation)
		return;
	const UnrealMipmap& mip = info->Mips[0];
	if (x + width > mip.Width || y + height > mip.Height)
		return;

	// Block-compressed source rectangles do not necessarily align with BC1
	// blocks. Re-uploading level zero is deterministic and rare for realtime
	// textures, while all ordinary formats use the smaller sub-image path.
	if (info->Format == TextureFormat::BC1)
	{
		std::vector<uint8_t> fullPixels;
		if (ConvertRect(*info, mip, 0, 0, mip.Width, mip.Height, false, fullPixels))
		{
			glBindTexture(GL_TEXTURE_2D, found->second.Texture.Texture);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mip.Width, mip.Height, GL_RGBA, GL_UNSIGNED_BYTE, fullPixels.data());
		}
		return;
	}

	std::vector<uint8_t> pixels;
	if (!ConvertRect(*info, mip, x, y, width, height, false, pixels))
		return;
	glBindTexture(GL_TEXTURE_2D, found->second.Texture.Texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	info->bRealtimeChanged = false;
}

bool WebGL2TextureManager::ConvertRect(const FTextureInfo& info, const UnrealMipmap& mip, int x, int y, int width, int height, bool masked, std::vector<uint8_t>& pixels) const
{
	if (width <= 0 || height <= 0 || x < 0 || y < 0 || x + width > mip.Width || y + height > mip.Height)
		return false;
	if (info.Format == TextureFormat::BC1)
	{
		std::vector<uint8_t> decoded;
		DecodeBC1(mip, decoded);
		pixels.resize(static_cast<size_t>(width) * height * 4);
		for (int row = 0; row < height; row++)
			std::memcpy(pixels.data() + static_cast<size_t>(row) * width * 4,
				decoded.data() + (static_cast<size_t>(y + row) * mip.Width + x) * 4,
				static_cast<size_t>(width) * 4);
		return true;
	}

	pixels.resize(static_cast<size_t>(width) * height * 4);
	for (int row = 0; row < height; row++)
	{
		for (int column = 0; column < width; column++)
		{
			const int sourceIndex = (y + row) * mip.Width + x + column;
			uint8_t* destination = pixels.data() + (static_cast<size_t>(row) * width + column) * 4;
			switch (info.Format)
			{
			case TextureFormat::P8:
			{
				if (!info.Palette || static_cast<size_t>(sourceIndex) >= mip.Data.size()) return false;
				const uint8_t paletteIndex = mip.Data[sourceIndex];
				const FColor color = (masked && paletteIndex == 0) ? FColor(0, 0, 0, 0) : info.Palette[paletteIndex];
				destination[0] = color.R; destination[1] = color.G; destination[2] = color.B; destination[3] = color.A;
				break;
			}
			case TextureFormat::BGRA8_LM:
			{
				if ((static_cast<size_t>(sourceIndex) + 1) * 4 > mip.Data.size()) return false;
				const FColor* colors = reinterpret_cast<const FColor*>(mip.Data.data());
				const FColor color = colors[sourceIndex];
				destination[0] = static_cast<uint8_t>(color.B << 1); destination[1] = static_cast<uint8_t>(color.G << 1);
				destination[2] = static_cast<uint8_t>(color.R << 1); destination[3] = static_cast<uint8_t>(color.A << 1);
				break;
			}
			case TextureFormat::R5G6B5:
			{
				if ((static_cast<size_t>(sourceIndex) + 1) * 2 > mip.Data.size()) return false;
				uint16_t packed;
				std::memcpy(&packed, mip.Data.data() + static_cast<size_t>(sourceIndex) * 2, sizeof(packed));
				Decode565(packed, destination);
				break;
			}
			case TextureFormat::RGB8:
			{
				const size_t offset = static_cast<size_t>(sourceIndex) * 3;
				if (offset + 3 > mip.Data.size()) return false;
				destination[0] = mip.Data[offset]; destination[1] = mip.Data[offset + 1]; destination[2] = mip.Data[offset + 2]; destination[3] = 255;
				break;
			}
			case TextureFormat::BGRA8:
			{
				const size_t offset = static_cast<size_t>(sourceIndex) * 4;
				if (offset + 4 > mip.Data.size()) return false;
				destination[0] = mip.Data[offset + 2]; destination[1] = mip.Data[offset + 1]; destination[2] = mip.Data[offset]; destination[3] = mip.Data[offset + 3];
				break;
			}
			case TextureFormat::RGBA32_F:
			{
				const size_t offset = static_cast<size_t>(sourceIndex) * 16;
				if (offset + 16 > mip.Data.size()) return false;
				float values[4];
				std::memcpy(values, mip.Data.data() + offset, sizeof(values));
				for (int channel = 0; channel < 4; channel++) destination[channel] = FloatToByte(values[channel]);
				break;
			}
			default:
				return false;
			}
		}
	}
	return true;
}
