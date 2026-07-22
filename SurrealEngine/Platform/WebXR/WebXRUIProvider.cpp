#include "Platform/WebXR/WebXRUIProvider.h"

#include <cmath>
#include <utility>

namespace
{
	constexpr float Pi = 3.14159265359f;

	XRUIPointerSource PointerSource(size_t handIndex)
	{
		return XRUIPointerSource::Tracked(handIndex + 1);
	}

	vec4 HandColor(XRHand hand, bool selecting)
	{
		if (hand == XRHand::Left)
			return selecting ? vec4(0.35f, 1.0f, 1.0f, 1.0f) : vec4(0.08f, 0.45f, 1.0f, 1.0f);
		return selecting ? vec4(1.0f, 0.9f, 0.2f, 1.0f) : vec4(1.0f, 0.28f, 0.06f, 1.0f);
	}

	void BuildRayBasis(const vec3& direction, vec3& forward, vec3& right, vec3& up)
	{
		forward = normalize(direction);
		const vec3 referenceUp = std::abs(forward.z) < 0.9f ?
			vec3(0.0f, 0.0f, 1.0f) : vec3(0.0f, 1.0f, 0.0f);
		right = normalize(cross(forward, referenceUp));
		up = normalize(cross(right, forward));
	}

	void AppendTriangle(Array<WebXR::UIVisualVertex>& vertices, const vec3& a,
		const vec3& b, const vec3& c, const vec4& color)
	{
		vertices.push_back({ a, color });
		vertices.push_back({ b, color });
		vertices.push_back({ c, color });
	}

	void AppendBox(Array<WebXR::UIVisualVertex>& vertices, const vec3& center,
		const vec3& forward, const vec3& right, const vec3& up, float halfForward,
		float halfRight, float halfUp, const vec4& color)
	{
		vec3 corners[8];
		for (int index = 0; index < 8; index++)
		{
			corners[index] = center + forward * ((index & 1) ? halfForward : -halfForward) +
				right * ((index & 2) ? halfRight : -halfRight) +
				up * ((index & 4) ? halfUp : -halfUp);
		}
		const int faces[12][3] = {
			{ 0, 2, 3 }, { 0, 3, 1 }, { 4, 5, 7 }, { 4, 7, 6 },
			{ 0, 1, 5 }, { 0, 5, 4 }, { 2, 6, 7 }, { 2, 7, 3 },
			{ 0, 4, 6 }, { 0, 6, 2 }, { 1, 3, 7 }, { 1, 7, 5 }
		};
		for (const auto& face : faces)
			AppendTriangle(vertices, corners[face[0]], corners[face[1]], corners[face[2]], color);
	}

	void AppendBeam(Array<WebXR::UIVisualVertex>& vertices, const vec3& start,
		const vec3& end, float radius, const vec4& color)
	{
		vec3 forward, right, up;
		BuildRayBasis(end - start, forward, right, up);
		if (length(end - start) <= 0.0001f || length(forward) <= 0.0001f)
			return;
		constexpr int sides = 8;
		for (int side = 0; side < sides; side++)
		{
			const float angle0 = 2.0f * Pi * static_cast<float>(side) / static_cast<float>(sides);
			const float angle1 = 2.0f * Pi * static_cast<float>(side + 1) / static_cast<float>(sides);
			const vec3 offset0 = (right * std::cos(angle0) + up * std::sin(angle0)) * radius;
			const vec3 offset1 = (right * std::cos(angle1) + up * std::sin(angle1)) * radius;
			AppendTriangle(vertices, start + offset0, end + offset0, end + offset1, color);
			AppendTriangle(vertices, start + offset0, end + offset1, start + offset1, color);
		}
	}

	const XRUICanvasReplayItem* FindSurface(const XRUICanvasReplayFrame& frame,
		XRUISurfaceKind kind)
	{
		for (const XRUICanvasReplayItem& item : frame.Items)
			if (item.Surface.Descriptor.Kind == kind)
				return &item;
		return nullptr;
	}

