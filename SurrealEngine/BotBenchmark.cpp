
#include "Precomp.h"
#include "BotBenchmark.h"
#include "Engine.h"
#include "Utils/CommandLine.h"
#include "Utils/File.h"
#include "Utils/JsonValue.h"
#include "Utils/Random.h"
#include "Package/PackageManager.h"
#include "UObject/ULevel.h"
#include "UObject/UActor.h"
#include "VM/Frame.h"
#include "VM/Bytecode.h"
#include "UObject/UClient.h"
#include "VM/ScriptCall.h"
#include <cctype>
#include <array>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>

std::unique_ptr<BotBenchmark> BotBenchmark::Instance;

namespace
{
	std::string JsonString(const JsonValue& value, const std::string& fallback)
	{
		return value.type() == JsonType::string ? value.to_string() : fallback;
	}

	int JsonInt(const JsonValue& value, int fallback)
	{
		return value.type() == JsonType::number ? value.to_int() : fallback;
	}

	uint64_t JsonUInt64(const JsonValue& value, uint64_t fallback)
	{
		if (value.type() == JsonType::string)
			return std::stoull(value.to_string());
		if (value.type() == JsonType::number)
			return static_cast<uint64_t>(value.to_double());
		return fallback;
	}

	float JsonFloat(const JsonValue& value, float fallback)
	{
		return value.type() == JsonType::number ? value.to_float() : fallback;
	}

	std::vector<int> JsonSkills(const JsonValue& value)
	{
		std::vector<int> skills;
		if (value.is_undefined())
			return skills;
		if (!value.is_array())
			throw std::runtime_error("Bot benchmark skills must be a JSON array");
		for (const JsonValue& item : value.items())
		{
			if (!item.is_number() || item.to_double() != static_cast<double>(item.to_int()))
				throw std::runtime_error("Bot benchmark skills must contain integers");
			skills.push_back(item.to_int());
		}
		return skills;
	}

	std::vector<int> ParseSkills(const std::string& text)
	{
		std::vector<int> skills;
		std::stringstream input(text);
		std::string token;
		while (std::getline(input, token, ','))
		{
			if (token.empty())
				throw std::runtime_error("--botbench-skills contains an empty entry");
			size_t parsed = 0;
			const int skill = std::stoi(token, &parsed);
			if (parsed != token.size())
				throw std::runtime_error("--botbench-skills entries must be integers");
			skills.push_back(skill);
		}
		if (skills.empty())
			throw std::runtime_error("--botbench-skills must not be empty");
		return skills;
	}

	std::string JoinSkills(const std::vector<int>& skills)
	{
		std::ostringstream out;
		for (size_t i = 0; i < skills.size(); i++)
		{
			if (i != 0)
				out << ',';
			out << skills[i];
		}
		return out.str();
	}

	std::string ObjectName(UObject* object)
	{
		if (!object)
			return {};
		return UObject::GetUClassFullName(object).ToString() + ":" + object->Name.ToString();
	}

	std::string ClassName(UObject* object)
	{
		return object ? UObject::GetUClassFullName(object).ToString() : std::string();
	}

	std::string PawnIdentity(UPawn* pawn)
	{
		if (pawn)
		{
			if (UPlayerReplicationInfo* pri = pawn->PlayerReplicationInfo())
				return "pri:" + std::to_string(pri->PlayerID());
			return "actor:" + pawn->Name.ToString();
		}
		return {};
	}

	std::string ToString(uint64_t value)
	{
		return std::to_string(value);
	}

	std::string ToString(int value)
	{
		return std::to_string(value);
	}

	std::string ToString(float value)
	{
		std::ostringstream out;
		out << std::fixed << std::setprecision(6) << value;
		return out.str();
	}

	std::string LatentActionName(LatentRunState state)
	{
		switch (state)
		{
		case LatentRunState::MoveTo: return "MoveTo";
		case LatentRunState::MoveToward: return "MoveToward";
		case LatentRunState::StrafeTo: return "StrafeTo";
		case LatentRunState::StrafeFacing: return "StrafeFacing";
		case LatentRunState::TurnTo: return "TurnTo";
		case LatentRunState::TurnToward: return "TurnToward";
		case LatentRunState::WaitForLanding: return "WaitForLanding";
		case LatentRunState::Sleep: return "Sleep";
		case LatentRunState::FinishAnim: return "FinishAnim";
		case LatentRunState::FinishInterpolation: return "FinishInterpolation";
		case LatentRunState::Stop: return "Stop";
		default: return "Continue";
		}
	}

	bool IsMovementLatent(const std::string& action)
	{
		return action == "MoveTo" || action == "MoveToward" ||
			action == "StrafeTo" || action == "StrafeFacing";
	}
}

bool BotBenchmark::Requested(const CommandLine& commandLine)
{
	return commandLine.HasArg("", "--botbench");
}

void BotBenchmark::Configure(const CommandLine& commandLine)
{
	Instance = std::unique_ptr<BotBenchmark>(new BotBenchmark(LoadConfig(commandLine)));
	Instance->OpenOutput();
}

bool BotBenchmark::IsActive()
{
	return Instance != nullptr;
}

BotBenchmark& BotBenchmark::Get()
{
	if (!Instance)
		throw std::runtime_error("BotBenchmark is not configured");
	return *Instance;
}

void BotBenchmark::Emit(const std::string& type, const std::map<std::string, std::string>& fields)
{
	if (Instance && !Instance->Finalized)
	{
		// The controlled HitWall fixture's deterministic contract includes the
		// complete before/result callback protocol, not just the resulting pawn
		// snapshot and self-reported assertions. Keep this fixture-specific so
		// passive diagnostics do not change ordinary benchmark digests.
		if (Instance->Config.FixtureId == "controlled-hitwall-blockall-v1" &&
			(type == "walking_hit_wall" || type == "walking_hit_wall_result"))
		{
			std::string canonical = "fixture_wall_event_tick=" + std::to_string(Instance->Tick) +
				"|type=" + type;
			for (const auto& field : fields)
				canonical += "|" + field.first + "=" + field.second;
			Instance->HashCanonical(canonical + "\n");
		}
		Instance->WriteEvent(type, fields);
	}
}

uint64_t BotBenchmark::NextWalkingHitWallId()
{
	return Instance && !Instance->Finalized ? ++Instance->WalkingHitWallSequence : 0;
}

void BotBenchmark::WalkingHitWallResult(uint64_t hitId, UPawn* pawn, UActor* blocker,
	bool eventEnabled, float normalX, float normalY, float normalZ)
{
	if (Instance && !Instance->Finalized)
		Instance->RecordWalkingHitWallResult(hitId, pawn, blocker, eventEnabled, normalX, normalY, normalZ);
}

void BotBenchmark::BeginDamage(UPawn* victim, int requestedDamage, UPawn* instigator, UObject* source, const std::string& damageType)
{
	if (!Instance || Instance->Finalized || !victim)
		return;
	auto& observation = Instance->ActiveDamage[victim];
	if (observation.Depth++ == 0)
	{
		observation.HealthBefore = victim->Health();
		observation.RequestedDamage = requestedDamage;
		observation.Instigator = instigator;
		observation.Source = source;
		observation.DamageType = damageType;
	}
}

void BotBenchmark::EndDamage(UPawn* victim)
{
	if (!Instance || !victim)
		return;
	auto it = Instance->ActiveDamage.find(victim);
	if (it == Instance->ActiveDamage.end())
		return;
	if (--it->second.Depth == 0)
	{
		DamageObservation observation = it->second;
		Instance->ActiveDamage.erase(it);
		if (!Instance->Finalized)
			Instance->RecordDamage(victim, observation);
	}
}

std::string BotBenchmark::WeaponMode(UWeapon* weapon)
{
	if (!weapon)
		return "unknown";
	std::string state = weapon->GetStateName().ToString();
	for (char& c : state)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return state.find("alt") != std::string::npos ? "alt" : "primary";
}

void BotBenchmark::BeginHitscan(UWeapon* weapon)
{
	if (!Instance || Instance->Finalized || !weapon)
		return;
	UPawn* shooter = UObject::TryCast<UPawn>(weapon->Owner());
	if (!shooter || !shooter->IsA("Bot"))
		return;
	auto& observation = Instance->ActiveHitscan[weapon];
	if (observation.Depth++ == 0)
	{
		observation.Shooter = shooter;
		observation.WeaponMode = ClassName(weapon) + "|" + WeaponMode(weapon);
		observation.OpponentHealthDamage = 0;
		observation.SelfHealthDamage = 0;
		observation.OpponentDamageEvents = 0;
	}
}

void BotBenchmark::EndHitscan(UWeapon* weapon)
{
	if (!Instance || !weapon)
		return;
	auto it = Instance->ActiveHitscan.find(weapon);
	if (it == Instance->ActiveHitscan.end())
		return;
	if (--it->second.Depth != 0)
		return;
	HitscanObservation observation = it->second;
	Instance->ActiveHitscan.erase(it);
	if (Instance->Finalized || !observation.Shooter)
		return;

	auto& telemetry = Instance->TelemetryForPawn(observation.Shooter);
	auto& weaponStats = telemetry.WeaponCombatStats[observation.WeaponMode];
	weaponStats.HitscanShots++;
	const bool hit = observation.OpponentDamageEvents != 0;
	if (hit)
		weaponStats.HitscanHits++;
	Instance->WriteEvent("hitscan_shot", {
		{ "shooter", PawnIdentity(observation.Shooter) },
		{ "weapon_mode", observation.WeaponMode },
		{ "hit_opponent", hit ? "true" : "false" },
		{ "opponent_damage_events", ToString(observation.OpponentDamageEvents) },
		{ "opponent_health_damage", ToString(observation.OpponentHealthDamage) },
		{ "self_health_damage", ToString(observation.SelfHealthDamage) }
	});
}

void BotBenchmark::ProjectileSpawned(UProjectile* projectile, UActor* spawner)
{
	if (!Instance || Instance->Finalized || !projectile || projectile->Damage() <= 0.0f)
		return;
	UPawn* shooter = UObject::TryCast<UPawn>(projectile->Instigator());
	if (!shooter || !shooter->IsA("Bot"))
		return;
	UWeapon* weapon = UObject::TryCast<UWeapon>(spawner);
	if (!weapon)
		weapon = shooter->Weapon();
	ProjectileObservation observation;
	observation.ShooterIdentity = PawnIdentity(shooter);
	observation.ProjectileClass = ClassName(projectile);
	observation.WeaponMode = weapon ? ClassName(weapon) + "|" + WeaponMode(weapon) : observation.ProjectileClass + "|projectile";
	Instance->Projectiles[projectile] = observation;
	auto& stats = Instance->TelemetryForPawn(shooter).WeaponCombatStats[observation.WeaponMode];
	stats.ProjectileLaunches++;
	Instance->WriteEvent("projectile_launch", {
		{ "projectile", ObjectName(projectile) },
		{ "projectile_class", observation.ProjectileClass },
		{ "shooter", observation.ShooterIdentity },
		{ "weapon_mode", observation.WeaponMode }
	});
}

