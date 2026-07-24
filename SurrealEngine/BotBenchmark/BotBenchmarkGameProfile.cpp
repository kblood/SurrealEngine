#include "BotBenchmarkGameProfile.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <utility>

namespace
{
	std::string NormalizeAlphanumeric(std::string_view value)
	{
		std::string normalized;
		normalized.reserve(value.size());
		for (const unsigned char character : value)
		{
			if (std::isalnum(character))
				normalized.push_back(static_cast<char>(std::tolower(character)));
		}
		return normalized;
	}

	bool IsOneOf(const std::string& value, std::initializer_list<const char*> candidates)
	{
		return std::any_of(candidates.begin(), candidates.end(), [&value](const char* candidate)
		{
			return value == candidate;
		});
	}

	std::vector<BotBenchmarkMapFeature> ArenaMapFeatures()
	{
		return {
			BotBenchmarkMapFeature::BasicNavigation,
			BotBenchmarkMapFeature::TightCorridors,
			BotBenchmarkMapFeature::VerticalTraversal,
			BotBenchmarkMapFeature::LiftMovers,
			BotBenchmarkMapFeature::DoorMovers,
			BotBenchmarkMapFeature::WaterTraversal,
			BotBenchmarkMapFeature::LowGravity,
			BotBenchmarkMapFeature::EnvironmentalHazards,
			BotBenchmarkMapFeature::LongSightlines,
			BotBenchmarkMapFeature::InventoryRouting
		};
	}

	std::vector<BotBenchmarkMapFeature> UnrealMapFeatures()
	{
		auto features = ArenaMapFeatures();
		features.push_back(BotBenchmarkMapFeature::ScriptedObjectives);
		features.push_back(BotBenchmarkMapFeature::DarkVisibility);
		return features;
	}

	BotBenchmarkGameProfile UnsupportedProfile(
		BotBenchmarkGameFamily family,
		std::string id,
		std::string identity,
		std::string version,
		std::string reason)
	{
		BotBenchmarkGameProfile profile;
		profile.Id = std::move(id);
		profile.Family = family;
		profile.NormalizedGameIdentity = std::move(identity);
		profile.NormalizedVersion = std::move(version);
		profile.UnsupportedReason = std::move(reason);
		return profile;
	}

	BotBenchmarkGameProfile MakeUT99Profile(std::string identity, std::string version)
	{
		BotBenchmarkGameProfile profile;
		profile.Id = "ut99-436-deathmatch";
		profile.Family = BotBenchmarkGameFamily::UnrealTournament99;
		profile.NormalizedGameIdentity = std::move(identity);
		profile.NormalizedVersion = std::move(version);
		profile.ControlledBenchmarkSupported = true;
		profile.Modes = {
			{ BotBenchmarkGameMode::Deathmatch, "Botpack.DeathMatchPlus", true }
		};
		profile.Capabilities = {
			BotBenchmarkCapability::SpectatorLogin,
			BotBenchmarkCapability::ControlledBotSpawn,
			BotBenchmarkCapability::BotConfigSkillControl,
			BotBenchmarkCapability::AutomaticBotSuppression,
			BotBenchmarkCapability::DeterministicSingleBotRoster,
			BotBenchmarkCapability::VerifiedBotClassCatalog
		};
		profile.RecommendedMapFeatures = ArenaMapFeatures();
		profile.Spawn.GameClass = "Botpack.DeathMatchPlus";
		profile.Spawn.SpectatorClass = "Botpack.CHSpectator";
		profile.Spawn.BotBaseClass = "Botpack.Bot";
		profile.Spawn.BotBaseClassName = "Bot";
		profile.Spawn.VerifiedBotClasses = { "Botpack.Bot" };
		profile.Spawn.BotConfigProperty = "BotConfig";
		profile.Spawn.SpawnCommand = "AddBots 1";
		profile.Spawn.SpectatorLogin = BotBenchmarkSpectatorLogin::OverrideClass;
		profile.Spawn.SkillInitialization = BotBenchmarkSkillInitialization::UT436InitializeSkill;
		profile.Spawn.MaximumExternalSkill = 7;
		profile.Spawn.SupportsRequestedNames = true;
		profile.Spawn.SuppressMinPlayers = true;
		return profile;
	}

