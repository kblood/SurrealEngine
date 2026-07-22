#pragma once

#include "Platform/WebXR/WebXRFrameBridge.h"
#include "Platform/WebXR/WebXRInputAdapter.h"
#include "Render/XRUISurfaceEngineBinding.h"

#include <array>

namespace WebXR
{
	constexpr PresentationTarget CinematicSurfaceTarget = { 2 };
	constexpr PresentationTarget LoadingSurfaceTarget = { 3 };
	constexpr PresentationTarget MenuSurfaceTarget = { 4 };

	struct PointerFeedback
	{
		bool Active = false;
		XRHand Hand = XRHand::Right;
		XRUISurfaceRay Ray;
		XRUISurfaceContact Contact;
		vec3 HitPoint = vec3(0.0f);
	};

#pragma pack(push, 1)
	struct PackedPointerFeedback
	{
		uint32_t Active = 0;
		uint32_t Hit = 0;
		uint32_t Surface = 0;
		uint32_t Hand = 0;
		float RayOrigin[3] = {};
		float RayDirection[3] = {};
		float HitPoint[3] = {};
		float UV[2] = {};
		float Pixel[2] = {};
		float Distance = 0.0f;
	};
#pragma pack(pop)
	static_assert(sizeof(PackedPointerFeedback) == 72);

	XRUIViewerPose BuildUIViewerPose(const ViewFamily& family);
	XRUISurfaceRay BuildUIRay(const XRPose& pose, const vec3& cameraLocation,
		const Coords& bodyRotation, float worldUnitsPerMeter, const RecenterState& recenter);
	std::array<XRUICanvasCaptureDescriptor, 3> BuildUICaptureDescriptors(float worldUnitsPerMeter);

	class UIInputConnector
	{
	public:
		void Update(const AdaptedInputSnapshot& input, XRUISurfaceEngineBinding& binding,
			const vec3& cameraLocation, const Coords& bodyRotation, float worldUnitsPerMeter,
			const RecenterState& recenter);
		void Cancel(XRUISurfaceEngineBinding& binding);
		const std::array<PointerFeedback, XRHandCount>& Feedback() const { return feedback; }

	private:
		std::array<bool, XRHandCount> active = {};
		std::array<PointerFeedback, XRHandCount> feedback = {};
	};
}

extern "C"
{
	uint32_t Surreal_GetWebXRPointerFeedbackSize();
	int Surreal_GetWebXRPointerFeedback(uint32_t hand, void* output, uint32_t outputBytes);
}
