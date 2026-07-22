#include "XR/XRUIRuntime.h"

#include <cmath>

namespace
{
	XRUIPointerSource PointerSource(size_t handIndex)
	{
		return XRUIPointerSource::Tracked(handIndex + 1);
	}
}

std::array<XRUICanvasCaptureDescriptor, 4> BuildXRUICaptureDescriptors(
	float worldUnitsPerMeter, const XRUISurfaceTargets& targets)
{
	auto hud = CreateXRUICanvasCaptureDescriptor(XRUISurfaceKind::Hud,
		1024, 768, 1, targets.Hud);
	auto cinematic = CreateXRUICanvasCaptureDescriptor(XRUISurfaceKind::Cinematic,
		1280, 720, 1, targets.Cinematic);
	auto loading = CreateXRUICanvasCaptureDescriptor(XRUISurfaceKind::Loading,
		1024, 768, 1, targets.Loading);
	auto menu = CreateXRUICanvasCaptureDescriptor(XRUISurfaceKind::Menu,
		1024, 768, 1, targets.Menu);
	for (XRUICanvasCaptureDescriptor* descriptor : { &hud, &cinematic, &loading, &menu })
	{
		descriptor->Surface.PhysicalWidth *= worldUnitsPerMeter;
		descriptor->Surface.HeadRelativeDistance *= worldUnitsPerMeter;
	}
	return { hud, cinematic, loading, menu };
}

XRUIViewerPose BuildXRUIViewerPose(const ViewFamily& family)
{
	XRUIViewerPose result;
	if (family.Views.empty())
		return result;
	result.Position = vec3(0.0f);
	result.Forward = vec3(0.0f);
	result.Up = vec3(0.0f);
	for (const ViewDescription& view : family.Views)
	{
		result.Position += view.Location;
		result.Forward += view.Rotation.XAxis;
		result.Up += view.Rotation.ZAxis;
	}
	result.Position /= static_cast<float>(family.Views.size());
	result.Forward = normalize(result.Forward);
	result.Up = normalize(result.Up);
	return result;
}

void XRUIInputConnector::Update(const XRUIInputFrame& input,
	XRUISurfaceEngineBinding& binding)
{
	const float missDistance = std::isfinite(input.MissDistance) && input.MissDistance > 0.0f ?
		input.MissDistance : 1.0f;
	for (size_t handIndex = 0; handIndex < XRHandCount; handIndex++)
	{
		const XRHand hand = handIndex == 0 ? XRHand::Left : XRHand::Right;
		const XRHandControllerState& controller = input.Controllers.Hands[handIndex];
		const XRUISurfaceRay& ray = input.AimRays[handIndex];
		if (!input.Session.AcceptsInput() || !controller.Connected ||
			!input.AimRayValid[handIndex] || length(ray.Direction) <= 0.0001f)
		{
			if (active[handIndex])
				binding.CancelPointer(PointerSource(handIndex));
			active[handIndex] = false;
			feedback[handIndex] = {};
			feedback[handIndex].Hand = hand;
			continue;
		}

		const XRUIPointerUpdateResult update = binding.UpdateRayPointer(
			PointerSource(handIndex), ray, controller.Select.Pressed);
		active[handIndex] = true;
		feedback[handIndex].Active = true;
		feedback[handIndex].Selecting = controller.Select.Pressed;
		feedback[handIndex].Hand = hand;
		feedback[handIndex].Ray = ray;
		feedback[handIndex].Contact = update.Contact;
		feedback[handIndex].HitPoint = update.Contact.Hit ?
			ray.Origin + ray.Direction * update.Contact.Distance :
			ray.Origin + ray.Direction * missDistance;
	}
}

void XRUIInputConnector::Cancel(XRUISurfaceEngineBinding& binding)
{
	for (size_t handIndex = 0; handIndex < XRHandCount; handIndex++)
	{
		if (active[handIndex])
			binding.CancelPointer(PointerSource(handIndex));
		active[handIndex] = false;
		feedback[handIndex] = {};
		feedback[handIndex].Hand = handIndex == 0 ? XRHand::Left : XRHand::Right;
	}
}
