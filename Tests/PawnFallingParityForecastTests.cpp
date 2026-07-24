#include "UObject/PawnFallingParityForecast.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace
{
	int Failures = 0;

	void Check(bool condition, const std::string& message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			Failures++;
		}
	}

	bool Near(float left, float right, float tolerance = 0.0001f)
	{
		return std::abs(left - right) <= tolerance;
	}

	PawnMovement::FallingParityEnvironment DefaultEnvironment(float elapsed = 0.02f)
	{
		return {
			.Gravity = vec3(0.0f, 0.0f, -950.0f),
			.GroundSpeed = 400.0f,
			.TerminalVelocity = 2500.0f,
			.Elapsed = elapsed
		};
	}

	PawnMovement::FallingParitySweepObservation ClearSweep()
	{
		return {
			.Collision = PawnMovement::FallingParityCollisionKind::Clear,
			.Fraction = 1.0f
		};
	}

	PawnMovement::FallingParitySweepObservation StaticSweep(
		float fraction, const vec3& normal)
	{
		return {
			.Collision = PawnMovement::FallingParityCollisionKind::StaticWorld,
			.Fraction = fraction,
			.Normal = normal
		};
	}

	vec3 NormalWithZ(float z)
	{
		return vec3(std::sqrt(1.0f - z * z), 0.0f, z);
	}

	void TestTickPhysicsSubstepSchedule()
	{
		using namespace PawnMovement;
		const FallingRetailPhysicsSlice normalSlice =
			SelectFallingRetailPhysicsSlice(0.02f);
		Check(normalSlice.Valid && Near(normalSlice.Elapsed, 0.02f)
			&& Near(normalSlice.RemainingTime, 0.0f),
			"a normal retail falling slice consumes the complete 0.02 time budget");
		const FallingRetailPhysicsSlice largeSlice =
			SelectFallingRetailPhysicsSlice(0.25f);
		Check(largeSlice.Valid && Near(largeSlice.Elapsed, 0.1f)
			&& Near(largeSlice.RemainingTime, 0.15f),
			"a 0.25 retail backlog selects 0.1 and retains an independent 0.15 remainder");
		const FallingRetailPhysicsSlice secondLargeSlice =
			SelectFallingRetailPhysicsSlice(largeSlice.RemainingTime);
		Check(secondLargeSlice.Valid && Near(secondLargeSlice.Elapsed, 0.075f)
			&& Near(secondLargeSlice.RemainingTime, 0.075f),
			"the next retail slice is selected from backlog rather than collision fractions");

		for (float elapsed : { 0.0166667f, 0.016666667f })
		{
			const FallingParitySubstepSchedule schedule =
				BuildFallingParitySubstepSchedule(elapsed);
			Check(schedule.Valid && !schedule.Exhausted && schedule.Count == 1,
				"both benchmark 1/60 timestep spellings produce one physics substep");
			Check(Near(schedule.Elapsed[0], elapsed, 0.00000001f),
				"the benchmark timestep spelling is preserved exactly");
		}

		const FallingParitySubstepSchedule schedule =
			BuildFallingParitySubstepSchedule(0.0625f);
		Check(schedule.Valid && schedule.Count == 4,
			"a 0.0625 frame is decomposed into four TickPhysics substeps");
		Check(Near(schedule.Elapsed[0], 0.02f)
			&& Near(schedule.Elapsed[1], 0.02f)
			&& Near(schedule.Elapsed[2], 0.02f)
			&& Near(schedule.Elapsed[3], 0.0025f),
			"substep scheduling matches TickPhysics's <=0.02 loop");

		const FallingParitySubstepSchedule exhausted =
			BuildFallingParitySubstepSchedule(0.05f, 2);
		Check(!exhausted.Valid && exhausted.Exhausted && exhausted.Count == 2,
			"a schedule that exceeds its configured bound fails open");
		Check(!BuildFallingParitySubstepSchedule(0.0f).Valid,
			"a zero-duration schedule is invalid");
	}

	void TestNoCollisionIntegrationUsesEachScheduledSubstep()
	{
		using namespace PawnMovement;
		FallingParityState state = {
			.Location = vec3(0.0f),
			.Velocity = vec3(100.0f, 0.0f, 0.0f)
		};
		const FallingParitySubstepSchedule schedule =
			BuildFallingParitySubstepSchedule(0.0625f);
		for (size_t index = 0; index < schedule.Count; index++)
		{
			const FallingParityTransition step = BeginFallingParityStep(
				state, DefaultEnvironment(schedule.Elapsed[index]));
			Check(step.Kind == FallingParityTransitionKind::ProbeDirectSweep,
				"a valid falling parity step requests its direct sweep");
			const FallingParityTransition clear =
				ResolveFallingParityDirectSweep(step, ClearSweep());
			Check(clear.Kind == FallingParityTransitionKind::Continue,
				"a clear direct sweep advances the parity state");
			state = clear.State;
		}
		Check(Near(state.Location.x, 6.25f)
			&& Near(state.Location.z, -2.4284375f, 0.0002f),
			"scheduled integration uses every updated substep velocity");
		Check(Near(state.Velocity.x, 100.0f)
			&& Near(state.Velocity.z, -59.375f),
			"scheduled integration retains the final TickFalling velocity");
		Check(!Near(state.Location.z, -3.7109375f, 0.01f),
			"scheduled integration differs from one invalid 0.0625 physics step");
	}

	void TestGroundAndTerminalSpeedCaps()
	{
		using namespace PawnMovement;
		FallingParityState state = { .Velocity = vec3(400.0f, 0.0f, 0.0f) };
		FallingParityEnvironment environment = DefaultEnvironment();
		environment.Acceleration = vec3(2048.0f, 0.0f, 0.0f);
		FallingParityTransition step = BeginFallingParityStep(state, environment);
		Check(Near(step.State.Velocity.x, 400.0f),
			"air control cannot increase horizontal speed beyond GroundSpeed");

		state.Velocity = vec3(3000.0f, 0.0f, 0.0f);
		environment.Acceleration = vec3(0.0f);
		step = BeginFallingParityStep(state, environment);
		Check(Near(length(step.State.Velocity), 2500.0f, 0.01f),
			"the direct sweep uses terminal-limited velocity");
	}

	void TestDirectAndAlignedLandingThresholds()
	{
		using namespace PawnMovement;
		const FallingParityState state = {
			.Velocity = vec3(100.0f, -100.0f, -50.0f)
		};
		const FallingParityTransition step = BeginFallingParityStep(
			state, { .GroundSpeed = 400.0f, .TerminalVelocity = 2500.0f,
				.Elapsed = 0.02f });
		const FallingParityTransition exactDirect =
			ResolveFallingParityDirectSweep(step,
				StaticSweep(0.5f, NormalWithZ(FallingParityWalkableNormalZ)));
		Check(exactDirect.Kind == FallingParityTransitionKind::ProbeAlignedSweep,
			"a direct hit at exactly the retail 0.7 threshold is not a landing");
		const FallingParityTransition higherDirect =
			ResolveFallingParityDirectSweep(step,
				StaticSweep(0.5f, NormalWithZ(0.7001f)));
		Check(higherDirect.Kind == FallingParityTransitionKind::Landed,
			"a direct hit above the retail 0.7 threshold is a landing");

		const FallingParityTransition alignedRequest =
			ResolveFallingParityDirectSweep(step,
				StaticSweep(0.25f, vec3(0.0f, 1.0f, 0.0f)));
		Check(alignedRequest.Kind == FallingParityTransitionKind::ProbeAlignedSweep,
			"a non-walkable direct static hit requests the aligned residual sweep");
		const FallingParityTransition exactAligned =
			ResolveFallingParityAlignedSweep(alignedRequest,
				StaticSweep(0.5f, NormalWithZ(FallingParityWalkableNormalZ)));
		Check(exactAligned.Kind == FallingParityTransitionKind::Continue,
			"an aligned hit at exactly the retail 0.7 threshold is not a landing");
		const FallingParityTransition higherAligned =
			ResolveFallingParityAlignedSweep(alignedRequest,
				StaticSweep(0.5f, NormalWithZ(0.7001f)));
		Check(higherAligned.Kind == FallingParityTransitionKind::Landed,
			"an aligned hit above the retail 0.7 threshold is a landing");
	}

	void TestAlignedSweepReconstructsDisplacementVelocity()
	{
		using namespace PawnMovement;
		const FallingParityState state = {
			.Location = vec3(10.0f, 20.0f, 30.0f),
			.Velocity = vec3(100.0f, -100.0f, -50.0f)
		};
		const FallingParityTransition step = BeginFallingParityStep(
			state, { .GroundSpeed = 400.0f, .TerminalVelocity = 2500.0f,
				.Elapsed = 0.02f });
		const FallingParityTransition alignedRequest =
			ResolveFallingParityDirectSweep(step,
				StaticSweep(0.25f, vec3(0.0f, 1.0f, 0.0f)));
		const FallingParityTransition result =
			ResolveFallingParityAlignedSweep(alignedRequest, ClearSweep());
		Check(result.Kind == FallingParityTransitionKind::Continue,
			"a clear aligned residual sweep continues falling");
		Check(Near(result.State.Location.x, 12.0f)
			&& Near(result.State.Location.y, 19.5f)
			&& Near(result.State.Location.z, 29.0f),
			"the aligned endpoint includes direct travel and the residual slide");
		Check(Near(result.State.Velocity.x, 100.0f)
			&& Near(result.State.Velocity.y, -25.0f)
			&& Near(result.State.Velocity.z, -50.0f),
			"post-slide velocity reconstructs XY while preserving falling Z velocity");

		const FallingParityTransition blocked =
			ResolveFallingParityAlignedSweep(alignedRequest,
				StaticSweep(0.5f, vec3(1.0f, 0.0f, 0.0f)));
		Check(blocked.Kind == FallingParityTransitionKind::Continue,
			"a partially blocked non-walkable aligned sweep continues within the contact bound");
		Check(Near(blocked.State.Location.x, 11.25f)
			&& Near(blocked.State.Location.y, 19.5f)
			&& Near(blocked.State.Location.z, 29.375f),
			"a blocked aligned sweep stops at its exact partial aligned fraction");
		Check(Near(blocked.State.Velocity.x, 62.5f)
			&& Near(blocked.State.Velocity.y, -25.0f)
			&& Near(blocked.State.Velocity.z, -50.0f),
			"blocked aligned velocity reconstructs XY while preserving falling Z velocity");

		const FallingParityTransition gravityStep = BeginFallingParityStep(
			state, { .Gravity = vec3(0.0f, 0.0f, -1000.0f),
				.GroundSpeed = 400.0f, .TerminalVelocity = 2500.0f,
				.Elapsed = 0.02f });
		const FallingParityTransition gravityAligned =
			ResolveFallingParityDirectSweep(gravityStep,
				StaticSweep(0.25f, vec3(0.0f, 1.0f, 0.0f)));
		const FallingParityTransition gravityResult =
			ResolveFallingParityAlignedSweep(gravityAligned, ClearSweep());
		Check(Near(gravityStep.State.Velocity.z, -70.0f)
			&& Near(gravityResult.State.Velocity.z, -50.0f),
			"wall reconstruction restores iteration-start Z after gravity integration");
	}

	void TestRetailThirdMoveAndRemainingTimeContinuation()
	{
		using namespace PawnMovement;
		const vec3 desiredDir = normalize(vec3(4.0f, 3.0f, -8.0f));
		const FallingTwoWallAdjustment crease = BuildFallingTwoWallAdjustment(
			desiredDir, vec3(4.0f, 3.0f, -8.0f),
			vec3(0.0f, 1.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f), 0.5f);
		Check(Near(crease.Delta.x, 0.0f) && Near(crease.Delta.y, 0.0f)
			&& Near(crease.Delta.z, -4.0f),
			"a perpendicular second plane produces the retail crease third-move delta");
		Check(!crease.Ditch,
			"vertical wall normals do not trigger the positive-Z retail ditch landing");

		const vec3 oldDitchNormal = normalize(vec3(1.0f, 0.0f, 0.5f));
		const vec3 newDitchNormal = normalize(vec3(-1.0f, 0.0f, 0.5f));
		const FallingTwoWallAdjustment ditch = BuildFallingTwoWallAdjustment(
			vec3(0.0f, 1.0f, 0.0f), vec3(0.0f, 8.0f, 0.0f),
			newDitchNormal, oldDitchNormal, 0.25f);
		Check(ditch.Ditch && Near(ditch.Delta.y, 6.0f),
			"opposed upward planes with a horizontal crease reproduce retail ditch detection");

		const FallingTwoWallAdjustment projected = BuildFallingTwoWallAdjustment(
			normalize(vec3(1.0f, 0.0f, -1.0f)), vec3(4.0f, 0.0f, -8.0f),
			normalize(vec3(1.0f, 1.0f, 0.0f)), vec3(1.0f, 0.0f, 0.0f), 0.5f);
		Check(Near(projected.Delta.x, 1.0f) && Near(projected.Delta.y, -1.0f)
			&& Near(projected.Delta.z, -4.0f),
			"same-side planes use the retail second-normal projection branch");

		const FallingRetailPhysicsSlice physicsSlice =
			SelectFallingRetailPhysicsSlice(0.02f);
		const vec3 continuedVelocity = ReconstructFallingCollisionVelocity(
			vec3(0.0f), vec3(1.5f, -0.375f, 0.0f), 0.02f, -50.0f);
		Check(Near(continuedVelocity.x, 75.0f)
			&& Near(continuedVelocity.y, -18.75f)
			&& Near(continuedVelocity.z, -50.0f),
			"the post-third-move velocity reconstructs horizontal travel and preserves falling Z");
		Check(physicsSlice.Valid && Near(physicsSlice.RemainingTime, 0.0f),
			"a clear third move exhausts the normal slice and schedules no fourth sweep");
	}

	void TestAtMostTwoDirectWallContactsAcrossSubsteps()
	{
		using namespace PawnMovement;
		FallingParityState state = {
			.Velocity = vec3(100.0f, -100.0f, -50.0f)
		};
		const FallingParityEnvironment environment = {
			.GroundSpeed = 400.0f,
			.TerminalVelocity = 2500.0f,
			.Elapsed = 0.02f
		};
		FallingParityTransition step = BeginFallingParityStep(state, environment);
		FallingParityTransition aligned = ResolveFallingParityDirectSweep(
			step, StaticSweep(0.25f, vec3(0.0f, 1.0f, 0.0f)));
		FallingParityTransition firstContact = ResolveFallingParityAlignedSweep(
			aligned, ClearSweep());
		Check(firstContact.Kind == FallingParityTransitionKind::Continue
			&& firstContact.State.NonWalkableStaticContacts == 1,
			"the first direct wall contact continues through a clear aligned sweep");

		step = BeginFallingParityStep(firstContact.State, environment);
		aligned = ResolveFallingParityDirectSweep(
			step, StaticSweep(0.25f, vec3(1.0f, 0.0f, 0.0f)));
		FallingParityTransition secondContact = ResolveFallingParityAlignedSweep(
			aligned, ClearSweep());
		Check(secondContact.Kind == FallingParityTransitionKind::Continue
			&& secondContact.State.NonWalkableStaticContacts == 2,
			"a second direct wall contact on the next substep remains inside the bound");

		step = BeginFallingParityStep(secondContact.State, environment);
		const FallingParityTransition thirdContact = ResolveFallingParityDirectSweep(
			step, StaticSweep(0.5f, vec3(0.0f, 1.0f, 0.0f)));
		Check(thirdContact.Kind == FallingParityTransitionKind::Unknown
			&& thirdContact.Reason
				== FallingParityReason::NonWalkableStaticContactLimitExceeded,
			"a third non-walkable static contact fails open");
	}

	void TestUnknownEvidenceFailsOpen()
	{
		using namespace PawnMovement;
		const FallingParityState state = { .Velocity = vec3(100.0f, 0.0f, 0.0f) };
		FallingParityEnvironment environment = DefaultEnvironment();
		environment.WaterPhysics = true;
		Check(BeginFallingParityStep(state, environment).Reason
			== FallingParityReason::WaterPhysicsUnknown,
			"water physics fails open before integration");
		environment.WaterPhysics = false;
		environment.Bounce = true;
		Check(BeginFallingParityStep(state, environment).Reason
			== FallingParityReason::BouncePhysicsUnknown,
			"bounce physics fails open before integration");
		environment.Bounce = false;
		const FallingParityTransition step = BeginFallingParityStep(state, environment);

		FallingParitySweepObservation collision = StaticSweep(
			0.5f, vec3(0.0f, 1.0f, 0.0f));
		collision.Collision = FallingParityCollisionKind::Mover;
		Check(ResolveFallingParityDirectSweep(step, collision).Reason
			== FallingParityReason::MoverCollisionUnknown,
			"mover collisions fail open");
		collision.Collision = FallingParityCollisionKind::DynamicActor;
		Check(ResolveFallingParityDirectSweep(step, collision).Reason
			== FallingParityReason::DynamicActorCollisionUnknown,
			"dynamic actor collisions fail open");
		collision = StaticSweep(std::numeric_limits<float>::quiet_NaN(),
			vec3(0.0f, 1.0f, 0.0f));
		Check(ResolveFallingParityDirectSweep(step, collision).Reason
			== FallingParityReason::InvalidSweepEvidence,
			"non-finite collision fractions fail open");
		collision = StaticSweep(0.5f, vec3(0.0f));
		Check(ResolveFallingParityDirectSweep(step, collision).Reason
			== FallingParityReason::InvalidSweepEvidence,
			"malformed collision normals fail open");

		const FallingParityTransition alignedRequest = ResolveFallingParityDirectSweep(
			step, StaticSweep(0.5f, vec3(0.0f, 1.0f, 0.0f)));
		collision = StaticSweep(0.5f, vec3(1.0f, 0.0f, 0.0f));
		collision.Collision = FallingParityCollisionKind::Mover;
		Check(ResolveFallingParityAlignedSweep(alignedRequest, collision).Reason
			== FallingParityReason::MoverCollisionUnknown,
			"aligned-sweep mover collisions fail open");
		collision.Collision = FallingParityCollisionKind::DynamicActor;
		Check(ResolveFallingParityAlignedSweep(alignedRequest, collision).Reason
			== FallingParityReason::DynamicActorCollisionUnknown,
			"aligned-sweep dynamic actor collisions fail open");
		collision = StaticSweep(std::numeric_limits<float>::quiet_NaN(),
			vec3(1.0f, 0.0f, 0.0f));
		Check(ResolveFallingParityAlignedSweep(alignedRequest, collision).Reason
			== FallingParityReason::InvalidSweepEvidence,
			"aligned-sweep non-finite fractions fail open");
		collision = StaticSweep(0.5f, vec3(0.0f));
		Check(ResolveFallingParityAlignedSweep(alignedRequest, collision).Reason
			== FallingParityReason::InvalidSweepEvidence,
			"aligned-sweep malformed normals fail open");

		const FallingParityTransition exhausted =
			EndFallingParityForecastHorizon(state);
		Check(exhausted.Kind == FallingParityTransitionKind::Unknown
			&& exhausted.Reason == FallingParityReason::ForecastHorizonExhausted,
			"a forecast that exhausts its horizon without landing fails open");
	}
}

int main()
{
	TestTickPhysicsSubstepSchedule();
	TestNoCollisionIntegrationUsesEachScheduledSubstep();
	TestGroundAndTerminalSpeedCaps();
	TestDirectAndAlignedLandingThresholds();
	TestAlignedSweepReconstructsDisplacementVelocity();
	TestRetailThirdMoveAndRemainingTimeContinuation();
	TestAtMostTwoDirectWallContactsAcrossSubsteps();
	TestUnknownEvidenceFailsOpen();
	if (Failures == 0)
		std::cout << "Pawn falling parity forecast tests passed\n";
	return Failures == 0 ? 0 : 1;
}
