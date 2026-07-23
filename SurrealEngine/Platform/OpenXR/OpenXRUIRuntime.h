#pragma once

#include "Platform/OpenXR/OpenXRView.h"
#include "XR/XRUIRuntime.h"
#include "XR/XRUIVisuals.h"

// The native Vulkan canvas is already in the orientation expected by an
// OpenXR quad. A second U-axis reflection mirrors all menu text and controls.
inline constexpr bool OpenXRUICanvasPresentationFlipHorizontal = false;
inline constexpr bool OpenXRUIHitTestReflectHorizontal = true;

// OpenXR quad texels run opposite the engine surface's canonical Right basis
// after the handedness conversion. Reflect only the native hit-test basis so
// the pointer addresses the texel the user sees without mirroring the image.
XRUICanvasReplayFrame BuildOpenXRUIHitTestFrame(
	const XRUICanvasReplayFrame& presentationFrame);

class OpenXRUICompositionSink
{
public:
	virtual ~OpenXRUICompositionSink() = default;
	virtual bool AllocateSurfaceTargets(
		const std::array<XRUICanvasCaptureDescriptor, 4>& descriptors) = 0;
	// Begin acquires and binds every visible target before canvas replay. End is
	// called after the render device has submitted that replay, and must always
	// unwind bindings/acquisitions even when rendered is false.
	virtual bool BeginSurfaceFrame(const XRUICanvasReplayFrame& frame,
		const std::array<XRUIPointerFeedback, XRHandCount>& feedback,
		const OpenXRUICompositionSpace& compositionSpace) = 0;
	virtual bool EndSurfaceFrame(bool rendered) = 0;
	virtual void ReleaseSurfaceTargets() = 0;
};

enum class OpenXRUICompositionLayerKind
{
	WorldProjection,
	SurfaceQuad,
	VisualOverlay
};

// Projection plus four UI quads remains the required five-layer baseline.
// Tracked controller visuals are an optional sixth layer after those quads.
bool SupportsOpenXRUIVisualOverlay(uint32_t maxLayerCount);
Array<OpenXRUICompositionLayerKind> BuildOpenXRUICompositionLayerOrder(
	uint32_t maxLayerCount, uint32_t surfaceQuadCount, bool visualOverlayReady);

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
		const std::array<bool, XRHandCount>& aimRayValid, float missDistance,
		bool includeNonInteractiveVisuals = true);
	bool BeginComposition(XRUISurfaceEngineBinding& binding,
		OpenXRUICompositionSink& sink,
		const OpenXRUICompositionSpace& compositionSpace);
	bool FinishComposition(OpenXRUICompositionSink& sink, bool rendered);
	void Stop(XRUISurfaceEngineBinding& binding, OpenXRUICompositionSink& sink);
	bool IsStarted() const { return started; }
	const std::array<XRUIPointerFeedback, XRHandCount>& Feedback() const
	{
		return input.Feedback();
	}
	const XRUIVisualFrame& VisualFrame() const { return visualFrame; }
	void SetPointerHand(XRHand hand) { visualSettings.PointerHand = hand; }

private:
	XRUIInputConnector input;
	XRUIVisualFrame visualFrame;
	XRUIVisualSettings visualSettings;
	bool started = false;
	bool compositionBegun = false;
};
