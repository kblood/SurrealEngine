#pragma once

#include <string>
#include <string_view>
#include <vector>

enum class BotBenchmarkGameFamily
{
	Unsupported,
	UnrealTournament99,
	Unreal
};

enum class BotBenchmarkGameMode
{
	Deathmatch,
	TeamDeathmatch,
	Cooperative,
	DarkMatch,
	KingOfTheHill
};

enum class BotBenchmarkCapability
{
	SpectatorLogin,
	ControlledBotSpawn,
	BotConfigSkillControl,
	AutomaticBotSuppression,
	DeterministicSingleBotRoster,
	VerifiedBotClassCatalog
};

enum class BotBenchmarkMapFeature
{
	BasicNavigation,
	TightCorridors,
	VerticalTraversal,
	LiftMovers,
	DoorMovers,
	WaterTraversal,
	LowGravity,
	EnvironmentalHazards,
	LongSightlines,
	InventoryRouting,
	ScriptedObjectives,
	DarkVisibility
};

enum class BotBenchmarkSpectatorLogin
{
	OverrideClass,
	PlayerClass
};

enum class BotBenchmarkSkillInitialization
{
	UT436InitializeSkill,
	Unreal226EffectiveSkill
};

struct BotBenchmarkModeProfile
{
	BotBenchmarkGameMode Mode = BotBenchmarkGameMode::Deathmatch;
	std::string GameClass;
	bool ControlledBenchmarkSupported = false;
};

struct BotBenchmarkSpawnContract
{
	std::string GameClass;
	std::string SpectatorClass;
	std::string BotBaseClass;
	std::string BotBaseClassName;
	std::vector<std::string> VerifiedBotClasses;
	std::string BotConfigProperty;
	std::string SpawnCommand;
	BotBenchmarkSpectatorLogin SpectatorLogin = BotBenchmarkSpectatorLogin::OverrideClass;
	BotBenchmarkSkillInitialization SkillInitialization = BotBenchmarkSkillInitialization::UT436InitializeSkill;
	int MaximumExternalSkill = -1;
	bool SupportsRequestedNames = false;
	bool SuppressMinPlayers = false;
	bool SuppressMultiPlayerBots = false;
	bool DisableRandomBotOrder = false;
	bool RequireNumBotsAccounting = false;
	bool RequireBotPRIFlag = false;
	bool RequireVerifiedConcreteBotClass = false;

	bool SupportsExternalSkill(int skill) const
	{
		return skill >= 0 && skill <= MaximumExternalSkill;
	}
};

struct BotBenchmarkGameProfile
{
	std::string Id;
	BotBenchmarkGameFamily Family = BotBenchmarkGameFamily::Unsupported;
	std::string NormalizedGameIdentity;
	std::string NormalizedVersion;
	bool ControlledBenchmarkSupported = false;
	std::string UnsupportedReason;
	std::vector<BotBenchmarkModeProfile> Modes;
	std::vector<BotBenchmarkCapability> Capabilities;
	std::vector<BotBenchmarkMapFeature> RecommendedMapFeatures;
	BotBenchmarkSpawnContract Spawn;

	bool SupportsMode(BotBenchmarkGameMode mode) const;
	bool HasCapability(BotBenchmarkCapability capability) const;
	bool RecommendsMapFeature(BotBenchmarkMapFeature feature) const;
};

class BotBenchmarkGameProfileResolver
{
public:
	static BotBenchmarkGameProfile Resolve(std::string_view gameIdentity, std::string_view version);
	static std::string NormalizeGameIdentity(std::string_view gameIdentity);
	static std::string NormalizeVersion(std::string_view version);
};

const char* BotBenchmarkGameFamilyName(BotBenchmarkGameFamily family);
const char* BotBenchmarkGameModeName(BotBenchmarkGameMode mode);
const char* BotBenchmarkCapabilityName(BotBenchmarkCapability capability);
const char* BotBenchmarkMapFeatureName(BotBenchmarkMapFeature feature);
