#include "Platform/WebXR/WebXRUIProvider.h"

#include <cmath>

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

std::array<XRUICanvasCaptureDescriptor, 4>
WebXR::BuildDirectUICaptureDescriptors(float worldUnitsPerMeter,
	const PerViewHudPresentation& hud, int pixelWidth, int pixelHeight,
	int canvasScale)
{
	auto descriptors = BuildUICaptureDescriptors(worldUnitsPerMeter);
	if (!hud.Enabled || !std::isfinite(hud.ConvergenceDepth) ||
		!std::isfinite(hud.HalfFovDegrees) || hud.ConvergenceDepth <= 0.0f ||
		hud.HalfFovDegrees <= 0.0f || hud.HalfFovDegrees >= 89.0f ||
		pixelWidth <= 0 || pixelHeight <= 0 || canvasScale <= 0)
		return descriptors;

	XRUICanvasCaptureDescriptor& directMenu = descriptors[3];
	XRUISurfaceDescriptor& menu = directMenu.Surface;
	menu.AnchorMode = XRUISurfaceAnchorMode::HeadRelativeEveryFrame;
	menu.HeadRelativeDistance = hud.ConvergenceDepth;
	menu.PhysicalWidth = 2.0f * hud.ConvergenceDepth *
		std::tan(hud.HalfFovDegrees * 3.14159265359f / 180.0f);
	menu.PixelWidth = pixelWidth;
	menu.PixelHeight = pixelHeight;
	directMenu.CanvasScale = canvasScale;
	return descriptors;
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
	const UIVisualSettings& settings, bool includeNonInteractiveSurfaces)
{
	return ::BuildXRUIVisualFrame(feedback, replayFrame, worldUnitsPerMeter,
		settings, includeNonInteractiveSurfaces);
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
