#pragma once

#include <cstdint>

// Seeds both random-number streams currently used by the engine. This is an
// explicit operation so deterministic tools can opt in without changing the
// seed chosen by the normal game startup path.
void SetRandomSeed(uint64_t seed);

int RandInt(int Min, int Max); // A random integer value between [Min, Max]
int RandInt(int Max); // A random integer value between [0, Max]
float FRand(); // A random float value between [0, 1]
float FRandRange(float Min, float Max);