void BotBenchmark::ProjectileDestroyed(UProjectile* projectile)
{
	if (!Instance || !projectile)
		return;
	auto it = Instance->Projectiles.find(projectile);
	if (it == Instance->Projectiles.end())
		return;
	ProjectileObservation observation = it->second;
	Instance->Projectiles.erase(it);
	if (Instance->Finalized)
		return;
	auto telemetryIt = Instance->Telemetry.find(observation.ShooterIdentity);
	if (telemetryIt != Instance->Telemetry.end())
	{
		auto& stats = telemetryIt->second.WeaponCombatStats[observation.WeaponMode];
		if (observation.HitOpponent)
			stats.ProjectileHits++;
		else
			stats.ProjectileMisses++;
	}
	Instance->WriteEvent("projectile_result", {
		{ "projectile", ObjectName(projectile) },
		{ "projectile_class", observation.ProjectileClass },
		{ "shooter", observation.ShooterIdentity },
		{ "weapon_mode", observation.WeaponMode },
		{ "hit_opponent", observation.HitOpponent ? "true" : "false" },
		{ "opponent_health_damage", ToString(observation.OpponentHealthDamage) }
	});
}

BotBenchmarkConfig BotBenchmark::LoadConfig(const CommandLine& commandLine)
{
	BotBenchmarkConfig config;
	bool botCountExplicit = false;

	const std::string configPath = commandLine.GetArg("", "--botbench-config");
	if (!configPath.empty())
	{
		JsonValue root = JsonValue::parse(File::read_all_text(configPath));
		if (root.type() != JsonType::object)
			throw std::runtime_error("Bot benchmark config root must be a JSON object");

		config.Scenario = JsonString(root["scenario"], config.Scenario);
		config.OutputDirectory = JsonString(root["output"], config.OutputDirectory);
		config.URL = JsonString(root["url"], config.URL);
		config.BotName = JsonString(root["bot_name"], config.BotName);
		config.FixtureId = JsonString(root["fixture_id"], config.FixtureId);
		config.Seed = JsonUInt64(root["seed"], config.Seed);
		config.MaxTicks = JsonUInt64(root["ticks"], config.MaxTicks);
		config.FixedDelta = JsonFloat(root["fixed_delta"], config.FixedDelta);
		config.Difficulty = JsonInt(root["difficulty"], config.Difficulty);
		if (!root["bots"].is_undefined())
		{
			config.BotCount = JsonInt(root["bots"], config.BotCount);
			botCountExplicit = true;
		}
		config.BotSkills = JsonSkills(root["skills"]);
	}

	const std::string scenarioArg = commandLine.GetArg("", "--botbench");
	if (!scenarioArg.empty())
		config.Scenario = scenarioArg;
	if (commandLine.HasArg("", "--botbench-output"))
		config.OutputDirectory = commandLine.GetArg("", "--botbench-output");
	if (commandLine.HasArg("", "--botbench-url"))
		config.URL = commandLine.GetArg("", "--botbench-url");
	if (commandLine.HasArg("", "--botbench-bot-name"))
		config.BotName = commandLine.GetArg("", "--botbench-bot-name");
	if (commandLine.HasArg("", "--botbench-fixture"))
		config.FixtureId = commandLine.GetArg("", "--botbench-fixture");
	if (commandLine.HasArg("", "--botbench-seed"))
		config.Seed = std::stoull(commandLine.GetArg("", "--botbench-seed"));
	if (commandLine.HasArg("", "--botbench-ticks"))
		config.MaxTicks = std::stoull(commandLine.GetArg("", "--botbench-ticks"));
	if (commandLine.HasArg("", "--botbench-fixed-delta"))
		config.FixedDelta = std::stof(commandLine.GetArg("", "--botbench-fixed-delta"));
	if (commandLine.HasArg("", "--botbench-skill"))
		config.Difficulty = std::stoi(commandLine.GetArg("", "--botbench-skill"));
	if (commandLine.HasArg("", "--botbench-skills"))
		config.BotSkills = ParseSkills(commandLine.GetArg("", "--botbench-skills"));
	if (commandLine.HasArg("", "--botbench-bots"))
	{
		config.BotCount = std::stoi(commandLine.GetArg("", "--botbench-bots"));
		botCountExplicit = true;
	}

	if (!config.BotSkills.empty())
	{
		if (botCountExplicit && config.BotCount != static_cast<int>(config.BotSkills.size()))
			throw std::runtime_error("--botbench-bots must match --botbench-skills entry count");
		config.BotCount = static_cast<int>(config.BotSkills.size());
	}

	if (config.OutputDirectory.empty())
		throw std::runtime_error("--botbench-output must not be empty");
	if (config.URL.empty())
		throw std::runtime_error("--botbench-url must not be empty");
	if (config.MaxTicks == 0 || config.MaxTicks > 10'000'000)
		throw std::runtime_error("--botbench-ticks must be in [1, 10000000]");
	if (!std::isfinite(config.FixedDelta) || config.FixedDelta <= 0.0f || config.FixedDelta > 1.0f)
		throw std::runtime_error("--botbench-fixed-delta must be in (0, 1]");
	if (config.Difficulty < 0 || config.Difficulty > 7)
		throw std::runtime_error("--botbench-skill must be in [0, 7]");
	if (config.BotCount < 1 || config.BotCount > 32)
		throw std::runtime_error("--botbench-bots must be in [1, 32]");
	for (int skill : config.BotSkills)
	{
		if (skill < 0 || skill > 7)
			throw std::runtime_error("--botbench-skills entries must be in [0, 7]");
	}

	return config;
}

BotBenchmark::BotBenchmark(BotBenchmarkConfig config) : Config(std::move(config))
{
}

void BotBenchmark::OpenOutput()
{
	std::filesystem::create_directories(Config.OutputDirectory);
	const std::filesystem::path eventsPath = std::filesystem::path(Config.OutputDirectory) / "events.jsonl";
	EventsFile = File::create_always(eventsPath.string());
	const std::vector<int> requestedSkills = Config.BotSkills.empty()
		? std::vector<int>(static_cast<size_t>(Config.BotCount), Config.Difficulty)
		: Config.BotSkills;

	WriteEvent("run_config", {
		{ "scenario", Config.Scenario },
		{ "seed", ToString(Config.Seed) },
		{ "ticks", ToString(Config.MaxTicks) },
		{ "fixed_delta", ToString(Config.FixedDelta) },
		{ "difficulty", ToString(Config.Difficulty) },
		{ "requested_skills", JoinSkills(requestedSkills) },
		{ "bots", ToString(Config.BotCount) },
		{ "bot_name", Config.BotName },
		{ "fixture_id", Config.FixtureId },
		{ "url", Config.URL }
	});
}

void BotBenchmark::Initialize(Engine& engine)
{
	// Reset immediately before any benchmark bot is spawned. This isolates bot
	// randomness from package loading while the constructor-time reset keeps
	// map/player initialization repeatable as well.
	SetRandomSeed(Config.Seed);
	const std::vector<int> requestedSkills = Config.BotSkills.empty()
		? std::vector<int>(static_cast<size_t>(Config.BotCount), Config.Difficulty)
		: Config.BotSkills;
	HashCanonical("requested_skills=" + JoinSkills(requestedSkills) + "\n");
	if (!ValidateViewportSpectator(engine))
		return;
	ConfigureBots(engine);
	if (ExitCode != 0)
		return;
	RunControlledFixture(engine);
	if (ExitCode != 0)
		return;
	ObserveBots(engine);

	WriteEvent("simulation_start", {
		{ "game", engine.LaunchInfo.gameName },
		{ "version", engine.LaunchInfo.gameVersionString },
		{ "map", engine.LevelInfo ? engine.LevelInfo->URL.Map : std::string() }
	});
}

