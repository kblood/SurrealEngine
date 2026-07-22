#pragma once

#include <cstddef>
#include <cstdint>

// Version 1 of the single-copy JavaScript/WASM frame ABI. These structures
// are wire layouts, not normal C++ object layouts: keep them packed, use only
// fixed-width members, and copy incoming data before using it.
namespace WebXRFrameABI
{
	constexpr uint32_t Version = 1;
	constexpr uint32_t MaxViews = 2;
	constexpr uint32_t KnownFlags = 0;

	enum Eye : uint32_t
	{
		None = 0,
		Left = 1,
		Right = 2
	};

#pragma pack(push, 1)
	struct FrameHeader
	{
		uint32_t version;
		uint32_t byteSize;
		uint32_t viewCount;
		uint32_t flags;
		double timestamp;
		uint32_t resetGeneration;
		uint32_t textureWidth;
		uint32_t textureHeight;
	};

	struct View
	{
		uint32_t eye;
		uint32_t arrayLayer;
		int32_t viewportX;
		int32_t viewportY;
		int32_t viewportWidth;
		int32_t viewportHeight;
		float position[3];
		float orientation[4];
		float projection[16];
	};
#pragma pack(pop)

	static_assert(sizeof(FrameHeader) == 36);
	static_assert(offsetof(FrameHeader, timestamp) == 16);
	static_assert(offsetof(FrameHeader, textureHeight) == 32);
	static_assert(sizeof(View) == 116);
	static_assert(offsetof(View, position) == 24);
	static_assert(offsetof(View, orientation) == 36);
	static_assert(offsetof(View, projection) == 52);
}

extern "C"
{
	uint32_t Surreal_GetWebXRFrameABIVersion();
	uint32_t Surreal_GetWebXRFrameHeaderSize();
	uint32_t Surreal_GetWebXRViewSize();
	uint32_t Surreal_GetWebXRFrameMaxViews();
	int Surreal_ValidateWebXRFrame(const void* frameData, uint32_t bufferBytes);
	int Surreal_RenderWebXRFrame(const void* frameData, uint32_t bufferBytes);
	int Surreal_GetWebXRFrameLastError();
}
