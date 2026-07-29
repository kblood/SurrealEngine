#include "ShadowReplayResultLoader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <initializer_list>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace Automation::TestFixtures
{
	namespace
	{
		using Json = nlohmann::json;

		ShadowReplayResultLoad Reject(ShadowReplayResultLoadStatus status,
			std::string error)
		{
			if (error.size() > MaximumDetailBytes)
				error.resize(MaximumDetailBytes);
			return { status, {}, std::move(error) };
		}

		bool HasExactFields(const Json& value,
			std::initializer_list<const char*> fields)
		{
			if (!value.is_object() || value.size() != fields.size())
				return false;
			return std::all_of(fields.begin(), fields.end(),
				[&](const char* field) { return value.contains(field); });
		}

		bool IsBoundedPrintable(const std::string& value, size_t maximumBytes,
			bool allowEmpty = false)
		{
			if ((!allowEmpty && value.empty()) || value.size() > maximumBytes)
				return false;
			return std::all_of(value.begin(), value.end(), [](unsigned char character)
			{
				return character >= 0x21 && character <= 0x7e;
			});
		}

		bool IsLowerSha256(const std::string& value)
		{
			return value.size() == 64 &&
				std::all_of(value.begin(), value.end(), [](unsigned char character)
				{
					return (character >= '0' && character <= '9') ||
						(character >= 'a' && character <= 'f');
				});
		}

		bool IsValidBinding(const ExpectedShadowReplayBinding& binding)
		{
			return IsBoundedPrintable(binding.FixtureId, MaximumDetailBytes) &&
				IsBoundedPrintable(binding.CaptureId, MaximumDetailBytes) &&
				IsLowerSha256(binding.FixtureSha256) &&
				IsLowerSha256(binding.ObservationSha256) &&
				IsLowerSha256(binding.ReportSha256) &&
				IsBoundedPrintable(binding.ModelTag, MaximumDetailBytes) &&
				IsLowerSha256(binding.ModelDigest);
		}

		std::optional<uint64_t> ParseBoundedRevision(const Json& value)
		{
			if (!value.is_string())
				return {};
			const std::string& text = value.get_ref<const std::string&>();
			if (text.empty() || !std::all_of(text.begin(), text.end(),
				[](unsigned char character)
				{
					return character >= '0' && character <= '9';
				}))
				return {};
			uint64_t parsed = 0;
			for (unsigned char character : text)
			{
				const uint64_t digit = character - '0';
				if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10)
					return {};
				parsed = parsed * 10 + digit;
			}
			if (parsed == 0 || parsed > MaximumCommandLifetimeTicks)
				return {};
			return parsed;
		}

		std::optional<uint64_t> ParseUnsigned(const Json& value)
		{
			if (!value.is_number_unsigned())
				return {};
			return value.get<uint64_t>();
		}

		std::optional<CommandKind> ParseCandidateKind(const std::string& action)
		{
			if (action == "walk_to_actor")
				return CommandKind::WalkToActor;
			if (action == "acquire_item")
				return CommandKind::AcquireItem;
			if (action == "interact")
				return CommandKind::Interact;
			if (action == "wait")
				return CommandKind::Wait;
			return {};
		}

		struct ParseGuards
		{
			bool DuplicateKey = false;
			bool ExcessiveNesting = false;
			std::vector<std::set<std::string>> ObjectKeys;

			bool Observe(int depth, Json::parse_event_t event, Json& parsed)
			{
				if (depth > MaximumShadowReplayResultDepth)
					ExcessiveNesting = true;
				if (event == Json::parse_event_t::object_start)
					ObjectKeys.emplace_back();
				else if (event == Json::parse_event_t::key)
				{
					if (ObjectKeys.empty() || !parsed.is_string() ||
						!ObjectKeys.back().insert(parsed.get<std::string>()).second)
						DuplicateKey = true;
				}
				else if (event == Json::parse_event_t::object_end && !ObjectKeys.empty())
					ObjectKeys.pop_back();
				return true;
			}
		};
	}

	ShadowReplayResultLoad LoadShadowReplayResult(
		std::string_view rawJson,
		const ExpectedShadowReplayBinding& expected)
	{
		if (rawJson.empty())
			return Reject(ShadowReplayResultLoadStatus::EmptyInput,
				"shadow replay result is empty");
		if (rawJson.size() > MaximumShadowReplayResultBytes)
			return Reject(ShadowReplayResultLoadStatus::InputTooLarge,
				"shadow replay result exceeds the 16 KiB bound");
		if (!IsValidBinding(expected))
			return Reject(ShadowReplayResultLoadStatus::InvalidBinding,
				"trusted shadow replay binding is invalid or non-canonical");

		ParseGuards guards;
		Json result;
		try
		{
			result = Json::parse(rawJson.begin(), rawJson.end(),
				[&](int depth, Json::parse_event_t event, Json& parsed)
				{
					return guards.Observe(depth, event, parsed);
				}, true, false);
		}
		catch (const std::exception& exception)
		{
			return Reject(ShadowReplayResultLoadStatus::InvalidJson,
				"invalid shadow replay result JSON: " + std::string(exception.what()));
		}
		if (guards.DuplicateKey)
			return Reject(ShadowReplayResultLoadStatus::DuplicateKey,
				"shadow replay result contains a duplicate object key");
		if (guards.ExcessiveNesting)
			return Reject(ShadowReplayResultLoadStatus::ExcessiveNesting,
				"shadow replay result exceeds the nesting bound");

		if (!HasExactFields(result, { "schema", "fixture_id", "capture_id",
			"fixture_sha256", "observation_sha256", "report_sha256", "model",
			"verdict", "reason", "proposal", "shadow_candidate",
			"dispatch_authorized", "controls_live_player" }) ||
			!result["schema"].is_string() ||
			result["schema"] != "surreal-visual-qa-shadow-replay-result-v1")
		{
			return Reject(ShadowReplayResultLoadStatus::InvalidSchema,
				"shadow replay result schema or top-level fields are invalid");
		}

		for (const char* field : { "fixture_id", "capture_id", "fixture_sha256",
			"observation_sha256", "report_sha256", "verdict", "reason" })
		{
			if (!result[field].is_string())
				return Reject(ShadowReplayResultLoadStatus::InvalidSchema,
					"shadow replay result contains a non-string required field");
		}
		const std::string fixtureId = result["fixture_id"].get<std::string>();
		const std::string captureId = result["capture_id"].get<std::string>();
		const std::string fixtureSha = result["fixture_sha256"].get<std::string>();
		const std::string observationSha = result["observation_sha256"].get<std::string>();
		const std::string reportSha = result["report_sha256"].get<std::string>();
		if (!IsBoundedPrintable(fixtureId, MaximumDetailBytes) ||
			!IsBoundedPrintable(captureId, MaximumDetailBytes) ||
			!IsLowerSha256(fixtureSha) || !IsLowerSha256(observationSha) ||
			!IsLowerSha256(reportSha))
		{
			return Reject(ShadowReplayResultLoadStatus::InvalidSchema,
				"shadow replay result provenance is malformed or non-canonical");
		}

		const Json& model = result["model"];
		if (!HasExactFields(model, { "tag", "digest" }) ||
			!model["tag"].is_string() || !model["digest"].is_string())
		{
			return Reject(ShadowReplayResultLoadStatus::InvalidSchema,
				"shadow replay model provenance is invalid");
		}
		const std::string modelTag = model["tag"].get<std::string>();
		const std::string modelDigest = model["digest"].get<std::string>();
		if (!IsBoundedPrintable(modelTag, MaximumDetailBytes) ||
			!IsLowerSha256(modelDigest))
		{
			return Reject(ShadowReplayResultLoadStatus::InvalidSchema,
				"shadow replay model provenance is malformed or non-canonical");
		}
		if (fixtureId != expected.FixtureId || captureId != expected.CaptureId ||
			fixtureSha != expected.FixtureSha256 ||
			observationSha != expected.ObservationSha256 ||
			reportSha != expected.ReportSha256 || modelTag != expected.ModelTag ||
			modelDigest != expected.ModelDigest)
		{
			return Reject(ShadowReplayResultLoadStatus::ProvenanceMismatch,
				"shadow replay result does not match the trusted provenance binding");
		}

		if (result["verdict"] != "passed" ||
			result["reason"] != "proposal matched the curated acceptance oracle")
		{
			return Reject(ShadowReplayResultLoadStatus::InvalidSchema,
				"shadow replay result was not accepted by the curated oracle");
		}
		if (!result["dispatch_authorized"].is_boolean() ||
			!result["controls_live_player"].is_boolean())
		{
			return Reject(ShadowReplayResultLoadStatus::InvalidSchema,
				"shadow replay authority flags must be booleans");
		}
		if (result["dispatch_authorized"].get<bool>() ||
			result["controls_live_player"].get<bool>())
		{
			return Reject(ShadowReplayResultLoadStatus::AuthorizationRejected,
				"shadow replay result claims live control or dispatch authority");
		}

		const Json& proposal = result["proposal"];
		if (!HasExactFields(proposal, { "action", "target_identity", "wait_ticks" }) ||
			!proposal["action"].is_string() ||
			!proposal["target_identity"].is_string())
		{
			return Reject(ShadowReplayResultLoadStatus::InvalidProposal,
				"shadow replay proposal shape or types are invalid");
		}
		const std::string action = proposal["action"].get<std::string>();
		const std::string proposalTarget = proposal["target_identity"].get<std::string>();
		const std::optional<uint64_t> proposalWait = ParseUnsigned(proposal["wait_ticks"]);
		if (!proposalWait || !IsBoundedPrintable(action, MaximumCommandIdBytes) ||
			!IsBoundedPrintable(proposalTarget, MaximumTargetIdentityBytes, true) ||
			*proposalWait > MaximumShadowReplayWaitTicks)
		{
			return Reject(ShadowReplayResultLoadStatus::InvalidProposal,
				"shadow replay proposal fields are invalid or unbounded");
		}

		if (action == "none")
		{
			if (!proposalTarget.empty() || *proposalWait != 0 ||
				!result["shadow_candidate"].is_null())
			{
				return Reject(ShadowReplayResultLoadStatus::InvalidProposal,
					"none proposal contains an action target or candidate");
			}
			return { ShadowReplayResultLoadStatus::NoAction, {},
				"curated shadow replay accepted no action" };
		}

		const std::optional<CommandKind> kind = ParseCandidateKind(action);
		if (!kind || (*kind == CommandKind::Wait ?
			(!proposalTarget.empty() || *proposalWait == 0) :
			(proposalTarget.empty() || *proposalWait != 0)))
		{
			return Reject(ShadowReplayResultLoadStatus::InvalidProposal,
				"shadow replay proposal action and fields are inconsistent");
		}

		const Json& candidateValue = result["shadow_candidate"];
		if (!HasExactFields(candidateValue, { "schema", "dispatch_authorized", "kind",
			"observation_revision", "target", "arrival_radius", "wait_ticks" }) ||
			!candidateValue["schema"].is_string() ||
			candidateValue["schema"] != "surreal-automation-shadow-candidate-v1" ||
			!candidateValue["dispatch_authorized"].is_boolean() ||
			!candidateValue["kind"].is_string())
		{
			return Reject(ShadowReplayResultLoadStatus::InvalidCandidate,
				"shadow replay candidate shape, schema, or types are invalid");
		}
		if (candidateValue["dispatch_authorized"].get<bool>())
			return Reject(ShadowReplayResultLoadStatus::AuthorizationRejected,
				"shadow replay candidate claims dispatch authority");
		if (candidateValue["kind"].get<std::string>() != action)
			return Reject(ShadowReplayResultLoadStatus::InvalidCandidate,
				"shadow replay candidate kind does not match its proposal");

		const std::optional<uint64_t> revision =
			ParseBoundedRevision(candidateValue["observation_revision"]);
		const std::optional<uint64_t> candidateWait =
			ParseUnsigned(candidateValue["wait_ticks"]);
		if (!revision || !candidateWait ||
			*candidateWait > MaximumShadowReplayWaitTicks)
		{
			return Reject(ShadowReplayResultLoadStatus::InvalidCandidate,
				"shadow replay candidate revision or wait is invalid or unbounded");
		}

		ShadowReplayCandidate candidate;
		candidate.DispatchAuthorized = false;
		candidate.Kind = *kind;
		candidate.ObservationRevision = *revision;
		candidate.WaitTicks = *candidateWait;
		if (*kind == CommandKind::Wait)
		{
			if (!candidateValue["target"].is_null() ||
				!candidateValue["arrival_radius"].is_null() ||
				*candidateWait != *proposalWait)
			{
				return Reject(ShadowReplayResultLoadStatus::InvalidCandidate,
					"shadow replay wait candidate fields do not match its proposal");
			}
		}
		else
		{
			const Json& target = candidateValue["target"];
			if (!HasExactFields(target, { "identity", "expected_class" }) ||
				!target["identity"].is_string() ||
				!target["expected_class"].is_string())
			{
				return Reject(ShadowReplayResultLoadStatus::InvalidCandidate,
					"shadow replay actor target shape or types are invalid");
			}
			const std::string identity = target["identity"].get<std::string>();
			const std::string expectedClass = target["expected_class"].get<std::string>();
			const std::optional<uint64_t> radius =
				ParseUnsigned(candidateValue["arrival_radius"]);
			const uint64_t expectedRadius = *kind == CommandKind::WalkToActor ?
				static_cast<uint64_t>(ShadowWalkArrivalRadius) :
				static_cast<uint64_t>(ShadowActorActionArrivalRadius);
			if (!IsBoundedPrintable(identity, MaximumTargetIdentityBytes) ||
				!IsBoundedPrintable(expectedClass, MaximumClassNameBytes) ||
				identity != proposalTarget || !radius || *radius != expectedRadius ||
				*candidateWait != 0)
			{
				return Reject(ShadowReplayResultLoadStatus::InvalidCandidate,
					"shadow replay actor candidate does not match its fixed policy fields");
			}
			candidate.Target = ShadowReplayTarget{ identity, expectedClass };
			candidate.ArrivalRadius = static_cast<double>(*radius);
		}

		return { ShadowReplayResultLoadStatus::Ready, std::move(candidate), {} };
	}
}
