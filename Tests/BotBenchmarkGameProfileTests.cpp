#include "BotBenchmark/BotBenchmarkGameProfile.h"

#include <algorithm>
#include <iostream>
#include <string>

namespace
{
	int failures = 0;

	void Check(bool condition, const std::string& message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			failures++;
		}
	}

	bool HasClass(const BotBenchmarkGameProfile& profile, const std::string& className)
	{
		return std::find(profile.Spawn.VerifiedBotClasses.begin(), profile.Spawn.VerifiedBotClasses.end(), className)
			!= profile.Spawn.VerifiedBotClasses.end();
	}

	void TestNormalization()
	{
		Check(BotBenchmarkGameProfileResolver::NormalizeGameIdentity(" Unreal Tournament: GOTY ")
			== "unrealtournamentgoty", "game identity normalization is case and punctuation independent");
		Check(BotBenchmarkGameProfileResolver::NormalizeVersion(" Version 436 ") == "436",
			"version label is normalized");
		Check(BotBenchmarkGameProfileResolver::NormalizeVersion("v226B") == "226b",
			"version prefix and case are normalized");
	}

	void TestUT99Profile()
	{
		const auto profile = BotBenchmarkGameProfileResolver::Resolve(" Unreal Tournament ", "Version 436");
		Check(profile.Id == "ut99-436-deathmatch", "UT99 resolves to the verified profile");
		Check(profile.Family == BotBenchmarkGameFamily::UnrealTournament99, "UT99 family is preserved");
		Check(profile.ControlledBenchmarkSupported, "UT99 436 controlled benchmark is supported");
		Check(profile.UnsupportedReason.empty(), "supported profile has no unsupported reason");
		Check(profile.Spawn.GameClass == "Botpack.DeathMatchPlus", "UT99 game class is exact");
		Check(profile.Spawn.SpectatorClass == "Botpack.CHSpectator", "UT99 spectator class is exact");
		Check(profile.Spawn.BotConfigProperty == "BotConfig", "UT99 requires BotConfig");
		Check(profile.Spawn.SpawnCommand == "AddBots 1", "UT99 uses the verified AddBots command");
		Check(profile.SupportsMode(BotBenchmarkGameMode::Deathmatch), "UT99 deathmatch mode is supported");
		Check(!profile.SupportsMode(BotBenchmarkGameMode::TeamDeathmatch),
			"unverified UT99 benchmark modes are not implied");
		Check(profile.HasCapability(BotBenchmarkCapability::ControlledBotSpawn),
			"UT99 advertises controlled spawning");
		Check(profile.HasCapability(BotBenchmarkCapability::BotConfigSkillControl),
			"UT99 advertises verified skill control");
		Check(profile.RecommendsMapFeature(BotBenchmarkMapFeature::LiftMovers),
			"UT99 matrix includes lift traversal");
		Check(profile.RecommendsMapFeature(BotBenchmarkMapFeature::LowGravity),
			"UT99 matrix includes low-gravity traversal");
	}

	void TestUT99VersionGateAndDeterminism()
	{
		const auto first = BotBenchmarkGameProfileResolver::Resolve("UT99", "451b");
		const auto second = BotBenchmarkGameProfileResolver::Resolve("ut-99", "V451B");
		Check(first.Family == BotBenchmarkGameFamily::UnrealTournament99, "unverified UT version keeps its family");
		Check(!first.ControlledBenchmarkSupported, "unverified UT version is blocked");
		Check(first.Id == second.Id && first.NormalizedGameIdentity == second.NormalizedGameIdentity
			&& first.NormalizedVersion == second.NormalizedVersion && first.UnsupportedReason == second.UnsupportedReason,
			"equivalent normalized UT inputs resolve deterministically");
		Check(first.Spawn.GameClass.empty() && first.Spawn.SpawnCommand.empty(),
			"unverified version does not inherit a guessed UT spawn contract");
		Check(first.UnsupportedReason.find("version 436") != std::string::npos,
			"unverified version gives an actionable verified-version boundary");
	}

	void TestRecognizedUnrealProfileIsConservative()
	{
		const auto profile = BotBenchmarkGameProfileResolver::Resolve("Unreal Gold", "226B");
		Check(profile.Id == "unreal-gold-226b-recognized", "Unreal Gold 226b resolves to the recognized profile");
		Check(profile.Family == BotBenchmarkGameFamily::Unreal, "Unreal family is preserved");
		Check(!profile.ControlledBenchmarkSupported, "Unreal benchmark remains unsupported");
		Check(profile.Spawn.GameClass == "UnrealShare.DeathMatchGame", "verified Unreal game class is recorded");
		Check(profile.Spawn.SpectatorClass == "UnrealShare.UnrealSpectator",
			"verified Unreal spectator class is recorded");
		Check(profile.Spawn.BotConfigProperty.empty() && profile.Spawn.SpawnCommand.empty(),
			"Unreal does not borrow unverified BotConfig/AddBots requirements");
		Check(HasClass(profile, "UnrealShare.FemaleOneBot") && HasClass(profile, "UnrealI.SkaarjPlayerBot"),
			"verified Unreal bot class catalog is recorded");
		Check(!profile.SupportsMode(BotBenchmarkGameMode::Deathmatch),
			"a verified game class does not imply benchmark support");
		Check(!profile.HasCapability(BotBenchmarkCapability::ControlledBotSpawn),
			"Unreal does not advertise unverified controlled spawning");
		Check(profile.HasCapability(BotBenchmarkCapability::VerifiedBotClassCatalog),
			"Unreal advertises only its verified class catalog");
		Check(profile.RecommendsMapFeature(BotBenchmarkMapFeature::ScriptedObjectives),
			"future Unreal matrix covers scripted traversal");
		Check(profile.RecommendsMapFeature(BotBenchmarkMapFeature::DarkVisibility),
			"future Unreal matrix covers dark visibility");
		Check(profile.UnsupportedReason.find("dedicated spawn adapter") != std::string::npos,
			"Unreal failure gives an actionable implementation requirement");
	}

	void TestUnknownAndUnverifiedUnrealStayEmpty()
	{
		const auto unverifiedUnreal = BotBenchmarkGameProfileResolver::Resolve("Unreal", "225f");
		Check(unverifiedUnreal.Family == BotBenchmarkGameFamily::Unreal,
			"unverified Unreal version retains recognized family");
		Check(unverifiedUnreal.Spawn.GameClass.empty() && unverifiedUnreal.Modes.empty(),
			"unverified Unreal version gets no copied class contract");

		const auto unknown = BotBenchmarkGameProfileResolver::Resolve("Deus Ex", "1112fm");
		Check(unknown.Family == BotBenchmarkGameFamily::Unsupported, "unknown game is unsupported");
		Check(!unknown.ControlledBenchmarkSupported && unknown.Spawn.VerifiedBotClasses.empty(),
			"unknown game has no inferred bot support");
		Check(unknown.UnsupportedReason.find("evidence-backed profile") != std::string::npos,
			"unknown game reports the evidence requirement");
	}
}

int main()
{
	TestNormalization();
	TestUT99Profile();
	TestUT99VersionGateAndDeterminism();
	TestRecognizedUnrealProfileIsConservative();
	TestUnknownAndUnverifiedUnrealStayEmpty();

	if (failures == 0)
		std::cout << "All bot benchmark game profile tests passed.\n";
	return failures == 0 ? 0 : 1;
}
