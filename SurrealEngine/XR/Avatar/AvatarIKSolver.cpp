#include "AvatarIKSolver.h"

#include <cmath>

namespace
{
	constexpr float Epsilon = 1e-4f;

	int FindJoint(const AvatarRig& rig, AvatarJointRole role)
	{
		for (size_t i = 0; i < rig.Joints.size(); i++)
			if (rig.Joints[i].Role == role)
				return (int)i;
		return -1;
	}

	vec3 SafeNormalize(const vec3& v, const vec3& fallback)
	{
		float len = length(v);
		if (!std::isfinite(len) || len <= Epsilon)
			return fallback;
		return v / len;
	}

	// Applies a rigid (rotate-about-anchorBind, then translate) transform to
	// any bind-pose point that belongs to the same rigid body as anchorBind -
	// e.g. moving a point on the torso by the pelvis's own solved transform.
	vec3 ApplyRigid(const AvatarJointTransform& t, const vec3& anchorBind, const vec3& bindPos)
	{
		return t.Rotation * (bindPos - anchorBind) + anchorBind + t.Translation;
	}

	// Bind-pose torso basis (up/right/forward), derived only from this rig's
	// own joint centroids. Deliberately does not assume any global axis is
	// "up" - AvatarAutoRig's own labeling only assumes that after applying
	// mesh->meshToObject (see AvatarAutoRig.cpp), which this pure-math module
	// never touches (see AvatarIKSolver.h).
	struct TorsoBasis
	{
		bool Valid = false;
		vec3 Up = vec3(0.0f, 0.0f, 1.0f);
		vec3 Right = vec3(0.0f, 1.0f, 0.0f);
		vec3 Forward = vec3(1.0f, 0.0f, 0.0f);
	};

	TorsoBasis BuildTorsoBasis(const AvatarRig& rig, int pelvisIdx, int headRefIdx)
	{
		TorsoBasis basis;
		if (pelvisIdx < 0 || headRefIdx < 0)
			return basis;

		vec3 upRaw = rig.Joints[headRefIdx].BindOrigin - rig.Joints[pelvisIdx].BindOrigin;
		float upLen = length(upRaw);
		if (upLen <= Epsilon)
			return basis;
		vec3 up = upRaw / upLen;

		static const AvatarJointRole leftLateralRoles[] = { AvatarJointRole::LeftShoulder, AvatarJointRole::LeftUpperArm, AvatarJointRole::LeftThigh };
		static const AvatarJointRole rightLateralRoles[] = { AvatarJointRole::RightShoulder, AvatarJointRole::RightUpperArm, AvatarJointRole::RightThigh };

		vec3 lateral(0.0f);
		for (size_t i = 0; i < 3 && length(lateral) <= Epsilon; i++)
		{
			int leftIdx = FindJoint(rig, leftLateralRoles[i]);
			int rightIdx = FindJoint(rig, rightLateralRoles[i]);
			if (leftIdx >= 0 && rightIdx >= 0)
				lateral = rig.Joints[rightIdx].BindOrigin - rig.Joints[leftIdx].BindOrigin;
		}

		vec3 right = (length(lateral) > Epsilon) ?
			SafeNormalize(lateral - up * dot(lateral, up), vec3(0.0f, 1.0f, 0.0f)) :
			vec3(0.0f, 1.0f, 0.0f);
		vec3 forward = SafeNormalize(cross(up, right), vec3(1.0f, 0.0f, 0.0f));

		basis.Valid = true;
		basis.Up = up;
		basis.Right = right;
		basis.Forward = forward;
		return basis;
	}

