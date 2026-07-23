#pragma once

#include "BotPolicy.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace BotAI
{
	struct PolicyDescriptor
	{
		std::string Id;
		uint32_t Version = 0;
	};

	enum class PolicyCreationStatus
	{
		Created,
		UnknownPolicy,
		ScriptOwnedPolicy
	};

	struct PolicyCreationResult
	{
		PolicyCreationStatus Status = PolicyCreationStatus::UnknownPolicy;
		std::unique_ptr<Policy> Instance;
		std::string Reason;

		explicit operator bool() const { return Status == PolicyCreationStatus::Created && Instance != nullptr; }
	};

	class PolicyRegistry
	{
	public:
		static const std::vector<PolicyDescriptor>& Enumerate();
		static PolicyCreationResult Create(std::string_view id);
	};
}
