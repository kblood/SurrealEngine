#include "Render/XRUISurfaces.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float GeometryEpsilon = 0.00001f;

	int CompositionOrder(XRUISurfaceKind kind)
	{
		switch (kind)
		{
		case XRUISurfaceKind::Hud: return 200;
		case XRUISurfaceKind::Cinematic: return 300;
		case XRUISurfaceKind::Loading: return 400;
		case XRUISurfaceKind::Menu: return 500;
		}
		return 500;
	}

	bool IsFinite(const vec3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	bool BuildHeadRelativePose(const XRUIViewerPose& viewer, float distance, XRUISurfacePose& pose)
	{
		if (!IsFinite(viewer.Position) || !IsFinite(viewer.Forward) || !IsFinite(viewer.Up) || !std::isfinite(distance) || distance <= 0.0f)
			return false;

		float forwardLength = length(viewer.Forward);
		float upLength = length(viewer.Up);
		if (!std::isfinite(forwardLength) || !std::isfinite(upLength) || forwardLength <= GeometryEpsilon || upLength <= GeometryEpsilon)
			return false;

		vec3 forward = viewer.Forward / forwardLength;
		vec3 viewerUp = viewer.Up / upLength;
		vec3 rightBasis = cross(forward, viewerUp);
		float rightLength = length(rightBasis);
		if (!IsFinite(rightBasis) || !std::isfinite(rightLength) || rightLength <= GeometryEpsilon)
			return false;

		vec3 right = rightBasis / rightLength;
		vec3 up = normalize(cross(right, forward));
		pose.Center = viewer.Position + forward * distance;
		pose.Right = right;
		pose.Up = up;
		pose.Normal = -forward;
		return true;
	}

	bool IsValidPose(const XRUISurfacePose& pose)
	{
		if (!IsFinite(pose.Center) || !IsFinite(pose.Right) || !IsFinite(pose.Up) || !IsFinite(pose.Normal))
			return false;
		vec3 right = normalize(pose.Right);
		vec3 up = normalize(pose.Up);
		vec3 normal = normalize(pose.Normal);
		return length(right) > GeometryEpsilon && length(up) > GeometryEpsilon && length(normal) > GeometryEpsilon &&
			std::abs(dot(right, up)) < 0.001f && std::abs(dot(right, normal)) < 0.001f && std::abs(dot(up, normal)) < 0.001f;
	}

	XRUIPointerEvent MakeEvent(XRUIPointerEventType type, const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& pixel)
	{
		return { type, source, surface, pixel };
	}
}

float XRUISurfaceDescriptor::PhysicalHeight() const
{
	if (!HasValidExtent())
		return 0.0f;
	return PhysicalWidth * static_cast<float>(PixelHeight) / static_cast<float>(PixelWidth);
}

bool XRUISurfaceDescriptor::HasValidExtent() const
{
	return PixelWidth > 0 && PixelHeight > 0 && std::isfinite(PhysicalWidth) && PhysicalWidth > 0.0f;
}

XRUISurfaceDescriptor CreateXRUISurfaceDescriptor(XRUISurfaceKind kind, int pixelWidth, int pixelHeight)
{
	XRUISurfaceDescriptor descriptor;
	descriptor.Kind = kind;
	descriptor.PixelWidth = pixelWidth;
	descriptor.PixelHeight = pixelHeight;
	descriptor.Interactive = kind == XRUISurfaceKind::Menu;
	descriptor.ContentLayer = kind == XRUISurfaceKind::Cinematic ? PresentationLayer::Cinematic : PresentationLayer::UserInterface;
	return descriptor;
}

void XRUISurfaceFramePolicy::Configure(const XRUISurfaceDescriptor& descriptor)
{
	if (SurfaceState* state = Find(descriptor.Kind))
	{
		bool anchorModeChanged = state->Descriptor.AnchorMode != descriptor.AnchorMode;
		state->Descriptor = descriptor;
		if (descriptor.AnchorMode == XRUISurfaceAnchorMode::WorldFixed)
		{
			state->Pose = descriptor.WorldPose;
			state->Anchored = IsValidPose(state->Pose);
		}
		else if (anchorModeChanged)
		{
			state->Anchored = false;
		}
		return;
	}

	SurfaceState state;
	state.Descriptor = descriptor;
	if (descriptor.AnchorMode == XRUISurfaceAnchorMode::WorldFixed)
	{
		state.Pose = descriptor.WorldPose;
		state.Anchored = IsValidPose(state.Pose);
	}
	States.push_back(state);
}

