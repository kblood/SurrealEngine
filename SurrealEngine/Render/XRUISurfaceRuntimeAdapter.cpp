#include "Render/XRUISurfaceRuntimeAdapter.h"

#include <algorithm>

bool XRUICanvasCaptureDescriptor::HasValidCanvas() const
{
	return Surface.HasValidExtent() && CanvasScale > 0;
}

Pointf XRUICanvasCaptureDescriptor::ToCanvasPixel(const Pointf& surfacePixel) const
{
	float scale = static_cast<float>(std::max(CanvasScale, 1));
	return Pointf(surfacePixel.x / scale, surfacePixel.y / scale);
}

XRUICanvasCaptureDescriptor CreateXRUICanvasCaptureDescriptor(XRUISurfaceKind kind, int pixelWidth, int pixelHeight, int canvasScale, PresentationTarget target)
{
	XRUICanvasCaptureDescriptor descriptor;
	descriptor.Surface = CreateXRUISurfaceDescriptor(kind, pixelWidth, pixelHeight);
	descriptor.Target = target;
	descriptor.CanvasScale = canvasScale;
	if (kind == XRUISurfaceKind::Cinematic)
		descriptor.Source = XRUICanvasReplaySource::CinematicFrame;
	else if (kind == XRUISurfaceKind::Loading)
		descriptor.Source = XRUICanvasReplaySource::LoadingFrame;
	return descriptor;
}

int XRUICanvasReplayItem::CanvasWidth() const
{
	return Surface.Descriptor.PixelWidth / std::max(CanvasScale, 1);
}

int XRUICanvasReplayItem::CanvasHeight() const
{
	return Surface.Descriptor.PixelHeight / std::max(CanvasScale, 1);
}

Pointf XRUICanvasReplayItem::ToCanvasPixel(const Pointf& surfacePixel) const
{
	float scale = static_cast<float>(std::max(CanvasScale, 1));
	return Pointf(surfacePixel.x / scale, surfacePixel.y / scale);
}

void XRUISurfaceRuntimeAdapter::Configure(const XRUICanvasCaptureDescriptor& descriptor)
{
	for (XRUICanvasCaptureDescriptor& capture : Captures)
	{
		if (capture.Surface.Kind == descriptor.Surface.Kind)
		{
			capture = descriptor;
			SurfacePolicy.Configure(descriptor.Surface);
			return;
		}
	}
	Captures.push_back(descriptor);
	SurfacePolicy.Configure(descriptor.Surface);
}

bool XRUISurfaceRuntimeAdapter::Show(XRUISurfaceKind kind, const XRUIViewerPose& viewerPose)
{
	return SurfacePolicy.Show(kind, viewerPose);
}

void XRUISurfaceRuntimeAdapter::Hide(XRUISurfaceKind kind)
{
	SurfacePolicy.Hide(kind);
}

bool XRUISurfaceRuntimeAdapter::Recenter(XRUISurfaceKind kind, const XRUIViewerPose& viewerPose)
{
	return SurfacePolicy.Recenter(kind, viewerPose);
}

bool XRUISurfaceRuntimeAdapter::IsVisible(XRUISurfaceKind kind) const
{
	return SurfacePolicy.IsVisible(kind);
}

XRUICanvasReplayFrame XRUISurfaceRuntimeAdapter::BuildReplayFrame() const
{
	XRUICanvasReplayFrame replayFrame;
	XRUISurfaceFrame surfaceFrame = SurfacePolicy.BuildFrame();
	for (const XRUISurfaceFrameItem& surface : surfaceFrame.Surfaces)
	{
		const XRUICanvasCaptureDescriptor* capture = FindCapture(surface.Descriptor.Kind);
		if (!capture || !capture->HasValidCanvas())
			continue;
		replayFrame.Items.push_back({ surface, capture->Source, capture->Target, capture->CanvasScale });
	}
	return replayFrame;
}

void XRUISurfaceRuntimeAdapter::ReplayVisibleCanvases(const XRUICanvasReplayFrame& frame, XRUICanvasReplaySink& sink) const
{
	for (const XRUICanvasReplayItem& item : frame.Items)
	{
		if (!sink.BeginCanvasCapture(item))
			continue;
		sink.ReplayCanvas(item);
		sink.EndCanvasCapture(item);
	}
}

