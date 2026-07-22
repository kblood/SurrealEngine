#include "Render/XRUISurfaceRuntimeAdapter.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static void Check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << message << '\n';
		std::exit(1);
	}
}

static bool Near(float a, float b, float epsilon = 0.001f)
{
	return std::abs(a - b) <= epsilon;
}

struct ReplaySink : XRUICanvasReplaySink
{
	bool BeginCanvasCapture(const XRUICanvasReplayItem& item) override
	{
		Begun.push_back(item.Surface.Descriptor.Kind);
		return AcceptCaptures;
	}

	void ReplayCanvas(const XRUICanvasReplayItem& item) override
	{
		Replayed.push_back(item.Surface.Descriptor.Kind);
		Sources.push_back(item.Source);
	}

	void EndCanvasCapture(const XRUICanvasReplayItem& item) override
	{
		Ended.push_back(item.Surface.Descriptor.Kind);
	}

	bool AcceptCaptures = true;
	Array<XRUISurfaceKind> Begun;
	Array<XRUISurfaceKind> Replayed;
	Array<XRUISurfaceKind> Ended;
	Array<XRUICanvasReplaySource> Sources;
};

struct MouseSink : XRUIMousePrimitiveSink
{
	void MoveCursor(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel) override
	{
		Moves.push_back({ source, surface, canvasPixel, false });
	}

	void PressPrimary(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel) override
	{
		Presses.push_back({ source, surface, canvasPixel, false });
	}

	void ReleasePrimary(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel, bool canceled) override
	{
		Releases.push_back({ source, surface, canvasPixel, canceled });
	}

	struct Event
	{
		XRUIPointerSource Source;
		XRUISurfaceKind Surface;
		Pointf Pixel;
		bool Canceled;
	};

	Array<Event> Moves;
	Array<Event> Presses;
	Array<Event> Releases;
};

static XRUISurfaceRuntimeAdapter MakeVisibleMenu(int canvasScale = 1)
{
	XRUISurfaceRuntimeAdapter adapter;
	XRUICanvasCaptureDescriptor menu = CreateXRUICanvasCaptureDescriptor(XRUISurfaceKind::Menu, 1000, 500, canvasScale, { 7 });
	menu.Surface.PhysicalWidth = 2.0f;
	adapter.Configure(menu);
	Check(adapter.Show(XRUISurfaceKind::Menu), "menu failed to show");
	return adapter;
}

static void TestDesktopDefaultIsOptIn()
{
	XRUISurfaceRuntimeAdapter adapter;
	ReplaySink replay;
	MouseSink mouse;
	XRUICanvasReplayFrame frame = adapter.BuildReplayFrame();
	adapter.ReplayVisibleCanvases(frame, replay);
	XRUIPointerUpdateResult result = adapter.UpdateMousePointer(frame, Pointf(10.0f, 20.0f), true, mouse);

	Check(frame.Items.empty(), "default adapter created an XR replay surface");
	Check(replay.Begun.empty() && replay.Replayed.empty() && replay.Ended.empty(), "default adapter intercepted desktop rendering");
	Check(!result.Contact.Hit && result.Events.empty(), "default adapter intercepted the desktop mouse");
	Check(mouse.Moves.empty() && mouse.Presses.empty() && mouse.Releases.empty(), "default adapter invoked mouse primitives");
}

static void TestCaptureDescriptorsAndReplayOrder()
{
	XRUISurfaceRuntimeAdapter adapter;
	for (XRUISurfaceKind kind : { XRUISurfaceKind::Menu, XRUISurfaceKind::Loading, XRUISurfaceKind::Hud, XRUISurfaceKind::Cinematic })
	{
		XRUICanvasCaptureDescriptor capture = CreateXRUICanvasCaptureDescriptor(kind, 1600, 900, 2, { static_cast<uint32_t>(10 + static_cast<int>(kind)) });
		adapter.Configure(capture);
		Check(adapter.Show(kind), "configured canvas failed to show");
	}

	XRUICanvasReplayFrame frame = adapter.BuildReplayFrame();
	Check(frame.Items.size() == 4, "visible canvas was omitted from replay frame");
	Check(frame.Items[0].Surface.Descriptor.Kind == XRUISurfaceKind::Hud, "HUD replay order is incorrect");
	Check(frame.Items[1].Surface.Descriptor.Kind == XRUISurfaceKind::Cinematic, "cinematic replay order is incorrect");
	Check(frame.Items[2].Surface.Descriptor.Kind == XRUISurfaceKind::Loading, "loading replay order is incorrect");
	Check(frame.Items[3].Surface.Descriptor.Kind == XRUISurfaceKind::Menu, "menu must be the final replay item");
	Check(frame.Items[3].CanvasWidth() == 800 && frame.Items[3].CanvasHeight() == 450, "canvas scale produced incorrect logical dimensions");
	Check(frame.Items[3].Target == PresentationTarget{ 13 }, "capture target was not preserved");

	ReplaySink replay;
	adapter.ReplayVisibleCanvases(frame, replay);
	Check(replay.Begun.size() == 4 && replay.Replayed.size() == 4 && replay.Ended.size() == 4, "capture lifecycle was incomplete");
	Check(replay.Replayed.back() == XRUISurfaceKind::Menu, "menu pixels were not replayed last");
	Check(replay.Sources[0] == XRUICanvasReplaySource::PlayerAndConsolePostRender, "HUD used the wrong existing canvas source");
	Check(replay.Sources[1] == XRUICanvasReplaySource::CinematicFrame, "cinematic used the wrong existing canvas source");
	Check(replay.Sources[2] == XRUICanvasReplaySource::LoadingFrame, "loading used the wrong existing canvas source");
	Check(replay.Sources[3] == XRUICanvasReplaySource::PlayerAndConsolePostRender, "menu used the wrong existing canvas source");

	ReplaySink rejected;
	rejected.AcceptCaptures = false;
	adapter.ReplayVisibleCanvases(frame, rejected);
	Check(rejected.Begun.size() == 4 && rejected.Replayed.empty() && rejected.Ended.empty(), "failed capture replayed or ended a canvas");
}

