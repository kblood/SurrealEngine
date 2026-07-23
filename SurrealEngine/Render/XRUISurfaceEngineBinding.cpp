#include "Render/XRUISurfaceEngineBinding.h"

XRUISurfaceEngineBinding::XRUISurfaceEngineBinding(XRUISurfaceEngineHost& host) : Host(host)
{
}

void XRUISurfaceEngineBinding::Configure(const XRUICanvasCaptureDescriptor& descriptor)
{
	Runtime.Configure(descriptor);
	if (HasViewerPose && DesiredVisibility[SurfaceIndex(descriptor.Surface.Kind)])
		Runtime.Show(descriptor.Surface.Kind, ViewerPose);
}

void XRUISurfaceEngineBinding::SetViewerPose(const XRUIViewerPose& viewerPose)
{
	ViewerPose = viewerPose;
	HasViewerPose = true;
	for (XRUISurfaceKind kind : { XRUISurfaceKind::Hud, XRUISurfaceKind::Cinematic, XRUISurfaceKind::Loading, XRUISurfaceKind::Menu })
	{
		if (DesiredVisibility[SurfaceIndex(kind)] && !Runtime.IsVisible(kind))
			Runtime.Show(kind, ViewerPose);
	}
}

void XRUISurfaceEngineBinding::ClearViewerPose()
{
	CancelAllPointers();
	for (XRUISurfaceKind kind : { XRUISurfaceKind::Hud, XRUISurfaceKind::Cinematic, XRUISurfaceKind::Loading, XRUISurfaceKind::Menu })
		Runtime.Hide(kind);
	HasViewerPose = false;
}

void XRUISurfaceEngineBinding::SetSurfaceActive(XRUISurfaceKind kind, bool active)
{
	size_t index = SurfaceIndex(kind);
	if (DesiredVisibility[index] == active)
		return;
	DesiredVisibility[index] = active;

	if (active)
	{
		if (HasViewerPose)
			Runtime.Show(kind, ViewerPose);
	}
	else
	{
		if (kind == XRUISurfaceKind::Menu)
			CancelAllPointers();
		Runtime.Hide(kind);
	}
}

bool XRUISurfaceEngineBinding::IsSurfaceVisible(XRUISurfaceKind kind) const
{
	return Runtime.IsVisible(kind);
}

bool XRUISurfaceEngineBinding::Recenter(XRUISurfaceKind kind)
{
	return HasViewerPose && Runtime.Recenter(kind, ViewerPose);
}

XRUICanvasReplayFrame XRUISurfaceEngineBinding::BuildReplayFrame() const
{
	return Runtime.BuildReplayFrame();
}

void XRUISurfaceEngineBinding::Replay(XRUICanvasReplayContext context)
{
	ReplayContext = context;
	Runtime.ReplayVisibleCanvases(Runtime.BuildReplayFrame(), *this);
	FlushPendingButtons(false);
}

XRUIPointerUpdateResult XRUISurfaceEngineBinding::UpdateRayPointer(const XRUIPointerSource& source, const XRUISurfaceRay& ray, bool primaryPressed)
{
	return UpdateRayPointer(Runtime.BuildReplayFrame(), source, ray, primaryPressed);
}

XRUIPointerUpdateResult XRUISurfaceEngineBinding::UpdateRayPointer(
	const XRUICanvasReplayFrame& frame, const XRUIPointerSource& source,
	const XRUISurfaceRay& ray, bool primaryPressed)
{
	RegisterPointer(source);
	return Runtime.UpdateRayPointer(frame, source, ray, primaryPressed, *this);
}

XRUIPointerUpdateResult XRUISurfaceEngineBinding::UpdateMousePointer(const Pointf& surfacePixel, bool primaryPressed)
{
	XRUIPointerSource source = XRUIPointerSource::Mouse();
	RegisterPointer(source);
	return Runtime.UpdateMousePointer(Runtime.BuildReplayFrame(), surfacePixel, primaryPressed, *this);
}

Array<XRUIPointerEvent> XRUISurfaceEngineBinding::CancelPointer(const XRUIPointerSource& source)
{
	Array<XRUIPointerEvent> events = Runtime.CancelPointer(source, *this);
	FlushPendingButtons(false);
	for (auto it = KnownPointers.begin(); it != KnownPointers.end(); ++it)
	{
		if (*it == source)
		{
			KnownPointers.erase(it);
			break;
		}
	}
	bool hasTrackedPointer = false;
	for (const XRUIPointerSource& known : KnownPointers)
		hasTrackedPointer = hasTrackedPointer || known.Kind == XRUIPointerSourceKind::Tracked;
	if (!hasTrackedPointer)
		Host.EndXRUIPointerSession();
	return events;
}

void XRUISurfaceEngineBinding::CancelAllPointers()
{
	Array<XRUIPointerSource> pointers = KnownPointers;
	for (const XRUIPointerSource& source : pointers)
		Runtime.CancelPointer(source, *this);
	KnownPointers.clear();
	FlushPendingButtons(false);
	Host.EndXRUIPointerSession();
}

bool XRUISurfaceEngineBinding::BeginCanvasCapture(const XRUICanvasReplayItem& item)
{
	return Accepts(item.Source) && Host.BeginXRUICanvasCapture(item);
}

void XRUISurfaceEngineBinding::ReplayCanvas(const XRUICanvasReplayItem& item)
{
	Host.ReplayXRUICanvas(item);
	if (item.Surface.Descriptor.Interactive)
		FlushPendingButtons(true);
}

void XRUISurfaceEngineBinding::EndCanvasCapture(const XRUICanvasReplayItem& item)
{
	Host.EndXRUICanvasCapture(item);
}

void XRUISurfaceEngineBinding::MoveCursor(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel)
{
	Host.MoveXRUICursor(source, surface, canvasPixel);
}

void XRUISurfaceEngineBinding::PressPrimary(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel)
{
	PendingButtons.push_back({ source, surface, canvasPixel, true, false });
}

void XRUISurfaceEngineBinding::ReleasePrimary(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel, bool canceled)
{
	PendingButtons.push_back({ source, surface, canvasPixel, false, canceled });
}

void XRUISurfaceEngineBinding::RegisterPointer(const XRUIPointerSource& source)
{
	for (const XRUIPointerSource& known : KnownPointers)
	{
		if (known == source)
			return;
	}
	KnownPointers.push_back(source);
}

void XRUISurfaceEngineBinding::FlushPendingButtons(bool captureAvailable)
{
	for (const PendingButton& button : PendingButtons)
	{
		if (button.Pressed)
		{
			if (captureAvailable && !LegacyButtonDown)
			{
				Host.PressXRUIPrimary(button.Source, button.Surface, button.Pixel);
				LegacyButtonDown = true;
			}
		}
		else if (LegacyButtonDown)
		{
			Host.ReleaseXRUIPrimary(button.Source, button.Surface, button.Pixel, button.Canceled || !captureAvailable);
			LegacyButtonDown = false;
		}
	}
	PendingButtons.clear();
}

bool XRUISurfaceEngineBinding::Accepts(XRUICanvasReplaySource source) const
{
	if (ReplayContext == XRUICanvasReplayContext::Cinematic)
		return source == XRUICanvasReplaySource::CinematicFrame;
	return source != XRUICanvasReplaySource::CinematicFrame;
}

size_t XRUISurfaceEngineBinding::SurfaceIndex(XRUISurfaceKind kind)
{
	return static_cast<size_t>(kind);
}
