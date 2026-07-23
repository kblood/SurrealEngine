#include "Render/Presentation.h"
#include "Render/XRUISurfaces.h"

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

static bool Near(const vec3& a, const vec3& b, float epsilon = 0.001f)
{
	return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon);
}

static bool HasEvent(const Array<XRUIPointerEvent>& events, XRUIPointerEventType type)
{
	for (const XRUIPointerEvent& event : events)
	{
		if (event.Type == type)
			return true;
	}
	return false;
}

static XRUISurfaceFrameItem MakeMenuSurface()
{
	XRUISurfaceFrameItem surface;
	surface.Descriptor = CreateXRUISurfaceDescriptor(XRUISurfaceKind::Menu, 1000, 500);
	surface.Descriptor.PhysicalWidth = 2.0f;
	surface.Pose.Center = vec3(2.0f, 0.0f, 0.0f);
	surface.Pose.Right = vec3(0.0f, -1.0f, 0.0f);
	surface.Pose.Up = vec3(0.0f, 0.0f, 1.0f);
	surface.Pose.Normal = vec3(-1.0f, 0.0f, 0.0f);
	surface.CompositionOrder = 500;
	return surface;
}

static void TestDefaultDesktopContract()
{
	PresentationPlan plan;
	for (PresentationLayer layer : { PresentationLayer::World, PresentationLayer::WeaponOverlay, PresentationLayer::UserInterface, PresentationLayer::Cinematic })
	{
		PresentationLayerDescription description = plan.GetLayer(layer);
		Check(description.Enabled, "XR UI policy changed default desktop layer visibility");
		Check(description.Target.IsDefault(), "XR UI policy changed a default desktop target");
	}

	XRUISurfaceFramePolicy policy;
	Check(policy.BuildFrame().Surfaces.empty(), "unconfigured XR UI policy must not create desktop-visible surfaces");
}

static void TestVisibilityAndCompositionOrder()
{
	XRUISurfaceFramePolicy policy;
	for (XRUISurfaceKind kind : { XRUISurfaceKind::Menu, XRUISurfaceKind::Loading, XRUISurfaceKind::Hud, XRUISurfaceKind::Cinematic })
	{
		policy.Configure(CreateXRUISurfaceDescriptor(kind, 1600, 900));
		Check(policy.Show(kind), "configured surface was not made visible");
	}

	XRUISurfaceFrame frame = policy.BuildFrame();
	Check(frame.Surfaces.size() == 4, "visible surface was omitted from frame");
	Check(frame.Surfaces[0].Descriptor.Kind == XRUISurfaceKind::Hud, "HUD must be first among UI surfaces");
	Check(frame.Surfaces[1].Descriptor.Kind == XRUISurfaceKind::Cinematic, "cinematic must render after HUD");
	Check(frame.Surfaces[2].Descriptor.Kind == XRUISurfaceKind::Loading, "loading must render after cinematic");
	Check(frame.Surfaces[3].Descriptor.Kind == XRUISurfaceKind::Menu, "menu must be the topmost surface");
	for (const XRUISurfaceFrameItem& item : frame.Surfaces)
	{
		Check(item.CompositionOrder > 100, "UI surface must compose after world and weapon layers");
		Check(item.DepthMode == XRUISurfaceDepthMode::IgnoreWorldDepth, "UI surface must not be hidden by world depth");
	}
	Check(frame.Surfaces[1].Descriptor.ContentLayer == PresentationLayer::Cinematic, "cinematic content source is incorrect");
	Check(frame.Surfaces[3].Descriptor.ContentLayer == PresentationLayer::UserInterface, "menu content source is incorrect");

	policy.Hide(XRUISurfaceKind::Menu);
	Check(!policy.IsVisible(XRUISurfaceKind::Menu), "hidden menu still reports visible");
	Check(policy.BuildFrame().Surfaces.size() == 3, "hidden menu remained in the frame");
}

