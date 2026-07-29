#include "BotAI/BotPolicyRegistry.h"

#include <iostream>
#include <string>

using namespace BotAI;

static int failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		failures++;
	}
}

static void TestEnumeration()
{
	const auto& policies = PolicyRegistry::Enumerate();
	Check(policies.size() == 2, "registry enumerates exactly the native experimental policies");
	Check(policies[0].Id == "tactical-state" && policies[0].Version == 1, "tactical policy descriptor is stable");
	Check(policies[1].Id == "utility-arena" && policies[1].Version == 1, "utility policy descriptor is stable");
	Check(policies[0].Id < policies[1].Id, "policy enumeration is deterministically ordered by ID");
}

static void TestFactoryAndVersions()
{
	for (const PolicyDescriptor& descriptor : PolicyRegistry::Enumerate())
	{
		PolicyCreationResult result = PolicyRegistry::Create(descriptor.Id);
		Check(static_cast<bool>(result), "factory creates enumerated policy " + descriptor.Id);
		if (result)
		{
			Check(result.Instance->GetId() == descriptor.Id, "created policy ID matches descriptor");
			Check(result.Instance->GetVersion() == descriptor.Version, "created policy version matches descriptor");
		}
	}
}

static void TestRejectedPolicies()
{
	PolicyCreationResult stock = PolicyRegistry::Create("stock-botpack");
	Check(stock.Status == PolicyCreationStatus::ScriptOwnedPolicy && !stock.Instance, "stock policy is explicitly rejected as script-owned");
	Check(!stock.Reason.empty(), "stock rejection explains the ownership boundary");

	PolicyCreationResult unknown = PolicyRegistry::Create("does-not-exist");
	Check(unknown.Status == PolicyCreationStatus::UnknownPolicy && !unknown.Instance, "unknown policy ID is explicitly rejected");
	Check(!unknown.Reason.empty(), "unknown policy rejection has a reason");
}

int main()
{
	TestEnumeration();
	TestFactoryAndVersions();
	TestRejectedPolicies();
	if (failures == 0)
		std::cout << "All bot policy registry tests passed.\n";
	return failures == 0 ? 0 : 1;
}