static void TestRayMappingUsesCanvasCoordinates()
{
	XRUISurfaceRuntimeAdapter adapter = MakeVisibleMenu(2);
	XRUICanvasReplayFrame frame = adapter.BuildReplayFrame();
	MouseSink mouse;
	XRUIPointerSource hand = XRUIPointerSource::Tracked(41);

	XRUIPointerUpdateResult hover = adapter.UpdateRayPointer(frame, hand, { vec3(0.0f), vec3(1.0f, 0.0f, 0.0f) }, false, mouse);
	Check(hover.Contact.Hit, "center aim ray missed the menu canvas");
	Check(mouse.Moves.size() == 1, "aim ray did not move the legacy cursor");
	Check(Near(mouse.Moves[0].Pixel.x, 250.0f) && Near(mouse.Moves[0].Pixel.y, 125.0f), "surface pixels were not converted to logical canvas coordinates");

	adapter.UpdateRayPointer(frame, hand, { vec3(0.0f), vec3(1.0f, 0.0f, 0.0f) }, true, mouse);
	adapter.UpdateRayPointer(frame, hand, { vec3(0.0f), vec3(1.0f, 0.0f, 0.0f) }, false, mouse);
	Check(mouse.Presses.size() == 1 && mouse.Releases.size() == 1, "tracked click did not reach the existing mouse primitives");
	Check(!mouse.Releases[0].Canceled, "valid tracked click was canceled");
}

static void TestSourceIsolationAndMouseFallback()
{
	XRUISurfaceRuntimeAdapter adapter = MakeVisibleMenu();
	XRUICanvasReplayFrame frame = adapter.BuildReplayFrame();
	MouseSink mouse;
	XRUISurfaceRay center = { vec3(0.0f), vec3(1.0f, 0.0f, 0.0f) };
	XRUIPointerSource left = XRUIPointerSource::Tracked(1);
	XRUIPointerSource right = XRUIPointerSource::Tracked(2);

	adapter.UpdateRayPointer(frame, left, center, false, mouse);
	adapter.UpdateRayPointer(frame, left, center, true, mouse);
	adapter.UpdateRayPointer(frame, right, center, false, mouse);
	adapter.UpdateRayPointer(frame, right, center, true, mouse);
	adapter.UpdateRayPointer(frame, right, center, false, mouse);
	Check(mouse.Presses.size() == 1 && mouse.Presses[0].Source == left, "second pointer stole the legacy button capture");
	Check(mouse.Releases.empty(), "second pointer released another source's button capture");

	adapter.UpdateRayPointer(frame, left, center, false, mouse);
	Check(mouse.Releases.size() == 1 && mouse.Releases[0].Source == left, "capture owner did not release its own button");

	adapter.UpdateMousePointer(frame, Pointf(250.0f, 125.0f), false, mouse);
	adapter.UpdateMousePointer(frame, Pointf(250.0f, 125.0f), true, mouse);
	adapter.UpdateMousePointer(frame, Pointf(250.0f, 125.0f), false, mouse);
	Check(mouse.Presses.size() == 2 && mouse.Presses[1].Source == XRUIPointerSource::Mouse(), "desktop mouse fallback could not press after tracked input");
	Check(mouse.Releases.size() == 2 && mouse.Releases[1].Source == XRUIPointerSource::Mouse(), "desktop mouse fallback could not release after tracked input");
}

static void TestPointerCancelReleasesOnlyItsCapture()
{
	XRUISurfaceRuntimeAdapter adapter = MakeVisibleMenu();
	XRUICanvasReplayFrame frame = adapter.BuildReplayFrame();
	MouseSink mouse;
	XRUIPointerSource hand = XRUIPointerSource::Tracked(9);
	XRUISurfaceRay center = { vec3(0.0f), vec3(1.0f, 0.0f, 0.0f) };

	adapter.UpdateRayPointer(frame, hand, center, false, mouse);
	adapter.UpdateRayPointer(frame, hand, center, true, mouse);
	adapter.CancelPointer(XRUIPointerSource::Tracked(10), mouse);
	Check(mouse.Releases.empty(), "unrelated disconnect released the active pointer");
	adapter.CancelPointer(hand, mouse);
	Check(mouse.Releases.size() == 1 && mouse.Releases[0].Canceled, "captured pointer disconnect did not cancel the legacy press");
}

int main()
{
	TestDesktopDefaultIsOptIn();
	TestCaptureDescriptorsAndReplayOrder();
	TestRayMappingUsesCanvasCoordinates();
	TestSourceIsolationAndMouseFallback();
	TestPointerCancelReleasesOnlyItsCapture();
	return 0;
}
