#include "Precomp.h"
#include "BotControlledMatch.h"
#include "BotBenchmarkGameProfile.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "UObject/UClient.h"
#include "UObject/ULevel.h"
#include "VM/ScriptCall.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace
{
	bool AsciiCaseInsensitiveEqual(const std::string& left, const std::string& right)
	{
		if (left.size() != right.size())
			return false;
		for (size_t index = 0; index < left.size(); index++)
		{
			auto lower = [](unsigned char character)
			{
				return character >= 'A' && character <= 'Z'
					? static_cast<unsigned char>(character + ('a' - 'A')) : character;
			};
			if (lower(static_cast<unsigned char>(left[index])) !=
				lower(static_cast<unsigned char>(right[index])))
				return false;
		}
		return true;
	}

	std::set<UPawn*> CaptureBots(Engine& engine)
	{
		std::set<UPawn*> bots;
		if (!engine.Level)
			return bots;
		for (UActor* actor : engine.Level->Actors)
		{
			UPawn* pawn = UObject::TryCast<UPawn>(actor);
			if (pawn && pawn->IsA("Bot") && !pawn->bDeleteMe())
				bots.insert(pawn);
		}
		return bots;
	}
}

BotControlledMatchResult BotControlledMatch::Setup(
	Engine& engine,
	const std::string& requestedURL,
	const BotBenchmarkRoster& roster,
	const std::function<void(const BotControlledParticipant&)>& participantSpawned)
{
	const BotBenchmarkGameProfile profile = BotBenchmarkGameProfileResolver::Resolve(
		engine.LaunchInfo.gameName, engine.LaunchInfo.gameVersionString);
	if (!profile.ControlledBenchmarkSupported)
		throw std::runtime_error("controlled bot match profile is unsupported: " + profile.UnsupportedReason);
	if (!profile.SupportsMode(BotBenchmarkGameMode::Deathmatch))
		throw std::runtime_error("controlled bot match profile has no verified deathmatch mode");

	engine.LaunchInfo.noEntryMap = true;
	engine.LaunchInfo.url = requestedURL;
	UnrealURL url(engine.GetDefaultURL(engine.packages->GetIniValue("system", "URL", "LocalMap")), requestedURL);
	url.AddOrReplaceOption("Bots=0");
	url.AddOrReplaceOption("MinPlayers=0");
	url.AddOrReplaceOption("InitialBots=0");
	url.AddOrReplaceOption("OverrideClass=" + profile.Spawn.SpectatorClass);
	engine.LoadMap(url);

	if (engine.GameInfo)
	{
		if (engine.GameInfo->HasProperty("RemainingBots"))
			engine.GameInfo->SetInt("RemainingBots", 0);
		if (engine.GameInfo->HasProperty("InitialBots"))
			engine.GameInfo->SetInt("InitialBots", 0);
		if (engine.GameInfo->HasProperty("MinPlayers"))
			engine.GameInfo->SetInt("MinPlayers", 0);
	}
	engine.LoginPlayer();

	BotControlledMatchResult result;
	result.Spectator = engine.viewport ? engine.viewport->Actor() : nullptr;
	UPlayerReplicationInfo* spectatorPRI = result.Spectator ? result.Spectator->PlayerReplicationInfo() : nullptr;
	if (!result.Spectator || !result.Spectator->IsA("Spectator") ||
		!spectatorPRI || !spectatorPRI->bIsSpectator())
		throw std::runtime_error("controlled bot match viewport login did not produce a spectator");

	if (!engine.GameInfo || !engine.GameInfo->HasProperty(profile.Spawn.BotConfigProperty))
		throw std::runtime_error("GameInfo has no " + profile.Spawn.BotConfigProperty +
			"; " + profile.Spawn.GameClass + " is required");
	UObject* botConfig = engine.GameInfo->GetUObject(profile.Spawn.BotConfigProperty);
	if (!botConfig || !botConfig->HasProperty("Difficulty"))
		throw std::runtime_error("BotConfig has no Difficulty property");

	if (engine.GameInfo->HasProperty("MinPlayers"))
		engine.GameInfo->SetInt("MinPlayers", 0);
	if (engine.GameInfo->HasProperty("InitialBots"))
		engine.GameInfo->SetInt("InitialBots", 0);
	if (engine.GameInfo->HasProperty("bRequireReady"))
		engine.GameInfo->SetBool("bRequireReady", false);
	if (botConfig->HasProperty("bAdjustSkill"))
		botConfig->SetBool("bAdjustSkill", false);
	if (botConfig->HasProperty("MinPlayers"))
		botConfig->SetInt("MinPlayers", 0);
	if (botConfig->HasProperty("InitialBots"))
		botConfig->SetInt("InitialBots", 0);

	std::set<UPawn*> controlledBots;
	for (const auto& requested : roster.GetParticipants())
	{
		const std::set<UPawn*> existingBots = CaptureBots(engine);
		botConfig->SetInt("Difficulty", requested.ExternalSkill);
		const bool commandFound = requested.RequestedName.empty()
			? engine.ExecCommand({ "AddBots", "1" })
			: engine.ExecCommand({ "AddBotNamed", requested.RequestedName });
		if (!commandFound)
		{
			if (requested.RequestedName.empty())
				throw std::runtime_error("AddBots exec function was not found");
			throw std::runtime_error(
				"requested bot names require AddBotNamed, which is unavailable from the spectator");
		}

		std::vector<UPawn*> newBots;
		for (UPawn* pawn : CaptureBots(engine))
		{
			if (existingBots.find(pawn) == existingBots.end())
				newBots.push_back(pawn);
		}
		if (newBots.size() != 1)
			throw std::runtime_error("stock bot spawn command did not create exactly one controlled bot for roster index " +
				std::to_string(requested.RosterIndex));

		UPawn* bot = newBots.front();
		controlledBots.insert(bot);
		UPlayerReplicationInfo* pri = bot->PlayerReplicationInfo();
		if (!pri)
			throw std::runtime_error("spawned controlled bot has no PlayerReplicationInfo at roster index " +
				std::to_string(requested.RosterIndex));

		BotControlledParticipant actual;
		actual.RosterIndex = requested.RosterIndex;
		actual.Identity = "pri:" + std::to_string(pri->PlayerID());
		actual.Actor = bot->Name.ToString();
		actual.PlayerName = pri->PlayerName();
		actual.ClassName = UObject::GetUClassFullName(bot).ToString();
		actual.Pawn = bot;
		result.Participants.push_back(std::move(actual));
		if (participantSpawned)
			participantSpawned(result.Participants.back());
		if (!requested.RequestedName.empty() &&
			!AsciiCaseInsensitiveEqual(result.Participants.back().PlayerName, requested.RequestedName))
			throw std::runtime_error("AddBotNamed did not produce the requested profile at roster index " +
				std::to_string(requested.RosterIndex));

		CallEvent(bot, "InitializeSkill", {
			ExpressionValue::FloatValue(static_cast<float>(requested.ExternalSkill)) });
		const bool expectedNovice = requested.ExternalSkill < 4;
		const float expectedSkill = static_cast<float>(
			expectedNovice ? requested.ExternalSkill : requested.ExternalSkill - 4);
		if (!bot->HasProperty("bNovice") || bot->GetBool("bNovice") != expectedNovice ||
			std::abs(bot->Skill() - expectedSkill) >= 0.001f)
			throw std::runtime_error("spawned bot skill did not match requested external tier at roster index " +
				std::to_string(requested.RosterIndex));
	}

	const std::set<UPawn*> finalBots = CaptureBots(engine);
	if (finalBots != controlledBots || finalBots.size() != roster.GetCount() ||
		result.Participants.size() != roster.GetCount())
		throw std::runtime_error("controlled bot roster did not exactly match the requested ordered roster");
	return result;
}
