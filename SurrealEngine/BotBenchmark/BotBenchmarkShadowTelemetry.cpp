#include "BotBenchmarkShadowTelemetry.h"

#include "BotAI/BotPolicyShadowTelemetry.h"

#include <algorithm>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>

namespace
{
	std::string JsonString(std::string_view value)
	{
		static constexpr char Hex[] = "0123456789abcdef";
		std::ostringstream output;
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
		return output.str();
	}

	void ValidateIdentity(std::string_view identity, const char* label)
	{
		if (identity.empty())
			throw std::invalid_argument(std::string(label) + " must not be empty");
		if (identity.size() > BotBenchmarkShadowTelemetry::MaximumIdentityBytes)
			throw std::invalid_argument(std::string(label) + " exceeds byte limit");
	}

	void ValidateConfigIdentity(const std::string& identity)
	{
		ValidateIdentity(identity, "benchmark configuration identity");
	}

	void SortAndValidatePolicies(std::vector<BotAI::PolicyDescriptor>& policies)
	{
		std::sort(policies.begin(), policies.end(), [](const auto& left, const auto& right)
		{
			return std::tie(left.Id, left.Version) < std::tie(right.Id, right.Version);
		});
		for (size_t index = 0; index < policies.size(); index++)
		{
			ValidateIdentity(policies[index].Id, "policy ID");
			if (policies[index].Version == 0)
				throw std::invalid_argument("policy version must be nonzero");
			if (index != 0 && policies[index - 1].Id == policies[index].Id)
				throw std::invalid_argument("duplicate shadow policy ID");
		}
	}

	template<typename Participant>
	void SortAndValidateParticipants(std::vector<Participant>& participants)
	{
		if (participants.size() > BotBenchmarkShadowTelemetry::MaximumParticipantCount)
			throw std::invalid_argument("shadow participant count exceeds limit");
		std::sort(participants.begin(), participants.end(), [](const auto& left, const auto& right)
		{
			return std::tie(left.RosterIndex, left.Identity) < std::tie(right.RosterIndex, right.Identity);
		});
		for (size_t index = 0; index < participants.size(); index++)
		{
			ValidateIdentity(participants[index].Identity, "shadow participant identity");
			if (participants[index].RosterIndex != index)
				throw std::invalid_argument("shadow participant roster indexes must be contiguous");
			for (size_t earlier = 0; earlier < index; earlier++)
			{
				if (participants[earlier].Identity == participants[index].Identity)
					throw std::invalid_argument("duplicate shadow participant identity");
			}
		}
	}
}

uint64_t BotBenchmarkShadowTelemetry::EventCap(uint64_t maxTicks)
{
	return maxTicks;
}

std::string BotBenchmarkShadowTelemetry::ManifestJson(const std::string& benchmarkConfigIdentity,
	uint64_t maxTicks,
	std::vector<BotAI::PolicyDescriptor> policies,
	std::vector<BotBenchmarkShadowParticipantDescriptor> participants)
{
	ValidateConfigIdentity(benchmarkConfigIdentity);
	SortAndValidatePolicies(policies);
	SortAndValidateParticipants(participants);

	std::ostringstream output;
	output.imbue(std::locale::classic());
	output << "{\n"
		<< "  \"schema\": \"surreal-bot-benchmark-shadow-manifest-v1\",\n"
		<< "  \"benchmark_config_id\": " << JsonString(benchmarkConfigIdentity) << ",\n"
		<< "  \"record_cap\": \"" << EventCap(maxTicks) << "\",\n"
		<< "  \"controls_live_bots\": false,\n"
		<< "  \"policies\": [\n";
	for (size_t index = 0; index < policies.size(); index++)
	{
		output << "    {\"id\": " << JsonString(policies[index].Id)
			<< ", \"version\": " << policies[index].Version << "}"
			<< (index + 1 == policies.size() ? "\n" : ",\n");
	}
	output << "  ],\n"
		<< "  \"participants\": [\n";
	for (size_t index = 0; index < participants.size(); index++)
	{
		output << "    {\"roster_index\": " << participants[index].RosterIndex
			<< ", \"identity\": " << JsonString(participants[index].Identity) << "}"
			<< (index + 1 == participants.size() ? "\n" : ",\n");
	}
	output << "  ],\n"
		<< "  \"observation_limits\": {\"items\": false, \"armor\": false, \"stuck_time\": false}\n"
		<< "}\n";
	return output.str();
}

std::string BotBenchmarkShadowTelemetry::EventJson(const std::string& benchmarkConfigIdentity,
	uint64_t sequence,
	uint64_t tick,
	std::vector<BotBenchmarkShadowParticipantState> participants)
{
	ValidateConfigIdentity(benchmarkConfigIdentity);
	SortAndValidateParticipants(participants);

	std::ostringstream output;
	output.imbue(std::locale::classic());
	output << "{\"schema\":\"surreal-bot-benchmark-shadow-event-v1\",\"seq\":\""
		<< sequence << "\",\"benchmark_config_id\":" << JsonString(benchmarkConfigIdentity)
		<< ",\"tick\":\"" << tick << "\",\"participants\":[";
	for (size_t index = 0; index < participants.size(); index++)
	{
		BotAI::ShadowTelemetryResult shadow =
			BotAI::PolicyShadowTelemetrySerializer::Serialize(participants[index].Policies);
		if (!shadow)
			throw std::invalid_argument("shadow policy telemetry is invalid: " + shadow.Error);
		if (index != 0)
			output << ',';
		output << "{\"roster_index\":" << participants[index].RosterIndex
			<< ",\"identity\":" << JsonString(participants[index].Identity)
			<< ",\"available\":" << (participants[index].Available ? "true" : "false")
			<< ",\"shadow\":" << shadow.Json << '}';
	}
	output << "]}\n";
	return output.str();
}
