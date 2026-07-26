#include "Precomp.h"
#include "BotMoveStallRecoveryFixture.h"

#include "BotBenchmarkRoster.h"
#include "BotControlledMatch.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Runtime/HeadlessDriver.h"
#include "UObject/UActor.h"
#include "UObject/UClass.h"
#include "UObject/ULevel.h"
#include "Utils/CommandLine.h"
#include "Utils/File.h"
#include "Utils/Logger.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <sstream>

namespace
{
	constexpr float FixtureTickSeconds = 0.25f;
	constexpr int StallTicks = 8;
	constexpr float MinimumRecoveryDisplacement = 8.0f;
	constexpr float MaximumStallDisplacement = 4.0f;
	constexpr float DestinationDistance = 2048.0f;

	void Fail(BotMoveStallRecoveryFixtureResult& result, std::string reason)
	{
		if (result.FailureReason.empty())
			result.FailureReason = std::move(reason);
	}

	bool IsSafeWalkingPawn(UPawn* pawn)
	{
		UZoneInfo* zone = pawn ? pawn->FootRegion().Zone : nullptr;
		return pawn && !pawn->bDeleteMe() && pawn->Health() > 0
			&& pawn->Role() == ROLE_Authority && pawn->Physics() == PHYS_Walking
			&& pawn->StateFrame && zone && !zone->bPainZone() && !zone->bWaterZone()
			&& length(zone->ZoneVelocity()) <= 0.001f;
	}

	bool MoveSafeRecoveryDistance(UPawn* pawn, const vec3& origin)
	{
		const std::array<vec3, 8> directions = {
			vec3(1.0f, 0.0f, 0.0f), vec3(-1.0f, 0.0f, 0.0f),
			vec3(0.0f, 1.0f, 0.0f), vec3(0.0f, -1.0f, 0.0f),
			normalize(vec3(1.0f, 1.0f, 0.0f)), normalize(vec3(1.0f, -1.0f, 0.0f)),
			normalize(vec3(-1.0f, 1.0f, 0.0f)), normalize(vec3(-1.0f, -1.0f, 0.0f)) };
		for (float distance : { 8.0f, 16.0f, 24.0f, 32.0f })
		{
			for (const vec3& direction : directions)
			{
				if (!pawn->SetLocation(origin + direction * distance))
					continue;
				pawn->UpdateActorZone();
				if (IsSafeWalkingPawn(pawn)
					&& length(pawn->Location().xy() - origin.xy()) > MaximumStallDisplacement)
					return true;
			}
		}
		return false;
	}

