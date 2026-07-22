#pragma once

#include "DeterministicRuntime.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Engine;

struct HeadlessDriverConfig
{
	DeterministicRuntimeConfig Runtime;
	uint64_t MaxTicks = 600;
};

struct HeadlessRunSummary
{
	DeterministicFrameTime FinalFrame;
	bool DriverCompleted = false;
	bool TickLimitReached = false;
};

// A driver owns scenario-specific setup and engine operations. Implementations
// may capture Engine& in their registry factory; the reusable runner only owns
// deterministic timing and lifecycle ordering.
class HeadlessDriver
{
public:
	virtual ~HeadlessDriver() = default;

	virtual HeadlessDriverConfig GetConfig() const = 0;
	virtual void Start() = 0;
	virtual bool IsComplete() const = 0;
	virtual void Tick(const DeterministicFrameTime& frameTime) = 0;
	virtual int Finish(const HeadlessRunSummary& summary) = 0;
};

class HeadlessDriverRunner
{
public:
	int Run(HeadlessDriver& driver) const;
};

class HeadlessDriverRegistry
{
public:
	using Factory = std::function<std::unique_ptr<HeadlessDriver>(Engine&)>;
	static constexpr int UnknownDriverExitCode = 2;

	struct Resolution
	{
		Factory Create;
		std::string Error;
		int ExitCode = 0;

		explicit operator bool() const { return static_cast<bool>(Create); }
	};

	void Register(std::string name, Factory factory);
	bool Contains(const std::string& name) const;
	Resolution Resolve(const std::string& name) const;
	std::unique_ptr<HeadlessDriver> Create(const std::string& name, Engine& engine) const;
	std::vector<std::string> Names() const;

private:
	struct Entry
	{
		std::string Name;
		Factory Create;
	};

	std::vector<Entry> Entries;
};

HeadlessDriverRegistry& GetHeadlessDriverRegistry();