void BotBenchmark::RunControlledFixture(Engine& engine)
{
	if (Config.FixtureId.empty())
		return;

	FixtureStatus = "running";
	auto setupFailure = [&](const std::string& reason)
	{
		FixtureSetupFailure(reason);
	};
	auto assertion = [&](const std::string& name, bool expected, bool actual)
	{
		FixtureAssertion(name, expected, actual);
	};

	if (Config.FixtureId != "controlled-reachability-blockall-v1" &&
		Config.FixtureId != "controlled-hitwall-blockall-v1")
	{
		setupFailure("Unknown controlled fixture: " + Config.FixtureId);
		return;
	}
	if (Config.BotCount != 1)
	{
		setupFailure(Config.FixtureId + " requires exactly one bot");
		return;
	}

	UPawn* bot = nullptr;
	for (UActor* actor : engine.Level->Actors)
	{
		UPawn* candidate = UObject::TryCast<UPawn>(actor);
		if (candidate && candidate->IsA("Bot") && RequestedSkillByIdentity.count(PawnIdentity(candidate)) != 0)
		{
			bot = candidate;
			break;
		}
	}
	if (!bot)
	{
		setupFailure("Controlled fixture could not find its configured bot");
		return;
	}
	if (Config.FixtureId == "controlled-hitwall-blockall-v1")
	{
		FixtureBot = bot;
		FixturePhase = ControlledFixturePhase::AwaitWalking;
		FixturePhaseStartTick = Tick;
		WriteEvent("fixture_wait", {
			{ "fixture_id", Config.FixtureId },
			{ "bot", ObjectName(bot) },
			{ "condition", "PHYS_Walking" },
			{ "timeout_ticks", "60" }
		});
		HashCanonical("fixture_wait=" + Config.FixtureId + "|" + ObjectName(bot) + "|PHYS_Walking|60\n");
		return;
	}

	UClass* targetClass = engine.packages->FindClass("Engine.AmbientSound");
	UClass* blockerClass = engine.packages->FindClass("Engine.BlockAll");
	if (!targetClass || !blockerClass)
	{
		setupFailure("Controlled fixture requires Engine.AmbientSound and Engine.BlockAll");
		return;
	}

	const vec3 originalLocation = bot->Location();
	const vec3 originalVelocity = bot->Velocity();
	const vec3 originalAcceleration = bot->Acceleration();
	const uint8_t originalPhysics = bot->Physics();
	bot->SetPhysics(PHYS_Walking);
	bot->Velocity() = vec3(0.0f);
	bot->Acceleration() = vec3(0.0f);

	const std::array<vec2, 8> directions = {
		vec2(1.0f, 0.0f), vec2(-1.0f, 0.0f), vec2(0.0f, 1.0f), vec2(0.0f, -1.0f),
		normalize(vec2(1.0f, 1.0f)), normalize(vec2(1.0f, -1.0f)),
		normalize(vec2(-1.0f, 1.0f)), normalize(vec2(-1.0f, -1.0f))
	};

	UActor* target = nullptr;
	vec3 targetLocation;
	vec2 direction;
	auto prepareFixtureActor = [](UActor* actor)
	{
		if (actor)
		{
			// Keypoint-derived stock helpers are static/no-delete by default.
			// These actors exist only for this runtime fixture and must be removable.
			actor->bStatic() = false;
			actor->bNoDelete() = false;
		}
		return actor;
	};
	auto destroyFixtureActor = [](UActor* actor)
	{
		return !actor || actor->Destroy();
	};
	for (const vec2& candidateDirection : directions)
	{
		const vec3 candidateLocation = originalLocation + vec3(candidateDirection * 256.0f, 0.0f);
		UActor* candidate = prepareFixtureActor(bot->Spawn(targetClass, {}, {}, candidateLocation, {}));
		if (candidate && bot->ActorReachable(candidate, false))
		{
			target = candidate;
			targetLocation = candidate->Location();
			direction = candidateDirection;
			break;
		}
		if (!destroyFixtureActor(candidate))
		{
			bot->Velocity() = originalVelocity;
			bot->Acceleration() = originalAcceleration;
			bot->SetPhysics(originalPhysics);
			setupFailure("Could not remove a rejected fixture target");
			return;
		}
	}
	if (!target)
	{
		bot->Velocity() = originalVelocity;
		bot->Acceleration() = originalAcceleration;
		bot->SetPhysics(originalPhysics);
		setupFailure("No clear 256-unit reachability lane was found at the bot spawn");
		return;
	}

	const bool clearActorReachable = bot->ActorReachable(target, false);

	std::vector<UActor*> blockers;
	const vec2 perpendicular(-direction.y, direction.x);
	for (int offset = -256; offset <= 256; offset += 64)
	{
		const vec3 blockerLocation = originalLocation + vec3(direction * 128.0f + perpendicular * static_cast<float>(offset), 0.0f);
		UActor* blocker = prepareFixtureActor(bot->Spawn(blockerClass, {}, {}, blockerLocation, {}));
		if (!blocker)
		{
			for (UActor* spawnedBlocker : blockers)
				destroyFixtureActor(spawnedBlocker);
			destroyFixtureActor(target);
			bot->Velocity() = originalVelocity;
			bot->Acceleration() = originalAcceleration;
			bot->SetPhysics(originalPhysics);
			setupFailure("Could not spawn the complete BlockAll wall");
			return;
		}
		blocker->SetCollisionSize(36.0f, 96.0f);
		blocker->SetCollision(true, true, true);
		blockers.push_back(blocker);
	}

	WriteEvent("fixture_setup", {
		{ "fixture_id", Config.FixtureId },
		{ "bot", ObjectName(bot) },
		{ "target", ObjectName(target) },
		{ "blocker_count", ToString(static_cast<int>(blockers.size())) },
		{ "source_x", ToString(originalLocation.x) },
		{ "source_y", ToString(originalLocation.y) },
		{ "source_z", ToString(originalLocation.z) },
		{ "target_x", ToString(targetLocation.x) },
		{ "target_y", ToString(targetLocation.y) },
		{ "target_z", ToString(targetLocation.z) },
		{ "direction_x", ToString(direction.x) },
		{ "direction_y", ToString(direction.y) },
		{ "blocker_radius", ToString(36.0f) },
		{ "blocker_height", ToString(96.0f) },
		{ "blocker_spacing", ToString(64.0f) },
		{ "blocker_offset_min", ToString(-256.0f) },
		{ "blocker_offset_max", ToString(256.0f) },
		{ "original_physics", ToString(static_cast<int>(originalPhysics)) },
		{ "probe_physics", ToString(static_cast<int>(bot->Physics())) }
	});
	HashCanonical("fixture_setup=" + Config.FixtureId +
		"|source=" + ToString(originalLocation.x) + "," + ToString(originalLocation.y) + "," + ToString(originalLocation.z) +
		"|target=" + ToString(targetLocation.x) + "," + ToString(targetLocation.y) + "," + ToString(targetLocation.z) +
		"|direction=" + ToString(direction.x) + "," + ToString(direction.y) +
		"|blockers=" + std::to_string(blockers.size()) + "|36.000000|96.000000|64.000000|-256.000000|256.000000\n");

	assertion("clear_actor_reachable", true, clearActorReachable);
	const bool blockedActorReachable = bot->ActorReachable(target, false);
	assertion("blockall_wall_actor_reachable", false, blockedActorReachable);
	assertion("point_reachable_target_ignores_dynamic_actors", true, bot->PointReachable(targetLocation));
	assertion("point_reachable_blocker_ignores_dynamic_actors", true, bot->PointReachable(blockers[blockers.size() / 2]->Location()));
	assertion("reachability_dry_run_preserves_location", true, bot->Location() == originalLocation);

	bool cleanupSucceeded = true;
	for (UActor* blocker : blockers)
		cleanupSucceeded = destroyFixtureActor(blocker) && cleanupSucceeded;
	cleanupSucceeded = destroyFixtureActor(target) && cleanupSucceeded;
	bot->Velocity() = originalVelocity;
	bot->Acceleration() = originalAcceleration;
	bot->SetPhysics(originalPhysics);
	assertion("fixture_actors_destroyed", true, cleanupSucceeded);

	FixtureStatus = FixtureAssertionsFailed == 0 ? "passed" : "failed";
	WriteEvent("fixture_complete", {
		{ "fixture_id", Config.FixtureId },
		{ "status", FixtureStatus },
		{ "assertions_total", ToString(FixtureAssertionsTotal) },
		{ "assertions_passed", ToString(FixtureAssertionsPassed) },
		{ "assertions_failed", ToString(FixtureAssertionsFailed) }
	});
	if (FixtureAssertionsFailed != 0)
	{
		FailureReason = "Controlled fixture assertions failed";
		ExitCode = 5;
	}
}

void BotBenchmark::FixtureAssertion(const std::string& name, bool expected, bool actual)
{
	const bool passed = expected == actual;
	FixtureAssertionsTotal++;
	if (passed)
		FixtureAssertionsPassed++;
	else
		FixtureAssertionsFailed++;
	WriteEvent("fixture_assert", {
		{ "fixture_id", Config.FixtureId },
		{ "name", name },
		{ "expected", expected ? "true" : "false" },
		{ "actual", actual ? "true" : "false" },
		{ "passed", passed ? "true" : "false" }
	});
	HashCanonical("fixture_assert=" + Config.FixtureId + "|" + name + "|" +
		(expected ? "true" : "false") + "|" + (actual ? "true" : "false") + "\n");
}

void BotBenchmark::FixtureSetupFailure(const std::string& reason)
{
	CleanupFixtureActors();
	FixtureStatus = "setup_error";
	FixturePhase = ControlledFixturePhase::Complete;
	FailureReason = reason;
	ExitCode = 2;
	WriteEvent("fixture_error", { { "fixture_id", Config.FixtureId }, { "reason", reason } });
}

bool BotBenchmark::CleanupFixtureActors()
{
	bool succeeded = true;
	for (UActor* blocker : FixtureBlockers)
		succeeded = (!blocker || blocker->bDeleteMe() || blocker->Destroy()) && succeeded;
	FixtureBlockers.clear();
	if (FixtureTarget)
		succeeded = (FixtureTarget->bDeleteMe() || FixtureTarget->Destroy()) && succeeded;
	FixtureTarget = nullptr;
	return succeeded;
}

void BotBenchmark::SetupHitWallFixture(Engine& engine)
{
	if (!FixtureBot || FixtureBot->bDeleteMe() || FixtureBot->Health() <= 0 || FixtureBot->Physics() != PHYS_Walking)
	{
		FixtureSetupFailure("HitWall fixture bot was not a live walking pawn at setup");
		return;
	}

	UClass* targetClass = engine.packages->FindClass("Engine.AmbientSound");
	UClass* blockerClass = engine.packages->FindClass("Engine.BlockAll");
	if (!targetClass || !blockerClass)
	{
		FixtureSetupFailure("HitWall fixture requires Engine.AmbientSound and Engine.BlockAll");
		return;
	}

	const vec3 source = FixtureBot->Location();
	FixtureBot->Velocity() = vec3(0.0f);
	FixtureBot->Acceleration() = vec3(0.0f);
	const std::array<vec2, 8> directions = {
		vec2(1.0f, 0.0f), vec2(-1.0f, 0.0f), vec2(0.0f, 1.0f), vec2(0.0f, -1.0f),
		normalize(vec2(1.0f, 1.0f)), normalize(vec2(1.0f, -1.0f)),
		normalize(vec2(-1.0f, 1.0f)), normalize(vec2(-1.0f, -1.0f))
	};
	auto prepareFixtureActor = [](UActor* actor)
	{
		if (actor)
		{
			actor->bStatic() = false;
			actor->bNoDelete() = false;
		}
		return actor;
	};

	vec2 direction;
	for (const vec2& candidateDirection : directions)
	{
		const vec3 candidateLocation = source + vec3(candidateDirection * 256.0f, 0.0f);
		UActor* candidate = prepareFixtureActor(FixtureBot->Spawn(targetClass, {}, {}, candidateLocation, {}));
		if (candidate && FixtureBot->ActorReachable(candidate, false))
		{
			FixtureTarget = candidate;
			direction = candidateDirection;
			break;
		}
		if (candidate && !candidate->Destroy())
		{
			FixtureSetupFailure("Could not remove a rejected HitWall fixture target");
			return;
		}
	}
	if (!FixtureTarget)
	{
		FixtureSetupFailure("No clear 256-unit HitWall fixture lane was found");
		return;
	}

	const vec2 perpendicular(-direction.y, direction.x);
	for (int offset = -256; offset <= 256; offset += 64)
	{
		const vec3 blockerLocation = source + vec3(direction * 128.0f + perpendicular * static_cast<float>(offset), 0.0f);
		UActor* blocker = prepareFixtureActor(FixtureBot->Spawn(blockerClass, {}, {}, blockerLocation, {}));
		if (!blocker)
		{
			FixtureSetupFailure("Could not spawn the complete HitWall BlockAll wall");
			return;
		}
		blocker->SetCollisionSize(36.0f, 96.0f);
		blocker->SetCollision(true, true, true);
		FixtureBlockers.push_back(blocker);
	}

	FixtureSourceX = source.x;
	FixtureSourceY = source.y;
	FixtureSourceZ = source.z;
	FixtureTargetX = FixtureTarget->Location().x;
	FixtureTargetY = FixtureTarget->Location().y;
	FixtureTargetZ = FixtureTarget->Location().z;
	FixtureDirectionX = direction.x;
	FixtureDirectionY = direction.y;
	std::string blockerNames;
	for (UActor* blocker : FixtureBlockers)
	{
		if (!blockerNames.empty())
			blockerNames += ">";
		blockerNames += blocker ? blocker->Name.ToString() : "None";
	}
	WriteEvent("fixture_setup", {
		{ "fixture_id", Config.FixtureId },
		{ "bot", ObjectName(FixtureBot) },
		{ "target", ObjectName(FixtureTarget) },
		{ "blocker_count", ToString(static_cast<int>(FixtureBlockers.size())) },
		{ "blockers", blockerNames },
		{ "source_x", ToString(FixtureSourceX) },
		{ "source_y", ToString(FixtureSourceY) },
		{ "source_z", ToString(FixtureSourceZ) },
		{ "target_x", ToString(FixtureTargetX) },
		{ "target_y", ToString(FixtureTargetY) },
		{ "target_z", ToString(FixtureTargetZ) },
		{ "direction_x", ToString(FixtureDirectionX) },
		{ "direction_y", ToString(FixtureDirectionY) },
		{ "blocker_radius", ToString(36.0f) },
		{ "blocker_height", ToString(96.0f) },
		{ "blocker_distance", ToString(128.0f) },
		{ "blocker_spacing", ToString(64.0f) },
		{ "blocker_offset_min", ToString(-256.0f) },
		{ "blocker_offset_max", ToString(256.0f) },
		{ "movement_timeout_ticks", "120" }
	});
	HashCanonical("fixture_setup=" + Config.FixtureId +
		"|source=" + ToString(FixtureSourceX) + "," + ToString(FixtureSourceY) + "," + ToString(FixtureSourceZ) +
		"|target=" + ToString(FixtureTargetX) + "," + ToString(FixtureTargetY) + "," + ToString(FixtureTargetZ) +
		"|direction=" + ToString(FixtureDirectionX) + "," + ToString(FixtureDirectionY) +
		"|blockers=" + blockerNames + "|36.000000|96.000000|128.000000|64.000000|-256.000000|256.000000|timeout=120\n");

	FixtureBot->bAdvancedTactics() = false;
	if (FixtureBot->HasProperty("bWallAdjust"))
		FixtureBot->SetBool("bWallAdjust", false);
	FixtureBot->bFromWall() = false;
	FixtureBot->GotoState(NameString("Roaming"), NameString());
	const vec3 beforeMoveToward = FixtureBot->Location();
	FixtureBot->MoveToward(FixtureTarget, 1.0f);
	FixtureAssertion("movetoward_preserves_location", true, FixtureBot->Location() == beforeMoveToward);
	FixtureAssertion("movetoward_latent_started", true,
		FixtureBot->StateFrame && FixtureBot->StateFrame->LatentState == LatentRunState::MoveToward);
	FixtureAssertion("movetoward_target_assigned", true, FixtureBot->MoveTarget() == FixtureTarget);
	FixtureAssertion("hitwall_setup_is_roaming", true, FixtureBot->GetStateName() == NameString("Roaming"));
	FixtureAssertion("hitwall_setup_is_walking", true, FixtureBot->Physics() == PHYS_Walking);
	FixturePhase = ControlledFixturePhase::Moving;
	FixturePhaseStartTick = Tick;
}

