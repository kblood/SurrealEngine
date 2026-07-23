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

	// Pelvis solve, factored out of Solve() so EstimateLegRoots/SolveWithLegs
	// can derive the same live hip positions Solve() itself uses for the
	// torso/arms, without duplicating the arithmetic.
	struct PelvisSolve
	{
		bool Driven = false;
		int PelvisIdx = -1;
		int HeadRefIdx = -1;
		float TorsoLength = 0.0f;
		AvatarJointTransform Transform; // identity/zero (bind pose) if !Driven
		TorsoBasis Basis;
	};

	PelvisSolve SolvePelvis(const AvatarRig& rig, const AvatarIKInput& input, const AvatarIKOptions& options)
	{
		PelvisSolve result;
		result.PelvisIdx = FindJoint(rig, AvatarJointRole::Pelvis);

		static const AvatarJointRole headChainRoles[] = { AvatarJointRole::Head, AvatarJointRole::Neck, AvatarJointRole::Chest, AvatarJointRole::Spine };
		for (AvatarJointRole role : headChainRoles)
		{
			int idx = FindJoint(rig, role);
			if (idx >= 0) { result.HeadRefIdx = idx; break; }
		}

		if (result.PelvisIdx < 0 || result.HeadRefIdx < 0)
			return result;

		result.TorsoLength = length(rig.Joints[result.HeadRefIdx].BindOrigin - rig.Joints[result.PelvisIdx].BindOrigin);
		if (result.TorsoLength <= Epsilon || !input.Head.Valid)
			return result;

		const vec3& pelvisBind = rig.Joints[result.PelvisIdx].BindOrigin;
		const vec3& refBind = rig.Joints[result.HeadRefIdx].BindOrigin;
		vec3 bindOffset = pelvisBind - refBind;
		float w = clamp(options.PelvisFollowHeadWeight, 0.0f, 1.0f);
		vec3 fullyTrackedPelvisPos = input.Head.Position + bindOffset;
		vec3 pelvisPos = mix(pelvisBind, fullyTrackedPelvisPos, w);

		result.Basis = BuildTorsoBasis(rig, result.PelvisIdx, result.HeadRefIdx);
		float yaw = 0.0f;
		if (result.Basis.Valid)
		{
			vec3 headForwardProj = input.Head.Forward - result.Basis.Up * dot(input.Head.Forward, result.Basis.Up);
			float projLen = length(headForwardProj);
			if (projLen > Epsilon)
			{
				headForwardProj = headForwardProj / projLen;
				float cosA = clamp(dot(result.Basis.Forward, headForwardProj), -1.0f, 1.0f);
				float sinA = dot(cross(result.Basis.Forward, headForwardProj), result.Basis.Up);
				yaw = std::atan2(sinA, cosA) * w;
			}
		}

		result.Transform.Rotation = (std::abs(yaw) > 1e-6f) ? quaternion(yaw, result.Basis.Up) : quaternion();
		result.Transform.Translation = pelvisPos - pelvisBind;
		result.Driven = true;
		return result;
	}

	// Analytic two-bone IK for one leg, the same law-of-cosines construction
	// as SolveArm - the knee's bend hint is simply the torso's own forward
	// direction (this rig has no tracked knee/foot orientation input the way
	// a hand has a controller), which keeps knees bending anatomically
	// forward rather than to a side. Leaves outTransforms untouched (so the
	// caller's rigid-torso-follow default stands) when the rig is missing a
	// role this needs.
	void SolveLeg(const AvatarRig& rig, AvatarJointRole thighRole, AvatarJointRole calfRole, AvatarJointRole footRole,
		const AvatarLegGroundProbe& groundProbe, bool overallGrounded, const AvatarJointTransform& pelvisTransform,
		const vec3& pelvisBind, const TorsoBasis& basis, float sideSign, float deltaTimeSeconds,
		AvatarLegStepState& state, Array<AvatarJointTransform>& outTransforms)
	{
		int thighIdx = FindJoint(rig, thighRole);
		int calfIdx = FindJoint(rig, calfRole);
		int footIdx = FindJoint(rig, footRole);
		if (thighIdx < 0 || calfIdx < 0 || footIdx < 0)
			return; // incomplete leg chain - leave the pelvis-follow default (same as a missing arm role)

		const vec3& thighBind = rig.Joints[thighIdx].BindOrigin;
		const vec3& calfBind = rig.Joints[calfIdx].BindOrigin;
		const vec3& footBind = rig.Joints[footIdx].BindOrigin;

		float thighLen = length(calfBind - thighBind);
		float calfLen = length(footBind - calfBind);
		if (thighLen <= Epsilon || calfLen <= Epsilon)
			return; // degenerate cluster for this leg - leave the rigid default rather than divide by zero

		const vec3& up = basis.Up;
		vec3 hipLive = ApplyRigid(pelvisTransform, pelvisBind, thighBind);
		float legLength = thighLen + calfLen;

		bool grounded = overallGrounded && groundProbe.Valid;

		// Ease GroundedBlend toward 0/1 rather than snapping, so airborne <->
		// grounded transitions blend (plan doc M3: "blend to a neutral
		// hanging-leg pose while airborne").
		const float blendRatePerSecond = 4.0f; // full transition in ~0.25s
		float targetBlend = grounded ? 1.0f : 0.0f;
		float maxStep = blendRatePerSecond * std::max(deltaTimeSeconds, 0.0f);
		if (state.GroundedBlend < targetBlend)
			state.GroundedBlend = std::min(targetBlend, state.GroundedBlend + maxStep);
		else if (state.GroundedBlend > targetBlend)
			state.GroundedBlend = std::max(targetBlend, state.GroundedBlend - maxStep);

		// Neutral hanging pose: leg extended nearly straight down from the
		// hip (not fully straight, so the two-bone solve below always has a
		// well-defined bend direction) - used whenever airborne/falling/
		// swimming, so a ground probe is never trusted for a foot that has
		// nowhere real to stand.
		vec3 neutralFootPos = hipLive - up * (legLength * 0.95f);

		// While airborne, freeze at the last known planted/stepping position
		// rather than jumping straight to the neutral pose - GroundedBlend
		// (eased above) is what actually fades this toward neutralFootPos
		// over time, so an airborne transition blends instead of snapping.
		vec3 steppedFootPos = state.Planted ? state.PlantedGround : neutralFootPos;
		if (grounded)
		{
			// Foot should end up under the hip's own horizontal position, at
			// the probed ground height - the single-support-under-hip model
			// simple VRIK-family foot IK uses absent a tracked foot input
			// (plan doc M3 / Godot-XR-Avatar reference).
			float groundHeight = dot(groundProbe.GroundPoint, up);
			vec3 desiredFootPos = hipLive - up * (dot(hipLive, up) - groundHeight);

			if (!state.Planted)
			{
				state.PlantedGround = desiredFootPos;
				state.StepStartGround = desiredFootPos;
				state.StepTargetGround = desiredFootPos;
				state.Planted = true;
				state.Phase = AvatarLegStepPhase::Idle;
			}

			// Step-distance threshold derived from this leg's own measured
			// length (plan doc M3), not a hardcoded constant.
			float stepThreshold = legLength * 0.45f;
			float teleportThreshold = legLength * 3.0f; // a jump this big is a respawn/map change, not a walking step

			vec3 delta = desiredFootPos - state.PlantedGround;
			vec3 deltaHoriz = delta - up * dot(delta, up);
			float horizDist = length(deltaHoriz);

			if (state.Phase == AvatarLegStepPhase::Idle)
			{
				if (horizDist > teleportThreshold)
				{
					state.PlantedGround = desiredFootPos;
				}
				else if (horizDist > stepThreshold)
				{
					state.Phase = (dot(deltaHoriz, basis.Forward) >= 0.0f) ? AvatarLegStepPhase::SteppingForward : AvatarLegStepPhase::SteppingBack;
					state.StepStartGround = state.PlantedGround;
					state.StepTargetGround = desiredFootPos;
					state.StepT = 0.0f;
				}
			}
			else
			{
				const float stepDurationSeconds = 0.30f;
				state.StepT = clamp(state.StepT + deltaTimeSeconds / stepDurationSeconds, 0.0f, 1.0f);
				if (state.StepT >= 1.0f)
				{
					state.PlantedGround = state.StepTargetGround;
					state.Phase = AvatarLegStepPhase::Idle;
					state.StepT = 0.0f;
				}
			}

			if (state.Phase == AvatarLegStepPhase::Idle)
			{
				steppedFootPos = state.PlantedGround;
			}
			else
			{
				float t = state.StepT;
				float eased = t * t * (3.0f - 2.0f * t); // smoothstep
				vec3 lin = mix(state.StepStartGround, state.StepTargetGround, eased);
				float liftHeight = legLength * 0.12f;
				steppedFootPos = lin + up * (std::sin(3.14159265f * t) * liftHeight);
			}
		}
		// Airborne: don't advance or re-plant the step machine at all - just
		// let GroundedBlend fade the output toward the neutral hang pose
		// below, rather than holding a stale ground contact.

		vec3 footTarget = mix(neutralFootPos, steppedFootPos, state.GroundedBlend);

		vec3 reach = footTarget - hipLive;
		float reachLen = length(reach);
		vec3 reachDir = SafeNormalize(reach, -up);

		float maxReach = legLength - 0.01f;
		float minReach = std::abs(thighLen - calfLen) + 0.01f;
		if (maxReach < minReach) maxReach = minReach;
		float clampedReachLen = clamp(reachLen, minReach, maxReach);
		vec3 clampedTarget = hipLive + reachDir * clampedReachLen;

		float cosHip = clamp((thighLen * thighLen + clampedReachLen * clampedReachLen - calfLen * calfLen) /
			(2.0f * thighLen * clampedReachLen), -1.0f, 1.0f);
		float hipAngle = std::acos(cosHip);

		vec3 hintOffset = basis.Forward * thighLen;
		vec3 planeNormal = cross(reachDir, hintOffset);
		float planeNormalLen = length(planeNormal);
		if (planeNormalLen <= Epsilon)
		{
			vec3 sideFallback = basis.Right * sideSign;
			planeNormal = cross(reachDir, sideFallback);
			planeNormalLen = length(planeNormal);
		}
		if (planeNormalLen <= Epsilon)
		{
			planeNormal = cross(reachDir, vec3(0.0f, 1.0f, 0.0f));
			planeNormalLen = length(planeNormal);
		}
		vec3 unitPlaneNormal = (planeNormalLen > Epsilon) ? planeNormal / planeNormalLen : vec3(0.0f, 1.0f, 0.0f);
		vec3 bendDir = SafeNormalize(cross(unitPlaneNormal, reachDir), basis.Forward);

		vec3 thighDir = SafeNormalize(reachDir * std::cos(hipAngle) + bendDir * std::sin(hipAngle), reachDir);
		vec3 kneePos = hipLive + thighDir * thighLen;
		vec3 calfDir = SafeNormalize(clampedTarget - kneePos, thighDir);

		vec3 bindThighDir = (calfBind - thighBind) / thighLen;
		vec3 bindCalfDir = (footBind - calfBind) / calfLen;

		AvatarJointTransform thighTransform;
		thighTransform.Rotation = rotation_between(bindThighDir, thighDir);
		thighTransform.Translation = hipLive - thighBind;

		AvatarJointTransform calfTransform;
		calfTransform.Rotation = rotation_between(bindCalfDir, calfDir);
		calfTransform.Translation = kneePos - calfBind;

		AvatarJointTransform footTransform;
		footTransform.Rotation = calfTransform.Rotation;
		footTransform.Translation = clampedTarget - footBind;

		outTransforms[thighIdx] = thighTransform;
		outTransforms[calfIdx] = calfTransform;
		outTransforms[footIdx] = footTransform;
	}
}