static void TestGameplayHudContract()
{
	XRUISurfaceVisibility noPlayer = ResolveXRUISurfaceVisibility(false, false);
	Check(!noPlayer.Hud && !noPlayer.Menu, "HUD appeared without a player canvas owner");
	XRUISurfaceVisibility gameplay = ResolveXRUISurfaceVisibility(true, false);
	Check(gameplay.Hud && !gameplay.Menu, "ordinary gameplay did not select the HUD surface");
	XRUISurfaceVisibility menu = ResolveXRUISurfaceVisibility(true, true);
	Check(!menu.Hud && menu.Menu, "menu did not replace the gameplay HUD surface");

	XRUISurfaceDescriptor hud = CreateXRUISurfaceDescriptor(XRUISurfaceKind::Hud, 1024, 768);
	Check(hud.AnchorMode == XRUISurfaceAnchorMode::HeadRelativeEveryFrame,
		"gameplay HUD is not viewer-relative");
	const float halfFovDegrees = std::atan((hud.PhysicalWidth * 0.5f) /
		hud.HeadRelativeDistance) * 180.0f / 3.14159265359f;
	Check(Near(halfFovDegrees, XRGameplayHudHalfFovDegrees, 0.01f),
		"gameplay HUD no longer matches the validated 50-degree width");
	Check(Near(hud.HeadRelativeDistance, XRGameplayHudDistanceMeters),
		"gameplay HUD no longer matches the validated convergence distance");

	XRUISurfaceFramePolicy policy;
	policy.Configure(hud);
	XRUIViewerPose first;
	Check(policy.Show(XRUISurfaceKind::Hud, first), "gameplay HUD failed to show");
	XRUIViewerPose turned;
	turned.Position = vec3(10.0f, 20.0f, 30.0f);
	turned.Forward = vec3(0.0f, 1.0f, 0.0f);
	policy.UpdateViewerPose(turned);
	XRUISurfaceFrame frame = policy.BuildFrame();
	Check(frame.Surfaces.size() == 1, "tracked gameplay HUD disappeared");
	Check(Near(frame.Surfaces[0].Pose.Center,
		vec3(10.0f, 20.0f + XRGameplayHudDistanceMeters, 30.0f)),
		"gameplay HUD did not follow the current viewer pose");
}

static void TestAnchorStabilityAndRecenter()
{
	XRUISurfaceFramePolicy policy;
	XRUISurfaceDescriptor menu = CreateXRUISurfaceDescriptor(XRUISurfaceKind::Menu, 1200, 600);
	menu.HeadRelativeDistance = 2.0f;
	policy.Configure(menu);

	XRUIViewerPose first;
	first.Position = vec3(0.0f);
	first.Forward = vec3(1.0f, 0.0f, 0.0f);
	Check(policy.Show(XRUISurfaceKind::Menu, first), "head-relative menu failed to anchor");
	XRUISurfacePose initial = policy.BuildFrame().Surfaces[0].Pose;
	Check(Near(initial.Center, vec3(2.0f, 0.0f, 0.0f)), "head-relative menu used the wrong initial center");
	policy.Configure(menu);
	Check(Near(policy.BuildFrame().Surfaces[0].Pose.Center, initial.Center), "reconfiguring the same anchor mode discarded a valid anchor");

	XRUIViewerPose moved;
	moved.Position = vec3(10.0f, 20.0f, 30.0f);
	moved.Forward = vec3(0.0f, 1.0f, 0.0f);
	Check(policy.Show(XRUISurfaceKind::Menu, moved), "repeated show failed");
	Check(Near(policy.BuildFrame().Surfaces[0].Pose.Center, initial.Center), "visible menu remained head-locked after initialization");

	Check(policy.Recenter(XRUISurfaceKind::Menu, moved), "explicit recenter failed");
	Check(Near(policy.BuildFrame().Surfaces[0].Pose.Center, vec3(10.0f, 22.0f, 30.0f)), "explicit recenter did not use the current head pose");

	policy.Hide(XRUISurfaceKind::Menu);
	moved.Position = vec3(-4.0f, 2.0f, 1.0f);
	Check(policy.Show(XRUISurfaceKind::Menu, moved), "reshow failed to initialize a fresh anchor");
	Check(Near(policy.BuildFrame().Surfaces[0].Pose.Center, vec3(-4.0f, 4.0f, 1.0f)), "reshow retained a stale head-relative anchor");

	XRUISurfaceDescriptor hud = CreateXRUISurfaceDescriptor(XRUISurfaceKind::Hud, 800, 600);
	hud.AnchorMode = XRUISurfaceAnchorMode::WorldFixed;
	hud.WorldPose.Center = vec3(7.0f, 8.0f, 9.0f);
	policy.Configure(hud);
	Check(policy.Show(XRUISurfaceKind::Hud, moved), "world-fixed HUD failed to show");
	XRUISurfaceFrame anchoredFrame = policy.BuildFrame();
	Check(Near(anchoredFrame.Surfaces[0].Pose.Center, vec3(7.0f, 8.0f, 9.0f)), "world-fixed surface used the viewer pose");
}

