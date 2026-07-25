#include "BotTargetSelectionProbeTracker.h"

#include <algorithm>
#include <utility>

namespace BotTargetSelectionProbe
{
	Tracker::Tracker(TrackerConfig config)
		: Config(config)
	{
	}

	bool Tracker::IsReady() const
	{
		return Config.MaxActiveCalls != 0 && Config.MaxRecords != 0;
	}

	ScopeToken Tracker::NextToken()
	{
		return { NextTokenValue++ };
	}

	bool Tracker::HasActiveCallForBot(const std::string& botId) const
	{
		return std::any_of(Frames.begin(), Frames.end(), [&botId](const Frame& frame)
		{
			return frame.Observation.BotId == botId;
		});
	}

	BeginScopeResult Tracker::Enter(AcquisitionEnter observation)
	{
		if (!IsReady())
			return { TrackerStatus::InvalidConfiguration };
		if (!observation.ContractValidated || observation.ContractId.empty())
			return { TrackerStatus::InvalidContract };
		if (observation.BotId.empty() || observation.RequestedTargetId.empty())
			return { TrackerStatus::InvalidIdentifier };
		if (Frames.size() >= Config.MaxActiveCalls)
			return { TrackerStatus::CapacityExceeded };
		if (CompletedRecords.size() >= Config.MaxRecords)
			return { TrackerStatus::RecordCapacityExceeded };

		const bool outermost = !HasActiveCallForBot(observation.BotId);
		const ScopeToken token = NextToken();
		Frames.push_back({ token, std::move(observation), outermost });
		return { TrackerStatus::Accepted, token, outermost };
	}

	TrackerStatus Tracker::ObserveResult(ScopeToken token, bool returnedAccepted,
		std::string observedTargetId)
	{
		if (!token)
			return TrackerStatus::InvalidToken;
		if (Frames.empty() || Frames.back().Token.Value != token.Value)
			return TrackerStatus::OutOfOrder;

		Frame& frame = Frames.back();
		if (frame.HasResult)
			return TrackerStatus::DuplicateResult;
		frame.HasResult = true;
		frame.ReturnedAccepted = returnedAccepted;
		frame.ObservedTargetId = std::move(observedTargetId);
		return TrackerStatus::Accepted;
	}

	AcquisitionOutcome Tracker::Classify(const Frame& frame)
	{
		if (!frame.ReturnedAccepted ||
			frame.ObservedTargetId != frame.Observation.RequestedTargetId)
		{
			return AcquisitionOutcome::RejectedOrUnchanged;
		}
		return frame.ObservedTargetId == frame.Observation.PreviousTargetId ?
			AcquisitionOutcome::AcceptedSameTarget :
			AcquisitionOutcome::AcceptedTargetChange;
	}

	TrackerStatus Tracker::Exit(ScopeToken token)
	{
		if (!token)
			return TrackerStatus::InvalidToken;
		if (Frames.empty() || Frames.back().Token.Value != token.Value)
			return TrackerStatus::OutOfOrder;

		Frame frame = std::move(Frames.back());
		Frames.pop_back();
		if (!frame.HasResult)
			return TrackerStatus::MissingResult;
		if (!frame.IsOutermostForBot)
			return TrackerStatus::Accepted;

		if (CompletedRecords.size() >= Config.MaxRecords)
			return TrackerStatus::RecordCapacityExceeded;
		const AcquisitionOutcome outcome = Classify(frame);
		CompletedRecords.push_back({
			std::move(frame.Observation.ContractId),
			std::move(frame.Observation.BotId),
			std::move(frame.Observation.PreviousTargetId),
			std::move(frame.Observation.RequestedTargetId),
			std::move(frame.ObservedTargetId),
			outcome,
		});
		return TrackerStatus::Accepted;
	}

	const std::vector<AcquisitionRecord>& Tracker::Records() const
	{
		return CompletedRecords;
	}

	bool Tracker::HasActiveScopes() const
	{
		return !Frames.empty();
	}
}
