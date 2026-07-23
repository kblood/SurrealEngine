#include "BotPolicyShadow.h"

#include <algorithm>
#include <utility>

namespace BotAI
{
	PolicyShadowEvaluator::PolicyShadowEvaluator(size_t maximumPolicies)
		: MaximumPolicies(maximumPolicies)
	{
		Entries.reserve(maximumPolicies);
	}

	ShadowAddStatus PolicyShadowEvaluator::AddPolicy(std::string_view id)
	{
		auto position = std::lower_bound(Entries.begin(), Entries.end(), id, [](const Entry& entry, std::string_view requestedId)
		{
			return entry.Snapshot.PolicyId < requestedId;
		});
		if (position != Entries.end() && position->Snapshot.PolicyId == id)
			return ShadowAddStatus::AlreadyPresent;

		PolicyCreationResult creation = PolicyRegistry::Create(id);
		if (creation.Status == PolicyCreationStatus::UnknownPolicy)
			return ShadowAddStatus::UnknownPolicy;
		if (creation.Status == PolicyCreationStatus::ScriptOwnedPolicy)
			return ShadowAddStatus::ScriptOwnedPolicy;
		if (Entries.size() >= MaximumPolicies)
			return ShadowAddStatus::CapacityReached;

		Entry entry;
		entry.Snapshot.PolicyId = creation.Instance->GetId();
		entry.Snapshot.PolicyVersion = creation.Instance->GetVersion();
		entry.PolicyInstance = std::move(creation.Instance);
		Entries.insert(position, std::move(entry));
		return ShadowAddStatus::Added;
	}

	void PolicyShadowEvaluator::Reset(uint64_t seed)
	{
		for (Entry& entry : Entries)
		{
			entry.PolicyInstance->Reset(seed);
			const std::string policyId = entry.Snapshot.PolicyId;
			const uint32_t policyVersion = entry.Snapshot.PolicyVersion;
			entry.Snapshot = {};
			entry.Snapshot.PolicyId = policyId;
			entry.Snapshot.PolicyVersion = policyVersion;
		}
	}

	void PolicyShadowEvaluator::Evaluate(const Observation& observation)
	{
		for (Entry& entry : Entries)
		{
			Decision decision = entry.PolicyInstance->Tick(observation);
			if (entry.Snapshot.HasDecision && entry.Snapshot.LatestDecision.Selected != decision.Selected)
				entry.Snapshot.ActionTransitionCount++;
			entry.Snapshot.EvaluationCount++;
			entry.Snapshot.LatestObservationTick = observation.Tick;
			entry.Snapshot.HasDecision = true;
			entry.Snapshot.LatestDecision = std::move(decision);
		}
	}

	std::vector<ShadowPolicySnapshot> PolicyShadowEvaluator::GetSnapshots() const
	{
		std::vector<ShadowPolicySnapshot> snapshots;
		snapshots.reserve(Entries.size());
		for (const Entry& entry : Entries)
			snapshots.push_back(entry.Snapshot);
		return snapshots;
	}
}
