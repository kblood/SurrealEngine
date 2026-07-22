#pragma once

#include "Render/XRUISurfaceRuntimeAdapter.h"

#include <array>

enum class XRUICanvasReplayContext
{
	Game,
	Cinematic
};

class XRUISurfaceEngineHost
{
public:
	virtual ~XRUISurfaceEngineHost() = default;
	virtual bool BeginXRUICanvasCapture(const XRUICanvasReplayItem& item) = 0;
	virtual void ReplayXRUICanvas(const XRUICanvasReplayItem& item) = 0;
	virtual void EndXRUICanvasCapture(const XRUICanvasReplayItem& item) = 0;
	virtual void MoveXRUICursor(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel) = 0;
	virtual void PressXRUIPrimary(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel) = 0;
	virtual void ReleaseXRUIPrimary(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel, bool canceled) = 0;
	virtual void EndXRUIPointerSession() = 0;
};

// Connects surface policy to the engine's existing canvas and mouse entry
// points. Graphics-resource ownership and compositor submission stay outside.
class XRUISurfaceEngineBinding final : private XRUICanvasReplaySink, private XRUIMousePrimitiveSink
{
public:
	explicit XRUISurfaceEngineBinding(XRUISurfaceEngineHost& host);

	void Configure(const XRUICanvasCaptureDescriptor& descriptor);
	void SetViewerPose(const XRUIViewerPose& viewerPose);
	void ClearViewerPose();
	void SetSurfaceActive(XRUISurfaceKind kind, bool active);
	void SetMenuActive(bool active) { SetSurfaceActive(XRUISurfaceKind::Menu, active); }
	void SetCinematicActive(bool active) { SetSurfaceActive(XRUISurfaceKind::Cinematic, active); }
	bool IsSurfaceVisible(XRUISurfaceKind kind) const;
	bool Recenter(XRUISurfaceKind kind);

	XRUICanvasReplayFrame BuildReplayFrame() const;
	void Replay(XRUICanvasReplayContext context);

	XRUIPointerUpdateResult UpdateRayPointer(const XRUIPointerSource& source, const XRUISurfaceRay& ray, bool primaryPressed);
	XRUIPointerUpdateResult UpdateMousePointer(const Pointf& surfacePixel, bool primaryPressed);
	Array<XRUIPointerEvent> CancelPointer(const XRUIPointerSource& source);
	void CancelAllPointers();

private:
	struct PendingButton
	{
		XRUIPointerSource Source;
		XRUISurfaceKind Surface = XRUISurfaceKind::Menu;
		Pointf Pixel;
		bool Pressed = false;
		bool Canceled = false;
	};

	bool BeginCanvasCapture(const XRUICanvasReplayItem& item) override;
	void ReplayCanvas(const XRUICanvasReplayItem& item) override;
	void EndCanvasCapture(const XRUICanvasReplayItem& item) override;
	void MoveCursor(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel) override;
	void PressPrimary(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel) override;
	void ReleasePrimary(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel, bool canceled) override;

	void RegisterPointer(const XRUIPointerSource& source);
	void FlushPendingButtons(bool captureAvailable);
	bool Accepts(XRUICanvasReplaySource source) const;
	static size_t SurfaceIndex(XRUISurfaceKind kind);

	XRUISurfaceEngineHost& Host;
	XRUISurfaceRuntimeAdapter Runtime;
	XRUIViewerPose ViewerPose;
	bool HasViewerPose = false;
	std::array<bool, 4> DesiredVisibility = {};
	Array<XRUIPointerSource> KnownPointers;
	Array<PendingButton> PendingButtons;
	XRUICanvasReplayContext ReplayContext = XRUICanvasReplayContext::Game;
	bool LegacyButtonDown = false;
};
