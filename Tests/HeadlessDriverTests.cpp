#include "Runtime/HeadlessDriver.h"
#include "Utils/Random.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}

	class RecordingDriver final : public HeadlessDriver
	{
	public:
		HeadlessDriverConfig Config;
		uint64_t CompleteAfter = 3;
		std::vector<uint64_t> Ticks;
		bool Started = false;
		bool Finished = false;
		int StartRandom = 0;
		HeadlessRunSummary Summary;

		HeadlessDriverConfig GetConfig() const override { return Config; }
		void Start() override
		{
			Started = true;
			StartRandom = RandInt(1000000);
		}
		bool IsComplete() const override { return Ticks.size() >= CompleteAfter; }
		void Tick(const DeterministicFrameTime& frameTime) override { Ticks.push_back(frameTime.Tick); }
		int Finish(const HeadlessRunSummary& summary) override
		{
			Finished = true;
			Summary = summary;
			return 17;
		}
	};
}

int main()
{
	RecordingDriver completed;
	completed.Config.Runtime.Seed = 314159;
	completed.Config.Runtime.FixedDelta = 0.01f;
	completed.Config.MaxTicks = 10;
	SetRandomSeed(completed.Config.Runtime.Seed);
	const int expectedStartRandom = RandInt(1000000);
	if (HeadlessDriverRunner().Run(completed) != 17)
		return Fail("runner did not return the driver's exit code");
	if (completed.StartRandom != expectedStartRandom)
		return Fail("runner did not apply the configured seed before startup");
	if (!completed.Started || !completed.Finished || completed.Ticks != std::vector<uint64_t>({ 1, 2, 3 }))
		return Fail("driver lifecycle ordering was incorrect");
	if (!completed.Summary.DriverCompleted || completed.Summary.TickLimitReached || completed.Summary.FinalFrame.Tick != 3)
		return Fail("completed-run summary was incorrect");

	RecordingDriver limited;
	limited.CompleteAfter = 10;
	limited.Config.MaxTicks = 2;
	HeadlessDriverRunner().Run(limited);
	if (limited.Ticks != std::vector<uint64_t>({ 1, 2 }) || limited.Summary.DriverCompleted ||
		!limited.Summary.TickLimitReached || limited.Summary.FinalFrame.Tick != 2)
		return Fail("tick limit was not enforced");

	RecordingDriver invalid;
	invalid.Config.MaxTicks = 0;
	bool rejectedZeroLimit = false;
	try
	{
		HeadlessDriverRunner().Run(invalid);
	}
	catch (const std::invalid_argument&)
	{
		rejectedZeroLimit = true;
	}
	if (!rejectedZeroLimit || invalid.Started)
		return Fail("zero tick limit was not rejected before driver startup");

	HeadlessDriverRegistry registry;
	registry.Register("zeta", [](Engine&) { return std::make_unique<RecordingDriver>(); });
	registry.Register("alpha", [](Engine&) { return std::make_unique<RecordingDriver>(); });
	if (!registry.Contains("alpha") || registry.Contains("missing") ||
		registry.Names() != std::vector<std::string>({ "alpha", "zeta" }))
		return Fail("driver registry lookup or ordering was incorrect");

	bool rejectedDuplicate = false;
	try
	{
		registry.Register("alpha", [](Engine&) { return std::make_unique<RecordingDriver>(); });
	}
	catch (const std::invalid_argument&)
	{
		rejectedDuplicate = true;
	}
	if (!rejectedDuplicate)
		return Fail("duplicate driver registration was accepted");

	return 0;
}
