#include "UObject/PawnFailedNavigationMemory.h"

#include <array>
#include <iostream>
#include <string>

static int Failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		Failures++;
	}
}

static const void* Target(int value)
{
	return reinterpret_cast<const void*>(static_cast<uintptr_t>(value));
}

static const PawnMovement::FailedNavigationMemoryEntry* FindEntry(
	const PawnMovement::FailedNavigationMemoryState& state, const void* target)
{
	for (const auto& entry : state.Entries)
	{
		if (entry.Target == target)
			return &entry;
	}
	return nullptr;
}

static PawnMovement::FailedNavigationFailureRecord Record(
	const PawnMovement::FailedNavigationMemoryState& state, int target,
	float x = 0.0f, float y = 0.0f, uint8_t requiredFailures = 1)
{
	return PawnMovement::RecordFailedNavigation(
		state, Target(target), x, y, 6.0f, 4.0f, requiredFailures);
}

static void TestImmediateTwoTargetAvoidance()
{
	auto first = Record({}, 1);
	Check(first.AvoidanceActivated, "one watchdog detection activates avoidance");
	Check(FindEntry(first.State, Target(1))->AvoidanceRemaining == 4.0f,
		"first target receives bounded avoidance");

	auto second = Record(first.State, 2);
	Check(second.AvoidanceActivated, "a different target independently activates avoidance");
	Check(FindEntry(second.State, Target(1)) && FindEntry(second.State, Target(2)),
		"two ping-pong targets are remembered simultaneously");
	Check(PawnMovement::FailedNavigationFirstHopPenalty(second.State, Target(1), true, 4096) == 4096,
		"first active target receives route penalty");
	Check(PawnMovement::FailedNavigationFirstHopPenalty(second.State, Target(2), true, 4096) == 4096,
		"second active target receives route penalty");

	auto refresh = Record(second.State, 1);
	Check(!refresh.AvoidanceActivated, "refreshing active avoidance is not a new activation");
	Check(FindEntry(refresh.State, Target(1))->AvoidanceRemaining == 4.0f,
		"matching target refreshes its own avoidance");
	Check(FindEntry(refresh.State, Target(2)) != nullptr,
		"refreshing one target preserves the other target");
}

static void TestLegacyStrikeThresholdStillWorks()
{
	auto first = Record({}, 1, 0.0f, 0.0f, 2);
	Check(!first.AvoidanceActivated, "generic two-strike mode retains a first strike");
	auto second = Record(first.State, 1, 0.0f, 0.0f, 2);
	Check(second.AvoidanceActivated, "generic two-strike mode activates on the second strike");
}

static void TestDeterministicReplacement()
{
	auto state = Record({}, 1).State;
	state = PawnMovement::AdvanceFailedNavigationMemory(
		state, 3.0f, 0.0f, 0.0f, { true, false }, 96.0f);
	state = Record(state, 2).State;
	auto replaced = Record(state, 3).State;
	Check(!FindEntry(replaced, Target(1)), "least remaining entry is replaced when both slots are active");
	Check(FindEntry(replaced, Target(2)) && FindEntry(replaced, Target(3)),
		"replacement preserves the longer-lived entry");

	auto tied = Record(Record({}, 1).State, 2).State;
	auto tiedReplacement = Record(tied, 3).State;
	Check(!FindEntry(tiedReplacement, Target(1)),
		"equal remaining lifetimes deterministically replace the lowest slot");
	Check(tiedReplacement.Entries[0].Target == Target(3),
		"tie replacement uses slot zero");
}

