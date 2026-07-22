#include "Render/XRUISurfaceEngineBinding.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

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

static const char* KindName(XRUISurfaceKind kind)
{
	switch (kind)
	{
	case XRUISurfaceKind::Hud: return "hud";
	case XRUISurfaceKind::Cinematic: return "cinematic";
	case XRUISurfaceKind::Loading: return "loading";
	case XRUISurfaceKind::Menu: return "menu";
	}
	return "unknown";
}

struct EngineHost : XRUISurfaceEngineHost
{
	bool BeginXRUICanvasCapture(const XRUICanvasReplayItem& item) override
	{
		Events.push_back(std::string("begin:") + KindName(item.Surface.Descriptor.Kind));
		return CaptureAvailable;
	}

	void ReplayXRUICanvas(const XRUICanvasReplayItem& item) override
	{
		Events.push_back(std::string("replay:") + KindName(item.Surface.Descriptor.Kind));
	}

	void EndXRUICanvasCapture(const XRUICanvasReplayItem& item) override
	{
		Events.push_back(std::string("end:") + KindName(item.Surface.Descriptor.Kind));
	}

	void MoveXRUICursor(const XRUIPointerSource& source, XRUISurfaceKind, const Pointf& canvasPixel) override
	{
		Moves.push_back({ source, canvasPixel });
		Events.push_back("move");
	}

	void PressXRUIPrimary(const XRUIPointerSource& source, XRUISurfaceKind, const Pointf&) override
	{
		ButtonSources.push_back(source);
		Events.push_back("press");
	}

	void ReleaseXRUIPrimary(const XRUIPointerSource& source, XRUISurfaceKind, const Pointf&, bool canceled) override
	{
		ButtonSources.push_back(source);
		Events.push_back(canceled ? "cancel" : "release");
	}

	void EndXRUIPointerSession() override
	{
		Events.push_back("end-pointer-session");
	}

	struct Move
	{
		XRUIPointerSource Source;
		Pointf Pixel;
	};

	bool CaptureAvailable = true;
	Array<std::string> Events;
	Array<Move> Moves;
	Array<XRUIPointerSource> ButtonSources;
};

static XRUICanvasCaptureDescriptor Capture(XRUISurfaceKind kind, uint32_t slot, int scale = 1)
{
	XRUICanvasCaptureDescriptor capture = CreateXRUICanvasCaptureDescriptor(kind, 1000, 500, scale, { slot });
	capture.Surface.PhysicalWidth = 2.0f;
	return capture;
}

static void TestVisibilityTransitionsKeepAStableAnchor()
{
	EngineHost host;
	XRUISurfaceEngineBinding binding(host);
	binding.Configure(Capture(XRUISurfaceKind::Menu, 7));
	binding.SetMenuActive(true);
	Check(binding.BuildReplayFrame().Items.empty(), "menu became visible before a viewer pose existed");

	XRUIViewerPose first;
	first.Position = vec3(1.0f, 2.0f, 3.0f);
	binding.SetViewerPose(first);
	XRUICanvasReplayFrame initial = binding.BuildReplayFrame();
	Check(initial.Items.size() == 1, "active menu did not appear when a viewer pose arrived");
	Check(Near(initial.Items[0].Surface.Pose.Center.x, 2.5f), "menu used the wrong initial anchor");

	XRUIViewerPose moved;
	moved.Position = vec3(20.0f, 30.0f, 40.0f);
	binding.SetViewerPose(moved);
	Check(Near(binding.BuildReplayFrame().Items[0].Surface.Pose.Center.x, 2.5f), "live viewer updates made the menu head-locked");

	binding.SetMenuActive(false);
	Check(binding.BuildReplayFrame().Items.empty(), "menu remained visible after its close transition");
	binding.SetMenuActive(true);
	Check(Near(binding.BuildReplayFrame().Items[0].Surface.Pose.Center.x, 21.5f), "menu reopen did not take a fresh viewer anchor");
	binding.ClearViewerPose();
	Check(binding.BuildReplayFrame().Items.empty(), "viewer loss left a surface visible");
}

