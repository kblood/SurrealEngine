#include "BotPolicyShadowTelemetry.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace BotAI
{
	namespace
	{
		ShadowTelemetryResult Reject(ShadowTelemetryStatus status, std::string error)
		{
			return { status, {}, std::move(error) };
		}

		bool IsContinuation(unsigned char byte)
		{
			return byte >= 0x80 && byte <= 0xbf;
		}

		bool IsValidUtf8(std::string_view value)
		{
			const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
			for (size_t index = 0; index < value.size();)
			{
				const unsigned char first = bytes[index];
				if (first <= 0x7f)
				{
					index++;
					continue;
				}
				if (first >= 0xc2 && first <= 0xdf)
				{
					if (index + 1 >= value.size() || !IsContinuation(bytes[index + 1]))
						return false;
					index += 2;
					continue;
				}
				if (first >= 0xe0 && first <= 0xef)
				{
					if (index + 2 >= value.size() || !IsContinuation(bytes[index + 2]))
						return false;
					const unsigned char second = bytes[index + 1];
					if ((first == 0xe0 && (second < 0xa0 || second > 0xbf))
						|| (first == 0xed && (second < 0x80 || second > 0x9f))
						|| (first != 0xe0 && first != 0xed && !IsContinuation(second)))
						return false;
					index += 3;
					continue;
				}
				if (first >= 0xf0 && first <= 0xf4)
				{
					if (index + 3 >= value.size() || !IsContinuation(bytes[index + 2]) || !IsContinuation(bytes[index + 3]))
						return false;
					const unsigned char second = bytes[index + 1];
					if ((first == 0xf0 && (second < 0x90 || second > 0xbf))
						|| (first == 0xf4 && (second < 0x80 || second > 0x8f))
						|| (first != 0xf0 && first != 0xf4 && !IsContinuation(second)))
						return false;
					index += 4;
					continue;
				}
				return false;
			}
			return true;
		}

		void AppendEscaped(std::ostringstream& output, std::string_view value)
		{
			static constexpr char Hex[] = "0123456789abcdef";
			output << '"';
			for (unsigned char byte : value)
			{
				switch (byte)
				{
				case '"': output << "\\\""; break;
				case '\\': output << "\\\\"; break;
				case '\b': output << "\\b"; break;
				case '\f': output << "\\f"; break;
				case '\n': output << "\\n"; break;
				case '\r': output << "\\r"; break;
				case '\t': output << "\\t"; break;
				default:
					if (byte < 0x20)
						output << "\\u00" << Hex[(byte >> 4) & 0x0f] << Hex[byte & 0x0f];
					else
						output << static_cast<char>(byte);
					break;
				}
			}
			output << '"';
		}

		std::string_view ActionName(Action action)
		{
			switch (action)
			{
			case Action::Idle: return "idle";
			case Action::Explore: return "explore";
			case Action::AcquireItem: return "acquire-item";
			case Action::HuntEnemy: return "hunt-enemy";
			case Action::AttackEnemy: return "attack-enemy";
			case Action::Retreat: return "retreat";
			case Action::InvestigateSound: return "investigate-sound";
			case Action::RecoverFromStuck: return "recover-from-stuck";
			}
			return {};
		}

		ShadowTelemetryResult Validate(const ShadowPolicySnapshot& snapshot)
		{
			if (snapshot.PolicyId.empty())
				return Reject(ShadowTelemetryStatus::InvalidPolicyId, "policy ID must not be empty");
			if (snapshot.PolicyId.size() > PolicyShadowTelemetrySerializer::MaximumPolicyIdBytes)
				return Reject(ShadowTelemetryStatus::OversizedPolicyId, "policy ID exceeds byte limit");
			if (snapshot.PolicyVersion == 0)
				return Reject(ShadowTelemetryStatus::InvalidPolicyVersion, "policy version must be nonzero");
			if (snapshot.LatestDecision.TargetIdentity.size() > PolicyShadowTelemetrySerializer::MaximumTargetIdentityBytes)
				return Reject(ShadowTelemetryStatus::OversizedTargetIdentity, "target identity exceeds byte limit");
			if (snapshot.LatestDecision.Reason.size() > PolicyShadowTelemetrySerializer::MaximumReasonBytes)
				return Reject(ShadowTelemetryStatus::OversizedReason, "decision reason exceeds byte limit");
			if (!IsValidUtf8(snapshot.PolicyId) || !IsValidUtf8(snapshot.LatestDecision.TargetIdentity)
				|| !IsValidUtf8(snapshot.LatestDecision.Reason))
				return Reject(ShadowTelemetryStatus::InvalidUtf8, "telemetry string is not valid UTF-8");
			if (ActionName(snapshot.LatestDecision.Selected).empty())
				return Reject(ShadowTelemetryStatus::InvalidAction, "decision action is not recognized");
			if (!std::isfinite(snapshot.LatestDecision.Score))
				return Reject(ShadowTelemetryStatus::NonFiniteScore, "decision score must be finite");

			if (!snapshot.HasDecision)
			{
				if (snapshot.EvaluationCount != 0 || snapshot.ActionTransitionCount != 0
					|| snapshot.LatestObservationTick != 0 || snapshot.LatestDecision.Selected != Action::Idle
					|| !snapshot.LatestDecision.TargetIdentity.empty() || snapshot.LatestDecision.Score != 0.0
					|| !snapshot.LatestDecision.Reason.empty() || !snapshot.LatestDecision.Alternatives.empty())
					return Reject(ShadowTelemetryStatus::InconsistentDecisionState, "undecided snapshot contains decision state");
			}
			else if (snapshot.EvaluationCount == 0 || snapshot.ActionTransitionCount >= snapshot.EvaluationCount)
			{
				return Reject(ShadowTelemetryStatus::InconsistentDecisionState, "decision counters are inconsistent");
			}
			return { ShadowTelemetryStatus::Serialized, {}, {} };
		}
	}

	ShadowTelemetryResult PolicyShadowTelemetrySerializer::Serialize(const std::vector<ShadowPolicySnapshot>& snapshots)
	{
		if (snapshots.size() > MaximumPolicyCount)
			return Reject(ShadowTelemetryStatus::TooManyPolicies, "policy snapshot count exceeds schema limit");

		std::vector<const ShadowPolicySnapshot*> ordered;
		ordered.reserve(snapshots.size());
		for (const ShadowPolicySnapshot& snapshot : snapshots)
		{
			ShadowTelemetryResult validation = Validate(snapshot);
			if (!validation)
				return validation;
			ordered.push_back(&snapshot);
		}
		std::sort(ordered.begin(), ordered.end(), [](const ShadowPolicySnapshot* left, const ShadowPolicySnapshot* right)
		{
			return left->PolicyId < right->PolicyId;
		});
		for (size_t index = 1; index < ordered.size(); index++)
		{
			if (ordered[index - 1]->PolicyId == ordered[index]->PolicyId)
				return Reject(ShadowTelemetryStatus::DuplicatePolicyId, "duplicate policy ID");
		}

		std::ostringstream output;
		output.imbue(std::locale::classic());
		output << std::setprecision(std::numeric_limits<double>::max_digits10);
		output << "{\"schema\":";
		AppendEscaped(output, SchemaId);
		output << ",\"policies\":[";
		for (size_t index = 0; index < ordered.size(); index++)
		{
			const ShadowPolicySnapshot& snapshot = *ordered[index];
			if (index != 0)
				output << ',';
			output << "{\"policyId\":";
			AppendEscaped(output, snapshot.PolicyId);
			output << ",\"policyVersion\":" << snapshot.PolicyVersion
				<< ",\"evaluationCount\":" << snapshot.EvaluationCount
				<< ",\"actionTransitions\":" << snapshot.ActionTransitionCount
				<< ",\"latestObservationTick\":" << snapshot.LatestObservationTick
				<< ",\"hasDecision\":" << (snapshot.HasDecision ? "true" : "false")
				<< ",\"selectedAction\":";
			AppendEscaped(output, ActionName(snapshot.LatestDecision.Selected));
			output << ",\"target\":";
			AppendEscaped(output, snapshot.LatestDecision.TargetIdentity);
			output << ",\"score\":";
			if (snapshot.LatestDecision.Score == 0.0)
				output << '0';
			else
				output << snapshot.LatestDecision.Score;
			output << ",\"reason\":";
			AppendEscaped(output, snapshot.LatestDecision.Reason);
			output << '}';
		}
		output << "]}";
		return { ShadowTelemetryStatus::Serialized, output.str(), {} };
	}
}
