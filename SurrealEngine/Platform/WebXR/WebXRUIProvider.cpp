#include "Platform/WebXR/WebXRUIProvider.h"

XRUIViewerPose WebXR::BuildUIViewerPose(const ViewFamily& family)
{
	return BuildXRUIViewerPose(family);
}

XRUISurfaceRay WebXR::BuildUIRay(const XRPose& pose, const vec3& cameraLocation,
	const Coords& bodyRotation, float worldUnitsPerMeter, const RecenterState& recenter)
{
	if (!pose.Valid || !recenter.Valid)
		return {};
	EngineTrackedPose enginePose = TransformCanonicalPose(
		vec3(pose.Position.X, pose.Position.Y, pose.Position.Z),
		vec4(pose.Orientation.X, pose.Orientation.Y, pose.Orientation.Z, pose.Orientation.W),
		cameraLocation, bodyRotation, worldUnitsPerMeter, recenter);
	return { enginePose.Position, normalize(enginePose.Forward) };
}

std::array<XRUICanvasCaptureDescriptor, 4> WebXR::BuildUICaptureDescriptors(float worldUnitsPerMeter)
{
	return ::BuildXRUICaptureDescriptors(worldUnitsPerMeter,
		{ HudSurfaceTarget, CinematicSurfaceTarget, LoadingSurfaceTarget, MenuSurfaceTarget });
}

XRUICanvasReplayFrame WebXR::OrientUIReplayFrame(XRUICanvasReplayFrame frame)
{
	// The shared/OpenXR surface basis deliberately carries the native reflected
	// -Y right axis. WebXR's decoded view projection uses +Y as screen right, so
	// flip only its replay copy. The same copy drives hit testing and composition.
	for (XRUICanvasReplayItem& item : frame.Items)
		item.Surface.Pose.Right = -item.Surface.Pose.Right;
	return frame;
}

WebXR::UIVisualFrame WebXR::BuildUIVisualFrame(
	const std::array<PointerFeedback, XRHandCount>& feedback,
	const XRUICanvasReplayFrame& replayFrame, float worldUnitsPerMeter,
	const UIVisualSettings& settings)
{
	return ::BuildXRUIVisualFrame(feedback, replayFrame, worldUnitsPerMeter, settings);
}

void WebXR::UIInputConnector::Update(const AdaptedInputSnapshot& input,
	XRUISurfaceEngineBinding& binding, const vec3& cameraLocation,
	const Coords& bodyRotation, float worldUnitsPerMeter, const RecenterState& recenter)
{
	XRUIInputFrame frame;
	frame.Session = input.Session;
	frame.Controllers = input.Controllers;
	frame.MissDistance = worldUnitsPerMeter;
	for (size_t handIndex = 0; handIndex < XRHandCount; handIndex++)
	{
		const XRPose& aim = input.Spaces.Aim[handIndex];
		frame.AimRays[handIndex] = BuildUIRay(aim, cameraLocation, bodyRotation,
			worldUnitsPerMeter, recenter);
		frame.AimRayValid[handIndex] = aim.Valid && recenter.Valid;
	}
	connector.Update(frame, binding, OrientUIReplayFrame(binding.BuildReplayFrame()));
}

void WebXR::UIInputConnector::Cancel(XRUISurfaceEngineBinding& binding)
{
	connector.Cancel(binding);
}