static void TestPointerDeliveryWaitsForMenuReplay()
{
	EngineHost host;
	XRUISurfaceEngineBinding binding(host);
	binding.Configure(Capture(XRUISurfaceKind::Menu, 7, 2));
	binding.SetViewerPose({});
	binding.SetMenuActive(true);
	XRUIPointerSource hand = XRUIPointerSource::Tracked(4);
	XRUISurfaceRay center = { vec3(0.0f), vec3(1.0f, 0.0f, 0.0f) };

	binding.UpdateRayPointer(hand, center, false);
	binding.UpdateRayPointer(hand, center, true);
	Check(host.Moves.size() == 2, "exact pointer contact did not update the existing cursor");
	Check(Near(host.Moves.back().Pixel.x, 250.0f) && Near(host.Moves.back().Pixel.y, 125.0f), "pointer contact did not use logical canvas coordinates");
	Check(host.ButtonSources.empty(), "button press ran before UWindow replay updated its cursor");

	host.Events.clear();
	binding.Replay(XRUICanvasReplayContext::Game);
	Check(host.Events.size() == 4, "menu capture lifecycle or delayed press was incomplete");
	Check(host.Events[0] == "begin:menu" && host.Events[1] == "replay:menu" && host.Events[2] == "press" && host.Events[3] == "end:menu", "button press did not follow menu replay deterministically");

	binding.UpdateRayPointer(hand, center, false);
	host.Events.clear();
	binding.Replay(XRUICanvasReplayContext::Game);
	Check(host.Events[2] == "release", "button release did not follow menu replay");
}

static void TestTopmostReplayContexts()
{
	EngineHost host;
	XRUISurfaceEngineBinding binding(host);
	for (XRUISurfaceKind kind : { XRUISurfaceKind::Menu, XRUISurfaceKind::Cinematic, XRUISurfaceKind::Loading })
	{
		binding.Configure(Capture(kind, 10 + static_cast<uint32_t>(kind)));
		binding.SetSurfaceActive(kind, true);
	}
	binding.SetViewerPose({});

	host.Events.clear();
	binding.Replay(XRUICanvasReplayContext::Game);
	Check(host.Events.size() == 6, "game replay included the cinematic or omitted a UI surface");
	Check(host.Events[0] == "begin:loading" && host.Events[3] == "begin:menu", "menu was not the final game UI capture");

	host.Events.clear();
	binding.Replay(XRUICanvasReplayContext::Cinematic);
	Check(host.Events.size() == 3 && host.Events[0] == "begin:cinematic" && host.Events[1] == "replay:cinematic" && host.Events[2] == "end:cinematic", "cinematic replay did not isolate the video canvas");
}

static void TestStartupHudHandsOffToMenu()
{
	EngineHost host;
	XRUISurfaceEngineBinding binding(host);
	binding.Configure(Capture(XRUISurfaceKind::Hud, 5));
	binding.Configure(Capture(XRUISurfaceKind::Menu, 7));
	binding.SetViewerPose({});
	binding.SetHudActive(true);
	binding.SetMenuActive(false);

	XRUICanvasReplayFrame intro = binding.BuildReplayFrame();
	Check(intro.Items.size() == 1 && intro.Items[0].Surface.Descriptor.Kind == XRUISurfaceKind::Hud,
		"startup prompt did not use the non-menu HUD surface");
	Check(!intro.Items[0].Surface.Descriptor.Interactive,
		"startup prompt unexpectedly captured the menu pointer");

	binding.SetHudActive(false);
	binding.SetMenuActive(true);
	XRUICanvasReplayFrame menu = binding.BuildReplayFrame();
	Check(menu.Items.size() == 1 && menu.Items[0].Surface.Descriptor.Kind == XRUISurfaceKind::Menu,
		"menu did not replace the startup prompt surface");
	Check(menu.Items[0].Surface.Descriptor.Interactive,
		"menu replacement did not accept tracked pointer input");
}

