#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class CommandLine;
class Engine;
class File;
class UObject;
class UActor;
class UPawn;
class UProjectile;
class UWeapon;

struct BotBenchmarkConfig
{
	std::string Scenario = "phase-a-smoke";
	std::string OutputDirectory = "botbench-output";
	std::string URL = "DM-Morbias][?Game=Botpack.DeathMatchPlus";
	std::string BotName = "Loque";
	std::string FixtureId;
	uint64_t Seed = 104729;
	uint64_t MaxTicks = 600;
	float FixedDelta = 1.0f / 60.0f;
	int Difficulty = 7;
	int BotCount = 1;
	std::vector<int> BotSkills;
};

// Deterministic, presentation-free benchmark plumbing. It is deliberately
// inert unless --botbench is present. Bot behavior code may optionally call
// Emit() for high-value trace points; those calls are no-ops during normal play.
class BotBenchmark
{
public:
	static bool Requested(const CommandLine& commandLine);
	static void Configure(const CommandLine& commandLine);
	static bool IsActive();
	static BotBenchmark& Get();

	static void Emit(const std::string& type, const std::map<std::string, std::string>& fields = {});
	static void BeginDamage(UPawn* victim, int requestedDamage, UPawn* instigator, UObject* source, const std::string& damageType);
	static void EndDamage(UPawn* victim);
	static void BeginHitscan(UWeapon* weapon);
	static void EndHitscan(UWeapon* weapon);
	static void ProjectileSpawned(UProjectile* projectile, UActor* spawner);
	static void ProjectileDestroyed(UProjectile* projectile);

	const BotBenchmarkConfig& GetConfig() const { return Config; }
	void Initialize(Engine& engine);
	void AfterTick(Engine& engine);
	bool IsComplete() const { return Tick >= Config.MaxTicks || ExitCode != 0 || Finalized; }
	void Finalize(Engine& engine);
	void Fail(const std::string& message, int exitCode = 4);
	int GetExitCode() const { return ExitCode; }

private:
	struct PawnSnapshot
	{
		std::string Canonical;
		std::string Identity;
		std::string Name;
		std::string PlayerName;
		std::string ClassName;
		std::string State;
		std::string Enemy;
		std::string MoveTarget;
		std::string MoveTargetState;
		std::string TouchingActors;
		int TouchingActorCount = 0;
		std::string LatentAction;
		std::string SelectedWeaponClass;
		std::set<std::string> WeaponClasses;
		int Health = 0;
		int InventoryCount = 0;
		int WeaponCount = 0;
		int ViableWeaponCount = 0;
		int UsefulAmmoTotal = 0;
		int ArmorTotal = 0;
		int PlayerID = -1;
		int PawnKillCount = 0;
		float Score = 0.0f;
		float PRIDeaths = 0.0f;
		float Skill = 0.0f;
		float MoveTargetHorizontalDistance = -1.0f;
		float MoveTargetVerticalDistance = -1.0f;
		float MoveTargetCombinedRadius = -1.0f;
		float VelocityX = 0.0f;
		float VelocityY = 0.0f;
		float VelocityZ = 0.0f;
		float AccelerationX = 0.0f;
		float AccelerationY = 0.0f;
		float AccelerationZ = 0.0f;
		float DestinationX = 0.0f;
		float DestinationY = 0.0f;
		float DestinationZ = 0.0f;
		float MoveTimer = 0.0f;
		float DesiredSpeed = 0.0f;
		int Physics = 0;
		bool MoveTargetCollidesActors = false;
		bool MoveTargetHidden = false;
		bool FireIntent = false;
		bool AltFireIntent = false;
		int RequestedSkill = -1;
		bool Novice = false;
		int64_t X = 0;
		int64_t Y = 0;
		int64_t Z = 0;
	};

	struct BotTelemetry
	{
		struct WeaponCombat
		{
			uint64_t HitscanShots = 0;
			uint64_t HitscanHits = 0;
			uint64_t ProjectileLaunches = 0;
			uint64_t ProjectileHits = 0;
			uint64_t ProjectileMisses = 0;
		};

