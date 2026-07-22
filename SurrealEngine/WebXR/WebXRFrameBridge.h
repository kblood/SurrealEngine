#pragma once

#include <cstddef>
#include <cstdint>

// Version 2 of the single-copy JavaScript/WASM frame ABI. These structures
// are wire layouts, not normal C++ object layouts: keep them packed, use only
// fixed-width members, and copy incoming data before using it.
namespace WebXRFrameABI
{
	constexpr uint32_t Version = 2;
	constexpr uint32_t MaxViews = 2;
	constexpr uint32_t MaxInputSources = 2;
	constexpr uint32_t MaxPacketBytes = 532;
	constexpr uint32_t KnownFlags = 0;
	constexpr uint32_t KnownControllerFlags = 0x0f;

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
		uint32_t inputSourceCount;
		uint32_t inputOffset;
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

	struct Controller
	{
		uint32_t sourceId;
		uint32_t handedness;
		uint32_t flags;
		uint32_t buttonsPressed;
		uint32_t buttonsTouched;
		uint32_t reserved;
		float axes[4];
		float buttonValues[8];
		float gripPosition[3];
		float gripOrientation[4];
		float aimPosition[3];
		float aimOrientation[4];
	};
#pragma pack(pop)

	static_assert(sizeof(FrameHeader) == 44);
	static_assert(offsetof(FrameHeader, timestamp) == 16);
	static_assert(offsetof(FrameHeader, textureHeight) == 32);
	static_assert(offsetof(FrameHeader, inputSourceCount) == 36);
	static_assert(offsetof(FrameHeader, inputOffset) == 40);
	static_assert(sizeof(View) == 116);
	static_assert(offsetof(View, position) == 24);
	static_assert(offsetof(View, orientation) == 36);
	static_assert(offsetof(View, projection) == 52);
	static_assert(sizeof(Controller) == 128);
	static_assert(offsetof(Controller, axes) == 24);
	static_assert(offsetof(Controller, buttonValues) == 40);
	static_assert(offsetof(Controller, gripPosition) == 72);
	static_assert(offsetof(Controller, gripOrientation) == 84);
	static_assert(offsetof(Controller, aimPosition) == 100);
	static_assert(offsetof(Controller, aimOrientation) == 112);
	static_assert(sizeof(FrameHeader) + MaxViews * sizeof(View) + MaxInputSources * sizeof(Controller) == MaxPacketBytes);
}

extern "C"
{
	uint32_t Surreal_GetWebXRFrameABIVersion();
	uint32_t Surreal_GetWebXRFrameHeaderSize();
	uint32_t Surreal_GetWebXRViewSize();
	uint32_t Surreal_GetWebXRFrameMaxViews();
	uint32_t Surreal_GetWebXRInputRecordSize();
	uint32_t Surreal_GetWebXRFrameMaxInputs();
	int Surreal_ValidateWebXRFrame(const void* frameData, uint32_t bufferBytes);
	int Surreal_RenderWebXRFrame(const void* frameData, uint32_t bufferBytes);
	int Surreal_GetWebXRFrameLastError();
	float Surreal_GetWebXRWorldUnitsPerMeter();
	int Surreal_SetWebXRWorldUnitsPerMeter(float worldUnitsPerMeter);
	void Surreal_ResetWebXRPose();
	uint32_t Surreal_GetWebXRPoseRecenterCount();
	uint32_t Surreal_GetWebXRPoseResetGeneration();
	int Surreal_RunWebXRPoseMathSelfTest();
	uint32_t Surreal_GetWebXRInputFrameGeneration();
	uint32_t Surreal_GetWebXRPublishedInputSourceCount();
	int Surreal_RunWebXRInputStateSelfTest();
}
