#pragma once

#include "Platform/WebXR/WebXRFrameBridge.h"
#include "Platform/WebXR/WebXRInputAdapter.h"
#include "XR/XRUIRuntime.h"
#include "XR/XRUIVisuals.h"

#include <array>

namespace WebXR
{
	constexpr PresentationTarget CinematicSurfaceTarget = { 2 };
	constexpr PresentationTarget LoadingSurfaceTarget = { 3 };
	constexpr PresentationTarget MenuSurfaceTarget = { 4 };
	constexpr PresentationTarget HudSurfaceTarget = { 5 };
	constexpr int ControllerVisualCompositionOrder = XRUIControllerVisualCompositionOrder;
	constexpr int HitMarkerCompositionOrder = XRUIHitMarkerCompositionOrder;

	using PointerFeedback = XRUIPointerFeedback;

#pragma pack(push, 1)
	struct PackedPointerFeedback
	{
		uint32_t Active = 0;
		uint32_t Selecting = 0;
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
	static_assert(sizeof(PackedPointerFeedback) == 76);

	using UIVisualSettings = XRUIVisualSettings;
	using UIVisualVertex = XRUIVisualVertex;
	using UIHandVisual = XRUIHandVisual;
	using UIVisualFrame = XRUIVisualFrame;

	XRUIViewerPose BuildUIViewerPose(const ViewFamily& family);
	XRUISurfaceRay BuildUIRay(const XRPose& pose, const vec3& cameraLocation,
		const Coords& bodyRotation, float worldUnitsPerMeter, const RecenterState& recenter);
	std::array<XRUICanvasCaptureDescriptor, 4> BuildUICaptureDescriptors(float worldUnitsPerMeter);
	XRUICanvasReplayFrame OrientUIReplayFrame(XRUICanvasReplayFrame frame);
	UIVisualFrame BuildUIVisualFrame(const std::array<PointerFeedback, XRHandCount>& feedback,
		const XRUICanvasReplayFrame& replayFrame, float worldUnitsPerMeter,
		const UIVisualSettings& settings = {});

	class UIInputConnector
	{
	public:
		void Update(const AdaptedInputSnapshot& input, XRUISurfaceEngineBinding& binding,
			const vec3& cameraLocation, const Coords& bodyRotation, float worldUnitsPerMeter,
			const RecenterState& recenter);
		void Cancel(XRUISurfaceEngineBinding& binding);
		const std::array<PointerFeedback, XRHandCount>& Feedback() const { return connector.Feedback(); }

	private:
		XRUIInputConnector connector;
	};
}

extern "C"
{
	uint32_t Surreal_GetWebXRPointerFeedbackSize();
	int Surreal_GetWebXRPointerFeedback(uint32_t hand, void* output, uint32_t outputBytes);
}
