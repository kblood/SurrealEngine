
#include "Random.h"

#include <cstdlib>
#include <random>

static std::default_random_engine s_Generator;

void SetRandomSeed(uint64_t seed)
{
	// std::srand only accepts an unsigned int. Fold the high bits so benchmark
	// seeds remain useful to both the C and C++ streams used by the engine.
	uint32_t seed32 = static_cast<uint32_t>(seed) ^ static_cast<uint32_t>(seed >> 32);
	s_Generator.seed(seed32);
	std::srand(seed32);
}

int RandInt(int Min, int Max)
{
	std::uniform_int_distribution<int> distribution(Min, Max);

	return distribution(s_Generator);
}

int RandInt(int Max)
{
	return RandInt(0, Max);
}

float FRand()
{
	return FRandRange(0.0f, 1.0f);
}

float FRandRange(float Min, float Max)
{
	std::uniform_real_distribution<float> distribution(Min, Max);

	return distribution(s_Generator);
}
