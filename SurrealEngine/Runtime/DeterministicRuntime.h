#pragma once

#include <cstdint>

struct RuntimeWallClock
{
	int Year = 2000;
	int Month = 0;
	int Day = 1;
	int DayOfWeek = 6;
	int Hour = 0;
	int Minute = 0;
	int Second = 0;
	int Millisecond = 0;
};

struct DeterministicRuntimeConfig
{
	uint64_t Seed = 104729;
	float FixedDelta = 1.0f / 60.0f;
	RuntimeWallClock WallClock;
};

struct DeterministicFrameTime
{
	uint64_t Tick = 0;
	float RealElapsed = 0.0f;
	float GameElapsed = 0.0f;
	double TotalReal = 0.0;
	double TotalGame = 0.0;
};

// Opt-in timing and seeding state for repeatable commandlets and headless
// drivers. Constructing this class has no global effect; ApplyRandomSeed and
// AdvanceFrame must both be called explicitly by the owning runtime.
class DeterministicRuntime
{
public:
	explicit DeterministicRuntime(DeterministicRuntimeConfig config = {});

	void ApplyRandomSeed() const;
	DeterministicFrameTime AdvanceFrame(float timeScale = 1.0f);
	void Reset();

	const DeterministicRuntimeConfig& GetConfig() const { return Config; }
	const DeterministicFrameTime& GetFrameTime() const { return FrameTime; }
	const RuntimeWallClock& GetWallClock() const { return Config.WallClock; }

private:
	DeterministicRuntimeConfig Config;
	DeterministicFrameTime FrameTime;
};
