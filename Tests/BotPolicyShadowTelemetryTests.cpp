#include "BotAI/BotPolicyShadowTelemetry.h"

#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

static ShadowPolicySnapshot Decided(std::string id, Action action = Action::AttackEnemy)
{
	ShadowPolicySnapshot snapshot;
	snapshot.PolicyId = std::move(id);
	snapshot.PolicyVersion = 1;
	snapshot.EvaluationCount = 4;
	snapshot.ActionTransitionCount = 2;
	snapshot.LatestObservationTick = 99;
	snapshot.HasDecision = true;
	snapshot.LatestDecision.Selected = action;
	snapshot.LatestDecision.TargetIdentity = "enemy";
	snapshot.LatestDecision.Score = 91.5;
	snapshot.LatestDecision.Reason = "visible combat";
	return snapshot;
}

static void TestExactOutputSortingEscapingAndNoAlternatives()
{
	ShadowPolicySnapshot utility = Decided("utility-arena");
	utility.LatestDecision.TargetIdentity = "enemy\"one\\two";
	utility.LatestDecision.Reason = "line1\nline2\t\x01";
	utility.LatestDecision.Alternatives.push_back({ Action::Retreat, 17.0, std::string(1000, 'x') });
	ShadowPolicySnapshot tactical = Decided("tactical-state", Action::Explore);
	tactical.EvaluationCount = 1;
	tactical.ActionTransitionCount = 0;
	tactical.LatestObservationTick = 7;
	tactical.LatestDecision.TargetIdentity.clear();
	tactical.LatestDecision.Score = -0.0;
	tactical.LatestDecision.Reason = "patrol";

	ShadowTelemetryResult result = PolicyShadowTelemetrySerializer::Serialize({ utility, tactical });
	Check(static_cast<bool>(result), "valid snapshots serialize");
	const std::string expected =
		"{\"schema\":\"surreal.bot-policy-shadow.v1\",\"policies\":["
		"{\"policyId\":\"tactical-state\",\"policyVersion\":1,\"evaluationCount\":1,\"actionTransitions\":0,\"latestObservationTick\":7,\"hasDecision\":true,\"selectedAction\":\"explore\",\"target\":\"\",\"score\":0,\"reason\":\"patrol\"},"
		"{\"policyId\":\"utility-arena\",\"policyVersion\":1,\"evaluationCount\":4,\"actionTransitions\":2,\"latestObservationTick\":99,\"hasDecision\":true,\"selectedAction\":\"attack-enemy\",\"target\":\"enemy\\\"one\\\\two\",\"score\":91.5,\"reason\":\"line1\\nline2\\t\\u0001\"}]}";
	Check(result.Json == expected, "serializer has exact schema, ordering, formatting, and escaping");
	Check(result.Json.find("alternatives") == std::string::npos && result.Json.find(std::string(50, 'x')) == std::string::npos,
		"alternatives and their unbounded strings are never serialized");
}

static void TestEmptyAndUndecidedOutput()
{
	ShadowTelemetryResult empty = PolicyShadowTelemetrySerializer::Serialize({});
	Check(empty.Json == "{\"schema\":\"surreal.bot-policy-shadow.v1\",\"policies\":[]}", "empty snapshot set has canonical output");

	ShadowPolicySnapshot undecided;
	undecided.PolicyId = "utility-arena";
	undecided.PolicyVersion = 1;
	ShadowTelemetryResult result = PolicyShadowTelemetrySerializer::Serialize({ undecided });
	Check(static_cast<bool>(result), "pristine undecided snapshot is valid");
	Check(result.Json.find("\"hasDecision\":false") != std::string::npos, "undecided state is explicit");
}

static void TestCountDuplicateAndIdentityRejections()
{
	std::vector<ShadowPolicySnapshot> tooMany;
	for (size_t index = 0; index <= PolicyShadowTelemetrySerializer::MaximumPolicyCount; index++)
		tooMany.push_back(Decided("policy-" + std::to_string(index)));
	Check(PolicyShadowTelemetrySerializer::Serialize(tooMany).Status == ShadowTelemetryStatus::TooManyPolicies,
		"policy count is bounded");

	Check(PolicyShadowTelemetrySerializer::Serialize({ Decided("same"), Decided("same") }).Status
		== ShadowTelemetryStatus::DuplicatePolicyId, "duplicate IDs are rejected");
	Check(PolicyShadowTelemetrySerializer::Serialize({ Decided("") }).Status == ShadowTelemetryStatus::InvalidPolicyId,
		"empty policy ID is rejected");
	auto zeroVersion = Decided("zero-version");
	zeroVersion.PolicyVersion = 0;
	Check(PolicyShadowTelemetrySerializer::Serialize({ zeroVersion }).Status == ShadowTelemetryStatus::InvalidPolicyVersion,
		"zero policy version is rejected");
}