	// Analytic two-bone IK for one arm. Leaves outTransforms untouched (so the
	// caller's rigid-torso-follow default stands) when the rig is missing a
	// role this needs or the target isn't valid.
	void SolveArm(const AvatarRig& rig, AvatarJointRole upperArmRole, AvatarJointRole forearmRole, AvatarJointRole handRole,
		const AvatarIKTarget& target, const AvatarJointTransform& torsoTransform, const vec3& torsoAnchorBind,
		const TorsoBasis& basis, float sideSign, Array<AvatarJointTransform>& outTransforms)
	{
		if (!target.Valid)
			return;

		int upperArmIdx = FindJoint(rig, upperArmRole);
		int forearmIdx = FindJoint(rig, forearmRole);
		int handIdx = FindJoint(rig, handRole);
		if (upperArmIdx < 0 || forearmIdx < 0 || handIdx < 0)
			return;

		const vec3& upperArmBind = rig.Joints[upperArmIdx].BindOrigin;
		const vec3& forearmBind = rig.Joints[forearmIdx].BindOrigin;
		const vec3& handBind = rig.Joints[handIdx].BindOrigin;

		float upperArmLen = length(forearmBind - upperArmBind);
		float forearmLen = length(handBind - forearmBind);
		if (upperArmLen <= Epsilon || forearmLen <= Epsilon)
			return; // degenerate cluster for this side - leave the rigid default rather than divide by zero

		vec3 rootLive = ApplyRigid(torsoTransform, torsoAnchorBind, upperArmBind);

		vec3 reach = target.Position - rootLive;
		float reachLen = length(reach);
		vec3 reachDir = SafeNormalize(reach, basis.Valid ? basis.Forward : vec3(1.0f, 0.0f, 0.0f));

		float maxReach = upperArmLen + forearmLen - 0.01f;
		float minReach = std::abs(upperArmLen - forearmLen) + 0.01f;
		if (maxReach < minReach) maxReach = minReach;
		float clampedReachLen = clamp(reachLen, minReach, maxReach);
		vec3 clampedTarget = rootLive + reachDir * clampedReachLen;

		float cosShoulder = clamp((upperArmLen * upperArmLen + clampedReachLen * clampedReachLen - forearmLen * forearmLen) /
			(2.0f * upperArmLen * clampedReachLen), -1.0f, 1.0f);
		float shoulderAngle = std::acos(cosShoulder);

		// Elbow bend hint: a point roughly behind and below the hand (biased
		// by the hand's own forward direction), the same "hint point behind
		// the effector" heuristic used by two-bone IK solvers generally, so
		// the elbow bends away from the target rather than through the body.
		vec3 upHint = basis.Valid ? basis.Up : vec3(0.0f, 0.0f, 1.0f);
		vec3 hintPoint = target.Position - target.Forward * (upperArmLen * 0.5f) - upHint * (upperArmLen * 0.5f);
		vec3 hintOffset = hintPoint - rootLive;

		vec3 planeNormal = cross(reachDir, hintOffset);
		float planeNormalLen = length(planeNormal);
		if (planeNormalLen <= Epsilon)
		{
			// Hint colinear with the reach direction (rare) - fall back to a
			// sideways bend on this arm's own side so the result stays finite
			// and anatomically on the correct side of the body.
			vec3 sideFallback = (basis.Valid ? basis.Right : vec3(0.0f, 1.0f, 0.0f)) * sideSign;
			planeNormal = cross(reachDir, sideFallback);
			planeNormalLen = length(planeNormal);
		}
		if (planeNormalLen <= Epsilon)
		{
			planeNormal = cross(reachDir, vec3(0.0f, 0.0f, 1.0f));
			planeNormalLen = length(planeNormal);
		}
		vec3 unitPlaneNormal = (planeNormalLen > Epsilon) ? planeNormal / planeNormalLen : vec3(0.0f, 0.0f, 1.0f);
		vec3 bendDir = SafeNormalize(cross(unitPlaneNormal, reachDir), vec3(0.0f, 0.0f, 1.0f));

		vec3 upperArmDir = SafeNormalize(reachDir * std::cos(shoulderAngle) + bendDir * std::sin(shoulderAngle), reachDir);
		vec3 elbowPos = rootLive + upperArmDir * upperArmLen;
		vec3 forearmDir = SafeNormalize(clampedTarget - elbowPos, upperArmDir);

		vec3 bindUpperArmDir = (forearmBind - upperArmBind) / upperArmLen;
		vec3 bindForearmDir = (handBind - forearmBind) / forearmLen;

		AvatarJointTransform upperArmTransform;
		upperArmTransform.Rotation = rotation_between(bindUpperArmDir, upperArmDir);
		upperArmTransform.Translation = rootLive - upperArmBind;

		AvatarJointTransform forearmTransform;
		forearmTransform.Rotation = rotation_between(bindForearmDir, forearmDir);
		forearmTransform.Translation = elbowPos - forearmBind;

		AvatarJointTransform handTransform;
		handTransform.Rotation = forearmTransform.Rotation;
		handTransform.Translation = clampedTarget - handBind;

		outTransforms[upperArmIdx] = upperArmTransform;
		outTransforms[forearmIdx] = forearmTransform;
		outTransforms[handIdx] = handTransform;
	}
}

