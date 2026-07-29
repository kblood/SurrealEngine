#include "UObject/PawnWallAdjustment.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

static int Failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		Failures++;
	}
}

static bool SameVector(const vec3& left, const vec3& right)
{
	return left.x == right.x && left.y == right.y && left.z == right.z;
}

static bool Near(float left, float right, float tolerance = 0.0001f)
{
	return std::abs(left - right) < tolerance;
}

static void TestVisseUsesCurrentEAdjustJumpVelocity()
{
	const vec3 velocity = PawnMovement::CalculateEAdjustJumpVelocity({
		.Location = vec3(1860.441f, 1457.698f, -664.0f),
		.Focus = vec3(2092.568f, 1212.516f, -664.0f),
		.Destination = vec3(1860.550f, 1456.973f, -646.450f),
		.GroundSpeed = 400.0f,
		.JumpZ = 357.5f
	});
	Check(Near(velocity.x, 275.004f, 0.01f) && Near(velocity.y, -290.470f, 0.01f),
		"Visse's adjusted jump starts at GroundSpeed toward the blocked Wandering focus");
	Check(Near(velocity.z, 357.5f), "current EAdjustJump returns JumpZ rather than the script's temporary boosted Z");
}

static void TestEAdjustJumpUsesDestinationFallback()
{
	const vec3 velocity = PawnMovement::CalculateEAdjustJumpVelocity({
		.Location = vec3(10.0f, 20.0f, 30.0f),
		.Focus = vec3(10.01f, 20.0f, 30.0f),
		.Destination = vec3(10.0f, 21.0f, 30.0f),
		.GroundSpeed = 400.0f,
		.JumpZ = 357.5f
	});
	Check(Near(velocity.x, 0.0f) && Near(velocity.y, 400.0f),
		"focus within the current squared-distance threshold falls back to Destination");
}

static void TestEAdjustJumpPreservesTinyTargetSpeed()
{
	const vec3 velocity = PawnMovement::CalculateEAdjustJumpVelocity({
		.Focus = vec3(0.1f, 0.0f, 100.0f),
		.Destination = vec3(0.0f, 1.0f, 0.0f),
		.GroundSpeed = 400.0f,
		.JumpZ = 357.5f
	});
	Check(Near(velocity.x, 100.0f) && Near(velocity.y, 0.0f),
		"a tiny non-fallback XY target retains delta divided by the 0.001-second floor");
	Check(length(velocity.xy()) < 400.0f, "current EAdjustJump clamps only speeds above GroundSpeed");
}

static PawnMovement::WallAdjustJumpDecisionInput SafeBotJump()
{
	return {
		.ProposedVelocity = vec3(200.0f, 0.0f, 357.5f),
		.AIPlayerBot = true,
		.UnrealTournament = true,
		.StockBot = true,
		.WanderingState = true,
		.KneeSweepClear = true,
		.NormalDownwardGravity = true
	};
}

static void TestOnlyPositivePainPredictionVetoesBotJump()
{
	auto input = SafeBotJump();
	Check(PawnMovement::ShouldAttemptWallAdjustJump(input), "safe or unknown trajectory preserves the stock wall-adjust jump");
	input.PredictedHarmfulPainZone = true;
	Check(!PawnMovement::ShouldAttemptWallAdjustJump(input), "positively predicted harmful pain makes an autonomous bot use lateral fallback");
}

static void TestPainVetoIsNarrowlyScoped()
{
	auto input = SafeBotJump();
	input.PredictedHarmfulPainZone = true;
	input.AIPlayerBot = false;
	Check(PawnMovement::ShouldAttemptWallAdjustJump(input), "human-controlled pawns preserve the stock wall-adjust jump");
	input.AIPlayerBot = true;
	input.NormalDownwardGravity = false;
	Check(PawnMovement::ShouldAttemptWallAdjustJump(input), "custom gravity preserves the stock wall-adjust jump");
	input.NormalDownwardGravity = true;
	input.CurrentlyInPainZone = true;
	Check(PawnMovement::ShouldAttemptWallAdjustJump(input), "a bot already in pain may wall-jump toward an escape");
}

