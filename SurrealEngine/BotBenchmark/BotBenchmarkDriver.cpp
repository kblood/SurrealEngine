#include "Precomp.h"
#include "BotBenchmarkDriver.h"
#include "BotControlledMatch.h"
#include "BotBenchmarkGameProfile.h"
#include "BotBenchmarkProtocol.h"
#include "BotBenchmarkShadowTelemetry.h"
#include "BotBenchmarkTelemetry.h"
#include "BotAI/BotPolicyObservationBuilder.h"
#include "BotAI/BotPolicyRegistry.h"
#include "BotAI/BotPolicyShadow.h"
#include "Engine.h"
#include "Runtime/HeadlessDriver.h"
#include "Utils/CommandLine.h"
#include "Utils/File.h"
#include "Utils/Logger.h"
#include "Package/PackageManager.h"
#include "UObject/ULevel.h"
#include "UObject/UClient.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
namespace
{
	class BotBenchmarkDriver final : public HeadlessDriver
	{
	public:
		BotBenchmarkDriver(Engine& engine, BotBenchmarkRunConfig config)
			: EngineRef(engine), Config(std::move(config))
		{
		}

		~BotBenchmarkDriver() override
		{
			if (QualityHookHandle != 0)
				Frame::CallHooks().Unregister(QualityHookHandle);
		}

		HeadlessDriverConfig GetConfig() const override
		{
			HeadlessDriverConfig config;
			config.Runtime.Seed = Config.GetSeed();
			config.Runtime.FixedDelta = Config.GetFixedDelta();
			config.MaxTicks = Config.GetMaxTicks();
			return config;
		}

		void Start() override
		{
			try
			{
				OpenTelemetry();
				ValidateGameProfile();
				if (!Complete)
					SetupControlledMatch();
				if (!Complete)
					OpenShadowTelemetry();
			}
			catch (const std::exception& e)
			{
				Fail(e.what());
			}

			try
			{
				WriteTelemetry("run_start", ExitCode == 0 ? "running" : "failed", FailureReason, 0, 0.0);
			}
			catch (const std::exception& e)
			{
				Fail(std::string("telemetry start failed: ") + e.what());
			}
		}

		bool IsComplete() const override
		{
			return Complete || Ticks >= Config.GetMaxTicks();
		}

		void Tick(const DeterministicFrameTime& frameTime) override
		{
			try
			{
				const float levelElapsed = frameTime.RealElapsed * clamp(EngineRef.LevelInfo->TimeDilation(), 0.0025f, 25.0f);
				EngineRef.TotalTime = frameTime.TotalReal;
				EngineRef.LevelInfo->TimeSeconds() += levelElapsed;
				Logger::Get()->SetTimeSeconds(EngineRef.LevelInfo->TimeSeconds());

				const RuntimeWallClock& wallClock = GetConfig().Runtime.WallClock;
				EngineRef.LevelInfo->Year() = wallClock.Year;
				EngineRef.LevelInfo->Month() = wallClock.Month;
				EngineRef.LevelInfo->Day() = wallClock.Day;
				EngineRef.LevelInfo->DayOfWeek() = wallClock.DayOfWeek;
				EngineRef.LevelInfo->Hour() = wallClock.Hour;
				EngineRef.LevelInfo->Minute() = wallClock.Minute;
				EngineRef.LevelInfo->Second() = wallClock.Second;
				EngineRef.LevelInfo->Millisecond() = wallClock.Millisecond;

				EngineRef.SetPause(false);
				CallEvent(EngineRef.console, EventName::Tick, { ExpressionValue::FloatValue(levelElapsed) });
				if (EngineRef.LaunchInfo.ue1Version >= 436)
				{
					EngineRef.LevelInfo->bDropDetail() = false;
					EngineRef.LevelInfo->bAggressiveLOD() = false;
				}
				EngineRef.Level->Tick(levelElapsed, false);
				Ticks = frameTime.Tick;
				WriteShadowTelemetry(frameTime.Tick, frameTime.RealElapsed);
				WriteTelemetry("tick", "running", {}, frameTime.Tick, frameTime.TotalReal);
			}
			catch (const std::exception& e)
			{
				Fail(e.what());
			}
		}

