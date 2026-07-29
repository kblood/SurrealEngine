#include "BotBenchmark/BotTargetSelectionProbeTracker.h"

#include <iostream>

namespace
{
	using namespace BotTargetSelectionProbe;

	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}

	AcquisitionEnter ValidEnter()
	{
		return { true, "ut436-setenemy-v1", "bot:alpha", "pawn:old", "pawn:new" };
	}
}

int main()
{
	Tracker invalid({ 0, 1 });
	if (invalid.IsReady() || invalid.Enter(ValidEnter()).Status != TrackerStatus::InvalidConfiguration)
		return Fail("zero active-call capacity did not fail closed");
	Tracker noRecordCapacity({ 1, 0 });
	if (noRecordCapacity.IsReady() || noRecordCapacity.Enter(ValidEnter()).Status != TrackerStatus::InvalidConfiguration)
		return Fail("zero record capacity did not fail closed");

	Tracker tracker;
	auto invalidContract = ValidEnter();
	invalidContract.ContractValidated = false;
	if (tracker.Enter(invalidContract).Status != TrackerStatus::InvalidContract)
		return Fail("unvalidated contract was accepted");
	invalidContract = ValidEnter();
	invalidContract.ContractId.clear();
	if (tracker.Enter(invalidContract).Status != TrackerStatus::InvalidContract)
		return Fail("missing contract identifier was accepted");
	auto invalidId = ValidEnter();
	invalidId.BotId.clear();
	if (tracker.Enter(invalidId).Status != TrackerStatus::InvalidIdentifier)
		return Fail("missing bot identifier was accepted");
	invalidId = ValidEnter();
	invalidId.RequestedTargetId.clear();
	if (tracker.Enter(invalidId).Status != TrackerStatus::InvalidIdentifier)
		return Fail("missing requested target identifier was accepted");

	auto changed = tracker.Enter(ValidEnter());
	if (changed.Status != TrackerStatus::Accepted || !changed.Token || !changed.IsOutermostForBot)
		return Fail("outer target acquisition was not accepted");
	if (tracker.ObserveResult(changed.Token, true, "pawn:new") != TrackerStatus::Accepted ||
		tracker.Exit(changed.Token) != TrackerStatus::Accepted || tracker.Records().size() != 1)
		return Fail("changed target acquisition did not emit a record");
	const auto& changeRecord = tracker.Records().front();
	if (changeRecord.ContractId != "ut436-setenemy-v1" || changeRecord.BotId != "bot:alpha" ||
		changeRecord.PreviousTargetId != "pawn:old" || changeRecord.RequestedTargetId != "pawn:new" ||
		changeRecord.ObservedTargetId != "pawn:new" ||
		changeRecord.Outcome != AcquisitionOutcome::AcceptedTargetChange)
	{
		return Fail("changed target acquisition record did not preserve its validated observations");
	}
	if (tracker.Counters().OutermostCalls != 1 ||
		tracker.Counters().AcceptedTargetChanges != 1 ||
		tracker.Counters().AcceptedSameTargets != 0 ||
		tracker.Counters().RejectedOrUnchanged != 0 ||
		tracker.Counters().MissingResults != 0 ||
		tracker.Counters().RecordCapacityExceeded != 0)
	{
		return Fail("changed target acquisition did not update exact counters");
	}

	auto same = ValidEnter();
	same.PreviousTargetId = same.RequestedTargetId;
	auto sameResult = tracker.Enter(same);
	if (!sameResult.IsOutermostForBot || tracker.ObserveResult(sameResult.Token, true, "pawn:new") != TrackerStatus::Accepted ||
		tracker.Exit(sameResult.Token) != TrackerStatus::Accepted ||
		tracker.Records().back().Outcome != AcquisitionOutcome::AcceptedSameTarget)
	{
		return Fail("same target acquisition was not classified distinctly");
	}

	auto rejected = tracker.Enter(ValidEnter());
	if (tracker.ObserveResult(rejected.Token, false, "pawn:old") != TrackerStatus::Accepted ||
		tracker.Exit(rejected.Token) != TrackerStatus::Accepted ||
		tracker.Records().back().Outcome != AcquisitionOutcome::RejectedOrUnchanged)
	{
		return Fail("rejected target acquisition was not classified conservatively");
	}

	auto mismatch = tracker.Enter(ValidEnter());
	if (tracker.ObserveResult(mismatch.Token, true, "pawn:other") != TrackerStatus::Accepted ||
		tracker.Exit(mismatch.Token) != TrackerStatus::Accepted ||
		tracker.Records().back().Outcome != AcquisitionOutcome::RejectedOrUnchanged)
	{
		return Fail("unexpected post-call target was not classified conservatively");
	}

	Tracker nested;
	auto outer = nested.Enter(ValidEnter());
	auto innerEnter = ValidEnter();
	innerEnter.PreviousTargetId = "pawn:new";
	innerEnter.RequestedTargetId = "pawn:nested";
	auto inner = nested.Enter(innerEnter);
	if (!outer.IsOutermostForBot || inner.IsOutermostForBot)
		return Fail("nested target acquisition was not collapsed");
	if (nested.Counters().OutermostCalls != 1 || nested.Counters().NestedCalls != 1)
		return Fail("nested target acquisition counters did not preserve outermost ownership");
	if (nested.ObserveResult(inner.Token, true, "pawn:nested") != TrackerStatus::Accepted ||
		nested.Exit(inner.Token) != TrackerStatus::Accepted || !nested.Records().empty())
	{
		return Fail("nested target acquisition emitted its own record");
	}
	if (nested.ObserveResult(outer.Token, true, "pawn:new") != TrackerStatus::Accepted ||
		nested.Exit(outer.Token) != TrackerStatus::Accepted || nested.Records().size() != 1)
	{
		return Fail("outer target acquisition was not emitted after nested call");
	}

	Tracker order;
	auto first = order.Enter(ValidEnter());
	auto secondEnter = ValidEnter();
	secondEnter.BotId = "bot:beta";
	auto second = order.Enter(secondEnter);
	if (!second.IsOutermostForBot || order.ObserveResult(first.Token, true, "pawn:new") != TrackerStatus::OutOfOrder ||
		order.Exit(first.Token) != TrackerStatus::OutOfOrder || !order.HasActiveScopes())
	{
		return Fail("out-of-order result or exit was accepted");
	}
	if (order.ObserveResult(second.Token, true, "pawn:new") != TrackerStatus::Accepted ||
		order.ObserveResult(second.Token, true, "pawn:new") != TrackerStatus::DuplicateResult ||
		order.Exit(second.Token) != TrackerStatus::Accepted || order.Exit(first.Token) != TrackerStatus::MissingResult ||
		order.HasActiveScopes())
	{
		return Fail("scope result handling did not remain fail-closed");
	}
	if (order.Counters().MissingResults != 1)
		return Fail("missing result did not increment the outermost terminal counter");

	Tracker capped({ 1, 1 });
	auto capFirst = capped.Enter(ValidEnter());
	if (capped.Enter(ValidEnter()).Status != TrackerStatus::CapacityExceeded ||
		capped.ObserveResult(capFirst.Token, true, "pawn:new") != TrackerStatus::Accepted ||
		capped.Exit(capFirst.Token) != TrackerStatus::Accepted ||
		capped.Enter(ValidEnter()).Status != TrackerStatus::RecordCapacityExceeded)
	{
		return Fail("tracker capacities did not fail closed");
	}
	if (capped.Counters().OutermostCalls != 2 || capped.Counters().AcceptedTargetChanges != 1 ||
		capped.Counters().RecordCapacityExceeded != 1)
	{
		return Fail("record-capacity admission did not preserve completed target counters");
	}

	return 0;
}
