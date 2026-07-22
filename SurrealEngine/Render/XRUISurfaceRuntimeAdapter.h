#pragma once

#include "Render/XRUISurfaces.h"

enum class XRUICanvasReplaySource
{
	// Existing UPlayerPawn.PostRender followed by UConsole.PostRender.
	PlayerAndConsolePostRender,
	// Existing video/intro frame canvas.
	CinematicFrame,
	// Existing loading-screen canvas.
	LoadingFrame
};

struct XRUICanvasCaptureDescriptor
{
	XRUISurfaceDescriptor Surface;
	XRUICanvasReplaySource Source = XRUICanvasReplaySource::PlayerAndConsolePostRender;
	PresentationTarget Target;
	int CanvasScale = 1;

	bool HasValidCanvas() const;
	Pointf ToCanvasPixel(const Pointf& surfacePixel) const;
};

XRUICanvasCaptureDescriptor CreateXRUICanvasCaptureDescriptor(XRUISurfaceKind kind, int pixelWidth, int pixelHeight, int canvasScale = 1, PresentationTarget target = {});

struct XRUICanvasReplayItem
{
	XRUISurfaceFrameItem Surface;
	XRUICanvasReplaySource Source = XRUICanvasReplaySource::PlayerAndConsolePostRender;
	PresentationTarget Target;
	int CanvasScale = 1;

	int CanvasWidth() const;
	int CanvasHeight() const;
	Pointf ToCanvasPixel(const Pointf& surfacePixel) const;
};

struct XRUICanvasReplayFrame
{
	// Ordered back-to-front. The menu remains the final replay and compositor item.
	Array<XRUICanvasReplayItem> Items;
};

class XRUICanvasReplaySink
{
public:
	virtual ~XRUICanvasReplaySink() = default;
	virtual bool BeginCanvasCapture(const XRUICanvasReplayItem& item) = 0;
	virtual void ReplayCanvas(const XRUICanvasReplayItem& item) = 0;
	virtual void EndCanvasCapture(const XRUICanvasReplayItem& item) = 0;
};

class XRUIMousePrimitiveSink
{
public:
	virtual ~XRUIMousePrimitiveSink() = default;
	virtual void MoveCursor(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel) = 0;
	virtual void PressPrimary(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel) = 0;
	virtual void ReleasePrimary(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel, bool canceled) = 0;
};

struct XRUIPointerUpdateResult
{
	XRUISurfaceContact Contact;
	Array<XRUIPointerEvent> Events;
};

// Owns only XR UI runtime policy. Existing desktop rendering and OS mouse
// dispatch remain unchanged until a caller explicitly builds/replays a frame
// or routes a pointer update through this adapter.
class XRUISurfaceRuntimeAdapter
{
public:
	void Configure(const XRUICanvasCaptureDescriptor& descriptor);
	bool Show(XRUISurfaceKind kind, const XRUIViewerPose& viewerPose = {});
	void Hide(XRUISurfaceKind kind);
	bool Recenter(XRUISurfaceKind kind, const XRUIViewerPose& viewerPose);
	bool IsVisible(XRUISurfaceKind kind) const;

	XRUICanvasReplayFrame BuildReplayFrame() const;
	void ReplayVisibleCanvases(const XRUICanvasReplayFrame& frame, XRUICanvasReplaySink& sink) const;

	XRUIPointerUpdateResult UpdateRayPointer(const XRUICanvasReplayFrame& frame, const XRUIPointerSource& source, const XRUISurfaceRay& ray, bool primaryPressed, XRUIMousePrimitiveSink& sink);
	XRUIPointerUpdateResult UpdateMousePointer(const XRUICanvasReplayFrame& frame, const Pointf& surfacePixel, bool primaryPressed, XRUIMousePrimitiveSink& sink);
	Array<XRUIPointerEvent> CancelPointer(const XRUIPointerSource& source, XRUIMousePrimitiveSink& sink);

private:
	const XRUICanvasCaptureDescriptor* FindCapture(XRUISurfaceKind kind) const;
	XRUISurfaceContact HitTestRay(const XRUICanvasReplayFrame& frame, const XRUISurfaceRay& ray) const;
	XRUISurfaceContact HitTestMouse(const XRUICanvasReplayFrame& frame, const Pointf& surfacePixel) const;
	void DispatchPointerEvents(const Array<XRUIPointerEvent>& events, XRUIMousePrimitiveSink& sink);

	Array<XRUICanvasCaptureDescriptor> Captures;
	XRUISurfaceFramePolicy SurfacePolicy;
	XRUISurfaceInputRouter InputRouter;
	bool HasPrimitiveCapture = false;
	XRUIPointerSource PrimitiveCaptureSource;
};
