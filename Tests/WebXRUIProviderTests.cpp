#include "Platform/WebXR/WebXRUIProvider.h"
#include "XR/XRHandedness.h"

#include <cmath>
#include <iostream>

namespace
{
	bool NearlyEqual(float a, float b, float epsilon = 0.001f)
	{
		return std::abs(a - b) <= epsilon;
	}

	class Host final : public XRUISurfaceEngineHost
	{
	public:
		bool BeginXRUICanvasCapture(const XRUICanvasReplayItem&) override { Captures++; return true; }
		void ReplayXRUICanvas(const XRUICanvasReplayItem&) override { Replays++; }
		void EndXRUICanvasCapture(const XRUICanvasReplayItem&) override {}
		void MoveXRUICursor(const XRUIPointerSource&, XRUISurfaceKind, const Pointf& pixel) override
		{
			Cursor = pixel;
		}
		void PressXRUIPrimary(const XRUIPointerSource&, XRUISurfaceKind, const Pointf&) override { Presses++; }
		void ReleaseXRUIPrimary(const XRUIPointerSource&, XRUISurfaceKind, const Pointf&, bool) override { Releases++; }
		void EndXRUIPointerSession() override {}

		int Captures = 0;
		int Replays = 0;
		int Presses = 0;
		int Releases = 0;
		Pointf Cursor;
	};

	bool StartupHudHandsOffWithoutClicking(const std::array<XRUICanvasCaptureDescriptor, 4>& descriptors,
		float worldUnitsPerMeter, const WebXR::RecenterState& recenter)
	{
		Host host;
		XRUISurfaceEngineBinding binding(host);
		binding.Configure(descriptors[0]);
		binding.Configure(descriptors[3]);
		binding.SetViewerPose({});
		binding.SetHudActive(true);

		WebXR::AdaptedInputSnapshot input;
		input.Session.Lifecycle = XRSessionLifecycle::Running;
		input.Session.Focus = XRSessionFocus::Focused;
		input.Controllers.ForHand(XRHand::Right).Connected = true;
		input.Controllers.ForHand(XRHand::Right).Select.Pressed = true;
		input.Spaces.AimFor(XRHand::Right).Valid = true;
		input.Spaces.AimFor(XRHand::Right).Orientation.W = 1.0f;
		WebXR::UIInputConnector connector;
		connector.Update(input, binding, vec3(0.0f), Coords::Identity(), worldUnitsPerMeter, recenter);

		const XRUICanvasReplayFrame intro =
			WebXR::OrientUIReplayFrame(binding.BuildReplayFrame());
		const WebXR::UIVisualFrame introVisuals = WebXR::BuildUIVisualFrame(
			connector.Feedback(), intro, worldUnitsPerMeter);
		if (intro.Items.size() != 1 || intro.Items[0].Surface.Descriptor.Kind != XRUISurfaceKind::Hud ||
			introVisuals.Hands.size() != 1 || introVisuals.Hands[0].Controller.empty() ||
			introVisuals.Hands[0].Laser.empty() || !introVisuals.Hands[0].HitMarker.empty() || host.Presses != 0)
			return false;

		binding.SetHudActive(false);
		binding.SetMenuActive(true);
		connector.Update(input, binding, vec3(0.0f), Coords::Identity(), worldUnitsPerMeter, recenter);
		binding.Replay(XRUICanvasReplayContext::Game);
		const WebXR::PointerFeedback& held = connector.Feedback()[XRHandIndex(XRHand::Right)];
		if (!held.Contact.Hit || held.Contact.Surface != XRUISurfaceKind::Menu || host.Presses != 0)
			return false;

		input.Controllers.ForHand(XRHand::Right).Select.Pressed = false;
		connector.Update(input, binding, vec3(0.0f), Coords::Identity(), worldUnitsPerMeter, recenter);
		binding.Replay(XRUICanvasReplayContext::Game);
		if (host.Presses != 0 || host.Releases != 0)
			return false;

		input.Controllers.ForHand(XRHand::Right).Select.Pressed = true;
		connector.Update(input, binding, vec3(0.0f), Coords::Identity(), worldUnitsPerMeter, recenter);
		binding.Replay(XRUICanvasReplayContext::Game);
		const XRUICanvasReplayFrame menu =
			WebXR::OrientUIReplayFrame(binding.BuildReplayFrame());
		const WebXR::UIVisualFrame menuVisuals = WebXR::BuildUIVisualFrame(
			connector.Feedback(), menu, worldUnitsPerMeter);
		return host.Presses == 1 && menu.Items.size() == 1 &&
			menu.Items[0].Surface.Descriptor.Kind == XRUISurfaceKind::Menu &&
			menuVisuals.Hands.size() == 1 && !menuVisuals.Hands[0].HitMarker.empty();
	}
}