	void AppendHitMarker(Array<WebXR::UIVisualVertex>& vertices,
		const WebXR::PointerFeedback& feedback, const XRUICanvasReplayItem& surface,
		float radius, const vec4& color)
	{
		constexpr int sides = 12;
		for (int side = 0; side < sides; side++)
		{
			const float angle0 = 2.0f * Pi * static_cast<float>(side) / static_cast<float>(sides);
			const float angle1 = 2.0f * Pi * static_cast<float>(side + 1) / static_cast<float>(sides);
			const vec3 p0 = feedback.HitPoint +
				(surface.Surface.Pose.Right * std::cos(angle0) + surface.Surface.Pose.Up * std::sin(angle0)) * radius;
			const vec3 p1 = feedback.HitPoint +
				(surface.Surface.Pose.Right * std::cos(angle1) + surface.Surface.Pose.Up * std::sin(angle1)) * radius;
			AppendTriangle(vertices, feedback.HitPoint, p0, p1, color);
		}
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

std::array<XRUICanvasCaptureDescriptor, 4> WebXR::BuildUICaptureDescriptors(float worldUnitsPerMeter)
{
	auto hud = CreateXRUICanvasCaptureDescriptor(XRUISurfaceKind::Hud,
		1024, 768, 1, HudSurfaceTarget);
	auto cinematic = CreateXRUICanvasCaptureDescriptor(XRUISurfaceKind::Cinematic,
		1280, 720, 1, CinematicSurfaceTarget);
	auto loading = CreateXRUICanvasCaptureDescriptor(XRUISurfaceKind::Loading,
		1024, 768, 1, LoadingSurfaceTarget);
	auto menu = CreateXRUICanvasCaptureDescriptor(XRUISurfaceKind::Menu,
		1024, 768, 1, MenuSurfaceTarget);
	for (XRUICanvasCaptureDescriptor* descriptor : { &hud, &cinematic, &loading, &menu })
	{
		descriptor->Surface.PhysicalWidth *= worldUnitsPerMeter;
		descriptor->Surface.HeadRelativeDistance *= worldUnitsPerMeter;
	}
	return { hud, cinematic, loading, menu };
}

WebXR::UIVisualFrame WebXR::BuildUIVisualFrame(
	const std::array<PointerFeedback, XRHandCount>& feedback,
	const XRUICanvasReplayFrame& replayFrame, float worldUnitsPerMeter,
	const UIVisualSettings& settings)
{
	UIVisualFrame frame;
	if (replayFrame.Items.empty() || !std::isfinite(worldUnitsPerMeter) || worldUnitsPerMeter <= 0.0f)
		return frame;

	for (const PointerFeedback& pointer : feedback)
	{
		if (!pointer.Active || length(pointer.Ray.Direction) <= 0.0001f)
			continue;
		UIHandVisual hand;
		hand.Hand = pointer.Hand;
		hand.Selecting = pointer.Selecting;
		const vec4 color = HandColor(pointer.Hand, pointer.Selecting);
		vec3 forward, right, up;
		BuildRayBasis(pointer.Ray.Direction, forward, right, up);

		const float bodyLength = settings.ControllerBodyLengthMeters * worldUnitsPerMeter;
		const float bodyWidth = settings.ControllerBodyWidthMeters * worldUnitsPerMeter;
		const float bodyHeight = settings.ControllerBodyHeightMeters * worldUnitsPerMeter;
		AppendBox(hand.Controller, pointer.Ray.Origin - forward * bodyLength * 0.45f,
			forward, right, up, bodyLength * 0.5f, bodyWidth * 0.5f, bodyHeight * 0.5f, color);
		const float gripLength = settings.ControllerGripLengthMeters * worldUnitsPerMeter;
		AppendBox(hand.Controller, pointer.Ray.Origin +
			forward * (settings.ControllerGripForwardOffsetMeters * worldUnitsPerMeter) +
			up * (settings.ControllerGripUpOffsetMeters * worldUnitsPerMeter),
			forward, right, up, bodyHeight * 0.65f, bodyWidth * 0.45f,
			gripLength * 0.5f, color * 0.82f);

		const float laserRadius = settings.LaserRadiusMeters * worldUnitsPerMeter *
			(pointer.Selecting ? settings.SelectingLaserScale : 1.0f);
		AppendBeam(hand.Laser, pointer.Ray.Origin, pointer.HitPoint, laserRadius,
			vec4(color.rgb(), pointer.Selecting ? 1.0f : 0.82f));
		if (pointer.Contact.Hit)
		{
			if (const XRUICanvasReplayItem* surface = FindSurface(replayFrame, pointer.Contact.Surface))
			{
				const vec4 markerColor = pointer.Selecting ? vec4(1.0f, 1.0f, 1.0f, 1.0f) : color;
				AppendHitMarker(hand.HitMarker, pointer, *surface,
					settings.HitMarkerRadiusMeters * worldUnitsPerMeter, markerColor);
			}
		}
		frame.Hands.push_back(std::move(hand));
	}
	return frame;
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
		feedback[handIndex].Selecting = controller.Select.Pressed;
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