	BotBenchmarkGameProfile MakeUnrealGold226Profile(std::string identity, std::string version)
	{
		BotBenchmarkGameProfile profile;
		profile.Id = "unreal-gold-226b-deathmatch";
		profile.Family = BotBenchmarkGameFamily::Unreal;
		profile.NormalizedGameIdentity = std::move(identity);
		profile.NormalizedVersion = std::move(version);
		profile.ControlledBenchmarkSupported = true;
		profile.Modes = {
			{ BotBenchmarkGameMode::Deathmatch, "UnrealShare.DeathMatchGame", true },
			{ BotBenchmarkGameMode::TeamDeathmatch, "UnrealShare.TeamGame", false },
			{ BotBenchmarkGameMode::Cooperative, "UnrealShare.CoopGame", false },
			{ BotBenchmarkGameMode::DarkMatch, "UnrealI.DarkMatch", false },
			{ BotBenchmarkGameMode::KingOfTheHill, "UnrealI.KingOfTheHill", false }
		};
		profile.Capabilities = {
			BotBenchmarkCapability::SpectatorLogin,
			BotBenchmarkCapability::ControlledBotSpawn,
			BotBenchmarkCapability::BotConfigSkillControl,
			BotBenchmarkCapability::AutomaticBotSuppression,
			BotBenchmarkCapability::DeterministicSingleBotRoster,
			BotBenchmarkCapability::VerifiedBotClassCatalog
		};
		profile.RecommendedMapFeatures = UnrealMapFeatures();
		profile.Spawn.GameClass = "UnrealShare.DeathMatchGame";
		profile.Spawn.SpectatorClass = "UnrealShare.UnrealSpectator";
		profile.Spawn.BotBaseClass = "UnrealShare.Bots";
		profile.Spawn.BotBaseClassName = "Bots";
		profile.Spawn.VerifiedBotClasses = {
			"UnrealShare.FemaleOneBot",
			"UnrealShare.MaleThreeBot",
			"UnrealI.FemaleTwoBot",
			"UnrealI.MaleOneBot",
			"UnrealI.MaleTwoBot",
			"UnrealI.SkaarjPlayerBot"
		};
		profile.Spawn.BotConfigProperty = "BotConfig";
		profile.Spawn.SpawnCommand = "AddBots 1";
		profile.Spawn.SpectatorLogin = BotBenchmarkSpectatorLogin::PlayerClass;
		profile.Spawn.SkillInitialization = BotBenchmarkSkillInitialization::Unreal226EffectiveSkill;
		profile.Spawn.MaximumExternalSkill = 3;
		profile.Spawn.SuppressMultiPlayerBots = true;
		profile.Spawn.DisableRandomBotOrder = true;
		profile.Spawn.RequireNumBotsAccounting = true;
		profile.Spawn.RequireBotPRIFlag = true;
		profile.Spawn.RequireVerifiedConcreteBotClass = true;
		return profile;
	}
}

bool BotBenchmarkGameProfile::SupportsMode(BotBenchmarkGameMode mode) const
{
	return std::any_of(Modes.begin(), Modes.end(), [mode](const BotBenchmarkModeProfile& candidate)
	{
		return candidate.Mode == mode && candidate.ControlledBenchmarkSupported;
	});
}

bool BotBenchmarkGameProfile::HasCapability(BotBenchmarkCapability capability) const
{
	return std::find(Capabilities.begin(), Capabilities.end(), capability) != Capabilities.end();
}

bool BotBenchmarkGameProfile::RecommendsMapFeature(BotBenchmarkMapFeature feature) const
{
	return std::find(RecommendedMapFeatures.begin(), RecommendedMapFeatures.end(), feature)
		!= RecommendedMapFeatures.end();
}

