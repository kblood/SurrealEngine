#pragma once

#include "BotAI/BotPolicyRegistry.h"
#include "BotAI/BotPolicyShadow.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct BotBenchmarkShadowParticipantDescriptor
{
	size_t RosterIndex = 0;
	std::string Identity;
};

struct BotBenchmarkShadowParticipantState
{
	size_t RosterIndex = 0;
	std::string Identity;
	bool Available = false;
	std::vector<BotAI::ShadowPolicySnapshot> Policies;
};

class BotBenchmarkShadowTelemetry
{
public:
	static constexpr size_t MaximumParticipantCount = 16;
	static constexpr size_t MaximumIdentityBytes = 128;

	static uint64_t EventCap(uint64_t maxTicks);
	static std::string ManifestJson(const std::string& benchmarkConfigIdentity,
		uint64_t maxTicks,
		std::vector<BotAI::PolicyDescriptor> policies,
		std::vector<BotBenchmarkShadowParticipantDescriptor> participants);
	static std::string EventJson(const std::string& benchmarkConfigIdentity,
		uint64_t sequence,
		uint64_t tick,
		std::vector<BotBenchmarkShadowParticipantState> participants);
};
