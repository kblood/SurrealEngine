#include "XR/Avatar/AvatarIKSolver.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	bool NearlyEqual(float a, float b, float epsilon)
	{
		return std::abs(a - b) <= epsilon;
	}

	bool NearlyEqual(const vec3& a, const vec3& b, float epsilon)
	{
		return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon) && NearlyEqual(a.z, b.z, epsilon);
	}

	bool IsFinite(const vec3& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}

	bool IsFinite(const quaternion& q)
	{
		return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
	}

	// A small synthetic biped, deliberately bound with its torso running
	// along +Y (not +Z) and arms reaching along -Z in the bind pose, so any
	// test that passes here could not be passing by accident of a hardcoded
	// Z-is-up assumption in the solver.
	int AddJoint(AvatarRig& rig, AvatarJointRole role, vec3 origin)
	{
		AvatarJoint joint;
		joint.Role = role;
		joint.Parent = -1; // unused by AvatarIKSolver (per-joint skinning has no parent chain)
		joint.BindOrigin = origin;
		joint.VertexCount = 0;
		rig.Joints.push_back(joint);
		return (int)rig.Joints.size() - 1;
	}

	AvatarRig BuildTestRig(bool includeRightArm = true)
	{
		AvatarRig rig;
		rig.Valid = true;
		AddJoint(rig, AvatarJointRole::Pelvis, vec3(0.0f, 0.0f, 0.0f));
		AddJoint(rig, AvatarJointRole::Spine, vec3(0.0f, 20.0f, 0.0f));
		AddJoint(rig, AvatarJointRole::Chest, vec3(0.0f, 40.0f, 0.0f));
		AddJoint(rig, AvatarJointRole::Neck, vec3(0.0f, 55.0f, 0.0f));
		AddJoint(rig, AvatarJointRole::Head, vec3(0.0f, 65.0f, 0.0f));
		AddJoint(rig, AvatarJointRole::LeftUpperArm, vec3(-15.0f, 40.0f, 0.0f));
		AddJoint(rig, AvatarJointRole::LeftForearm, vec3(-15.0f, 40.0f, -25.0f));
		AddJoint(rig, AvatarJointRole::LeftHand, vec3(-15.0f, 40.0f, -50.0f));
		if (includeRightArm)
		{
			AddJoint(rig, AvatarJointRole::RightUpperArm, vec3(15.0f, 40.0f, 0.0f));
			AddJoint(rig, AvatarJointRole::RightForearm, vec3(15.0f, 40.0f, -25.0f));
			AddJoint(rig, AvatarJointRole::RightHand, vec3(15.0f, 40.0f, -50.0f));
		}
		return rig;
	}

	int FindJointIndex(const AvatarRig& rig, AvatarJointRole role)
	{
		for (size_t i = 0; i < rig.Joints.size(); i++)
			if (rig.Joints[i].Role == role)
				return (int)i;
		return -1;
	}

	vec3 SolvedPosition(const AvatarRig& rig, const Array<AvatarJointTransform>& transforms, AvatarJointRole role)
	{
		int idx = FindJointIndex(rig, role);
		if (idx < 0)
			throw std::runtime_error("role missing from test rig");
		return rig.Joints[idx].BindOrigin + transforms[idx].Translation;
	}

	AvatarIKTarget MakeTarget(vec3 position, vec3 forward = vec3(0.0f, 0.0f, -1.0f), vec3 up = vec3(0.0f, 1.0f, 0.0f))
	{
		AvatarIKTarget target;
		target.Valid = true;
		target.Position = position;
		target.Forward = normalize(forward);
		target.Up = normalize(up);
		return target;
	}

	void TestNoInputLeavesExactBindPose()
	{
		AvatarRig rig = BuildTestRig();
		AvatarIKInput input; // everything invalid/default
		AvatarIKOptions options;
		Array<AvatarJointTransform> transforms;
		AvatarIKSolver::Solve(rig, input, options, transforms);

		Require(transforms.size() == rig.Joints.size(), "output size did not match joint count");
		for (size_t i = 0; i < transforms.size(); i++)
		{
			Require(NearlyEqual(transforms[i].Translation, vec3(0.0f), 0.00001f), "untracked solve moved a joint away from bind pose");
			Require(transforms[i].Rotation == quaternion(), "untracked solve rotated a joint away from bind pose");
		}
	}

	void TestHandTracksReachableTarget()
	{
		AvatarRig rig = BuildTestRig();
		AvatarIKInput input;
		input.Head = MakeTarget(vec3(0.0f, 65.0f, 0.0f)); // head at its own bind position - pelvis shouldn't move
		vec3 leftTarget(-25.0f, 30.0f, -30.0f);
		vec3 rightTarget(20.0f, 45.0f, -35.0f);
		input.LeftHand = MakeTarget(leftTarget);
		input.RightHand = MakeTarget(rightTarget);
		AvatarIKOptions options;
		Array<AvatarJointTransform> transforms;
		AvatarIKSolver::Solve(rig, input, options, transforms);

		vec3 solvedLeft = SolvedPosition(rig, transforms, AvatarJointRole::LeftHand);
		vec3 solvedRight = SolvedPosition(rig, transforms, AvatarJointRole::RightHand);
		float leftError = length(solvedLeft - leftTarget);
		float rightError = length(solvedRight - rightTarget);
		std::cout << "  hand tracking error: left=" << leftError << " right=" << rightError << " units\n";
		Require(leftError < 0.01f, "solved left hand position did not track a reachable target");
		Require(rightError < 0.01f, "solved right hand position did not track a reachable target");

		// Elbow must be a real bend (not the bone collapsed straight), and
		// both bones must reach their own measured length.
		vec3 elbow = SolvedPosition(rig, transforms, AvatarJointRole::LeftForearm);
		vec3 root = SolvedPosition(rig, transforms, AvatarJointRole::LeftUpperArm);
		float upperLen = length(elbow - root);
		float forearmLen = length(solvedLeft - elbow);
		Require(NearlyEqual(upperLen, 25.0f, 0.01f), "solved upper arm length drifted from the rig's own measured length");
		Require(NearlyEqual(forearmLen, 25.0f, 0.01f), "solved forearm length drifted from the rig's own measured length");
	}

	void TestHandClampsUnreachableTarget()
	{
		AvatarRig rig = BuildTestRig();
		AvatarIKInput input;
		input.LeftHand = MakeTarget(vec3(-300.0f, 40.0f, 0.0f)); // far beyond the 50-unit arm span
		AvatarIKOptions options;
		Array<AvatarJointTransform> transforms;
		AvatarIKSolver::Solve(rig, input, options, transforms);

		vec3 root(-15.0f, 40.0f, 0.0f);
		vec3 solvedLeft = SolvedPosition(rig, transforms, AvatarJointRole::LeftHand);
		Require(IsFinite(solvedLeft), "unreachable target produced a non-finite hand position");
		float reach = length(solvedLeft - root);
		std::cout << "  clamped reach for an unreachable target: " << reach << " units (arm span 50)\n";
		Require(reach < 50.0f && reach > 40.0f, "unreachable target was not clamped to the arm's own measured span");
	}

	void TestPelvisFollowsHeadProportionally()
	{
		AvatarRig rig = BuildTestRig();
		vec3 headDelta(5.0f, 12.0f, -3.0f);
		vec3 headBind(0.0f, 65.0f, 0.0f);

		for (float weight : { 0.0f, 0.5f, 1.0f })
		{
			AvatarIKInput input;
			input.Head = MakeTarget(headBind + headDelta);
			AvatarIKOptions options;
			options.PelvisFollowHeadWeight = weight;
			Array<AvatarJointTransform> transforms;
			AvatarIKSolver::Solve(rig, input, options, transforms);

			vec3 solvedPelvis = SolvedPosition(rig, transforms, AvatarJointRole::Pelvis);
			vec3 expectedDelta = headDelta * weight;
			float error = length((solvedPelvis - vec3(0.0f)) - expectedDelta);
			std::cout << "  pelvis follow weight=" << weight << " delta error=" << error << " units\n";
			Require(error < 0.01f, "pelvis motion was not exactly proportional to head motion by the follow weight");
		}
	}

	void TestSpineInterpolatesBetweenPelvisAndHead()
	{
		AvatarRig rig = BuildTestRig();
		vec3 headTarget(10.0f, 90.0f, -8.0f); // head moved up, forward, and sideways
		AvatarIKInput input;
		input.Head = MakeTarget(headTarget);
		AvatarIKOptions options;
		Array<AvatarJointTransform> transforms;
		AvatarIKSolver::Solve(rig, input, options, transforms);

		vec3 pelvis = SolvedPosition(rig, transforms, AvatarJointRole::Pelvis);
		vec3 spine = SolvedPosition(rig, transforms, AvatarJointRole::Spine);
		vec3 chest = SolvedPosition(rig, transforms, AvatarJointRole::Chest);
		vec3 neck = SolvedPosition(rig, transforms, AvatarJointRole::Neck);
		vec3 head = SolvedPosition(rig, transforms, AvatarJointRole::Head);

		Require(length(head - headTarget) < 0.01f, "head joint did not track its own target");
		// Height (the rig's own bind-pose "up", which here is +Y) must climb
		// monotonically from pelvis to head - proof spine/chest/neck actually
		// interpolate rather than snapping to one end.
		Require(pelvis.y < spine.y && spine.y < chest.y && chest.y < neck.y && neck.y < head.y,
			"spine/chest/neck did not interpolate monotonically between pelvis and head");
	}

	void TestElbowStaysOnAnatomicalSide()
	{
		AvatarRig rig = BuildTestRig();
		AvatarIKInput input;
		input.Head = MakeTarget(vec3(0.0f, 65.0f, 0.0f));
		// A clearly bent pose: hand pulled in close to the chest, well short
		// of full reach, so the solver must actually bend the elbow rather
		// than leave the arm straight.
		input.LeftHand = MakeTarget(vec3(-10.0f, 42.0f, -15.0f));
		AvatarIKOptions options;
		Array<AvatarJointTransform> transforms;
		AvatarIKSolver::Solve(rig, input, options, transforms);

		vec3 elbow = SolvedPosition(rig, transforms, AvatarJointRole::LeftForearm);
		std::cout << "  left elbow position for a bent-arm pose: (" << elbow.x << ", " << elbow.y << ", " << elbow.z << ")\n";
		// Left arm's own side is negative X (see BuildTestRig) - the elbow
		// must stay on that side, not cross through the spine (x == 0) into
		// the opposite/right side of the body.
		Require(elbow.x < -2.0f, "elbow crossed toward the opposite side of the body instead of bending on its own side");
	}

	void TestMissingArmRoleDegradesGracefully()
	{
		AvatarRig rig = BuildTestRig(/*includeRightArm=*/false);
		AvatarIKInput input;
		input.Head = MakeTarget(vec3(3.0f, 68.0f, 0.0f));
		input.LeftHand = MakeTarget(vec3(-20.0f, 35.0f, -20.0f));
		input.RightHand = MakeTarget(vec3(999.0f, 999.0f, 999.0f)); // no right-arm roles to receive this
		AvatarIKOptions options;
		Array<AvatarJointTransform> transforms;
		AvatarIKSolver::Solve(rig, input, options, transforms);

		Require(transforms.size() == rig.Joints.size(), "output size did not match a rig missing an arm's roles");
		for (size_t i = 0; i < transforms.size(); i++)
		{
			Require(IsFinite(transforms[i].Translation) && IsFinite(transforms[i].Rotation),
				"a rig missing an arm's roles produced a non-finite transform");
		}
		vec3 solvedLeft = SolvedPosition(rig, transforms, AvatarJointRole::LeftHand);
		Require(length(solvedLeft - vec3(-20.0f, 35.0f, -20.0f)) < 0.01f,
			"the present arm should still solve normally when the other side is missing its roles");
	}

	void TestPelvisYawFollowsHeadOnItsOwnBindAxes()
	{
		// This rig's own bind-pose "forward" is (0,0,-1) (see BuildTestRig
		// header comment) - not the engine/XR convention's (1,0,0). Turn the
		// head 90 degrees (to face the rig's own bind-pose "right", (1,0,0))
		// and confirm the pelvis yaw follows using the rig's own axes, not
		// any hardcoded global convention.
		AvatarRig rig = BuildTestRig();
		AvatarIKInput input;
		input.Head = MakeTarget(vec3(0.0f, 65.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f));
		AvatarIKOptions options;
		Array<AvatarJointTransform> transforms;
		AvatarIKSolver::Solve(rig, input, options, transforms);

		int pelvisIdx = FindJointIndex(rig, AvatarJointRole::Pelvis);
		vec3 rotatedBindForward = transforms[pelvisIdx].Rotation * vec3(0.0f, 0.0f, -1.0f);
		std::cout << "  pelvis-rotated bind forward after a 90 degree head turn: (" << rotatedBindForward.x
			<< ", " << rotatedBindForward.y << ", " << rotatedBindForward.z << ")\n";
		Require(NearlyEqual(rotatedBindForward, vec3(1.0f, 0.0f, 0.0f), 0.01f),
			"pelvis yaw did not follow the head turn expressed in the rig's own bind-pose axes");
	}
}

int main()
{
	try
	{
		TestNoInputLeavesExactBindPose();
		TestHandTracksReachableTarget();
		TestHandClampsUnreachableTarget();
		TestPelvisFollowsHeadProportionally();
		TestSpineInterpolatesBetweenPelvisAndHead();
		TestElbowStaysOnAnatomicalSide();
		TestMissingArmRoleDegradesGracefully();
		TestPelvisYawFollowsHeadOnItsOwnBindAxes();
		std::cout << "Avatar IK solver tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Avatar IK solver test failed: " << error.what() << '\n';
		return 1;
	}
}