void AvatarIKSolver::Solve(const AvatarRig& rig, const AvatarIKInput& input, const AvatarIKOptions& options, Array<AvatarJointTransform>& outTransforms)
{
	size_t n = rig.Joints.size();
	outTransforms.clear();
	outTransforms.resize(n);
	if (n == 0)
		return;

	PelvisSolve pelvis = SolvePelvis(rig, input, options);

	// Default: every joint rigidly follows the pelvis's solved transform (or
	// stays at bind pose if the pelvis itself isn't driven) unless overridden
	// below - this is the "sensible default" fallback for anything this
	// milestone doesn't explicitly solve (shoulders, or an arm/leg missing a
	// role/target).
	for (size_t i = 0; i < n; i++)
		outTransforms[i] = pelvis.Transform;

	if (pelvis.Driven)
	{
		const vec3& pelvisBind = rig.Joints[pelvis.PelvisIdx].BindOrigin;
		vec3 pelvisPos = pelvisBind + pelvis.Transform.Translation;
		vec3 headPos = input.Head.Position;

		static const AvatarJointRole torsoChain[] = { AvatarJointRole::Spine, AvatarJointRole::Chest, AvatarJointRole::Neck };
		for (AvatarJointRole role : torsoChain)
		{
			int idx = FindJoint(rig, role);
			if (idx < 0)
				continue;
			float t = clamp(length(rig.Joints[idx].BindOrigin - pelvisBind) / pelvis.TorsoLength, 0.0f, 1.0f);
			vec3 pos = mix(pelvisPos, headPos, t);
			outTransforms[idx].Rotation = pelvis.Transform.Rotation;
			outTransforms[idx].Translation = pos - rig.Joints[idx].BindOrigin;
		}

		if (rig.Joints[pelvis.HeadRefIdx].Role == AvatarJointRole::Head)
		{
			outTransforms[pelvis.HeadRefIdx].Rotation = quaternion();
			outTransforms[pelvis.HeadRefIdx].Translation = headPos - rig.Joints[pelvis.HeadRefIdx].BindOrigin;
		}
	}

	// Arms attach to the chest when the rig has one (a torso lean moves the
	// arm root with it); otherwise fall back to the pelvis, then to no motion
	// at all if neither role exists.
	int chestIdx = FindJoint(rig, AvatarJointRole::Chest);
	AvatarJointTransform armAnchorTransform;
	vec3 armAnchorBind(0.0f);
	if (chestIdx >= 0 && pelvis.Driven)
	{
		armAnchorTransform = outTransforms[chestIdx];
		armAnchorBind = rig.Joints[chestIdx].BindOrigin;
	}
	else if (pelvis.PelvisIdx >= 0)
	{
		armAnchorTransform = pelvis.Transform;
		armAnchorBind = rig.Joints[pelvis.PelvisIdx].BindOrigin;
	}

	SolveArm(rig, AvatarJointRole::LeftUpperArm, AvatarJointRole::LeftForearm, AvatarJointRole::LeftHand,
		input.LeftHand, armAnchorTransform, armAnchorBind, pelvis.Basis, -1.0f, outTransforms);
	SolveArm(rig, AvatarJointRole::RightUpperArm, AvatarJointRole::RightForearm, AvatarJointRole::RightHand,
		input.RightHand, armAnchorTransform, armAnchorBind, pelvis.Basis, 1.0f, outTransforms);
}