static void TestIndependentAdvanceEscapeInvalidationAndExpiry()
{
	auto state = Record({}, 1, 0.0f, 0.0f, 2).State;
	state = Record(state, 2, 80.0f, 0.0f, 2).State;
	auto escaped = PawnMovement::AdvanceFailedNavigationMemory(
		state, 0.1f, 100.0f, 0.0f, { true, true }, 96.0f);
	Check(!FindEntry(escaped, Target(1)), "escaping an inactive first strike clears only that entry");
	Check(FindEntry(escaped, Target(2)), "nearby inactive first strike survives independent escape check");

	auto active = Record({}, 1, 0.0f, 0.0f).State;
	auto activeAfterEscape = PawnMovement::AdvanceFailedNavigationMemory(
		active, 0.1f, 100.0f, 0.0f, { true, false }, 96.0f);
	Check(FindEntry(activeAfterEscape, Target(1)) != nullptr,
		"horizontal escape does not clear active avoidance");
	Check(FindEntry(activeAfterEscape, Target(1))->AvoidanceRemaining > 3.8f
		&& FindEntry(activeAfterEscape, Target(1))->AvoidanceRemaining < 4.0f,
		"active avoidance retains its bounded lifetime after escape");
	auto activeInvalidated = PawnMovement::AdvanceFailedNavigationMemory(
		active, 0.1f, 0.0f, 0.0f, { false, false }, 96.0f);
	Check(!FindEntry(activeInvalidated, Target(1)),
		"target invalidation still clears active avoidance");

	auto invalidated = PawnMovement::AdvanceFailedNavigationMemory(
		state, 0.1f, 0.0f, 0.0f, { false, true }, 96.0f);
	Check(!FindEntry(invalidated, Target(1)), "invalid first target clears independently");
	Check(FindEntry(invalidated, Target(2)), "live second target survives invalidation of first");

	auto staggered = Record({}, 1).State;
	staggered = PawnMovement::AdvanceFailedNavigationMemory(
		staggered, 3.0f, 0.0f, 0.0f, { true, false }, 96.0f);
	staggered = Record(staggered, 2).State;
	staggered = PawnMovement::AdvanceFailedNavigationMemory(
		staggered, 3.1f, 0.0f, 0.0f, { true, true }, 96.0f);
	Check(!FindEntry(staggered, Target(1)), "older entry expires independently");
	Check(FindEntry(staggered, Target(2))
		&& FindEntry(staggered, Target(2))->RepeatWindowRemaining > 2.8f,
		"newer entry retains its independent repeat window");
}

static void TestFinalEndpointPenaltySelection()
{
	using namespace PawnMovement;
	const auto active = Record({}, 1).State;
	const std::array<FailedNavigationEndpointCandidate, 2> candidates = {{
		{ .Endpoint = Target(1), .Cost = 100, .StableOrder = 0 },
		{ .Endpoint = Target(2), .Cost = 500, .StableOrder = 1 }
	}};
	const auto selection = SelectFailedNavigationEndpoint(active, candidates, 4096);
	Check(selection.Found && selection.CandidateIndex == 1,
		"a safe final endpoint wins over a cheaper remembered endpoint");
	Check(selection.AdjustedCost == 500, "the selected safe endpoint retains its graph cost");
	Check(selection.PenaltyApplications == 1,
		"one remembered final endpoint produces one application per search");
}

static void TestEquivalentEndpointPenaltySelection()
{
	using namespace PawnMovement;
	const auto active = Record({}, 1).State;
	const std::array<FailedNavigationEndpointCandidate, 2> candidates = {{
		{ .Endpoint = Target(2), .EquivalenceGroup = Target(90), .Cost = 100, .StableOrder = 0 },
		{ .Endpoint = Target(3), .Cost = 500, .StableOrder = 1 }
	}};
	const auto selection = SelectFailedNavigationEndpoint(
		active, candidates, 4096, { Target(90), nullptr });
	Check(selection.Found && selection.CandidateIndex == 1,
		"a declared equivalent sibling receives the active target penalty");
	Check(selection.PenaltyApplications == 1,
		"an equivalent sibling produces one application per search");
}

static void TestUnrelatedAndNullGroupsRemainUnpenalized()
{
	using namespace PawnMovement;
	const auto active = Record({}, 1).State;
	const std::array<FailedNavigationEndpointCandidate, 2> unrelated = {{
		{ .Endpoint = Target(2), .EquivalenceGroup = Target(91), .Cost = 100, .StableOrder = 0 },
		{ .Endpoint = Target(3), .Cost = 500, .StableOrder = 1 }
	}};
	const auto unrelatedSelection = SelectFailedNavigationEndpoint(
		active, unrelated, 4096, { Target(90), nullptr });
	Check(unrelatedSelection.Found && unrelatedSelection.CandidateIndex == 0,
		"an unrelated equivalence group preserves normal endpoint choice");
	Check(unrelatedSelection.PenaltyApplications == 0,
		"an unrelated group is not counted as an application");

	const std::array<FailedNavigationEndpointCandidate, 2> nullGroup = {{
		{ .Endpoint = Target(2), .Cost = 100, .StableOrder = 0 },
		{ .Endpoint = Target(3), .Cost = 500, .StableOrder = 1 }
	}};
	const auto nullSelection = SelectFailedNavigationEndpoint(
		active, nullGroup, 4096, { nullptr, nullptr });
	Check(nullSelection.Found && nullSelection.CandidateIndex == 0,
		"null groups never make distinct endpoints equivalent");
	Check(nullSelection.PenaltyApplications == 0,
		"null-group candidates are not counted as equivalent applications");
}