static void TestSourceIsolationAndMouseFallback()
{
	EngineHost host;
	XRUISurfaceEngineBinding binding(host);
	binding.Configure(Capture(XRUISurfaceKind::Menu, 7));
	binding.SetViewerPose({});
	binding.SetMenuActive(true);
	XRUISurfaceRay center = { vec3(0.0f), vec3(1.0f, 0.0f, 0.0f) };
	XRUIPointerSource left = XRUIPointerSource::Tracked(1);
	XRUIPointerSource right = XRUIPointerSource::Tracked(2);

	binding.UpdateRayPointer(left, center, false);
	binding.UpdateRayPointer(left, center, true);
	binding.UpdateRayPointer(right, center, false);
	binding.UpdateRayPointer(right, center, true);
	binding.Replay(XRUICanvasReplayContext::Game);
	Check(host.ButtonSources.size() == 1 && host.ButtonSources[0] == left, "second tracked source stole the menu press");

	binding.UpdateRayPointer(right, center, false);
	binding.Replay(XRUICanvasReplayContext::Game);
	Check(host.ButtonSources.size() == 1, "second tracked source released another source's press");
	binding.UpdateRayPointer(left, center, false);
	binding.Replay(XRUICanvasReplayContext::Game);
	Check(host.ButtonSources.size() == 2 && host.ButtonSources[1] == left, "capturing tracked source did not release itself");

	binding.UpdateMousePointer(Pointf(500.0f, 250.0f), false);
	binding.UpdateMousePointer(Pointf(500.0f, 250.0f), true);
	binding.Replay(XRUICanvasReplayContext::Game);
	binding.UpdateMousePointer(Pointf(500.0f, 250.0f), false);
	binding.Replay(XRUICanvasReplayContext::Game);
	Check(host.ButtonSources.size() == 4 && host.ButtonSources[2] == XRUIPointerSource::Mouse() && host.ButtonSources[3] == XRUIPointerSource::Mouse(), "desktop mouse fallback failed after tracked input");
}

static void TestUnavailableCaptureDropsPressAndCancelsHeldInput()
{
	EngineHost host;
	XRUISurfaceEngineBinding binding(host);
	binding.Configure(Capture(XRUISurfaceKind::Menu, 7));
	binding.SetViewerPose({});
	binding.SetMenuActive(true);
	XRUIPointerSource hand = XRUIPointerSource::Tracked(3);
	XRUISurfaceRay center = { vec3(0.0f), vec3(1.0f, 0.0f, 0.0f) };

	binding.UpdateRayPointer(hand, center, false);
	binding.UpdateRayPointer(hand, center, true);
	host.CaptureAvailable = false;
	binding.Replay(XRUICanvasReplayContext::Game);
	Check(host.ButtonSources.empty(), "press reached a menu whose capture target was unavailable");

	host.CaptureAvailable = true;
	binding.UpdateRayPointer(hand, center, false);
	binding.UpdateRayPointer(hand, center, true);
	binding.Replay(XRUICanvasReplayContext::Game);
	Check(host.ButtonSources.size() == 1, "fresh press did not recover after capture became available");
	binding.SetMenuActive(false);
	Check(host.Events[host.Events.size() - 2] == "cancel" && host.Events.back() == "end-pointer-session", "menu close did not cancel input and restore the pointer session");
}

int main()
{
	TestVisibilityTransitionsKeepAStableAnchor();
	TestPointerDeliveryWaitsForMenuReplay();
	TestTopmostReplayContexts();
	TestStartupHudHandsOffToMenu();
	TestSourceIsolationAndMouseFallback();
	TestUnavailableCaptureDropsPressAndCancelsHeldInput();
	return 0;
}