	std::string ResultText(const BotMoveStallRecoveryFixtureResult& result)
	{
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << "schema=surreal-bot-move-stall-recovery-fixture-v3\n"
			<< "ran=" << (result.Ran ? "true" : "false") << "\n"
			<< "passed=" << (result.Passed ? "true" : "false") << "\n"
			<< "targetless_move_to_timeout_mode="
			<< (result.TargetlessTimeoutFixtureMode ? "true" : "false") << "\n"
			<< "direct_actor_move_toward_timeout_mode="
			<< (result.DirectActorFixtureMode ? "true" : "false") << "\n"
			<< "safe_walking_start=" << (result.SafeWalkingStart ? "true" : "false") << "\n"
			<< "targetless_move_to_armed=" << (result.TargetlessMoveToArmed ? "true" : "false") << "\n"
			<< "targetless_timeout_pre_write_state_valid="
			<< (result.TargetlessTimeoutPreWriteStateValid ? "true" : "false") << "\n"
			<< "targetless_timeout_applied="
			<< (result.TargetlessTimeoutApplied ? "true" : "false") << "\n"
			<< "direct_actor_move_toward_armed="
			<< (result.DirectActorMoveTowardArmed ? "true" : "false") << "\n"
			<< "direct_actor_timeout_applied="
			<< (result.DirectActorTimeoutApplied ? "true" : "false") << "\n"
			<< "direct_actor_target_destroy_requested="
			<< (result.DirectActorTargetDestroyRequested ? "true" : "false") << "\n"
			<< "stalled_without_displacement=" << (result.StalledWithoutDisplacement ? "true" : "false") << "\n"
			<< "safe_recovery_relocation=" << (result.SafeRecoveryRelocation ? "true" : "false") << "\n"
			<< "pawn_actor=" << result.PawnActor << "\n"
			<< "direct_actor_target=" << result.DirectActorTarget << "\n"
			<< "direct_actor_target_class=" << result.DirectActorTargetClass << "\n"
			<< "detections=" << result.Detections << "\n"
			<< "episode_starts=" << result.EpisodeStarts << "\n"
			<< "episode_resets=" << result.EpisodeResets << "\n"
			<< "cleared_within_2_seconds=" << result.ClearedWithin2Seconds << "\n"
			<< "cleared_after_2_seconds_within_5_seconds="
			<< result.ClearedAfter2SecondsWithin5Seconds << "\n"
			<< "replanned_within_5_seconds=" << result.ReplannedWithin5Seconds << "\n"
			<< "missed_5_second_deadline=" << result.Missed5SecondDeadline << "\n"
			<< "excluded_intentional_stops=" << result.ExcludedIntentionalStops << "\n"
			<< "censored_life_boundaries=" << result.CensoredLifeBoundaries << "\n"
			<< "censored_run_end=" << result.CensoredRunEnd << "\n"
			<< "unknown=" << result.Unknown << "\n"
			<< "record_overflows=" << result.RecordOverflows << "\n"
			<< "record_count=" << result.Records.size() << "\n"
			<< "failure_reason=" << result.FailureReason << "\n";
		for (size_t index = 0; index < result.Records.size(); index++)
		{
			const auto& record = result.Records[index];
			out << "record_" << index << "_actor=" << record.SourcePawnActor << "\n"
				<< "record_" << index << "_sequence=" << record.Sequence << "\n"
				<< "record_" << index << "_life_id=" << record.LifeId << "\n"
				<< "record_" << index << "_episode_id=" << record.EpisodeId << "\n"
				<< "record_" << index << "_seconds=" << record.SecondsSinceDetection << "\n"
				<< "record_" << index << "_outcome=" << static_cast<int>(record.Outcome) << "\n";
		}
		for (size_t index = 0; index < result.DecisionRecords.size(); index++)
		{
			const auto& record = result.DecisionRecords[index];
			out << "decision_" << index << "_actor=" << record.SourcePawnActor << "\n"
				<< "decision_" << index << "_sequence=" << record.Sequence << "\n"
				<< "decision_" << index << "_life_id=" << record.LifeId << "\n"
				<< "decision_" << index << "_episode_id=" << record.EpisodeId << "\n"
				<< "decision_" << index << "_latent_mode=" << static_cast<int>(record.LatentMode) << "\n"
				<< "decision_" << index << "_decision=" << static_cast<int>(record.Decision) << "\n"
				<< "decision_" << index << "_target_live="
				<< (record.MoveTargetLive ? "true" : "false") << "\n"
				<< "decision_" << index << "_target_name=" << record.MoveTargetName << "\n"
				<< "decision_" << index << "_target_class=" << record.MoveTargetClass << "\n"
				<< "decision_" << index << "_move_timer=" << record.MoveTimer << "\n";
		}
		return out.str();
	}

	class BotMoveStallRecoveryFixtureDriver final : public HeadlessDriver
	{
	public:
		explicit BotMoveStallRecoveryFixtureDriver(Engine& engine) : EngineRef(engine) {}

		HeadlessDriverConfig GetConfig() const override { return { .MaxTicks = 1 }; }

		void Start() override
		{
			BotMoveStallRecoveryFixtureConfig config;
			config.URL = commandline ? commandline->GetArg("", "--botbench-url") : std::string();
			const std::string difficulty = commandline
				? commandline->GetArg("", "--botbench-difficulty") : std::string();
			if (!difficulty.empty())
			{
				try { config.ExternalSkill = std::stoi(difficulty); }
				catch (const std::exception&) { Result.FailureReason = "move-stall fixture difficulty must be an integer"; Complete = true; return; }
			}
			const std::string directActorMode = commandline
				? commandline->GetArg("", "--botbench-fixture-direct-actor-timeout") : std::string();
			const std::string targetlessMode = commandline
				? commandline->GetArg("", "--botbench-fixture-targetless-timeout") : std::string();
			if (!targetlessMode.empty())
			{
				if (targetlessMode != "0" && targetlessMode != "1")
				{
					Result.FailureReason = "targetless fixture mode must be 0 or 1";
					Complete = true;
					return;
				}
				config.TargetlessMoveToTimeout = targetlessMode == "1";
			}
			if (!directActorMode.empty())
			{
				if (directActorMode != "0" && directActorMode != "1")
				{
					Result.FailureReason = "direct-actor fixture mode must be 0 or 1";
					Complete = true;
					return;
				}
				config.DirectActorMoveTowardTimeout = directActorMode == "1";
			}
			if (config.TargetlessMoveToTimeout && config.DirectActorMoveTowardTimeout)
			{
				Result.FailureReason = "move-stall fixture action modes are mutually exclusive";
				Complete = true;
				return;
			}
			Result = BotMoveStallRecoveryFixture::Run(EngineRef, config);
			const std::string output = commandline
				? commandline->GetArg("", "--botbench-output") : std::string();
			if (!output.empty())
			{
				std::filesystem::create_directories(output);
				File::write_all_text((std::filesystem::path(output)
					/ "move-stall-recovery-fixture-result.txt").string(), ResultText(Result));
			}
			LogMessage("Move-stall recovery fixture result: "
				+ std::string(Result.Passed ? "passed" : "failed"));
			Complete = true;
		}

