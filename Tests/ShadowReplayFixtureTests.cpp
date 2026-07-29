#include "Fixtures/ShadowReplayFixture.h"

#include <cmath>
#include <iostream>
#include <string>
#include <utility>

using namespace Automation;
using namespace Automation::TestFixtures;

namespace
{
	int Failures = 0;

	void Check(bool condition, const std::string& message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			Failures++;
		}
	}

	bool Near(double left, double right)
	{
		return std::abs(left - right) < 0.0001;
	}

	bool HasValidSequence(const ShadowReplayFixtureRun& run)
	{
		for (size_t index = 0; index < run.Telemetry.size(); index++)
		{
			if (run.Telemetry[index].Sequence != index + 1 ||
				!ValidateTelemetryEvent(run.Telemetry[index]))
				return false;
		}
		return true;
	}

	const TargetSnapshot* FindTarget(const ObservationSnapshot& observation,
		const std::string& identity)
	{
		for (const TargetSnapshot& target : observation.Targets)
		{
			if (target.Identity == identity)
				return &target;
		}
		return nullptr;
	}

	ObservationSnapshot Observation()
	{
		ObservationSnapshot observation;
		observation.Revision = 7;
		observation.Tick = 10;
		observation.PlayerIdentity = "actor:DeusEx.JCDentonMale:JCDentonMale0#0";
		observation.PlayerPosition = {};

		TargetSnapshot light;
		light.ObservationRevision = 7;
		light.Identity = "actor:Engine.Light:Light155#0";
		light.ClassName = "Engine.Light";
		light.Location = { 160.0, 0.0, 0.0 };
		light.Reachable = true;
		observation.Targets.push_back(light);

		TargetSnapshot item;
		item.ObservationRevision = 7;
		item.Identity = "actor:DeusEx.Ammo10mm:Ammo10mm0#0";
		item.ClassName = "DeusEx.Ammo10mm";
		item.StateToken = "Pickup";
		item.Location = { 160.0, 0.0, 0.0 };
		item.Reachable = true;
		item.Acquirable = true;
		observation.Targets.push_back(item);

		TargetSnapshot switchTarget;
		switchTarget.ObservationRevision = 7;
		switchTarget.Identity = "actor:DeusEx.Switch1:Switch1#0";
		switchTarget.ClassName = "DeusEx.Switch1";
		switchTarget.StateToken = "Active";
		switchTarget.Location = { 20.0, 0.0, 0.0 };
		switchTarget.Reachable = true;
		switchTarget.Interactable = true;
		observation.Targets.push_back(switchTarget);
		return observation;
	}

	ObservationSnapshot StackedResourceObservation()
	{
		ObservationSnapshot observation;
		observation.Revision = 7;
		observation.Tick = 10;
		observation.PlayerIdentity = "actor:Fixture.Player:Player0#0";

		TargetSnapshot target;
		target.ObservationRevision = 7;
		target.Identity = "actor:Fixture.StackItem:WorldStack1#0";
		target.ClassName = "Fixture.StackItem";
		target.StateToken = "Pickup";
		target.Location = { 160.0, 0.0, 0.0 };
		target.Reachable = true;
		target.Acquirable = true;
		target.InventoryResource = TargetSnapshot::InventoryResourceSnapshot{
			TargetSnapshot::InventoryResourceKind::AmmoAmount, 6 };
		observation.Targets.push_back(target);

		TargetSnapshot owned = target;
		owned.Identity = "actor:Fixture.StackItem:OwnedStack0#0";
		owned.OwnerIdentity = observation.PlayerIdentity;
		owned.StateToken = "Owned";
		owned.Location = {};
		owned.Reachable = false;
		owned.Interactable = false;
		owned.Acquirable = false;
		observation.Targets.push_back(owned);

		TargetSnapshot distractor = target;
		distractor.Identity = "actor:Fixture.StackItem:WorldStack2#0";
		distractor.Location = { 200.0, 40.0, 0.0 };
		observation.Targets.push_back(distractor);
		return observation;
	}

	ObservationSnapshot EventReceiverObservation()
	{
		ObservationSnapshot observation;
		observation.Revision = 7;
		observation.Tick = 10;
		observation.PlayerIdentity = "actor:Fixture.Player:Player0#0";

		TargetSnapshot panel;
		panel.ObservationRevision = 7;
		panel.Identity = "actor:Fixture.Panel:Panel0#0";
		panel.ClassName = "Fixture.Panel";
		panel.StateToken = "Active";
		panel.TagName = "Panel";
		panel.EventName = "FixtureDoor";
		panel.Location = { 20.0, 0.0, 0.0 };
		panel.Reachable = true;
		panel.Interactable = true;
		observation.Targets.push_back(panel);

		TargetSnapshot receiver;
		receiver.ObservationRevision = 7;
		receiver.Identity = "actor:Fixture.Door:Door0#0";
		receiver.ClassName = "Fixture.Door";
		receiver.StateToken = "Closed";
		receiver.TagName = "FixtureDoor";
		receiver.Location = { 80.0, 0.0, 0.0 };
		observation.Targets.push_back(receiver);

		TargetSnapshot distractor = receiver;
		distractor.Identity = "actor:Fixture.Door:Door1#0";
		distractor.TagName = "OtherDoor";
		distractor.Location = { 80.0, 40.0, 0.0 };
		observation.Targets.push_back(distractor);
		return observation;
	}

	ShadowReplayCandidate ActorCandidate(CommandKind kind,
		std::string identity, std::string className)
	{
		ShadowReplayCandidate candidate;
		candidate.Kind = kind;
		candidate.ObservationRevision = 7;
		candidate.Target = ShadowReplayTarget{ std::move(identity), std::move(className) };
		candidate.ArrivalRadius = kind == CommandKind::WalkToActor ?
			ShadowWalkArrivalRadius : ShadowActorActionArrivalRadius;
		return candidate;
	}

	void TestDeterministicWalkAndWait()
	{
		const ShadowReplayCandidate walk = ActorCandidate(CommandKind::WalkToActor,
			"actor:Engine.Light:Light155#0", "Engine.Light");
		ShadowReplayFixtureRun first = RunShadowReplayFixture(
			walk, Observation(), "fixture-walk", 100);
		ShadowReplayFixtureRun second = RunShadowReplayFixture(
			walk, Observation(), "fixture-walk", 100);
		Check(first.Projection && first.Result &&
			first.Result->State == CommandState::Succeeded && first.Result->Tick == 41,
			"walk-to-actor fixture did not reach its deterministic arrival tick");
		Check(!first.SyntheticInputActive && Near(first.FinalPosition.X, 120.0) &&
			Near(first.FinalPosition.Y, 0.0),
			"walk-to-actor fixture did not release input at the expected position");
		Check(first.TelemetryJson == second.TelemetryJson &&
			first.Result && second.Result &&
			CommandResultJson(*first.Result) == CommandResultJson(*second.Result) &&
			Near(first.FinalPosition.X, second.FinalPosition.X),
			"repeated walk-to-actor fixture evidence is not deterministic");
		Check(first.Telemetry.size() == 34 && HasValidSequence(first) &&
			first.Telemetry.front().Type == TelemetryEventType::CommandIssued &&
			first.Telemetry.back().Type == TelemetryEventType::CommandResult,
			"walk-to-actor fixture telemetry lifecycle is incomplete or invalid");

		ShadowReplayCandidate wait;
		wait.Kind = CommandKind::Wait;
		wait.ObservationRevision = 7;
		wait.WaitTicks = 5;
		ShadowReplayFixtureRun waited = RunShadowReplayFixture(
			wait, Observation(), "fixture-wait", 20);
		Check(waited.Result && waited.Result->State == CommandState::Succeeded &&
			waited.Result->Tick == 15 && waited.InteractionRequests == 0 &&
			!waited.SyntheticInputActive && HasValidSequence(waited) &&
			waited.Telemetry.size() == 7,
			"bounded wait fixture did not finish exactly without synthetic input");
	}

	void TestStockTransitionProofs()
	{
		const ShadowReplayCandidate acquire = ActorCandidate(CommandKind::AcquireItem,
			"actor:DeusEx.Ammo10mm:Ammo10mm0#0", "DeusEx.Ammo10mm");
		ShadowReplayFixtureRun acquired = RunShadowReplayFixture(
			acquire, Observation(), "fixture-acquire", 100);
		Check(acquired.Result && acquired.Result->State == CommandState::Succeeded &&
			acquired.Result->Tick == 39 && acquired.InteractionRequests == 1 &&
			acquired.Result->Reason == "exact target ownership transferred to the player" &&
			!acquired.SyntheticInputActive,
			"acquisition fixture did not require one request and ownership proof");

		const ShadowReplayCandidate interact = ActorCandidate(CommandKind::Interact,
			"actor:DeusEx.Switch1:Switch1#0", "DeusEx.Switch1");
		ShadowReplayFixtureRun interacted = RunShadowReplayFixture(
			interact, Observation(), "fixture-interact", 30);
		Check(interacted.Result && interacted.Result->State == CommandState::Succeeded &&
			interacted.Result->Tick == 12 && interacted.InteractionRequests == 1 &&
			interacted.Result->Reason == "target state transition observed after stock interaction" &&
			!interacted.SyntheticInputActive,
			"interaction fixture did not require one request and state-transition proof");
	}

	void TestStackedResourceMergeProof()
	{
		const std::string targetIdentity =
			"actor:Fixture.StackItem:WorldStack1#0";
		const std::string ownedIdentity =
			"actor:Fixture.StackItem:OwnedStack0#0";
		const std::string distractorIdentity =
			"actor:Fixture.StackItem:WorldStack2#0";
		const ShadowReplayCandidate acquire = ActorCandidate(CommandKind::AcquireItem,
			targetIdentity, "Fixture.StackItem");
		ShadowReplayFixtureConfig mergeConfig;
		mergeConfig.StockTransition =
			ShadowReplayFixtureStockTransition::AcquireTombstoneExactResourceDelta;
		mergeConfig.AuxiliaryTargetIdentity = ownedIdentity;
		ShadowReplayFixtureRun first = RunShadowReplayFixture(
			acquire, StackedResourceObservation(), "fixture-stack-merge", 100,
			mergeConfig);
		ShadowReplayFixtureRun second = RunShadowReplayFixture(
			acquire, StackedResourceObservation(), "fixture-stack-merge", 100,
			mergeConfig);
		const TargetSnapshot* target = FindTarget(first.FinalObservation, targetIdentity);
		const TargetSnapshot* owned = FindTarget(first.FinalObservation, ownedIdentity);
		const TargetSnapshot* distractor = FindTarget(
			first.FinalObservation, distractorIdentity);
		Check(first.Result && first.Result->State == CommandState::Succeeded &&
			first.Result->Tick == 39 && first.InteractionRequests == 1 &&
			first.Result->Reason ==
				"target tombstone and matching ammo_amount increase observed from 6 to 12",
			"stacked acquisition did not require the exact tombstone/resource proof");
		Check(target && target->Deleted && target->OwnerIdentity.empty() &&
			target->InventoryResource && target->InventoryResource->Value == 6 &&
			owned && !owned->Deleted &&
			owned->OwnerIdentity == first.FinalObservation.PlayerIdentity &&
			owned->InventoryResource && owned->InventoryResource->Value == 12 &&
			distractor && !distractor->Deleted && distractor->OwnerIdentity.empty() &&
			distractor->InventoryResource && distractor->InventoryResource->Value == 6,
			"stacked acquisition did not preserve exact identities and untouched distractor");
		Check(first.StockTransitionTick == 39 &&
			!first.SyntheticInputActive && HasValidSequence(first),
			"stacked acquisition transition scope or input release is invalid");
		Check(first.TelemetryJson == second.TelemetryJson && first.Result && second.Result &&
			CommandResultJson(*first.Result) == CommandResultJson(*second.Result) &&
			ObservationSnapshotJson(first.FinalObservation) ==
				ObservationSnapshotJson(second.FinalObservation),
			"repeated stacked acquisition evidence is not deterministic");

		ShadowReplayFixtureConfig tombstoneOnly;
		tombstoneOnly.StockTransition =
			ShadowReplayFixtureStockTransition::AcquireTombstoneNoResourceDelta;
		tombstoneOnly.AuxiliaryTargetIdentity = ownedIdentity;
		ShadowReplayFixtureRun rejected = RunShadowReplayFixture(
			acquire, StackedResourceObservation(), "fixture-stack-no-delta", 45,
			tombstoneOnly);
		const TargetSnapshot* rejectedTarget = FindTarget(
			rejected.FinalObservation, targetIdentity);
		const TargetSnapshot* rejectedOwned = FindTarget(
			rejected.FinalObservation, ownedIdentity);
		Check(rejected.Result && rejected.Result->State == CommandState::TimedOut &&
			rejected.InteractionRequests == 1 && rejectedTarget && rejectedTarget->Deleted &&
			rejectedOwned && rejectedOwned->InventoryResource &&
			rejectedOwned->InventoryResource->Value == 6 &&
			rejected.StockTransitionTick == 39 &&
			!rejected.SyntheticInputActive,
			"target tombstone without the exact resource delta was accepted");

		ShadowReplayFixtureConfig abortAtTransition = mergeConfig;
		abortAtTransition.AbortAtTick = 39;
		ShadowReplayFixtureRun aborted = RunShadowReplayFixture(
			acquire, StackedResourceObservation(), "fixture-stack-abort", 100,
			abortAtTransition);
		const TargetSnapshot* abortedTarget = FindTarget(
			aborted.FinalObservation, targetIdentity);
		const TargetSnapshot* abortedOwned = FindTarget(
			aborted.FinalObservation, ownedIdentity);
		Check(aborted.Result && aborted.Result->State == CommandState::Cancelled &&
			aborted.Result->Tick == 39 && aborted.AbortResult &&
			aborted.AbortResult->State == CommandState::Succeeded &&
			!aborted.StockTransitionTick && abortedTarget && !abortedTarget->Deleted &&
			abortedOwned && abortedOwned->InventoryResource &&
			abortedOwned->InventoryResource->Value == 6 &&
			!aborted.SyntheticInputActive,
			"abort at the proof boundary mutated stock or left synthetic input active");
	}

	void TestCorrelatedEventReceiverProof()
	{
		const std::string panelIdentity = "actor:Fixture.Panel:Panel0#0";
		const std::string receiverIdentity = "actor:Fixture.Door:Door0#0";
		const std::string distractorIdentity = "actor:Fixture.Door:Door1#0";
		const ShadowReplayCandidate interact = ActorCandidate(CommandKind::Interact,
			panelIdentity, "Fixture.Panel");
		ShadowReplayFixtureConfig receiverConfig;
		receiverConfig.StockTransition =
			ShadowReplayFixtureStockTransition::InteractEventReceiverMovement;
		receiverConfig.AuxiliaryTargetIdentity = receiverIdentity;
		ShadowReplayFixtureRun first = RunShadowReplayFixture(
			interact, EventReceiverObservation(), "fixture-event-receiver", 30,
			receiverConfig);
		ShadowReplayFixtureRun second = RunShadowReplayFixture(
			interact, EventReceiverObservation(), "fixture-event-receiver", 30,
			receiverConfig);
		const TargetSnapshot* panel = FindTarget(first.FinalObservation, panelIdentity);
		const TargetSnapshot* receiver = FindTarget(
			first.FinalObservation, receiverIdentity);
		const TargetSnapshot* distractor = FindTarget(
			first.FinalObservation, distractorIdentity);
		Check(first.Result && first.Result->State == CommandState::Succeeded &&
			first.Result->Tick == 12 && first.InteractionRequests == 1 &&
			first.Result->Reason ==
				"target event receiver transition observed after stock interaction",
			"interaction did not require the correlated receiver transition proof");
		Check(panel && panel->StateToken == "Active" && !panel->Deleted &&
			panel->EventName == "FixtureDoor" && receiver &&
			Near(receiver->Location.Y, 64.0) && receiver->StateToken == "Closed" &&
			distractor && Near(distractor->Location.Y, 40.0),
			"event interaction changed the panel or unrelated receiver");
		Check(first.StockTransitionTick == 12 &&
			!first.SyntheticInputActive && first.TelemetryJson == second.TelemetryJson &&
			ObservationSnapshotJson(first.FinalObservation) ==
				ObservationSnapshotJson(second.FinalObservation),
			"event receiver transition scope is not deterministic or input-safe");

		ShadowReplayFixtureConfig unrelatedOnly;
		unrelatedOnly.StockTransition =
			ShadowReplayFixtureStockTransition::InteractUnrelatedActorMovement;
		unrelatedOnly.AuxiliaryTargetIdentity = distractorIdentity;
		ShadowReplayFixtureRun rejected = RunShadowReplayFixture(
			interact, EventReceiverObservation(), "fixture-unrelated-receiver", 20,
			unrelatedOnly);
		const TargetSnapshot* rejectedReceiver = FindTarget(
			rejected.FinalObservation, receiverIdentity);
		const TargetSnapshot* movedDistractor = FindTarget(
			rejected.FinalObservation, distractorIdentity);
		Check(rejected.Result && rejected.Result->State == CommandState::TimedOut &&
			rejected.InteractionRequests == 1 && rejectedReceiver &&
			Near(rejectedReceiver->Location.Y, 0.0) && movedDistractor &&
			Near(movedDistractor->Location.Y, 104.0) &&
			rejected.StockTransitionTick == 12 &&
			!rejected.SyntheticInputActive,
			"uncorrelated receiver movement was accepted as interaction proof");

		ShadowReplayFixtureConfig subthreshold;
		subthreshold.StockTransition =
			ShadowReplayFixtureStockTransition::InteractEventReceiverSubthresholdJitter;
		subthreshold.AuxiliaryTargetIdentity = receiverIdentity;
		ShadowReplayFixtureRun jittered = RunShadowReplayFixture(
			interact, EventReceiverObservation(), "fixture-subthreshold-receiver", 20,
			subthreshold);
		const TargetSnapshot* jitteredReceiver = FindTarget(
			jittered.FinalObservation, receiverIdentity);
		Check(jittered.Result && jittered.Result->State == CommandState::TimedOut &&
			jitteredReceiver && Near(jitteredReceiver->Location.Y, 0.0005) &&
			jittered.StockTransitionTick == 12 && !jittered.SyntheticInputActive,
			"subthreshold correlated receiver jitter was accepted as interaction proof");
	}

	void TestStockTransitionConfigValidation()
	{
		const ShadowReplayCandidate acquire = ActorCandidate(CommandKind::AcquireItem,
			"actor:Fixture.StackItem:WorldStack1#0", "Fixture.StackItem");
		const ShadowReplayCandidate interact = ActorCandidate(CommandKind::Interact,
			"actor:Fixture.Panel:Panel0#0", "Fixture.Panel");
		auto IsRejectedWithoutTelemetry = [](const ShadowReplayFixtureRun& run)
		{
			return run.Projection.Status == ShadowReplayStatus::InvalidCommand &&
				!run.Projection.Command && !run.Result && run.Telemetry.empty();
		};

		ShadowReplayFixtureConfig missingAuxiliary;
		missingAuxiliary.StockTransition =
			ShadowReplayFixtureStockTransition::AcquireTombstoneExactResourceDelta;
		Check(IsRejectedWithoutTelemetry(RunShadowReplayFixture(
			acquire, StackedResourceObservation(), "fixture-missing-aux", 100,
			missingAuxiliary)),
			"explicit stock transition accepted a missing auxiliary identity");

		ShadowReplayFixtureConfig resourceOnInteract;
		resourceOnInteract.StockTransition =
			ShadowReplayFixtureStockTransition::AcquireTombstoneExactResourceDelta;
		resourceOnInteract.AuxiliaryTargetIdentity =
			"actor:Fixture.Door:Door0#0";
		Check(IsRejectedWithoutTelemetry(RunShadowReplayFixture(
			interact, EventReceiverObservation(), "fixture-resource-on-interact", 30,
			resourceOnInteract)),
			"resource transition mode accepted an interact command");

		ShadowReplayFixtureConfig receiverOnAcquire;
		receiverOnAcquire.StockTransition =
			ShadowReplayFixtureStockTransition::InteractEventReceiverMovement;
		receiverOnAcquire.AuxiliaryTargetIdentity =
			"actor:Fixture.StackItem:OwnedStack0#0";
		Check(IsRejectedWithoutTelemetry(RunShadowReplayFixture(
			acquire, StackedResourceObservation(), "fixture-receiver-on-acquire", 100,
			receiverOnAcquire)),
			"receiver transition mode accepted an acquire command");

		ObservationSnapshot ambiguousStack = StackedResourceObservation();
		TargetSnapshot duplicateOwned = ambiguousStack.Targets[1];
		duplicateOwned.Identity = "actor:Fixture.StackItem:OwnedStack1#0";
		ambiguousStack.Targets.push_back(duplicateOwned);
		ShadowReplayFixtureConfig merge;
		merge.StockTransition =
			ShadowReplayFixtureStockTransition::AcquireTombstoneExactResourceDelta;
		merge.AuxiliaryTargetIdentity = "actor:Fixture.StackItem:OwnedStack0#0";
		Check(IsRejectedWithoutTelemetry(RunShadowReplayFixture(
			acquire, ambiguousStack, "fixture-ambiguous-stack", 100, merge)),
			"resource transition accepted ambiguous owned matching-class stacks");

		ObservationSnapshot overflow = StackedResourceObservation();
		overflow.Targets[1].InventoryResource->Value =
			MaximumInventoryResourceValue;
		Check(IsRejectedWithoutTelemetry(RunShadowReplayFixture(
			acquire, overflow, "fixture-stack-overflow", 100, merge)),
			"resource transition accepted a snapshot-value overflow");

		ObservationSnapshot ambiguousReceiver = EventReceiverObservation();
		TargetSnapshot duplicateReceiver = ambiguousReceiver.Targets[1];
		duplicateReceiver.Identity = "actor:Fixture.Door:Door2#0";
		ambiguousReceiver.Targets.push_back(duplicateReceiver);
		ShadowReplayFixtureConfig receiver;
		receiver.StockTransition =
			ShadowReplayFixtureStockTransition::InteractEventReceiverMovement;
		receiver.AuxiliaryTargetIdentity = "actor:Fixture.Door:Door0#0";
		Check(IsRejectedWithoutTelemetry(RunShadowReplayFixture(
			interact, ambiguousReceiver, "fixture-ambiguous-receiver", 30, receiver)),
			"receiver transition accepted ambiguous Event/Tag correlation");

		ShadowReplayFixtureConfig unknown;
		unknown.StockTransition =
			static_cast<ShadowReplayFixtureStockTransition>(999);
		Check(IsRejectedWithoutTelemetry(RunShadowReplayFixture(
			acquire, StackedResourceObservation(), "fixture-unknown-mode", 100,
			unknown)),
			"unknown stock transition mode did not fail closed");
	}

	void TestFailureAndAbortPaths()
	{
		const ShadowReplayCandidate acquire = ActorCandidate(CommandKind::AcquireItem,
			"actor:DeusEx.Ammo10mm:Ammo10mm0#0", "DeusEx.Ammo10mm");
		ShadowReplayFixtureConfig noTransition;
		noTransition.StockTransition = ShadowReplayFixtureStockTransition::None;
		ShadowReplayFixtureRun timedOut = RunShadowReplayFixture(
			acquire, Observation(), "fixture-acquire-timeout", 45, noTransition);
		Check(timedOut.Result && timedOut.Result->State == CommandState::TimedOut &&
			timedOut.InteractionRequests == 1 && !timedOut.SyntheticInputActive,
			"missing acquisition proof did not produce a bounded timeout");

		const ShadowReplayCandidate walk = ActorCandidate(CommandKind::WalkToActor,
			"actor:Engine.Light:Light155#0", "Engine.Light");
		ShadowReplayFixtureConfig frozen;
		frozen.FreezeMovement = true;
		ShadowReplayFixtureRun stuck = RunShadowReplayFixture(
			walk, Observation(), "fixture-walk-stuck", 200, frozen);
		Check(stuck.Result && stuck.Result->State == CommandState::Failed &&
			stuck.Result->Reason == "movement made no bounded progress" &&
			!stuck.SyntheticInputActive,
			"frozen fixture movement did not fail stuck and release input");

		ShadowReplayFixtureConfig abortedConfig;
		abortedConfig.AbortAtTick = 20;
		ShadowReplayFixtureRun aborted = RunShadowReplayFixture(
			acquire, Observation(), "fixture-acquire-abort", 100, abortedConfig);
		Check(aborted.Result && aborted.Result->State == CommandState::Cancelled &&
			aborted.Result->Tick == 20 && aborted.AbortResult &&
			aborted.AbortResult->State == CommandState::Succeeded &&
			!aborted.SyntheticInputActive,
			"bounded fixture abort did not cancel both controllers and release input");
		Check(aborted.Telemetry.size() >= 4 &&
			aborted.Telemetry[aborted.Telemetry.size() - 2].State == CommandState::Cancelled &&
			aborted.Telemetry.back().Kind == CommandKind::Abort &&
			aborted.Telemetry.back().State == CommandState::Succeeded,
			"fixture abort telemetry does not contain both terminal lifecycles");
	}

	void TestFixtureBounds()
	{
		const ShadowReplayCandidate walk = ActorCandidate(CommandKind::WalkToActor,
			"actor:Engine.Light:Light155#0", "Engine.Light");
		ShadowReplayFixtureRun unbounded = RunShadowReplayFixture(
			walk, Observation(), "fixture-unbounded", MaximumShadowReplayFixtureTicks + 1);
		Check(unbounded.Projection.Status == ShadowReplayStatus::InvalidCommand &&
			!unbounded.Result && unbounded.Telemetry.empty(),
			"fixture execution accepted an unbounded lease");

		ShadowReplayCandidate authorized = walk;
		authorized.DispatchAuthorized = true;
		ShadowReplayFixtureRun rejected = RunShadowReplayFixture(
			authorized, Observation(), "fixture-authorized", 100);
		Check(rejected.Projection.Status == ShadowReplayStatus::DispatchFlagSet &&
			!rejected.Result && rejected.Telemetry.empty(),
			"fixture execution accepted a candidate claiming dispatch authority");
	}
}

int main()
{
	TestDeterministicWalkAndWait();
	TestStockTransitionProofs();
	TestStackedResourceMergeProof();
	TestCorrelatedEventReceiverProof();
	TestStockTransitionConfigValidation();
	TestFailureAndAbortPaths();
	TestFixtureBounds();
	if (Failures == 0)
		std::cout << "Shadow replay fixture tests passed\n";
	return Failures == 0 ? 0 : 1;
}
