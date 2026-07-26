#include "Precomp.h"
#include "BotBenchmarkDriver.h"
#include "BotInventoryRouteHandoffFixture.h"
#include "BotMoveStallRecoveryFixture.h"
#include "BotWalkingHitWallCornerFixture.h"
#include "BotControlledMatch.h"
#include "BotBenchmarkGameProfile.h"
#include "BotBenchmarkDeathAttributionCoordinator.h"
#include "BotBenchmarkDeathAttributionHookContract.h"
#include "BotBenchmarkHazardDeathPartition.h"
#include "BotTargetSelectionHookContract.h"
#include "BotTargetSelectionProbeTracker.h"
#include "BotBenchmarkProtocol.h"
#include "BotBenchmarkQualityObservation.h"
#include "MapCatalogDriver.h"
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
#include "UObject/UProperty.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <set>
namespace
{
	constexpr double DeathAttributionRecentWindowSeconds = 2.0;

	std::string JsonString(const std::string& value)
	{
		std::ostringstream out;
		out << '"';
		for (unsigned char character : value)
		{
			switch (character)
			{
			case '\\': out << "\\\\"; break;
			case '"': out << "\\\""; break;
			case '\n': out << "\\n"; break;
			case '\r': out << "\\r"; break;
			case '\t': out << "\\t"; break;
			default: out << static_cast<char>(character); break;
			}
		}
		out << '"';
		return out.str();
	}

	class BotBenchmarkDriver final : public HeadlessDriver
	{
	public:
		BotBenchmarkDriver(Engine& engine, BotBenchmarkRunConfig config)
			: EngineRef(engine), Config(std::move(config))
		{
			EngineRef.SetBotBenchmarkHarmfulZoneEscapeEnabled(Config.IsHarmfulZoneEscapeEnabled());
			EngineRef.SetBotBenchmarkWalkingPreflightPositiveDpsVetoEnabled(
				Config.IsWalkingPreflightPositiveDpsVetoEnabled());
			EngineRef.SetBotBenchmarkHazardSwimEgressEnabled(Config.IsHazardSwimEgressEnabled());
			EngineRef.SetBotBenchmarkHazardSwimEgressLiveEnabled(
				Config.IsHazardSwimEgressLiveEnabled());
			EngineRef.SetBotBenchmarkFallingHazardRecoveryEnabled(
				Config.IsFallingHazardRecoveryEnabled());
			EngineRef.SetBotBenchmarkFallingHazardRecoveryLiveEnabled(
				Config.IsFallingHazardRecoveryLiveEnabled());
			EngineRef.SetBotBenchmarkFailedNavigationAvoidanceEnabled(
				Config.IsFailedNavigationAvoidanceEnabled());
			EngineRef.SetBotBenchmarkTargetlessMoveToTimeoutEnabled(
				Config.IsTargetlessMoveToTimeoutEnabled());
			EngineRef.SetBotBenchmarkDirectActorMoveTowardTimeoutEnabled(
				Config.IsDirectActorMoveTowardTimeoutEnabled());
			EngineRef.SetBotBenchmarkInventoryDirectReachSupportObserverEnabled(
				Config.IsInventoryDirectReachSupportObserverEnabled());
			EngineRef.SetBotBenchmarkInventoryMarkerDirectReachSafetyEnabled(
				Config.IsInventoryMarkerDirectReachSafetyEnabled());
			EngineRef.SetBotBenchmarkNativePathCommitObserverEnabled(
				Config.IsNativePathCommitObserverEnabled());
			EngineRef.SetBotBenchmarkDirectReachCommandObserverEnabled(
				Config.IsDirectReachCommandObserverEnabled());
		}

		~BotBenchmarkDriver() override
		{
			if (QualityHookHandle != 0)
				Frame::CallHooks().Unregister(QualityHookHandle);
			if (TargetSelectionHookHandle != 0)
				Frame::CallHooks().Unregister(TargetSelectionHookHandle);
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
					WriteRealizedBotCapabilities();
				if (!Complete)
					OpenShadowTelemetry();
				if (!Complete)
					OpenRouteExecutionTelemetry();
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
				EngineRef.SetBotBenchmarkObserverTick(frameTime.Tick);
				CallEvent(EngineRef.console, EventName::Tick, { ExpressionValue::FloatValue(levelElapsed) });
				if (EngineRef.LaunchInfo.ue1Version >= 436)
				{
					EngineRef.LevelInfo->bDropDetail() = false;
					EngineRef.LevelInfo->bAggressiveLOD() = false;
				}
				EngineRef.Level->Tick(levelElapsed, false);
				Ticks = frameTime.Tick;
				uint64_t aiFrameScopeMicroseconds = 0;
				MeasureAiFrameScope(aiFrameScopeMicroseconds, [&]
				{
					ObserveNavigationCoverage();
				});
				WriteShadowTelemetry(frameTime.Tick, frameTime.RealElapsed, aiFrameScopeMicroseconds);
				WriteRouteExecutionTelemetry(frameTime.Tick);
				WriteTelemetry("tick", "running", {}, frameTime.Tick, frameTime.TotalReal,
					&aiFrameScopeMicroseconds);
				AiFrameTiming.AddSampleMicroseconds(aiFrameScopeMicroseconds);
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
			if (ExitCode == 0 && RouteExecutionTelemetryFile && RouteExecutionTelemetryEventCount != Ticks)
				Fail("route-execution telemetry did not produce exactly one record per simulated tick");

			const double simulatedSeconds = static_cast<double>(Ticks) * Config.GetFixedDelta();
			try
			{
				for (const auto& [identity, pawn] : CaptureLiveControlledBots())
				{
					auto runtime = QualityParticipants.find(identity);
					if (runtime == QualityParticipants.end())
						continue;
					pawn->EndMoveStallRecoveryRun();
					pawn->EndHazardSwimEgressRun();
					pawn->EndHazardResidenceRun();
					CensorAllOpenDirectReachCommands(runtime->second, "run_end_censor", Ticks);
					AccumulateNativePawnCounters(identity, runtime->second, pawn,
						BotBenchmarkDriverDetail::NativePawnCounterSample::LivePawn);
				}
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
				FailureReason, ActualRoster, AiFrameTiming.GetSummary());
			std::filesystem::create_directories(Config.GetOutputDirectory());
			const std::filesystem::path summaryPath = std::filesystem::path(Config.GetOutputDirectory()) / "summary.json";
			File::write_all_text(summaryPath.string(), runSummary.ToJson(Config));
			LogMessage("Bot benchmark summary: " + summaryPath.string());
			return ExitCode;
		}

	private:
		template <typename Callback>
		static void MeasureAiFrameScope(uint64_t& totalMicroseconds, Callback&& callback)
		{
			const auto began = std::chrono::steady_clock::now();
			callback();
			const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - began).count();
			if (elapsed > 0)
				totalMicroseconds += static_cast<uint64_t>(elapsed);
		}