void BotBenchmark::RecordWalkingHitWallResult(uint64_t hitId, UPawn* pawn, UActor* blocker,
	bool eventEnabled, float normalX, float normalY, float normalZ)
{
	if (FixturePhase != ControlledFixturePhase::Moving || pawn != FixtureBot)
		return;

	FixtureHitCount++;
	const bool ownedBlocker = std::find(FixtureBlockers.begin(), FixtureBlockers.end(), blocker) != FixtureBlockers.end();
	FixtureAllBlockersOwned = FixtureAllBlockersOwned && hitId != 0 && ownedBlocker;
	FixtureAllBlockersNonMover = FixtureAllBlockersNonMover && blocker && !blocker->IsA("Mover");
	FixtureAllEventsEnabled = FixtureAllEventsEnabled && eventEnabled;
	const float normalLength = std::sqrt(normalX * normalX + normalY * normalY);
	const float normalDot = normalLength > 0.0f
		? (normalX * FixtureDirectionX + normalY * FixtureDirectionY) / normalLength
		: 1.0f;
	FixtureAllNormalsOpposeLane = FixtureAllNormalsOpposeLane && normalDot < -0.9f;
	if (FixtureHitCount != 1)
		return;

	FixtureFirstBotAlive = !pawn->bDeleteMe() && pawn->Health() > 0;
	const float sourceProjection = (pawn->Location().x - FixtureSourceX) * FixtureDirectionX +
		(pawn->Location().y - FixtureSourceY) * FixtureDirectionY;
	FixtureFirstSourceSide = sourceProjection <= 130.0f;
	FixtureFirstWalking = pawn->Physics() == PHYS_Walking;
	FixtureFirstFromWall = pawn->bFromWall();
	const vec3 sideDelta = pawn->Destination() - pawn->Location();
	const float sideDistance = length(sideDelta);
	FixtureFirstSideDestination = sideDistance >= 0.75f && sideDistance <= 1.25f;
	const vec3 focusDelta = pawn->Focus() - vec3(FixtureTargetX, FixtureTargetY, FixtureTargetZ);
	FixtureFirstFocusEndpoint = length(focusDelta) <= 0.01f;
	FixtureFirstLatentContinue = pawn->StateFrame && pawn->StateFrame->LatentState == LatentRunState::Continue;
	FixtureFirstMoveTimerNonnegative = pawn->MoveTimer() >= 0.0f;
	if (pawn->StateFrame && pawn->StateFrame->Func && pawn->StateFrame->Func->Name == NameString("Roaming"))
	{
		UState* roaming = static_cast<UState*>(pawn->StateFrame->Func);
		const int adjustLabel = roaming->Code->FindLabelIndex(NameString("AdjustFromWall"));
		FixtureFirstAdjustLabel = adjustLabel >= 0 && pawn->StateFrame->StatementIndex == static_cast<size_t>(adjustLabel);
	}
}

void BotBenchmark::CompleteHitWallFixture()
{
	FixtureAssertion("fixture_wall_callback_observed", true, FixtureHitCount > 0);
	FixtureAssertion("fixture_wall_callback_ids_and_blockers_match", true, FixtureAllBlockersOwned);
	FixtureAssertion("fixture_wall_callback_is_non_mover", true, FixtureAllBlockersNonMover);
	FixtureAssertion("fixture_wall_callback_event_enabled", true, FixtureAllEventsEnabled);
	FixtureAssertion("fixture_wall_normal_opposes_lane", true, FixtureAllNormalsOpposeLane);
	FixtureAssertion("fixture_bot_alive_after_callback", true, FixtureFirstBotAlive);
	FixtureAssertion("fixture_bot_remains_source_side", true, FixtureFirstSourceSide);
	FixtureAssertion("fixture_bot_remains_walking", true, FixtureFirstWalking);
	FixtureAssertion("stock_hitwall_sets_from_wall", true, FixtureFirstFromWall);
	FixtureAssertion("stock_hitwall_sets_one_unit_side_destination", true, FixtureFirstSideDestination);
	FixtureAssertion("stock_hitwall_preserves_endpoint_focus", true, FixtureFirstFocusEndpoint);
	FixtureAssertion("stock_hitwall_selects_roaming_adjust_label", true, FixtureFirstAdjustLabel);
	FixtureAssertion("stock_hitwall_clears_movetoward_latent", true, FixtureFirstLatentContinue);
	FixtureAssertion("stock_hitwall_keeps_move_timer_nonnegative", true, FixtureFirstMoveTimerNonnegative);
	FixtureAssertion("fixture_actors_destroyed", true, CleanupFixtureActors());
	HashCanonical("fixture_wall_callback_count=" + std::to_string(FixtureHitCount) + "\n");

	FixtureStatus = FixtureAssertionsFailed == 0 ? "passed" : "failed";
	FixturePhase = ControlledFixturePhase::Complete;
	WriteEvent("fixture_complete", {
		{ "fixture_id", Config.FixtureId },
		{ "status", FixtureStatus },
		{ "assertions_total", ToString(FixtureAssertionsTotal) },
		{ "assertions_passed", ToString(FixtureAssertionsPassed) },
		{ "assertions_failed", ToString(FixtureAssertionsFailed) },
		{ "walking_hit_wall_pairs", ToString(FixtureHitCount) }
	});
	if (FixtureAssertionsFailed != 0)
	{
		FailureReason = "Controlled HitWall fixture assertions failed";
		ExitCode = 5;
	}
	else
	{
		StopRequested = true;
	}
}

void BotBenchmark::AdvanceControlledFixture(Engine& engine)
{
	if (Config.FixtureId != "controlled-hitwall-blockall-v1")
		return;
	if (FixturePhase == ControlledFixturePhase::AwaitWalking)
	{
		if (FixtureBot && !FixtureBot->bDeleteMe() && FixtureBot->Health() > 0 &&
			FixtureBot->StateFrame && FixtureBot->Physics() == PHYS_Walking)
		{
			SetupHitWallFixture(engine);
		}
		else if (Tick - FixturePhaseStartTick >= 60)
		{
			FixtureSetupFailure("HitWall fixture bot did not enter PHYS_Walking within 60 ticks");
		}
	}
	else if (FixturePhase == ControlledFixturePhase::Moving)
	{
		if (FixtureHitCount > 0 || Tick - FixturePhaseStartTick >= 120)
			CompleteHitWallFixture();
	}
}

bool BotBenchmark::ValidateViewportSpectator(Engine& engine)
{
	UPlayerPawn* viewportPawn = engine.viewport ? engine.viewport->Actor() : nullptr;
	ViewportActorClass = ClassName(viewportPawn);
	ViewportActorIsSpectator = viewportPawn && viewportPawn->IsA("Spectator");
	UPlayerReplicationInfo* pri = viewportPawn ? viewportPawn->PlayerReplicationInfo() : nullptr;
	ViewportPRIIsSpectator = pri && pri->bIsSpectator();

	WriteEvent("benchmark_spectator", {
		{ "actor", ObjectName(viewportPawn) },
		{ "class", ViewportActorClass },
		{ "class_is_spectator", ViewportActorIsSpectator ? "true" : "false" },
		{ "pri_is_spectator", ViewportPRIIsSpectator ? "true" : "false" }
	});

	if (!viewportPawn || !ViewportActorIsSpectator || !ViewportPRIIsSpectator)
	{
		FailureReason = "Benchmark viewport login did not produce a spectator";
		ExitCode = 2;
		WriteEvent("setup_error", { { "reason", FailureReason } });
		return false;
	}
	return true;
}

