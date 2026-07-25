#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BotTargetSelectionProbe
{
	enum class TrackerStatus
	{
		Accepted,
		InvalidConfiguration,
		InvalidContract,
		InvalidIdentifier,
		InvalidToken,
		OutOfOrder,
		DuplicateResult,
		MissingResult,
		CapacityExceeded,
		RecordCapacityExceeded,
	};

	enum class AcquisitionOutcome
	{
		AcceptedTargetChange,
		AcceptedSameTarget,
		RejectedOrUnchanged,
	};

	struct ScopeToken
	{
		uint64_t Value = 0;

		explicit operator bool() const { return Value != 0; }
	};

	struct TrackerConfig
	{
		size_t MaxActiveCalls = 64;
		size_t MaxRecords = 1024;
	};

	// ContractId must be the stable identifier produced by the code which has
	// already validated the game-specific SetEnemy hook contract.
	struct AcquisitionEnter
	{
		bool ContractValidated = false;
		std::string ContractId;
		std::string BotId;
		std::string PreviousTargetId;
		std::string RequestedTargetId;
	};

	struct BeginScopeResult
	{
		TrackerStatus Status = TrackerStatus::InvalidConfiguration;
		ScopeToken Token;
		bool IsOutermostForBot = false;
	};

	struct AcquisitionRecord
	{
		std::string ContractId;
		std::string BotId;
		std::string PreviousTargetId;
		std::string RequestedTargetId;
		std::string ObservedTargetId;
		AcquisitionOutcome Outcome = AcquisitionOutcome::RejectedOrUnchanged;
	};

	class Tracker
	{
	public:
		explicit Tracker(TrackerConfig config = {});

		bool IsReady() const;
		BeginScopeResult Enter(AcquisitionEnter observation);
		TrackerStatus ObserveResult(ScopeToken token, bool returnedAccepted,
			std::string observedTargetId);
		TrackerStatus Exit(ScopeToken token);

		const std::vector<AcquisitionRecord>& Records() const;
		bool HasActiveScopes() const;

	private:
		struct Frame
		{
			ScopeToken Token;
			AcquisitionEnter Observation;
			bool IsOutermostForBot = false;
			bool HasResult = false;
			bool ReturnedAccepted = false;
			std::string ObservedTargetId;
		};

		ScopeToken NextToken();
		bool HasActiveCallForBot(const std::string& botId) const;
		static AcquisitionOutcome Classify(const Frame& frame);

		TrackerConfig Config;
		uint64_t NextTokenValue = 1;
		std::vector<Frame> Frames;
		std::vector<AcquisitionRecord> CompletedRecords;
	};
}
