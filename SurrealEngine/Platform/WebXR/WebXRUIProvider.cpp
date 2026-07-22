#include "Platform/WebXR/WebXRUIProvider.h"

namespace
{
	XRUIPointerSource PointerSource(size_t handIndex)
	{
		return XRUIPointerSource::Tracked(handIndex + 1);
	}
}

XRUIViewerPose WebXR::BuildUIViewerPose(const ViewFamily& family)
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

XRUISurfaceRay WebXR::BuildUIRay(const XRPose& pose, const vec3& cameraLocation,
	const Coords& bodyRotation, float worldUnitsPerMeter, const RecenterState& recenter)
{
	if (!pose.Valid || !recenter.Valid)
		return {};
	EngineTrackedPose enginePose = TransformCanonicalPose(
		vec3(pose.Position.X, pose.Position.Y, pose.Position.Z),
		vec4(pose.Orientation.X, pose.Orientation.Y, pose.Orientation.Z, pose.Orientation.W),
		cameraLocation, bodyRotation, worldUnitsPerMeter, recenter);
	return { enginePose.Position, normalize(enginePose.Forward) };
}

std::array<XRUICanvasCaptureDescriptor, 3> WebXR::BuildUICaptureDescriptors(float worldUnitsPerMeter)
{
	auto cinematic = CreateXRUICanvasCaptureDescriptor(XRUISurfaceKind::Cinematic,
		1280, 720, 1, CinematicSurfaceTarget);
	auto loading = CreateXRUICanvasCaptureDescriptor(XRUISurfaceKind::Loading,
		1024, 768, 1, LoadingSurfaceTarget);
	auto menu = CreateXRUICanvasCaptureDescriptor(XRUISurfaceKind::Menu,
		1024, 768, 1, MenuSurfaceTarget);
	for (XRUICanvasCaptureDescriptor* descriptor : { &cinematic, &loading, &menu })
	{
		descriptor->Surface.PhysicalWidth *= worldUnitsPerMeter;
		descriptor->Surface.HeadRelativeDistance *= worldUnitsPerMeter;
	}
	return { cinematic, loading, menu };
}

void WebXR::UIInputConnector::Update(const AdaptedInputSnapshot& input,
	XRUISurfaceEngineBinding& binding, const vec3& cameraLocation,
	const Coords& bodyRotation, float worldUnitsPerMeter, const RecenterState& recenter)
{
	for (size_t handIndex = 0; handIndex < XRHandCount; handIndex++)
	{
		const XRHand hand = handIndex == 0 ? XRHand::Left : XRHand::Right;
		const XRHandControllerState& controller = input.Controllers.Hands[handIndex];
		const XRPose& aim = input.Spaces.Aim[handIndex];
		if (!input.Session.AcceptsInput() || !controller.Connected || !aim.Valid || !recenter.Valid)
		{
			if (active[handIndex])
				binding.CancelPointer(PointerSource(handIndex));
			active[handIndex] = false;
			feedback[handIndex] = {};
			feedback[handIndex].Hand = hand;
			continue;
		}

		const XRUISurfaceRay ray = BuildUIRay(aim, cameraLocation, bodyRotation,
			worldUnitsPerMeter, recenter);
		const XRUIPointerUpdateResult update = binding.UpdateRayPointer(
			PointerSource(handIndex), ray, controller.Select.Pressed);
		active[handIndex] = true;
		feedback[handIndex].Active = true;
		feedback[handIndex].Hand = hand;
		feedback[handIndex].Ray = ray;
		feedback[handIndex].Contact = update.Contact;
		feedback[handIndex].HitPoint = update.Contact.Hit ?
			ray.Origin + ray.Direction * update.Contact.Distance : ray.Origin + ray.Direction * worldUnitsPerMeter;
	}
}

void WebXR::UIInputConnector::Cancel(XRUISurfaceEngineBinding& binding)
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