		int Finish(const HeadlessRunSummary& summary) override
		{
			if (summary.TickLimitReached && ExitCode == 0)
				Fail("headless runner reached its tick limit before driver completion");
			if (ExitCode == 0 && ShadowTelemetryFile && ShadowTelemetryEventCount != Ticks)
				Fail("shadow telemetry did not produce exactly one record per simulated tick");

			const double simulatedSeconds = static_cast<double>(Ticks) * Config.GetFixedDelta();
			try
			{
				WriteTelemetry("run_result", ExitCode == 0 ? "complete" : "failed",
					FailureReason, Ticks, simulatedSeconds);
			}
			catch (const std::exception& e)
			{
				Fail(std::string("telemetry finalization failed: ") + e.what());
			}

			const std::string status = ExitCode == 0 ? "complete" : "failed";
			BotBenchmarkRunSummary runSummary(status, ExitCode, Ticks, simulatedSeconds,
				EngineRef.LaunchInfo.gameName, EngineRef.LaunchInfo.gameVersionString,
				EngineRef.LevelInfo ? EngineRef.LevelInfo->URL.Map : std::string(),
				FailureReason, ActualRoster);
			std::filesystem::create_directories(Config.GetOutputDirectory());
			const std::filesystem::path summaryPath = std::filesystem::path(Config.GetOutputDirectory()) / "summary.json";
			File::write_all_text(summaryPath.string(), runSummary.ToJson(Config));
			LogMessage("Bot benchmark summary: " + summaryPath.string());
			return ExitCode;
		}

	private:
		struct ShadowParticipantRuntime
		{
			size_t RosterIndex = 0;
			std::string Identity;
			std::unique_ptr<BotAI::PolicyShadowEvaluator> Evaluator;
			bool HasPreviousHealth = false;
			int PreviousHealth = 0;
		};

		struct QualityParticipantRuntime
		{
			std::string Actor;
			std::string PlayerName;
			std::string ClassName;
			UPlayerReplicationInfo* Pri = nullptr;
			BotBenchmarkBotState LastState;
			bool HasLastState = false;
			uint64_t KillsExact = 0;
			uint64_t DeathsExact = 0;
			uint64_t SuicidesExact = 0;
			uint64_t EnvironmentalDeathsExact = 0;
			uint64_t HazardExposedDeathsProxy = 0;
			uint64_t HitWallEventsExact = 0;
		};

		void ValidateGameProfile()
		{
			const BotBenchmarkGameProfile profile = BotBenchmarkGameProfileResolver::Resolve(
				EngineRef.LaunchInfo.gameName, EngineRef.LaunchInfo.gameVersionString);
			if (!profile.ControlledBenchmarkSupported)
			{
				Fail("bot benchmark game profile is unsupported: " + profile.UnsupportedReason);
				return;
			}
			if (!profile.SupportsMode(BotBenchmarkGameMode::Deathmatch))
				Fail("bot benchmark game profile has no verified deathmatch mode");
		}

		static std::string PawnIdentity(UPawn* pawn)
		{
			if (!pawn)
				return {};
			if (UPlayerReplicationInfo* pri = pawn->PlayerReplicationInfo())
				return "pri:" + std::to_string(pri->PlayerID());
			return "actor:" + pawn->Name.ToString();
		}

		static bool IsHazardZone(UZoneInfo* zone)
		{
			return zone &&
				((zone->HasProperty("bPainZone") && zone->GetBool("bPainZone")) ||
				(zone->HasProperty("bKillZone") && zone->GetBool("bKillZone")) ||
				(zone->HasProperty("DamagePerSec") && zone->GetInt("DamagePerSec") > 0));
		}

		static bool IsInHazardZone(UPawn* pawn)
		{
			return pawn &&
				((pawn->HasProperty("Region") && IsHazardZone(pawn->Region().Zone)) ||
				(pawn->HasProperty("FootRegion") && IsHazardZone(pawn->FootRegion().Zone)));
		}

		static bool HasMovementIntent(UPawn* pawn)
		{
			if (!pawn)
				return false;
			if (pawn->StateFrame)
			{
				switch (pawn->StateFrame->LatentState)
				{
				case LatentRunState::MoveTo:
				case LatentRunState::MoveToward:
				case LatentRunState::StrafeTo:
				case LatentRunState::StrafeFacing:
					return true;
				default:
					break;
				}
			}
			if (!pawn->HasProperty("Acceleration"))
				return false;
			const vec3 acceleration = pawn->Acceleration();
			return acceleration.x * acceleration.x + acceleration.y * acceleration.y > 0.0625f;
		}

