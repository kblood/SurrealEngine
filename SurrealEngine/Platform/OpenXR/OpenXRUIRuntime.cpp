#include "Platform/OpenXR/OpenXRUIRuntime.h"

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
	input.Update(frame, binding);
}

bool OpenXRUIRuntime::Compose(XRUISurfaceEngineBinding& binding,
	OpenXRUICompositionSink& sink)
{
	return started && sink.ComposeSurfaceFrame(binding.BuildReplayFrame(), input.Feedback());
}

void OpenXRUIRuntime::Stop(XRUISurfaceEngineBinding& binding,
	OpenXRUICompositionSink& sink)
{
	if (!started)
		return;
	input.Cancel(binding);
	binding.ClearViewerPose();
	sink.ReleaseSurfaceTargets();
	started = false;
}
