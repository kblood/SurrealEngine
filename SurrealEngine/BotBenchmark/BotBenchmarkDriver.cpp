#include "Precomp.h"
#include "BotBenchmarkDriver.h"
#include "BotControlledMatch.h"
#include "BotBenchmarkGameProfile.h"
#include "BotBenchmarkDeathAttributionCoordinator.h"
#include "BotBenchmarkDeathAttributionHookContract.h"
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
#include "UObject/UProperty.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
namespace
{
	constexpr double DeathAttributionRecentWindowSeconds = 2.0;

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
			EngineRef.SetBotBenchmarkFailedNavigationAvoidanceEnabled(
				Config.IsFailedNavigationAvoidanceEnabled());
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
			uint64_t DamageTakenExact = 0;
			uint64_t DamageTakenFromOtherParticipantsExact = 0;
			uint64_t DamageTakenFromSelfExact = 0;
			uint64_t DamageTakenFromNonParticipantsExact = 0;
			uint64_t DamageDealtToOtherParticipantsExact = 0;
			BotBenchmarkDriverDetail::DeathAttributionCounters DeathAttribution;
			uint64_t HitWallEventsExact = 0;
			BotBenchmarkDriverDetail::NativePawnCounterEpoch NativeCounterEpoch;
			BotBenchmarkDriverDetail::NativePawnCounters NativeCounterTotals;
			std::vector<PawnMovement::WalkingStepPreflightDiagnosticRecord>
				PendingWalkingStepPreflightDiagnostics;
			std::vector<PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord>
				PendingWalkingStepPreflightPositiveDpsVetoActions;
			std::vector<PawnMovement::FallingParityRealizedRecord>
				PendingFallingParityRealizedRecords;
			std::vector<PawnMovement::FallingHazardDiagnosticRecord>
				PendingFallingHazardDiagnostics;
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
			counters.MoveStallDetections = pawn->MoveStallDetectionCount();
			counters.MoveStallEpisodeResets = pawn->MoveStallEpisodeResetCount();
			counters.MoveStallForcedReplans = pawn->MoveStallForcedReplanCount();
			counters.MoveStallNavigationForcedReplans =
				pawn->MoveStallNavigationForcedReplanCount();
			counters.MoveStallTargetlessMoveToTimeouts =
				pawn->MoveStallTargetlessMoveToTimeoutCount();
			counters.MoveStallEligibleSeconds = pawn->MoveStallEligibleSeconds();
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
			counters.HazardSwimEgressFallingPreMoveAnchorCaptures =
				pawn->HazardSwimEgressFallingPreMoveAnchorCaptureCount();
				counters.HazardSwimEgressFallingPreMoveAnchorUses =
					pawn->HazardSwimEgressFallingPreMoveAnchorUseCount();
				counters.HazardSwimEgressLiveApplies = pawn->HazardSwimEgressLiveApplyCount();
				counters.HazardSwimEgressLiveActiveTicks = pawn->HazardSwimEgressLiveActiveTickCount();
				counters.HazardSwimEgressLiveProbeRejected = pawn->HazardSwimEgressLiveProbeRejectedCount();
				counters.HazardSwimEgressLiveSuccessfulExits = pawn->HazardSwimEgressLiveSuccessfulExitCount();
				counters.HazardSwimEgressDirectNavProbes = pawn->HazardSwimEgressDirectNavProbeCount();
				counters.HazardSwimEgressDirectNavSafeCandidates = pawn->HazardSwimEgressDirectNavSafeCandidateCount();
				counters.HazardSwimEgressDirectNavBestCandidateName =
					pawn->HazardSwimEgressDirectNavBestCandidateName();
			counters.HazardSwimEgressAnchorKnown = pawn->HasHazardSwimEgressAnchor();
			counters.HazardSwimEgressAnchorSource = pawn->HazardSwimEgressAnchorSourceName();
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

		void RecordKilled(UPawn* killer, UPawn* victim)
		{
			const std::string victimIdentity = PawnIdentity(victim);
			auto victimRuntime = QualityParticipants.find(victimIdentity);
			if (victimRuntime != QualityParticipants.end())
			{
				QualityParticipantRuntime& counters = victimRuntime->second;
				victim->RecordHazardSwimEgressDeath();
				victim->FinishFallingHazardDeath();
				victim->FinishFallingParityRealizedTrace(
					PawnMovement::FallingParityRealizedOutcome::Died);
				AccumulateNativePawnCounters(victimIdentity, counters, victim,
					BotBenchmarkDriverDetail::NativePawnCounterSample::DeathFlush);
				auto diagnostics = victim->DrainWalkingStepPreflightDiagnostics();
				auto vetoActions = victim->DrainWalkingStepPreflightPositiveDpsVetoActions();
				auto parityRecords = victim->DrainFallingParityRealizedRecords();
				auto hazardDiagnostics = victim->DrainFallingHazardDiagnostics();
				victim->EndWalkingStepPreflightLife();
				counters.PendingWalkingStepPreflightDiagnostics.insert(
					counters.PendingWalkingStepPreflightDiagnostics.end(),
					std::make_move_iterator(diagnostics.begin()),
					std::make_move_iterator(diagnostics.end()));
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
				break;
			}
			AcceptCoordinatorStatus(status, "hook cleanup");
		}

