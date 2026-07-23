#pragma once

#include "Render/ViewFamily.h"
#include "Render/XRUISurfaceEngineBinding.h"
#include "XR/XRCommon.h"
#include "XR/XRHapticFeedbackPolicy.h"

#include <array>

struct XRUISurfaceTargets
{
	PresentationTarget Hud;
	PresentationTarget Cinematic;
	PresentationTarget Loading;
	PresentationTarget Menu;
};

std::array<XRUICanvasCaptureDescriptor, 4> BuildXRUICaptureDescriptors(
	float worldUnitsPerMeter, const XRUISurfaceTargets& targets);
XRUIViewerPose BuildXRUIViewerPose(const ViewFamily& family);

struct XRUIInputFrame
{
	XRSessionState Session;
	XRControllerSnapshot Controllers;
	std::array<XRUISurfaceRay, XRHandCount> AimRays;
	std::array<bool, XRHandCount> AimRayValid = {};
	float MissDistance = 1.0f;
};

struct XRUIPointerFeedback
{
	bool Active = false;
	bool Selecting = false;
	XRHand Hand = XRHand::Right;
	XRUISurfaceRay Ray;
	XRUISurfaceContact Contact;
	vec3 HitPoint = vec3(0.0f);
};

void ResolveXRUIHapticFeedback(XRHapticFeedbackPolicy& policy,
	const std::array<XRUIPointerFeedback, XRHandCount>& feedback,
	IXRHapticSink* sink);

// Makes the engine's synchronous map/save-load boundary authoritative for the
// shared loading surface and guarantees cleanup when a load throws.
class XRUILoadingSurfaceScope
{
public:
	explicit XRUILoadingSurfaceScope(XRUISurfaceEngineBinding* binding);
	~XRUILoadingSurfaceScope();

	XRUILoadingSurfaceScope(const XRUILoadingSurfaceScope&) = delete;
	XRUILoadingSurfaceScope& operator=(const XRUILoadingSurfaceScope&) = delete;

private:
	XRUISurfaceEngineBinding* Binding = nullptr;
};

// Provider-neutral controller-to-UI policy. Providers only translate their
// tracked aim poses into AimRays and supply semantic controller snapshots.
class XRUIInputConnector
{
public:
	void Update(const XRUIInputFrame& input, XRUISurfaceEngineBinding& binding);
	void Update(const XRUIInputFrame& input, XRUISurfaceEngineBinding& binding,
		const XRUICanvasReplayFrame& frame);
	void Cancel(XRUISurfaceEngineBinding& binding);
	const std::array<XRUIPointerFeedback, XRHandCount>& Feedback() const { return feedback; }

private:
	std::array<bool, XRHandCount> active = {};
	std::array<XRUIPointerFeedback, XRHandCount> feedback = {};
};
