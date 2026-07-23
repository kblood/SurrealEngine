#pragma once

#include "XRCommon.h"

enum class XRWeaponVisualAnchor : uint8_t
{
	Aim,
	Grip
};

struct XRWeaponPoseOptions
{
	XREngineVector3 LocalOffset;
	XRQuaternion LocalRotation;
	float Scale = 5.0f;
	bool Mirror = false;
	XRWeaponVisualAnchor VisualAnchor = XRWeaponVisualAnchor::Aim;
};

struct XRWeaponPoseResult
{
	bool Valid = false;
	XRHand Hand = XRHand::Right;
	XREnginePose VisualPose;
	XREngineVector3 AimDirection;
	float Scale = 5.0f;
	bool Mirror = false;
	XRWeaponVisualAnchor VisualAnchor = XRWeaponVisualAnchor::Aim;
};

// Visuals follow the aim pose by default, matching the established hardware
// baseline. Grip anchoring is explicit; firing always follows the independent
// aim pose. Local offsets use engine pose axes: +X forward, +Y right, +Z up.
XRWeaponPoseResult SolveXRWeaponPose(const XREnginePose& gripPose, const XREnginePose& aimPose,
	XRHand dominantHand, const XRWeaponPoseOptions& options = {});

XRWeaponPoseResult SolveXRWeaponPose(const XRSpaceSamples& spaces, const XRWorldTransform& worldTransform,
	XRHand dominantHand, const XRWeaponPoseOptions& options = {});
