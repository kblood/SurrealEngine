#pragma once

#include "AutomationProtocol.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Automation
{
	static constexpr uint64_t MaximumShadowReplayWaitTicks = 600;
	static constexpr double ShadowWalkArrivalRadius = 40.0;
	static constexpr double ShadowActorActionArrivalRadius = 48.0;

	struct ShadowReplayTarget
	{
		std::string Identity;
		std::string ExpectedClass;
	};

	struct ShadowReplayCandidate
	{
		bool DispatchAuthorized = false;
		CommandKind Kind = CommandKind::Wait;
		uint64_t ObservationRevision = 0;
		std::optional<ShadowReplayTarget> Target;
		std::optional<double> ArrivalRadius;
		uint64_t WaitTicks = 0;
	};

	enum class ShadowReplayStatus
	{
		Ready,
		InvalidObservation,
		DispatchFlagSet,
		UnsupportedKind,
		InvalidCandidate,
		StaleObservation,
		TargetRejected,
		InvalidCommand
	};

	struct ShadowReplayProjection
	{
		ShadowReplayStatus Status = ShadowReplayStatus::InvalidCandidate;
		std::optional<AutomationCommand> Command;
		std::optional<TargetResolutionStatus> TargetStatus;
		std::string Error;

		explicit operator bool() const
		{
			return Status == ShadowReplayStatus::Ready && Command.has_value();
		}
	};

	ShadowReplayProjection ProjectShadowReplayCandidate(
		const ShadowReplayCandidate& candidate,
		const ObservationSnapshot& observation,
		std::string commandId,
		uint64_t leaseTicks);
}
