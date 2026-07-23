#pragma once

#include "BotPolicyRegistry.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace BotAI
{
	enum class ShadowAddStatus
	{
		Added,
		AlreadyPresent,
		CapacityReached,
		UnknownPolicy,
		ScriptOwnedPolicy
	};

	struct ShadowPolicySnapshot
	{
		std::string PolicyId;
		uint32_t PolicyVersion = 0;
		uint64_t EvaluationCount = 0;
		uint64_t ActionTransitionCount = 0;
		uint64_t LatestObservationTick = 0;
		bool HasDecision = false;
		Decision LatestDecision;
	};

	class PolicyShadowEvaluator
	{
	public:
		explicit PolicyShadowEvaluator(size_t maximumPolicies = 8);

		ShadowAddStatus AddPolicy(std::string_view id);
		void Reset(uint64_t seed);
		void Evaluate(const Observation& observation);

		size_t GetMaximumPolicies() const { return MaximumPolicies; }
		size_t GetPolicyCount() const { return Entries.size(); }
		std::vector<ShadowPolicySnapshot> GetSnapshots() const;

	private:
		struct Entry
		{
			std::unique_ptr<Policy> PolicyInstance;
			ShadowPolicySnapshot Snapshot;
		};

		size_t MaximumPolicies = 0;
		std::vector<Entry> Entries;
	};
}
