#include "Platform/OpenXR/OpenXRUIRuntime.h"

XRUICanvasReplayFrame BuildOpenXRUIHitTestFrame(
	const XRUICanvasReplayFrame& presentationFrame)
{
	XRUICanvasReplayFrame result = presentationFrame;
	if (OpenXRUIHitTestReflectHorizontal)
	{
		for (XRUICanvasReplayItem& item : result.Items)
			item.Surface.Pose.Right = -item.Surface.Pose.Right;
	}
	return result;
}

bool SupportsOpenXRUIVisualOverlay(uint32_t maxLayerCount)
{
	return maxLayerCount >= 6;
}

Array<OpenXRUICompositionLayerKind> BuildOpenXRUICompositionLayerOrder(
	uint32_t maxLayerCount, uint32_t surfaceQuadCount, bool visualOverlayReady)
{
	Array<OpenXRUICompositionLayerKind> order;
	if (maxLayerCount < surfaceQuadCount + 1)
		return order;
	order.push_back(OpenXRUICompositionLayerKind::WorldProjection);
	for (uint32_t index = 0; index < surfaceQuadCount; index++)
		order.push_back(OpenXRUICompositionLayerKind::SurfaceQuad);
	if (visualOverlayReady && SupportsOpenXRUIVisualOverlay(maxLayerCount) &&
		order.size() < maxLayerCount)
		order.push_back(OpenXRUICompositionLayerKind::VisualOverlay);
	return order;
}

bool OpenXRUIRuntime::Start(XRUISurfaceEngineBinding& binding,
	OpenXRUICompositionSink& sink, float worldUnitsPerMeter)
{
	if (started)
		return true;
	const auto descriptors = BuildXRUICaptureDescriptors(worldUnitsPerMeter, SurfaceTargets);
	if (!sink.AllocateSurfaceTargets(descriptors))
		return false;
	for (const XRUICanvasCaptureDescriptor& descriptor : descriptors)
		binding.Configure(descriptor);
	started = true;
	return true;
}

void OpenXRUIRuntime::Update(XRUISurfaceEngineBinding& binding,
	const ViewFamily& views, const XRSessionState& session,
	const XRControllerSnapshot& controllers,
	const std::array<XRUISurfaceRay, XRHandCount>& aimRays,
	const std::array<bool, XRHandCount>& aimRayValid, float missDistance)
{
	if (!started)
		return;
	binding.SetViewerPose(BuildXRUIViewerPose(views));
	XRUIInputFrame frame;
	frame.Session = session;
	frame.Controllers = controllers;
	frame.AimRays = aimRays;
	frame.AimRayValid = aimRayValid;
	frame.MissDistance = missDistance;
	const XRUICanvasReplayFrame presentationFrame = binding.BuildReplayFrame();
	input.Update(frame, binding,
		BuildOpenXRUIHitTestFrame(presentationFrame));
	visualFrame = BuildXRUIVisualFrame(input.Feedback(), presentationFrame,
		missDistance, visualSettings);
}

bool OpenXRUIRuntime::BeginComposition(XRUISurfaceEngineBinding& binding,
	OpenXRUICompositionSink& sink,
	const OpenXRUICompositionSpace& compositionSpace)
{
	if (!started || compositionBegun)
		return false;
	compositionBegun = sink.BeginSurfaceFrame(binding.BuildReplayFrame(),
		input.Feedback(), compositionSpace);
	return compositionBegun;
}

bool OpenXRUIRuntime::FinishComposition(OpenXRUICompositionSink& sink,
	bool rendered)
{
	if (!compositionBegun)
		return false;
	const bool completed = sink.EndSurfaceFrame(rendered);
	compositionBegun = false;
	return completed;
}

void OpenXRUIRuntime::Stop(XRUISurfaceEngineBinding& binding,
	OpenXRUICompositionSink& sink)
{
	if (!started)
		return;
	FinishComposition(sink, false);
	input.Cancel(binding);
	visualFrame = {};
	binding.ClearViewerPose();
	sink.ReleaseSurfaceTargets();
	started = false;
}