void BotBenchmark::ConfigureBots(Engine& engine)
{
	// DeathMatchPlus may maintain MinPlayers on its first ticks. Disable that
	// auto-fill before spawning the exact requested benchmark roster.
	if (engine.GameInfo)
	{
		if (engine.GameInfo->HasProperty("MinPlayers"))
			engine.GameInfo->SetInt("MinPlayers", 0);
		if (engine.GameInfo->HasProperty("InitialBots"))
			engine.GameInfo->SetInt("InitialBots", 0);
		// The stock DeathMatchPlus AddBot path sends bots to Dying while the
		// ready/countdown gate is active. Benchmarks start immediately, so open
		// that gate before creating the controlled roster.
		if (engine.GameInfo->HasProperty("bRequireReady"))
			engine.GameInfo->SetBool("bRequireReady", false);
	}

	UObject* botConfig = nullptr;
	if (engine.GameInfo && engine.GameInfo->HasProperty("BotConfig"))
		botConfig = engine.GameInfo->GetUObject("BotConfig");

	if (!botConfig)
	{
		FailureReason = "GameInfo has no BotConfig; Botpack.DeathMatchPlus is required";
		ExitCode = 2;
		WriteEvent("setup_error", { { "reason", FailureReason } });
		return;
	}

	if (!botConfig->HasProperty("Difficulty"))
	{
		FailureReason = "BotConfig has no Difficulty property";
		ExitCode = 2;
		WriteEvent("setup_error", { { "reason", FailureReason } });
		return;
	}

	if (botConfig->HasProperty("bAdjustSkill"))
		botConfig->SetBool("bAdjustSkill", false);
	if (botConfig->HasProperty("MinPlayers"))
		botConfig->SetInt("MinPlayers", 0);
	if (botConfig->HasProperty("InitialBots"))
		botConfig->SetInt("InitialBots", 0);

	const std::vector<int> requestedSkills = Config.BotSkills.empty()
		? std::vector<int>(static_cast<size_t>(Config.BotCount), Config.Difficulty)
		: Config.BotSkills;
	WriteEvent("bot_configured", {
		{ "class", UObject::GetUClassFullName(botConfig).ToString() },
		{ "difficulty", ToString(Config.Difficulty) },
		{ "requested_skills", JoinSkills(requestedSkills) },
		{ "auto_adjust", "false" },
		{ "require_ready", "false" }
	});

	std::set<UPawn*> existingBots;
	for (UActor* actor : engine.Level->Actors)
	{
		UPawn* pawn = UObject::TryCast<UPawn>(actor);
		if (pawn && pawn->IsA("Bot"))
			existingBots.insert(pawn);
	}

	for (size_t index = 0; index < requestedSkills.size(); index++)
	{
		const int requestedSkill = requestedSkills[index];
		botConfig->SetInt("Difficulty", static_cast<uint32_t>(requestedSkill));

		bool namedCommandAvailable = false;
		if (index == 0 && !Config.BotName.empty())
			namedCommandAvailable = engine.ExecCommand({ "AddBotNamed", Config.BotName });

		auto findNewBots = [&]()
		{
			std::vector<UPawn*> result;
			for (UActor* actor : engine.Level->Actors)
			{
				UPawn* pawn = UObject::TryCast<UPawn>(actor);
				if (pawn && pawn->IsA("Bot") && existingBots.find(pawn) == existingBots.end())
					result.push_back(pawn);
			}
			return result;
		};

		std::vector<UPawn*> newBots = findNewBots();
		if (newBots.empty())
		{
			if (index == 0 && !Config.BotName.empty() && !namedCommandAvailable)
				WriteEvent("setup_warning", { { "reason", "AddBotNamed exec function was not found" } });
			if (!engine.ExecCommand({ "AddBots", "1" }))
			{
				FailureReason = "AddBots exec function was not found";
				ExitCode = 2;
				WriteEvent("setup_error", { { "reason", FailureReason } });
				return;
			}
			newBots = findNewBots();
		}

		if (newBots.size() != 1)
		{
			FailureReason = "An explicit AddBot command did not create exactly one bot";
			ExitCode = 2;
			WriteEvent("setup_error", {
				{ "reason", FailureReason },
				{ "requested_index", ToString(static_cast<int>(index)) },
				{ "observed_new_bots", ToString(static_cast<int>(newBots.size())) }
			});
			return;
		}

		UPawn* spawned = newBots.front();
		// ChallengeBotInfo adds per-profile BotSkills while individualizing. Call
		// the stock Bot.InitializeSkill function after spawn so a benchmark's
		// requested external 0..7 tier is exact rather than profile-dependent.
		CallEvent(spawned, "InitializeSkill", { ExpressionValue::FloatValue(static_cast<float>(requestedSkill)) });
		const std::string identity = PawnIdentity(spawned);
		RequestedSkillByIdentity[identity] = requestedSkill;
		existingBots.insert(spawned);

		const bool expectedNovice = requestedSkill < 4;
		const float expectedInternalSkill = static_cast<float>(expectedNovice ? requestedSkill : requestedSkill - 4);
		const bool actualNovice = spawned->HasProperty("bNovice") && spawned->GetBool("bNovice");
		const float actualInternalSkill = spawned->Skill();
		const bool mappingValid = actualNovice == expectedNovice && std::abs(actualInternalSkill - expectedInternalSkill) < 0.001f;
		WriteEvent("bot_skill_configured", {
			{ "index", ToString(static_cast<int>(index)) },
			{ "identity", identity },
			{ "actor", ObjectName(spawned) },
			{ "requested_external_skill", ToString(requestedSkill) },
			{ "expected_novice", expectedNovice ? "true" : "false" },
			{ "expected_internal_skill", ToString(expectedInternalSkill) },
			{ "actual_novice", actualNovice ? "true" : "false" },
			{ "actual_internal_skill", ToString(actualInternalSkill) },
			{ "mapping_valid", mappingValid ? "true" : "false" }
		});
		if (!mappingValid)
		{
			FailureReason = "Spawned bot skill did not match the stock external-to-internal mapping";
			ExitCode = 2;
			WriteEvent("setup_error", { { "reason", FailureReason }, { "identity", identity } });
			return;
		}
	}
}

void BotBenchmark::AfterTick(Engine& engine)
{
	Tick++;
	HashCanonical("tick=" + std::to_string(Tick) + "\n");
	ObserveBots(engine);
	AdvanceControlledFixture(engine);
}

BotBenchmark::PawnSnapshot BotBenchmark::Capture(UPawn* pawn) const
{
	PawnSnapshot snapshot;
	snapshot.Name = pawn->Name.ToString();
	if (UPlayerReplicationInfo* pri = pawn->PlayerReplicationInfo())
	{
		snapshot.PlayerID = pri->PlayerID();
		snapshot.PlayerName = pri->PlayerName();
		snapshot.Score = pri->Score();
		snapshot.PRIDeaths = pri->Deaths();
		snapshot.Identity = "pri:" + std::to_string(snapshot.PlayerID);
	}
	else
	{
		snapshot.Identity = "actor:" + snapshot.Name;
	}
	auto requestedSkill = RequestedSkillByIdentity.find(snapshot.Identity);
	if (requestedSkill != RequestedSkillByIdentity.end())
		snapshot.RequestedSkill = requestedSkill->second;
	snapshot.ClassName = UObject::GetUClassFullName(pawn).ToString();
	snapshot.State = pawn->GetStateName().ToString();
	snapshot.LatentAction = pawn->StateFrame ? LatentActionName(pawn->StateFrame->LatentState) : "None";
	snapshot.Enemy = ObjectName(pawn->Enemy());
	snapshot.MoveTarget = ObjectName(pawn->MoveTarget());
	if (UActor* moveTarget = pawn->MoveTarget())
	{
		const vec3 moveDelta = moveTarget->Location() - pawn->Location();
		snapshot.MoveTargetHorizontalDistance = length(moveDelta.xy());
		snapshot.MoveTargetVerticalDistance = std::abs(moveDelta.z);
		snapshot.MoveTargetCombinedRadius = pawn->CollisionRadius() + moveTarget->CollisionRadius();
		snapshot.MoveTargetState = moveTarget->GetStateName().ToString();
		snapshot.MoveTargetCollidesActors = moveTarget->bCollideActors();
		snapshot.MoveTargetHidden = moveTarget->bHidden();
	}
	{
		std::ostringstream touching;
		for (UActor* actor : pawn->Touching())
		{
			if (!actor)
				continue;
			if (snapshot.TouchingActorCount++ != 0)
				touching << '>';
			touching << ObjectName(actor)
				<< "[state=" << actor->GetStateName().ToString()
				<< ",hidden=" << (actor->bHidden() ? "true" : "false")
				<< ",collides=" << (actor->bCollideActors() ? "true" : "false")
				<< ']';
		}
		snapshot.TouchingActors = touching.str();
	}
	snapshot.Health = pawn->Health();
	snapshot.PawnKillCount = pawn->KillCount();
	snapshot.Skill = pawn->Skill();
	snapshot.Novice = pawn->HasProperty("bNovice") ? pawn->GetBool("bNovice") : false;
	snapshot.FireIntent = pawn->bFire() != 0;
	snapshot.AltFireIntent = pawn->bAltFire() != 0;
	snapshot.VelocityX = pawn->Velocity().x;
	snapshot.VelocityY = pawn->Velocity().y;
	snapshot.VelocityZ = pawn->Velocity().z;
	snapshot.AccelerationX = pawn->Acceleration().x;
	snapshot.AccelerationY = pawn->Acceleration().y;
	snapshot.AccelerationZ = pawn->Acceleration().z;
	snapshot.DestinationX = pawn->Destination().x;
	snapshot.DestinationY = pawn->Destination().y;
	snapshot.DestinationZ = pawn->Destination().z;
	snapshot.MoveTimer = pawn->MoveTimer();
	snapshot.DesiredSpeed = pawn->DesiredSpeed();
	snapshot.Physics = pawn->Physics();
	snapshot.X = static_cast<int64_t>(std::llround(pawn->Location().x * 256.0));
	snapshot.Y = static_cast<int64_t>(std::llround(pawn->Location().y * 256.0));
	snapshot.Z = static_cast<int64_t>(std::llround(pawn->Location().z * 256.0));

	snapshot.SelectedWeaponClass = ClassName(pawn->Weapon());
	std::set<UObject*> usefulAmmo;
	UInventory* inventory = pawn->Inventory();
	for (int guard = 0; inventory && guard < 256; guard++)
	{
		snapshot.InventoryCount++;
		if (inventory->bIsAnArmor())
			snapshot.ArmorTotal += std::max(inventory->Charge(), 0);

		if (UWeapon* weapon = UObject::TryCast<UWeapon>(inventory))
		{
			snapshot.WeaponCount++;
			snapshot.WeaponClasses.insert(ClassName(weapon));
			UObject* ammo = weapon->HasProperty("AmmoType") ? weapon->GetUObject("AmmoType") : nullptr;
			if (ammo && ammo->HasProperty("AmmoAmount"))
				usefulAmmo.insert(ammo);
			const int ammoAmount = ammo && ammo->HasProperty("AmmoAmount") ? static_cast<int>(ammo->GetInt("AmmoAmount")) : 0;
			if (weapon->bMeleeWeapon() || ammoAmount > 0)
				snapshot.ViableWeaponCount++;
		}
		inventory = UObject::TryCast<UInventory>(inventory->Inventory());
	}
	for (UObject* ammo : usefulAmmo)
		snapshot.UsefulAmmoTotal += static_cast<int>(ammo->GetInt("AmmoAmount"));

	std::ostringstream canonical;
	canonical << snapshot.Identity << '|' << snapshot.Name << '|' << snapshot.PlayerName
		<< '|' << snapshot.ClassName << '|' << snapshot.State
		<< '|' << snapshot.Health << '|' << snapshot.InventoryCount
		<< '|' << snapshot.SelectedWeaponClass << '|' << snapshot.WeaponCount
		<< '|' << snapshot.ViableWeaponCount << '|' << snapshot.UsefulAmmoTotal
		<< '|' << snapshot.ArmorTotal << '|' << snapshot.PlayerID
		<< '|' << std::fixed << std::setprecision(6) << snapshot.Score
		<< '|' << snapshot.PRIDeaths << '|' << snapshot.PawnKillCount
		<< '|' << snapshot.RequestedSkill
		<< '|' << std::fixed << std::setprecision(6) << snapshot.Skill
		<< '|' << (snapshot.Novice ? 1 : 0)
		<< '|' << snapshot.X << '|' << snapshot.Y << '|' << snapshot.Z
		<< '|' << snapshot.LatentAction
		<< '|' << snapshot.Enemy << '|' << snapshot.MoveTarget
		<< '|' << (snapshot.FireIntent ? 1 : 0)
		<< '|' << (snapshot.AltFireIntent ? 1 : 0);
	for (const std::string& weaponClass : snapshot.WeaponClasses)
		canonical << "|weapon:" << weaponClass;
	snapshot.Canonical = canonical.str();
	return snapshot;
}