static void TestStringBoundsAndUtf8()
{
	auto oversizedId = Decided(std::string(PolicyShadowTelemetrySerializer::MaximumPolicyIdBytes + 1, 'i'));
	Check(PolicyShadowTelemetrySerializer::Serialize({ oversizedId }).Status == ShadowTelemetryStatus::OversizedPolicyId,
		"oversized policy ID is rejected");
	auto oversizedTarget = Decided("policy");
	oversizedTarget.LatestDecision.TargetIdentity.assign(PolicyShadowTelemetrySerializer::MaximumTargetIdentityBytes + 1, 't');
	Check(PolicyShadowTelemetrySerializer::Serialize({ oversizedTarget }).Status == ShadowTelemetryStatus::OversizedTargetIdentity,
		"oversized target identity is rejected");
	auto oversizedReason = Decided("policy");
	oversizedReason.LatestDecision.Reason.assign(PolicyShadowTelemetrySerializer::MaximumReasonBytes + 1, 'r');
	Check(PolicyShadowTelemetrySerializer::Serialize({ oversizedReason }).Status == ShadowTelemetryStatus::OversizedReason,
		"oversized reason is rejected");

	auto invalidUtf8 = Decided("policy");
	invalidUtf8.LatestDecision.Reason = std::string("\xc0\xaf", 2);
	Check(PolicyShadowTelemetrySerializer::Serialize({ invalidUtf8 }).Status == ShadowTelemetryStatus::InvalidUtf8,
		"overlong invalid UTF-8 is rejected");
	auto validUtf8 = Decided("b\xc3\xb8t");
	Check(static_cast<bool>(PolicyShadowTelemetrySerializer::Serialize({ validUtf8 })), "valid multibyte UTF-8 is accepted");
}

static void TestDecisionValidation()
{
	auto nonfinite = Decided("policy");
	nonfinite.LatestDecision.Score = std::numeric_limits<double>::infinity();
	Check(PolicyShadowTelemetrySerializer::Serialize({ nonfinite }).Status == ShadowTelemetryStatus::NonFiniteScore,
		"nonfinite score is rejected");
	auto invalidAction = Decided("policy");
	invalidAction.LatestDecision.Selected = static_cast<Action>(999);
	Check(PolicyShadowTelemetrySerializer::Serialize({ invalidAction }).Status == ShadowTelemetryStatus::InvalidAction,
		"unrecognized action is rejected");

	auto undecidedWithCounts = Decided("policy");
	undecidedWithCounts.HasDecision = false;
	Check(PolicyShadowTelemetrySerializer::Serialize({ undecidedWithCounts }).Status == ShadowTelemetryStatus::InconsistentDecisionState,
		"undecided snapshot containing decision state is rejected");
	auto decidedWithoutEvaluation = Decided("policy");
	decidedWithoutEvaluation.EvaluationCount = 0;
	decidedWithoutEvaluation.ActionTransitionCount = 0;
	Check(PolicyShadowTelemetrySerializer::Serialize({ decidedWithoutEvaluation }).Status == ShadowTelemetryStatus::InconsistentDecisionState,
		"decision without an evaluation is rejected");
	auto impossibleTransitions = Decided("policy");
	impossibleTransitions.ActionTransitionCount = impossibleTransitions.EvaluationCount;
	Check(PolicyShadowTelemetrySerializer::Serialize({ impossibleTransitions }).Status == ShadowTelemetryStatus::InconsistentDecisionState,
		"transition count cannot equal or exceed evaluation count");
}

static void TestStableActionNamesAndNumberFormatting()
{
	const std::vector<std::pair<Action, std::string_view>> actionNames = {
		{ Action::Idle, "idle" },
		{ Action::Explore, "explore" },
		{ Action::AcquireItem, "acquire-item" },
		{ Action::HuntEnemy, "hunt-enemy" },
		{ Action::AttackEnemy, "attack-enemy" },
		{ Action::Retreat, "retreat" },
		{ Action::InvestigateSound, "investigate-sound" },
		{ Action::RecoverFromStuck, "recover-from-stuck" }
	};
	for (const auto& [action, name] : actionNames)
	{
		const ShadowTelemetryResult result = PolicyShadowTelemetrySerializer::Serialize({ Decided("policy", action) });
		Check(result.Json.find("\"selectedAction\":\"" + std::string(name) + "\"") != std::string::npos,
			"action has stable telemetry name " + std::string(name));
	}

	auto fractional = Decided("policy");
	fractional.LatestDecision.Score = 1.0 / 3.0;
	const ShadowTelemetryResult result = PolicyShadowTelemetrySerializer::Serialize({ fractional });
	Check(result.Json.find("\"score\":0.33333333333333331") != std::string::npos,
		"score uses deterministic round-trip precision");
}

int main()
{
	TestExactOutputSortingEscapingAndNoAlternatives();
	TestEmptyAndUndecidedOutput();
	TestCountDuplicateAndIdentityRejections();
	TestStringBoundsAndUtf8();
	TestDecisionValidation();
	TestStableActionNamesAndNumberFormatting();
	if (failures == 0)
		std::cout << "All bot policy shadow telemetry tests passed.\n";
	return failures == 0 ? 0 : 1;
}