		bool IsComplete() const override { return Complete; }
		void Tick(const DeterministicFrameTime&) override {}
		int Finish(const HeadlessRunSummary&) override { return Result.Passed ? 0 : 1; }

	private:
		Engine& EngineRef;
		BotMoveStallRecoveryFixtureResult Result;
		bool Complete = false;
	};
}

BotMoveStallRecoveryFixtureResult BotMoveStallRecoveryFixture::Run(
	Engine& engine, const BotMoveStallRecoveryFixtureConfig& config)
{
	BotMoveStallRecoveryFixtureResult result;
	result.TargetlessTimeoutFixtureMode = config.TargetlessMoveToTimeout;
	result.DirectActorFixtureMode = config.DirectActorMoveTowardTimeout;
	UPawn* pawn = nullptr;
	float originalAccelRate = 0.0f;
	float originalDesiredSpeed = 0.0f;
	float originalTimerRate = 0.0f;
	float originalTimerCounter = 0.0f;
	vec3 originalLocation;
	vec3 originalVelocity;
	vec3 originalAcceleration;
	int originalPhysics = PHYS_None;
	LatentRunState originalLatentState = LatentRunState::Continue;
	vec3 originalDestination;
	vec3 originalFocus;
	UActor* originalMoveTarget = nullptr;
	float originalMoveTimer = 0.0f;
	bool originalTickEnabled = false;
	bool originalUpdateTacticsEnabled = false;
	bool originalTargetlessTimeoutEnabled = false;
	bool originalDirectActorTimeoutEnabled = false;
	UActor* directActorTarget = nullptr;
	vec3 fixtureOrigin;
	try
	{
		originalTargetlessTimeoutEnabled = engine.IsBotBenchmarkTargetlessMoveToTimeoutEnabled();
		originalDirectActorTimeoutEnabled = engine.IsBotBenchmarkDirectActorMoveTowardTimeoutEnabled();
		if (config.URL.empty())
			throw std::runtime_error("move-stall fixture URL is required");
		if (config.TargetlessMoveToTimeout && config.DirectActorMoveTowardTimeout)
			throw std::runtime_error("move-stall fixture action modes are mutually exclusive");
		if (originalTargetlessTimeoutEnabled)
			throw std::runtime_error("move-stall fixture requires the targetless MoveTo timeout experiment to be disabled");
		engine.SetBotBenchmarkTargetlessMoveToTimeoutEnabled(config.TargetlessMoveToTimeout);
		engine.SetBotBenchmarkDirectActorMoveTowardTimeoutEnabled(
			config.DirectActorMoveTowardTimeout);
		const BotBenchmarkRoster roster = BotBenchmarkRoster::Parse(
			std::string("1"), {}, {}, config.ExternalSkill);
		const BotControlledMatchResult match = BotControlledMatch::Setup(engine, config.URL, roster);
		if (match.Participants.size() != 1 || !match.Participants.front().Pawn)
			throw std::runtime_error("controlled move-stall fixture did not create one live bot");
		pawn = match.Participants.front().Pawn;
		result.PawnActor = pawn->Name.ToString();
		originalAccelRate = pawn->AccelRate();
		originalDesiredSpeed = pawn->DesiredSpeed();
		originalTimerRate = pawn->TimerRate();
		originalTimerCounter = pawn->TimerCounter();
		originalLocation = pawn->Location();
		originalVelocity = pawn->Velocity();
		originalAcceleration = pawn->Acceleration();
		originalPhysics = pawn->Physics();
		originalLatentState = pawn->StateFrame
			? pawn->StateFrame->LatentState : LatentRunState::Continue;
		originalDestination = pawn->Destination();
		originalFocus = pawn->Focus();
		originalMoveTarget = pawn->MoveTarget();
		originalMoveTimer = pawn->MoveTimer();
		originalTickEnabled = pawn->IsEventEnabled(EventName::Tick);
		originalUpdateTacticsEnabled = pawn->IsEventEnabled(EventName::UpdateTactics);
		bool foundSafeStart = false;
		for (UActor* actor : engine.Level->Actors)
		{
			if (!actor || actor->bDeleteMe() || !actor->IsA("PlayerStart"))
				continue;
			if (!pawn->SetLocation(actor->Location()))
				continue;
			pawn->UpdateActorZone();
			pawn->SetPhysics(PHYS_Walking);
			if (IsSafeWalkingPawn(pawn))
			{
				foundSafeStart = true;
				break;
			}
		}
		if (!foundSafeStart)
			throw std::runtime_error("fixture could not establish a safe walking PlayerStart context");
		result.SafeWalkingStart = true;
		fixtureOrigin = pawn->Location();
		pawn->DisableEvent(ToNameString(EventName::Tick));
		pawn->DisableEvent(ToNameString(EventName::UpdateTactics));
		pawn->TimerRate() = 0.0f;
		pawn->TimerCounter() = 0.0f;
		pawn->AccelRate() = 0.0f;
		pawn->Velocity() = vec3(0.0f);
		pawn->Acceleration() = vec3(0.0f);
		if (config.DirectActorMoveTowardTimeout)
		{
			if (!engine.packages)
				throw std::runtime_error("direct-actor fixture has no package manager");
			UClass* targetClass = engine.packages->FindClass("Engine.BlockAll");
			if (!targetClass)
				throw std::runtime_error("direct-actor fixture could not resolve Engine.BlockAll");
			directActorTarget = pawn->Spawn(targetClass, {}, {},
				fixtureOrigin + vec3(DestinationDistance, 0.0f, 0.0f), Rotator());
			if (!directActorTarget || directActorTarget->bDeleteMe())
				throw std::runtime_error("direct-actor fixture could not spawn its target");
			result.DirectActorTarget = directActorTarget->Name.ToString();
			result.DirectActorTargetClass = directActorTarget->Class
				? directActorTarget->Class->Name.ToString() : std::string();
			pawn->MoveToward(directActorTarget, 1.0f);
			if (!pawn->StateFrame || pawn->StateFrame->LatentState != LatentRunState::MoveToward
				|| pawn->MoveTarget() != directActorTarget || pawn->MoveTimer() <= 2.0f)
			{
				throw std::runtime_error("fixture failed to arm a finite direct-actor MoveToward command");
			}
			result.DirectActorMoveTowardArmed = true;
		}
		else
		{
			pawn->MoveTo(fixtureOrigin + vec3(DestinationDistance, 0.0f, 0.0f), 1.0f);
			if (!pawn->StateFrame || pawn->StateFrame->LatentState != LatentRunState::MoveTo
				|| pawn->MoveTarget() || pawn->MoveTimer() <= 2.0f)
				throw std::runtime_error("fixture failed to arm a finite targetless MoveTo latent command");
			result.TargetlessMoveToArmed = true;
		}

		bool targetlessActionObserved = false;
		for (int index = 0; index < StallTicks; index++)
		{
			if (config.TargetlessMoveToTimeout)
			{
				// The native latent command must still be live immediately before the
				// watchdog tick. Together with disabled script Tick/Timer callbacks this
				// rules out natural or script expiry as the source of the timeout.
				if (!pawn->StateFrame || pawn->StateFrame->LatentState != LatentRunState::MoveTo
					|| pawn->MoveTarget() || !std::isfinite(pawn->MoveTimer())
					|| pawn->MoveTimer() <= 0.0f)
				{
					throw std::runtime_error("targetless timeout fixture observed natural or script expiry before watchdog action");
				}
			}
			pawn->Tick(FixtureTickSeconds);
			if (config.TargetlessMoveToTimeout
				&& pawn->MoveStallTargetlessMoveToTimeoutCount() == 1)
			{
				targetlessActionObserved = true;
				break;
			}
		}
		const float stallDistance = length(pawn->Location().xy() - fixtureOrigin.xy());
		result.StalledWithoutDisplacement = stallDistance <= MaximumStallDisplacement;
		if (!result.StalledWithoutDisplacement || pawn->MoveStallDetectionCount() != 1
			|| pawn->MoveStallRecoveryEpisodeStartCount() != 1)
			throw std::runtime_error("fixture did not produce exactly one stationary move-stall detection");

		if (config.TargetlessMoveToTimeout)
		{
			if (!targetlessActionObserved)
				throw std::runtime_error("fixture did not observe the targetless MoveTo timeout action");
			result.DecisionRecords = pawn->DrainMoveStallRecoveryDecisionRecords();
			const bool exactTargetlessDecision = result.DecisionRecords.size() == 1
				&& result.DecisionRecords[0].SourcePawnActor == result.PawnActor
				&& result.DecisionRecords[0].Sequence > 0
				&& result.DecisionRecords[0].LifeId > 0
				&& result.DecisionRecords[0].EpisodeId > 0
				&& result.DecisionRecords[0].LatentMode
					== PawnMovement::MoveStallLatentMode::MoveTo
				&& result.DecisionRecords[0].Decision
					== PawnMovement::MoveStallRecoveryDecision::TargetlessTimeout
				&& !result.DecisionRecords[0].MoveTargetKnown
				&& !result.DecisionRecords[0].MoveTargetLive
				&& std::isfinite(result.DecisionRecords[0].MoveTimer)
				&& result.DecisionRecords[0].MoveTimer > 0.0f;
			result.TargetlessTimeoutPreWriteStateValid = exactTargetlessDecision;
			result.TargetlessTimeoutApplied = pawn->MoveStallTargetlessMoveToTimeoutCount() == 1
				&& pawn->MoveStallForcedReplanCount() == 1 && pawn->MoveTimer() < 0.0f
				&& pawn->Acceleration() == vec3(0.0f);
			if (!result.TargetlessTimeoutPreWriteStateValid || !result.TargetlessTimeoutApplied
				|| pawn->MoveStallNavigationForcedReplanCount() != 0
				|| pawn->MoveStallDirectActorMoveTowardTimeoutCount() != 0)
			{
				throw std::runtime_error("fixture targetless timeout counters or pre-write decision record did not reconcile");
			}
		}
		else if (config.DirectActorMoveTowardTimeout)
		{
			result.DecisionRecords = pawn->DrainMoveStallRecoveryDecisionRecords();
			result.DirectActorTimeoutApplied = pawn->MoveStallDirectActorMoveTowardTimeoutCount() == 1
				&& pawn->MoveStallForcedReplanCount() == 1 && pawn->MoveTimer() < 0.0f
				&& pawn->Acceleration() == vec3(0.0f);
			const bool exactDirectActorDecision = result.DecisionRecords.size() == 1
				&& result.DecisionRecords[0].SourcePawnActor == result.PawnActor
				&& result.DecisionRecords[0].Sequence > 0
				&& result.DecisionRecords[0].LifeId > 0
				&& result.DecisionRecords[0].EpisodeId > 0
				&& result.DecisionRecords[0].LatentMode
					== PawnMovement::MoveStallLatentMode::MoveToward
				&& result.DecisionRecords[0].Decision
					== PawnMovement::MoveStallRecoveryDecision::DirectActorMoveTowardTimeout
				&& result.DecisionRecords[0].MoveTargetLive
				&& result.DecisionRecords[0].MoveTargetName == result.DirectActorTarget
				&& result.DecisionRecords[0].MoveTargetClass == result.DirectActorTargetClass
				&& result.DecisionRecords[0].MoveTimer > 0.0f;
			if (!result.DirectActorTimeoutApplied || !exactDirectActorDecision
				|| pawn->MoveStallNavigationForcedReplanCount() != 0
				|| pawn->MoveStallTargetlessMoveToTimeoutCount() != 0)
			{
				throw std::runtime_error("fixture direct-actor timeout counters or decision record did not reconcile");
			}
		}
		else
		{
			result.SafeRecoveryRelocation = MoveSafeRecoveryDistance(pawn, fixtureOrigin);
			if (!result.SafeRecoveryRelocation)
				throw std::runtime_error("fixture could not make a safe recovery displacement");
			pawn->Tick(FixtureTickSeconds);
		}
		result.Ran = true;
		result.Detections = pawn->MoveStallDetectionCount();
		result.EpisodeStarts = pawn->MoveStallRecoveryEpisodeStartCount();
		result.EpisodeResets = pawn->MoveStallEpisodeResetCount();
		result.ClearedWithin2Seconds = pawn->MoveStallRecoveryClearedWithin2SecondsCount();
		result.ClearedAfter2SecondsWithin5Seconds = pawn->MoveStallRecoveryClearedAfter2SecondsWithin5SecondsCount();
		result.ReplannedWithin5Seconds = pawn->MoveStallRecoveryReplannedWithin5SecondsCount();
		result.Missed5SecondDeadline = pawn->MoveStallRecoveryMissed5SecondDeadlineCount();
		result.ExcludedIntentionalStops = pawn->MoveStallRecoveryExcludedIntentionalStopCount();
		result.CensoredLifeBoundaries = pawn->MoveStallRecoveryCensoredLifeBoundaryCount();
		result.CensoredRunEnd = pawn->MoveStallRecoveryCensoredRunEndCount();
		result.Unknown = pawn->MoveStallRecoveryUnknownCount();
		result.RecordOverflows = pawn->MoveStallRecoveryEpisodeRecordOverflowCount();
		result.Records = pawn->DrainMoveStallRecoveryEpisodeRecords();
		if (!config.TargetlessMoveToTimeout && !config.DirectActorMoveTowardTimeout)
		{
			const bool exactCounters = result.Detections == 1 && result.EpisodeStarts == 1
				&& result.EpisodeResets == 1 && result.ClearedWithin2Seconds == 1
				&& result.ClearedAfter2SecondsWithin5Seconds == 0 && result.ReplannedWithin5Seconds == 0
				&& result.Missed5SecondDeadline == 0 && result.ExcludedIntentionalStops == 0
				&& result.CensoredLifeBoundaries == 0 && result.CensoredRunEnd == 0
				&& result.Unknown == 0 && result.RecordOverflows == 0;
			const bool exactRecord = result.Records.size() == 1
				&& result.Records[0].SourcePawnActor == result.PawnActor
				&& result.Records[0].Sequence > 0 && result.Records[0].LifeId > 0
				&& result.Records[0].EpisodeId > 0
				&& std::isfinite(result.Records[0].SecondsSinceDetection)
				&& result.Records[0].SecondsSinceDetection > 0.0f
				&& result.Records[0].SecondsSinceDetection <= 2.0f
				&& result.Records[0].Outcome
					== PawnMovement::MoveStallRecoveryEpisodeOutcome::ClearedWithin2Seconds;
			if (!exactCounters || !exactRecord)
				throw std::runtime_error("fixture terminal recovery counters and record did not reconcile");
		}
		result.Passed = true;
	}
	catch (const std::exception& error)
	{
		Fail(result, error.what());
	}

	if (pawn && !pawn->bDeleteMe())
	{
		pawn->AccelRate() = originalAccelRate;
		pawn->DesiredSpeed() = originalDesiredSpeed;
		pawn->Velocity() = originalVelocity;
		pawn->Acceleration() = originalAcceleration;
		pawn->TimerRate() = originalTimerRate;
		pawn->TimerCounter() = originalTimerCounter;
		pawn->SetPhysics(originalPhysics);
		pawn->Destination() = originalDestination;
		pawn->Focus() = originalFocus;
		pawn->MoveTarget() = originalMoveTarget;
		pawn->MoveTimer() = originalMoveTimer;
		if (pawn->StateFrame)
			pawn->StateFrame->LatentState = originalLatentState;
		if (originalTickEnabled) pawn->EnableEvent(ToNameString(EventName::Tick));
		else pawn->DisableEvent(ToNameString(EventName::Tick));
		if (originalUpdateTacticsEnabled) pawn->EnableEvent(ToNameString(EventName::UpdateTactics));
		else pawn->DisableEvent(ToNameString(EventName::UpdateTactics));
		pawn->SetLocation(originalLocation);
		pawn->UpdateActorZone();
	}
	if (directActorTarget && !directActorTarget->bDeleteMe())
	{
		directActorTarget->Destroy();
		result.DirectActorTargetDestroyRequested = true;
	}
	engine.SetBotBenchmarkDirectActorMoveTowardTimeoutEnabled(originalDirectActorTimeoutEnabled);
	engine.SetBotBenchmarkTargetlessMoveToTimeoutEnabled(originalTargetlessTimeoutEnabled);
	return result;
}

void RegisterBotMoveStallRecoveryFixtureDriver(HeadlessDriverRegistry& registry)
{
	if (!registry.Contains("bot-move-stall-recovery-fixture"))
	{
		registry.Register("bot-move-stall-recovery-fixture", [](Engine& engine)
		{
			return std::make_unique<BotMoveStallRecoveryFixtureDriver>(engine);
		});
	}
}