		void RecordKilled(UPawn* killer, UPawn* victim)
		{
			const std::string victimIdentity = PawnIdentity(victim);
			auto victimRuntime = QualityParticipants.find(victimIdentity);
			if (victimRuntime != QualityParticipants.end())
			{
				QualityParticipantRuntime& counters = victimRuntime->second;
				counters.DeathsExact++;
				const bool environmental = !killer || !killer->bIsPlayer();
				if (killer == victim || environmental)
					counters.SuicidesExact++;
				if (environmental)
					counters.EnvironmentalDeathsExact++;
				if (IsInHazardZone(victim))
					counters.HazardExposedDeathsProxy++;
			}

			if (killer && killer != victim && killer->bIsPlayer())
			{
				auto killerRuntime = QualityParticipants.find(PawnIdentity(killer));
				if (killerRuntime != QualityParticipants.end())
					killerRuntime->second.KillsExact++;
			}
		}

		void RegisterQualityHooks()
		{
			VMCallHook hook;
			hook.Enter = [this](UFunction* function, UObject* instance,
				VMCallArguments& arguments) -> VMCallHookCleanup
			{
				if (!function)
					return {};
				if (function->Name == "Killed" && instance == EngineRef.GameInfo)
				{
					const bool outermost = KilledHookDepth++ == 0;
					if (outermost && arguments.Size() >= 2)
					{
						UPawn* killer = UObject::TryCast<UPawn>(arguments.Values()[0].ToObject());
						UPawn* victim = UObject::TryCast<UPawn>(arguments.Values()[1].ToObject());
						if (victim)
							RecordKilled(killer, victim);
					}
					return [this]() { KilledHookDepth--; };
				}
				if (function->Name == "HitWall")
				{
					UPawn* pawn = UObject::TryCast<UPawn>(instance);
					auto runtime = QualityParticipants.find(PawnIdentity(pawn));
					if (runtime == QualityParticipants.end())
						return {};
					const bool outermost = HitWallHookDepth++ == 0;
					if (outermost)
						runtime->second.HitWallEventsExact++;
					return [this]() { HitWallHookDepth--; };
				}
				return {};
			};
			QualityHookHandle = Frame::CallHooks().Register(std::move(hook));
		}

		std::vector<std::pair<std::string, UPawn*>> CaptureLiveControlledBots() const
		{
			std::vector<std::pair<std::string, UPawn*>> bots;
			if (!EngineRef.Level)
				return bots;
			for (UActor* actor : EngineRef.Level->Actors)
			{
				UPawn* pawn = UObject::TryCast<UPawn>(actor);
				if (!pawn || pawn->bDeleteMe())
					continue;
				const std::string identity = PawnIdentity(pawn);
				const auto controlled = std::find_if(ActualRoster.begin(), ActualRoster.end(), [&](const auto& participant)
				{
					return participant.Identity == identity;
				});
				if (controlled != ActualRoster.end())
					bots.emplace_back(identity, pawn);
			}
			std::sort(bots.begin(), bots.end(), [](const auto& left, const auto& right)
			{
				return left.first < right.first;
			});
			for (size_t index = 1; index < bots.size(); index++)
			{
				if (bots[index - 1].first == bots[index].first)
					throw std::runtime_error("multiple live controlled bots share identity '" + bots[index].first + "'");
			}
			return bots;
		}