AvatarLegRootEstimate AvatarIKSolver::EstimateLegRoots(const AvatarRig& rig, const AvatarIKInput& input, const AvatarIKOptions& options)
{
	AvatarLegRootEstimate result;

	PelvisSolve pelvis = SolvePelvis(rig, input, options);
	result.PelvisDriven = pelvis.Driven;
	if (!pelvis.Driven)
		return result;

	if (pelvis.Basis.Valid)
	{
		result.Up = pelvis.Basis.Up;
		result.Forward = pelvis.Basis.Forward;
	}

	const vec3& pelvisBind = rig.Joints[pelvis.PelvisIdx].BindOrigin;

	auto estimateLeg = [&](AvatarJointRole thighRole, AvatarJointRole calfRole, AvatarJointRole footRole, bool& outValid, vec3& outHip, float& outLength)
	{
		int thighIdx = FindJoint(rig, thighRole);
		int calfIdx = FindJoint(rig, calfRole);
		int footIdx = FindJoint(rig, footRole);
		if (thighIdx < 0 || calfIdx < 0 || footIdx < 0)
			return;

		const vec3& thighBind = rig.Joints[thighIdx].BindOrigin;
		float legLength = length(rig.Joints[calfIdx].BindOrigin - thighBind) + length(rig.Joints[footIdx].BindOrigin - rig.Joints[calfIdx].BindOrigin);
		if (legLength <= Epsilon)
			return;

		outHip = ApplyRigid(pelvis.Transform, pelvisBind, thighBind);
		outLength = legLength;
		outValid = true;
	};

	estimateLeg(AvatarJointRole::LeftThigh, AvatarJointRole::LeftCalf, AvatarJointRole::LeftFoot, result.LeftValid, result.LeftHipMeshLocal, result.LeftLegLength);
	estimateLeg(AvatarJointRole::RightThigh, AvatarJointRole::RightCalf, AvatarJointRole::RightFoot, result.RightValid, result.RightHipMeshLocal, result.RightLegLength);

	return result;
}