void BotBenchmark::ObserveBots(Engine& engine)
{
	std::map<std::string, PawnSnapshot> current;
	int nonBotParticipants = 0;
	if (engine.Level)
	{
		for (UActor* actor : engine.Level->Actors)
		{
			UPawn* pawn = UObject::TryCast<UPawn>(actor);
			if (!pawn)
				continue;

			const bool isBot = pawn->IsA("Bot");
			UPlayerReplicationInfo* pri = pawn->PlayerReplicationInfo();
			const bool isSpectator = pawn->IsA("Spectator") || (pri && pri->bIsSpectator());
			if (!isBot && pawn->bIsPlayer() && !isSpectator && !pawn->bDeleteMe())
				nonBotParticipants++;
			if (!isBot)
				continue;

			PawnSnapshot snapshot = Capture(pawn);
			current[snapshot.Identity] = snapshot;
			HashCanonical(snapshot.Canonical + "\n");

			auto previous = PreviousPawns.find(snapshot.Identity);
			UpdateTelemetry(snapshot, previous != PreviousPawns.end() ? &previous->second : nullptr);
			const bool live = snapshot.Health > 0 && snapshot.State != "Dying";
			ObservedLiveBot = ObservedLiveBot || live;
			MaximumObservedInventory = std::max(MaximumObservedInventory, snapshot.InventoryCount);
			if (live && previous != PreviousPawns.end() && previous->second.Health > 0 && previous->second.State != "Dying")
			{
				const int64_t dx = snapshot.X - previous->second.X;
				const int64_t dy = snapshot.Y - previous->second.Y;
				// Positions are quantized to 1/256 UU. Require more than one
				// horizontal Unreal unit so spawn settling alone does not pass.
				ObservedBotMovement = ObservedBotMovement || dx * dx + dy * dy > 256LL * 256LL;
			}
			if (previous == PreviousPawns.end() || previous->second.Canonical != snapshot.Canonical)
			{
				WriteEvent(previous == PreviousPawns.end() ? "bot_observed" : "bot_state", {
					{ "identity", snapshot.Identity },
					{ "actor", snapshot.Name },
					{ "player_name", snapshot.PlayerName },
					{ "class", snapshot.ClassName },
					{ "state", snapshot.State },
					{ "latent_action", snapshot.LatentAction },
					{ "health", ToString(snapshot.Health) },
					{ "inventory_count", ToString(snapshot.InventoryCount) },
					{ "selected_weapon_class", snapshot.SelectedWeaponClass },
					{ "weapon_count", ToString(snapshot.WeaponCount) },
					{ "viable_weapon_count", ToString(snapshot.ViableWeaponCount) },
					{ "useful_ammo_total", ToString(snapshot.UsefulAmmoTotal) },
					{ "armor_total", ToString(snapshot.ArmorTotal) },
					{ "pri_score", ToString(snapshot.Score) },
					{ "pri_deaths", ToString(snapshot.PRIDeaths) },
					{ "pawn_kill_count", ToString(snapshot.PawnKillCount) },
					{ "requested_external_skill", ToString(snapshot.RequestedSkill) },
					{ "skill", ToString(snapshot.Skill) },
					{ "novice", snapshot.Novice ? "true" : "false" },
					{ "x_q256", std::to_string(snapshot.X) },
					{ "y_q256", std::to_string(snapshot.Y) },
					{ "z_q256", std::to_string(snapshot.Z) },
					{ "enemy", snapshot.Enemy },
					{ "move_target", snapshot.MoveTarget },
					{ "fire_intent", snapshot.FireIntent ? "true" : "false" },
					{ "alt_fire_intent", snapshot.AltFireIntent ? "true" : "false" }
				});
			}

			if (previous != PreviousPawns.end() && previous->second.Health > 0 && snapshot.Health <= 0)
				Deaths++;

			UPawn* enemy = UObject::TryCast<UPawn>(pawn->Enemy());
			if (enemy && !enemy->IsA("Bot"))
			{
				NonBotEnemyTargetObservations++;
				const std::string target = ObjectName(enemy);
				if (NonBotEnemyTargets.insert(target).second)
				{
					WriteEvent("non_bot_enemy_target", {
						{ "bot", ObjectName(pawn) },
						{ "target", target },
						{ "target_is_spectator", enemy->IsA("Spectator") ? "true" : "false" }
					});
				}
			}
		}
	}

	MaximumObservedBots = std::max(MaximumObservedBots, static_cast<int>(current.size()));
	MaximumObservedNonBotParticipants = std::max(MaximumObservedNonBotParticipants, nonBotParticipants);
	PreviousPawns = std::move(current);
}

BotBenchmark::BotTelemetry& BotBenchmark::TelemetryForPawn(UPawn* pawn)
{
	const std::string identity = PawnIdentity(pawn);
	auto& telemetry = Telemetry[identity];
	telemetry.Identity = identity;
	if (pawn)
	{
		telemetry.LastActor = pawn->Name.ToString();
		if (UPlayerReplicationInfo* pri = pawn->PlayerReplicationInfo())
			telemetry.PlayerName = pri->PlayerName();
		auto requested = RequestedSkillByIdentity.find(identity);
		if (requested != RequestedSkillByIdentity.end())
			telemetry.RequestedSkill = requested->second;
	}
	return telemetry;
}

void BotBenchmark::RecordDamage(UPawn* victim, const DamageObservation& observation)
{
	const int healthAfter = victim->Health();
	const int rawHealthDelta = std::max(0, observation.HealthBefore - healthAfter);
	const int effectiveHealthDamage = observation.HealthBefore > 0
		? std::max(0, observation.HealthBefore - std::max(healthAfter, 0))
		: 0;
	const bool fatal = observation.HealthBefore > 0 && healthAfter <= 0;
	const bool selfDamage = observation.Instigator == victim;
	const bool opponentDamage = observation.Instigator && observation.Instigator != victim;
	const std::string victimIdentity = PawnIdentity(victim);
	const std::string instigatorIdentity = PawnIdentity(observation.Instigator);
	const std::string sourceClass = ClassName(observation.Source);

	if (victim->IsA("Bot") && observation.HealthBefore > 0)
	{
		auto& victimTelemetry = TelemetryForPawn(victim);
		victimTelemetry.DamageEventsTaken++;
		victimTelemetry.ExactDamageTaken += effectiveHealthDamage;
		if (fatal)
			victimTelemetry.FatalDamageDeaths++;
	}
	if (observation.Instigator && observation.Instigator->IsA("Bot") && observation.HealthBefore > 0)
	{
		auto& instigatorTelemetry = TelemetryForPawn(observation.Instigator);
		if (selfDamage)
			instigatorTelemetry.ExactSelfDamage += effectiveHealthDamage;
		else
		{
			instigatorTelemetry.DamageEventsDealt++;
			instigatorTelemetry.ExactDamageDealt += effectiveHealthDamage;
			if (fatal)
				instigatorTelemetry.FatalDamageKills++;
		}
	}

	if (opponentDamage && observation.HealthBefore > 0)
	{
		for (auto& [weapon, hitscan] : ActiveHitscan)
		{
			if (hitscan.Depth > 0 && hitscan.Shooter == observation.Instigator)
			{
				hitscan.OpponentDamageEvents++;
				hitscan.OpponentHealthDamage += effectiveHealthDamage;
			}
		}
		if (UProjectile* projectile = UObject::TryCast<UProjectile>(observation.Source))
		{
			auto projectileIt = Projectiles.find(projectile);
			if (projectileIt != Projectiles.end())
			{
				projectileIt->second.HitOpponent = true;
				projectileIt->second.OpponentHealthDamage += effectiveHealthDamage;
			}
		}
	}
	else if (selfDamage && observation.HealthBefore > 0)
	{
		for (auto& [weapon, hitscan] : ActiveHitscan)
			if (hitscan.Depth > 0 && hitscan.Shooter == observation.Instigator)
				hitscan.SelfHealthDamage += effectiveHealthDamage;
	}

	WriteEvent("damage", {
		{ "victim", victimIdentity },
		{ "victim_actor", ObjectName(victim) },
		{ "instigator", instigatorIdentity.empty() ? "None" : instigatorIdentity },
		{ "source", ObjectName(observation.Source) },
		{ "source_class", sourceClass },
		{ "damage_type", observation.DamageType },
		{ "requested_damage", ToString(observation.RequestedDamage) },
		{ "health_before", ToString(observation.HealthBefore) },
		{ "health_after", ToString(healthAfter) },
		{ "raw_health_delta", ToString(rawHealthDelta) },
		{ "effective_health_damage", ToString(effectiveHealthDamage) },
		{ "self_damage", selfDamage ? "true" : "false" },
		{ "fatal", fatal ? "true" : "false" }
	});
}