		BotAI::RuntimeObservationSnapshot CaptureShadowObservation(
			ShadowParticipantRuntime& runtime,
			UPawn* self,
			const std::vector<std::pair<std::string, UPawn*>>& liveBots,
			uint64_t tick,
			double deltaSeconds) const
		{
			BotAI::RuntimeObservationSnapshot snapshot;
			snapshot.Tick = tick;
			snapshot.DeltaSeconds = deltaSeconds;
			snapshot.Self.StableIdentity = runtime.Identity;
			snapshot.Self.Position = { self->Location().x, self->Location().y, self->Location().z };
			snapshot.Self.HealthFraction = static_cast<double>(self->Health()) / 100.0;

			if (runtime.HasPreviousHealth && self->Health() < runtime.PreviousHealth)
				snapshot.Self.RecentIncomingDamage =
					static_cast<double>(runtime.PreviousHealth - self->Health()) / 100.0;
			runtime.HasPreviousHealth = true;
			runtime.PreviousHealth = self->Health();

			UWeapon* weapon = self->Weapon();
			if (weapon && !weapon->bDeleteMe())
			{
				snapshot.Self.HasUsableWeapon = true;
				snapshot.Self.AmmunitionFraction = 1.0;
				if (weapon->HasProperty("AmmoType"))
				{
					UObject* ammo = weapon->GetUObject("AmmoType");
					if (ammo && ammo->HasProperty("AmmoAmount") && ammo->HasProperty("MaxAmmo"))
					{
						const uint32_t amount = ammo->GetInt("AmmoAmount");
						const uint32_t maximum = ammo->GetInt("MaxAmmo");
						snapshot.Self.AmmunitionFraction = maximum == 0
							? 0.0 : static_cast<double>(amount) / static_cast<double>(maximum);
						const bool melee = weapon->HasProperty("bMeleeWeapon") && weapon->GetBool("bMeleeWeapon");
						snapshot.Self.HasUsableWeapon = melee || amount > 0;
					}
				}
			}

			for (const auto& [identity, other] : liveBots)
			{
				if (other == self || other->Health() <= 0)
					continue;
				const bool visible = self->LineOfSightTo(other, false);
				if (!visible)
					continue;
				const bool firingAtBot = other->Enemy() == self && (other->bFire() != 0 || other->bAltFire() != 0);

				BotAI::RuntimeEnemySnapshot enemy;
				enemy.StableIdentity = identity;
				enemy.Position = { other->Location().x, other->Location().y, other->Location().z };
				enemy.Velocity = { other->Velocity().x, other->Velocity().y, other->Velocity().z };
				enemy.Confidence = 1.0;
				enemy.EstimatedHealthFraction = static_cast<double>(other->Health()) / 100.0;
				enemy.Visible = visible;
				enemy.HasLineOfSight = visible;
				enemy.FiringAtBot = firingAtBot;
				enemy.Hostile = true;
				snapshot.Enemies.push_back(std::move(enemy));
			}
			return snapshot;
		}

		void OpenShadowTelemetry()
		{
			std::vector<BotBenchmarkShadowParticipantDescriptor> participants;
			participants.reserve(ActualRoster.size());
			ShadowParticipants.reserve(ActualRoster.size());
			const auto& policies = BotAI::PolicyRegistry::Enumerate();
			for (const auto& actual : ActualRoster)
			{
				auto runtime = std::make_unique<ShadowParticipantRuntime>();
				runtime->RosterIndex = actual.RosterIndex;
				runtime->Identity = actual.Identity;
				runtime->Evaluator = std::make_unique<BotAI::PolicyShadowEvaluator>(policies.size());
				for (const auto& policy : policies)
				{
					if (runtime->Evaluator->AddPolicy(policy.Id) != BotAI::ShadowAddStatus::Added)
						throw std::runtime_error("failed to initialize shadow policy '" + policy.Id + "'");
				}
				runtime->Evaluator->Reset(Config.GetSeed() + static_cast<uint64_t>(actual.RosterIndex));
				participants.push_back({ actual.RosterIndex, actual.Identity });
				ShadowParticipants.push_back(std::move(runtime));
			}

			const std::filesystem::path outputDirectory(Config.GetOutputDirectory());
			const std::filesystem::path manifestPath = outputDirectory / "shadow-manifest.json";
			File::write_all_text(manifestPath.string(), BotBenchmarkShadowTelemetry::ManifestJson(
				TelemetryConfigIdentity, Config.GetMaxTicks(), policies, std::move(participants)));
			const std::filesystem::path eventsPath = outputDirectory / "shadow-decisions.jsonl";
			ShadowTelemetryFile = File::create_always(eventsPath.string());
			LogMessage("Bot benchmark shadow manifest: " + manifestPath.string());
			LogMessage("Bot benchmark shadow decisions: " + eventsPath.string());
		}

		void WriteShadowTelemetry(uint64_t tick, double deltaSeconds)
		{
			if (!ShadowTelemetryFile)
				return;
			if (ShadowTelemetryEventCount >= ShadowTelemetryEventCap)
				throw std::runtime_error("bot benchmark shadow telemetry event cap reached");

			const auto liveBots = CaptureLiveControlledBots();
			std::vector<BotBenchmarkShadowParticipantState> states;
			states.reserve(ShadowParticipants.size());
			for (auto& runtime : ShadowParticipants)
			{
				const auto live = std::find_if(liveBots.begin(), liveBots.end(), [&](const auto& bot)
				{
					return bot.first == runtime->Identity;
				});
				const bool available = live != liveBots.end() && live->second->Health() > 0;
				if (available)
				{
					BotAI::RuntimeObservationSnapshot snapshot = CaptureShadowObservation(
						*runtime, live->second, liveBots, tick, deltaSeconds);
					runtime->Evaluator->Evaluate(BotAI::PolicyObservationBuilder::Build(snapshot));
				}
				states.push_back({ runtime->RosterIndex, runtime->Identity, available,
					runtime->Evaluator->GetSnapshots() });
			}

			const std::string line = BotBenchmarkShadowTelemetry::EventJson(
				TelemetryConfigIdentity, ShadowTelemetryEventCount, tick, std::move(states));
			ShadowTelemetryFile->write(line.data(), line.size());
			ShadowTelemetryEventCount++;
		}

