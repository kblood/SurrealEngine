#pragma once

#include "XRCommon.h"

struct XRWeaponPoseOptions
{
	XREngineVector3 LocalOffset;
	XRQuaternion LocalRotation;
	float Scale = 5.0f;
	bool Mirror = false;
};

struct XRWeaponPoseResult
{
	bool Valid = false;
	XRHand Hand = XRHand::Right;
	XREnginePose VisualPose;
	XREngineVector3 AimDirection;
	float Scale = 5.0f;
	bool Mirror = false;
};

// The visual transform follows the grip pose while firing remains aligned to
// the independent aim pose. Local offsets use engine pose axes: +X forward,
// +Y right, and +Z up.
XRWeaponPoseResult SolveXRWeaponPose(const XREnginePose& gripPose, const XREnginePose& aimPose,
	XRHand dominantHand, const XRWeaponPoseOptions& options = {});

XRWeaponPoseResult SolveXRWeaponPose(const XRSpaceSamples& spaces, const XRWorldTransform& worldTransform,
	XRHand dominantHand, const XRWeaponPoseOptions& options = {});
