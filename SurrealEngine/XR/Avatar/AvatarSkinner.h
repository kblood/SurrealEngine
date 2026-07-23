#pragma once

// CPU linear-blend skinning of an AvatarRig's bind pose. M1 only ever drives
// this with identity joint transforms (see Docs/FULLBODY_VR_AVATAR_PLAN.md,
// milestone M1: "no IK driving it yet"), but the joint-transform interface
// is written so the M2 IK solver can plug in real per-joint poses later
// without changing this file.

#include "AvatarAutoRig.h"
#include "Math/quaternion.h"

struct AvatarJointTransform
{
	quaternion Rotation; // relative to the joint's own bind orientation (identity = bind pose)
	vec3 Translation = vec3(0.0f); // relative to the joint's BindOrigin
};

class AvatarSkinner
{
public:
	// jointTransforms may be null (or shorter than rig.Joints) - any joint
	// without an entry skins at its bind pose (identity transform).
	static void Skin(const AvatarRig& rig, const Array<AvatarJointTransform>* jointTransforms, Array<vec3>& outPositions, Array<vec3>& outNormals);

	// M1 entry point: skins every vertex at exactly its bind-pose position.
	static void SkinBindPose(const AvatarRig& rig, Array<vec3>& outPositions, Array<vec3>& outNormals);
};
