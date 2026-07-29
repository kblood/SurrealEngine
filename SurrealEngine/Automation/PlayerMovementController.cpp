#include "PlayerMovementController.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Automation
{
	namespace
	{
		static constexpr double Pi = 3.14159265358979323846;
		static constexpr double AimDeadzoneRadians = 0.017453292519943295;
		static constexpr double AimFullScaleRadians = 0.5235987755982988;

		bool IsFinitePoint(const WorldPoint& point)
		{
			return std::isfinite(point.X) && std::isfinite(point.Y) && std::isfinite(point.Z) &&
				std::abs(point.X) <= MaximumCoordinateMagnitude &&
				std::abs(point.Y) <= MaximumCoordinateMagnitude &&
				std::abs(point.Z) <= MaximumCoordinateMagnitude;
		}

		double NormalizeAngle(double angle)
		{
			return std::remainder(angle, 2.0 * Pi);
		}

		bool SupportsMovement(CommandKind kind)
		{
			return kind == CommandKind::WalkToPoint || kind == CommandKind::WalkToActor ||
				kind == CommandKind::AcquireItem || kind == CommandKind::Interact;
		}

		MovementStep Terminal(MovementStatus status, uint64_t tick,
			double horizontalDistance, double verticalDistance, std::string reason)
		{
			MovementStep step;
			step.Status = status;
			step.Tick = tick;
			step.HorizontalDistance = horizontalDistance;
			step.VerticalDistance = verticalDistance;
			step.Reason = std::move(reason);
			return step;
		}
	}

	std::optional<double> ComputeStockInteractionArrivalRadius(
		const WorldPoint& pawnPosition, double eyeHeight,
		const WorldPoint& target, double maximumInteractionDistance,
		double safetyFraction)
	{
		if (!IsFinitePoint(pawnPosition) || !IsFinitePoint(target) || !std::isfinite(eyeHeight) ||
			!std::isfinite(maximumInteractionDistance) || maximumInteractionDistance <= 0.0 ||
			maximumInteractionDistance > MaximumArrivalRadius ||
			!std::isfinite(safetyFraction) || safetyFraction <= 0.0 || safetyFraction >= 1.0)
			return std::nullopt;
		const double usableDistance = maximumInteractionDistance * safetyFraction;
		const double eyeVerticalDelta = target.Z - (pawnPosition.Z + eyeHeight);
		if (std::abs(eyeVerticalDelta) >= usableDistance)
			return std::nullopt;
		const double horizontalDistance = std::sqrt(
			usableDistance * usableDistance - eyeVerticalDelta * eyeVerticalDelta);
		const double pawnVerticalDelta = target.Z - pawnPosition.Z;
		return std::sqrt(horizontalDistance * horizontalDistance +
			pawnVerticalDelta * pawnVerticalDelta);
	}

	AimStep PlayerAimController::Update(const AimObservation& observation,
		const WorldPoint& target) const
	{
		if (!IsFinitePoint(observation.EyePosition) || !IsFinitePoint(target) ||
			!std::isfinite(observation.ViewYawRadians) ||
			!std::isfinite(observation.ViewPitchRadians))
			return {};

		const double deltaX = target.X - observation.EyePosition.X;
		const double deltaY = target.Y - observation.EyePosition.Y;
		const double deltaZ = target.Z - observation.EyePosition.Z;
		const double horizontalDistance = std::hypot(deltaX, deltaY);
		if (horizontalDistance <= 0.0001 && std::abs(deltaZ) <= 0.0001)
			return {};

		AimStep step;
		step.YawErrorRadians = NormalizeAngle(
			std::atan2(deltaY, deltaX) - observation.ViewYawRadians);
		step.PitchErrorRadians = NormalizeAngle(
			std::atan2(deltaZ, horizontalDistance) - observation.ViewPitchRadians);
		if (std::abs(step.YawErrorRadians) <= AimDeadzoneRadians &&
			std::abs(step.PitchErrorRadians) <= AimDeadzoneRadians)
		{
			step.Status = AimStatus::Aligned;
			return step;
		}

		step.Status = AimStatus::Aligning;
		step.Turn = std::clamp(step.YawErrorRadians / AimFullScaleRadians, -1.0, 1.0);
		step.LookUp = std::clamp(step.PitchErrorRadians / AimFullScaleRadians, -1.0, 1.0);
		return step;
	}

	PlayerMovementController::PlayerMovementController(MovementControllerConfig config)
		: Config(std::move(config))
	{
	}

	ValidationResult PlayerMovementController::Start(const AutomationCommand& command,
		const std::optional<TargetSnapshot>& resolvedTarget,
		uint64_t currentTick, uint64_t observationRevision)
	{
		Reset();
		ValidationResult validation = ValidateCommandForSnapshot(command, currentTick, observationRevision);
		if (!validation)
			return validation;
		if (!SupportsMovement(command.Kind))
			return { ValidationStatus::InvalidCommandKind, "command kind does not produce movement" };

		if (command.Kind == CommandKind::WalkToPoint)
		{
			if (resolvedTarget)
				return { ValidationStatus::InvalidTarget, "walk-to-point must not supply an actor snapshot" };
			Target = *command.Point;
		}
		else
		{
			if (!resolvedTarget || !command.Target ||
				resolvedTarget->ObservationRevision != observationRevision ||
				resolvedTarget->Identity != command.Target->Identity ||
				(!command.Target->ExpectedClass.empty() && resolvedTarget->ClassName != command.Target->ExpectedClass) ||
				resolvedTarget->Deleted)
				return { ValidationStatus::InvalidTarget, "resolved movement target does not match the command snapshot" };
			Target = resolvedTarget->Location;
		}

		if (!IsFinitePoint(Target))
			return { ValidationStatus::InvalidPoint, "resolved movement target is invalid or unbounded" };
		if (!std::isfinite(Config.HeadingDeadzoneRadians) || Config.HeadingDeadzoneRadians < 0.0 ||
			!std::isfinite(Config.AdvanceHeadingRadians) ||
			Config.AdvanceHeadingRadians <= Config.HeadingDeadzoneRadians ||
			Config.AdvanceHeadingRadians > Pi ||
			!std::isfinite(Config.FullTurnRadians) || Config.FullTurnRadians <= 0.0 ||
			!std::isfinite(Config.ProgressEpsilon) || Config.ProgressEpsilon <= 0.0 ||
			!std::isfinite(Config.VerticalTolerance) || Config.VerticalTolerance < 0.0 ||
			Config.StuckTicks == 0 || Config.ObstacleProbeTicks == 0 ||
			Config.ObstacleStrafeTicks == 0 ||
			Config.MaximumObstacleRecoveries == 0)
			return { ValidationStatus::InvalidResult, "movement controller configuration is invalid" };

		Active = true;
		ActiveCommandId = command.Id;
		ArrivalRadius = *command.ArrivalRadius;
		DeadlineTick = command.DeadlineTick;
		LastTick = currentTick;
		LastProgressTick = currentTick;
		BestHorizontalDistance = std::numeric_limits<double>::infinity();
		ObstacleStrafeTicksRemaining = 0;
		ObstacleRecoveryCount = 0;
		ObstacleStrafeDirection = 1.0;
		return {};
	}

	ValidationResult PlayerMovementController::Retarget(const WorldPoint& target,
		double arrivalRadius, uint64_t currentTick)
	{
		if (!Active)
			return { ValidationStatus::InvalidCommandState,
				"inactive movement controller cannot accept a route segment" };
		if (!IsFinitePoint(target))
			return { ValidationStatus::InvalidPoint,
				"route segment target is invalid or unbounded" };
		if (!std::isfinite(arrivalRadius) || arrivalRadius <= 0.0 ||
			arrivalRadius > MaximumArrivalRadius)
			return { ValidationStatus::InvalidArrivalRadius,
				"route segment arrival radius is invalid" };
		if (currentTick < LastTick || currentTick > DeadlineTick)
			return { ValidationStatus::InvalidTickWindow,
				"route segment tick is outside the active command window" };

		Target = target;
		ArrivalRadius = arrivalRadius;
		LastTick = currentTick;
		LastProgressTick = currentTick;
		BestHorizontalDistance = std::numeric_limits<double>::infinity();
		return {};
	}

	ValidationResult PlayerMovementController::Abort(
		const AutomationCommand& abortCommand, uint64_t currentTick,
		uint64_t observationRevision)
	{
		if (!Active)
			return { ValidationStatus::InvalidAbort,
				"no active movement command can be aborted" };
		ValidationResult validation = ValidateCommandForSnapshot(
			abortCommand, currentTick, observationRevision);
		if (!validation)
			return validation;
		if (abortCommand.Kind != CommandKind::Abort ||
			abortCommand.AbortCommandId != ActiveCommandId)
			return { ValidationStatus::InvalidAbort,
				"abort command does not name the active movement command" };
		Reset();
		return {};
	}

	MovementStep PlayerMovementController::Update(const MovementObservation& observation)
	{
		if (!Active)
			return Terminal(MovementStatus::Invalid, observation.Tick, 0.0, 0.0, "movement controller is inactive");
		if (observation.Tick < LastTick || !IsFinitePoint(observation.Position) || !std::isfinite(observation.YawRadians))
		{
			Reset();
			return Terminal(MovementStatus::Invalid, observation.Tick, 0.0, 0.0, "movement observation is invalid or out of order");
		}

		const double deltaX = Target.X - observation.Position.X;
		const double deltaY = Target.Y - observation.Position.Y;
		const double deltaZ = Target.Z - observation.Position.Z;
		const double horizontalDistance = std::hypot(deltaX, deltaY);
		const double verticalDistance = std::abs(deltaZ);
		LastTick = observation.Tick;

		if (horizontalDistance <= ArrivalRadius &&
			verticalDistance <= std::max(ArrivalRadius, Config.VerticalTolerance))
		{
			Reset();
			return Terminal(MovementStatus::Arrived, observation.Tick,
				horizontalDistance, verticalDistance, "arrival envelope reached");
		}
		if (observation.Tick > DeadlineTick)
		{
			Reset();
			return Terminal(MovementStatus::TimedOut, observation.Tick,
				horizontalDistance, verticalDistance, "movement command deadline passed");
		}

		if (!std::isfinite(BestHorizontalDistance) ||
			horizontalDistance + Config.ProgressEpsilon < BestHorizontalDistance)
		{
			BestHorizontalDistance = horizontalDistance;
			LastProgressTick = observation.Tick;
		}
		else if (observation.Tick - LastProgressTick >= Config.StuckTicks)
		{
			Reset();
			return Terminal(MovementStatus::Stuck, observation.Tick,
				horizontalDistance, verticalDistance, "movement made no bounded progress");
		}

		const double desiredYaw = std::atan2(deltaY, deltaX);
		const double yawError = NormalizeAngle(desiredYaw - observation.YawRadians);
		double turn = std::clamp(yawError / Config.FullTurnRadians, -1.0, 1.0);
		if (std::abs(yawError) <= Config.HeadingDeadzoneRadians)
			turn = 0.0;
		double forward = std::abs(yawError) <= Config.AdvanceHeadingRadians ?
			std::clamp(std::cos(yawError), 0.0, 1.0) : 0.0;
		double strafe = 0.0;
		if (ObstacleStrafeTicksRemaining > 0)
		{
			ObstacleStrafeTicksRemaining--;
			forward = std::min(forward, 0.25);
			strafe = ObstacleStrafeDirection;
		}
		else if (observation.Tick - LastProgressTick >= Config.ObstacleProbeTicks &&
			ObstacleRecoveryCount < Config.MaximumObstacleRecoveries)
		{
			ObstacleRecoveryCount++;
			ObstacleStrafeDirection = (ObstacleRecoveryCount % 2 == 1) ? 1.0 : -1.0;
			ObstacleStrafeTicksRemaining = Config.ObstacleStrafeTicks - 1;
			LastProgressTick = observation.Tick;
			forward = std::min(forward, 0.25);
			strafe = ObstacleStrafeDirection;
		}

		MovementStep step;
		step.Status = MovementStatus::Running;
		step.Tick = observation.Tick;
		step.Forward = forward;
		step.Strafe = strafe;
		step.Turn = turn;
		step.HorizontalDistance = horizontalDistance;
		step.VerticalDistance = verticalDistance;
		return step;
	}

	void PlayerMovementController::Reset()
	{
		Active = false;
		ActiveCommandId.clear();
		Target = {};
		ArrivalRadius = 0.0;
		DeadlineTick = 0;
		LastTick = 0;
		LastProgressTick = 0;
		BestHorizontalDistance = 0.0;
		ObstacleStrafeTicksRemaining = 0;
		ObstacleRecoveryCount = 0;
		ObstacleStrafeDirection = 1.0;
	}

	void PlayerMovementInputAdapter::Apply(const MovementStep& step, InputCommandTarget& target)
	{
		if (step.Status != MovementStatus::Running ||
			!std::isfinite(step.Forward) || !std::isfinite(step.Strafe) || !std::isfinite(step.Turn))
		{
			Release(target);
			return;
		}

		target.InputCommand("Axis aBaseY Speed=7000.0",
			{ InputSourceId::Synthetic, ForwardControl }, static_cast<float>(std::clamp(step.Forward, -1.0, 1.0)));
		target.InputCommand("Axis aStrafe Speed=7000.0",
			{ InputSourceId::Synthetic, StrafeControl }, static_cast<float>(std::clamp(step.Strafe, -1.0, 1.0)));
		target.InputCommand("Axis aTurn Speed=4096.0",
			{ InputSourceId::Synthetic, TurnControl }, static_cast<float>(std::clamp(step.Turn, -1.0, 1.0)));
		Active = true;
	}

	void PlayerMovementInputAdapter::Release(InputCommandTarget& target)
	{
		if (Active)
		{
			target.ReleaseInputControl({ InputSourceId::Synthetic, ForwardControl });
			target.ReleaseInputControl({ InputSourceId::Synthetic, StrafeControl });
			target.ReleaseInputControl({ InputSourceId::Synthetic, TurnControl });
		}
		Active = false;
	}

	void PlayerAimInputAdapter::Apply(const AimStep& step,
		InputCommandTarget& target)
	{
		if (step.Status != AimStatus::Aligning || !std::isfinite(step.Turn) ||
			!std::isfinite(step.LookUp))
		{
			Release(target);
			return;
		}

		target.InputCommand("Axis aTurn Speed=4096.0",
			{ InputSourceId::Synthetic, TurnControl },
			static_cast<float>(std::clamp(step.Turn, -1.0, 1.0)));
		target.InputCommand("Axis aLookUp Speed=4096.0",
			{ InputSourceId::Synthetic, LookUpControl },
			static_cast<float>(std::clamp(step.LookUp, -1.0, 1.0)));
		Active = true;
	}

	void PlayerAimInputAdapter::Release(InputCommandTarget& target)
	{
		if (Active)
		{
			target.ReleaseInputControl({ InputSourceId::Synthetic, TurnControl });
			target.ReleaseInputControl({ InputSourceId::Synthetic, LookUpControl });
		}
		Active = false;
	}
}
