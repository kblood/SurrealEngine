#include "BotBenchmark/BotBenchmarkRoster.h"

#include <functional>
#include <iostream>
#include <optional>
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
	const BotBenchmarkRoster defaults = BotBenchmarkRoster::Parse({}, {}, {}, 3);
	if (defaults.GetCount() != 1)
		return Fail("absent bot count did not default to one");
	const auto& defaultParticipant = defaults.GetParticipants().front();
	if (defaultParticipant.RosterIndex != 0 || !defaultParticipant.RequestedName.empty()
		|| defaultParticipant.ExternalSkill != 3
		|| defaultParticipant.CanonicalIdentityFragment
			!= "participant-v1:index=0;external_skill=3;requested_name_hex=")
		return Fail("default participant descriptor was not stable and complete");

	const BotBenchmarkRoster explicitRoster = BotBenchmarkRoster::Parse(
		std::string("3"), std::string("7, 0,4"), std::string(" Loque,\tTamerlane , Xan "), 2);
	const auto& participants = explicitRoster.GetParticipants();
	if (participants.size() != 3 || participants[0].RosterIndex != 0 || participants[1].RosterIndex != 1
		|| participants[2].RosterIndex != 2)
		return Fail("explicit roster ordering changed");
	if (participants[0].ExternalSkill != 7 || participants[1].ExternalSkill != 0
		|| participants[2].ExternalSkill != 4)
		return Fail("per-bot skills were not preserved in roster order");
	if (participants[0].RequestedName != "Loque" || participants[1].RequestedName != "Tamerlane"
		|| participants[2].RequestedName != "Xan")
		return Fail("requested bot names were not trimmed and preserved");
	if (participants[0].CanonicalIdentityFragment
		!= "participant-v1:index=0;external_skill=7;requested_name_hex=4c6f717565")
		return Fail("canonical participant identity did not bind ordered skill and exact name bytes");

	const BotBenchmarkRoster uniform = BotBenchmarkRoster::Parse(std::string("2"), {}, {}, 6);
	if (uniform.GetParticipants()[0].ExternalSkill != 6 || uniform.GetParticipants()[1].ExternalSkill != 6)
		return Fail("absent skill list did not repeat uniform difficulty");

	for (const std::string& invalidCount : {
		std::string(""), std::string("0"), std::string("17"), std::string("-1"),
		std::string("+1"), std::string("two"), std::string("2x"),
		std::string("999999999999999999999999999999999999") })
	{
		if (!Rejects([&] { BotBenchmarkRoster::Parse(invalidCount, {}, {}, 3); }))
			return Fail("invalid or overflowing bot count was accepted: " + invalidCount);
	}

	for (const std::string& invalidSkills : {
		std::string("7"), std::string("7,,4"), std::string("7,4,"), std::string(",7,4"),
		std::string("7,-1,4"), std::string("7,8,4"), std::string("7,no,4"),
		std::string("7,999999999999999999999999999999,4") })
	{
		if (!Rejects([&] { BotBenchmarkRoster::Parse(std::string("3"), invalidSkills, {}, 3); }))
			return Fail("invalid, empty, mismatched, or overflowing skill list was accepted: " + invalidSkills);
	}
	if (!Rejects([] { BotBenchmarkRoster::Parse(std::string("1"), std::string(""), {}, 3); }))
		return Fail("present empty skill list was treated as absent");

	for (const std::string& invalidNames : {
		std::string("Loque"), std::string("Loque,,Xan"), std::string("Loque,   ,Xan"),
		std::string("Loque,Tamerlane,"), std::string("Loque,loQUE,Xan") })
	{
		if (!Rejects([&] { BotBenchmarkRoster::Parse(std::string("3"), {}, invalidNames, 3); }))
			return Fail("invalid, empty, mismatched, or duplicate name list was accepted: " + invalidNames);
	}
	if (!Rejects([] { BotBenchmarkRoster::Parse(std::string("1"), {}, std::string(""), 3); }))
		return Fail("present empty name list was treated as absent");

	if (!Rejects([] { BotBenchmarkRoster::Parse({}, {}, {}, -1); })
		|| !Rejects([] { BotBenchmarkRoster::Parse({}, std::string("7"), {}, 8); }))
		return Fail("invalid uniform difficulty was accepted");

	return 0;
}