static void TestInvalidViewerBasisAndAnchorModeChange()
{
	XRUISurfaceFramePolicy policy;
	XRUISurfaceDescriptor menu = CreateXRUISurfaceDescriptor(XRUISurfaceKind::Menu, 1200, 600);
	policy.Configure(menu);

	XRUIViewerPose zeroForward;
	zeroForward.Forward = vec3(0.0f);
	Check(!policy.Show(XRUISurfaceKind::Menu, zeroForward), "zero viewer forward vector produced a head-relative anchor");
	Check(policy.BuildFrame().Surfaces.empty(), "invalid zero-forward anchor entered the frame");

	XRUIViewerPose parallelBasis;
	parallelBasis.Forward = vec3(1.0f, 0.0f, 0.0f);
	parallelBasis.Up = vec3(2.0f, 0.0f, 0.0f);
	Check(!policy.Show(XRUISurfaceKind::Menu, parallelBasis), "parallel viewer forward and up vectors produced a head-relative anchor");
	Check(policy.BuildFrame().Surfaces.empty(), "invalid parallel-basis anchor entered the frame");

	XRUISurfaceDescriptor worldMenu = menu;
	worldMenu.AnchorMode = XRUISurfaceAnchorMode::WorldFixed;
	worldMenu.WorldPose.Center = vec3(7.0f, 8.0f, 9.0f);
	policy.Configure(worldMenu);
	Check(policy.Show(XRUISurfaceKind::Menu), "world-fixed menu failed to show before mode change");
	Check(Near(policy.BuildFrame().Surfaces[0].Pose.Center, worldMenu.WorldPose.Center), "world-fixed menu used the wrong pose before mode change");

	menu.HeadRelativeDistance = 2.0f;
	policy.Configure(menu);
	Check(policy.BuildFrame().Surfaces.empty(), "changing to head-relative mode retained the old world anchor");

	XRUIViewerPose validViewer;
	validViewer.Position = vec3(1.0f, 2.0f, 3.0f);
	Check(policy.Show(XRUISurfaceKind::Menu, validViewer), "head-relative menu failed to re-anchor after mode change");
	Check(Near(policy.BuildFrame().Surfaces[0].Pose.Center, vec3(3.0f, 2.0f, 3.0f)), "mode change reused the old world pose instead of the viewer pose");
}

static void TestAspectAndRayMapping()
{
	XRUISurfaceFrameItem menu = MakeMenuSurface();
	Check(Near(menu.Descriptor.PhysicalHeight(), 1.0f), "physical height did not preserve pixel aspect");

	XRUISurfaceContact center = MapRayToXRUISurface(menu, { vec3(0.0f), vec3(1.0f, 0.0f, 0.0f) });
	Check(center.Hit, "center ray missed menu");
	Check(Near(center.Pixel.x, 500.0f) && Near(center.Pixel.y, 250.0f), "center ray mapped to wrong pixels");

	vec3 topLeft = menu.Pose.Center - menu.Pose.Right * 1.0f + menu.Pose.Up * 0.5f;
	XRUISurfaceContact inclusiveEdge = MapRayToXRUISurface(menu, { vec3(0.0f), topLeft });
	Check(inclusiveEdge.Hit, "top-left half-open edge should be addressable");
	Check(Near(inclusiveEdge.Pixel.x, 0.0f) && Near(inclusiveEdge.Pixel.y, 0.0f), "top-left edge mapped incorrectly");

	vec3 rightEdge = menu.Pose.Center + menu.Pose.Right * 1.0f;
	Check(!MapRayToXRUISurface(menu, { vec3(0.0f), rightEdge }).Hit, "exclusive right edge produced an out-of-range pixel");
	vec3 bottomEdge = menu.Pose.Center - menu.Pose.Up * 0.5f;
	Check(!MapRayToXRUISurface(menu, { vec3(0.0f), bottomEdge }).Hit, "exclusive bottom edge produced an out-of-range pixel");
	Check(!MapRayToXRUISurface(menu, { vec3(3.0f, 0.0f, 0.0f), vec3(-1.0f, 0.0f, 0.0f) }).Hit, "back-face ray should not interact with a UI surface");

	XRUISurfaceFrame stacked;
	XRUISurfaceFrameItem cinematic = menu;
	cinematic.Descriptor.Kind = XRUISurfaceKind::Cinematic;
	cinematic.Descriptor.Interactive = true;
	cinematic.CompositionOrder = 300;
	stacked.Surfaces.push_back(cinematic);
	stacked.Surfaces.push_back(menu);
	Check(HitTestXRUISurfaces(stacked, { vec3(0.0f), vec3(1.0f, 0.0f, 0.0f) }).Surface == XRUISurfaceKind::Menu, "hit testing did not prefer the topmost surface");
}