XRUIPointerUpdateResult XRUISurfaceRuntimeAdapter::UpdateRayPointer(const XRUICanvasReplayFrame& frame, const XRUIPointerSource& source, const XRUISurfaceRay& ray, bool primaryPressed, XRUIMousePrimitiveSink& sink)
{
	XRUIPointerUpdateResult result;
	result.Contact = HitTestRay(frame, ray);
	result.Events = InputRouter.Update(source, result.Contact, primaryPressed);
	DispatchPointerEvents(result.Events, sink);
	return result;
}

XRUIPointerUpdateResult XRUISurfaceRuntimeAdapter::UpdateMousePointer(const XRUICanvasReplayFrame& frame, const Pointf& surfacePixel, bool primaryPressed, XRUIMousePrimitiveSink& sink)
{
	XRUIPointerUpdateResult result;
	result.Contact = HitTestMouse(frame, surfacePixel);
	result.Events = InputRouter.Update(XRUIPointerSource::Mouse(), result.Contact, primaryPressed);
	DispatchPointerEvents(result.Events, sink);
	return result;
}

Array<XRUIPointerEvent> XRUISurfaceRuntimeAdapter::CancelPointer(const XRUIPointerSource& source, XRUIMousePrimitiveSink& sink)
{
	Array<XRUIPointerEvent> events = InputRouter.Cancel(source);
	DispatchPointerEvents(events, sink);
	return events;
}

const XRUICanvasCaptureDescriptor* XRUISurfaceRuntimeAdapter::FindCapture(XRUISurfaceKind kind) const
{
	for (const XRUICanvasCaptureDescriptor& capture : Captures)
	{
		if (capture.Surface.Kind == kind)
			return &capture;
	}
	return nullptr;
}

XRUISurfaceContact XRUISurfaceRuntimeAdapter::HitTestRay(const XRUICanvasReplayFrame& frame, const XRUISurfaceRay& ray) const
{
	for (auto it = frame.Items.rbegin(); it != frame.Items.rend(); ++it)
	{
		if (!it->Surface.Descriptor.Interactive)
			continue;
		XRUISurfaceContact contact = MapRayToXRUISurface(it->Surface, ray);
		if (contact.Hit)
			return contact;
	}
	return {};
}

XRUISurfaceContact XRUISurfaceRuntimeAdapter::HitTestMouse(const XRUICanvasReplayFrame& frame, const Pointf& surfacePixel) const
{
	for (auto it = frame.Items.rbegin(); it != frame.Items.rend(); ++it)
	{
		XRUISurfaceContact contact = MapMouseToXRUISurface(it->Surface, surfacePixel);
		if (contact.Hit)
			return contact;
	}
	return {};
}

void XRUISurfaceRuntimeAdapter::DispatchPointerEvents(const Array<XRUIPointerEvent>& events, XRUIMousePrimitiveSink& sink)
{
	for (const XRUIPointerEvent& event : events)
	{
		const XRUICanvasCaptureDescriptor* capture = FindCapture(event.Surface);
		if (!capture)
			continue;
		Pointf canvasPixel = capture->ToCanvasPixel(event.Pixel);

		switch (event.Type)
		{
		case XRUIPointerEventType::Move:
			if (!HasPrimitiveCapture || PrimitiveCaptureSource == event.Source)
				sink.MoveCursor(event.Source, event.Surface, canvasPixel);
			break;
		case XRUIPointerEventType::PrimaryDown:
			if (!HasPrimitiveCapture)
			{
				PrimitiveCaptureSource = event.Source;
				HasPrimitiveCapture = true;
				sink.PressPrimary(event.Source, event.Surface, canvasPixel);
			}
			break;
		case XRUIPointerEventType::PrimaryUp:
			if (HasPrimitiveCapture && PrimitiveCaptureSource == event.Source)
			{
				sink.ReleasePrimary(event.Source, event.Surface, canvasPixel, false);
				HasPrimitiveCapture = false;
			}
			break;
		case XRUIPointerEventType::PrimaryCancel:
			if (HasPrimitiveCapture && PrimitiveCaptureSource == event.Source)
			{
				sink.ReleasePrimary(event.Source, event.Surface, canvasPixel, true);
				HasPrimitiveCapture = false;
			}
			break;
		case XRUIPointerEventType::HoverEnter:
		case XRUIPointerEventType::HoverLeave:
		case XRUIPointerEventType::PrimaryClick:
			break;
		}
	}
}
