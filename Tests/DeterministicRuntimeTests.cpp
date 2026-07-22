#include "Runtime/DeterministicRuntime.h"
#include "Utils/Random.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}

	bool NearlyEqual(double a, double b)
	{
		return std::abs(a - b) < 0.000001;
	}

	struct RandomSample
	{
		int Integer;
		float Fraction;
		int CInteger;

		bool operator==(const RandomSample&) const = default;
	};

	std::vector<RandomSample> SampleRandomSequence(uint64_t seed)
	{
		SetRandomSeed(seed);
		std::vector<RandomSample> result;
		for (int i = 0; i < 8; i++)
			result.push_back({ RandInt(-100, 100), FRand(), std::rand() });
		return result;
	}
}

int main()
{
	const auto first = SampleRandomSequence(0x12345678abcdef01ULL);
	const auto repeated = SampleRandomSequence(0x12345678abcdef01ULL);
	const auto different = SampleRandomSequence(0x12345678abcdef02ULL);
	if (first != repeated)
		return Fail("the same seed did not replay both engine random streams");
	if (first == different)
		return Fail("different seeds unexpectedly produced the same sample");

	DeterministicRuntimeConfig config;
	config.Seed = 7436991;
	config.FixedDelta = 0.02f;
	config.WallClock.Hour = 12;
	DeterministicRuntime runtime(config);

	runtime.ApplyRandomSeed();
	const int seededValue = RandInt(1000000);
	runtime.ApplyRandomSeed();
	if (seededValue != RandInt(1000000))
		return Fail("runtime did not reapply its configured seed");

	DeterministicFrameTime frame = runtime.AdvanceFrame();
	if (frame.Tick != 1 || !NearlyEqual(frame.RealElapsed, 0.02) ||
		!NearlyEqual(frame.GameElapsed, 0.02) || !NearlyEqual(frame.TotalReal, 0.02))
		return Fail("first fixed frame was incorrect");

	frame = runtime.AdvanceFrame(0.5f);
	if (frame.Tick != 2 || !NearlyEqual(frame.GameElapsed, 0.01) ||
		!NearlyEqual(frame.TotalReal, 0.04) || !NearlyEqual(frame.TotalGame, 0.03))
		return Fail("scaled fixed frame was incorrect");
	if (runtime.GetWallClock().Hour != 12)
		return Fail("configured wall clock was not preserved");

	runtime.Reset();
	if (runtime.GetFrameTime().Tick != 0 || runtime.GetFrameTime().TotalReal != 0.0)
		return Fail("runtime reset did not clear accumulated time");

	bool rejectedDelta = false;
	try
	{
		DeterministicRuntimeConfig invalid;
		invalid.FixedDelta = 0.0f;
		DeterministicRuntime invalidRuntime(invalid);
	}
	catch (const std::invalid_argument&)
	{
		rejectedDelta = true;
	}
	if (!rejectedDelta)
		return Fail("invalid fixed delta was accepted");

	bool rejectedScale = false;
	try
	{
		runtime.AdvanceFrame(std::numeric_limits<float>::infinity());
	}
	catch (const std::invalid_argument&)
	{
		rejectedScale = true;
	}
	if (!rejectedScale)
		return Fail("invalid time scale was accepted");

	return 0;
}