bool XRUISurfaceFramePolicy::Show(XRUISurfaceKind kind, const XRUIViewerPose& viewerPose)
{
	SurfaceState* state = Find(kind);
	if (!state || !state->Descriptor.HasValidExtent())
		return false;

	if (!state->Anchored && !InitializeAnchor(*state, viewerPose))
		return false;

	state->Visible = true;
	return true;
}

void XRUISurfaceFramePolicy::Hide(XRUISurfaceKind kind)
{
	if (SurfaceState* state = Find(kind))
	{
		state->Visible = false;
		if (state->Descriptor.AnchorMode == XRUISurfaceAnchorMode::HeadRelativeOnShow)
			state->Anchored = false;
	}
}

bool XRUISurfaceFramePolicy::Recenter(XRUISurfaceKind kind, const XRUIViewerPose& viewerPose)
{
	SurfaceState* state = Find(kind);
	if (!state || state->Descriptor.AnchorMode != XRUISurfaceAnchorMode::HeadRelativeOnShow)
		return false;
	state->Anchored = false;
	return InitializeAnchor(*state, viewerPose);
}

bool XRUISurfaceFramePolicy::IsVisible(XRUISurfaceKind kind) const
{
	const SurfaceState* state = Find(kind);
	return state && state->Visible;
}

XRUISurfaceFrame XRUISurfaceFramePolicy::BuildFrame() const
{
	XRUISurfaceFrame frame;
	for (const SurfaceState& state : States)
	{
		if (state.Visible && state.Anchored && state.Descriptor.HasValidExtent())
			frame.Surfaces.push_back({ state.Descriptor, state.Pose, CompositionOrder(state.Descriptor.Kind), XRUISurfaceDepthMode::IgnoreWorldDepth });
	}
	std::sort(frame.Surfaces.begin(), frame.Surfaces.end(), [](const XRUISurfaceFrameItem& a, const XRUISurfaceFrameItem& b)
	{
		return a.CompositionOrder < b.CompositionOrder;
	});
	return frame;
}

XRUISurfaceFramePolicy::SurfaceState* XRUISurfaceFramePolicy::Find(XRUISurfaceKind kind)
{
	for (SurfaceState& state : States)
	{
		if (state.Descriptor.Kind == kind)
			return &state;
	}
	return nullptr;
}

const XRUISurfaceFramePolicy::SurfaceState* XRUISurfaceFramePolicy::Find(XRUISurfaceKind kind) const
{
	for (const SurfaceState& state : States)
	{
		if (state.Descriptor.Kind == kind)
			return &state;
	}
	return nullptr;
}

bool XRUISurfaceFramePolicy::InitializeAnchor(SurfaceState& state, const XRUIViewerPose& viewerPose)
{
	if (state.Descriptor.AnchorMode == XRUISurfaceAnchorMode::WorldFixed)
	{
		state.Pose = state.Descriptor.WorldPose;
		state.Anchored = IsValidPose(state.Pose);
	}
	else
	{
		state.Anchored = BuildHeadRelativePose(viewerPose, state.Descriptor.HeadRelativeDistance, state.Pose);
	}
	return state.Anchored;
}

XRUISurfaceContact MapRayToXRUISurface(const XRUISurfaceFrameItem& surface, const XRUISurfaceRay& ray)
{
	XRUISurfaceContact contact;
	contact.Surface = surface.Descriptor.Kind;
	if (!surface.Descriptor.HasValidExtent() || !IsValidPose(surface.Pose))
		return contact;

	vec3 direction = normalize(ray.Direction);
	vec3 normal = normalize(surface.Pose.Normal);
	float denominator = dot(direction, normal);
	if (length(direction) <= GeometryEpsilon || denominator >= -GeometryEpsilon)
		return contact;

	float distance = dot(surface.Pose.Center - ray.Origin, normal) / denominator;
	if (!std::isfinite(distance) || distance <= 0.0f)
		return contact;

	vec3 point = ray.Origin + direction * distance;
	vec3 offset = point - surface.Pose.Center;
	float width = surface.Descriptor.PhysicalWidth;
	float height = surface.Descriptor.PhysicalHeight();
	float u = dot(offset, normalize(surface.Pose.Right)) / width + 0.5f;
	float v = 0.5f - dot(offset, normalize(surface.Pose.Up)) / height;
	if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f)
		return contact;

	contact.Hit = true;
	contact.UV = vec2(u, v);
	contact.Pixel = Pointf(u * surface.Descriptor.PixelWidth, v * surface.Descriptor.PixelHeight);
	contact.Distance = distance;
	return contact;
}