void BotBenchmark::UpdateTelemetry(const PawnSnapshot& snapshot, const PawnSnapshot* previous)
{
	BotTelemetry& telemetry = Telemetry[snapshot.Identity];
	if (snapshot.Health > 0 && (snapshot.FireIntent || snapshot.AltFireIntent))
		telemetry.FiringIntentTicks++;
	if (!telemetry.Initialized)
	{
		telemetry.Initialized = true;
		telemetry.Identity = snapshot.Identity;
		telemetry.PlayerName = snapshot.PlayerName;
		telemetry.LastActor = snapshot.Name;
		telemetry.RequestedSkill = snapshot.RequestedSkill;
		telemetry.FirstObservedTick = Tick;
		telemetry.InitialWeaponCount = snapshot.WeaponCount;
		telemetry.InitialWeaponClasses = snapshot.WeaponClasses;
		telemetry.MaximumWeaponCount = snapshot.WeaponCount;
		telemetry.InitialUsefulAmmo = snapshot.UsefulAmmoTotal;
		telemetry.MaximumUsefulAmmo = snapshot.UsefulAmmoTotal;
		telemetry.MaximumArmor = snapshot.ArmorTotal;
		telemetry.InitialScore = snapshot.Score;
		telemetry.LastScore = snapshot.Score;
		telemetry.MaximumPRIDeaths = snapshot.PRIDeaths;
		telemetry.MaximumPawnKillCount = snapshot.PawnKillCount;
		if (snapshot.ViableWeaponCount > 0)
			telemetry.FirstViableWeaponTick = Tick;
		if (snapshot.Health > 0 && snapshot.Health < 100)
			telemetry.HealthDeficitStartTick = Tick;
		telemetry.AwaitingRespawn = snapshot.Health <= 0;
		WriteEvent("bot_resources", {
			{ "identity", snapshot.Identity },
			{ "actor", snapshot.Name },
			{ "selected_weapon_class", snapshot.SelectedWeaponClass },
			{ "weapon_count", ToString(snapshot.WeaponCount) },
			{ "viable_weapon_count", ToString(snapshot.ViableWeaponCount) },
			{ "useful_ammo_total", ToString(snapshot.UsefulAmmoTotal) },
			{ "health", ToString(snapshot.Health) },
			{ "armor_total", ToString(snapshot.ArmorTotal) },
			{ "pri_score", ToString(snapshot.Score) },
			{ "pri_deaths", ToString(snapshot.PRIDeaths) }
		});
	}
	else if (previous)
	{
		if (snapshot.Health <= 0)
			telemetry.AwaitingRespawn = true;
		const bool lifeReset = snapshot.Health > 0 && telemetry.AwaitingRespawn;
		if (!lifeReset && snapshot.Health > 0 && previous->Health > 0 && snapshot.Name == previous->Name)
		{
			const double dx = static_cast<double>(snapshot.X - previous->X) / 256.0;
			const double dy = static_cast<double>(snapshot.Y - previous->Y) / 256.0;
			const double dz = static_cast<double>(snapshot.Z - previous->Z) / 256.0;
			const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
			telemetry.TravelDistanceUnits += distance;

			if (IsMovementLatent(snapshot.LatentAction))
			{
				telemetry.MovementGoalSeconds += Config.FixedDelta;
				if (distance < 0.25)
				{
					telemetry.CurrentNoProgressSeconds += Config.FixedDelta;
					telemetry.NoProgressSecondsTotal += Config.FixedDelta;
					telemetry.LongestNoProgressSeconds = std::max(telemetry.LongestNoProgressSeconds, telemetry.CurrentNoProgressSeconds);
					if (!telemetry.InStuckInterval && telemetry.CurrentNoProgressSeconds >= 2.0)
					{
						telemetry.InStuckInterval = true;
						telemetry.StuckEvents++;
						WriteEvent("bot_stuck", {
							{ "identity", snapshot.Identity },
							{ "actor", snapshot.Name },
							{ "state", snapshot.State },
							{ "latent_action", snapshot.LatentAction },
							{ "move_target", snapshot.MoveTarget },
							{ "move_target_horizontal_distance", ToString(snapshot.MoveTargetHorizontalDistance) },
							{ "move_target_vertical_distance", ToString(snapshot.MoveTargetVerticalDistance) },
							{ "move_target_combined_radius", ToString(snapshot.MoveTargetCombinedRadius) },
							{ "move_target_state", snapshot.MoveTargetState },
							{ "move_target_collides_actors", snapshot.MoveTargetCollidesActors ? "true" : "false" },
							{ "move_target_hidden", snapshot.MoveTargetHidden ? "true" : "false" },
							{ "physics", ToString(snapshot.Physics) },
							{ "location_x", ToString(static_cast<float>(snapshot.X) / 256.0f) },
							{ "location_y", ToString(static_cast<float>(snapshot.Y) / 256.0f) },
							{ "location_z", ToString(static_cast<float>(snapshot.Z) / 256.0f) },
							{ "destination_x", ToString(snapshot.DestinationX) },
							{ "destination_y", ToString(snapshot.DestinationY) },
							{ "destination_z", ToString(snapshot.DestinationZ) },
							{ "velocity_x", ToString(snapshot.VelocityX) },
							{ "velocity_y", ToString(snapshot.VelocityY) },
							{ "velocity_z", ToString(snapshot.VelocityZ) },
							{ "acceleration_x", ToString(snapshot.AccelerationX) },
							{ "acceleration_y", ToString(snapshot.AccelerationY) },
							{ "acceleration_z", ToString(snapshot.AccelerationZ) },
							{ "desired_speed", ToString(snapshot.DesiredSpeed) },
							{ "move_timer", ToString(snapshot.MoveTimer) },
							{ "touching_actor_count", ToString(snapshot.TouchingActorCount) },
							{ "touching_actors", snapshot.TouchingActors },
							{ "no_progress_seconds", ToString(static_cast<float>(telemetry.CurrentNoProgressSeconds)) }
						});
					}
				}
				else
				{
					telemetry.CurrentNoProgressSeconds = 0.0;
					telemetry.InStuckInterval = false;
				}
			}
			else
			{
				telemetry.CurrentNoProgressSeconds = 0.0;
				telemetry.InStuckInterval = false;
			}
		}
		if (lifeReset)
		{
			telemetry.RespawnsObserved++;
			telemetry.AwaitingRespawn = false;
			telemetry.HealthDeficitStartTick = UINT64_MAX;
		}
		else
		{
			const int weaponDelta = snapshot.WeaponCount - previous->WeaponCount;
			if (weaponDelta > 0)
			{
				telemetry.WeaponGainProxy += weaponDelta;
				if (telemetry.FirstWeaponGainTick == UINT64_MAX)
					telemetry.FirstWeaponGainTick = Tick;
			}

			const int ammoDelta = snapshot.UsefulAmmoTotal - previous->UsefulAmmoTotal;
			if (ammoDelta > 0)
			{
				telemetry.UsefulAmmoGainProxy += ammoDelta;
				if (telemetry.FirstUsefulAmmoGainTick == UINT64_MAX)
					telemetry.FirstUsefulAmmoGainTick = Tick;
			}
			else if (ammoDelta < 0)
			{
				telemetry.UsefulAmmoSpentProxy -= ammoDelta;
			}

			if (previous->Health > 0 && snapshot.Health < previous->Health)
			{
				telemetry.DamageTakenSnapshot += previous->Health - std::max(snapshot.Health, 0);
				if (snapshot.Health > 0 && telemetry.HealthDeficitStartTick == UINT64_MAX)
					telemetry.HealthDeficitStartTick = Tick;
			}
			else if (previous->Health > 0 && snapshot.Health > previous->Health)
			{
				telemetry.HealthGained += snapshot.Health - previous->Health;
				telemetry.HealthRecoveryEvents++;
				if (telemetry.HealthDeficitStartTick != UINT64_MAX)
				{
					const uint64_t latency = Tick - telemetry.HealthDeficitStartTick;
					telemetry.HealthRecoveryLatencyTicksTotal += latency;
					telemetry.HealthRecoveryLatencyTicksMax = std::max(telemetry.HealthRecoveryLatencyTicksMax, latency);
				}
				telemetry.HealthDeficitStartTick = snapshot.Health < 100 ? Tick : UINT64_MAX;
			}
		}

		const float scoreDelta = snapshot.Score - previous->Score;
		if (scoreDelta > 0.0f)
			telemetry.PositiveScoreDeltaKillProxy += scoreDelta;

		if (snapshot.SelectedWeaponClass != previous->SelectedWeaponClass ||
			snapshot.WeaponCount != previous->WeaponCount ||
			snapshot.UsefulAmmoTotal != previous->UsefulAmmoTotal ||
			snapshot.Health != previous->Health || snapshot.ArmorTotal != previous->ArmorTotal ||
			snapshot.Score != previous->Score || snapshot.PRIDeaths != previous->PRIDeaths)
		{
			WriteEvent("bot_resource_delta", {
				{ "identity", snapshot.Identity },
				{ "actor", snapshot.Name },
				{ "selected_weapon_class", snapshot.SelectedWeaponClass },
				{ "weapon_delta", ToString(snapshot.WeaponCount - previous->WeaponCount) },
				{ "useful_ammo_delta", ToString(snapshot.UsefulAmmoTotal - previous->UsefulAmmoTotal) },
				{ "health_delta", ToString(snapshot.Health - previous->Health) },
				{ "armor_delta", ToString(snapshot.ArmorTotal - previous->ArmorTotal) },
				{ "score_delta", ToString(snapshot.Score - previous->Score) },
				{ "pri_deaths_delta", ToString(snapshot.PRIDeaths - previous->PRIDeaths) }
			});
		}
	}

	telemetry.PlayerName = snapshot.PlayerName;
	telemetry.LastActor = snapshot.Name;
	telemetry.LastScore = snapshot.Score;
	telemetry.MaximumWeaponCount = std::max(telemetry.MaximumWeaponCount, snapshot.WeaponCount);
	telemetry.MaximumUsefulAmmo = std::max(telemetry.MaximumUsefulAmmo, snapshot.UsefulAmmoTotal);
	telemetry.MaximumArmor = std::max(telemetry.MaximumArmor, snapshot.ArmorTotal);
	telemetry.MaximumPRIDeaths = std::max(telemetry.MaximumPRIDeaths, snapshot.PRIDeaths);
	telemetry.MaximumPawnKillCount = std::max(telemetry.MaximumPawnKillCount, snapshot.PawnKillCount);
	if (telemetry.FirstNovelWeaponTick == UINT64_MAX)
	{
		for (const std::string& weaponClass : snapshot.WeaponClasses)
		{
			if (telemetry.InitialWeaponClasses.find(weaponClass) == telemetry.InitialWeaponClasses.end())
			{
				telemetry.FirstNovelWeaponTick = Tick;
				break;
			}
		}
	}
	if (snapshot.ViableWeaponCount > 0 && telemetry.FirstViableWeaponTick == UINT64_MAX)
		telemetry.FirstViableWeaponTick = Tick;
	if (!snapshot.SelectedWeaponClass.empty())
		telemetry.SelectedWeaponClasses.insert(snapshot.SelectedWeaponClass);
}

void BotBenchmark::Finalize(Engine& engine)
{
	if (Finalized)
		return;
	if (FailureReason.empty() && Config.FixtureId == "controlled-hitwall-blockall-v1" &&
		FixturePhase != ControlledFixturePhase::Complete)
	{
		CleanupFixtureActors();
		FailureReason = "Controlled HitWall fixture ended before completion";
		ExitCode = 5;
		FixtureStatus = "failed";
	}

	if (FailureReason.empty() && MaximumObservedBots != Config.BotCount)
	{
		FailureReason = "Observed bot count did not match requested roster";
		ExitCode = 2;
	}
	if (FailureReason.empty() && MaximumObservedNonBotParticipants != 0)
	{
		FailureReason = "A non-bot player pawn participated in the benchmark";
		ExitCode = 2;
	}
	if (FailureReason.empty() && NonBotEnemyTargetObservations != 0)
	{
		FailureReason = "A controlled bot targeted a non-bot pawn";
		ExitCode = 2;
	}
	if (FailureReason.empty() && Config.FixtureId.empty() && !ObservedLiveBot)
	{
		FailureReason = "Controlled bot never entered a live state";
		ExitCode = 2;
	}
	if (FailureReason.empty() && Config.FixtureId.empty() && MaximumObservedInventory == 0)
	{
		FailureReason = "Controlled bot never acquired inventory";
		ExitCode = 2;
	}
	if (FailureReason.empty() && Config.FixtureId.empty() && !ObservedBotMovement)
	{
		FailureReason = "Controlled bot never moved horizontally";
		ExitCode = 2;
	}

	const std::string status = FailureReason.empty() ? "passed" : "failed";
	WriteEvent("run_end", {
		{ "status", status },
		{ "reason", FailureReason },
		{ "ticks", ToString(Tick) },
		{ "digest", DigestHex() }
	});
	WriteSummary(&engine, status, FailureReason);
	EventsFile.reset();
	Finalized = true;
}

void BotBenchmark::Fail(const std::string& message, int exitCode)
{
	if (Finalized)
		return;
	FailureReason = message;
	ExitCode = exitCode;
	if (EventsFile)
	{
		WriteEvent("run_end", {
			{ "status", "error" },
			{ "reason", FailureReason },
			{ "ticks", ToString(Tick) },
			{ "digest", DigestHex() }
		});
	}
	WriteSummary(nullptr, "error", FailureReason);
	EventsFile.reset();
	Finalized = true;
}

void BotBenchmark::WriteEvent(const std::string& type, const std::map<std::string, std::string>& fields)
{
	if (!EventsFile)
		return;

	JsonValue event = JsonValue::object();
	event["schema"].set_number(1);
	event["seq"].set_number(static_cast<double>(EventSequence++));
	event["tick"].set_number(static_cast<double>(Tick));
	event["type"].set_string(type);

	JsonValue payload = JsonValue::object();
	for (const auto& field : fields)
		payload[field.first].set_string(field.second);
	event["fields"] = payload;

	const std::string line = event.to_json(false) + "\n";
	EventsFile->write(line.data(), line.size());
}

