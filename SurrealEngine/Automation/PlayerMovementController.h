#pragma once

#include "AutomationProtocol.h"
#include "Input/InputCommandTarget.h"

#include <cstdint>
#include <string>

namespace Automation
{
	enum class MovementStatus
	{
		Running,
		Arrived,
		Stuck,
		TimedOut,
		Invalid
	};

	struct MovementControllerConfig
	{
		double HeadingDeadzoneRadians = 0.035;
		double AdvanceHeadingRadians = 0.7853981633974483;
		double FullTurnRadians = 0.7853981633974483;
		double ProgressEpsilon = 1.0;
		double VerticalTolerance = 64.0;
		uint64_t StuckTicks = 120;
		uint64_t ObstacleProbeTicks = 30;
		uint64_t ObstacleStrafeTicks = 45;
		uint32_t MaximumObstacleRecoveries = 1;
	};

	struct MovementObservation
	{
		uint64_t Tick = 0;
		WorldPoint Position;
		double YawRadians = 0.0;
	};

	struct MovementStep
	{
		MovementStatus Status = MovementStatus::Invalid;
		uint64_t Tick = 0;
		double Forward = 0.0;
		double Strafe = 0.0;
		double Turn = 0.0;
		double HorizontalDistance = 0.0;
		double VerticalDistance = 0.0;
		std::string Reason;
	};

	enum class AimStatus
	{
		Aligning,
		Aligned,
		Invalid
	};

	struct AimObservation
	{
		WorldPoint EyePosition;
		double ViewYawRadians = 0.0;
		double ViewPitchRadians = 0.0;
	};

	struct AimStep
	{
		AimStatus Status = AimStatus::Invalid;
		double Turn = 0.0;
		double LookUp = 0.0;
		double YawErrorRadians = 0.0;
		double PitchErrorRadians = 0.0;
	};

	std::optional<double> ComputeStockInteractionArrivalRadius(
		const WorldPoint& pawnPosition, double eyeHeight,
		const WorldPoint& target, double maximumInteractionDistance,
		double safetyFraction = 0.9);

	class PlayerAimController
	{
	public:
		AimStep Update(const AimObservation& observation,
			const WorldPoint& target) const;
	};

	class PlayerMovementController
	{
	public:
		explicit PlayerMovementController(MovementControllerConfig config = {});

		ValidationResult Start(const AutomationCommand& command,
			const std::optional<TargetSnapshot>& resolvedTarget,
			uint64_t currentTick, uint64_t observationRevision);
		ValidationResult Retarget(const WorldPoint& target, double arrivalRadius,
			uint64_t currentTick);
		ValidationResult Abort(const AutomationCommand& abortCommand,
			uint64_t currentTick, uint64_t observationRevision);
		MovementStep Update(const MovementObservation& observation);
		void Reset();

		bool IsActive() const { return Active; }
		const std::string& CommandId() const { return ActiveCommandId; }

	private:
		MovementControllerConfig Config;
		bool Active = false;
		std::string ActiveCommandId;
		WorldPoint Target;
		double ArrivalRadius = 0.0;
		uint64_t DeadlineTick = 0;
		uint64_t LastTick = 0;
		uint64_t LastProgressTick = 0;
		double BestHorizontalDistance = 0.0;
		uint64_t ObstacleStrafeTicksRemaining = 0;
		uint32_t ObstacleRecoveryCount = 0;
		double ObstacleStrafeDirection = 1.0;
	};

	class PlayerMovementInputAdapter
	{
	public:
		void Apply(const MovementStep& step, InputCommandTarget& target);
		void Release(InputCommandTarget& target);

		bool IsActive() const { return Active; }

	private:
		// UE1 PlayerPawn scripts multiply aTurn by DeltaTime before applying it to
		// the 16-bit rotator. 4096 produces a bounded 172.8 degrees/second in the
		// stock walking path (32 * 0.24 * 4096 rotator units/second).
		static constexpr double TurnAxisSpeed = 4096.0;
		static constexpr int32_t ForwardControl = 1001;
		static constexpr int32_t StrafeControl = 1002;
		static constexpr int32_t TurnControl = 1003;
		bool Active = false;
	};

	class PlayerAimInputAdapter
	{
	public:
		void Apply(const AimStep& step, InputCommandTarget& target);
		void Release(InputCommandTarget& target);

		bool IsActive() const { return Active; }

	private:
		static constexpr int32_t TurnControl = 1011;
		static constexpr int32_t LookUpControl = 1012;
		bool Active = false;
	};
}