static void TestTwoActiveEquivalentGroups()
{
	using namespace PawnMovement;
	auto active = Record({}, 1).State;
	active = Record(active, 2).State;
	const std::array<FailedNavigationEndpointCandidate, 3> candidates = {{
		{ .Endpoint = Target(3), .EquivalenceGroup = Target(90), .Cost = 100, .StableOrder = 0 },
		{ .Endpoint = Target(4), .EquivalenceGroup = Target(91), .Cost = 200, .StableOrder = 1 },
		{ .Endpoint = Target(5), .Cost = 500, .StableOrder = 2 }
	}};
	const auto selection = SelectFailedNavigationEndpoint(
		active, candidates, 4096, { Target(90), Target(91) });
	Check(selection.Found && selection.CandidateIndex == 2,
		"equivalent siblings of two active targets both yield to a safe endpoint");
	Check(selection.PenaltyApplications == 2,
		"two distinct penalized final candidates count once each");
}

static void TestPerEntryRelatedEndpointSelection()
{
	using namespace PawnMovement;
	auto active = Record({}, 1).State;
	active = Record(active, 2).State;
	const std::array<FailedNavigationEndpointCandidate, 3> candidates = {{
		{ .Endpoint = Target(3), .AvoidedEntries = { true, false }, .Cost = 100, .StableOrder = 0 },
		{ .Endpoint = Target(4), .AvoidedEntries = { false, true }, .Cost = 200, .StableOrder = 1 },
		{ .Endpoint = Target(5), .Cost = 500, .StableOrder = 2 }
	}};
	const auto selection = SelectFailedNavigationEndpoint(active, candidates, 4096);
	Check(selection.Found && selection.CandidateIndex == 2,
		"non-transitive relations independently avoid siblings of two active targets");
	Check(selection.PenaltyApplications == 2,
		"each related final candidate is counted once");

	active.Entries[1].AvoidanceRemaining = 0.0f;
	const auto oneActiveSelection = SelectFailedNavigationEndpoint(active, candidates, 4096);
	Check(oneActiveSelection.Found && oneActiveSelection.CandidateIndex == 1,
		"a relation mask does not penalize an inactive memory entry");
}

static void TestOnlyFailedEndpointRemainsReachable()
{
	using namespace PawnMovement;
	const auto active = Record({}, 1).State;
	const std::array<FailedNavigationEndpointCandidate, 1> candidates = {{
		{ .Endpoint = Target(1), .Cost = 100, .StableOrder = 7 }
	}};
	const auto selection = SelectFailedNavigationEndpoint(active, candidates, 4096);
	Check(selection.Found && selection.CandidateIndex == 0,
		"a remembered endpoint remains usable when no alternative exists");
	Check(selection.AdjustedCost == 4196, "the only endpoint reports its bounded adjusted cost");
	Check(selection.PenaltyApplications == 1, "the only remembered endpoint is counted once");
}

static void TestDuplicateEndpointRoutesArePenalizedOnce()
{
	using namespace PawnMovement;
	const auto active = Record({}, 1).State;
	const std::array<FailedNavigationEndpointCandidate, 4> candidates = {{
		{ .Endpoint = Target(1), .Cost = 300, .StableOrder = 3 },
		{ .Endpoint = Target(1), .Cost = 100, .StableOrder = 5 },
		{ .Endpoint = Target(1), .Cost = 100, .StableOrder = 2 },
		{ .Endpoint = Target(2), .Cost = 4196, .StableOrder = 4 }
	}};
	const auto selection = SelectFailedNavigationEndpoint(active, candidates, 4096);
	Check(selection.Found && selection.CandidateIndex == 2,
		"the cheapest duplicate route and lower stable order win an adjusted-cost tie");
	Check(selection.AdjustedCost == 4196, "duplicate endpoint uses its cheapest base route");
	Check(selection.PenaltyApplications == 1,
		"duplicate routes to one remembered endpoint count as one application");
}

static void TestInternalRememberedNodeDoesNotAffectFinalSelection()
{
	using namespace PawnMovement;
	const auto active = Record({}, 1).State;
	const std::array<FailedNavigationEndpointCandidate, 2> candidates = {{
		{ .Endpoint = Target(2), .Cost = 200, .StableOrder = 1 },
		{ .Endpoint = Target(3), .Cost = 200, .StableOrder = 0 }
	}};
	const auto selection = SelectFailedNavigationEndpoint(active, candidates, 4096);
	Check(selection.Found && selection.CandidateIndex == 1,
		"a remembered node absent from final endpoints cannot distort route selection");
	Check(selection.PenaltyApplications == 0, "an internal remembered node is not counted");
}

static void TestPenaltyIsNarrow()
{
	auto active = Record({}, 1).State;
	Check(PawnMovement::FailedNavigationFirstHopPenalty(active, Target(1), false, 4096) == 0,
		"matching interior node is not globally penalized");
	Check(PawnMovement::FailedNavigationFirstHopPenalty(active, Target(2), true, 4096) == 0,
		"unremembered reachable endpoint is not penalized");
	Check(PawnMovement::FailedNavigationFirstHopPenalty(active, Target(1), true, 0) == 0,
		"non-positive penalty is rejected");
}

