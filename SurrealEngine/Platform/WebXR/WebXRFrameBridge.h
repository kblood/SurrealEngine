#pragma once

#include "Math/coords.h"
#include "Render/ViewFamily.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace WebXR
{
	// Version 3 requires host-owned persistent render targets. Browser compositor
	// textures must never be passed into a native render which can Asyncify-suspend.
	constexpr uint32_t FrameABIVersion = 3;
	constexpr uint32_t MaxViews = 2;
	constexpr uint32_t FrameProjectionDepthZeroToOne = 1u << 0;
	constexpr uint32_t FrameSharedStereoAtlas = 1u << 1;
	constexpr uint32_t FrameKnownFlags = FrameProjectionDepthZeroToOne | FrameSharedStereoAtlas;

	enum class FrameError : int
	{
		None = 0,
		InvalidHeader = 1,
		InvalidView = 2,
		TextureUnavailable = 3,
		RenderDeviceUnavailable = 4,
		PresentationRejected = 5,
		RenderFailed = 6
	};

	enum class Eye : uint32_t
	{
		None = 0,
		Left = 1,
		Right = 2
	};

#pragma pack(push, 1)
	struct PackedFrameHeader
	{
		uint32_t Version;
		uint32_t ByteSize;
		uint32_t ViewCount;
		uint32_t TextureCount;
		double Timestamp;
		uint32_t ResetGeneration;
		uint32_t Flags;
	};

	struct PackedView
	{
		uint32_t Eye;
		uint32_t TextureIndex;
		uint32_t ArrayLayer;
		uint32_t TextureWidth;
		uint32_t TextureHeight;
		int32_t ViewportX;
		int32_t ViewportY;
		int32_t ViewportWidth;
		int32_t ViewportHeight;
		float Position[3];
		float Orientation[4];
		float Projection[16];
	};
#pragma pack(pop)

	static_assert(sizeof(PackedFrameHeader) == 32);
	static_assert(offsetof(PackedFrameHeader, Timestamp) == 16);
	static_assert(sizeof(PackedView) == 128);
	static_assert(offsetof(PackedView, Position) == 36);
	static_assert(offsetof(PackedView, Projection) == 64);

	struct DecodedFrame
	{
		PackedFrameHeader Header = {};
		std::array<PackedView, MaxViews> Views = {};
	};

	struct RecenterState
	{
		bool Valid = false;
		uint32_t ResetGeneration = 0;
		uint32_t RecenterCount = 0;
		vec3 OriginMeters = vec3(0.0f);
		float YawOffset = 0.0f;
	};

	struct EngineTrackedPose
	{
		vec3 Position = vec3(0.0f);
		vec3 Forward = vec3(1.0f, 0.0f, 0.0f);
		vec3 Right = vec3(0.0f, 1.0f, 0.0f);
		vec3 Up = vec3(0.0f, 0.0f, 1.0f);
	};

	bool DecodeFrame(const void* frameData, uint32_t bufferBytes, DecodedFrame& result, FrameError& error);
	EngineTrackedPose TransformCanonicalPose(const vec3& positionMeters, const vec4& orientation,
		const vec3& cameraLocation, const Coords& bodyRotation, float worldUnitsPerMeter,
		const RecenterState& recenter);
	ViewFamily BuildViewFamily(const DecodedFrame& frame, const vec3& cameraLocation,
		const Coords& bodyRotation, float worldUnitsPerMeter, RecenterState& recenter);
	void SetLastFrameError(FrameError error);
	FrameError GetLastFrameError();
}

extern "C"
{
	uint32_t Surreal_GetWebXRFrameABIVersion();
	uint32_t Surreal_GetWebXRFrameHeaderSize();
	uint32_t Surreal_GetWebXRViewSize();
	uint32_t Surreal_GetWebXRFrameMaxViews();
	int Surreal_ValidateWebXRFrame(const void* frameData, uint32_t bufferBytes);
	int Surreal_GetWebXRFrameLastError();
	int Surreal_RenderWebXRFrame(const void* frameData, uint32_t bufferBytes);
	float Surreal_GetWebXRWorldUnitsPerMeter();
	int Surreal_SetWebXRWorldUnitsPerMeter(float worldUnitsPerMeter);
	void Surreal_ResetWebXRPose();
	uint32_t Surreal_GetWebXRPoseRecenterCount();
}
