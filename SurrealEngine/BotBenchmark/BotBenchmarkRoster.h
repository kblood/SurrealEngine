#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct BotBenchmarkParticipantDescriptor
{
	size_t RosterIndex = 0;
	std::string RequestedName;
	int ExternalSkill = 0;
	std::string CanonicalIdentityFragment;
};

class BotBenchmarkRoster
{
public:
	static constexpr size_t MaximumParticipants = 16;

	static BotBenchmarkRoster Parse(
		std::optional<std::string> botCount,
		std::optional<std::string> perBotSkills,
		std::optional<std::string> requestedNames,
		int uniformDifficulty);

	size_t GetCount() const { return Participants.size(); }
	const std::vector<BotBenchmarkParticipantDescriptor>& GetParticipants() const { return Participants; }

private:
	explicit BotBenchmarkRoster(std::vector<BotBenchmarkParticipantDescriptor> participants);

	std::vector<BotBenchmarkParticipantDescriptor> Participants;
};
