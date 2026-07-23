#pragma once

#include "XR/XRUIRuntime.h"

// Provider-neutral, world-space UI feedback geometry. Providers may render it
// with their native compositor, but every backend consumes the exact pointer
// contact that drove hover and click.
constexpr int XRUIControllerVisualCompositionOrder = 100;
constexpr int XRUIHitMarkerCompositionOrder = 600;

struct XRUIVisualSettings
{
	float ControllerBodyLengthMeters = 0.14f;
	float ControllerBodyWidthMeters = 0.045f;
	float ControllerBodyHeightMeters = 0.04f;
	float ControllerGripLengthMeters = 0.10f;
	float ControllerGripForwardOffsetMeters = -0.075f;
	float ControllerGripUpOffsetMeters = -0.065f;
	float LaserRadiusMeters = 0.0025f;
	float SelectingLaserScale = 1.6f;
	float HitMarkerRadiusMeters = 0.014f;
};

struct XRUIVisualVertex
{
	vec3 Position = vec3(0.0f);
	vec4 Color = vec4(1.0f);
};

struct XRUIHandVisual
{
	XRHand Hand = XRHand::Right;
	bool Selecting = false;
	Array<XRUIVisualVertex> Controller;
	Array<XRUIVisualVertex> Laser;
	Array<XRUIVisualVertex> HitMarker;
};

struct XRUIVisualFrame
{
	// Controllers and lasers precede opaque UI. The exact-contact marker follows
	// it, so provider compositors can keep the target truthful and visible.
	Array<XRUIHandVisual> Hands;
};

XRUIVisualFrame BuildXRUIVisualFrame(
	const std::array<XRUIPointerFeedback, XRHandCount>& feedback,
	const XRUICanvasReplayFrame& replayFrame, float worldUnitsPerMeter,
	const XRUIVisualSettings& settings = {});
