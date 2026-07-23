#include "BotBenchmarkRoster.h"

#include <charconv>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace
{
	bool IsAsciiWhitespace(char character)
	{
		return character == ' ' || character == '\t' || character == '\r' || character == '\n'
			|| character == '\f' || character == '\v';
	}

	std::string Trim(std::string value)
	{
		size_t first = 0;
		while (first < value.size() && IsAsciiWhitespace(value[first]))
			first++;
		size_t last = value.size();
		while (last > first && IsAsciiWhitespace(value[last - 1]))
			last--;
		return value.substr(first, last - first);
	}

	std::vector<std::string> Split(const std::string& value)
	{
		std::vector<std::string> entries;
		size_t start = 0;
		while (true)
		{
			const size_t separator = value.find(',', start);
			entries.push_back(value.substr(start, separator == std::string::npos ? separator : separator - start));
			if (separator == std::string::npos)
				break;
			start = separator + 1;
		}
		return entries;
	}

	int ParseDecimal(const std::string& text, const char* field)
	{
		if (text.empty())
			throw std::invalid_argument(std::string(field) + " must not be empty");
		int value = 0;
		const char* first = text.data();
		const char* last = first + text.size();
		const auto result = std::from_chars(first, last, value);
		if (result.ec == std::errc::result_out_of_range)
			throw std::invalid_argument(std::string(field) + " is out of range");
		if (result.ec != std::errc() || result.ptr != last)
			throw std::invalid_argument(std::string(field) + " must be a decimal integer");
		return value;
	}

	std::string AsciiLower(std::string value)
	{
		for (char& character : value)
		{
			if (character >= 'A' && character <= 'Z')
				character = static_cast<char>(character + ('a' - 'A'));
		}
		return value;
	}

	std::string HexBytes(const std::string& value)
	{
		static constexpr char Digits[] = "0123456789abcdef";
		std::string result;
		result.reserve(value.size() * 2);
		for (unsigned char byte : value)
		{
			result.push_back(Digits[byte >> 4]);
			result.push_back(Digits[byte & 0x0f]);
		}
		return result;
	}

	std::string IdentityFragment(size_t index, int skill, const std::string& name)
	{
		return "participant-v1:index=" + std::to_string(index)
			+ ";external_skill=" + std::to_string(skill)
			+ ";requested_name_hex=" + HexBytes(name);
	}
}

BotBenchmarkRoster::BotBenchmarkRoster(std::vector<BotBenchmarkParticipantDescriptor> participants)
	: Participants(std::move(participants))
{
}

BotBenchmarkRoster BotBenchmarkRoster::Parse(
	std::optional<std::string> botCount,
	std::optional<std::string> perBotSkills,
	std::optional<std::string> requestedNames,
	int uniformDifficulty)
{
	if (uniformDifficulty < 0 || uniformDifficulty > 7)
		throw std::invalid_argument("uniform bot difficulty must be between 0 and 7");

	const int count = botCount ? ParseDecimal(*botCount, "bot count") : 1;
	if (count < 1 || count > static_cast<int>(MaximumParticipants))
		throw std::invalid_argument("bot count must be between 1 and 16");

	std::vector<int> skills;
	if (perBotSkills)
	{
		for (std::string entry : Split(*perBotSkills))
		{
			entry = Trim(std::move(entry));
			if (entry.empty())
				throw std::invalid_argument("per-bot skills contains an empty entry");
			const int skill = ParseDecimal(entry, "per-bot skill");
			if (skill < 0 || skill > 7)
				throw std::invalid_argument("per-bot skills must be between 0 and 7");
			skills.push_back(skill);
		}
		if (skills.size() != static_cast<size_t>(count))
			throw std::invalid_argument("per-bot skill count must match bot count");
	}
	else
	{
		skills.assign(static_cast<size_t>(count), uniformDifficulty);
	}

	std::vector<std::string> names(static_cast<size_t>(count));
	if (requestedNames)
	{
		auto parsedNames = Split(*requestedNames);
		std::unordered_set<std::string> normalizedNames;
		for (std::string& name : parsedNames)
		{
			name = Trim(std::move(name));
			if (name.empty())
				throw std::invalid_argument("requested bot names contains an empty entry");
			if (!normalizedNames.insert(AsciiLower(name)).second)
				throw std::invalid_argument("requested bot names must be unique case-insensitively");
		}
		if (parsedNames.size() != static_cast<size_t>(count))
			throw std::invalid_argument("requested bot name count must match bot count");
		names = std::move(parsedNames);
	}

	std::vector<BotBenchmarkParticipantDescriptor> participants;
	participants.reserve(static_cast<size_t>(count));
	for (size_t index = 0; index < static_cast<size_t>(count); index++)
	{
		BotBenchmarkParticipantDescriptor participant;
		participant.RosterIndex = index;
		participant.RequestedName = std::move(names[index]);
		participant.ExternalSkill = skills[index];
		participant.CanonicalIdentityFragment =
			IdentityFragment(index, participant.ExternalSkill, participant.RequestedName);
		participants.push_back(std::move(participant));
	}
	return BotBenchmarkRoster(std::move(participants));
}
