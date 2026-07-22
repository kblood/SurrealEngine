#pragma once

#include "XR/XRUIRuntime.h"

class OpenXRUICompositionSink
{
public:
	virtual ~OpenXRUICompositionSink() = default;
	virtual bool AllocateSurfaceTargets(
		const std::array<XRUICanvasCaptureDescriptor, 4>& descriptors) = 0;
	virtual bool ComposeSurfaceFrame(const XRUICanvasReplayFrame& frame,
		const std::array<XRUIPointerFeedback, XRHandCount>& feedback) = 0;
	virtual void ReleaseSurfaceTargets() = 0;
};

// SDK-free native runtime seam. The OpenXR/Vulkan backend owns only target
// allocation and composition; all surface/input policy remains shared.
class OpenXRUIRuntime
{
public:
	static constexpr XRUISurfaceTargets SurfaceTargets = {
		{ 5 }, // startup HUD
		{ 2 }, // cinematic
		{ 3 }, // loading
		{ 4 }  // menu
	};

	bool Start(XRUISurfaceEngineBinding& binding, OpenXRUICompositionSink& sink,
		float worldUnitsPerMeter);
	void Update(XRUISurfaceEngineBinding& binding, const ViewFamily& views,
		const XRSessionState& session, const XRControllerSnapshot& controllers,
		const std::array<XRUISurfaceRay, XRHandCount>& aimRays,
		const std::array<bool, XRHandCount>& aimRayValid, float missDistance);
	bool Compose(XRUISurfaceEngineBinding& binding, OpenXRUICompositionSink& sink);
	void Stop(XRUISurfaceEngineBinding& binding, OpenXRUICompositionSink& sink);
	bool IsStarted() const { return started; }
	const std::array<XRUIPointerFeedback, XRHandCount>& Feedback() const
	{
		return input.Feedback();
	}

private:
	XRUIInputConnector input;
	bool started = false;
};
