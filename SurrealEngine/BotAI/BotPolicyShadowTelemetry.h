#pragma once

#include "BotPolicyShadow.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace BotAI
{
	enum class ShadowTelemetryStatus
	{
		Serialized,
		TooManyPolicies,
		DuplicatePolicyId,
		InvalidPolicyId,
		InvalidPolicyVersion,
		OversizedPolicyId,
		OversizedTargetIdentity,
		OversizedReason,
		InvalidUtf8,
		InvalidAction,
		NonFiniteScore,
		InconsistentDecisionState
	};

	struct ShadowTelemetryResult
	{
		ShadowTelemetryStatus Status = ShadowTelemetryStatus::InconsistentDecisionState;
		std::string Json;
		std::string Error;

		explicit operator bool() const { return Status == ShadowTelemetryStatus::Serialized; }
	};

	class PolicyShadowTelemetrySerializer
	{
	public:
		static constexpr std::string_view SchemaId = "surreal.bot-policy-shadow.v1";
		static constexpr size_t MaximumPolicyCount = 16;
		static constexpr size_t MaximumPolicyIdBytes = 64;
		static constexpr size_t MaximumTargetIdentityBytes = 128;
		static constexpr size_t MaximumReasonBytes = 512;

		static ShadowTelemetryResult Serialize(const std::vector<ShadowPolicySnapshot>& snapshots);
	};
}
