#include "XR/Avatar/AvatarIKSolver.h"
#include "XR/Avatar/AvatarSkinner.h"
#include "XR/XRWeaponPoseSolver.h"

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

	AvatarRig BuildTestRig(bool includeRightArm = true, bool includeRightLeg = true)
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
		// Legs hang below the pelvis (-Y, since this rig's own bind-pose "up"
		// is +Y - see the header comment above) - thigh/calf both 25 units,
		// same as the arm segments, so leg tests can reuse the arm tests'
		// reach numbers.
		AddJoint(rig, AvatarJointRole::LeftThigh, vec3(-15.0f, -20.0f, 0.0f));
		AddJoint(rig, AvatarJointRole::LeftCalf, vec3(-15.0f, -45.0f, 0.0f));
		AddJoint(rig, AvatarJointRole::LeftFoot, vec3(-15.0f, -70.0f, 0.0f));
		if (includeRightLeg)
		{
			AddJoint(rig, AvatarJointRole::RightThigh, vec3(15.0f, -20.0f, 0.0f));
			AddJoint(rig, AvatarJointRole::RightCalf, vec3(15.0f, -45.0f, 0.0f));
			AddJoint(rig, AvatarJointRole::RightFoot, vec3(15.0f, -70.0f, 0.0f));
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

	// M5 reconciliation: AvatarRenderer::BuildIKInput builds this solver's
	// RightHand target directly from the same engine-space grip pose (see
	// Engine.cpp's SetXRAvatarInput/RightHandGrip) that, when avatar
	// diagnostics are enabled, Engine.cpp also feeds SolveXRWeaponPose with
	// XRWeaponVisualAnchor::Grip - the weapon and hand targets are the same
	// pose sample, just converted into different spaces. This test proves the
	// numeric consequence: with XRWeaponPoseSolver's default (zero) local
	// offset, its grip-anchored visual position equals the raw grip pose
	// exactly, and this solver's hand joint tracks that same position (within
	// the arm's reach) across a sequence of distinct poses, the way a moving
	// controller would drive both frame to frame. Together they show the
	// avatar's hand and the weapon's grip anchor land in the same place
	// without any extra offset needed on either side.
	void TestHandTracksSimulatedGripSequence()
	{
		AvatarRig rig = BuildTestRig();
		AvatarIKOptions options;

		XRWeaponPoseOptions weaponOptions;
		weaponOptions.VisualAnchor = XRWeaponVisualAnchor::Grip;

		// All within the rig's 50-unit right-arm span (upper arm 25 + forearm
		// 25 from the RightUpperArm root at (15, 40, 0)), simulating a
		// controller moving frame to frame.
		const vec3 gripSequence[] = {
			vec3(15.0f, 40.0f, -45.0f), // near, but not exactly at, full arm extension
			vec3(20.0f, 45.0f, -35.0f),
			vec3(10.0f, 30.0f, -40.0f),
			vec3(25.0f, 55.0f, -20.0f),
			vec3(5.0f, 35.0f, -45.0f),
			vec3(18.0f, 48.0f, -30.0f),
		};

		float maxHandWeaponGap = 0.0f;
		for (vec3 gripPosition : gripSequence)
		{
			XREnginePose gripPose;
			gripPose.Valid = true;
			gripPose.Position = { gripPosition.x, gripPosition.y, gripPosition.z };
			gripPose.Orientation = {}; // identity - position is all this test checks

			const XRWeaponPoseResult weaponResult = SolveXRWeaponPose(gripPose, gripPose, XRHand::Right, weaponOptions);
			Require(weaponResult.Valid && weaponResult.VisualAnchor == XRWeaponVisualAnchor::Grip,
				"grip-anchored weapon pose solve failed for a simulated grip sample");
			vec3 weaponWorld(weaponResult.VisualPose.Position.X, weaponResult.VisualPose.Position.Y, weaponResult.VisualPose.Position.Z);
			Require(NearlyEqual(weaponWorld, gripPosition, 0.0001f),
				"default-offset grip anchor did not reproduce the raw grip pose exactly");

			AvatarIKInput input;
			input.RightHand = MakeTarget(gripPosition);
			Array<AvatarJointTransform> transforms;
			AvatarIKSolver::Solve(rig, input, options, transforms);
			vec3 solvedHand = SolvedPosition(rig, transforms, AvatarJointRole::RightHand);

			float handWeaponGap = length(solvedHand - weaponWorld);
			std::cout << "  grip (" << gripPosition.x << "," << gripPosition.y << "," << gripPosition.z
				<< ") hand/weapon-grip gap: " << handWeaponGap << " units\n";
			Require(handWeaponGap < 0.01f, "solved hand joint did not land on the weapon's grip-anchored position");
			maxHandWeaponGap = std::max(maxHandWeaponGap, handWeaponGap);
		}
		std::cout << "  max hand/weapon-grip gap across sequence: " << maxHandWeaponGap << " units\n";
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

	AvatarLegGroundProbe MakeGroundProbe(vec3 groundPoint)
	{
		AvatarLegGroundProbe probe;
		probe.Valid = true;
		probe.GroundPoint = groundPoint;
		return probe;
	}

	// M3 leg tests below. BuildTestRig's legs hang along -Y from the pelvis
	// (thigh at y=-20, foot at y=-70 in bind pose, 50 units of leg length
	// total, same as the arms), so "ground height" in these tests is just a
	// Y coordinate.

	void TestNoGroundProbeBlendsToNeutralHangPose()
	{
		AvatarRig rig = BuildTestRig();
		AvatarIKInput input;
		input.Head = MakeTarget(vec3(0.0f, 65.0f, 0.0f)); // pelvis stays at bind position
		input.Grounded = true; // even claiming "grounded" shouldn't matter without a probe hit
		AvatarIKOptions options;
		AvatarLegIKState legState;
		Array<AvatarJointTransform> transforms;
		// A large delta time settles GroundedBlend fully in one call.
		AvatarIKSolver::SolveWithLegs(rig, input, options, 1.0f, legState, transforms);

		vec3 hip(-15.0f, -20.0f, 0.0f);
		vec3 expectedFoot = hip - vec3(0.0f, 1.0f, 0.0f) * (50.0f * 0.95f);
		vec3 solvedFoot = SolvedPosition(rig, transforms, AvatarJointRole::LeftFoot);
		std::cout << "  neutral hang foot position: (" << solvedFoot.x << ", " << solvedFoot.y << ", " << solvedFoot.z << ")\n";
		Require(NearlyEqual(solvedFoot, expectedFoot, 0.01f), "an ungrounded leg did not settle to the neutral hanging pose");
	}

	void TestGroundedFootMatchesProbedHeight()
	{
		AvatarRig rig = BuildTestRig();
		AvatarIKInput input;
		input.Head = MakeTarget(vec3(0.0f, 65.0f, 0.0f)); // pelvis stays at bind position
		input.Grounded = true;
		const float groundY = -60.0f; // within reach: hip is at y=-20, leg span is 50
		input.LeftFootGround = MakeGroundProbe(vec3(-15.0f, groundY, 0.0f));
		input.RightFootGround = MakeGroundProbe(vec3(15.0f, groundY, 0.0f));
		AvatarIKOptions options;
		AvatarLegIKState legState;
		Array<AvatarJointTransform> transforms;
		AvatarIKSolver::SolveWithLegs(rig, input, options, 1.0f, legState, transforms);

		vec3 solvedLeft = SolvedPosition(rig, transforms, AvatarJointRole::LeftFoot);
		vec3 solvedRight = SolvedPosition(rig, transforms, AvatarJointRole::RightFoot);
		std::cout << "  grounded foot height: left=" << solvedLeft.y << " right=" << solvedRight.y << " (probed ground=" << groundY << ")\n";
		Require(std::abs(solvedLeft.y - groundY) < 0.05f, "left foot did not settle at the probed ground height");
		Require(std::abs(solvedRight.y - groundY) < 0.05f, "right foot did not settle at the probed ground height");

		// Bone lengths must stay exactly as measured from the bind pose - a
		// foot at plausible ground height must not come from a stretched leg.
		vec3 knee = SolvedPosition(rig, transforms, AvatarJointRole::LeftCalf);
		vec3 hip = SolvedPosition(rig, transforms, AvatarJointRole::LeftThigh);
		Require(NearlyEqual(length(knee - hip), 25.0f, 0.01f), "thigh length drifted while grounding the foot");
		Require(NearlyEqual(length(solvedLeft - knee), 25.0f, 0.01f), "calf length drifted while grounding the foot");
	}

	void TestStepMachineAdvancesFootOnceThresholdExceeded()
	{
		AvatarRig rig = BuildTestRig();
		AvatarIKOptions options;
		AvatarLegIKState legState;
		Array<AvatarJointTransform> transforms;

		const float groundY = -60.0f;
		AvatarIKInput input;
		input.Head = MakeTarget(vec3(0.0f, 65.0f, 0.0f));
		input.Grounded = true;
		input.LeftFootGround = MakeGroundProbe(vec3(-15.0f, groundY, 0.0f));
		input.RightFootGround = MakeGroundProbe(vec3(15.0f, groundY, 0.0f));

		// Frame 0: plant the foot and fully settle the grounded blend.
		AvatarIKSolver::SolveWithLegs(rig, input, options, 1.0f, legState, transforms);
		Require(legState.Left.Phase == AvatarLegStepPhase::Idle, "leg did not start planted and idle");
		vec3 plantedFoot = SolvedPosition(rig, transforms, AvatarJointRole::LeftFoot);

		// Move the pelvis forward (this rig's own bind-pose "forward" is
		// (0,0,-1) - see the header comment) by less than the step threshold
		// (leg length 50 * 0.45 = 22.5) - the foot must stay exactly planted,
		// not continuously chase the hip every frame.
		const float smallForwardMove = 10.0f;
		input.Head = MakeTarget(vec3(0.0f, 65.0f, -smallForwardMove));
		input.LeftFootGround = MakeGroundProbe(vec3(-15.0f, groundY, -smallForwardMove)); // ground plane still flat, follows probe under the new hip position
		input.RightFootGround = MakeGroundProbe(vec3(15.0f, groundY, -smallForwardMove));
		AvatarIKSolver::SolveWithLegs(rig, input, options, 0.05f, legState, transforms);
		Require(legState.Left.Phase == AvatarLegStepPhase::Idle, "leg started stepping before the threshold was exceeded");
		vec3 stillPlanted = SolvedPosition(rig, transforms, AvatarJointRole::LeftFoot);
		Require(NearlyEqual(stillPlanted, plantedFoot, 0.01f), "a sub-threshold pelvis move dragged the planted foot instead of leaving it in place");

		// Now move far enough (35 units, past the 22.5 threshold) to force a step.
		const float bigForwardMove = 35.0f;
		input.Head = MakeTarget(vec3(0.0f, 65.0f, -bigForwardMove));
		input.LeftFootGround = MakeGroundProbe(vec3(-15.0f, groundY, -bigForwardMove));
		input.RightFootGround = MakeGroundProbe(vec3(15.0f, groundY, -bigForwardMove));
		AvatarIKSolver::SolveWithLegs(rig, input, options, 0.05f, legState, transforms);
		Require(legState.Left.Phase == AvatarLegStepPhase::SteppingForward, "leg did not start a forward step once the threshold was exceeded");

		// Keep advancing the same step (same target) until it completes -
		// the foot must reach the new target and stop, not stretch forever.
		vec3 expectedFinalFoot(-15.0f, groundY, -bigForwardMove);
		for (int i = 0; i < 20 && legState.Left.Phase != AvatarLegStepPhase::Idle; i++)
			AvatarIKSolver::SolveWithLegs(rig, input, options, 0.05f, legState, transforms);

		Require(legState.Left.Phase == AvatarLegStepPhase::Idle, "step never completed (foot stretching indefinitely instead of stepping)");
		vec3 finalFoot = SolvedPosition(rig, transforms, AvatarJointRole::LeftFoot);
		std::cout << "  foot position after completed step: (" << finalFoot.x << ", " << finalFoot.y << ", " << finalFoot.z << ")\n";
		Require(NearlyEqual(finalFoot, expectedFinalFoot, 0.05f), "completed step did not land the foot at the new planted target");

		vec3 knee = SolvedPosition(rig, transforms, AvatarJointRole::LeftCalf);
		vec3 hip = SolvedPosition(rig, transforms, AvatarJointRole::LeftThigh);
		Require(NearlyEqual(length(knee - hip), 25.0f, 0.01f), "thigh length drifted during a step");
		Require(NearlyEqual(length(finalFoot - knee), 25.0f, 0.01f), "calf length drifted during a step");
	}

	void TestAirborneBlendsAwayFromStaleGroundContact()
	{
		AvatarRig rig = BuildTestRig();
		AvatarIKOptions options;
		AvatarLegIKState legState;
		Array<AvatarJointTransform> transforms;

		const float groundY = -60.0f;
		AvatarIKInput input;
		input.Head = MakeTarget(vec3(0.0f, 65.0f, 0.0f));
		input.Grounded = true;
		input.LeftFootGround = MakeGroundProbe(vec3(-15.0f, groundY, 0.0f));
		input.RightFootGround = MakeGroundProbe(vec3(15.0f, groundY, 0.0f));

		AvatarIKSolver::SolveWithLegs(rig, input, options, 1.0f, legState, transforms);
		vec3 groundedFoot = SolvedPosition(rig, transforms, AvatarJointRole::LeftFoot);
		Require(std::abs(groundedFoot.y - groundY) < 0.05f, "setup failed: foot was not grounded before the airborne test began");

		// Go airborne (falling/jumping) - the probe may still nominally hit
		// the same ground, but Grounded=false must be respected regardless
		// (plan doc M3: "ground probes are meaningless there").
		input.Grounded = false;
		AvatarIKSolver::SolveWithLegs(rig, input, options, 0.05f, legState, transforms);
		vec3 justAirborneFoot = SolvedPosition(rig, transforms, AvatarJointRole::LeftFoot);
		// One small time step should not snap all the way to neutral (this
		// is a blend, not an instant cut) but must have started moving.
		Require(length(justAirborneFoot - groundedFoot) > 0.01f, "airborne transition did not blend at all (looks like a snap, not a blend)");

		vec3 hip = SolvedPosition(rig, transforms, AvatarJointRole::LeftThigh);
		float legLength = 50.0f;
		Require(length(justAirborneFoot - hip) <= legLength + 0.5f, "airborne foot exceeded the leg's own measured reach");

		// After enough airborne time the foot must settle at the neutral
		// hang pose, not hold the stale ground contact.
		for (int i = 0; i < 20; i++)
			AvatarIKSolver::SolveWithLegs(rig, input, options, 0.1f, legState, transforms);

		vec3 settledFoot = SolvedPosition(rig, transforms, AvatarJointRole::LeftFoot);
		vec3 expectedNeutral = hip - vec3(0.0f, 1.0f, 0.0f) * (legLength * 0.95f);
		std::cout << "  settled airborne foot position: (" << settledFoot.x << ", " << settledFoot.y << ", " << settledFoot.z << ")\n";
		Require(NearlyEqual(settledFoot, expectedNeutral, 0.05f), "airborne leg never settled at the neutral hang pose");
		Require(std::abs(settledFoot.y - groundY) > 1.0f, "airborne foot stayed suspiciously close to the stale ground contact");
	}

	void TestMissingLegRoleDegradesGracefully()
	{
		AvatarRig rig = BuildTestRig(/*includeRightArm=*/true, /*includeRightLeg=*/false);
		AvatarIKInput input;
		input.Head = MakeTarget(vec3(3.0f, 68.0f, 0.0f));
		input.Grounded = true;
		input.LeftFootGround = MakeGroundProbe(vec3(-15.0f, -60.0f, 0.0f));
		input.RightFootGround = MakeGroundProbe(vec3(999.0f, 999.0f, 999.0f)); // no right-leg roles to receive this
		AvatarIKOptions options;
		AvatarLegIKState legState;
		Array<AvatarJointTransform> transforms;
		AvatarIKSolver::SolveWithLegs(rig, input, options, 1.0f, legState, transforms);

		Require(transforms.size() == rig.Joints.size(), "output size did not match a rig missing a leg's roles");
		for (size_t i = 0; i < transforms.size(); i++)
		{
			Require(IsFinite(transforms[i].Translation) && IsFinite(transforms[i].Rotation),
				"a rig missing a leg's roles produced a non-finite transform");
		}

		vec3 solvedLeft = SolvedPosition(rig, transforms, AvatarJointRole::LeftFoot);
		Require(std::abs(solvedLeft.y - (-60.0f)) < 0.5f, "the present leg should still ground normally when the other side is missing its roles");
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

	// ---- M4 tests below: head/neck triangle culling and height calibration ----

	void TestHeadOrNeckVertexDetection()
	{
		AvatarRig rig;
		rig.Valid = true;
		int chestIdx = AddJoint(rig, AvatarJointRole::Chest, vec3(0.0f, 40.0f, 0.0f));
		int neckIdx = AddJoint(rig, AvatarJointRole::Neck, vec3(0.0f, 55.0f, 0.0f));
		int headIdx = AddJoint(rig, AvatarJointRole::Head, vec3(0.0f, 65.0f, 0.0f));

		rig.FrameVerts = 6;
		rig.VertexJoint = { (uint8_t)chestIdx, (uint8_t)chestIdx, (uint8_t)neckIdx, (uint8_t)neckIdx, (uint8_t)headIdx, (uint8_t)headIdx };

		Require(!AvatarSkinner::IsHeadOrNeckVertex(rig, 0), "chest vertex misclassified as head/neck");
		Require(!AvatarSkinner::IsHeadOrNeckVertex(rig, 1), "chest vertex misclassified as head/neck");
		Require(AvatarSkinner::IsHeadOrNeckVertex(rig, 2), "neck vertex not detected as head/neck");
		Require(AvatarSkinner::IsHeadOrNeckVertex(rig, 3), "neck vertex not detected as head/neck");
		Require(AvatarSkinner::IsHeadOrNeckVertex(rig, 4), "head vertex not detected as head/neck");
		Require(AvatarSkinner::IsHeadOrNeckVertex(rig, 5), "head vertex not detected as head/neck");

		// Out-of-range indices must fail safe, not throw/crash.
		Require(!AvatarSkinner::IsHeadOrNeckVertex(rig, -1), "negative vertex index was not handled safely");
		Require(!AvatarSkinner::IsHeadOrNeckVertex(rig, 999), "out-of-range vertex index was not handled safely");
	}

	void TestTriangleCullingMatchesHeadNeckVertexCount()
	{
		// 9 vertices in 3 bands (chest/neck/head), 2 triangles per band-pair so
		// there is at least one pure-chest, pure-neck, pure-head, and mixed
		// triangle - the same "skip if any vertex is Head/Neck" rule
		// AvatarRenderer::DrawSkinnedMesh's drawTri applies.
		AvatarRig rig;
		rig.Valid = true;
		int chestIdx = AddJoint(rig, AvatarJointRole::Chest, vec3(0.0f, 40.0f, 0.0f));
		int neckIdx = AddJoint(rig, AvatarJointRole::Neck, vec3(0.0f, 55.0f, 0.0f));
		int headIdx = AddJoint(rig, AvatarJointRole::Head, vec3(0.0f, 65.0f, 0.0f));

		rig.FrameVerts = 9;
		rig.VertexJoint = {
			(uint8_t)chestIdx, (uint8_t)chestIdx, (uint8_t)chestIdx,
			(uint8_t)neckIdx,  (uint8_t)neckIdx,  (uint8_t)neckIdx,
			(uint8_t)headIdx,  (uint8_t)headIdx,  (uint8_t)headIdx
		};

		struct Tri { int v[3]; bool expectedHeadOrNeck; };
		Tri tris[] = {
			{ { 0, 1, 2 }, false }, // pure chest
			{ { 1, 2, 0 }, false }, // pure chest, different winding
			{ { 0, 1, 3 }, true },  // chest+neck seam
			{ { 3, 4, 5 }, true },  // pure neck
			{ { 3, 4, 6 }, true },  // neck+head seam
			{ { 6, 7, 8 }, true },  // pure head
		};

		auto referencesHeadOrNeck = [&](const Tri& tri)
		{
			return AvatarSkinner::IsHeadOrNeckVertex(rig, tri.v[0]) ||
				AvatarSkinner::IsHeadOrNeckVertex(rig, tri.v[1]) || AvatarSkinner::IsHeadOrNeckVertex(rig, tri.v[2]);
		};

		int expectedCulledCount = 0;
		int emittedCullOff = 0;
		int emittedCullOn = 0;
		for (const Tri& tri : tris)
		{
			bool got = referencesHeadOrNeck(tri);
			Require(got == tri.expectedHeadOrNeck, "triangle head/neck classification did not match the hand-derived expectation");
			if (got)
				expectedCulledCount++;

			emittedCullOff++; // cull disabled: every triangle is still emitted
			if (!got)
				emittedCullOn++; // cull enabled: only non-head/neck triangles are emitted
		}

		std::cout << "  triangles=" << (sizeof(tris) / sizeof(tris[0])) << " head/neck=" << expectedCulledCount
			<< " emitted(cull off)=" << emittedCullOff << " emitted(cull on)=" << emittedCullOn << "\n";
		Require(emittedCullOff - emittedCullOn == expectedCulledCount,
			"emitted triangle count did not drop by exactly the head/neck-referencing triangle count when culling was enabled");
		Require(emittedCullOff == (int)(sizeof(tris) / sizeof(tris[0])), "cull-disabled path did not emit every triangle");

		// None of the triangles left in the cull-on set may reference a
		// culled (Head/Neck) vertex.
		for (const Tri& tri : tris)
		{
			if (!tri.expectedHeadOrNeck)
				Require(!referencesHeadOrNeck(tri), "a triangle kept under culling unexpectedly referenced a Head/Neck vertex");
		}
	}

	void TestRigHeadHeightMeasuresAboveFeet()
	{
		// BuildTestRig's own bind pose: Head at y=65, both feet at y=-70,
		// pelvis at y=0 (see the header comment - this rig's own "up" is +Y).
		AvatarRig rig = BuildTestRig();
		AvatarRigHeightEstimate estimate = AvatarIKSolver::EstimateRigHeadHeight(rig);
		Require(estimate.Valid, "rig head height estimate was invalid for a rig with full leg roles");
		std::cout << "  rig reference head height (above feet): " << estimate.Height << " units\n";
		Require(NearlyEqual(estimate.Height, 135.0f, 0.01f), "rig head height did not measure head-above-feet from the rig's own bind pose");
		Require(NearlyEqual(estimate.Up, vec3(0.0f, 1.0f, 0.0f), 0.01f), "rig head height did not use the rig's own bind-pose up axis");
	}

	void TestRigHeadHeightFallsBackToPelvisWithoutLegs()
	{
		AvatarRig rig;
		rig.Valid = true;
		AddJoint(rig, AvatarJointRole::Pelvis, vec3(0.0f, 0.0f, 0.0f));
		AddJoint(rig, AvatarJointRole::Head, vec3(0.0f, 65.0f, 0.0f));

		AvatarRigHeightEstimate estimate = AvatarIKSolver::EstimateRigHeadHeight(rig);
		Require(estimate.Valid, "rig head height estimate was invalid for a legless torso-only rig");
		std::cout << "  rig reference head height (above pelvis, no legs): " << estimate.Height << " units\n";
		Require(NearlyEqual(estimate.Height, 65.0f, 0.01f), "legless rig did not fall back to head-above-pelvis height");
	}

	void TestRigHeadHeightInvalidWithoutHeadOrPelvis()
	{
		AvatarRig rig;
		rig.Valid = true;
		AddJoint(rig, AvatarJointRole::Head, vec3(0.0f, 65.0f, 0.0f)); // no Pelvis role at all

		AvatarRigHeightEstimate estimate = AvatarIKSolver::EstimateRigHeadHeight(rig);
		Require(!estimate.Valid, "rig head height estimate should be invalid without a Pelvis role");
	}

	void TestCalibrationScaleIsProportionalToTrackedHeight()
	{
		// A few different tracked head-height-above-floor samples against the
		// same 135-unit rig reference (see TestRigHeadHeightMeasuresAboveFeet) -
		// scale must be exactly tracked/reference, i.e. 1.0 when the player is
		// tracked at exactly the rig's own height, and scale up/down
		// proportionally otherwise.
		const float referenceHeight = 135.0f;
		struct Case { float tracked; float expectedScale; };
		Case cases[] = {
			{ 135.0f, 1.0f },   // exact match -> no correction needed
			{ 270.0f, 2.0f },   // tracked player twice as tall as the rig
			{ 67.5f,  0.5f },   // tracked player half as tall as the rig
			{ 189.0f, 1.4f },   // an arbitrary non-round ratio
		};

		for (const Case& c : cases)
		{
			AvatarCalibrationInput input;
			input.RigReferenceHeadHeight = referenceHeight;
			input.TrackedHeadHeightAboveFloor = c.tracked;
			AvatarCalibrationResult result = AvatarIKSolver::ComputeCalibration(input);

			std::cout << "  tracked=" << c.tracked << " reference=" << referenceHeight << " -> scale=" << result.Scale << "\n";
			Require(result.AutoDetected, "calibration did not report auto-detection for valid height input");
			Require(NearlyEqual(result.Scale, c.expectedScale, 0.001f), "calibration scale was not proportional to tracked/reference height");
		}
	}

	void TestCalibrationScaleClampsExtremeInput()
	{
		AvatarCalibrationInput input;
		input.RigReferenceHeadHeight = 135.0f;
		input.TrackedHeadHeightAboveFloor = 135.0f * 50.0f; // absurdly tall reading (bad tracking sample)
		AvatarCalibrationResult result = AvatarIKSolver::ComputeCalibration(input);
		std::cout << "  clamped scale for an extreme tracked height: " << result.Scale << "\n";
		Require(result.AutoDetected, "an extreme but positive height input should still count as auto-detected");
		Require(result.Scale < 50.0f, "calibration scale was not clamped to a sane range for an extreme tracked height");
		Require(result.Scale > 0.0f, "calibration scale must always stay positive");
	}

	void TestCalibrationFallsBackToUnityWithoutUsableHeights()
	{
		AvatarCalibrationInput input; // both heights left at 0 - "unknown"
		AvatarCalibrationResult result = AvatarIKSolver::ComputeCalibration(input);
		Require(!result.AutoDetected, "calibration should not report auto-detection with no usable height data");
		Require(NearlyEqual(result.Scale, 1.0f, 0.0001f), "calibration did not fall back to scale 1.0 without usable height data");
	}

	void TestManualScaleOverrideReplacesAutoDetection()
	{
		AvatarCalibrationInput input;
		input.RigReferenceHeadHeight = 135.0f;
		input.TrackedHeadHeightAboveFloor = 270.0f; // would auto-detect to 2.0 without the override
		input.HasManualScaleOverride = true;
		input.ManualScaleOverride = 1.75f;

		AvatarCalibrationResult result = AvatarIKSolver::ComputeCalibration(input);
		std::cout << "  manual override=1.75 with auto-detect data present -> scale=" << result.Scale << "\n";
		Require(!result.AutoDetected, "a manual override must not be reported as auto-detected");
		Require(NearlyEqual(result.Scale, 1.75f, 0.0001f), "manual scale override did not replace the auto-detected scale");
	}

	void TestManualScaleOverrideAppliesEvenWithoutHeightData()
	{
		AvatarCalibrationInput input; // no usable auto-detect data at all
		input.HasManualScaleOverride = true;
		input.ManualScaleOverride = 0.85f;

		AvatarCalibrationResult result = AvatarIKSolver::ComputeCalibration(input);
		Require(!result.AutoDetected, "a manual override must not be reported as auto-detected");
		Require(NearlyEqual(result.Scale, 0.85f, 0.0001f), "manual scale override was not applied when no auto-detect data was available");
	}
}

int main()
{
	try
	{
		TestNoInputLeavesExactBindPose();
		TestHandTracksReachableTarget();
		TestHandTracksSimulatedGripSequence();
		TestHandClampsUnreachableTarget();
		TestPelvisFollowsHeadProportionally();
		TestSpineInterpolatesBetweenPelvisAndHead();
		TestElbowStaysOnAnatomicalSide();
		TestMissingArmRoleDegradesGracefully();
		TestPelvisYawFollowsHeadOnItsOwnBindAxes();
		TestNoGroundProbeBlendsToNeutralHangPose();
		TestGroundedFootMatchesProbedHeight();
		TestStepMachineAdvancesFootOnceThresholdExceeded();
		TestAirborneBlendsAwayFromStaleGroundContact();
		TestMissingLegRoleDegradesGracefully();
		TestHeadOrNeckVertexDetection();
		TestTriangleCullingMatchesHeadNeckVertexCount();
		TestRigHeadHeightMeasuresAboveFeet();
		TestRigHeadHeightFallsBackToPelvisWithoutLegs();
		TestRigHeadHeightInvalidWithoutHeadOrPelvis();
		TestCalibrationScaleIsProportionalToTrackedHeight();
		TestCalibrationScaleClampsExtremeInput();
		TestCalibrationFallsBackToUnityWithoutUsableHeights();
		TestManualScaleOverrideReplacesAutoDetection();
		TestManualScaleOverrideAppliesEvenWithoutHeightData();
		std::cout << "Avatar IK solver tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Avatar IK solver test failed: " << error.what() << '\n';
		return 1;
	}
}