void AvatarIKSolver::Solve(const AvatarRig& rig, const AvatarIKInput& input, const AvatarIKOptions& options, Array<AvatarJointTransform>& outTransforms)
{
	size_t n = rig.Joints.size();
	outTransforms.clear();
	outTransforms.resize(n);
	if (n == 0)
		return;

	int pelvisIdx = FindJoint(rig, AvatarJointRole::Pelvis);

	static const AvatarJointRole headChainRoles[] = { AvatarJointRole::Head, AvatarJointRole::Neck, AvatarJointRole::Chest, AvatarJointRole::Spine };
	int headRefIdx = -1;
	for (AvatarJointRole role : headChainRoles)
	{
		int idx = FindJoint(rig, role);
		if (idx >= 0) { headRefIdx = idx; break; }
	}

	float torsoLength = (pelvisIdx >= 0 && headRefIdx >= 0) ?
		length(rig.Joints[headRefIdx].BindOrigin - rig.Joints[pelvisIdx].BindOrigin) : 0.0f;

	AvatarJointTransform pelvisTransform; // identity/zero by default (bind pose)
	bool pelvisDriven = false;
	TorsoBasis basis;

	if (pelvisIdx >= 0 && headRefIdx >= 0 && torsoLength > Epsilon && input.Head.Valid)
	{
		const vec3& pelvisBind = rig.Joints[pelvisIdx].BindOrigin;
		const vec3& refBind = rig.Joints[headRefIdx].BindOrigin;
		vec3 bindOffset = pelvisBind - refBind;
		float w = clamp(options.PelvisFollowHeadWeight, 0.0f, 1.0f);
		vec3 fullyTrackedPelvisPos = input.Head.Position + bindOffset;
		vec3 pelvisPos = mix(pelvisBind, fullyTrackedPelvisPos, w);

		basis = BuildTorsoBasis(rig, pelvisIdx, headRefIdx);
		float yaw = 0.0f;
		if (basis.Valid)
		{
			vec3 headForwardProj = input.Head.Forward - basis.Up * dot(input.Head.Forward, basis.Up);
			float projLen = length(headForwardProj);
			if (projLen > Epsilon)
			{
				headForwardProj = headForwardProj / projLen;
				float cosA = clamp(dot(basis.Forward, headForwardProj), -1.0f, 1.0f);
				float sinA = dot(cross(basis.Forward, headForwardProj), basis.Up);
				yaw = std::atan2(sinA, cosA) * w;
			}
		}

		pelvisTransform.Rotation = (std::abs(yaw) > 1e-6f) ? quaternion(yaw, basis.Up) : quaternion();
		pelvisTransform.Translation = pelvisPos - pelvisBind;
		pelvisDriven = true;
	}

	// Default: every joint rigidly follows the pelvis's solved transform (or
	// stays at bind pose if the pelvis itself isn't driven) unless overridden
	// below - this is the "sensible default" fallback for anything this
	// milestone doesn't explicitly solve (shoulders, legs, or an arm missing
	// a role/target).
	for (size_t i = 0; i < n; i++)
		outTransforms[i] = pelvisTransform;

	if (pelvisDriven)
	{
		const vec3& pelvisBind = rig.Joints[pelvisIdx].BindOrigin;
		vec3 pelvisPos = pelvisBind + pelvisTransform.Translation;
		vec3 headPos = input.Head.Position;

		static const AvatarJointRole torsoChain[] = { AvatarJointRole::Spine, AvatarJointRole::Chest, AvatarJointRole::Neck };
		for (AvatarJointRole role : torsoChain)
		{
			int idx = FindJoint(rig, role);
			if (idx < 0)
				continue;
			float t = clamp(length(rig.Joints[idx].BindOrigin - pelvisBind) / torsoLength, 0.0f, 1.0f);
			vec3 pos = mix(pelvisPos, headPos, t);
			outTransforms[idx].Rotation = pelvisTransform.Rotation;
			outTransforms[idx].Translation = pos - rig.Joints[idx].BindOrigin;
		}

		if (rig.Joints[headRefIdx].Role == AvatarJointRole::Head)
		{
			outTransforms[headRefIdx].Rotation = quaternion();
			outTransforms[headRefIdx].Translation = headPos - rig.Joints[headRefIdx].BindOrigin;
		}
	}

	// Arms attach to the chest when the rig has one (a torso lean moves the
	// arm root with it); otherwise fall back to the pelvis, then to no motion
	// at all if neither role exists.
	int chestIdx = FindJoint(rig, AvatarJointRole::Chest);
	AvatarJointTransform armAnchorTransform;
	vec3 armAnchorBind(0.0f);
	if (chestIdx >= 0 && pelvisDriven)
	{
		armAnchorTransform = outTransforms[chestIdx];
		armAnchorBind = rig.Joints[chestIdx].BindOrigin;
	}
	else if (pelvisIdx >= 0)
	{
		armAnchorTransform = pelvisTransform;
		armAnchorBind = rig.Joints[pelvisIdx].BindOrigin;
	}

	SolveArm(rig, AvatarJointRole::LeftUpperArm, AvatarJointRole::LeftForearm, AvatarJointRole::LeftHand,
		input.LeftHand, armAnchorTransform, armAnchorBind, basis, -1.0f, outTransforms);
	SolveArm(rig, AvatarJointRole::RightUpperArm, AvatarJointRole::RightForearm, AvatarJointRole::RightHand,
		input.RightHand, armAnchorTransform, armAnchorBind, basis, 1.0f, outTransforms);
}