BotBenchmarkGameProfile BotBenchmarkGameProfileResolver::Resolve(
	std::string_view gameIdentity,
	std::string_view version)
{
	std::string identity = NormalizeGameIdentity(gameIdentity);
	std::string normalizedVersion = NormalizeVersion(version);

	if (IsOneOf(identity, { "unrealtournament", "unrealtournamentgoty", "ut99" }))
	{
		if (normalizedVersion == "436")
			return MakeUT99Profile(std::move(identity), std::move(normalizedVersion));

		return UnsupportedProfile(
			BotBenchmarkGameFamily::UnrealTournament99,
			"ut99-unverified-version",
			std::move(identity),
			std::move(normalizedVersion),
			"Unreal Tournament identity recognized, but the controlled profile is verified only for version 436. "
			"Verify this installation's Botpack scripts and benchmark lifecycle before enabling it.");
	}

	if (IsOneOf(identity, { "unreal", "unrealgold" }))
	{
		if (normalizedVersion == "226b")
			return MakeUnrealGold226Profile(std::move(identity), std::move(normalizedVersion));

		return UnsupportedProfile(
			BotBenchmarkGameFamily::Unreal,
			"unreal-unverified-version",
			std::move(identity),
			std::move(normalizedVersion),
			"Unreal identity recognized, but only the user-owned Unreal Gold 226b class catalog has been verified. "
			"Export this installation's game scripts and add a version-specific profile before benchmarking bots.");
	}

	return UnsupportedProfile(
		BotBenchmarkGameFamily::Unsupported,
		"unsupported-game",
		std::move(identity),
		std::move(normalizedVersion),
		"No verified bot benchmark profile matches this normalized game identity and version. "
		"Add an evidence-backed profile instead of reusing UT99 class or spawn assumptions.");
}

std::string BotBenchmarkGameProfileResolver::NormalizeGameIdentity(std::string_view gameIdentity)
{
	return NormalizeAlphanumeric(gameIdentity);
}

std::string BotBenchmarkGameProfileResolver::NormalizeVersion(std::string_view version)
{
	std::string normalized = NormalizeAlphanumeric(version);
	if (normalized.starts_with("version") && normalized.size() > 7)
		normalized.erase(0, 7);
	if (normalized.size() > 1 && normalized[0] == 'v' && std::isdigit(static_cast<unsigned char>(normalized[1])))
		normalized.erase(0, 1);
	return normalized;
}

const char* BotBenchmarkGameFamilyName(BotBenchmarkGameFamily family)
{
	switch (family)
	{
	case BotBenchmarkGameFamily::Unsupported: return "unsupported";
	case BotBenchmarkGameFamily::UnrealTournament99: return "ut99";
	case BotBenchmarkGameFamily::Unreal: return "unreal";
	}
	return "unsupported";
}

const char* BotBenchmarkGameModeName(BotBenchmarkGameMode mode)
{
	switch (mode)
	{
	case BotBenchmarkGameMode::Deathmatch: return "deathmatch";
	case BotBenchmarkGameMode::TeamDeathmatch: return "team-deathmatch";
	case BotBenchmarkGameMode::Cooperative: return "cooperative";
	case BotBenchmarkGameMode::DarkMatch: return "dark-match";
	case BotBenchmarkGameMode::KingOfTheHill: return "king-of-the-hill";
	}
	return "unknown";
}

const char* BotBenchmarkCapabilityName(BotBenchmarkCapability capability)
{
	switch (capability)
	{
	case BotBenchmarkCapability::SpectatorLogin: return "spectator-login";
	case BotBenchmarkCapability::ControlledBotSpawn: return "controlled-bot-spawn";
	case BotBenchmarkCapability::BotConfigSkillControl: return "bot-config-skill-control";
	case BotBenchmarkCapability::AutomaticBotSuppression: return "automatic-bot-suppression";
	case BotBenchmarkCapability::DeterministicSingleBotRoster: return "deterministic-single-bot-roster";
	case BotBenchmarkCapability::VerifiedBotClassCatalog: return "verified-bot-class-catalog";
	}
	return "unknown";
}

const char* BotBenchmarkMapFeatureName(BotBenchmarkMapFeature feature)
{
	switch (feature)
	{
	case BotBenchmarkMapFeature::BasicNavigation: return "basic-navigation";
	case BotBenchmarkMapFeature::TightCorridors: return "tight-corridors";
	case BotBenchmarkMapFeature::VerticalTraversal: return "vertical-traversal";
	case BotBenchmarkMapFeature::LiftMovers: return "lift-movers";
	case BotBenchmarkMapFeature::DoorMovers: return "door-movers";
	case BotBenchmarkMapFeature::WaterTraversal: return "water-traversal";
	case BotBenchmarkMapFeature::LowGravity: return "low-gravity";
	case BotBenchmarkMapFeature::EnvironmentalHazards: return "environmental-hazards";
	case BotBenchmarkMapFeature::LongSightlines: return "long-sightlines";
	case BotBenchmarkMapFeature::InventoryRouting: return "inventory-routing";
	case BotBenchmarkMapFeature::ScriptedObjectives: return "scripted-objectives";
	case BotBenchmarkMapFeature::DarkVisibility: return "dark-visibility";
	}
	return "unknown";
}
