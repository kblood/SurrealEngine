#include "BotPolicyRegistry.h"

#include "TacticalBotPolicy.h"
#include "UtilityBotPolicy.h"

#include <algorithm>
#include <memory>

namespace BotAI
{
	const std::vector<PolicyDescriptor>& PolicyRegistry::Enumerate()
	{
		static const std::vector<PolicyDescriptor> policies = []
		{
			TacticalBotPolicy tactical;
			UtilityBotPolicy utility;
			std::vector<PolicyDescriptor> descriptors
			{
				{ tactical.GetId(), tactical.GetVersion() },
				{ utility.GetId(), utility.GetVersion() }
			};
			std::sort(descriptors.begin(), descriptors.end(), [](const PolicyDescriptor& left, const PolicyDescriptor& right)
			{
				return left.Id < right.Id;
			});
			return descriptors;
		}();
		return policies;
	}

	PolicyCreationResult PolicyRegistry::Create(std::string_view id)
	{
		if (id == "tactical-state")
			return { PolicyCreationStatus::Created, std::make_unique<TacticalBotPolicy>(), {} };
		if (id == "utility-arena")
			return { PolicyCreationStatus::Created, std::make_unique<UtilityBotPolicy>(), {} };
		if (id == "stock-botpack")
		{
			return {
				PolicyCreationStatus::ScriptOwnedPolicy,
				nullptr,
				"stock-botpack remains owned by the UnrealScript runtime"
			};
		}
		return { PolicyCreationStatus::UnknownPolicy, nullptr, "unknown bot policy ID" };
	}
}
