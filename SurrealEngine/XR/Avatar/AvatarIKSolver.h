#pragma once

// Pure math: rig bind pose + head/hand targets -> per-joint transforms.
// See Docs/FULLBODY_VR_AVATAR_PLAN.md, milestone M2. Everything here operates
// in whichever space the caller's targets are expressed in (this module never
// looks at a UActor/UMesh/XR provider) - AvatarRenderer is the place that
// converts real XR engine-space poses into the rig's own bind-pose (mesh
// local) space before calling Solve, because that is the space
// AvatarJoint::BindOrigin/AvatarSkinner already work in.

#include "AvatarAutoRig.h"
#include "AvatarSkinner.h"
#include "Math/vec.h"
#include "Math/quaternion.h"

// One tracked pose (head or a hand grip), already in the rig's bind-pose
// space. Forward/Up are unit direction vectors (not a full orientation
// frame) - all this solver needs an orientation for is the elbow bend hint
// and the pelvis facing direction, not vertex-accurate hand rotation.
struct AvatarIKTarget
{
	bool Valid = false;
	vec3 Position = vec3(0.0f);
	vec3 Forward = vec3(1.0f, 0.0f, 0.0f);
	vec3 Up = vec3(0.0f, 0.0f, 1.0f);
};

struct AvatarIKInput
{
	AvatarIKTarget Head;
	AvatarIKTarget LeftHand;
	AvatarIKTarget RightHand;
};

struct AvatarIKOptions
{
	// 0 = pelvis/spine stay at bind pose regardless of head motion; 1 = pelvis
	// tracks head motion 1:1 (translated by the bind-pose pelvis-to-head
	// offset). Every VRIK-family implementation surveyed in the plan doc
	// exposes this as a tunable rather than hardcoding full tracking.
	float PelvisFollowHeadWeight = 1.0f;
};

// A pose already in engine-world space (native engine axes: +X forward, +Y
// right, +Z up - the same convention XRWeaponPoseSolver's engine poses use),
// before conversion into any particular avatar's mesh-local space. Kept as
// plain engine math types (not XR types) so this header stays free of any XR
// provider dependency; AvatarRenderer is what converts a real
// XRSpaceSamples/XREnginePose sample into this.
struct AvatarEnginePose
{
	bool Valid = false;
	vec3 Position = vec3(0.0f);
	quaternion Orientation;
};

struct AvatarIKFrameInput
{
	AvatarEnginePose Head;
	AvatarEnginePose LeftHandGrip;
	AvatarEnginePose RightHandGrip;
};

class AvatarIKSolver
{
public:
	// outTransforms is resized to rig.Joints.size() and filled with one
	// AvatarJointTransform per joint, ready to hand straight to
	// AvatarSkinner::Skin. A joint whose role isn't driven by any of the
	// logic below (missing input target, missing role, degenerate bone
	// length) is left following the pelvis rigidly (or at bind pose if the
	// pelvis itself isn't driven) rather than left at a stale/garbage value.
	// Never throws, never produces NaN/Inf given finite input.
	static void Solve(const AvatarRig& rig, const AvatarIKInput& input, const AvatarIKOptions& options, Array<AvatarJointTransform>& outTransforms);
};
