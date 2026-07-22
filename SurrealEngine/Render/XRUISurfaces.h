#pragma once

#include "Math/vec.h"
#include "Render/Presentation.h"
#include "Utils/Array.h"

#include <cstdint>

enum class XRUISurfaceKind
{
	Hud,
	Cinematic,
	Loading,
	Menu
};

enum class XRUISurfaceAnchorMode
{
	WorldFixed,
	HeadRelativeOnShow
};

// UI surfaces are composed independently of scene depth. This prevents a
// world or backing quad from hiding a menu, loading screen, or cinematic.
enum class XRUISurfaceDepthMode
{
	IgnoreWorldDepth
};

struct XRUIViewerPose
{
	vec3 Position = vec3(0.0f);
	vec3 Forward = vec3(1.0f, 0.0f, 0.0f);
	vec3 Up = vec3(0.0f, 0.0f, 1.0f);
};

struct XRUISurfacePose
{
	vec3 Center = vec3(0.0f);
	vec3 Right = vec3(0.0f, -1.0f, 0.0f);
	vec3 Up = vec3(0.0f, 0.0f, 1.0f);
	vec3 Normal = vec3(-1.0f, 0.0f, 0.0f);
};

struct XRUISurfaceDescriptor
{
	XRUISurfaceKind Kind = XRUISurfaceKind::Menu;
	PresentationLayer ContentLayer = PresentationLayer::UserInterface;
	XRUISurfaceAnchorMode AnchorMode = XRUISurfaceAnchorMode::HeadRelativeOnShow;
	XRUISurfacePose WorldPose;
	int PixelWidth = 0;
	int PixelHeight = 0;
	float PhysicalWidth = 1.4f;
	float HeadRelativeDistance = 1.5f;
	bool Interactive = false;

	float PhysicalHeight() const;
	bool HasValidExtent() const;
};

// Provides safe role defaults. Pixel dimensions remain explicit because they
// come from the engine-owned UI/cinematic canvas, not a graphics backend.
XRUISurfaceDescriptor CreateXRUISurfaceDescriptor(XRUISurfaceKind kind, int pixelWidth, int pixelHeight);

struct XRUISurfaceFrameItem
{
	XRUISurfaceDescriptor Descriptor;
	XRUISurfacePose Pose;
	int CompositionOrder = 0;
	XRUISurfaceDepthMode DepthMode = XRUISurfaceDepthMode::IgnoreWorldDepth;
};

struct XRUISurfaceFrame
{
	// Ordered back-to-front. Every item is after the world/weapon layers.
	Array<XRUISurfaceFrameItem> Surfaces;
};

class XRUISurfaceFramePolicy
{
public:
	void Configure(const XRUISurfaceDescriptor& descriptor);
	bool Show(XRUISurfaceKind kind, const XRUIViewerPose& viewerPose = {});
	void Hide(XRUISurfaceKind kind);
	bool Recenter(XRUISurfaceKind kind, const XRUIViewerPose& viewerPose);
	bool IsVisible(XRUISurfaceKind kind) const;
	XRUISurfaceFrame BuildFrame() const;

private:
	struct SurfaceState
	{
		XRUISurfaceDescriptor Descriptor;
		XRUISurfacePose Pose;
		bool Visible = false;
		bool Anchored = false;
	};

	SurfaceState* Find(XRUISurfaceKind kind);
	const SurfaceState* Find(XRUISurfaceKind kind) const;
	bool InitializeAnchor(SurfaceState& state, const XRUIViewerPose& viewerPose);

	Array<SurfaceState> States;
};

struct XRUISurfaceRay
{
	vec3 Origin = vec3(0.0f);
	vec3 Direction = vec3(0.0f);
};

struct XRUISurfaceContact
{
	bool Hit = false;
	XRUISurfaceKind Surface = XRUISurfaceKind::Menu;
	Pointf Pixel;
	vec2 UV = vec2(0.0f);
	float Distance = 0.0f;
};

// Geometry is deliberately separate from pointer lifecycle. XRCommon can
// replace the producer of XRUISurfaceRay without changing UI routing.
XRUISurfaceContact MapRayToXRUISurface(const XRUISurfaceFrameItem& surface, const XRUISurfaceRay& ray);
XRUISurfaceContact HitTestXRUISurfaces(const XRUISurfaceFrame& frame, const XRUISurfaceRay& ray);
XRUISurfaceContact MapMouseToXRUISurface(const XRUISurfaceFrameItem& surface, const Pointf& pixel);

enum class XRUIPointerSourceKind
{
	Mouse,
	Tracked
};

struct XRUIPointerSource
{
	XRUIPointerSourceKind Kind = XRUIPointerSourceKind::Mouse;
	uint64_t Id = 0;

	static XRUIPointerSource Mouse() { return {}; }
	static XRUIPointerSource Tracked(uint64_t id) { return { XRUIPointerSourceKind::Tracked, id }; }
	bool operator==(const XRUIPointerSource&) const = default;
};

enum class XRUIPointerEventType
{
	HoverEnter,
	Move,
	HoverLeave,
	PrimaryDown,
	PrimaryUp,
	PrimaryClick,
	PrimaryCancel
};

struct XRUIPointerEvent
{
	XRUIPointerEventType Type = XRUIPointerEventType::Move;
	XRUIPointerSource Source;
	XRUISurfaceKind Surface = XRUISurfaceKind::Menu;
	Pointf Pixel;
};

// Converts independently keyed mouse/tracked contacts into UI lifecycle
// events. A press can only be released by the same source that created it.
class XRUISurfaceInputRouter
{
public:
	Array<XRUIPointerEvent> Update(const XRUIPointerSource& source, const XRUISurfaceContact& contact, bool primaryPressed);
	Array<XRUIPointerEvent> Cancel(const XRUIPointerSource& source);

private:
	struct PointerState
	{
		XRUIPointerSource Source;
		bool PrimaryPressed = false;
		bool HasHover = false;
		XRUISurfaceKind HoverSurface = XRUISurfaceKind::Menu;
		Pointf HoverPixel;
		bool HasCapture = false;
		XRUISurfaceKind CaptureSurface = XRUISurfaceKind::Menu;
		Pointf CapturePixel;
	};

	PointerState& GetOrCreate(const XRUIPointerSource& source);
	Array<PointerState> Pointers;
};
