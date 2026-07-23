#include "BotBenchmark/BotSpectatorMatch.h"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	int Fail(const std::string& message)
	{
		std::cerr << message << '\n';
		return 1;
	}

	bool Rejects(const std::function<void()>& action)
	{
		try
		{
			action();
		}
		catch (const std::invalid_argument&)
		{
			return true;
		}
		return false;
	}
}

int main()
{
	const BotSpectatorMatchConfig defaults = BotSpectatorMatchConfig::Parse({}, {}, {});
	if (defaults.GetURL() != "DM-Morbias][?Game=Botpack.DeathMatchPlus")
		return Fail("spectator match default URL changed");
	if (defaults.GetDurationSeconds() != 0.0 || defaults.GetRoster().GetCount() != 4)
		return Fail("spectator match defaults were not an unlimited four-bot match");
	for (const auto& participant : defaults.GetRoster().GetParticipants())
	{
		if (participant.ExternalSkill != 3)
			return Fail("spectator match default skill was not applied uniformly");
	}

	const BotSpectatorMatchConfig mixed = BotSpectatorMatchConfig::Parse(
		"DM-Deck16][?Game=Botpack.DeathMatchPlus", "1", "90.5", "3", "7,4,0");
	if (mixed.GetURL() != "DM-Deck16][?Game=Botpack.DeathMatchPlus" ||
		mixed.GetDurationSeconds() != 90.5 || mixed.GetRoster().GetCount() != 3)
		return Fail("explicit spectator match configuration was not preserved");
	if (mixed.GetRoster().GetParticipants()[0].ExternalSkill != 7 ||
		mixed.GetRoster().GetParticipants()[1].ExternalSkill != 4 ||
		mixed.GetRoster().GetParticipants()[2].ExternalSkill != 0)
		return Fail("spectator match per-bot skills were not preserved");

	for (const std::string& invalid : { "-1", "8", "3x" })
	{
		if (!Rejects([&] { BotSpectatorMatchConfig::Parse({}, invalid, {}); }))
			return Fail("invalid spectator difficulty was accepted: " + invalid);
	}
	for (const std::string& invalid : { "-1", "nan", "inf", "86401", "1x" })
	{
		if (!Rejects([&] { BotSpectatorMatchConfig::Parse({}, {}, invalid); }))
			return Fail("invalid spectator duration was accepted: " + invalid);
	}
	if (!Rejects([] { BotSpectatorMatchConfig::Parse({}, {}, {}, "3", "7,4"); }))
		return Fail("mismatched spectator skill list was accepted");
	return 0;
}
