#pragma once

#include "BotBenchmarkRoster.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class Engine;
class UPawn;
class UPlayerPawn;

struct BotControlledParticipant
{
	size_t RosterIndex = 0;
	std::string Identity;
	std::string Actor;
	std::string PlayerName;
	std::string ClassName;
	UPawn* Pawn = nullptr;
};

struct BotControlledMatchResult
{
	UPlayerPawn* Spectator = nullptr;
	std::vector<BotControlledParticipant> Participants;
};

// Creates the verified, bot-only UT436 deathmatch used by both the deterministic
// benchmark and the rendered spectator match. The viewport owns a spectator;
// only the explicitly requested bots participate in the game.
class BotControlledMatch
{
public:
	static BotControlledMatchResult Setup(
		Engine& engine,
		const std::string& url,
		const BotBenchmarkRoster& roster,
		const std::function<void(const BotControlledParticipant&)>& participantSpawned = {});
};