XRUISurfaceContact HitTestXRUISurfaces(const XRUISurfaceFrame& frame, const XRUISurfaceRay& ray)
{
	for (auto it = frame.Surfaces.rbegin(); it != frame.Surfaces.rend(); ++it)
	{
		if (!it->Descriptor.Interactive)
			continue;
		XRUISurfaceContact contact = MapRayToXRUISurface(*it, ray);
		if (contact.Hit)
			return contact;
	}
	return {};
}

XRUISurfaceContact MapMouseToXRUISurface(const XRUISurfaceFrameItem& surface, const Pointf& pixel)
{
	XRUISurfaceContact contact;
	contact.Surface = surface.Descriptor.Kind;
	if (!surface.Descriptor.Interactive || !surface.Descriptor.HasValidExtent() || pixel.x < 0.0f || pixel.y < 0.0f || pixel.x >= surface.Descriptor.PixelWidth || pixel.y >= surface.Descriptor.PixelHeight)
		return contact;

	contact.Hit = true;
	contact.Pixel = pixel;
	contact.UV = vec2(pixel.x / surface.Descriptor.PixelWidth, pixel.y / surface.Descriptor.PixelHeight);
	return contact;
}

Array<XRUIPointerEvent> XRUISurfaceInputRouter::Update(const XRUIPointerSource& source, const XRUISurfaceContact& contact, bool primaryPressed)
{
	Array<XRUIPointerEvent> events;
	PointerState& state = GetOrCreate(source);

	bool sameHover = state.HasHover && contact.Hit && state.HoverSurface == contact.Surface;
	if (state.HasHover && !sameHover)
		events.push_back(MakeEvent(XRUIPointerEventType::HoverLeave, source, state.HoverSurface, state.HoverPixel));
	if (contact.Hit && !sameHover)
		events.push_back(MakeEvent(XRUIPointerEventType::HoverEnter, source, contact.Surface, contact.Pixel));
	if (contact.Hit)
		events.push_back(MakeEvent(XRUIPointerEventType::Move, source, contact.Surface, contact.Pixel));

	state.HasHover = contact.Hit;
	if (contact.Hit)
	{
		state.HoverSurface = contact.Surface;
		state.HoverPixel = contact.Pixel;
	}

	if (primaryPressed && !state.PrimaryPressed)
	{
		if (contact.Hit)
		{
			state.HasCapture = true;
			state.CaptureSurface = contact.Surface;
			state.CapturePixel = contact.Pixel;
			events.push_back(MakeEvent(XRUIPointerEventType::PrimaryDown, source, contact.Surface, contact.Pixel));
		}
	}
	else if (!primaryPressed && state.PrimaryPressed && state.HasCapture)
	{
		if (contact.Hit && contact.Surface == state.CaptureSurface)
		{
			events.push_back(MakeEvent(XRUIPointerEventType::PrimaryUp, source, contact.Surface, contact.Pixel));
			events.push_back(MakeEvent(XRUIPointerEventType::PrimaryClick, source, contact.Surface, contact.Pixel));
		}
		else
		{
			events.push_back(MakeEvent(XRUIPointerEventType::PrimaryCancel, source, state.CaptureSurface, state.CapturePixel));
		}
		state.HasCapture = false;
	}

	state.PrimaryPressed = primaryPressed;
	return events;
}

Array<XRUIPointerEvent> XRUISurfaceInputRouter::Cancel(const XRUIPointerSource& source)
{
	Array<XRUIPointerEvent> events;
	for (auto it = Pointers.begin(); it != Pointers.end(); ++it)
	{
		if (!(it->Source == source))
			continue;
		if (it->HasCapture)
			events.push_back(MakeEvent(XRUIPointerEventType::PrimaryCancel, source, it->CaptureSurface, it->CapturePixel));
		if (it->HasHover)
			events.push_back(MakeEvent(XRUIPointerEventType::HoverLeave, source, it->HoverSurface, it->HoverPixel));
		Pointers.erase(it);
		break;
	}
	return events;
}

XRUISurfaceInputRouter::PointerState& XRUISurfaceInputRouter::GetOrCreate(const XRUIPointerSource& source)
{
	for (PointerState& state : Pointers)
	{
		if (state.Source == source)
			return state;
	}
	PointerState state;
	state.Source = source;
	Pointers.push_back(state);
	return Pointers.back();
}