void BotBenchmark::WriteSummary(Engine* engine, const std::string& status, const std::string& reason)
{
	JsonValue summary = JsonValue::object();
	summary["schema"].set_number(1);
	summary["status"].set_string(status);
	summary["reason"].set_string(reason);
	summary["scenario"].set_string(Config.Scenario);
	summary["fixture_id"].set_string(Config.FixtureId);
	summary["seed"].set_string(std::to_string(Config.Seed));
	summary["fixed_delta"].set_number(Config.FixedDelta);
	summary["ticks"].set_number(static_cast<double>(Tick));
	summary["simulated_seconds"].set_number(static_cast<double>(Tick) * Config.FixedDelta);
	summary["requested_difficulty"].set_number(Config.Difficulty);
	summary["requested_bots"].set_number(Config.BotCount);
	const std::vector<int> requestedSkills = Config.BotSkills.empty()
		? std::vector<int>(static_cast<size_t>(Config.BotCount), Config.Difficulty)
		: Config.BotSkills;
	JsonValue requestedSkillValues = JsonValue::array();
	for (int skill : requestedSkills)
		requestedSkillValues.items().push_back(JsonValue::number(skill));
	summary["requested_skills"] = std::move(requestedSkillValues);
	summary["skill_mode"].set_string(Config.BotSkills.empty() ? "uniform" : "per_bot");
	summary["maximum_observed_bots"].set_number(MaximumObservedBots);
	summary["maximum_observed_inventory"].set_number(MaximumObservedInventory);
	summary["viewport_actor_class"].set_string(ViewportActorClass);
	summary["viewport_actor_is_spectator"].set_boolean(ViewportActorIsSpectator);
	summary["viewport_pri_is_spectator"].set_boolean(ViewportPRIIsSpectator);
	summary["maximum_observed_non_bot_participants"].set_number(MaximumObservedNonBotParticipants);
	summary["non_bot_enemy_target_observations"].set_number(static_cast<double>(NonBotEnemyTargetObservations));
	JsonValue nonBotTargets = JsonValue::array();
	for (const std::string& target : NonBotEnemyTargets)
		nonBotTargets.items().push_back(JsonValue::string(target));
	summary["non_bot_enemy_targets"] = std::move(nonBotTargets);
	summary["observed_live_bot"].set_boolean(ObservedLiveBot);
	summary["observed_bot_movement"].set_boolean(ObservedBotMovement);
	summary["deaths"].set_number(static_cast<double>(Deaths));
	summary["event_count"].set_number(static_cast<double>(EventSequence));
	summary["digest_fnv1a64"].set_string(DigestHex());
	JsonValue fixture = JsonValue::object();
	fixture["id"].set_string(Config.FixtureId);
	fixture["status"].set_string(FixtureStatus);
	fixture["assertions_total"].set_number(FixtureAssertionsTotal);
	fixture["assertions_passed"].set_number(FixtureAssertionsPassed);
	fixture["assertions_failed"].set_number(FixtureAssertionsFailed);
	summary["fixture"] = std::move(fixture);
	summary["url"].set_string(Config.URL);
	if (engine)
	{
		summary["game"].set_string(engine->LaunchInfo.gameName);
		summary["game_version"].set_string(engine->LaunchInfo.gameVersionString);
		summary["map"].set_string(engine->LevelInfo ? engine->LevelInfo->URL.Map : std::string());
	}

	JsonValue bots = JsonValue::array();
	for (const auto& entry : PreviousPawns)
	{
		const PawnSnapshot& pawn = entry.second;
		JsonValue item = JsonValue::object();
		item["actor"].set_string(pawn.Name);
		item["class"].set_string(pawn.ClassName);
		item["state"].set_string(pawn.State);
		item["health"].set_number(pawn.Health);
		item["inventory_count"].set_number(pawn.InventoryCount);
		item["selected_weapon_class"].set_string(pawn.SelectedWeaponClass);
		item["weapon_count"].set_number(pawn.WeaponCount);
		item["viable_weapon_count"].set_number(pawn.ViableWeaponCount);
		item["useful_ammo_total"].set_number(pawn.UsefulAmmoTotal);
		item["armor_total"].set_number(pawn.ArmorTotal);
		item["pri_score"].set_number(pawn.Score);
		item["pri_deaths"].set_number(pawn.PRIDeaths);
		item["pawn_kill_count"].set_number(pawn.PawnKillCount);
		item["requested_external_skill"].set_number(pawn.RequestedSkill);
		item["skill"].set_number(pawn.Skill);
		item["novice"].set_boolean(pawn.Novice);
		item["fire_intent"].set_boolean(pawn.FireIntent);
		item["alt_fire_intent"].set_boolean(pawn.AltFireIntent);
		bots.items().push_back(std::move(item));
	}
	summary["bots"] = bots;

	// These aggregates intentionally distinguish exact script-call observations
	// from fixed-tick snapshots and tick-delta proxies.
	JsonValue metrics = JsonValue::array();
	for (const auto& entry : Telemetry)
	{
		const BotTelemetry& telemetry = entry.second;
		JsonValue item = JsonValue::object();
		item["identity"].set_string(telemetry.Identity);
		item["player_name"].set_string(telemetry.PlayerName);
		item["last_actor"].set_string(telemetry.LastActor);
		item["requested_external_skill"].set_number(telemetry.RequestedSkill);
		item["first_observed_tick"].set_number(static_cast<double>(telemetry.FirstObservedTick));
		if (telemetry.FirstViableWeaponTick == UINT64_MAX)
			item["first_viable_weapon_tick"].set_null();
		else
			item["first_viable_weapon_tick"].set_number(static_cast<double>(telemetry.FirstViableWeaponTick));
		if (telemetry.FirstNovelWeaponTick == UINT64_MAX)
			item["first_nonstarter_weapon_tick"].set_null();
		else
			item["first_nonstarter_weapon_tick"].set_number(static_cast<double>(telemetry.FirstNovelWeaponTick));
		if (telemetry.FirstWeaponGainTick == UINT64_MAX)
			item["first_weapon_gain_tick"].set_null();
		else
			item["first_weapon_gain_tick"].set_number(static_cast<double>(telemetry.FirstWeaponGainTick));
		if (telemetry.FirstUsefulAmmoGainTick == UINT64_MAX)
			item["first_useful_ammo_gain_tick"].set_null();
		else
			item["first_useful_ammo_gain_tick"].set_number(static_cast<double>(telemetry.FirstUsefulAmmoGainTick));
		item["initial_weapon_count"].set_number(telemetry.InitialWeaponCount);
		item["maximum_weapon_count"].set_number(telemetry.MaximumWeaponCount);
		item["weapon_gain_proxy"].set_number(telemetry.WeaponGainProxy);
		item["initial_useful_ammo"].set_number(telemetry.InitialUsefulAmmo);
		item["maximum_useful_ammo"].set_number(telemetry.MaximumUsefulAmmo);
		item["useful_ammo_gain_proxy"].set_number(telemetry.UsefulAmmoGainProxy);
		item["useful_ammo_spent_proxy"].set_number(telemetry.UsefulAmmoSpentProxy);
		item["maximum_armor"].set_number(telemetry.MaximumArmor);
		item["health_gained"].set_number(telemetry.HealthGained);
		item["health_recovery_events"].set_number(telemetry.HealthRecoveryEvents);
		item["health_recovery_latency_ticks_total"].set_number(static_cast<double>(telemetry.HealthRecoveryLatencyTicksTotal));
		item["health_recovery_latency_ticks_max"].set_number(static_cast<double>(telemetry.HealthRecoveryLatencyTicksMax));
		item["damage_taken_snapshot_proxy"].set_number(telemetry.DamageTakenSnapshot);
		item["respawns_observed"].set_number(static_cast<double>(telemetry.RespawnsObserved));
		item["maximum_pawn_kill_count"].set_number(telemetry.MaximumPawnKillCount);
		item["initial_score"].set_number(telemetry.InitialScore);
		item["last_score"].set_number(telemetry.LastScore);
		item["maximum_pri_deaths"].set_number(telemetry.MaximumPRIDeaths);
		item["positive_score_delta_kill_proxy"].set_number(telemetry.PositiveScoreDeltaKillProxy);
		item["travel_distance_units"].set_number(telemetry.TravelDistanceUnits);
		item["movement_goal_seconds"].set_number(telemetry.MovementGoalSeconds);
		item["no_progress_seconds_proxy"].set_number(telemetry.NoProgressSecondsTotal);
		item["longest_no_progress_seconds_proxy"].set_number(telemetry.LongestNoProgressSeconds);
		item["stuck_events_proxy"].set_number(static_cast<double>(telemetry.StuckEvents));
		item["firing_intent_ticks"].set_number(static_cast<double>(telemetry.FiringIntentTicks));
		item["firing_intent_seconds"].set_number(static_cast<double>(telemetry.FiringIntentTicks) * Config.FixedDelta);
		item["damage_events_dealt_exact"].set_number(static_cast<double>(telemetry.DamageEventsDealt));
		item["damage_events_taken_exact"].set_number(static_cast<double>(telemetry.DamageEventsTaken));
		item["damage_dealt_exact"].set_number(telemetry.ExactDamageDealt);
		item["damage_taken_exact"].set_number(telemetry.ExactDamageTaken);
		item["self_damage_exact"].set_number(telemetry.ExactSelfDamage);
		item["fatal_damage_kills_exact"].set_number(static_cast<double>(telemetry.FatalDamageKills));
		item["fatal_damage_deaths_exact"].set_number(static_cast<double>(telemetry.FatalDamageDeaths));
		JsonValue combatByWeapon = JsonValue::array();
		for (const auto& [weaponMode, combat] : telemetry.WeaponCombatStats)
		{
			JsonValue weaponItem = JsonValue::object();
			weaponItem["weapon_mode"].set_string(weaponMode);
			weaponItem["hitscan_shots"].set_number(static_cast<double>(combat.HitscanShots));
			weaponItem["hitscan_hits"].set_number(static_cast<double>(combat.HitscanHits));
			weaponItem["hitscan_misses"].set_number(static_cast<double>(combat.HitscanShots - combat.HitscanHits));
			if (combat.HitscanShots == 0)
				weaponItem["hitscan_accuracy"].set_null();
			else
				weaponItem["hitscan_accuracy"].set_number(static_cast<double>(combat.HitscanHits) / combat.HitscanShots);
			weaponItem["projectile_launches"].set_number(static_cast<double>(combat.ProjectileLaunches));
			weaponItem["projectile_hits_finalized"].set_number(static_cast<double>(combat.ProjectileHits));
			weaponItem["projectile_misses_finalized"].set_number(static_cast<double>(combat.ProjectileMisses));
			weaponItem["projectiles_unresolved"].set_number(static_cast<double>(combat.ProjectileLaunches - combat.ProjectileHits - combat.ProjectileMisses));
			combatByWeapon.items().push_back(std::move(weaponItem));
		}
		item["combat_by_weapon"] = std::move(combatByWeapon);
		JsonValue weaponClasses = JsonValue::array();
		for (const std::string& weaponClass : telemetry.SelectedWeaponClasses)
			weaponClasses.items().push_back(JsonValue::string(weaponClass));
		item["selected_weapon_classes"] = std::move(weaponClasses);
		metrics.items().push_back(std::move(item));
	}
	summary["bot_metrics"] = metrics;
	summary["metric_semantics"].set_string("damage/hitscan/projectile/firing-intent fields exact at script/native boundaries; snapshots exact per fixed tick; fields ending _proxy are tick-delta derived");

	const std::filesystem::path summaryPath = std::filesystem::path(Config.OutputDirectory) / "summary.json";
	File::write_all_text(summaryPath.string(), summary.to_json(true) + "\n");
}

void BotBenchmark::HashCanonical(const std::string& text)
{
	for (unsigned char value : text)
	{
		Digest ^= static_cast<uint64_t>(value);
		Digest *= 1099511628211ULL;
	}
}

std::string BotBenchmark::DigestHex() const
{
	std::ostringstream out;
	out << std::hex << std::setfill('0') << std::setw(16) << Digest;
	return out.str();
}