int main()
{
	ViewFamily family;
	ViewDescription left;
	left.Location = vec3(10.0f, -2.0f, 4.0f);
	left.Rotation = Coords::Identity();
	ViewDescription right = left;
	right.Location.y = 2.0f;
	family.Views = { left, right };
	const XRUIViewerPose viewer = WebXR::BuildUIViewerPose(family);
	if (!NearlyEqual(viewer.Position.x, 10.0f) || !NearlyEqual(viewer.Position.y, 0.0f) ||
		!NearlyEqual(viewer.Forward.x, 1.0f) || !NearlyEqual(viewer.Up.z, 1.0f))
		return 1;

	const float units = 40.0f;
	const auto descriptors = WebXR::BuildUICaptureDescriptors(units);
	if (descriptors[0].Target != WebXR::HudSurfaceTarget ||
		descriptors[1].Target != WebXR::CinematicSurfaceTarget ||
		descriptors[2].Target != WebXR::LoadingSurfaceTarget ||
		descriptors[3].Target != WebXR::MenuSurfaceTarget ||
		descriptors[0].Surface.Interactive ||
		!NearlyEqual(descriptors[3].Surface.PhysicalWidth, 56.0f) ||
		!descriptors[3].Surface.Interactive)
		return 2;
	PerViewHudPresentation directHud;
	directHud.Enabled = true;
	directHud.HalfFovDegrees = 25.0f;
	directHud.ConvergenceDepth = 68.9f;
	const auto directDescriptors = WebXR::BuildDirectUICaptureDescriptors(
		units, directHud, 800, 600, 2);
	const float directWidth = 2.0f * directHud.ConvergenceDepth *
		std::tan(directHud.HalfFovDegrees * 3.14159265359f / 180.0f);
	if (directDescriptors[3].Surface.AnchorMode !=
			XRUISurfaceAnchorMode::HeadRelativeEveryFrame ||
		!NearlyEqual(directDescriptors[3].Surface.HeadRelativeDistance,
			directHud.ConvergenceDepth) ||
		!NearlyEqual(directDescriptors[3].Surface.PhysicalWidth, directWidth) ||
		directDescriptors[3].Surface.PixelWidth != 800 ||
		directDescriptors[3].Surface.PixelHeight != 600 ||
		directDescriptors[3].CanvasScale != 2)
		return 26;
	const Pointf directLogicalCenter = directDescriptors[3].ToCanvasPixel(
		Pointf(400.0f, 300.0f));
	if (!NearlyEqual(directLogicalCenter.x, 200.0f) ||
		!NearlyEqual(directLogicalCenter.y, 150.0f))
		return 29;
	Host directHost;
	XRUISurfaceEngineBinding directBinding(directHost);
	directBinding.Configure(directDescriptors[3]);
	directBinding.SetViewerPose({});
	directBinding.SetMenuActive(true);
	XRUIViewerPose movedViewer;
	movedViewer.Position = vec3(4.0f, 5.0f, 6.0f);
	movedViewer.Forward = vec3(0.0f, 1.0f, 0.0f);
	movedViewer.Up = vec3(0.0f, 0.0f, 1.0f);
	directBinding.SetViewerPose(movedViewer);
	const XRUICanvasReplayFrame directReplay =
		WebXR::OrientUIReplayFrame(directBinding.BuildReplayFrame());
	if (directReplay.Items.size() != 1 ||
		!NearlyEqual(directReplay.Items[0].Surface.Pose.Center.x, 4.0f) ||
		!NearlyEqual(directReplay.Items[0].Surface.Pose.Center.y,
			5.0f + directHud.ConvergenceDepth) ||
		!NearlyEqual(directReplay.Items[0].Surface.Pose.Center.z, 6.0f))
		return 27;
	const XRUISurfaceFrameItem& directMenu = directReplay.Items[0].Surface;
	const XRUISurfaceContact directCenter = MapRayToXRUISurface(directMenu,
		{ movedViewer.Position, movedViewer.Forward });
	const vec3 directRightPoint = directMenu.Pose.Center +
		directMenu.Pose.Right * (directMenu.Descriptor.PhysicalWidth * 0.5f);
	const XRUISurfaceContact directRight = MapRayToXRUISurface(directMenu,
		{ movedViewer.Position, directRightPoint - movedViewer.Position });
	if (!directCenter.Hit ||
		!NearlyEqual(directCenter.Pixel.x, 399.5f, 1.0f) ||
		!NearlyEqual(directCenter.Pixel.y, 299.5f, 1.0f) ||
		directRight.Hit)
		return 28;
	directBinding.UpdateRayPointer(XRUIPointerSource::Tracked(1),
		{ movedViewer.Position, movedViewer.Forward }, false);
	if (!NearlyEqual(directHost.Cursor.x, 200.0f, 1.0f) ||
		!NearlyEqual(directHost.Cursor.y, 150.0f, 1.0f))
		return 30;

	WebXR::RecenterState recenter;
	recenter.Valid = true;
	if (!StartupHudHandsOffWithoutClicking(descriptors, units, recenter))
		return 19;
	XRPose aim;
	aim.Valid = true;
	aim.Position = { 0.1f, 0.0f, -1.0f };
	aim.Orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
	const XRUISurfaceRay ray = WebXR::BuildUIRay(aim, vec3(0.0f), Coords::Identity(), units, recenter);
	if (!NearlyEqual(ray.Origin.x, 40.0f) || !NearlyEqual(ray.Origin.y, 4.0f) ||
		!NearlyEqual(ray.Direction.x, 1.0f))
		return 3;

	Host host;
	XRUISurfaceEngineBinding binding(host);
	binding.Configure(descriptors[3]);
	binding.SetViewerPose({});
	binding.SetMenuActive(true);
	WebXR::AdaptedInputSnapshot input;
	input.Session.Lifecycle = XRSessionLifecycle::Running;
	input.Session.Focus = XRSessionFocus::Focused;
	input.Controllers.ForHand(XRHand::Right).Connected = true;
	input.Spaces.AimFor(XRHand::Right).Valid = true;
	input.Spaces.AimFor(XRHand::Right).Orientation.W = 1.0f;
	WebXR::UIInputConnector connector;
	connector.Update(input, binding, vec3(0.0f), Coords::Identity(), units, recenter);
	const WebXR::PointerFeedback& feedback = connector.Feedback()[XRHandIndex(XRHand::Right)];
	if (!feedback.Active || !feedback.Contact.Hit ||
		!NearlyEqual(feedback.Contact.Pixel.x, 511.5f, 1.0f) ||
		!NearlyEqual(feedback.Contact.Pixel.y, 383.5f, 1.0f))
		return 4;

	input.Controllers.ForHand(XRHand::Right).Select.Pressed = true;
	connector.Update(input, binding, vec3(0.0f), Coords::Identity(), units, recenter);
	binding.Replay(XRUICanvasReplayContext::Game);
	if (host.Captures != 1 || host.Replays != 1 || host.Presses != 1)
		return 5;
	const WebXR::PointerFeedback& selectingFeedback =
		connector.Feedback()[XRHandIndex(XRHand::Right)];
	if (!selectingFeedback.Selecting)
		return 6;

	binding.Configure(descriptors[2]);
	binding.SetSurfaceActive(XRUISurfaceKind::Loading, true);
	const XRUICanvasReplayFrame nativeReplayFrame = binding.BuildReplayFrame();
	const XRUICanvasReplayFrame replayFrame = WebXR::OrientUIReplayFrame(nativeReplayFrame);
	if (replayFrame.Items.size() != 2 ||
		replayFrame.Items[0].Surface.Descriptor.Kind != XRUISurfaceKind::Loading ||
		replayFrame.Items[1].Surface.Descriptor.Kind != XRUISurfaceKind::Menu ||
		selectingFeedback.Contact.Surface != XRUISurfaceKind::Menu)
		return 7;
	const vec3 nativeRight = nativeReplayFrame.Items[1].Surface.Pose.Right;
	const vec3 webRight = replayFrame.Items[1].Surface.Pose.Right;
	if (!NearlyEqual(nativeRight.y, -1.0f) || !NearlyEqual(webRight.y, 1.0f))
		return 23;
	const XRUISurfaceFrameItem& menuSurface = replayFrame.Items[1].Surface;
	const vec3 readableRight = menuSurface.Pose.Center + menuSurface.Pose.Right *
		(menuSurface.Descriptor.PhysicalWidth * 0.25f);
	const XRUISurfaceContact readableContact = MapRayToXRUISurface(
		menuSurface, { vec3(0.0f), readableRight });
	if (!readableContact.Hit || !NearlyEqual(readableContact.UV.x, 0.75f) ||
		!NearlyEqual(readableContact.Pixel.x,
			menuSurface.Descriptor.PixelWidth * 0.75f, 1.0f))
		return 24;
	const WebXR::UIVisualFrame visuals = WebXR::BuildUIVisualFrame(
		connector.Feedback(), replayFrame, units);
	if (visuals.Hands.size() != 1 || visuals.Hands[0].Hand != XRHand::Right ||
		!visuals.Hands[0].Selecting || visuals.Hands[0].Controller.size() != 72 ||
		visuals.Hands[0].Laser.size() != 48 || visuals.Hands[0].HitMarker.size() != 36)
		return 8;
	if (!(WebXR::ControllerVisualCompositionOrder < replayFrame.Items[0].Surface.CompositionOrder &&
		replayFrame.Items[0].Surface.CompositionOrder < replayFrame.Items[1].Surface.CompositionOrder &&
		WebXR::HitMarkerCompositionOrder > replayFrame.Items[1].Surface.CompositionOrder))
		return 9;
	for (const WebXR::UIVisualVertex& vertex : visuals.Hands[0].Laser)
	{
		if (vertex.Position.x < -0.001f ||
			vertex.Position.x > selectingFeedback.HitPoint.x + 0.001f ||
			!NearlyEqual(std::sqrt(vertex.Position.y * vertex.Position.y +
				vertex.Position.z * vertex.Position.z),
				0.0025f * units * 1.6f, 0.001f))
			return 10;
	}
	for (size_t index = 0; index < visuals.Hands[0].HitMarker.size(); index += 3)
	{
		const vec3& center = visuals.Hands[0].HitMarker[index].Position;
		if (!NearlyEqual(center.x, selectingFeedback.HitPoint.x) ||
			!NearlyEqual(center.y, selectingFeedback.HitPoint.y) ||
			!NearlyEqual(center.z, selectingFeedback.HitPoint.z) ||
			visuals.Hands[0].HitMarker[index].Color.a != 1.0f)
			return 11;
	}
	auto bothHands = connector.Feedback();
	bothHands[XRHandIndex(XRHand::Left)] = selectingFeedback;
	bothHands[XRHandIndex(XRHand::Left)].Hand = XRHand::Left;
	bothHands[XRHandIndex(XRHand::Left)].Selecting = false;
	bothHands[XRHandIndex(XRHand::Left)].Ray.Origin.y -= 4.0f;
	bothHands[XRHandIndex(XRHand::Left)].HitPoint.y -= 4.0f;
	const WebXR::UIVisualFrame pairedVisuals = WebXR::BuildUIVisualFrame(bothHands, replayFrame, units);
	if (pairedVisuals.Hands.size() != 2 || pairedVisuals.Hands[0].Hand != XRHand::Left ||
		pairedVisuals.Hands[0].Selecting || pairedVisuals.Hands[1].Hand != XRHand::Right ||
		!pairedVisuals.Hands[1].Selecting ||
		!pairedVisuals.Hands[0].Laser.empty() || pairedVisuals.Hands[1].Laser.empty() ||
		pairedVisuals.Hands[0].Controller[0].Color == pairedVisuals.Hands[1].Controller[0].Color)
		return 12;
	const XRHandedness leftHanded{ XRHand::Left };
	WebXR::UIVisualSettings leftPointer;
	leftPointer.PointerHand = leftHanded.Dominant;
	const WebXR::UIVisualFrame leftPointerVisuals = WebXR::BuildUIVisualFrame(
		bothHands, replayFrame, units, leftPointer);
	if (leftPointerVisuals.Hands[0].Laser.empty() ||
		!leftPointerVisuals.Hands[1].Laser.empty())
		return 25;

	// A held select remains visual state; it does not synthesize another edge.
	connector.Update(input, binding, vec3(0.0f), Coords::Identity(), units, recenter);
	binding.Replay(XRUICanvasReplayContext::Game);
	if (host.Presses != 1)
		return 13;
	input.Controllers.ForHand(XRHand::Right).Select.Pressed = false;
	connector.Update(input, binding, vec3(0.0f), Coords::Identity(), units, recenter);
	binding.Replay(XRUICanvasReplayContext::Game);
	if (host.Releases != 1)
		return 14;

	if (!WebXR::BuildUIVisualFrame(connector.Feedback(), {}, units).Hands.empty())
		return 15;

	connector.Cancel(binding);
	if (connector.Feedback()[XRHandIndex(XRHand::Right)].Active ||
		connector.Feedback()[XRHandIndex(XRHand::Right)].Selecting ||
		!WebXR::BuildUIVisualFrame(connector.Feedback(), replayFrame, units).Hands.empty())
		return 16;
	connector.Update(input, binding, vec3(0.0f), Coords::Identity(), units, recenter);
	if (!connector.Feedback()[XRHandIndex(XRHand::Right)].Active ||
		connector.Feedback()[XRHandIndex(XRHand::Right)].Selecting ||
		!connector.Feedback()[XRHandIndex(XRHand::Right)].Contact.Hit)
		return 17;
	input.Controllers.ForHand(XRHand::Right).Select.Pressed = true;
	connector.Update(input, binding, vec3(0.0f), Coords::Identity(), units, recenter);
	binding.Replay(XRUICanvasReplayContext::Game);
	if (host.Presses != 2 ||
		!connector.Feedback()[XRHandIndex(XRHand::Right)].Selecting)
		return 18;

	std::cout << "WebXR UI provider tests passed\n";
	return 0;
}
