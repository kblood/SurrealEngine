#include "DeterministicRuntime.h"
#include "Utils/Random.h"

#include <cmath>
#include <stdexcept>
#include <utility>

DeterministicRuntime::DeterministicRuntime(DeterministicRuntimeConfig config)
	: Config(std::move(config))
{
	if (!std::isfinite(Config.FixedDelta) || Config.FixedDelta <= 0.0f)
		throw std::invalid_argument("deterministic fixed delta must be finite and positive");
}

void DeterministicRuntime::ApplyRandomSeed() const
{
	SetRandomSeed(Config.Seed);
}

DeterministicFrameTime DeterministicRuntime::AdvanceFrame(float timeScale)
{
	if (!std::isfinite(timeScale) || timeScale < 0.0f)
		throw std::invalid_argument("deterministic time scale must be finite and non-negative");

	FrameTime.Tick++;
	FrameTime.RealElapsed = Config.FixedDelta;
	FrameTime.GameElapsed = Config.FixedDelta * timeScale;
	FrameTime.TotalReal = static_cast<double>(FrameTime.Tick) * Config.FixedDelta;
	FrameTime.TotalGame += FrameTime.GameElapsed;
	return FrameTime;
}

void DeterministicRuntime::Reset()
{
	FrameTime = {};
}