static void TestTrackedPointerEdgesAndDisconnect()
{
	XRUISurfaceFrameItem menu = MakeMenuSurface();
	XRUISurfaceContact center = MapRayToXRUISurface(menu, { vec3(0.0f), vec3(1.0f, 0.0f, 0.0f) });
	XRUISurfaceContact miss;
	XRUISurfaceInputRouter router;
	XRUIPointerSource left = XRUIPointerSource::Tracked(1);

	Array<XRUIPointerEvent> hover = router.Update(left, center, false);
	Check(HasEvent(hover, XRUIPointerEventType::HoverEnter) && HasEvent(hover, XRUIPointerEventType::Move), "tracked hover did not enter and move");
	Array<XRUIPointerEvent> down = router.Update(left, center, true);
	Check(HasEvent(down, XRUIPointerEventType::PrimaryDown), "tracked press edge was lost");
	Array<XRUIPointerEvent> outsideRelease = router.Update(left, miss, false);
	Check(HasEvent(outsideRelease, XRUIPointerEventType::HoverLeave), "leaving the quad did not clear hover");
	Check(HasEvent(outsideRelease, XRUIPointerEventType::PrimaryCancel), "off-surface release did not cancel capture");
	Check(!HasEvent(outsideRelease, XRUIPointerEventType::PrimaryClick), "off-surface release generated a click");

	router.Update(left, miss, true);
	router.Update(left, center, true);
	Array<XRUIPointerEvent> enteredWhileHeld = router.Update(left, center, false);
	Check(!HasEvent(enteredWhileHeld, XRUIPointerEventType::PrimaryClick), "entering while already held generated a click without an on-surface press");

	router.Update(left, center, false);
	router.Update(left, center, true);
	Array<XRUIPointerEvent> click = router.Update(left, center, false);
	Check(HasEvent(click, XRUIPointerEventType::PrimaryUp) && HasEvent(click, XRUIPointerEventType::PrimaryClick), "valid tracked release did not click");

	XRUIPointerSource right = XRUIPointerSource::Tracked(2);
	router.Update(right, center, false);
	router.Update(right, center, true);
	Array<XRUIPointerEvent> disconnected = router.Cancel(right);
	Check(HasEvent(disconnected, XRUIPointerEventType::PrimaryCancel), "disconnect did not cancel a captured press");
	Check(HasEvent(disconnected, XRUIPointerEventType::HoverLeave), "disconnect did not clear hover");
	Check(!HasEvent(router.Update(right, miss, false), XRUIPointerEventType::PrimaryUp), "reconnected pointer released stale state");
}

static void TestMouseFallbackAndSourceIsolation()
{
	XRUISurfaceFrameItem menu = MakeMenuSurface();
	XRUISurfaceContact mouseContact = MapMouseToXRUISurface(menu, Pointf(250.0f, 125.0f));
	Check(mouseContact.Hit && Near(mouseContact.UV.x, 0.25f) && Near(mouseContact.UV.y, 0.25f), "desktop mouse did not preserve canvas coordinates");
	Check(!MapMouseToXRUISurface(menu, Pointf(1000.0f, 125.0f)).Hit, "desktop mouse accepted exclusive right edge");

	XRUISurfaceInputRouter router;
	XRUIPointerSource left = XRUIPointerSource::Tracked(11);
	XRUIPointerSource mouse = XRUIPointerSource::Mouse();
	router.Update(left, mouseContact, false);
	router.Update(left, mouseContact, true);

	router.Update(mouse, mouseContact, false);
	Array<XRUIPointerEvent> mouseDown = router.Update(mouse, mouseContact, true);
	Array<XRUIPointerEvent> mouseUp = router.Update(mouse, mouseContact, false);
	Check(HasEvent(mouseDown, XRUIPointerEventType::PrimaryDown), "mouse fallback press was lost while tracked pointer was held");
	Check(HasEvent(mouseUp, XRUIPointerEventType::PrimaryClick), "mouse fallback release was blocked by tracked pointer state");

	Array<XRUIPointerEvent> leftUp = router.Update(left, mouseContact, false);
	Check(HasEvent(leftUp, XRUIPointerEventType::PrimaryClick), "mouse release incorrectly released the tracked pointer capture");
}

int main()
{
	TestDefaultDesktopContract();
	TestVisibilityAndCompositionOrder();
	TestGameplayHudContract();
	TestAnchorStabilityAndRecenter();
	TestInvalidViewerBasisAndAnchorModeChange();
	TestAspectAndRayMapping();
	TestTrackedPointerEdgesAndDisconnect();
	TestMouseFallbackAndSourceIsolation();
	return 0;
}