static void TestLiftAndSpecialSafeguards()
{
	PawnMovement::FailedNavigationEligibility ordinary = { .TargetLive = true };
	Check(PawnMovement::CanRememberFailedNavigation(ordinary), "ordinary live target is eligible");
	auto rejected = ordinary; rejected.TargetLive = false;
	Check(!PawnMovement::CanRememberFailedNavigation(rejected), "deleted target is rejected");
	rejected = ordinary; rejected.LiftCenter = true;
	Check(!PawnMovement::CanRememberFailedNavigation(rejected), "lift center is rejected");
	rejected = ordinary; rejected.SpecialGoalRedirected = true;
	Check(!PawnMovement::CanRememberFailedNavigation(rejected), "special redirect is rejected");
	rejected = ordinary; rejected.BasedOnLift = true;
	Check(!PawnMovement::CanRememberFailedNavigation(rejected), "lift rider is rejected");
	rejected = ordinary; rejected.LiftInterpolating = true;
	Check(!PawnMovement::CanRememberFailedNavigation(rejected), "moving lift is rejected");
	rejected = ordinary; rejected.LiftDelaying = true;
	Check(!PawnMovement::CanRememberFailedNavigation(rejected), "delayed lift is rejected");
	rejected = ordinary; rejected.LiftWaitingForPawn = true;
	Check(!PawnMovement::CanRememberFailedNavigation(rejected), "lift waiting for pawn is rejected");
}

static void TestLiftExitLandingTopology()
{
	using namespace PawnMovement;
	const std::array<const void*, 2> leftNeighbors = { Target(10), Target(11) };
	const std::array<const void*, 2> rightNeighbors = { Target(10), Target(6) };
	const FailedNavigationLiftExitTopology deckLeft = {
		.Zone = Target(4), .X = 722.609253f, .Y = 1905.590332f, .Z = -626.541626f,
		.AdjacentLandingNodes = leftNeighbors
	};
	const FailedNavigationLiftExitTopology deckRight = {
		.Zone = Target(4), .X = 1057.635742f, .Y = 1901.329590f, .Z = -626.542725f,
		.AdjacentLandingNodes = rightNeighbors
	};
	Check(AreFailedNavigationLiftExitsOnSameLanding(deckLeft, deckRight, 400.0f, 70.0f),
		"Deck16 Left and Right exits sharing PathNode10 form one landing");

	const std::array<const void*, 1> otherNeighbors = { Target(47) };
	auto unrelated = deckRight;
	unrelated.AdjacentLandingNodes = otherNeighbors;
	Check(!AreFailedNavigationLiftExitsOnSameLanding(deckLeft, unrelated, 400.0f, 70.0f),
		"nearby exits without a common landing node remain unrelated");

	auto differentFloor = deckRight;
	differentFloor.Z = -754.54f;
	Check(!AreFailedNavigationLiftExitsOnSameLanding(deckLeft, differentFloor, 400.0f, 70.0f),
		"a shared graph neighbor does not merge vertically separate lift floors");

	auto differentZone = deckRight;
	differentZone.Zone = Target(1);
	Check(!AreFailedNavigationLiftExitsOnSameLanding(deckLeft, differentZone, 400.0f, 70.0f),
		"a shared node does not merge exits across zone boundaries");

	const std::array<const void*, 1> nullNeighbor = { nullptr };
	auto invalidTopology = deckRight;
	invalidTopology.AdjacentLandingNodes = nullNeighbor;
	Check(!AreFailedNavigationLiftExitsOnSameLanding(deckLeft, invalidTopology, 400.0f, 70.0f),
		"null topology identities cannot create a landing group");
}

int main()
{
	TestImmediateTwoTargetAvoidance();
	TestLegacyStrikeThresholdStillWorks();
	TestDeterministicReplacement();
	TestIndependentAdvanceEscapeInvalidationAndExpiry();
	TestFinalEndpointPenaltySelection();
	TestEquivalentEndpointPenaltySelection();
	TestUnrelatedAndNullGroupsRemainUnpenalized();
	TestTwoActiveEquivalentGroups();
	TestPerEntryRelatedEndpointSelection();
	TestOnlyFailedEndpointRemainsReachable();
	TestDuplicateEndpointRoutesArePenalizedOnce();
	TestInternalRememberedNodeDoesNotAffectFinalSelection();
	TestPenaltyIsNarrow();
	TestLiftAndSpecialSafeguards();
	TestLiftExitLandingTopology();
	if (Failures == 0)
		std::cout << "Pawn failed-navigation memory tests passed\n";
	return Failures == 0 ? 0 : 1;
}