		void WriteRealizedBotCapabilities()
		{
			const auto liveBots = CaptureLiveControlledBots();
			std::ostringstream out;
			out.imbue(std::locale::classic());
			out << std::fixed << std::setprecision(6)
				<< "{\n  \"schema\":\"surreal-bot-realized-capability-observation-v1\",\n"
				<< "  \"sample\":\"post_spawn_pre_tick\",\n  \"participants\":[";
			bool first = true;
			for (const BotBenchmarkActualParticipant& actual : ActualRoster)
			{
				auto live = std::find_if(liveBots.begin(), liveBots.end(), [&](const auto& item)
				{
					return item.first == actual.Identity;
				});
				if (live == liveBots.end() || !live->second)
					throw std::runtime_error("realized-capability observation is missing a controlled bot");
				UPawn* pawn = live->second;
				const char* requiredProperties[] = {
					"GroundSpeed", "WaterSpeed", "AirSpeed", "JumpZ", "MaxStepHeight", "AccelRate",
					"bCanWalk", "bCanJump", "bCanSwim", "bCanFly", "bCanOpenDoors", "bCanDoSpecial",
				};
				for (const char* property : requiredProperties)
				{
					if (!pawn->HasProperty(property))
						throw std::runtime_error(std::string("realized-capability bot is missing property: ") + property);
				}
				if (!first)
					out << ',';
				first = false;
				out << "\n    {\"identity\":" << JsonString(actual.Identity)
					<< ",\"actor\":" << JsonString(actual.Actor)
					<< ",\"class\":" << JsonString(actual.ClassName)
					<< ",\"movement\":{\"ground_speed\":" << pawn->GroundSpeed()
					<< ",\"water_speed\":" << pawn->WaterSpeed()
					<< ",\"air_speed\":" << pawn->AirSpeed()
					<< ",\"jump_z\":" << pawn->JumpZ()
					<< ",\"max_step_height\":" << pawn->MaxStepHeight()
					<< ",\"accel_rate\":" << pawn->AccelRate()
					<< "},\"capabilities\":{\"walk\":" << (pawn->bCanWalk() ? "true" : "false")
					<< ",\"jump\":" << (pawn->bCanJump() ? "true" : "false")
					<< ",\"swim\":" << (pawn->bCanSwim() ? "true" : "false")
					<< ",\"fly\":" << (pawn->bCanFly() ? "true" : "false")
					<< ",\"open_doors\":" << (pawn->bCanOpenDoors() ? "true" : "false")
					<< ",\"special\":" << (pawn->bCanDoSpecial() ? "true" : "false") << "}}";
			}
			out << "\n  ]\n}\n";
			std::filesystem::create_directories(Config.GetOutputDirectory());
			File::write_all_text((std::filesystem::path(Config.GetOutputDirectory())
				/ "bot-realized-capabilities.json").string(), out.str());
		}

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
			uint64_t DamageTakenExact = 0;
			uint64_t DamageTakenFromOtherParticipantsExact = 0;
			uint64_t DamageTakenFromSelfExact = 0;
			uint64_t DamageTakenFromNonParticipantsExact = 0;
			uint64_t DamageDealtToOtherParticipantsExact = 0;
			uint64_t ConfirmedPickupsExact = 0;
			uint64_t ConfirmedWeaponPickupsExact = 0;
			uint64_t ConfirmedAmmoPickupsExact = 0;
			uint64_t ConfirmedHealthPickupsExact = 0;
			uint64_t ConfirmedArmorPickupsExact = 0;
			uint64_t ConfirmedOtherPickupsExact = 0;
			uint64_t PickupSourceConsumedUnconfirmedExact = 0;
			std::unique_ptr<BotBenchmarkQualityObservation::NavigationCoverageAccumulator>
				NavigationCoverage;
			BotBenchmarkDriverDetail::DeathAttributionCounters DeathAttribution;
			uint64_t HitWallEventsExact = 0;
			BotBenchmarkDriverDetail::NativePawnCounterEpoch NativeCounterEpoch;
			BotBenchmarkDriverDetail::NativePawnCounters NativeCounterTotals;
			std::vector<PawnMovement::WalkingStepPreflightDiagnosticRecord>
				PendingWalkingStepPreflightDiagnostics;
			std::vector<PawnMovement::InventoryDirectReachSupportDiagnosticRecord>
				PendingInventoryDirectReachSupportDiagnostics;
			std::vector<BotBenchmarkDirectReachCommandRecord>
				PendingDirectReachCommandRecords;
			std::vector<BotBenchmarkDirectReachCommandRecord>
				OpenDirectReachCommandRecords;
			std::vector<PawnMovement::DirectReachCommandObservation>
				PendingDirectReachCommandObservations;
			std::vector<PawnMovement::WalkingHitWallDispatchDiagnosticRecord>
				PendingWalkingHitWallDispatchDiagnostics;
			std::vector<PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord>
				PendingWalkingStepPreflightPositiveDpsVetoActions;
			std::vector<PawnMovement::FallingParityRealizedRecord>
				PendingFallingParityRealizedRecords;
			std::vector<PawnMovement::FallingHazardDiagnosticRecord>
				PendingFallingHazardDiagnostics;
			std::vector<PawnMovement::HazardWaterEgressDiagnosticRecord>
				PendingHazardWaterEgressDiagnostics;
			std::vector<PawnMoveStallRecoveryEpisodeRecord>
				PendingMoveStallRecoveryEpisodeRecords;
			std::vector<PawnMoveStallRecoveryDecisionRecord>
				PendingMoveStallRecoveryDecisionRecords;
			std::vector<PawnMovement::RoutePathCommitRecord>
				PendingRoutePathCommitRecords;
			uint64_t RoutePathCommitOverflowExact = 0;
			std::optional<BotBenchmarkHazardDeathPartitionRecord>
				StagedHazardDeathPartitionRecord;
			std::optional<BotBenchmarkDeathAttribution::ScopeToken>
				StagedHazardDeathPartitionToken;
			std::vector<BotBenchmarkHazardDeathPartitionRecord>
				PendingHazardDeathPartitionRecords;
			uint64_t NextHazardDeathPartitionSequence = 1;
			BotTargetSelectionProbe::Tracker TargetSelectionTracker;
			size_t CapturedTargetSelectionRecords = 0;
			std::vector<BotBenchmarkTargetSelectionRecord> PendingTargetSelectionRecords;
			uint64_t TargetSelectionInvalidIdentifierExact = 0;
			uint64_t TargetSelectionTrackerCapacityExceededExact = 0;
			uint64_t TargetSelectionIntegrityFailuresExact = 0;
			uint64_t NextTargetSelectionSequence = 1;
			uint64_t NextDirectReachCommandSequence = 1;
			uint64_t DirectReachCommandObservationsExact = 0;
			uint64_t DirectReachCommandSuccessesExact = 0;
			uint64_t DirectReachCommandFailuresExact = 0;
			uint64_t DirectReachCommandSameLifeExactExact = 0;
			uint64_t DirectReachCommandUnlinkedExact = 0;
			uint64_t DirectReachCommandOverflowsExact = 0;
			uint64_t DirectReachCommandHazardousDeathsExact = 0;
			uint64_t DirectReachCommandNonhazardDeathsExact = 0;
			uint64_t DirectReachCommandClearedExact = 0;
			uint64_t DirectReachCommandLifeBoundaryCensoredExact = 0;
			uint64_t DirectReachCommandRunEndCensoredExact = 0;
			uint64_t DirectReachCommandCommandReplacedExact = 0;
		};

		struct ActiveTargetSelectionCall
		{
			UFunction* Function = nullptr;
			UObject* Instance = nullptr;
			std::string BotIdentity;
			BotTargetSelectionProbe::ScopeToken Token;
		};

		enum class AttributionScopeKind
		{
			TakeDamage,
			EnvironmentalSource,
			Killed,
		};

		struct ActiveAttributionCall
		{
			UFunction* Function = nullptr;
			UObject* Instance = nullptr;
			std::string VictimIdentity;
			BotBenchmarkDeathAttribution::ScopeToken Token;
			AttributionScopeKind Kind = AttributionScopeKind::TakeDamage;
			int PreHealth = 0;
			bool CanonicalDamageFrame = false;
			std::string InstigatorIdentity;
			bool OutermostKilled = false;
		};

		enum class PickupCategory
		{
			Weapon,
			Ammo,
			Health,
			Armor,
			Other,
		};

		struct ActivePickupTouch
		{
			UInventory* Source = nullptr;
			std::string CollectorIdentity;
			PickupCategory Category = PickupCategory::Other;
			bool SourceWasUnowned = false;
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
			IsUnrealTournamentProfile = EngineRef.packages->IsUnrealTournament();
		}

		static std::string PawnIdentity(UPawn* pawn)
		{
			if (!pawn)
				return {};
			if (UPlayerReplicationInfo* pri = pawn->PlayerReplicationInfo())
				return "pri:" + std::to_string(pri->PlayerID());
			return "actor:" + pawn->Name.ToString();
		}

		static std::string ActorIdentity(UActor* actor)
		{
			if (!actor)
				return {};
			if (UPawn* pawn = UObject::TryCast<UPawn>(actor))
				return PawnIdentity(pawn);
			return "actor:" + actor->Name.ToString();
		}

		static std::string PhysicsModeName(uint8_t physics)
		{
			switch (static_cast<EPhysics>(physics))
			{
			case PHYS_None: return "None";
			case PHYS_Walking: return "Walking";
			case PHYS_Falling: return "Falling";
			case PHYS_Swimming: return "Swimming";
			case PHYS_Flying: return "Flying";
			case PHYS_Rotating: return "Rotating";
			case PHYS_Projectile: return "Projectile";
			case PHYS_Rolling: return "Rolling";
			case PHYS_Interpolating: return "Interpolating";
			case PHYS_MovingBrush: return "MovingBrush";
			case PHYS_Spider: return "Spider";
			case PHYS_Trailer: return "Trailer";
			default: return "Unknown";
			}
		}

		static std::string LatentActionName(LatentRunState action)
		{
			switch (action)
			{
			case LatentRunState::Continue: return "Continue";
			case LatentRunState::Stop: return "Stop";
			case LatentRunState::Sleep: return "Sleep";
			case LatentRunState::FinishAnim: return "FinishAnim";
			case LatentRunState::FinishInterpolation: return "FinishInterpolation";
			case LatentRunState::MoveTo: return "MoveTo";
			case LatentRunState::MoveToward: return "MoveToward";
			case LatentRunState::StrafeTo: return "StrafeTo";
			case LatentRunState::StrafeFacing: return "StrafeFacing";
			case LatentRunState::TurnTo: return "TurnTo";
			case LatentRunState::TurnToward: return "TurnToward";
			case LatentRunState::WaitForLanding: return "WaitForLanding";
			default: return "Unknown";
			}
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

		static UFunction* FindClassFunction(UClass* cls, const NameString& name)
		{
			for (UClass* current = cls; current;
				current = UObject::TryCast<UClass>(current->BaseStruct))
			{
				if (UFunction* function = current->GetFunction(name))
					return function;
			}
			return nullptr;
		}

		static BotBenchmarkDeathAttribution::HookValueKind PropertyKind(UProperty* property)
		{
			using BotBenchmarkDeathAttribution::HookValueKind;
			if (!property)
				return HookValueKind::Unknown;
			switch (property->ValueType)
			{
			case ExpressionValueType::ValueInt: return HookValueKind::Integer;
			case ExpressionValueType::ValueObject: return HookValueKind::Object;
			case ExpressionValueType::ValueVector: return HookValueKind::Vector;
			case ExpressionValueType::ValueName: return HookValueKind::Name;
			case ExpressionValueType::ValueBool: return HookValueKind::Boolean;
			default: return HookValueKind::Unknown;
			}
		}

		static BotBenchmarkDeathAttribution::HookSignatureShape SignatureShape(UFunction* function)
		{
			using namespace BotBenchmarkDeathAttribution;
			HookSignatureShape signature;
			if (!function)
				return signature;
			signature.FunctionName = function->Name.ToString();
			for (UField* field = function->Children; field; field = field->Next)
			{
				UProperty* property = UObject::TryCast<UProperty>(field);
				if (!property || !AllFlags(property->PropFlags, PropertyFlags::Parm))
					continue;
				HookParameterShape parameter;
				parameter.Name = property->Name.ToString();
				parameter.Kind = PropertyKind(property);
				if (auto objectProperty = UObject::TryCast<UObjectProperty>(property);
					objectProperty && objectProperty->ObjectClass)
					parameter.ObjectClass = objectProperty->ObjectClass->Name.ToString();
				if (AllFlags(property->PropFlags, PropertyFlags::ReturnParm))
					signature.ReturnValue = std::move(parameter);
				else
					signature.Parameters.push_back(std::move(parameter));
			}
			return signature;
		}

		static BotTargetSelectionHookContract::ValueKind TargetSelectionPropertyKind(UProperty* property)
		{
			using BotTargetSelectionHookContract::ValueKind;
			if (!property)
				return ValueKind::Unknown;
			switch (property->ValueType)
			{
			case ExpressionValueType::ValueObject: return ValueKind::Object;
			case ExpressionValueType::ValueBool: return ValueKind::Boolean;
			default: return ValueKind::Unknown;
			}
		}

		static BotTargetSelectionHookContract::SignatureShape TargetSelectionSignatureShape(
			UFunction* function)
		{
			using namespace BotTargetSelectionHookContract;
			BotTargetSelectionHookContract::SignatureShape signature;
			if (!function)
				return signature;
			signature.FunctionName = function->Name.ToString();
			for (UField* field = function->Children; field; field = field->Next)
			{
				UProperty* property = UObject::TryCast<UProperty>(field);
				if (!property || !AllFlags(property->PropFlags, PropertyFlags::Parm))
					continue;
				ParameterShape parameter;
				parameter.Name = property->Name.ToString();
				parameter.Kind = TargetSelectionPropertyKind(property);
				if (auto objectProperty = UObject::TryCast<UObjectProperty>(property);
					objectProperty && objectProperty->ObjectClass)
				{
					parameter.ObjectClass = objectProperty->ObjectClass->Name.ToString();
				}
				if (AllFlags(property->PropFlags, PropertyFlags::ReturnParm))
					signature.ReturnValue = std::move(parameter);
				else
					signature.Parameters.push_back(std::move(parameter));
			}
			return signature;
		}

		static std::string TargetSelectionOutcomeName(
			BotTargetSelectionProbe::AcquisitionOutcome outcome)
		{
			switch (outcome)
			{
			case BotTargetSelectionProbe::AcquisitionOutcome::AcceptedTargetChange:
				return "accepted_target_change";
			case BotTargetSelectionProbe::AcquisitionOutcome::AcceptedSameTarget:
				return "accepted_same_target";
			case BotTargetSelectionProbe::AcquisitionOutcome::RejectedOrUnchanged:
				return "rejected_or_unchanged";
			}
			return "unknown";
		}

		bool ValidateAttributionFunction(UFunction* function,
			BotBenchmarkDeathAttribution::HookPoint point)
		{
			if (!function)
			{
				Fail("death attribution hook function is unavailable");
				return false;
			}
			if (ValidatedAttributionFunctions.count(function) != 0)
				return true;
			const std::string error = BotBenchmarkDeathAttribution::ValidateHookContract(
				point, SignatureShape(function));
			if (!error.empty())
			{
				Fail("death attribution hook contract mismatch: " + error);
				return false;
			}
			ValidatedAttributionFunctions.insert(function);
			return true;
		}

		bool ValidateAttributionHookContracts()
		{
			using BotBenchmarkDeathAttribution::HookPoint;
			Package* enginePackage = EngineRef.packages->GetPackage("Engine");
			if (!enginePackage)
			{
				Fail("death attribution requires the Engine package");
				return false;
			}
			UClass* pawnClass = enginePackage->GetClass("Pawn");
			UClass* actorClass = enginePackage->GetClass("Actor");
			UClass* gameInfoClass = enginePackage->GetClass("GameInfo");
			UClass* moverClass = enginePackage->GetClass("Mover");
			CanonicalTakeDamageFunction = FindClassFunction(pawnClass, "TakeDamage");
			CanonicalAddVelocityFunction = FindClassFunction(pawnClass, "AddVelocity");
			if (!ValidateAttributionFunction(CanonicalTakeDamageFunction, HookPoint::TakeDamage) ||
				!ValidateAttributionFunction(CanonicalAddVelocityFunction, HookPoint::AddVelocity) ||
				!ValidateAttributionFunction(FindClassFunction(gameInfoClass, "Killed"), HookPoint::Killed) ||
				!ValidateAttributionFunction(FindClassFunction(pawnClass, "PainTimer"), HookPoint::PainTimer) ||
				!ValidateAttributionFunction(FindClassFunction(actorClass, "FellOutOfWorld"), HookPoint::FellOutOfWorld) ||
				!ValidateAttributionFunction(FindClassFunction(moverClass, "EncroachingOn"), HookPoint::MoverEncroachingOn) ||
				!ValidateAttributionFunction(FindClassFunction(pawnClass, "Landed"), HookPoint::Landed))
				return false;
			if (IsUnrealTournamentProfile &&
				!ValidateAttributionFunction(FindClassFunction(pawnClass, "TakeFallingDamage"),
					HookPoint::TakeFallingDamage))
				return false;
			if (!EngineRef.GameInfo || !ValidateAttributionFunction(
				FindClassFunction(EngineRef.GameInfo->Class, "Killed"), HookPoint::Killed))
				return false;
			return true;
		}

		double AttributionTimeSeconds()
		{
			if (!EngineRef.LevelInfo || !std::isfinite(EngineRef.LevelInfo->TimeSeconds()) ||
				EngineRef.LevelInfo->TimeSeconds() < 0.0f)
			{
				Fail("death attribution observed an invalid Level.TimeSeconds value");
				return 0.0;
			}
			return EngineRef.LevelInfo->TimeSeconds();
		}

		bool AcceptCoordinatorStatus(BotBenchmarkDeathAttribution::CoordinatorStatus status,
			const char* operation)
		{
			using BotBenchmarkDeathAttribution::CoordinatorStatus;
			if (status == CoordinatorStatus::Accepted || status == CoordinatorStatus::Ignored)
				return true;
			Fail(std::string("death attribution coordinator rejected ") + operation);
			return false;
		}

		void ApplyAttribution(const std::string& victimIdentity,
			const BotBenchmarkDeathAttribution::Decision& decision)
		{
			auto runtime = QualityParticipants.find(victimIdentity);
			if (runtime == QualityParticipants.end())
				return;
			runtime->second.DeathAttribution.Apply(decision);
		}

		static const char* KillerRelationName(BotBenchmarkDeathAttribution::DeathKiller relation)
		{
			using BotBenchmarkDeathAttribution::DeathKiller;
			switch (relation)
			{
			case DeathKiller::SelfPlayer: return "self_player";
			case DeathKiller::EnemyPlayer: return "enemy_player";
			case DeathKiller::NonPlayer: return "non_player";
			default: return "none";
			}
		}

		static const char* AttributionName(BotBenchmarkDeathAttribution::Kind attribution)
		{
			using BotBenchmarkDeathAttribution::Kind;
			switch (attribution)
			{
			case Kind::DirectSelfKill: return "direct_self_kill";
			case Kind::DirectEnemyKill: return "direct_enemy_kill";
			case Kind::UnassistedEnvironmentalDeath: return "unassisted_environmental_death";
			case Kind::RecentEnemyContributedEnvironmentalDeathProxy:
				return "recent_enemy_contributed_environmental_death_proxy";
			default: return "ambiguous_death";
			}
		}

		static const char* EnvironmentalSourceName(
			BotBenchmarkDeathAttribution::EnvironmentalSource source)
		{
			using BotBenchmarkDeathAttribution::EnvironmentalSource;
			switch (source)
			{
			case EnvironmentalSource::PainTimer: return "pain_timer";
			case EnvironmentalSource::FellOutOfWorld: return "fell_out_of_world";
			case EnvironmentalSource::TakeFallingDamage: return "take_falling_damage";
			case EnvironmentalSource::MoverEncroachingOn: return "mover_encroaching_on";
			default: return "landed";
			}
		}

		void StageHazardDeathPartition(const std::string& victimIdentity,
			QualityParticipantRuntime& runtime, UPawn* victim,
			BotBenchmarkDeathAttribution::DeathKiller killerRelation,
			const std::vector<PawnMovement::FallingParityRealizedRecord>& parityRecords,
			const std::vector<PawnMovement::FallingHazardDiagnosticRecord>& hazardDiagnostics,
			const std::vector<PawnMovement::HazardWaterEgressDiagnosticRecord>& waterDiagnostics)
		{
			if (runtime.StagedHazardDeathPartitionRecord)
			{
				Fail("hazard death partition already has a staged terminal witness for " + victimIdentity);
				return;
			}
			BotBenchmarkHazardDeathPartitionRecord record;
			record.SourcePawnActor = victim->Name.ToString();
			record.DeathTimeSeconds = AttributionTimeSeconds();
			record.KillerRelation = KillerRelationName(killerRelation);
			record.MoveTargetKnown = victim->MoveTarget() && !victim->MoveTarget()->bDeleteMe();
			if (record.MoveTargetKnown)
				record.MoveTargetName = victim->MoveTarget()->Name.ToString();
			record.MovementIntent = HasMovementIntent(victim);
			record.PhysicsMode = victim->HasProperty("Physics")
				? PhysicsModeName(victim->Physics()) : std::string();

			const auto water = std::find_if(waterDiagnostics.begin(), waterDiagnostics.end(),
				[](const auto& diagnostic)
				{
					return diagnostic.Terminal ==
						PawnMovement::HazardWaterEgressTerminal::DeathBeforeExit;
				});
			if (water != waterDiagnostics.end())
			{
				record.WaterEgressTerminalKnown = true;
				record.WaterEgressSequence = water->Sequence;
				record.WaterEgressLifeId = water->Entry.LifeId;
				record.WaterEgressEpisodeId = water->Entry.EpisodeId;
			}
			const auto falling = std::find_if(hazardDiagnostics.begin(), hazardDiagnostics.end(),
				[](const auto& diagnostic)
				{
					return diagnostic.Kind == PawnMovement::FallingHazardDiagnosticKind::Terminal
						&& diagnostic.Generation.Terminal == PawnMovement::FallingHazardTerminal::Died;
				});
			if (falling != hazardDiagnostics.end())
			{
				record.FallingHazardTerminalKnown = true;
				record.FallingHazardSequence = falling->Sequence;
				record.FallingHazardLifeId = falling->Generation.Life.Value;
				record.FallingHazardFallEpisodeId = falling->Generation.FallEpisode.Value;
				record.FallingHazardGenerationId = falling->Generation.Generation.Value;
				record.FallingHazardCorrelation = PawnMovement::FallingHazardCorrelationName(
					falling->Correlation);
			}
			const auto parity = std::find_if(parityRecords.begin(), parityRecords.end(),
				[](const auto& record)
				{
					return record.Outcome == PawnMovement::FallingParityRealizedOutcome::Died;
				});
			if (parity != parityRecords.end())
			{
				record.FallingParityTerminalKnown = true;
				record.FallingParityLifeGeneration = parity->Correlation.LifeGeneration;
				record.FallingParityInvocationToken = parity->Correlation.InvocationToken;
				record.FallingParityWalkingIteration = parity->Correlation.WalkingIteration;
			}
			record.HazardPrefix = BotBenchmarkHazardDeathPartition::HazardPrefixName(
				BotBenchmarkHazardDeathPartition::ClassifyPrefix({
					record.WaterEgressTerminalKnown, record.FallingHazardTerminalKnown }));
			runtime.StagedHazardDeathPartitionRecord = std::move(record);
			runtime.StagedHazardDeathPartitionToken.reset();
		}

		bool BindStagedHazardDeathPartition(const std::string& victimIdentity,
			BotBenchmarkDeathAttribution::ScopeToken token)
		{
			auto runtime = QualityParticipants.find(victimIdentity);
			if (runtime == QualityParticipants.end() || !runtime->second.StagedHazardDeathPartitionRecord
				|| runtime->second.StagedHazardDeathPartitionToken || !token)
			{
				Fail("hazard death partition could not bind its staged witness to Killed scope for "
					+ victimIdentity);
				return false;
			}
			runtime->second.StagedHazardDeathPartitionToken = token;
			return true;
		}

		void FinalizeHazardDeathPartition(const std::string& victimIdentity,
			const BotBenchmarkDeathAttribution::Decision& decision,
			const std::optional<BotBenchmarkDeathAttribution::EnvironmentalSource>& source,
			BotBenchmarkDeathAttribution::ScopeToken token)
		{
			auto runtime = QualityParticipants.find(victimIdentity);
			if (runtime == QualityParticipants.end() || !runtime->second.StagedHazardDeathPartitionRecord
				|| !runtime->second.StagedHazardDeathPartitionToken
				|| runtime->second.StagedHazardDeathPartitionToken->Value != token.Value)
			{
				Fail("hazard death partition has no staged terminal witness for " + victimIdentity);
				return;
			}
			auto record = std::move(*runtime->second.StagedHazardDeathPartitionRecord);
			runtime->second.StagedHazardDeathPartitionRecord.reset();
			runtime->second.StagedHazardDeathPartitionToken.reset();
			record.Sequence = runtime->second.NextHazardDeathPartitionSequence++;
			record.Attribution = AttributionName(decision.Attribution);
			record.HadRecentEnemyContribution = decision.HadRecentEnemyContribution;
			record.HadRecentEnemyMomentumContribution = decision.HadRecentEnemyMomentumContribution;
			if (source)
				record.EnvironmentalSource = EnvironmentalSourceName(*source);
			runtime->second.PendingHazardDeathPartitionRecords.push_back(std::move(record));
		}

		void DiscardStagedHazardDeathPartition(const std::string& victimIdentity)
		{
			auto runtime = QualityParticipants.find(victimIdentity);
			if (runtime != QualityParticipants.end())
			{
				runtime->second.StagedHazardDeathPartitionRecord.reset();
				runtime->second.StagedHazardDeathPartitionToken.reset();
			}
		}

		static BotBenchmarkDriverDetail::NativePawnCounters CaptureNativePawnCounters(
			UPawn* pawn)
		{
			BotBenchmarkDriverDetail::NativePawnCounters counters;
			counters.PainLedgeVetoes = pawn->PainLedgeVetoCount();
			counters.PainLedgeRepeatVetoes = pawn->PainLedgeRepeatVetoCount();
			counters.PainLedgeRecoveryAttempts = pawn->PainLedgeRecoveryAttemptCount();
			counters.PainLedgeRecoveryEscapes = pawn->PainLedgeRecoveryEscapeCount();
			counters.WallAdjustCalls = pawn->WallAdjustCallCount();
			counters.WallAdjustRepeats = pawn->WallAdjustRepeatCount();
			counters.WallAdjustRecoveryAttempts = pawn->WallAdjustRecoveryAttemptCount();
			counters.WallAdjustRecoverySuccesses = pawn->WallAdjustRecoverySuccessCount();
			counters.WallAdjustForcedReplans = pawn->WallAdjustForcedReplanCount();
			counters.WalkingHitWallDispatchObservations =
				pawn->WalkingHitWallDispatchObservationCount();
			counters.WalkingHitWallDispatchLegacyZBand =
				pawn->WalkingHitWallDispatchLegacyZBandCount();
			counters.WalkingHitWallDispatchMinHitWall =
				pawn->WalkingHitWallDispatchMinHitWallCount();
			counters.WalkingHitWallDispatchDisagreements =
				pawn->WalkingHitWallDispatchDisagreementCount();
			counters.WalkingHitWallDispatchCallbacks =
				pawn->WalkingHitWallDispatchCallbackCount();
			counters.WalkingHitWallDispatchDiagnosticOverflows =
				pawn->WalkingHitWallDispatchDiagnosticOverflowCount();
			counters.MoveStallDetections = pawn->MoveStallDetectionCount();
			counters.MoveStallEpisodeResets = pawn->MoveStallEpisodeResetCount();
			counters.MoveStallForcedReplans = pawn->MoveStallForcedReplanCount();
			counters.MoveStallNavigationForcedReplans =
				pawn->MoveStallNavigationForcedReplanCount();
			counters.MoveStallTargetlessMoveToTimeouts =
				pawn->MoveStallTargetlessMoveToTimeoutCount();
			counters.MoveStallDirectActorMoveTowardTimeouts =
				pawn->MoveStallDirectActorMoveTowardTimeoutCount();
			counters.MoveStallEligibleSeconds = pawn->MoveStallEligibleSeconds();
			counters.MoveStallRecoveryEpisodes = pawn->MoveStallRecoveryEpisodeStartCount();
			counters.MoveStallRecoveryClearedWithin2Seconds =
				pawn->MoveStallRecoveryClearedWithin2SecondsCount();
			counters.MoveStallRecoveryClearedAfter2SecondsWithin5Seconds =
				pawn->MoveStallRecoveryClearedAfter2SecondsWithin5SecondsCount();
			counters.MoveStallRecoveryReplannedWithin5Seconds =
				pawn->MoveStallRecoveryReplannedWithin5SecondsCount();
			counters.MoveStallRecoveryMissed5SecondDeadline =
				pawn->MoveStallRecoveryMissed5SecondDeadlineCount();
			counters.MoveStallRecoveryExcludedIntentionalStops =
				pawn->MoveStallRecoveryExcludedIntentionalStopCount();
			counters.MoveStallRecoveryCensoredLifeBoundaries =
				pawn->MoveStallRecoveryCensoredLifeBoundaryCount();
			counters.MoveStallRecoveryCensoredRunEnd =
				pawn->MoveStallRecoveryCensoredRunEndCount();
			counters.MoveStallRecoveryUnknown = pawn->MoveStallRecoveryUnknownCount();
			counters.MoveStallRecoveryEpisodeRecordOverflows =
				pawn->MoveStallRecoveryEpisodeRecordOverflowCount();
			counters.MoveStallRecoveryDecisionRecordOverflows =
				pawn->MoveStallRecoveryDecisionRecordOverflowCount();
			counters.FailedNavigationAvoidanceActivations =
				pawn->FailedNavigationAvoidanceActivationCount();
			counters.FailedNavigationSafeguardSuppressions =
				pawn->FailedNavigationSafeguardSuppressionCount();
			counters.FailedNavigationRoutePenaltyApplications =
				pawn->FailedNavigationRoutePenaltyApplicationCount();
			counters.HarmfulZoneEscapeEpisodes = pawn->HarmfulZoneEscapeEpisodeCount();
			counters.HarmfulZoneEscapeCenterEntries = pawn->HarmfulZoneEscapeCenterEntryCount();
			counters.HarmfulZoneEscapeFootEntries = pawn->HarmfulZoneEscapeFootEntryCount();
			counters.HarmfulZoneEscapeRecoveryAttempts =
				pawn->HarmfulZoneEscapeRecoveryAttemptCount();
			counters.HarmfulZoneEscapeSuccessfulEscapes =
				pawn->HarmfulZoneEscapeSuccessfulEscapeCount();
			counters.HarmfulZoneEscapeForcedReplans =
				pawn->HarmfulZoneEscapeForcedReplanCount();
			counters.HarmfulZoneEscapeNoSafeCandidates =
				pawn->HarmfulZoneEscapeNoSafeCandidateCount();
			counters.HazardSwimEgressEpisodes = pawn->HazardSwimEgressEpisodeCount();
			counters.HazardSwimEgressEligible = pawn->HazardSwimEgressEligibleCount();
			counters.HazardSwimEgressAuthorized = pawn->HazardSwimEgressAuthorizedCount();
			counters.HazardSwimEgressDebounced = pawn->HazardSwimEgressDebouncedCount();
			counters.HazardSwimEgressNoAnchorRejected = pawn->HazardSwimEgressNoAnchorRejectedCount();
			counters.HazardSwimEgressExited = pawn->HazardSwimEgressExitCount();
			counters.HazardSwimEgressDeathsBeforeExit = pawn->HazardSwimEgressDeathsBeforeExitCount();
			counters.HazardSwimEgressForcedReplans = pawn->HazardSwimEgressForcedReplanCount();
			counters.HazardSwimEgressForcedReplanSameCommandReissued =
				pawn->HazardSwimEgressForcedReplanSameCommandReissuedCount();
			counters.HazardSwimEgressForcedReplanDifferentCommandIssued =
				pawn->HazardSwimEgressForcedReplanDifferentCommandIssuedCount();
			counters.HazardSwimEgressForcedReplanHazardClearedBeforeCommand =
				pawn->HazardSwimEgressForcedReplanHazardClearedBeforeCommandCount();
			counters.HazardSwimEgressForcedReplanFellBeforeCommand =
				pawn->HazardSwimEgressForcedReplanFellBeforeCommandCount();
			counters.HazardSwimEgressForcedReplanDiedBeforeCommand =
				pawn->HazardSwimEgressForcedReplanDiedBeforeCommandCount();
			counters.HazardSwimEgressForcedReplanLifeBoundaryCensored =
				pawn->HazardSwimEgressForcedReplanLifeBoundaryCensoredCount();
			counters.HazardSwimEgressForcedReplanRunEndCensored =
				pawn->HazardSwimEgressForcedReplanRunEndCensoredCount();
			counters.HazardSwimEgressForcedReplanEpisodeAbandoned =
				pawn->HazardSwimEgressForcedReplanEpisodeAbandonedCount();
			counters.HazardSwimEgressFallingPreMoveAnchorCaptures =
				pawn->HazardSwimEgressFallingPreMoveAnchorCaptureCount();
				counters.HazardSwimEgressFallingPreMoveAnchorUses =
					pawn->HazardSwimEgressFallingPreMoveAnchorUseCount();
				counters.HazardSwimEgressLiveApplies = pawn->HazardSwimEgressLiveApplyCount();
				counters.HazardSwimEgressLiveActiveTicks = pawn->HazardSwimEgressLiveActiveTickCount();
				counters.HazardSwimEgressLiveProbeRejected = pawn->HazardSwimEgressLiveProbeRejectedCount();
				counters.HazardSwimEgressLiveSuccessfulExits = pawn->HazardSwimEgressLiveSuccessfulExitCount();
				counters.HazardSwimEgressLiveShadowCandidates = pawn->HazardSwimEgressLiveShadowCandidateCount();
				counters.HazardSwimEgressLiveShadowFallingTerminals = pawn->HazardSwimEgressLiveShadowFallingTerminalCount();
				counters.HazardSwimEgressLiveShadowHazardClearedTerminals = pawn->HazardSwimEgressLiveShadowHazardClearedTerminalCount();
				counters.HazardSwimEgressLiveShadowProbeBlockedTerminals = pawn->HazardSwimEgressLiveShadowProbeBlockedTerminalCount();
				counters.HazardSwimEgressDirectNavProbes = pawn->HazardSwimEgressDirectNavProbeCount();
			counters.HazardSwimEgressDirectNavSafeCandidates = pawn->HazardSwimEgressDirectNavSafeCandidateCount();
			counters.HazardResidenceEpisodes = pawn->HazardResidenceEpisodeCount();
			counters.HazardResidenceCleared = pawn->HazardResidenceClearedCount();
			counters.HazardResidenceDeaths = pawn->HazardResidenceDeathCount();
			counters.HazardResidenceLifeBoundaryCensored = pawn->HazardResidenceLifeBoundaryCensoredCount();
			counters.HazardResidenceRunEndCensored = pawn->HazardResidenceRunEndCensoredCount();
			counters.HazardResidenceUnknown = pawn->HazardResidenceUnknownCount();
			counters.HazardResidenceReentries = pawn->HazardResidenceReentryCount();
			counters.HazardResidenceCommandChanges = pawn->HazardResidenceCommandChangeCount();
			counters.HazardResidenceCandidatesObserved = pawn->HazardResidenceCandidateObservedCount();
			counters.HazardResidenceCandidateOtherCommands = pawn->HazardResidenceCandidateOtherCommandCount();
				counters.HazardSwimEgressDirectNavBestCandidateName =
					pawn->HazardSwimEgressDirectNavBestCandidateName();
			counters.HazardSwimEgressAnchorKnown = pawn->HasHazardSwimEgressAnchor();
			counters.HazardSwimEgressAnchorSource = pawn->HazardSwimEgressAnchorSourceName();
			counters.FallingHazardRecoveryPromotions = pawn->FallingHazardRecoveryPromotionCount();
			counters.FallingHazardRecoveryAdvanceCalls = pawn->FallingHazardRecoveryAdvanceCount();
			counters.FallingHazardRecoveryContextRejected =
				pawn->FallingHazardRecoveryContextRejectedCount();
			counters.FallingHazardRecoveryNoActiveFallEpisode =
				pawn->FallingHazardRecoveryNoActiveFallEpisodeCount();
			counters.FallingHazardRecoveryNoPrefix = pawn->FallingHazardRecoveryNoPrefixCount();
			counters.FallingHazardRecoveryEligible = pawn->FallingHazardRecoveryEligibleCount();
			counters.FallingHazardRecoveryAnchorRejected =
				pawn->FallingHazardRecoveryAnchorRejectedCount();
			counters.FallingHazardRecoveryProbeRejected =
				pawn->FallingHazardRecoveryProbeRejectedCount();
			counters.FallingHazardRecoveryLiveApplies = pawn->FallingHazardRecoveryLiveApplyCount();
			counters.FallingHazardRecoveryLiveActiveTicks =
				pawn->FallingHazardRecoveryLiveActiveTickCount();
			counters.FallingHazardRecoverySafeLandings = pawn->FallingHazardRecoverySafeLandingCount();
			counters.FallingHazardRecoveryHarmfulEntries =
				pawn->FallingHazardRecoveryHarmfulEntryCount();
			counters.FallingHazardRecoveryDeaths = pawn->FallingHazardRecoveryDeathCount();
			counters.FallingHazardRecoveryTimeouts = pawn->FallingHazardRecoveryTimeoutCount();
			counters.ExternalImpulseFallHarmfulWitnesses = pawn->ExternalImpulseFallHarmfulWitnessCount();
			counters.ExternalImpulseFallNoAirControl = pawn->ExternalImpulseFallNoAirControlCount();
			counters.ExternalImpulseFallAlternativesTested = pawn->ExternalImpulseFallAlternativesTestedCount();
			counters.ExternalImpulseFallCertified = pawn->ExternalImpulseFallCertifiedCount();
			counters.ExternalImpulseFallUncertified = pawn->ExternalImpulseFallUncertifiedCount();
			counters.FallingSeamDetections = pawn->FallingSeamDetectionCount();
			counters.HorizontalCornerCandidateProbes =
				pawn->HorizontalCornerCandidateProbeCount();
			counters.HorizontalCornerAuthorizedEscapes =
				pawn->HorizontalCornerAuthorizedEscapeCount();
			counters.HorizontalCornerTargetProgressRejects =
				pawn->HorizontalCornerTargetProgressRejectCount();
			counters.HorizontalCornerUnknownOrUnsafeSupport =
				pawn->HorizontalCornerUnknownOrUnsafeSupportCount();
			counters.FallingSeamEpisodes = pawn->FallingSeamEpisodeCount();
			counters.FallingSeamInvalidGeometryRejects =
				pawn->FallingSeamInvalidGeometryRejectCount();
			counters.FallingSeamAuthorizableEpisodes =
				pawn->FallingSeamAuthorizableEpisodeCount();
			counters.HorizontalCornerAuthorizedCandidates =
				pawn->HorizontalCornerAuthorizedCandidateCount();
			counters.HorizontalCornerBlockedSweepCandidates =
				pawn->HorizontalCornerBlockedSweepCandidateCount();
			counters.HorizontalCornerNoStaticWalkableSupportCandidates =
				pawn->HorizontalCornerNoStaticWalkableSupportCandidateCount();
			counters.HorizontalCornerPainSupportCandidates =
				pawn->HorizontalCornerPainSupportCandidateCount();
			counters.HorizontalCornerNoActiveMovementIntentOrTargetCandidates =
				pawn->HorizontalCornerNoActiveMovementIntentOrTargetCandidateCount();
			counters.HorizontalCornerTrueTargetRegressionCandidates =
				pawn->HorizontalCornerTrueTargetRegressionCandidateCount();
			counters.HorizontalCornerUnknownEvidenceCandidates =
				pawn->HorizontalCornerUnknownEvidenceCandidateCount();
			counters.WalkingStepPreflightObservations =
				pawn->WalkingStepPreflightObservationCount();
			counters.WalkingStepPreflightUnsupportedEndpoints =
				pawn->WalkingStepPreflightUnsupportedEndpointCount();
			counters.WalkingStepPreflightNoDecisions =
				pawn->WalkingStepPreflightNoDecisionCount();
			counters.WalkingStepPreflightProvisionalAuthorizations =
				pawn->WalkingStepPreflightProvisionalAuthorizationCount();
			counters.WalkingStepPreflightAuthorizations =
				pawn->WalkingStepPreflightAuthorizationCount();
			counters.WalkingStepPreflightAuthorizableEpisodes =
				pawn->WalkingStepPreflightAuthorizableEpisodeCount();
			counters.WalkingStepPreflightPositiveDpsVetoEligible =
				pawn->WalkingStepPreflightPositiveDpsVetoEligibleCount();
			counters.WalkingStepPreflightPositiveDpsVetoApplied =
				pawn->WalkingStepPreflightPositiveDpsVetoAppliedCount();
			counters.WalkingStepPreflightPositiveDpsVetoDebounced =
				pawn->WalkingStepPreflightPositiveDpsVetoDebouncedCount();
			counters.WalkingStepPreflightPositiveDpsVetoForcedReplans =
				pawn->WalkingStepPreflightPositiveDpsVetoForcedReplanCount();
			counters.WalkingStepPreflightPositiveDpsVetoRollbackRejected =
				pawn->WalkingStepPreflightPositiveDpsVetoRollbackRejectedCount();
			counters.WalkingStepPreflightPositiveDpsVetoActionOverflows =
				pawn->WalkingStepPreflightPositiveDpsVetoActionOverflowCount();
			counters.WalkingStepPreflightDiagnosticOverflows =
				pawn->WalkingStepPreflightDiagnosticOverflowCount();
			counters.InventoryDirectReachSupportObservations =
				pawn->InventoryDirectReachSupportObservationCount();
			counters.InventoryDirectReachSupportSafeSupported =
				pawn->InventoryDirectReachSupportSafeSupportedCount();
			counters.InventoryDirectReachSupportSafeUnsupportedNoObservedHazard =
				pawn->InventoryDirectReachSupportSafeUnsupportedNoObservedHazardCount();
			counters.InventoryDirectReachSupportUnsafeHarmfulFootZone =
				pawn->InventoryDirectReachSupportUnsafeHarmfulFootZoneCount();
			counters.InventoryDirectReachSupportUnsafeUnsupportedOverHarmful =
				pawn->InventoryDirectReachSupportUnsafeUnsupportedOverHarmfulCount();
			counters.InventoryDirectReachSupportUnavailable =
				pawn->InventoryDirectReachSupportUnavailableCount();
			counters.InventoryDirectReachSupportDiagnosticOverflows =
				pawn->InventoryDirectReachSupportDiagnosticOverflowCount();
			counters.InventoryMarkerDirectReachRejects =
				pawn->InventoryMarkerDirectReachRejectCount();
			counters.FallingParityRealizedEpisodes = pawn->FallingParityRealizedEpisodeCount();
			counters.FallingParityRealizedSteps = pawn->FallingParityRealizedStepCount();
			counters.FallingParityRealizedMatchedSteps = pawn->FallingParityRealizedMatchedStepCount();
			counters.FallingParityRealizedMatchedLandingSteps =
				pawn->FallingParityRealizedMatchedLandingStepCount();
			counters.FallingParityRealizedMismatches = pawn->FallingParityRealizedMismatchCount();
			counters.FallingParityRealizedUnknowns = pawn->FallingParityRealizedUnknownCount();
			counters.FallingParityRealizedCallbackBarriers = pawn->FallingParityRealizedCallbackBarrierCount();
			counters.FallingParityRealizedPainEntries = pawn->FallingParityRealizedPainEntryCount();
			counters.FallingParityRealizedDeaths = pawn->FallingParityRealizedDeathCount();
			counters.FallingParityRealizedLandings = pawn->FallingParityRealizedLandingCount();
			counters.FallingParityRealizedContinuityLosses = pawn->FallingParityRealizedContinuityLossCount();
			counters.FallingParityRealizedRecordOverflows = pawn->FallingParityRealizedRecordOverflowCount();
			const PawnMovement::FallingHazardRuntimeCounters& hazard =
				pawn->FallingHazardRuntimeCounterValues();
			counters.VerticalPainColumnEpisodesStarted = hazard.EpisodesStarted;
			counters.VerticalPainColumnEpisodesCompleted = hazard.EpisodesCompleted;
			counters.VerticalPainColumnTruePositiveOutcomes =
				hazard.TruePositiveOutcomes;
			counters.VerticalPainColumnFalsePositiveOutcomes =
				hazard.FalsePositiveOutcomes;
			counters.VerticalPainColumnFalseNegativeOutcomes =
				hazard.FalseNegativeOutcomes;
			counters.VerticalPainColumnTrueNegativeOutcomes =
				hazard.TrueNegativeOutcomes;
			counters.VerticalPainColumnAmbiguousOutcomes = hazard.AmbiguousOutcomes;
			counters.VerticalPainColumnUnknownOutcomes = hazard.UnknownOutcomes;
			counters.VerticalPainColumnDiagnosticOverflows =
				hazard.DiagnosticOverflows;
			counters.VerticalPainColumnGenerationCapacityExhaustions =
				hazard.GenerationCapacityExhaustions;
			counters.PersistentHarmfulFallCandidatesStarted =
				hazard.PersistentHarmfulFallCandidatesStarted;
			counters.PersistentHarmfulFallPromotions =
				hazard.PersistentHarmfulFallPromotions;
			counters.PersistentHarmfulFallResets = hazard.PersistentHarmfulFallResets;
			counters.PersistentHarmfulFallConfirmedHarmfulEntries =
				hazard.PersistentHarmfulFallConfirmedHarmfulEntries;
			counters.PersistentHarmfulFallObservedLeadSamples =
				hazard.PersistentHarmfulFallObservedLeadSamples;
			counters.PersistentHarmfulFallObservedLeadMilliseconds =
				hazard.PersistentHarmfulFallObservedLeadMilliseconds;
			counters.SingleHarmfulFallPrefixCandidatesStarted =
				hazard.SingleHarmfulFallPrefixCandidatesStarted;
			counters.SingleHarmfulFallPrefixPromotions =
				hazard.SingleHarmfulFallPrefixPromotions;
			counters.SingleHarmfulFallPrefixResets =
				hazard.SingleHarmfulFallPrefixResets;
			counters.SingleHarmfulFallPrefixConfirmedHarmfulEntries =
				hazard.SingleHarmfulFallPrefixConfirmedHarmfulEntries;
			counters.SingleHarmfulFallPrefixObservedLeadSamples =
				hazard.SingleHarmfulFallPrefixObservedLeadSamples;
			counters.SingleHarmfulFallPrefixObservedLeadMilliseconds =
				hazard.SingleHarmfulFallPrefixObservedLeadMilliseconds;
			counters.DirectHarmfulWaterEntryCandidates =
				hazard.DirectHarmfulWaterEntryCandidates;
			counters.DirectHarmfulWaterEntryConfirmed =
				hazard.DirectHarmfulWaterEntryConfirmed;
			counters.DirectHarmfulWaterEntryConfirmedNoHarm =
				hazard.DirectHarmfulWaterEntryConfirmedNoHarm;
			counters.DirectHarmfulWaterEntryUnresolved =
				hazard.DirectHarmfulWaterEntryUnresolved;
			counters.DirectHarmfulWaterEntryLeadSamples =
				hazard.DirectHarmfulWaterEntryLeadSamples;
			counters.DirectHarmfulWaterEntryLeadMilliseconds =
				hazard.DirectHarmfulWaterEntryLeadMilliseconds;
			counters.DirectHarmfulWaterEntryCertificateResults =
				hazard.DirectHarmfulWaterEntryCertificateResults;
			counters.WalkingStepPreflightReasons = pawn->WalkingStepPreflightReasonCounts();
			return counters;
		}

		void AccumulateNativePawnCounters(const std::string& identity,
			QualityParticipantRuntime& runtime, UPawn* pawn,
			BotBenchmarkDriverDetail::NativePawnCounterSample sample)
		{
			const auto began = runtime.NativeCounterEpoch.BeginPawn(pawn, sample);
			if (began.ResetLifeAttribution)
				AttributionCoordinator.ResetLife(identity);
			runtime.NativeCounterEpoch.Accumulate(CaptureNativePawnCounters(pawn),
				runtime.NativeCounterTotals);
		}

		void AccountDirectReachCommandTerminal(QualityParticipantRuntime& runtime,
			const char* terminal)
		{
			const std::string value = terminal;
			if (value == "hazardous_death")
				runtime.DirectReachCommandHazardousDeathsExact++;
			else if (value == "nonhazard_death")
				runtime.DirectReachCommandNonhazardDeathsExact++;
			else if (value == "cleared")
				runtime.DirectReachCommandClearedExact++;
			else if (value == "life_boundary_censor")
				runtime.DirectReachCommandLifeBoundaryCensoredExact++;
			else if (value == "run_end_censor")
				runtime.DirectReachCommandRunEndCensoredExact++;
			else if (value == "command_replaced")
				runtime.DirectReachCommandCommandReplacedExact++;
			else
				Fail(std::string("invalid direct-reach terminal: ") + value);
		}

		void ResolveOpenDirectReachCommands(QualityParticipantRuntime& runtime,
			uint64_t lifeId, const char* terminal, bool hazardTerminalExact,
			uint64_t terminalTick)
		{
			for (auto it = runtime.OpenDirectReachCommandRecords.begin();
				it != runtime.OpenDirectReachCommandRecords.end();)
			{
				if (it->LifeId != lifeId)
				{
					++it;
					continue;
				}
				it->TerminalTick = terminalTick;
				it->Terminal = terminal;
				it->HazardTerminalExact = hazardTerminalExact;
				it->Sequence = runtime.NextDirectReachCommandSequence++;
				runtime.PendingDirectReachCommandRecords.push_back(std::move(*it));
				AccountDirectReachCommandTerminal(runtime, terminal);
				it = runtime.OpenDirectReachCommandRecords.erase(it);
			}
		}

		void CensorStaleOpenDirectReachCommands(QualityParticipantRuntime& runtime,
			uint64_t liveLifeId, uint64_t terminalTick)
		{
			for (auto it = runtime.OpenDirectReachCommandRecords.begin();
				it != runtime.OpenDirectReachCommandRecords.end();)
			{
				if (it->LifeId == liveLifeId)
				{
					++it;
					continue;
				}
				it->TerminalTick = terminalTick;
				it->Terminal = "life_boundary_censor";
				it->HazardTerminalExact = false;
				it->Sequence = runtime.NextDirectReachCommandSequence++;
				runtime.PendingDirectReachCommandRecords.push_back(std::move(*it));
				AccountDirectReachCommandTerminal(runtime, "life_boundary_censor");
				it = runtime.OpenDirectReachCommandRecords.erase(it);
			}
		}

		void CensorAllOpenDirectReachCommands(QualityParticipantRuntime& runtime,
			const char* terminal, uint64_t terminalTick)
		{
			for (auto& record : runtime.OpenDirectReachCommandRecords)
			{
				record.TerminalTick = terminalTick;
				record.Terminal = terminal;
				record.HazardTerminalExact = false;
				record.Sequence = runtime.NextDirectReachCommandSequence++;
				runtime.PendingDirectReachCommandRecords.push_back(std::move(record));
				AccountDirectReachCommandTerminal(runtime, terminal);
			}
			runtime.OpenDirectReachCommandRecords.clear();
		}

		void CloseReplacedDirectReachCommands(QualityParticipantRuntime& runtime,
			UPawn* pawn, const BotBenchmarkBotState& state, bool routeHeadPresent,
			uint64_t terminalTick)
		{
			const bool activeDirectCommand = state.LatentAction == "MoveTo"
				|| state.LatentAction == "MoveToward";
			UActor* moveTarget = pawn->MoveTarget();
			for (auto it = runtime.OpenDirectReachCommandRecords.begin();
				it != runtime.OpenDirectReachCommandRecords.end();)
			{
				const bool targetMatches = moveTarget && !moveTarget->bDeleteMe()
					&& moveTarget->Index == it->TargetActorIndex
					&& moveTarget == it->TargetAddress;
				if (it->LifeId == pawn->DirectReachCommandLifeId() && activeDirectCommand
					&& !routeHeadPresent && targetMatches)
				{
					++it;
					continue;
				}
				it->TerminalTick = terminalTick;
				it->Terminal = "command_replaced";
				it->HazardTerminalExact = false;
				it->Sequence = runtime.NextDirectReachCommandSequence++;
				runtime.PendingDirectReachCommandRecords.push_back(std::move(*it));
				AccountDirectReachCommandTerminal(runtime, "command_replaced");
				it = runtime.OpenDirectReachCommandRecords.erase(it);
			}
		}

		void RecordKilled(UPawn* killer, UPawn* victim)
		{
			const std::string victimIdentity = PawnIdentity(victim);
			auto victimRuntime = QualityParticipants.find(victimIdentity);
			if (victimRuntime != QualityParticipants.end())
			{
				QualityParticipantRuntime& counters = victimRuntime->second;
				victim->RecordHazardSwimEgressDeath();
				const bool directReachHazardTerminalExact = victim->RecordHazardResidenceDeath();
				ResolveOpenDirectReachCommands(counters, victim->DirectReachCommandLifeId(),
					directReachHazardTerminalExact ? "hazardous_death" : "nonhazard_death",
					directReachHazardTerminalExact, Ticks + 1);
				victim->FinishFallingHazardDeath();
				victim->FinishFallingParityRealizedTrace(
					PawnMovement::FallingParityRealizedOutcome::Died);
				auto diagnostics = victim->DrainWalkingStepPreflightDiagnostics();
				auto walkingHitWallDiagnostics =
					victim->DrainWalkingHitWallDispatchDiagnostics();
				auto vetoActions = victim->DrainWalkingStepPreflightPositiveDpsVetoActions();
				auto parityRecords = victim->DrainFallingParityRealizedRecords();
				auto hazardDiagnostics = victim->DrainFallingHazardDiagnostics();
				auto waterEgressDiagnostics = victim->DrainHazardWaterEgressDiagnostics();
				auto routePathCommitRecords = victim->DrainRoutePathCommitRecords();
				auto directReachCommandObservations = victim->DrainDirectReachCommandObservations();
				counters.DirectReachCommandOverflowsExact = std::max(
					counters.DirectReachCommandOverflowsExact,
					victim->DirectReachCommandOverflowCount());
				counters.RoutePathCommitOverflowExact = std::max(
					counters.RoutePathCommitOverflowExact, victim->RoutePathCommitOverflowCount());
				BotBenchmarkDeathAttribution::DeathKiller killerRelation =
					BotBenchmarkDeathAttribution::DeathKiller::None;
				if (killer)
				{
					killerRelation = !killer->bIsPlayer()
						? BotBenchmarkDeathAttribution::DeathKiller::NonPlayer
						: killer == victim ? BotBenchmarkDeathAttribution::DeathKiller::SelfPlayer
						: BotBenchmarkDeathAttribution::DeathKiller::EnemyPlayer;
				}
				StageHazardDeathPartition(victimIdentity, counters, victim, killerRelation,
					parityRecords, hazardDiagnostics, waterEgressDiagnostics);
				victim->EndWalkingStepPreflightLife();
				AccumulateNativePawnCounters(victimIdentity, counters, victim,
					BotBenchmarkDriverDetail::NativePawnCounterSample::DeathFlush);
				auto moveStallRecoveryRecords = victim->DrainMoveStallRecoveryEpisodeRecords();
				auto moveStallDecisionRecords = victim->DrainMoveStallRecoveryDecisionRecords();
				counters.PendingWalkingStepPreflightDiagnostics.insert(
					counters.PendingWalkingStepPreflightDiagnostics.end(),
					std::make_move_iterator(diagnostics.begin()),
					std::make_move_iterator(diagnostics.end()));
				counters.PendingWalkingHitWallDispatchDiagnostics.insert(
					counters.PendingWalkingHitWallDispatchDiagnostics.end(),
					std::make_move_iterator(walkingHitWallDiagnostics.begin()),
					std::make_move_iterator(walkingHitWallDiagnostics.end()));
				counters.PendingWalkingStepPreflightPositiveDpsVetoActions.insert(
					counters.PendingWalkingStepPreflightPositiveDpsVetoActions.end(),
					std::make_move_iterator(vetoActions.begin()),
					std::make_move_iterator(vetoActions.end()));
				counters.PendingFallingParityRealizedRecords.insert(
					counters.PendingFallingParityRealizedRecords.end(),
					std::make_move_iterator(parityRecords.begin()),
					std::make_move_iterator(parityRecords.end()));
				counters.PendingFallingHazardDiagnostics.insert(
					counters.PendingFallingHazardDiagnostics.end(),
					std::make_move_iterator(hazardDiagnostics.begin()),
					std::make_move_iterator(hazardDiagnostics.end()));
				counters.PendingHazardWaterEgressDiagnostics.insert(
					counters.PendingHazardWaterEgressDiagnostics.end(),
					std::make_move_iterator(waterEgressDiagnostics.begin()),
					std::make_move_iterator(waterEgressDiagnostics.end()));
				counters.PendingMoveStallRecoveryEpisodeRecords.insert(
					counters.PendingMoveStallRecoveryEpisodeRecords.end(),
					std::make_move_iterator(moveStallRecoveryRecords.begin()),
					std::make_move_iterator(moveStallRecoveryRecords.end()));
				counters.PendingMoveStallRecoveryDecisionRecords.insert(
					counters.PendingMoveStallRecoveryDecisionRecords.end(),
					std::make_move_iterator(moveStallDecisionRecords.begin()),
					std::make_move_iterator(moveStallDecisionRecords.end()));
				counters.PendingRoutePathCommitRecords.insert(
					counters.PendingRoutePathCommitRecords.end(),
					std::make_move_iterator(routePathCommitRecords.begin()),
					std::make_move_iterator(routePathCommitRecords.end()));
				for (const auto& observation : directReachCommandObservations)
				{
					BotBenchmarkDirectReachCommandRecord record{
						counters.NextDirectReachCommandSequence++, observation.LifeId,
						observation.TargetActorIndex, observation.TargetAddress,
						observation.TargetName,
						observation.TargetClass, observation.Reached, observation.CheckNavpoint,
						observation.ResolvedWallSlide, observation.WalkingSimulationIterations,
						std::string(), false, "unavailable_life_boundary" };
					record.ReachSequence = observation.Sequence;
					record.NativeTick = observation.NativeTick;
					record.CallerOrigin = PawnMovement::DirectReachCommandCallerOriginName(
						observation.CallerOrigin);
					record.RejectReason = PawnMovement::DirectReachCommandRejectReasonName(
						observation.RejectReason);
					counters.PendingDirectReachCommandRecords.push_back(std::move(record));
					counters.DirectReachCommandObservationsExact++;
					if (observation.Reached)
						counters.DirectReachCommandSuccessesExact++;
					else
						counters.DirectReachCommandFailuresExact++;
					counters.DirectReachCommandUnlinkedExact++;
				}
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

		void FinishAttributionCall(UFunction* function, UObject* instance,
			AttributionScopeKind expectedKind)
		{
			if (ActiveAttributionCalls.empty())
			{
				Fail("death attribution hook cleanup had no active call");
				return;
			}
			const ActiveAttributionCall call = ActiveAttributionCalls.back();
			if (call.Function != function || call.Instance != instance || call.Kind != expectedKind)
			{
				Fail("death attribution hook cleanup was out of order");
				return;
			}
			ActiveAttributionCalls.pop_back();
			if (expectedKind == AttributionScopeKind::TakeDamage)
				RecordCanonicalDamage(call);
			using namespace BotBenchmarkDeathAttribution;
			CoordinatorStatus status = CoordinatorStatus::InvalidToken;
			switch (expectedKind)
			{
			case AttributionScopeKind::TakeDamage:
				status = AttributionCoordinator.ExitTakeDamage(call.Token);
				break;
			case AttributionScopeKind::EnvironmentalSource:
				status = AttributionCoordinator.ExitEnvironmentalSource(call.Token);
				break;
			case AttributionScopeKind::Killed:
				status = AttributionCoordinator.ExitKilled(call.Token);
				if (call.OutermostKilled)
				{
					auto runtime = QualityParticipants.find(call.VictimIdentity);
					if (runtime != QualityParticipants.end()
						&& (runtime->second.StagedHazardDeathPartitionRecord
							|| runtime->second.StagedHazardDeathPartitionToken))
					{
						Fail("hazard death partition did not finalize the Killed attribution scope for "
							+ call.VictimIdentity);
						DiscardStagedHazardDeathPartition(call.VictimIdentity);
					}
				}
				break;
			}
			AcceptCoordinatorStatus(status, "hook cleanup");
		}

		bool AddQualityCounter(uint64_t& counter, uint64_t amount,
			const char* name)
		{
			if (amount > std::numeric_limits<uint64_t>::max() - counter)
			{
				Fail(std::string("bot quality telemetry counter overflow: ") + name);
				return false;
			}
			counter += amount;
			return true;
		}

		bool AddDamageCounter(uint64_t& counter, uint64_t amount, const char* name)
		{
			return AddQualityCounter(counter, amount, name);
		}

		static PickupCategory ClassifyPickupCategory(UInventory* source)
		{
			if (UObject::TryCast<UWeapon>(source))
				return PickupCategory::Weapon;
			if (source && source->IsA("Ammo"))
				return PickupCategory::Ammo;
			if (source && source->IsA("Health"))
				return PickupCategory::Health;
			if (source && source->bIsAnArmor())
				return PickupCategory::Armor;
			return PickupCategory::Other;
		}

		void RecordPickupTouch(const ActivePickupTouch& touch)
		{
			auto collector = QualityParticipants.find(touch.CollectorIdentity);
			if (collector == QualityParticipants.end() || !touch.Source)
				return;
			using namespace BotBenchmarkQualityObservation;
			const PickupTransitionEvidence evidence = ClassifyPickupTransition({
				touch.SourceWasUnowned,
				touch.Source->Owner() && PawnIdentity(UObject::TryCast<UPawn>(touch.Source->Owner()))
					== touch.CollectorIdentity,
				touch.Source->bDeleteMe()
			});
			if (evidence == PickupTransitionEvidence::SourceConsumedUnconfirmed)
			{
				AddQualityCounter(collector->second.PickupSourceConsumedUnconfirmedExact, 1,
					"pickup_source_consumed_unconfirmed_exact");
				return;
			}
			if (!IsConfirmedPickupAcquisition(evidence))
				return;
			if (!AddQualityCounter(collector->second.ConfirmedPickupsExact, 1,
				"confirmed_pickups_exact"))
				return;
			uint64_t* categoryCounter = &collector->second.ConfirmedOtherPickupsExact;
			const char* categoryName = "confirmed_other_pickups_exact";
			switch (touch.Category)
			{
			case PickupCategory::Weapon:
				categoryCounter = &collector->second.ConfirmedWeaponPickupsExact;
				categoryName = "confirmed_weapon_pickups_exact";
				break;
			case PickupCategory::Ammo:
				categoryCounter = &collector->second.ConfirmedAmmoPickupsExact;
				categoryName = "confirmed_ammo_pickups_exact";
				break;
			case PickupCategory::Health:
				categoryCounter = &collector->second.ConfirmedHealthPickupsExact;
				categoryName = "confirmed_health_pickups_exact";
				break;
			case PickupCategory::Armor:
				categoryCounter = &collector->second.ConfirmedArmorPickupsExact;
				categoryName = "confirmed_armor_pickups_exact";
				break;
			case PickupCategory::Other:
				break;
			}
			AddQualityCounter(*categoryCounter, 1, categoryName);
		}

		void FinishPickupTouch(UInventory* source, const ActivePickupTouch& touch)
		{
			auto depth = PickupTouchDepths.find(source);
			if (depth == PickupTouchDepths.end() || depth->second == 0)
			{
				Fail("pickup telemetry Touch cleanup had no active source");
				return;
			}
			if (--depth->second != 0)
				return;
			PickupTouchDepths.erase(depth);
			if (touch.SourceWasUnowned)
				RecordPickupTouch(touch);
		}

		void RecordCanonicalDamage(const ActiveAttributionCall& call)
		{
			if (!call.CanonicalDamageFrame || call.PreHealth <= 0)
				return;
			UPawn* victim = UObject::TryCast<UPawn>(call.Instance);
			if (!victim || victim->Health() >= call.PreHealth)
				return;
			const uint64_t amount = static_cast<uint64_t>(
				static_cast<int64_t>(call.PreHealth) - victim->Health());
			auto victimRuntime = QualityParticipants.find(call.VictimIdentity);
			if (victimRuntime == QualityParticipants.end())
			{
				Fail("canonical damage lost its benchmark victim");
				return;
			}
			if (!AddDamageCounter(victimRuntime->second.DamageTakenExact, amount,
				"damage_taken_exact"))
				return;
			auto instigatorRuntime = QualityParticipants.find(call.InstigatorIdentity);
			if (instigatorRuntime == QualityParticipants.end())
			{
				AddDamageCounter(victimRuntime->second.DamageTakenFromNonParticipantsExact,
					amount, "damage_taken_from_nonparticipants_exact");
				return;
			}
			if (call.InstigatorIdentity == call.VictimIdentity)
			{
				AddDamageCounter(victimRuntime->second.DamageTakenFromSelfExact, amount,
					"damage_taken_from_self_exact");
				return;
			}
			if (!AddDamageCounter(victimRuntime->second.DamageTakenFromOtherParticipantsExact,
				amount, "damage_taken_from_other_participants_exact"))
				return;
			AddDamageCounter(instigatorRuntime->second.DamageDealtToOtherParticipantsExact,
				amount, "damage_dealt_to_other_participants_exact");
		}

		bool BeginKilledCall(const std::string& victimIdentity)
		{
			return KilledHookDepths[victimIdentity]++ == 0;
		}

		void FinishKilledCall(const std::string& victimIdentity)
		{
			auto depth = KilledHookDepths.find(victimIdentity);
			if (depth == KilledHookDepths.end() || depth->second == 0)
			{
				Fail("death attribution Killed depth cleanup had no active victim");
				return;
			}
			if (--depth->second == 0)
				KilledHookDepths.erase(depth);
		}

		void RegisterQualityHooks()
		{
			using namespace BotBenchmarkDeathAttribution;
			if (!ValidateAttributionHookContracts())
				return;
			VMCallHook hook;
			hook.Enter = [this](UFunction* function, UObject* instance,
				VMCallArguments& arguments) -> VMCallHookCleanup
			{
				using namespace BotBenchmarkDeathAttribution;
				if (!function)
					return {};
				if (function->Name == "Killed" && instance == EngineRef.GameInfo)
				{
					if (!ValidateAttributionFunction(function, HookPoint::Killed))
						return {};
					if (arguments.Size() != 3)
					{
						Fail("death attribution Killed call argument count does not match its contract");
						return {};
					}
					UPawn* killer = UObject::TryCast<UPawn>(arguments.Values()[0].ToObject());
					UPawn* victim = UObject::TryCast<UPawn>(arguments.Values()[1].ToObject());
					const std::string victimIdentity = PawnIdentity(victim);
					const bool outermost = BeginKilledCall(victimIdentity);
					if (outermost && victim)
						RecordKilled(killer, victim);

					if (QualityParticipants.find(victimIdentity) == QualityParticipants.end())
						return [this, victimIdentity]() { FinishKilledCall(victimIdentity); };
					DeathKiller relation = DeathKiller::None;
					if (killer)
					{
						if (!killer->bIsPlayer()) relation = DeathKiller::NonPlayer;
						else if (killer == victim) relation = DeathKiller::SelfPlayer;
						else relation = DeathKiller::EnemyPlayer;
					}
					const auto entered = AttributionCoordinator.EnterKilled({ victimIdentity, relation,
						AttributionTimeSeconds(), true });
					if (!AcceptCoordinatorStatus(entered.Status, "Killed enter"))
					{
						DiscardStagedHazardDeathPartition(victimIdentity);
						return [this, victimIdentity]() { FinishKilledCall(victimIdentity); };
					}
					if (entered.IsOutermost != outermost)
					{
						Fail("death attribution Killed victim depth disagreed with coordinator");
						AttributionCoordinator.ExitKilled(entered.Token);
						DiscardStagedHazardDeathPartition(victimIdentity);
						return [this, victimIdentity]() { FinishKilledCall(victimIdentity); };
					}
					if (outermost && !BindStagedHazardDeathPartition(victimIdentity, entered.Token))
					{
						AttributionCoordinator.ExitKilled(entered.Token);
						DiscardStagedHazardDeathPartition(victimIdentity);
						return [this, victimIdentity]() { FinishKilledCall(victimIdentity); };
					}
					ActiveAttributionCalls.push_back({ function, instance, victimIdentity, entered.Token,
						AttributionScopeKind::Killed, 0, false, {}, outermost });
					return [this, function, instance, victimIdentity]()
					{
						FinishAttributionCall(function, instance, AttributionScopeKind::Killed);
						FinishKilledCall(victimIdentity);
					};
				}
				if (function->Name == "TakeDamage")
				{
					UPawn* victim = UObject::TryCast<UPawn>(instance);
					const std::string victimIdentity = PawnIdentity(victim);
					if (QualityParticipants.find(victimIdentity) == QualityParticipants.end())
						return {};
					if (!ValidateAttributionFunction(function, HookPoint::TakeDamage))
						return {};
					if (arguments.Size() != 5)
					{
						Fail("death attribution TakeDamage call argument count does not match its contract");
						return {};
					}
					UPawn* instigator = UObject::TryCast<UPawn>(arguments.Values()[1].ToObject());
					DamageInstigator relation = DamageInstigator::NoneOrNonPlayer;
					if (instigator && instigator->bIsPlayer())
						relation = instigator == victim ? DamageInstigator::SelfPlayer :
							DamageInstigator::EnemyPlayer;
					const bool canonical = function == CanonicalTakeDamageFunction;
					const int preHealth = victim->Health();
					const auto entered = AttributionCoordinator.EnterTakeDamage({ victimIdentity,
						relation, AttributionTimeSeconds(), victim->Health(),
						canonical ? DamageFrameKind::Canonical :
							DamageFrameKind::Override, true });
					if (!AcceptCoordinatorStatus(entered.Status, "TakeDamage enter"))
						return {};
					ActiveAttributionCalls.push_back({ function, instance, victimIdentity, entered.Token,
						AttributionScopeKind::TakeDamage, preHealth, canonical,
						PawnIdentity(instigator) });
					return [this, function, instance]()
					{
						FinishAttributionCall(function, instance, AttributionScopeKind::TakeDamage);
					};
				}
				if (function == CanonicalAddVelocityFunction)
				{
					UPawn* victim = UObject::TryCast<UPawn>(instance);
					const std::string victimIdentity = PawnIdentity(victim);
					if (QualityParticipants.find(victimIdentity) == QualityParticipants.end())
						return {};
					if (!ValidateAttributionFunction(function, HookPoint::AddVelocity))
						return {};
					if (arguments.Size() != 1)
					{
						Fail("death attribution AddVelocity call argument count does not match its contract");
						return {};
					}
					const vec3 velocity = arguments.Values()[0].ToVector();
					if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) ||
						!std::isfinite(velocity.z))
					{
						Fail("death attribution observed non-finite AddVelocity input");
						return {};
					}
					AcceptCoordinatorStatus(AttributionCoordinator.ObserveAddVelocity(victimIdentity,
						velocity.x != 0.0f || velocity.y != 0.0f || velocity.z != 0.0f),
						"AddVelocity observation");
					return {};
				}
				if (function->Name == "Touch")
				{
					UInventory* source = UObject::TryCast<UInventory>(instance);
					UPawn* collector = arguments.Size() == 1
						? UObject::TryCast<UPawn>(arguments.Values()[0].ToObject()) : nullptr;
					const std::string collectorIdentity = PawnIdentity(collector);
					if (!source || !collector ||
						QualityParticipants.find(collectorIdentity) == QualityParticipants.end())
						return {};
					const uint32_t depth = PickupTouchDepths[source]++;
					if (depth != 0 || source->Owner() != nullptr)
						return [this, source]() { FinishPickupTouch(source, {}); };
					const ActivePickupTouch touch{ source, collectorIdentity,
						ClassifyPickupCategory(source), true };
					return [this, source, touch]() { FinishPickupTouch(source, touch); };
				}

				std::optional<EnvironmentalSource> source;
				std::optional<HookPoint> point;
				UPawn* environmentalVictim = UObject::TryCast<UPawn>(instance);
				if (function->Name == "PainTimer")
				{
					source = EnvironmentalSource::PainTimer;
					point = HookPoint::PainTimer;
				}
				else if (function->Name == "FellOutOfWorld")
				{
					source = EnvironmentalSource::FellOutOfWorld;
					point = HookPoint::FellOutOfWorld;
				}
				else if (function->Name == "TakeFallingDamage")
				{
					source = EnvironmentalSource::TakeFallingDamage;
					point = HookPoint::TakeFallingDamage;
				}
				else if (function->Name == "Landed")
				{
					source = EnvironmentalSource::Landed;
					point = HookPoint::Landed;
				}
				else if (function->Name == "EncroachingOn" && UObject::TryCast<UMover>(instance))
				{
					source = EnvironmentalSource::MoverEncroachingOn;
					point = HookPoint::MoverEncroachingOn;
					if (arguments.Size() != 1)
					{
						Fail("death attribution EncroachingOn call argument count does not match its contract");
						return {};
					}
					environmentalVictim = UObject::TryCast<UPawn>(arguments.Values()[0].ToObject());
				}
				if (source && point)
				{
					const std::string victimIdentity = PawnIdentity(environmentalVictim);
					if (QualityParticipants.find(victimIdentity) == QualityParticipants.end())
						return {};
					if (!ValidateAttributionFunction(function, *point))
						return {};
					const size_t expectedArguments = *point == HookPoint::Landed ||
						*point == HookPoint::MoverEncroachingOn ? 1 : 0;
					if (arguments.Size() != expectedArguments)
					{
						Fail("death attribution environmental call argument count does not match its contract");
						return {};
					}
					const auto entered = AttributionCoordinator.EnterEnvironmentalSource({
						victimIdentity, *source, AttributionTimeSeconds(), true });
					if (!AcceptCoordinatorStatus(entered.Status, "environmental source enter"))
						return {};
					ActiveAttributionCalls.push_back({ function, instance, victimIdentity, entered.Token,
						AttributionScopeKind::EnvironmentalSource });
					return [this, function, instance]()
					{
						FinishAttributionCall(function, instance,
							AttributionScopeKind::EnvironmentalSource);
					};
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
			hook.ObserveResult = [this](UFunction* function, UObject* instance,
				const Array<ExpressionValue>&, const ExpressionValue&)
			{
				using namespace BotBenchmarkDeathAttribution;
				auto call = std::find_if(ActiveAttributionCalls.rbegin(),
					ActiveAttributionCalls.rend(), [function, instance](const auto& active)
					{
						return active.Function == function && active.Instance == instance;
					});
				if (call == ActiveAttributionCalls.rend())
					return;
				if (call->Kind == AttributionScopeKind::TakeDamage)
				{
					UPawn* victim = UObject::TryCast<UPawn>(instance);
					if (!victim || !AcceptCoordinatorStatus(
						AttributionCoordinator.ObserveTakeDamageResult(call->Token, victim->Health()),
						"TakeDamage result"))
						return;
				}
				else if (call->Kind == AttributionScopeKind::Killed)
				{
					const auto result = AttributionCoordinator.ObserveKilledResult(call->Token);
					if (!AcceptCoordinatorStatus(result.Status, "Killed result"))
						return;
					if (result.Attribution)
					{
						ApplyAttribution(call->VictimIdentity, *result.Attribution);
						FinalizeHazardDeathPartition(call->VictimIdentity,
							*result.Attribution, result.Source, call->Token);
					}
				}
			};
			QualityHookHandle = Frame::CallHooks().Register(std::move(hook));
		}

		void DisableTargetSelectionObserver(std::string status, std::string reason)
		{
			TargetSelectionObserverStatus = std::move(status);
			TargetSelectionObserverReason = std::move(reason);
			if (TargetSelectionHookHandle != 0)
			{
				Frame::CallHooks().Unregister(TargetSelectionHookHandle);
				TargetSelectionHookHandle = 0;
			}
		}

		void FinishTargetSelectionCall(UFunction* function, UObject* instance,
			BotTargetSelectionProbe::ScopeToken token)
		{
			if (ActiveTargetSelectionCalls.empty() || !token)
				return;
			const ActiveTargetSelectionCall call = ActiveTargetSelectionCalls.back();
			if (call.Function != function || call.Instance != instance || call.Token.Value != token.Value)
			{
				auto runtime = QualityParticipants.find(call.BotIdentity);
				if (runtime != QualityParticipants.end())
					runtime->second.TargetSelectionIntegrityFailuresExact++;
				DisableTargetSelectionObserver("disabled_integrity_failure",
					"target-selection call lifecycle was not stack ordered");
				return;
			}
			ActiveTargetSelectionCalls.pop_back();
			auto runtime = QualityParticipants.find(call.BotIdentity);
			if (runtime == QualityParticipants.end())
				return;
			const auto status = runtime->second.TargetSelectionTracker.Exit(token);
			if (status != BotTargetSelectionProbe::TrackerStatus::Accepted &&
				status != BotTargetSelectionProbe::TrackerStatus::MissingResult)
			{
				runtime->second.TargetSelectionIntegrityFailuresExact++;
				DisableTargetSelectionObserver("disabled_integrity_failure",
					"target-selection tracker cleanup failed");
			}
		}

		void ObserveTargetSelectionResult(UFunction* function, UObject* instance,
			const ExpressionValue& result)
		{
			if (ActiveTargetSelectionCalls.empty())
				return;
			const ActiveTargetSelectionCall& call = ActiveTargetSelectionCalls.back();
			if (call.Function != function || call.Instance != instance)
				return;
			auto runtime = QualityParticipants.find(call.BotIdentity);
			if (runtime == QualityParticipants.end())
				return;
			UPawn* pawn = UObject::TryCast<UPawn>(instance);
			if (!pawn || result.GetType() != ExpressionValueType::ValueBool)
			{
				runtime->second.TargetSelectionIntegrityFailuresExact++;
				DisableTargetSelectionObserver("disabled_integrity_failure",
					"target-selection result did not match the validated contract");
				return;
			}
			const auto status = runtime->second.TargetSelectionTracker.ObserveResult(
				call.Token, result.ToBool(), PawnIdentity(pawn->Enemy()));
			if (status != BotTargetSelectionProbe::TrackerStatus::Accepted)
			{
				runtime->second.TargetSelectionIntegrityFailuresExact++;
				DisableTargetSelectionObserver("disabled_integrity_failure",
					"target-selection tracker result failed");
			}
		}

		void RegisterTargetSelectionObserver()
		{
			if (!Config.IsTargetSelectionObserverEnabled())
				return;
			TargetSelectionObserverStatus = "disabled_contract_mismatch";
			TargetSelectionObserverReason.clear();
			std::map<UFunction*, std::string> discovered;
			for (const auto& [identity, pawn] : CaptureLiveControlledBots())
			{
				for (UClass* cls = pawn->Class; cls;
					cls = UObject::TryCast<UClass>(cls->BaseStruct))
				{
					auto inspect = [&](UState* owner)
					{
						for (const auto& [name, function] : owner->Functions)
						{
							if (!function || function->Name != "SetEnemy")
								continue;
							const std::string contractId = "setenemy-v1:" +
								owner->Name.ToString();
							auto [it, inserted] = discovered.emplace(function, contractId);
							if (!inserted && it->second != contractId)
							{
								TargetSelectionObserverReason =
									"SetEnemy has inconsistent declaring identities";
							}
						}
					};
					inspect(cls);
					for (const auto& [name, state] : cls->States)
						if (state) inspect(state);
				}
			}
			if (!TargetSelectionObserverReason.empty() || discovered.empty())
			{
				if (TargetSelectionObserverReason.empty())
					TargetSelectionObserverReason = "no SetEnemy implementation was discovered";
				return;
			}
			for (const auto& [function, contractId] : discovered)
			{
				const std::string error = BotTargetSelectionHookContract::ValidateSetEnemySignature(
					TargetSelectionSignatureShape(function));
				if (!error.empty())
				{
					TargetSelectionObserverReason = "SetEnemy contract mismatch: " + error;
					return;
				}
			}
			ValidatedTargetSelectionFunctions = std::move(discovered);
			TargetSelectionObserverStatus = "active";
			VMCallHook hook;
			hook.Enter = [this](UFunction* function, UObject* instance,
				VMCallArguments& arguments) -> VMCallHookCleanup
			{
				auto known = ValidatedTargetSelectionFunctions.find(function);
				UPawn* pawn = UObject::TryCast<UPawn>(instance);
				const std::string botId = PawnIdentity(pawn);
				auto runtime = QualityParticipants.find(botId);
				if (known == ValidatedTargetSelectionFunctions.end() || !pawn ||
					runtime == QualityParticipants.end())
					return {};
				if (arguments.Size() != 1)
				{
					runtime->second.TargetSelectionIntegrityFailuresExact++;
					DisableTargetSelectionObserver("disabled_integrity_failure",
						"target-selection call arguments did not match the validated contract");
					return {};
				}
				UPawn* requested = UObject::TryCast<UPawn>(arguments.Values()[0].ToObject());
				const std::string requestedId = PawnIdentity(requested);
				if (!requested || requested->bDeleteMe() || requestedId.empty())
				{
					runtime->second.TargetSelectionInvalidIdentifierExact++;
					return {};
				}
				auto entered = runtime->second.TargetSelectionTracker.Enter({ true, known->second,
					botId, PawnIdentity(pawn->Enemy()), requestedId });
				if (entered.Status == BotTargetSelectionProbe::TrackerStatus::CapacityExceeded ||
					entered.Status == BotTargetSelectionProbe::TrackerStatus::RecordCapacityExceeded)
				{
					runtime->second.TargetSelectionTrackerCapacityExceededExact++;
					return {};
				}
				if (entered.Status != BotTargetSelectionProbe::TrackerStatus::Accepted)
				{
					runtime->second.TargetSelectionIntegrityFailuresExact++;
					DisableTargetSelectionObserver("disabled_integrity_failure",
						"target-selection tracker rejected a validated call");
					return {};
				}
				ActiveTargetSelectionCalls.push_back({ function, instance, botId, entered.Token });
				return [this, function, instance, token = entered.Token]()
				{
					FinishTargetSelectionCall(function, instance, token);
				};
			};
			hook.ObserveResult = [this](UFunction* function, UObject* instance,
				const Array<ExpressionValue>&, const ExpressionValue& result)
			{
				ObserveTargetSelectionResult(function, instance, result);
			};
			TargetSelectionHookHandle = Frame::CallHooks().Register(std::move(hook));
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

		void InitializeNavigationCoverage()
		{
			using namespace BotBenchmarkQualityObservation;
			if (!EngineRef.LevelInfo)
				return;
			std::vector<NavigationCoverageNode> nodes;
			std::set<UNavigationPoint*> seen;
			for (UNavigationPoint* point = EngineRef.LevelInfo->NavigationPointList(); point;
				point = point->nextNavigationPoint())
			{
				if (!seen.insert(point).second || seen.size() > MaximumNavigationCoverageNodes)
				{
					LogMessage("Bot benchmark navigation coverage catalog is unavailable: "
						"navigation list is cyclic or exceeds its fixed capacity");
					return;
				}
				if (point->bDeleteMe())
					continue;
				nodes.push_back({ point->Name.ToString(), {
					point->Location().x, point->Location().y, point->Location().z },
					point->CollisionRadius(), point->CollisionHeight() });
			}
			std::sort(nodes.begin(), nodes.end(), [](const auto& left, const auto& right)
			{
				return left.StableId < right.StableId;
			});
			NavigationCoverageCatalog catalog = BuildNavigationCoverageCatalog(std::move(nodes));
			if (!catalog.IsValid())
			{
				LogMessage("Bot benchmark navigation coverage catalog is unavailable: invalid "
					"shared navigation snapshot");
				return;
			}
			NavigationCoverageCatalogNodeCount = catalog.Nodes.size();
			NavigationCoverageUnion = std::make_unique<NavigationCoverageAccumulator>(catalog);
			for (auto& [identity, runtime] : QualityParticipants)
				runtime.NavigationCoverage = std::make_unique<NavigationCoverageAccumulator>(catalog);
		}

		void ObserveNavigationCoverage()
		{
			using namespace BotBenchmarkQualityObservation;
			if (!NavigationCoverageUnion)
				return;
			for (const auto& [identity, pawn] : CaptureLiveControlledBots())
			{
				auto runtime = QualityParticipants.find(identity);
				if (runtime == QualityParticipants.end() || !runtime->second.NavigationCoverage)
					continue;
				const NavigationObserver observer{ { pawn->Location().x, pawn->Location().y,
					pawn->Location().z }, pawn->CollisionRadius(), pawn->CollisionHeight() };
				runtime->second.NavigationCoverage->Observe(observer);
				NavigationCoverageUnion->Observe(observer);
			}
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

		void WriteShadowTelemetry(uint64_t tick, double deltaSeconds, uint64_t& aiFrameScopeMicroseconds)
		{
			if (!ShadowTelemetryFile)
				return;
			if (ShadowTelemetryEventCount >= ShadowTelemetryEventCap)
				throw std::runtime_error("bot benchmark shadow telemetry event cap reached");

			std::vector<BotBenchmarkShadowParticipantState> states;
			MeasureAiFrameScope(aiFrameScopeMicroseconds, [&]
			{
				const auto liveBots = CaptureLiveControlledBots();
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
			});

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

		void OpenRouteExecutionTelemetry()
		{
			const std::filesystem::path outputDirectory(Config.GetOutputDirectory());
			const std::filesystem::path eventsPath = outputDirectory / "route-execution.jsonl";
			RouteExecutionTelemetryFile = File::create_always(eventsPath.string());
			LogMessage("Bot benchmark route execution: " + eventsPath.string());
		}

		void WriteRouteExecutionTelemetry(uint64_t tick)
		{
			if (!RouteExecutionTelemetryFile)
				return;
			if (RouteExecutionTelemetryEventCount >= Config.GetMaxTicks())
				throw std::runtime_error("route-execution telemetry event cap reached");
			const auto liveBots = CaptureLiveControlledBots();
			std::ostringstream out;
			auto writeRoutePathCommits = [&](const std::vector<PawnMovement::RoutePathCommitRecord>& records)
			{
				out << '[';
				for (size_t recordIndex = 0; recordIndex < records.size(); recordIndex++)
				{
					if (recordIndex != 0)
						out << ',';
					const auto& record = records[recordIndex];
					out << "{\"sequence\":\"" << record.Sequence << "\",\"origin\":"
						<< JsonString(record.Origin == PawnMovement::RoutePathCommitOrigin::FindPathToward
							? "find_path_toward" : "find_best_inventory_path")
						<< ",\"phase\":\"pre_special_cache_commit\",\"raw_endpoint_cost\":"
						<< record.RawEndpointCost << ",\"adjusted_endpoint_cost\":"
						<< record.AdjustedEndpointCost
						<< ",\"failed_navigation_penalty_applications\":"
						<< record.FailedNavigationPenaltyApplications
						<< ",\"cache_clear\":" << (record.CacheClear ? "true" : "false")
						<< ",\"truncated_by_route_cache\":"
						<< (record.TruncatedByRouteCache ? "true" : "false") << ",\"nodes\":[";
					for (size_t nodeIndex = 0; nodeIndex < record.Nodes.size(); nodeIndex++)
					{
						if (nodeIndex != 0)
							out << ',';
						out << "{\"name\":" << JsonString(record.Nodes[nodeIndex].Name)
							<< ",\"class\":" << JsonString(record.Nodes[nodeIndex].ClassName) << '}';
					}
					out << "],\"edges\":[";
					for (size_t edgeIndex = 0; edgeIndex < record.Edges.size(); edgeIndex++)
					{
						if (edgeIndex != 0)
							out << ',';
						const auto& edge = record.Edges[edgeIndex];
						out << "{\"reachspec_index\":" << edge.ReachSpecIndex
							<< ",\"start_node\":" << JsonString(edge.StartNode)
							<< ",\"end_node\":" << JsonString(edge.EndNode)
							<< ",\"distance\":" << edge.Distance
							<< ",\"collision_radius\":" << edge.CollisionRadius
							<< ",\"collision_height\":" << edge.CollisionHeight
							<< ",\"reach_flags_raw\":" << edge.ReachFlags
							<< ",\"unknown_reach_flags\":" << edge.UnknownReachFlags
							<< ",\"pruned\":" << (edge.Pruned ? "true" : "false") << '}';
					}
					out << "]}";
				}
				out << ']';
			};
			out.imbue(std::locale::classic());
			out << std::fixed << std::setprecision(6)
				<< "{\"schema\":\"surreal-bot-route-execution-observation-v1\",\"seq\":\""
				<< RouteExecutionTelemetryEventCount << "\",\"benchmark_config_id\":"
				<< JsonString(TelemetryConfigIdentity) << ",\"tick\":\"" << tick << "\",\"participants\":[";
			bool first = true;
			for (const auto& actual : ActualRoster)
			{
				if (!first)
					out << ',';
				first = false;
				auto live = std::find_if(liveBots.begin(), liveBots.end(), [&](const auto& item)
				{
					return item.first == actual.Identity;
				});
				UPawn* pawn = live == liveBots.end() ? nullptr : live->second;
				auto runtime = QualityParticipants.find(actual.Identity);
				std::vector<PawnMovement::RoutePathCommitRecord> pathCommits;
				uint64_t pathCommitOverflow = 0;
				if (runtime != QualityParticipants.end())
				{
					if (pawn)
					{
						runtime->second.RoutePathCommitOverflowExact = std::max(
							runtime->second.RoutePathCommitOverflowExact,
							pawn->RoutePathCommitOverflowCount());
						auto drained = pawn->DrainRoutePathCommitRecords();
						runtime->second.PendingRoutePathCommitRecords.insert(
							runtime->second.PendingRoutePathCommitRecords.end(),
							std::make_move_iterator(drained.begin()),
							std::make_move_iterator(drained.end()));
					}
					pathCommits = std::move(runtime->second.PendingRoutePathCommitRecords);
					runtime->second.PendingRoutePathCommitRecords.clear();
					pathCommitOverflow = runtime->second.RoutePathCommitOverflowExact;
				}
				out << "{\"roster_index\":" << actual.RosterIndex << ",\"identity\":"
					<< JsonString(actual.Identity) << ",\"available\":" << (pawn && pawn->Health() > 0 ? "true" : "false");
				if (pawn && pawn->Health() > 0)
				{
					const vec3 location = pawn->Location();
					const auto previous = RouteExecutionPreviousLocations.find(actual.Identity);
					const bool progressKnown = previous != RouteExecutionPreviousLocations.end();
					const double displacement = progressKnown ? length(location - previous->second) : 0.0;
					RouteExecutionPreviousLocations[actual.Identity] = location;
					out << ",\"position\":{\"x\":" << location.x << ",\"y\":" << location.y
						<< ",\"z\":" << location.z << "},\"velocity\":{\"x\":" << pawn->Velocity().x
						<< ",\"y\":" << pawn->Velocity().y << ",\"z\":" << pawn->Velocity().z
						<< "},\"progress_known\":" << (progressKnown ? "true" : "false")
						<< ",\"displacement_since_previous_tick\":" << displacement;
					UActor* target = pawn->HasProperty("MoveTarget") ? pawn->MoveTarget() : nullptr;
					out << ",\"move_target\":";
					if (target && !target->bDeleteMe())
						out << "{\"name\":" << JsonString(target->Name.ToString()) << ",\"class\":"
							<< JsonString(target->Class->Name.ToString()) << '}';
					else
						out << "null";
					out << ",\"zone\":";
					if (pawn->Region().Zone && !pawn->Region().Zone->bDeleteMe())
						out << "{\"name\":" << JsonString(pawn->Region().Zone->Name.ToString())
							<< ",\"zone_number\":" << static_cast<unsigned int>(pawn->Region().ZoneNumber) << '}';
					else
						out << "null";
					out << ",\"route_cache\":[";
					bool firstRoute = true;
					if (pawn->HasProperty("RouteCache"))
					{
						for (UNavigationPoint* point : pawn->RouteCache())
						{
							if (!point || point->bDeleteMe())
								break;
							if (!firstRoute)
								out << ',';
							firstRoute = false;
							out << "{\"name\":" << JsonString(point->Name.ToString()) << ",\"class\":"
								<< JsonString(point->Class->Name.ToString()) << '}';
						}
					}
					out << ']';
				}
				out << ",\"native_path_commit_overflows_exact\":\""
					<< pathCommitOverflow << "\",\"native_path_commits\":";
				writeRoutePathCommits(pathCommits);
				out << '}';
			}
			out << "]}\n";
			const std::string line = out.str();
			RouteExecutionTelemetryFile->write(line.data(), line.size());
			RouteExecutionTelemetryEventCount++;
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
				bot.PhysicsMode.clear();
				bot.LatentAction.clear();
				bot.AccelerationX = bot.AccelerationY = bot.AccelerationZ = 0.0;
				bot.DestinationX = bot.DestinationY = bot.DestinationZ = 0.0;
				bot.MoveTimer = 0.0;
				bot.MoveTargetIdentity.clear();
				bot.MoveTargetName.clear();
				if (pawn)
				{
					bot.DirectReachCommandOverflowsExact = std::max(
						runtime.DirectReachCommandOverflowsExact,
						pawn->DirectReachCommandOverflowCount());
					AccumulateNativePawnCounters(actual.Identity, runtime, pawn,
						BotBenchmarkDriverDetail::NativePawnCounterSample::LivePawn);
					runtime.Pri = pawn->PlayerReplicationInfo();
					bot.State = pawn->GetStateName().ToString();
					bot.PositionX = pawn->Location().x;
					bot.PositionY = pawn->Location().y;
					bot.PositionZ = pawn->Location().z;
					bot.VelocityX = pawn->Velocity().x;
					bot.VelocityY = pawn->Velocity().y;
					bot.VelocityZ = pawn->Velocity().z;
					bot.PhysicsMode = pawn->HasProperty("Physics") ? PhysicsModeName(pawn->Physics()) : std::string();
					bot.LatentAction = pawn->StateFrame
						? LatentActionName(pawn->StateFrame->LatentState) : std::string();
					if (pawn->HasProperty("Acceleration"))
					{
						bot.AccelerationX = pawn->Acceleration().x;
						bot.AccelerationY = pawn->Acceleration().y;
						bot.AccelerationZ = pawn->Acceleration().z;
					}
					if (pawn->HasProperty("Destination"))
					{
						bot.DestinationX = pawn->Destination().x;
						bot.DestinationY = pawn->Destination().y;
						bot.DestinationZ = pawn->Destination().z;
					}
					if (pawn->HasProperty("MoveTimer"))
						bot.MoveTimer = pawn->MoveTimer();
					if (pawn->HasProperty("MoveTarget"))
					{
						UActor* moveTarget = pawn->MoveTarget();
						bot.MoveTargetIdentity = ActorIdentity(moveTarget);
						bot.MoveTargetName = moveTarget ? moveTarget->Name.ToString() : std::string();
					}
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
				const auto& targetSelection = runtime.TargetSelectionTracker.Counters();
				bot.TargetSelectionOutermostCallsExact = targetSelection.OutermostCalls;
				bot.TargetSelectionNestedCallsExact = targetSelection.NestedCalls;
				bot.TargetSelectionAcceptedTargetChangesExact = targetSelection.AcceptedTargetChanges;
				bot.TargetSelectionAcceptedSameTargetExact = targetSelection.AcceptedSameTargets;
				bot.TargetSelectionRejectedOrUnchangedExact = targetSelection.RejectedOrUnchanged;
				bot.TargetSelectionMissingResultsExact = targetSelection.MissingResults;
				bot.TargetSelectionInvalidIdentifierExact =
					runtime.TargetSelectionInvalidIdentifierExact;
				bot.TargetSelectionTrackerCapacityExceededExact =
					runtime.TargetSelectionTrackerCapacityExceededExact;
				bot.TargetSelectionRecordOverflowsExact = targetSelection.RecordCapacityExceeded;
				bot.TargetSelectionIntegrityFailuresExact =
					runtime.TargetSelectionIntegrityFailuresExact;
				const auto& targetSelectionRecords = runtime.TargetSelectionTracker.Records();
				while (runtime.CapturedTargetSelectionRecords < targetSelectionRecords.size())
				{
					const auto& record = targetSelectionRecords[runtime.CapturedTargetSelectionRecords++];
					runtime.PendingTargetSelectionRecords.push_back({
						runtime.NextTargetSelectionSequence++, record.ContractId, record.BotId,
						record.PreviousTargetId, record.RequestedTargetId, record.ObservedTargetId,
						TargetSelectionOutcomeName(record.Outcome) });
				}
				bot.TargetSelectionRecords = runtime.PendingTargetSelectionRecords;
				bot.EnvironmentalDeathsExact = runtime.EnvironmentalDeathsExact;
				bot.HazardExposedDeathsProxy = runtime.HazardExposedDeathsProxy;
				bot.DamageTakenExact = runtime.DamageTakenExact;
				bot.DamageTakenFromOtherParticipantsExact =
					runtime.DamageTakenFromOtherParticipantsExact;
				bot.DamageTakenFromSelfExact = runtime.DamageTakenFromSelfExact;
				bot.DamageTakenFromNonParticipantsExact =
					runtime.DamageTakenFromNonParticipantsExact;
				bot.DamageDealtToOtherParticipantsExact =
					runtime.DamageDealtToOtherParticipantsExact;
				bot.ConfirmedPickupsExact = runtime.ConfirmedPickupsExact;
				bot.ConfirmedWeaponPickupsExact = runtime.ConfirmedWeaponPickupsExact;
				bot.ConfirmedAmmoPickupsExact = runtime.ConfirmedAmmoPickupsExact;
				bot.ConfirmedHealthPickupsExact = runtime.ConfirmedHealthPickupsExact;
				bot.ConfirmedArmorPickupsExact = runtime.ConfirmedArmorPickupsExact;
				bot.ConfirmedOtherPickupsExact = runtime.ConfirmedOtherPickupsExact;
				bot.PickupSourceConsumedUnconfirmedExact =
					runtime.PickupSourceConsumedUnconfirmedExact;
				bot.NavigationCoverageCatalogNodesExact = NavigationCoverageCatalogNodeCount;
				bot.NavigationCoverageVisitedNodesExact = runtime.NavigationCoverage
					? runtime.NavigationCoverage->UniqueVisitedNodeCount() : 0;
				bot.NavigationCoverageUnionVisitedNodesExact = NavigationCoverageUnion
					? NavigationCoverageUnion->UniqueVisitedNodeCount() : 0;
				bot.DirectSelfKills = runtime.DeathAttribution.DirectSelfKills;
				bot.DirectEnemyKills = runtime.DeathAttribution.DirectEnemyKills;
				bot.UnassistedEnvironmentalDeaths =
					runtime.DeathAttribution.UnassistedEnvironmentalDeaths;
				bot.RecentEnemyContributedEnvironmentalDeathsProxy =
					runtime.DeathAttribution.RecentEnemyContributedEnvironmentalDeathsProxy;
				bot.AmbiguousDeaths = runtime.DeathAttribution.AmbiguousDeaths;
				bot.RecentEnemyMomentumContributedEnvironmentalDeathsProxy =
					runtime.DeathAttribution.RecentEnemyMomentumContributedEnvironmentalDeathsProxy;
				bot.HitWallEventsExact = runtime.HitWallEventsExact;
				const auto& native = runtime.NativeCounterTotals;
				bot.WalkingHitWallDispatchObservationsExact =
					native.WalkingHitWallDispatchObservations;
				bot.WalkingHitWallDispatchLegacyZBandExact =
					native.WalkingHitWallDispatchLegacyZBand;
				bot.WalkingHitWallDispatchMinHitWallExact =
					native.WalkingHitWallDispatchMinHitWall;
				bot.WalkingHitWallDispatchDisagreementsExact =
					native.WalkingHitWallDispatchDisagreements;
				bot.WalkingHitWallDispatchCallbacksExact =
					native.WalkingHitWallDispatchCallbacks;
				bot.WalkingHitWallDispatchDiagnosticOverflowsExact =
					native.WalkingHitWallDispatchDiagnosticOverflows;
				bot.PainLedgeVetoesExact = native.PainLedgeVetoes;
				bot.PainLedgeRepeatVetoesExact = native.PainLedgeRepeatVetoes;
				bot.PainLedgeRecoveryAttemptsExact = native.PainLedgeRecoveryAttempts;
				bot.PainLedgeRecoveryEscapesExact = native.PainLedgeRecoveryEscapes;
				bot.WallAdjustCallsExact = native.WallAdjustCalls;
				bot.WallAdjustRepeatsExact = native.WallAdjustRepeats;
				bot.WallAdjustRecoveryAttemptsExact = native.WallAdjustRecoveryAttempts;
				bot.WallAdjustRecoverySuccessesExact = native.WallAdjustRecoverySuccesses;
				bot.WallAdjustForcedReplansExact = native.WallAdjustForcedReplans;
				bot.MoveStallDetectionsExact = native.MoveStallDetections;
				bot.MoveStallEpisodeResetsExact = native.MoveStallEpisodeResets;
				bot.MoveStallForcedReplansExact = native.MoveStallForcedReplans;
				bot.MoveStallNavigationForcedReplansExact =
					native.MoveStallNavigationForcedReplans;
				bot.MoveStallTargetlessMoveToTimeoutsExact =
					native.MoveStallTargetlessMoveToTimeouts;
				bot.MoveStallDirectActorMoveTowardTimeoutsExact =
					native.MoveStallDirectActorMoveTowardTimeouts;
				bot.MoveStallEligibleSeconds = native.MoveStallEligibleSeconds;
				bot.MoveStallRecoveryEpisodesExact = native.MoveStallRecoveryEpisodes;
				bot.MoveStallRecoveryClearedWithin2SecondsExact =
					native.MoveStallRecoveryClearedWithin2Seconds;
				bot.MoveStallRecoveryClearedAfter2SecondsWithin5SecondsExact =
					native.MoveStallRecoveryClearedAfter2SecondsWithin5Seconds;
				bot.MoveStallRecoveryReplannedWithin5SecondsExact =
					native.MoveStallRecoveryReplannedWithin5Seconds;
				bot.MoveStallRecoveryMissed5SecondDeadlineExact =
					native.MoveStallRecoveryMissed5SecondDeadline;
				bot.MoveStallRecoveryExcludedIntentionalStopsExact =
					native.MoveStallRecoveryExcludedIntentionalStops;
				bot.MoveStallRecoveryCensoredLifeBoundariesExact =
					native.MoveStallRecoveryCensoredLifeBoundaries;
				bot.MoveStallRecoveryCensoredRunEndExact =
					native.MoveStallRecoveryCensoredRunEnd;
				bot.MoveStallRecoveryUnknownExact = native.MoveStallRecoveryUnknown;
				bot.MoveStallRecoveryEpisodeRecordOverflowsExact =
					native.MoveStallRecoveryEpisodeRecordOverflows;
				bot.MoveStallRecoveryDecisionRecordOverflowsExact =
					native.MoveStallRecoveryDecisionRecordOverflows;
				bot.FailedNavigationAvoidanceActivationsExact =
					native.FailedNavigationAvoidanceActivations;
				bot.FailedNavigationSafeguardSuppressionsExact =
					native.FailedNavigationSafeguardSuppressions;
				bot.FailedNavigationRoutePenaltyApplicationsExact =
					native.FailedNavigationRoutePenaltyApplications;
				bot.HarmfulZoneEscapeEpisodesExact = native.HarmfulZoneEscapeEpisodes;
				bot.HarmfulZoneEscapeCenterEntriesExact = native.HarmfulZoneEscapeCenterEntries;
				bot.HarmfulZoneEscapeFootEntriesExact = native.HarmfulZoneEscapeFootEntries;
				bot.HarmfulZoneEscapeRecoveryAttemptsExact =
					native.HarmfulZoneEscapeRecoveryAttempts;
				bot.HarmfulZoneEscapeSuccessfulEscapesExact =
					native.HarmfulZoneEscapeSuccessfulEscapes;
				bot.HarmfulZoneEscapeForcedReplansExact =
					native.HarmfulZoneEscapeForcedReplans;
				bot.HarmfulZoneEscapeNoSafeCandidatesExact =
					native.HarmfulZoneEscapeNoSafeCandidates;
				bot.HazardSwimEgressEpisodesExact = native.HazardSwimEgressEpisodes;
				bot.HazardSwimEgressEligibleExact = native.HazardSwimEgressEligible;
				bot.HazardSwimEgressAuthorizedExact = native.HazardSwimEgressAuthorized;
				bot.HazardSwimEgressDebouncedExact = native.HazardSwimEgressDebounced;
				bot.HazardSwimEgressNoAnchorRejectedExact = native.HazardSwimEgressNoAnchorRejected;
				bot.HazardSwimEgressExitedExact = native.HazardSwimEgressExited;
				bot.HazardSwimEgressDeathsBeforeExitExact = native.HazardSwimEgressDeathsBeforeExit;
				bot.HazardSwimEgressForcedReplansExact = native.HazardSwimEgressForcedReplans;
				bot.HazardSwimEgressForcedReplanSameCommandReissuedExact =
					native.HazardSwimEgressForcedReplanSameCommandReissued;
				bot.HazardSwimEgressForcedReplanDifferentCommandIssuedExact =
					native.HazardSwimEgressForcedReplanDifferentCommandIssued;
				bot.HazardSwimEgressForcedReplanHazardClearedBeforeCommandExact =
					native.HazardSwimEgressForcedReplanHazardClearedBeforeCommand;
				bot.HazardSwimEgressForcedReplanFellBeforeCommandExact =
					native.HazardSwimEgressForcedReplanFellBeforeCommand;
				bot.HazardSwimEgressForcedReplanDiedBeforeCommandExact =
					native.HazardSwimEgressForcedReplanDiedBeforeCommand;
				bot.HazardSwimEgressForcedReplanLifeBoundaryCensoredExact =
					native.HazardSwimEgressForcedReplanLifeBoundaryCensored;
				bot.HazardSwimEgressForcedReplanRunEndCensoredExact =
					native.HazardSwimEgressForcedReplanRunEndCensored;
				bot.HazardSwimEgressForcedReplanEpisodeAbandonedExact =
					native.HazardSwimEgressForcedReplanEpisodeAbandoned;
				bot.HazardSwimEgressFallingPreMoveAnchorCapturesExact =
					native.HazardSwimEgressFallingPreMoveAnchorCaptures;
				bot.HazardSwimEgressFallingPreMoveAnchorUsesExact =
					native.HazardSwimEgressFallingPreMoveAnchorUses;
				bot.HazardSwimEgressLiveAppliesExact = native.HazardSwimEgressLiveApplies;
				bot.HazardSwimEgressLiveActiveTicksExact = native.HazardSwimEgressLiveActiveTicks;
				bot.HazardSwimEgressLiveProbeRejectedExact = native.HazardSwimEgressLiveProbeRejected;
				bot.HazardSwimEgressLiveSuccessfulExitsExact = native.HazardSwimEgressLiveSuccessfulExits;
				bot.HazardSwimEgressLiveShadowCandidatesExact = native.HazardSwimEgressLiveShadowCandidates;
				bot.HazardSwimEgressLiveShadowFallingTerminalsExact = native.HazardSwimEgressLiveShadowFallingTerminals;
				bot.HazardSwimEgressLiveShadowHazardClearedTerminalsExact = native.HazardSwimEgressLiveShadowHazardClearedTerminals;
				bot.HazardSwimEgressLiveShadowProbeBlockedTerminalsExact = native.HazardSwimEgressLiveShadowProbeBlockedTerminals;
				bot.HazardSwimEgressDirectNavProbesExact = native.HazardSwimEgressDirectNavProbes;
				bot.HazardSwimEgressDirectNavSafeCandidatesExact = native.HazardSwimEgressDirectNavSafeCandidates;
				bot.HazardResidenceEpisodesExact = native.HazardResidenceEpisodes;
				bot.HazardResidenceClearedExact = native.HazardResidenceCleared;
				bot.HazardResidenceDeathsExact = native.HazardResidenceDeaths;
				bot.HazardResidenceLifeBoundaryCensoredExact = native.HazardResidenceLifeBoundaryCensored;
				bot.HazardResidenceRunEndCensoredExact = native.HazardResidenceRunEndCensored;
				bot.HazardResidenceUnknownExact = native.HazardResidenceUnknown;
				bot.HazardResidenceReentriesExact = native.HazardResidenceReentries;
				bot.HazardResidenceCommandChangesExact = native.HazardResidenceCommandChanges;
				bot.HazardResidenceCandidatesObservedExact = native.HazardResidenceCandidatesObserved;
				bot.HazardResidenceCandidateOtherCommandsExact = native.HazardResidenceCandidateOtherCommands;
				bot.HazardSwimEgressDirectNavBestCandidateName = native.HazardSwimEgressDirectNavBestCandidateName;
				bot.HazardSwimEgressAnchorKnown = native.HazardSwimEgressAnchorKnown;
				bot.HazardSwimEgressAnchorSource = native.HazardSwimEgressAnchorSource;
				bot.HazardWaterEgressDiagnosticOverflowsExact = pawn
					? pawn->HazardWaterEgressDiagnosticOverflowCount() : 0;
				bot.FallingHazardRecoveryPromotionsExact = native.FallingHazardRecoveryPromotions;
				bot.FallingHazardRecoveryAdvanceCallsExact =
					native.FallingHazardRecoveryAdvanceCalls;
				bot.FallingHazardRecoveryContextRejectedExact =
					native.FallingHazardRecoveryContextRejected;
				bot.FallingHazardRecoveryNoActiveFallEpisodeExact =
					native.FallingHazardRecoveryNoActiveFallEpisode;
				bot.FallingHazardRecoveryNoPrefixExact = native.FallingHazardRecoveryNoPrefix;
				bot.FallingHazardRecoveryEligibleExact = native.FallingHazardRecoveryEligible;
				bot.FallingHazardRecoveryAnchorRejectedExact =
					native.FallingHazardRecoveryAnchorRejected;
				bot.FallingHazardRecoveryProbeRejectedExact =
					native.FallingHazardRecoveryProbeRejected;
				bot.FallingHazardRecoveryLiveAppliesExact =
					native.FallingHazardRecoveryLiveApplies;
				bot.FallingHazardRecoveryLiveActiveTicksExact =
					native.FallingHazardRecoveryLiveActiveTicks;
				bot.FallingHazardRecoverySafeLandingsExact =
					native.FallingHazardRecoverySafeLandings;
				bot.FallingHazardRecoveryHarmfulEntriesExact =
					native.FallingHazardRecoveryHarmfulEntries;
				bot.FallingHazardRecoveryDeathsExact = native.FallingHazardRecoveryDeaths;
				bot.FallingHazardRecoveryTimeoutsExact = native.FallingHazardRecoveryTimeouts;
				bot.ExternalImpulseFallHarmfulWitnessesExact = native.ExternalImpulseFallHarmfulWitnesses;
				bot.ExternalImpulseFallNoAirControlExact = native.ExternalImpulseFallNoAirControl;
				bot.ExternalImpulseFallAlternativesTestedExact = native.ExternalImpulseFallAlternativesTested;
				bot.ExternalImpulseFallCertifiedExact = native.ExternalImpulseFallCertified;
				bot.ExternalImpulseFallUncertifiedExact = native.ExternalImpulseFallUncertified;
				bot.FallingSeamDetectionsExact = native.FallingSeamDetections;
				bot.HorizontalCornerCandidateProbesExact =
					native.HorizontalCornerCandidateProbes;
				bot.HorizontalCornerAuthorizedEscapesExact =
					native.HorizontalCornerAuthorizedEscapes;
				bot.HorizontalCornerTargetProgressRejectsExact =
					native.HorizontalCornerTargetProgressRejects;
				bot.HorizontalCornerUnknownOrUnsafeSupportExact =
					native.HorizontalCornerUnknownOrUnsafeSupport;
				bot.FallingSeamEpisodesExact = native.FallingSeamEpisodes;
				bot.FallingSeamInvalidGeometryRejectsExact =
					native.FallingSeamInvalidGeometryRejects;
				bot.FallingSeamAuthorizableEpisodesExact =
					native.FallingSeamAuthorizableEpisodes;
				bot.HorizontalCornerAuthorizedCandidatesExact =
					native.HorizontalCornerAuthorizedCandidates;
				bot.HorizontalCornerBlockedSweepCandidatesExact =
					native.HorizontalCornerBlockedSweepCandidates;
				bot.HorizontalCornerNoStaticWalkableSupportCandidatesExact =
					native.HorizontalCornerNoStaticWalkableSupportCandidates;
				bot.HorizontalCornerPainSupportCandidatesExact =
					native.HorizontalCornerPainSupportCandidates;
				bot.HorizontalCornerNoActiveMovementIntentOrTargetCandidatesExact =
					native.HorizontalCornerNoActiveMovementIntentOrTargetCandidates;
				bot.HorizontalCornerTrueTargetRegressionCandidatesExact =
					native.HorizontalCornerTrueTargetRegressionCandidates;
				bot.HorizontalCornerUnknownEvidenceCandidatesExact =
					native.HorizontalCornerUnknownEvidenceCandidates;
				bot.WalkingStepPreflightObservationsExact =
					native.WalkingStepPreflightObservations;
				bot.WalkingStepPreflightUnsupportedEndpointsExact =
					native.WalkingStepPreflightUnsupportedEndpoints;
				bot.WalkingStepPreflightNoDecisionsExact =
					native.WalkingStepPreflightNoDecisions;
				bot.WalkingStepPreflightProvisionalAuthorizationsExact =
					native.WalkingStepPreflightProvisionalAuthorizations;
				bot.WalkingStepPreflightAuthorizationsExact =
					native.WalkingStepPreflightAuthorizations;
				bot.WalkingStepPreflightAuthorizableEpisodesExact =
					native.WalkingStepPreflightAuthorizableEpisodes;
				bot.WalkingStepPreflightPositiveDpsVetoEligibleExact =
					native.WalkingStepPreflightPositiveDpsVetoEligible;
				bot.WalkingStepPreflightPositiveDpsVetoAppliedExact =
					native.WalkingStepPreflightPositiveDpsVetoApplied;
				bot.WalkingStepPreflightPositiveDpsVetoDebouncedExact =
					native.WalkingStepPreflightPositiveDpsVetoDebounced;
				bot.WalkingStepPreflightPositiveDpsVetoForcedReplansExact =
					native.WalkingStepPreflightPositiveDpsVetoForcedReplans;
				bot.WalkingStepPreflightPositiveDpsVetoRollbackRejectedExact =
					native.WalkingStepPreflightPositiveDpsVetoRollbackRejected;
				bot.WalkingStepPreflightPositiveDpsVetoActionOverflowsExact =
					native.WalkingStepPreflightPositiveDpsVetoActionOverflows;
				bot.WalkingStepPreflightDiagnosticOverflowsExact =
					native.WalkingStepPreflightDiagnosticOverflows;
				bot.InventoryDirectReachSupportObservationsExact =
					native.InventoryDirectReachSupportObservations;
				bot.InventoryDirectReachSupportSafeSupportedExact =
					native.InventoryDirectReachSupportSafeSupported;
				bot.InventoryDirectReachSupportSafeUnsupportedNoObservedHazardExact =
					native.InventoryDirectReachSupportSafeUnsupportedNoObservedHazard;
				bot.InventoryDirectReachSupportUnsafeHarmfulFootZoneExact =
					native.InventoryDirectReachSupportUnsafeHarmfulFootZone;
				bot.InventoryDirectReachSupportUnsafeUnsupportedOverHarmfulExact =
					native.InventoryDirectReachSupportUnsafeUnsupportedOverHarmful;
				bot.InventoryDirectReachSupportUnavailableExact =
					native.InventoryDirectReachSupportUnavailable;
				bot.InventoryDirectReachSupportDiagnosticOverflowsExact =
					native.InventoryDirectReachSupportDiagnosticOverflows;
				bot.InventoryMarkerDirectReachRejectsExact =
					native.InventoryMarkerDirectReachRejects;
				bot.FallingParityRealizedEpisodesExact = native.FallingParityRealizedEpisodes;
				bot.FallingParityRealizedStepsExact = native.FallingParityRealizedSteps;
				bot.FallingParityRealizedMatchedStepsExact = native.FallingParityRealizedMatchedSteps;
				bot.FallingParityRealizedMatchedLandingStepsExact =
					native.FallingParityRealizedMatchedLandingSteps;
				bot.FallingParityRealizedMismatchesExact = native.FallingParityRealizedMismatches;
				bot.FallingParityRealizedUnknownsExact = native.FallingParityRealizedUnknowns;
				bot.FallingParityRealizedCallbackBarriersExact = native.FallingParityRealizedCallbackBarriers;
				bot.FallingParityRealizedPainEntriesExact = native.FallingParityRealizedPainEntries;
				bot.FallingParityRealizedDeathsExact = native.FallingParityRealizedDeaths;
				bot.FallingParityRealizedLandingsExact = native.FallingParityRealizedLandings;
				bot.FallingParityRealizedContinuityLossesExact = native.FallingParityRealizedContinuityLosses;
				bot.FallingParityRealizedRecordOverflowsExact = native.FallingParityRealizedRecordOverflows;
				bot.VerticalPainColumnEpisodesStartedExact =
					native.VerticalPainColumnEpisodesStarted;
				bot.VerticalPainColumnEpisodesCompletedExact =
					native.VerticalPainColumnEpisodesCompleted;
				bot.VerticalPainColumnTruePositiveOutcomesExact =
					native.VerticalPainColumnTruePositiveOutcomes;
				bot.VerticalPainColumnFalsePositiveOutcomesExact =
					native.VerticalPainColumnFalsePositiveOutcomes;
				bot.VerticalPainColumnFalseNegativeOutcomesExact =
					native.VerticalPainColumnFalseNegativeOutcomes;
				bot.VerticalPainColumnTrueNegativeOutcomesExact =
					native.VerticalPainColumnTrueNegativeOutcomes;
				bot.VerticalPainColumnAmbiguousOutcomesExact =
					native.VerticalPainColumnAmbiguousOutcomes;
				bot.VerticalPainColumnUnknownOutcomesExact =
					native.VerticalPainColumnUnknownOutcomes;
				bot.VerticalPainColumnDiagnosticOverflowsExact =
					native.VerticalPainColumnDiagnosticOverflows;
				bot.VerticalPainColumnGenerationCapacityExhaustionsExact =
					native.VerticalPainColumnGenerationCapacityExhaustions;
				bot.PersistentHarmfulFallCandidatesStartedExact =
					native.PersistentHarmfulFallCandidatesStarted;
				bot.PersistentHarmfulFallPromotionsExact =
					native.PersistentHarmfulFallPromotions;
				bot.PersistentHarmfulFallResetsExact = native.PersistentHarmfulFallResets;
				bot.PersistentHarmfulFallConfirmedHarmfulEntriesExact =
					native.PersistentHarmfulFallConfirmedHarmfulEntries;
				bot.PersistentHarmfulFallObservedLeadSamplesExact =
					native.PersistentHarmfulFallObservedLeadSamples;
				bot.PersistentHarmfulFallObservedLeadMillisecondsExact =
					native.PersistentHarmfulFallObservedLeadMilliseconds;
				bot.SingleHarmfulFallPrefixCandidatesStartedExact =
					native.SingleHarmfulFallPrefixCandidatesStarted;
				bot.SingleHarmfulFallPrefixPromotionsExact =
					native.SingleHarmfulFallPrefixPromotions;
				bot.SingleHarmfulFallPrefixResetsExact =
					native.SingleHarmfulFallPrefixResets;
				bot.SingleHarmfulFallPrefixConfirmedHarmfulEntriesExact =
					native.SingleHarmfulFallPrefixConfirmedHarmfulEntries;
				bot.SingleHarmfulFallPrefixObservedLeadSamplesExact =
					native.SingleHarmfulFallPrefixObservedLeadSamples;
				bot.SingleHarmfulFallPrefixObservedLeadMillisecondsExact =
					native.SingleHarmfulFallPrefixObservedLeadMilliseconds;
				bot.DirectHarmfulWaterEntryCandidatesExact =
					native.DirectHarmfulWaterEntryCandidates;
				bot.DirectHarmfulWaterEntryConfirmedExact =
					native.DirectHarmfulWaterEntryConfirmed;
				bot.DirectHarmfulWaterEntryConfirmedNoHarmExact =
					native.DirectHarmfulWaterEntryConfirmedNoHarm;
				bot.DirectHarmfulWaterEntryUnresolvedExact =
					native.DirectHarmfulWaterEntryUnresolved;
				bot.DirectHarmfulWaterEntryLeadSamplesExact =
					native.DirectHarmfulWaterEntryLeadSamples;
				bot.DirectHarmfulWaterEntryLeadMillisecondsExact =
					native.DirectHarmfulWaterEntryLeadMilliseconds;
				bot.DirectHarmfulWaterEntryCertificateResultsExact =
					native.DirectHarmfulWaterEntryCertificateResults;
				if (pawn)
				{
					CensorStaleOpenDirectReachCommands(runtime, pawn->DirectReachCommandLifeId(),
						Ticks);
					for (const auto& [lifeId, terminal] :
						pawn->DrainDirectReachHazardResidenceTerminals())
					{
						switch (terminal)
						{
						case PawnMovement::HazardResidenceTerminal::Cleared:
							ResolveOpenDirectReachCommands(runtime, lifeId, "cleared", false, Ticks);
							break;
						case PawnMovement::HazardResidenceTerminal::Death:
							ResolveOpenDirectReachCommands(runtime, lifeId, "hazardous_death", true,
								Ticks);
							break;
						case PawnMovement::HazardResidenceTerminal::LifeBoundary:
							ResolveOpenDirectReachCommands(runtime, lifeId,
								"life_boundary_censor", false, Ticks);
							break;
						case PawnMovement::HazardResidenceTerminal::RunEnd:
							ResolveOpenDirectReachCommands(runtime, lifeId, "run_end_censor", false,
								Ticks);
							break;
						case PawnMovement::HazardResidenceTerminal::None:
						case PawnMovement::HazardResidenceTerminal::Unknown:
							break;
						}
					}
					auto diagnostics = pawn->DrainWalkingStepPreflightDiagnostics();
					auto inventoryDirectReachDiagnostics =
						pawn->DrainInventoryDirectReachSupportDiagnostics();
					auto directReachCommandObservations =
						pawn->DrainDirectReachCommandObservations();
					runtime.DirectReachCommandOverflowsExact = std::max(
						runtime.DirectReachCommandOverflowsExact,
						pawn->DirectReachCommandOverflowCount());
					auto walkingHitWallDiagnostics =
						pawn->DrainWalkingHitWallDispatchDiagnostics();
					runtime.PendingWalkingStepPreflightDiagnostics.insert(
						runtime.PendingWalkingStepPreflightDiagnostics.end(),
						std::make_move_iterator(diagnostics.begin()),
						std::make_move_iterator(diagnostics.end()));
					runtime.PendingInventoryDirectReachSupportDiagnostics.insert(
						runtime.PendingInventoryDirectReachSupportDiagnostics.end(),
						std::make_move_iterator(inventoryDirectReachDiagnostics.begin()),
						std::make_move_iterator(inventoryDirectReachDiagnostics.end()));
					const bool activeDirectCommand = bot.LatentAction == "MoveTo"
						|| bot.LatentAction == "MoveToward";
					const bool routeHeadPresent = EngineRef.LaunchInfo.ue1Version > 219
						&& pawn->RouteCache()[0] != nullptr;
					UActor* moveTarget = pawn->MoveTarget();
					CloseReplacedDirectReachCommands(runtime, pawn, bot, routeHeadPresent, Ticks);
					std::vector<PawnMovement::DirectReachCommandObservation>
						directReachCommandObservationsReady;
					std::vector<PawnMovement::DirectReachCommandObservation>
						stillPendingDirectReachCommandObservations;
					for (auto& observation : runtime.PendingDirectReachCommandObservations)
					{
						if (observation.NativeTick < Ticks)
							directReachCommandObservationsReady.push_back(std::move(observation));
						else
							stillPendingDirectReachCommandObservations.push_back(std::move(observation));
					}
					runtime.PendingDirectReachCommandObservations =
						std::move(stillPendingDirectReachCommandObservations);
					for (auto& observation : directReachCommandObservations)
					{
						if (observation.NativeTick < Ticks)
							directReachCommandObservationsReady.push_back(std::move(observation));
						else
							runtime.PendingDirectReachCommandObservations.push_back(std::move(observation));
					}
					for (const auto& observation : directReachCommandObservationsReady)
					{
						const bool targetMatches = moveTarget && !moveTarget->bDeleteMe()
							&& moveTarget->Index == observation.TargetActorIndex
							&& moveTarget == observation.TargetAddress;
						std::string linkStatus;
						if (!observation.Reached)
							linkStatus = "not_reached";
						else if (observation.LifeId != pawn->DirectReachCommandLifeId())
							linkStatus = "unavailable_life_boundary";
						else if (observation.CallerOrigin !=
							PawnMovement::DirectReachCommandCallerOrigin::ScriptActorReachable)
							linkStatus = "unavailable_caller_origin";
						else if (!activeDirectCommand)
							linkStatus = "unavailable_no_active_direct_command";
						else if (routeHeadPresent)
							linkStatus = "unavailable_route_head";
						else if (!targetMatches)
							linkStatus = "unavailable_target_replaced";
						else
							linkStatus = "same_life_exact";
						BotBenchmarkDirectReachCommandRecord record{
							0, observation.LifeId,
							observation.TargetActorIndex, observation.TargetAddress,
							observation.TargetName,
							observation.TargetClass, observation.Reached, observation.CheckNavpoint,
							observation.ResolvedWallSlide, observation.WalkingSimulationIterations,
							bot.LatentAction, routeHeadPresent, std::move(linkStatus), Ticks, 0,
							std::string(), false };
						record.ReachSequence = observation.Sequence;
						record.NativeTick = observation.NativeTick;
						record.CallerOrigin = PawnMovement::DirectReachCommandCallerOriginName(
							observation.CallerOrigin);
						record.RejectReason = PawnMovement::DirectReachCommandRejectReasonName(
							observation.RejectReason);
						runtime.DirectReachCommandObservationsExact++;
						if (observation.Reached)
							runtime.DirectReachCommandSuccessesExact++;
						else
							runtime.DirectReachCommandFailuresExact++;
						if (record.LinkStatus == "same_life_exact")
						{
							runtime.DirectReachCommandSameLifeExactExact++;
							runtime.OpenDirectReachCommandRecords.push_back(std::move(record));
						}
						else
						{
							runtime.DirectReachCommandUnlinkedExact++;
							record.Sequence = runtime.NextDirectReachCommandSequence++;
							runtime.PendingDirectReachCommandRecords.push_back(std::move(record));
						}
					}
					runtime.PendingWalkingHitWallDispatchDiagnostics.insert(
						runtime.PendingWalkingHitWallDispatchDiagnostics.end(),
						std::make_move_iterator(walkingHitWallDiagnostics.begin()),
						std::make_move_iterator(walkingHitWallDiagnostics.end()));
					auto vetoActions = pawn->DrainWalkingStepPreflightPositiveDpsVetoActions();
					runtime.PendingWalkingStepPreflightPositiveDpsVetoActions.insert(
						runtime.PendingWalkingStepPreflightPositiveDpsVetoActions.end(),
						std::make_move_iterator(vetoActions.begin()),
						std::make_move_iterator(vetoActions.end()));
					auto parityRecords = pawn->DrainFallingParityRealizedRecords();
					runtime.PendingFallingParityRealizedRecords.insert(
						runtime.PendingFallingParityRealizedRecords.end(),
						std::make_move_iterator(parityRecords.begin()),
						std::make_move_iterator(parityRecords.end()));
					auto hazardDiagnostics = pawn->DrainFallingHazardDiagnostics();
					runtime.PendingFallingHazardDiagnostics.insert(
						runtime.PendingFallingHazardDiagnostics.end(),
						std::make_move_iterator(hazardDiagnostics.begin()),
						std::make_move_iterator(hazardDiagnostics.end()));
					auto waterEgressDiagnostics = pawn->DrainHazardWaterEgressDiagnostics();
					runtime.PendingHazardWaterEgressDiagnostics.insert(
						runtime.PendingHazardWaterEgressDiagnostics.end(),
						std::make_move_iterator(waterEgressDiagnostics.begin()),
						std::make_move_iterator(waterEgressDiagnostics.end()));
					auto moveStallRecoveryRecords = pawn->DrainMoveStallRecoveryEpisodeRecords();
					auto moveStallDecisionRecords = pawn->DrainMoveStallRecoveryDecisionRecords();
					runtime.PendingMoveStallRecoveryEpisodeRecords.insert(
						runtime.PendingMoveStallRecoveryEpisodeRecords.end(),
						std::make_move_iterator(moveStallRecoveryRecords.begin()),
						std::make_move_iterator(moveStallRecoveryRecords.end()));
					runtime.PendingMoveStallRecoveryDecisionRecords.insert(
						runtime.PendingMoveStallRecoveryDecisionRecords.end(),
						std::make_move_iterator(moveStallDecisionRecords.begin()),
						std::make_move_iterator(moveStallDecisionRecords.end()));
				}
				bot.WalkingStepPreflightDiagnostics = std::move(
					runtime.PendingWalkingStepPreflightDiagnostics);
				runtime.PendingWalkingStepPreflightDiagnostics.clear();
				bot.InventoryDirectReachSupportDiagnostics = std::move(
					runtime.PendingInventoryDirectReachSupportDiagnostics);
				runtime.PendingInventoryDirectReachSupportDiagnostics.clear();
				bot.DirectReachCommandRecords = std::move(runtime.PendingDirectReachCommandRecords);
				runtime.PendingDirectReachCommandRecords.clear();
				bot.DirectReachCommandObservationsExact = runtime.DirectReachCommandObservationsExact;
				bot.DirectReachCommandSuccessesExact = runtime.DirectReachCommandSuccessesExact;
				bot.DirectReachCommandFailuresExact = runtime.DirectReachCommandFailuresExact;
				bot.DirectReachCommandSameLifeExactExact = runtime.DirectReachCommandSameLifeExactExact;
				bot.DirectReachCommandUnlinkedExact = runtime.DirectReachCommandUnlinkedExact;
				bot.DirectReachCommandOverflowsExact = runtime.DirectReachCommandOverflowsExact;
				bot.DirectReachCommandHazardousDeathsExact =
					runtime.DirectReachCommandHazardousDeathsExact;
				bot.DirectReachCommandNonhazardDeathsExact =
					runtime.DirectReachCommandNonhazardDeathsExact;
				bot.DirectReachCommandClearedExact = runtime.DirectReachCommandClearedExact;
				bot.DirectReachCommandLifeBoundaryCensoredExact =
					runtime.DirectReachCommandLifeBoundaryCensoredExact;
				bot.DirectReachCommandRunEndCensoredExact =
					runtime.DirectReachCommandRunEndCensoredExact;
				bot.DirectReachCommandCommandReplacedExact =
					runtime.DirectReachCommandCommandReplacedExact;
				bot.WalkingHitWallDispatchDiagnostics = std::move(
					runtime.PendingWalkingHitWallDispatchDiagnostics);
				runtime.PendingWalkingHitWallDispatchDiagnostics.clear();
				bot.WalkingStepPreflightPositiveDpsVetoActions = std::move(
					runtime.PendingWalkingStepPreflightPositiveDpsVetoActions);
				runtime.PendingWalkingStepPreflightPositiveDpsVetoActions.clear();
				bot.FallingParityRealizedRecords = std::move(
					runtime.PendingFallingParityRealizedRecords);
				runtime.PendingFallingParityRealizedRecords.clear();
				bot.VerticalPainColumnDiagnostics = std::move(
					runtime.PendingFallingHazardDiagnostics);
				runtime.PendingFallingHazardDiagnostics.clear();
				bot.HazardWaterEgressDiagnostics = std::move(
					runtime.PendingHazardWaterEgressDiagnostics);
				runtime.PendingHazardWaterEgressDiagnostics.clear();
				bot.MoveStallRecoveryEpisodes = std::move(
					runtime.PendingMoveStallRecoveryEpisodeRecords);
				runtime.PendingMoveStallRecoveryEpisodeRecords.clear();
				bot.MoveStallRecoveryDecisions = std::move(
					runtime.PendingMoveStallRecoveryDecisionRecords);
				runtime.PendingMoveStallRecoveryDecisionRecords.clear();
				bot.TargetSelectionRecords = std::move(runtime.PendingTargetSelectionRecords);
				runtime.PendingTargetSelectionRecords.clear();
				bot.HazardDeathPartitionRecords = std::move(
					runtime.PendingHazardDeathPartitionRecords);
				runtime.PendingHazardDeathPartitionRecords.clear();
				bot.WalkingStepPreflightReasonsExact = native.WalkingStepPreflightReasons;
				runtime.LastState = bot;
				runtime.HasLastState = true;
				bots.push_back(std::move(bot));
			}
			return bots;
		}

		void WriteTelemetry(const std::string& type, const std::string& status,
			const std::string& failureReason, uint64_t tick, double simulatedSeconds,
			uint64_t* aiFrameScopeMicroseconds = nullptr)
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
			event.TargetSelectionObserverRequested = Config.IsTargetSelectionObserverEnabled();
			event.TargetSelectionObserverStatus = TargetSelectionObserverStatus;
			event.TargetSelectionObserverReason = TargetSelectionObserverReason;
			event.InventoryDirectReachSupportObserverRequested =
				Config.IsInventoryDirectReachSupportObserverEnabled();
			event.NativePathCommitObserverRequested =
				Config.IsNativePathCommitObserverEnabled();
			event.DirectReachCommandObserverRequested =
				Config.IsDirectReachCommandObserverEnabled();
			if (aiFrameScopeMicroseconds)
			{
				MeasureAiFrameScope(*aiFrameScopeMicroseconds, [&]
				{
					event.Bots = CaptureBotStates();
				});
			}
			else
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
			InitializeNavigationCoverage();
			RegisterTargetSelectionObserver();
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
		std::shared_ptr<File> RouteExecutionTelemetryFile;
		uint64_t TelemetryEventCount = 0;
		uint64_t ShadowTelemetryEventCount = 0;
		uint64_t RouteExecutionTelemetryEventCount = 0;
		std::map<std::string, vec3> RouteExecutionPreviousLocations;
		BotBenchmarkAiFrameTiming AiFrameTiming;
		uint64_t Ticks = 0;
		int ExitCode = 0;
		bool Complete = false;
		std::string FailureReason;
		std::vector<BotBenchmarkActualParticipant> ActualRoster;
		std::map<std::string, QualityParticipantRuntime> QualityParticipants;
		std::vector<std::unique_ptr<ShadowParticipantRuntime>> ShadowParticipants;
		BotBenchmarkDeathAttribution::Coordinator AttributionCoordinator{
			DeathAttributionRecentWindowSeconds };
		std::vector<ActiveAttributionCall> ActiveAttributionCalls;
		std::set<UFunction*> ValidatedAttributionFunctions;
		UFunction* CanonicalTakeDamageFunction = nullptr;
		UFunction* CanonicalAddVelocityFunction = nullptr;
		VMCallHookHandle QualityHookHandle = 0;
		VMCallHookHandle TargetSelectionHookHandle = 0;
		std::map<UFunction*, std::string> ValidatedTargetSelectionFunctions;
		std::vector<ActiveTargetSelectionCall> ActiveTargetSelectionCalls;
		std::string TargetSelectionObserverStatus;
		std::string TargetSelectionObserverReason;
		std::map<std::string, uint32_t> KilledHookDepths;
		std::map<UInventory*, uint32_t> PickupTouchDepths;
		std::unique_ptr<BotBenchmarkQualityObservation::NavigationCoverageAccumulator>
			NavigationCoverageUnion;
		size_t NavigationCoverageCatalogNodeCount = 0;
		uint32_t HitWallHookDepth = 0;
		bool IsUnrealTournamentProfile = false;
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
			OptionalCommandLineArg("--botbench-names"),
			OptionalCommandLineArg("--botbench-harmful-zone-escape"),
			OptionalCommandLineArg("--botbench-walking-preflight-positive-dps-veto"),
			OptionalCommandLineArg("--botbench-hazard-swim-egress"),
			OptionalCommandLineArg("--botbench-hazard-swim-egress-live"),
			OptionalCommandLineArg("--botbench-failed-navigation-avoidance"),
			OptionalCommandLineArg("--botbench-falling-hazard-recovery"),
			OptionalCommandLineArg("--botbench-falling-hazard-recovery-live"),
			OptionalCommandLineArg("--botbench-targetless-move-to-timeout"),
			OptionalCommandLineArg("--botbench-direct-actor-move-toward-timeout"),
			OptionalCommandLineArg("--botbench-target-selection-observer"),
			OptionalCommandLineArg("--botbench-inventory-direct-reach-support-observer"),
			OptionalCommandLineArg("--botbench-inventory-marker-direct-reach-safety"),
			OptionalCommandLineArg("--botbench-native-path-commit-observer"),
			OptionalCommandLineArg("--botbench-direct-reach-command-observer"));
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
	RegisterBotWalkingHitWallCornerFixtureDriver(registry);
	RegisterBotMoveStallRecoveryFixtureDriver(registry);
	RegisterBotInventoryRouteHandoffFixtureDriver(registry);
	RegisterMapCatalogDriver(registry);
}
