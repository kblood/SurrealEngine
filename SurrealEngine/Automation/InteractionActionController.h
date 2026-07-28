#pragma once

#include "AutomationProtocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Automation
{
	struct InteractionActionStep
	{
		CommandState State = CommandState::Failed;
		uint64_t Tick = 0;
		bool RequestInteraction = false;
		std::string Reason;
	};

	class InteractionActionController
	{
	public:
		ValidationResult Start(const AutomationCommand& command,
			const ObservationSnapshot& observation);
		InteractionActionStep Update(const ObservationSnapshot& observation);
		ValidationResult Abort(const AutomationCommand& abortCommand,
			uint64_t currentTick, uint64_t observationRevision,
			InteractionActionStep& cancelledStep);
		void Reset();

		bool IsActive() const { return Active; }
		const std::string& CommandId() const { return Command.Id; }

	private:
		InteractionActionStep Finish(CommandState state, uint64_t tick, std::string reason);
		size_t OwnedClassCount(const ObservationSnapshot& observation) const;
		std::optional<uint64_t> OwnedClassResourceTotal(
			const ObservationSnapshot& observation,
			TargetSnapshot::InventoryResourceKind kind) const;

		bool Active = false;
		bool InteractionRequested = false;
		AutomationCommand Command;
		TargetSnapshot BaselineTarget;
		std::string PlayerIdentity;
		size_t BaselineOwnedClassCount = 0;
		std::optional<TargetSnapshot::InventoryResourceKind> BaselineResourceKind;
		std::optional<uint64_t> BaselineOwnedResourceTotal;
		std::vector<TargetSnapshot> BaselineEventReceivers;
		uint64_t LastTick = 0;
		uint64_t LastRevision = 0;
	};
}
