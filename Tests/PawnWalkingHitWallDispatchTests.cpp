#include "UObject/PawnWalkingHitWallDispatch.h"

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

	void TestHeadOnContactDispatchesAtStockThreshold()
	{
		const auto result = PawnMovement::EvaluateWalkingHitWallDispatch(
			vec3(1.0f, 0.0f, 0.0f), vec3(-300.0f, 0.0f, 0.0f), -0.5f);
		Check(result.Valid, "finite non-zero collision inputs are valid");
		Check(std::abs(result.NormalVelocityDot + 1.0f) < 0.0001f,
			"head-on normal dot normalized velocity is -1");
		Check(result.LegacyVerticalWallBand,
			"a vertical wall belongs to the historical Z-band");
		Check(result.MinHitWallDispatch,
			"head-on collision crosses Botpack's -0.5 MinHitWall threshold");
		Check(PawnMovement::SelectWalkingHitWallDispatch(result, true),
			"the enabled candidate selects a threshold-crossing contact");
	}

	void TestGlancingContactIsFilteredByMinHitWall()
	{
		const auto result = PawnMovement::EvaluateWalkingHitWallDispatch(
			vec3(1.0f, 0.0f, 0.0f), vec3(-1.0f, 4.0f, 0.0f), -0.5f);
		Check(result.Valid && result.LegacyVerticalWallBand,
			"a finite glancing vertical contact remains in the legacy band");
		Check(result.NormalVelocityDot > -0.5f,
			"glancing contact is less opposing than the stock threshold");
		Check(!result.MinHitWallDispatch,
			"MinHitWall filters the glancing contact");
		Check(PawnMovement::SelectWalkingHitWallDispatch(result, false),
			"the disabled candidate preserves the legacy vertical-wall band");
		Check(!PawnMovement::SelectWalkingHitWallDispatch(result, true),
			"the enabled candidate filters the glancing contact");
	}

	void TestStateAdjustedThreshold()
	{
		const vec3 normal(1.0f, 0.0f, 0.0f);
		const vec3 velocity(-0.4f, 0.916515f, 0.0f);
		const auto defaultState = PawnMovement::EvaluateWalkingHitWallDispatch(
			normal, velocity, -0.5f);
		const auto adjustedState = PawnMovement::EvaluateWalkingHitWallDispatch(
			normal, velocity, -0.35f);
		Check(!defaultState.MinHitWallDispatch,
			"a -0.4 contact does not cross the default -0.5 threshold");
		Check(adjustedState.MinHitWallDispatch,
			"the Botpack +0.15 state adjustment admits the same contact");
	}

	void TestExactBoundaryDoesNotOverclaimDispatch()
	{
		const auto result = PawnMovement::EvaluateWalkingHitWallDispatch(
			vec3(1.0f, 0.0f, 0.0f), vec3(-100.0f, 0.0f, 0.0f), -1.0f);
		Check(!result.MinHitWallDispatch,
			"the observer keeps the unverified exact boundary fail-closed");
		Check(!PawnMovement::SelectWalkingHitWallDispatch(result, true),
			"the enabled candidate also rejects the exact threshold boundary");
	}

	void TestInvalidInputsFailClosed()
	{
		const auto zeroVelocity = PawnMovement::EvaluateWalkingHitWallDispatch(
			vec3(1.0f, 0.0f, 0.0f), vec3(0.0f), -0.5f);
		Check(!zeroVelocity.Valid, "zero velocity cannot define Velocity.Normal");
		const auto nonFiniteNormal = PawnMovement::EvaluateWalkingHitWallDispatch(
			vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f),
			vec3(-1.0f, 0.0f, 0.0f), -0.5f);
		Check(!nonFiniteNormal.Valid, "non-finite collision normal fails closed");
		const auto nonFiniteThreshold = PawnMovement::EvaluateWalkingHitWallDispatch(
			vec3(1.0f, 0.0f, 0.0f), vec3(-1.0f, 0.0f, 0.0f),
			std::numeric_limits<float>::infinity());
		Check(!nonFiniteThreshold.Valid, "non-finite MinHitWall fails closed");
		Check(!PawnMovement::SelectWalkingHitWallDispatch(nonFiniteThreshold, true),
			"the enabled candidate rejects non-finite inputs");
	}
}

int main()
{
	TestHeadOnContactDispatchesAtStockThreshold();
	TestGlancingContactIsFilteredByMinHitWall();
	TestStateAdjustedThreshold();
	TestExactBoundaryDoesNotOverclaimDispatch();
	TestInvalidInputsFailClosed();
	if (Failures == 0)
		std::cout << "Pawn walking HitWall dispatch tests passed\n";
	return Failures == 0 ? 0 : 1;
}
