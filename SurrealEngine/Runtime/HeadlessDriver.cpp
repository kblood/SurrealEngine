#include "HeadlessDriver.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

int HeadlessDriverRunner::Run(HeadlessDriver& driver) const
{
	const HeadlessDriverConfig config = driver.GetConfig();
	if (config.MaxTicks == 0)
		throw std::invalid_argument("headless driver max ticks must be positive");

	DeterministicRuntime runtime(config.Runtime);
	runtime.ApplyRandomSeed();
	driver.Start();

	while (!driver.IsComplete() && runtime.GetFrameTime().Tick < config.MaxTicks)
		driver.Tick(runtime.AdvanceFrame());

	HeadlessRunSummary summary;
	summary.FinalFrame = runtime.GetFrameTime();
	summary.DriverCompleted = driver.IsComplete();
	summary.TickLimitReached = !summary.DriverCompleted && summary.FinalFrame.Tick == config.MaxTicks;
	return driver.Finish(summary);
}

void HeadlessDriverRegistry::Register(std::string name, Factory factory)
{
	if (name.empty())
		throw std::invalid_argument("headless driver name must not be empty");
	if (!factory)
		throw std::invalid_argument("headless driver factory must not be empty");
	if (Contains(name))
		throw std::invalid_argument("headless driver already registered: " + name);

	Entries.push_back({ std::move(name), std::move(factory) });
}

bool HeadlessDriverRegistry::Contains(const std::string& name) const
{
	return std::any_of(Entries.begin(), Entries.end(), [&](const Entry& entry) { return entry.Name == name; });
}

std::unique_ptr<HeadlessDriver> HeadlessDriverRegistry::Create(const std::string& name, Engine& engine) const
{
	auto it = std::find_if(Entries.begin(), Entries.end(), [&](const Entry& entry) { return entry.Name == name; });
	return it != Entries.end() ? it->Create(engine) : nullptr;
}

std::vector<std::string> HeadlessDriverRegistry::Names() const
{
	std::vector<std::string> result;
	result.reserve(Entries.size());
	for (const Entry& entry : Entries)
		result.push_back(entry.Name);
	std::sort(result.begin(), result.end());
	return result;
}

HeadlessDriverRegistry& GetHeadlessDriverRegistry()
{
	static HeadlessDriverRegistry registry;
	return registry;
}