		bool AddDamageCounter(uint64_t& counter, uint64_t amount,
			const char* name)
		{
			if (amount > std::numeric_limits<uint64_t>::max() - counter)
			{
				Fail(std::string("damage telemetry counter overflow: ") + name);
				return false;
			}
			counter += amount;
			return true;
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
						return [this, victimIdentity]() { FinishKilledCall(victimIdentity); };
					if (entered.IsOutermost != outermost)
					{
						Fail("death attribution Killed victim depth disagreed with coordinator");
						AttributionCoordinator.ExitKilled(entered.Token);
						return [this, victimIdentity]() { FinishKilledCall(victimIdentity); };
					}
					ActiveAttributionCalls.push_back({ function, instance, victimIdentity, entered.Token,
						AttributionScopeKind::Killed });
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
					}
				}
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
				bot.PhysicsMode.clear();
				bot.LatentAction.clear();
				bot.AccelerationX = bot.AccelerationY = bot.AccelerationZ = 0.0;
				bot.DestinationX = bot.DestinationY = bot.DestinationZ = 0.0;
				bot.MoveTimer = 0.0;
				bot.MoveTargetIdentity.clear();
				bot.MoveTargetName.clear();
				if (pawn)
				{
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
				bot.MoveStallEligibleSeconds = native.MoveStallEligibleSeconds;
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
				bot.HazardSwimEgressFallingPreMoveAnchorCapturesExact =
					native.HazardSwimEgressFallingPreMoveAnchorCaptures;
				bot.HazardSwimEgressFallingPreMoveAnchorUsesExact =
					native.HazardSwimEgressFallingPreMoveAnchorUses;
				bot.HazardSwimEgressLiveAppliesExact = native.HazardSwimEgressLiveApplies;
				bot.HazardSwimEgressLiveActiveTicksExact = native.HazardSwimEgressLiveActiveTicks;
				bot.HazardSwimEgressLiveProbeRejectedExact = native.HazardSwimEgressLiveProbeRejected;
				bot.HazardSwimEgressLiveSuccessfulExitsExact = native.HazardSwimEgressLiveSuccessfulExits;
				bot.HazardSwimEgressDirectNavProbesExact = native.HazardSwimEgressDirectNavProbes;
				bot.HazardSwimEgressDirectNavSafeCandidatesExact = native.HazardSwimEgressDirectNavSafeCandidates;
				bot.HazardSwimEgressDirectNavBestCandidateName = native.HazardSwimEgressDirectNavBestCandidateName;
				bot.HazardSwimEgressAnchorKnown = native.HazardSwimEgressAnchorKnown;
				bot.HazardSwimEgressAnchorSource = native.HazardSwimEgressAnchorSource;
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
				if (pawn)
				{
					auto diagnostics = pawn->DrainWalkingStepPreflightDiagnostics();
					runtime.PendingWalkingStepPreflightDiagnostics.insert(
						runtime.PendingWalkingStepPreflightDiagnostics.end(),
						std::make_move_iterator(diagnostics.begin()),
						std::make_move_iterator(diagnostics.end()));
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
				}
				bot.WalkingStepPreflightDiagnostics = std::move(
					runtime.PendingWalkingStepPreflightDiagnostics);
				runtime.PendingWalkingStepPreflightDiagnostics.clear();
				bot.WalkingStepPreflightPositiveDpsVetoActions = std::move(
					runtime.PendingWalkingStepPreflightPositiveDpsVetoActions);
				runtime.PendingWalkingStepPreflightPositiveDpsVetoActions.clear();
				bot.FallingParityRealizedRecords = std::move(
					runtime.PendingFallingParityRealizedRecords);
				runtime.PendingFallingParityRealizedRecords.clear();
				bot.VerticalPainColumnDiagnostics = std::move(
					runtime.PendingFallingHazardDiagnostics);
				runtime.PendingFallingHazardDiagnostics.clear();
				bot.WalkingStepPreflightReasonsExact = native.WalkingStepPreflightReasons;
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
		BotBenchmarkDeathAttribution::Coordinator AttributionCoordinator{
			DeathAttributionRecentWindowSeconds };
		std::vector<ActiveAttributionCall> ActiveAttributionCalls;
		std::set<UFunction*> ValidatedAttributionFunctions;
		UFunction* CanonicalTakeDamageFunction = nullptr;
		UFunction* CanonicalAddVelocityFunction = nullptr;
		VMCallHookHandle QualityHookHandle = 0;
		std::map<std::string, uint32_t> KilledHookDepths;
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
			OptionalCommandLineArg("--botbench-failed-navigation-avoidance"));
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