void AvatarIKSolver::SolveWithLegs(const AvatarRig& rig, const AvatarIKInput& input, const AvatarIKOptions& options,
	float deltaTimeSeconds, AvatarLegIKState& legState, Array<AvatarJointTransform>& outTransforms)
{
	Solve(rig, input, options, outTransforms);

	PelvisSolve pelvis = SolvePelvis(rig, input, options);
	if (!pelvis.Driven || !pelvis.Basis.Valid)
		return; // legs left at whatever Solve() already put there (pelvis-follow default, or bind pose)

	const vec3& pelvisBind = rig.Joints[pelvis.PelvisIdx].BindOrigin;

	SolveLeg(rig, AvatarJointRole::LeftThigh, AvatarJointRole::LeftCalf, AvatarJointRole::LeftFoot,
		input.LeftFootGround, input.Grounded, pelvis.Transform, pelvisBind, pelvis.Basis, -1.0f,
		deltaTimeSeconds, legState.Left, outTransforms);
	SolveLeg(rig, AvatarJointRole::RightThigh, AvatarJointRole::RightCalf, AvatarJointRole::RightFoot,
		input.RightFootGround, input.Grounded, pelvis.Transform, pelvisBind, pelvis.Basis, 1.0f,
		deltaTimeSeconds, legState.Right, outTransforms);
}
