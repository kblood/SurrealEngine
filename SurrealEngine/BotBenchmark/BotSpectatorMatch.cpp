#include "Precomp.h"
#include "BotSpectatorMatch.h"
#include "BotControlledMatch.h"
#include "Engine.h"
#include "UObject/UClient.h"
#include "UObject/ULevel.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"

#include <algorithm>
#include <utility>

namespace
{
	std::optional<std::string> OptionalArg(const char* name)
	{
		if (!commandline || !commandline->HasArg("", name))
			return {};
		return commandline->GetArg("", name);
	}
}

BotSpectatorMatch::BotSpectatorMatch(BotSpectatorMatchConfig config)
	: Config(std::move(config))
{
}

void BotSpectatorMatch::Setup(Engine& engine)
{
	const BotControlledMatchResult match = BotControlledMatch::Setup(
		engine, Config.GetURL(), Config.GetRoster());
	ControlledBots.clear();
	ControlledBots.reserve(match.Participants.size());
	for (const auto& participant : match.Participants)
		ControlledBots.push_back(participant.Pawn);
	FollowLiveBot(engine);
	LogMessage("Bot spectator match started: " + std::to_string(match.Participants.size()) +
		" bots on " + engine.LevelInfo->URL.Map +
		(Config.GetDurationSeconds() > 0.0
			? ", duration " + std::to_string(Config.GetDurationSeconds()) + " seconds"
			: ", unlimited duration"));
}

void BotSpectatorMatch::Tick(Engine& engine, float elapsedSeconds)
{
	FollowLiveBot(engine);
	ElapsedSeconds += static_cast<double>(elapsedSeconds);
	if (Config.GetDurationSeconds() > 0.0 && ElapsedSeconds >= Config.GetDurationSeconds())
	{
		LogMessage("Bot spectator match reached its requested duration");
		engine.quit = true;
	}
}

void BotSpectatorMatch::FollowLiveBot(Engine& engine)
{
	UPlayerPawn* spectator = engine.viewport ? engine.viewport->Actor() : nullptr;
	if (!spectator || spectator->bDeleteMe())
		return;
	UActor* current = spectator->ViewTarget();
	UPawn* currentPawn = UObject::TryCast<UPawn>(current);
	if (currentPawn && std::find(ControlledBots.begin(), ControlledBots.end(), currentPawn) != ControlledBots.end() &&
		!currentPawn->bDeleteMe() && currentPawn->Health() > 0)
	{
		spectator->bBehindView() = true;
		return;
	}

	spectator->ViewTarget() = nullptr;
	for (UPawn* pawn : ControlledBots)
	{
		if (!pawn || pawn->bDeleteMe() || pawn->Health() <= 0)
			continue;
		spectator->ViewTarget() = pawn;
		spectator->bBehindView() = true;
		LogMessage("Bot spectator camera following " + pawn->Name.ToString());
		return;
	}
}

std::unique_ptr<BotSpectatorMatch> CreateBotSpectatorMatchFromCommandLine()
{
	if (!commandline || !commandline->HasArg("", "--bot-spectator"))
		return {};
	return std::make_unique<BotSpectatorMatch>(BotSpectatorMatchConfig::Parse(
		commandline->GetArg("", "--bot-spectator-url"),
		commandline->GetArg("", "--bot-spectator-difficulty"),
		commandline->GetArg("", "--bot-spectator-seconds"),
		OptionalArg("--bot-spectator-bots"),
		OptionalArg("--bot-spectator-skills")));
}