		void OpenTelemetry()
		{
			std::filesystem::create_directories(Config.GetOutputDirectory());
			const std::filesystem::path outputDirectory(Config.GetOutputDirectory());
			const std::filesystem::path manifestPath = outputDirectory / "manifest.json";
			File::write_all_text(manifestPath.string(), BotBenchmarkTelemetryProtocol::ManifestJson(Config));
			const std::filesystem::path eventsPath = outputDirectory / "events.jsonl";
			TelemetryFile = File::create_always(eventsPath.string());
			LogMessage("Bot benchmark manifest: " + manifestPath.string());
			LogMessage("Bot benchmark telemetry: " + eventsPath.string());
		}

		std::vector<BotBenchmarkBotState> CaptureBotStates()
		{
			std::vector<BotBenchmarkBotState> bots;
			const auto liveBots = CaptureLiveControlledBots();
			for (const BotBenchmarkActualParticipant& actual : ActualRoster)
			{
				auto runtimeEntry = QualityParticipants.find(actual.Identity);
				if (runtimeEntry == QualityParticipants.end())
					continue;
				QualityParticipantRuntime& runtime = runtimeEntry->second;
				const auto live = std::find_if(liveBots.begin(), liveBots.end(), [&](const auto& item)
				{
					return item.first == actual.Identity;
				});
				UPawn* pawn = live == liveBots.end() ? nullptr : live->second;
				BotBenchmarkBotState bot = runtime.HasLastState ? runtime.LastState : BotBenchmarkBotState{};
				bot.Identity = actual.Identity;
				bot.Actor = runtime.Actor;
				bot.PlayerName = runtime.PlayerName;
				bot.ClassName = runtime.ClassName;
				if (pawn)
				{
					runtime.Pri = pawn->PlayerReplicationInfo();
					bot.State = pawn->GetStateName().ToString();
					bot.PositionX = pawn->Location().x;
					bot.PositionY = pawn->Location().y;
					bot.PositionZ = pawn->Location().z;
					bot.VelocityX = pawn->Velocity().x;
					bot.VelocityY = pawn->Velocity().y;
					bot.VelocityZ = pawn->Velocity().z;
					bot.Health = pawn->Health();
					bot.MovementIntent = HasMovementIntent(pawn);
					bot.InHazardZone = IsInHazardZone(pawn);
				}
				else
				{
					bot.State = "Unavailable";
					bot.Health = 0;
					bot.VelocityX = bot.VelocityY = bot.VelocityZ = 0.0;
					bot.MovementIntent = false;
					bot.InHazardZone = false;
				}
				if (runtime.Pri)
				{
					bot.Score = runtime.Pri->HasProperty("Score") ? runtime.Pri->GetFloat("Score") : 0.0;
					bot.PriDeaths = runtime.Pri->HasProperty("Deaths") ? runtime.Pri->GetFloat("Deaths") : 0.0;
				}
				bot.KillsExact = runtime.KillsExact;
				bot.DeathsExact = runtime.DeathsExact;
				bot.SuicidesExact = runtime.SuicidesExact;
				bot.EnvironmentalDeathsExact = runtime.EnvironmentalDeathsExact;
				bot.HazardExposedDeathsProxy = runtime.HazardExposedDeathsProxy;
				bot.HitWallEventsExact = runtime.HitWallEventsExact;
				runtime.LastState = bot;
				runtime.HasLastState = true;
				bots.push_back(std::move(bot));
			}
			return bots;
		}