		std::string Identity;
		std::string PlayerName;
		std::string LastActor;
		int RequestedSkill = -1;
		std::set<std::string> SelectedWeaponClasses;
		std::set<std::string> InitialWeaponClasses;
		uint64_t FirstObservedTick = 0;
		uint64_t FirstViableWeaponTick = UINT64_MAX;
		uint64_t FirstNovelWeaponTick = UINT64_MAX;
		uint64_t FirstWeaponGainTick = UINT64_MAX;
		uint64_t FirstUsefulAmmoGainTick = UINT64_MAX;
		uint64_t HealthDeficitStartTick = UINT64_MAX;
		uint64_t HealthRecoveryLatencyTicksTotal = 0;
		uint64_t HealthRecoveryLatencyTicksMax = 0;
		uint64_t RespawnsObserved = 0;
		int InitialWeaponCount = 0;
		int MaximumWeaponCount = 0;
		int WeaponGainProxy = 0;
		int InitialUsefulAmmo = 0;
		int MaximumUsefulAmmo = 0;
		int UsefulAmmoGainProxy = 0;
		int UsefulAmmoSpentProxy = 0;
		int MaximumArmor = 0;
		int HealthGained = 0;
		int HealthRecoveryEvents = 0;
		int DamageTakenSnapshot = 0;
		int MaximumPawnKillCount = 0;
		float InitialScore = 0.0f;
		float LastScore = 0.0f;
		float MaximumPRIDeaths = 0.0f;
		float PositiveScoreDeltaKillProxy = 0.0f;
		double TravelDistanceUnits = 0.0;
		double MovementGoalSeconds = 0.0;
		double NoProgressSecondsTotal = 0.0;
		double CurrentNoProgressSeconds = 0.0;
		double LongestNoProgressSeconds = 0.0;
		uint64_t StuckEvents = 0;
		uint64_t FiringIntentTicks = 0;
		uint64_t DamageEventsDealt = 0;
		uint64_t DamageEventsTaken = 0;
		uint64_t FatalDamageKills = 0;
		uint64_t FatalDamageDeaths = 0;
		int ExactDamageDealt = 0;
		int ExactDamageTaken = 0;
		int ExactSelfDamage = 0;
		std::map<std::string, WeaponCombat> WeaponCombatStats;
		bool Initialized = false;
		bool AwaitingRespawn = false;
		bool InStuckInterval = false;
	};

	struct DamageObservation
	{
		int Depth = 0;
		int HealthBefore = 0;
		int RequestedDamage = 0;
		UPawn* Instigator = nullptr;
		UObject* Source = nullptr;
		std::string DamageType;
	};

	struct HitscanObservation
	{
		int Depth = 0;
		UPawn* Shooter = nullptr;
		std::string WeaponMode;
		int OpponentHealthDamage = 0;
		int SelfHealthDamage = 0;
		uint64_t OpponentDamageEvents = 0;
	};

	struct ProjectileObservation
	{
		std::string ShooterIdentity;
		std::string WeaponMode;
		std::string ProjectileClass;
		bool HitOpponent = false;
		int OpponentHealthDamage = 0;
	};

	explicit BotBenchmark(BotBenchmarkConfig config);

	static BotBenchmarkConfig LoadConfig(const CommandLine& commandLine);
	static std::unique_ptr<BotBenchmark> Instance;

	void OpenOutput();
	bool ValidateViewportSpectator(Engine& engine);
	void ConfigureBots(Engine& engine);
	void RunControlledFixture(Engine& engine);
	void ObserveBots(Engine& engine);
	void UpdateTelemetry(const PawnSnapshot& snapshot, const PawnSnapshot* previous);
	BotTelemetry& TelemetryForPawn(UPawn* pawn);
	void RecordDamage(UPawn* victim, const DamageObservation& observation);
	static std::string WeaponMode(UWeapon* weapon);
	void WriteEvent(const std::string& type, const std::map<std::string, std::string>& fields);
	void WriteSummary(Engine* engine, const std::string& status, const std::string& reason);
	PawnSnapshot Capture(UPawn* pawn) const;
	void HashCanonical(const std::string& text);
	std::string DigestHex() const;

	BotBenchmarkConfig Config;
	std::shared_ptr<File> EventsFile;
	std::map<std::string, PawnSnapshot> PreviousPawns;
	std::map<std::string, BotTelemetry> Telemetry;
	std::map<std::string, int> RequestedSkillByIdentity;
	std::map<UPawn*, DamageObservation> ActiveDamage;
	std::map<UWeapon*, HitscanObservation> ActiveHitscan;
	std::map<UProjectile*, ProjectileObservation> Projectiles;
	uint64_t Tick = 0;
	uint64_t EventSequence = 0;
	uint64_t Digest = 1469598103934665603ULL;
	uint64_t Deaths = 0;
	int MaximumObservedBots = 0;
	int MaximumObservedInventory = 0;
	int MaximumObservedNonBotParticipants = 0;
	uint64_t NonBotEnemyTargetObservations = 0;
	std::set<std::string> NonBotEnemyTargets;
	std::string ViewportActorClass;
	bool ViewportActorIsSpectator = false;
	bool ViewportPRIIsSpectator = false;
	int ExitCode = 0;
	bool ObservedLiveBot = false;
	bool ObservedBotMovement = false;
	std::string FixtureStatus = "inactive";
	int FixtureAssertionsTotal = 0;
	int FixtureAssertionsPassed = 0;
	int FixtureAssertionsFailed = 0;
	bool Finalized = false;
	std::string FailureReason;
};
