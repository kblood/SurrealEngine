#pragma once

#include "Automation/ShadowReplayAdapter.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Automation::TestFixtures
{
	static constexpr uint64_t MaximumShadowReplayFixtureTicks = 2000;

	enum class ShadowReplayFixtureStockTransition
	{
		CommandDefault,
		None,
		AcquireTombstoneExactResourceDelta,
		AcquireTombstoneNoResourceDelta,
		InteractEventReceiverMovement,
		InteractEventReceiverSubthresholdJitter,
		InteractUnrelatedActorMovement
	};

	struct ShadowReplayFixtureConfig
	{
		double FixedDelta = 0.02;
		double MovementSpeed = 200.0;
		ShadowReplayFixtureStockTransition StockTransition =
			ShadowReplayFixtureStockTransition::CommandDefault;
		std::string AuxiliaryTargetIdentity;
		bool FreezeMovement = false;
		std::optional<uint64_t> AbortAtTick;
	};

	struct ShadowReplayFixtureRun
	{
		ShadowReplayProjection Projection;
		std::optional<CommandResult> Result;
		std::optional<CommandResult> AbortResult;
		std::vector<TelemetryEvent> Telemetry;
		std::string TelemetryJson;
		WorldPoint FinalPosition;
		ObservationSnapshot FinalObservation;
		std::optional<uint64_t> StockTransitionTick;
		size_t InteractionRequests = 0;
		bool SyntheticInputActive = false;
	};

	ShadowReplayFixtureRun RunShadowReplayFixture(
		const ShadowReplayCandidate& candidate,
		const ObservationSnapshot& initialObservation,
		std::string commandId,
		uint64_t leaseTicks,
		ShadowReplayFixtureConfig config = {});
}