		void WriteTelemetry(const std::string& type, const std::string& status,
			const std::string& failureReason, uint64_t tick, double simulatedSeconds)
		{
			if (!TelemetryFile)
				return;
			if (TelemetryEventCount >= TelemetryEventCap)
				throw std::runtime_error("bot benchmark telemetry event cap reached");

			BotBenchmarkTelemetryEvent event;
			event.Sequence = TelemetryEventCount;
			event.Tick = tick;
			event.SimulatedSeconds = simulatedSeconds;
			event.Type = type;
			event.Map = EngineRef.LevelInfo ? EngineRef.LevelInfo->URL.Map : std::string();
			event.Status = status;
			event.FailureReason = failureReason;
			event.Bots = CaptureBotStates();
			const std::string line = BotBenchmarkTelemetryProtocol::EventJson(TelemetryConfigIdentity, std::move(event));
			TelemetryFile->write(line.data(), line.size());
			TelemetryEventCount++;
		}

		void SetupControlledMatch()
		{
			ActualRoster.reserve(Config.GetRoster().GetCount());
			BotControlledMatch::Setup(EngineRef, Config.GetURL(), Config.GetRoster(),
				[this](const BotControlledParticipant& participant)
			{
				BotBenchmarkActualParticipant actual;
				actual.RosterIndex = participant.RosterIndex;
				actual.Identity = participant.Identity;
				actual.Actor = participant.Actor;
				actual.PlayerName = participant.PlayerName;
				actual.ClassName = participant.ClassName;
				ActualRoster.push_back(std::move(actual));
				QualityParticipantRuntime runtime;
				runtime.Actor = participant.Actor;
				runtime.PlayerName = participant.PlayerName;
				runtime.ClassName = participant.ClassName;
				runtime.Pri = participant.Pawn ? participant.Pawn->PlayerReplicationInfo() : nullptr;
				QualityParticipants.emplace(participant.Identity, std::move(runtime));
			});
			RegisterQualityHooks();
		}

		void Fail(std::string reason)
		{
			if (ExitCode == 0)
			{
				ExitCode = 2;
				FailureReason = std::move(reason);
				LogMessage("Bot benchmark failed: " + FailureReason);
			}
			Complete = true;
		}

		Engine& EngineRef;
		const BotBenchmarkRunConfig Config;
		const std::string TelemetryConfigIdentity = BotBenchmarkTelemetryProtocol::ConfigIdentity(Config);
		const uint64_t TelemetryEventCap = BotBenchmarkTelemetryProtocol::EventCap(Config.GetMaxTicks());
		const uint64_t ShadowTelemetryEventCap = BotBenchmarkShadowTelemetry::EventCap(Config.GetMaxTicks());
		std::shared_ptr<File> TelemetryFile;
		std::shared_ptr<File> ShadowTelemetryFile;
		uint64_t TelemetryEventCount = 0;
		uint64_t ShadowTelemetryEventCount = 0;
		uint64_t Ticks = 0;
		int ExitCode = 0;
		bool Complete = false;
		std::string FailureReason;
		std::vector<BotBenchmarkActualParticipant> ActualRoster;
		std::map<std::string, QualityParticipantRuntime> QualityParticipants;
		std::vector<std::unique_ptr<ShadowParticipantRuntime>> ShadowParticipants;
		VMCallHookHandle QualityHookHandle = 0;
		uint32_t KilledHookDepth = 0;
		uint32_t HitWallHookDepth = 0;
	};

	std::optional<std::string> OptionalCommandLineArg(const char* name)
	{
		if (!commandline || !commandline->HasArg("", name))
			return {};
		return commandline->GetArg("", name);
	}

	BotBenchmarkRunConfig ConfigFromCommandLine()
	{
		return BotBenchmarkRunConfig::Parse(
			commandline ? commandline->GetArg("", "--botbench-url") : std::string(),
			commandline ? commandline->GetArg("", "--botbench-output") : std::string(),
			commandline ? commandline->GetArg("", "--botbench-seed") : std::string(),
			commandline ? commandline->GetArg("", "--botbench-ticks") : std::string(),
			commandline ? commandline->GetArg("", "--botbench-fixed-delta") : std::string(),
			commandline ? commandline->GetArg("", "--botbench-difficulty") : std::string(),
			OptionalCommandLineArg("--botbench-bots"),
			OptionalCommandLineArg("--botbench-skills"),
			OptionalCommandLineArg("--botbench-names"));
	}
}

void RegisterBotBenchmarkDriver(HeadlessDriverRegistry& registry)
{
	if (!registry.Contains("bot-benchmark"))
	{
		registry.Register("bot-benchmark", [](Engine& engine)
		{
			return std::make_unique<BotBenchmarkDriver>(engine, ConfigFromCommandLine());
		});
	}
}
