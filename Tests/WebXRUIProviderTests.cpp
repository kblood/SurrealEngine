#include "Platform/WebXR/WebXRUIProvider.h"

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
		void ReleaseXRUIPrimary(const XRUIPointerSource&, XRUISurfaceKind, const Pointf&, bool) override {}
		void EndXRUIPointerSession() override {}

		int Captures = 0;
		int Replays = 0;
		int Presses = 0;
		Pointf Cursor;
	};
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
	if (descriptors[0].Target != WebXR::CinematicSurfaceTarget ||
		descriptors[1].Target != WebXR::LoadingSurfaceTarget ||
		descriptors[2].Target != WebXR::MenuSurfaceTarget ||
		!NearlyEqual(descriptors[2].Surface.PhysicalWidth, 56.0f) ||
		!descriptors[2].Surface.Interactive)
		return 2;

	WebXR::RecenterState recenter;
	recenter.Valid = true;
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
	binding.Configure(descriptors[2]);
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

	std::cout << "WebXR UI provider tests passed\n";
	return 0;
}