static void TestPainVetoRequiresUTBotWanderingContract()
{
	auto input = SafeBotJump();
	input.PredictedHarmfulPainZone = true;
	input.UnrealTournament = false;
	Check(PawnMovement::ShouldAttemptWallAdjustJump(input), "Unreal and other games do not use the UT Bot Wandering guard");
	input.UnrealTournament = true;
	input.StockBot = false;
	Check(PawnMovement::ShouldAttemptWallAdjustJump(input), "non-Bot pawns preserve their wall-adjust jump");
	input.StockBot = true;
	input.WanderingState = false;
	Check(PawnMovement::ShouldAttemptWallAdjustJump(input), "other UT Bot states preserve their own HitWall contracts");
}

static void TestWallAdjustJumpRequiresSafeProbeInputs()
{
	auto input = SafeBotJump();
	input.KneeSweepClear = false;
	Check(!PawnMovement::ShouldAttemptWallAdjustJump(input), "blocked knee sweep continues to lateral wall adjustment");
	input.KneeSweepClear = true;
	input.ProposedVelocity.x = std::numeric_limits<float>::quiet_NaN();
	Check(!PawnMovement::ShouldAttemptWallAdjustJump(input), "non-finite proposed jump continues to lateral wall adjustment");
	input.ProposedVelocity = vec3(0.0f, std::numeric_limits<float>::infinity(), 357.5f);
	Check(!PawnMovement::ShouldAttemptWallAdjustJump(input), "unbounded proposed jump cannot reach the native jump branch");
}

static void TestUTPlayerUsesOneBodyDiameter()
{
	const float collisionRadius = 17.0f;
	const float distance = PawnMovement::WallAdjustmentDistance(collisionRadius);
	Check(distance == 34.0f, "UT player wall adjustment spans one collision diameter");
	Check(distance > 1.0f, "wall adjustment cannot be consumed by the one-unit arrival envelope");
	Check(SameVector(PawnMovement::WallAdjustmentDelta({ 0.0f, 1.0f, 0.0f }, collisionRadius), { 0.0f, 34.0f, 0.0f }), "right-side delta preserves direction and scales distance");
}

static void TestLargerBodiesReceiveProportionalClearance()
{
	Check(PawnMovement::WallAdjustmentDistance(32.0f) == 64.0f, "larger pawn uses its own body clearance");
	Check(SameVector(PawnMovement::WallAdjustmentDelta({ 0.0f, -1.0f, 0.0f }, 32.0f), { 0.0f, -64.0f, 0.0f }), "left-side semantics remain the exact inverse");
}

static void TestInvalidRadiusFailsClosed()
{
	Check(PawnMovement::WallAdjustmentDistance(-4.0f) == 0.0f, "negative collision radius cannot create an inverted adjustment");
	Check(PawnMovement::WallAdjustmentDistance(std::numeric_limits<float>::infinity()) == 0.0f, "non-finite collision radius cannot create an unbounded adjustment");
	Check(std::isfinite(PawnMovement::WallAdjustmentDistance(std::numeric_limits<float>::max())), "large finite collision radius cannot overflow the adjustment");
}

int main()
{
	TestVisseUsesCurrentEAdjustJumpVelocity();
	TestEAdjustJumpUsesDestinationFallback();
	TestEAdjustJumpPreservesTinyTargetSpeed();
	TestOnlyPositivePainPredictionVetoesBotJump();
	TestPainVetoIsNarrowlyScoped();
	TestPainVetoRequiresUTBotWanderingContract();
	TestWallAdjustJumpRequiresSafeProbeInputs();
	TestUTPlayerUsesOneBodyDiameter();
	TestLargerBodiesReceiveProportionalClearance();
	TestInvalidRadiusFailsClosed();
	if (Failures == 0)
		std::cout << "Pawn wall adjustment tests passed\n";
	return Failures == 0 ? 0 : 1;
}
