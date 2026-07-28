#include "Automation/InteractionActionController.h"

#include <iostream>
#include <stdexcept>

using namespace Automation;

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void Check(const ValidationResult& result, const char* message)
	{
		Check(static_cast<bool>(result), message);
	}

	TargetSnapshot Target(uint64_t revision)
	{
		TargetSnapshot target;
		target.ObservationRevision = revision;
		target.Identity = "actor:100:NanoKey0";
		target.ClassName = "DeusEx.NanoKey";
		target.StateToken = "Pickup";
		target.Location = { 10.0, 20.0, 30.0 };
		target.Reachable = true;
		target.Interactable = true;
		target.Acquirable = true;
		return target;
	}

	ObservationSnapshot Observation(uint64_t revision, uint64_t tick)
	{
		ObservationSnapshot observation;
		observation.Revision = revision;
		observation.Tick = tick;
		observation.PlayerIdentity = "actor:200:JCDentonMale0";
		observation.PlayerPosition = {};
		observation.Targets = { Target(revision) };
		return observation;
	}

	AutomationCommand ActorCommand(CommandKind kind)
	{
		AutomationCommand command;
		command.Id = kind == CommandKind::AcquireItem ? "acquire.nano-key" : "interact.panel";
		command.Kind = kind;
		command.IssuedTick = 10;
		command.DeadlineTick = 30;
		command.Target = TargetSelector{ 1, "actor:100:NanoKey0", "DeusEx.NanoKey" };
		command.ArrivalRadius = 32.0;
		return command;
	}

	void SetObservationVersion(ObservationSnapshot& observation,
		uint64_t revision, uint64_t tick)
	{
		observation.Revision = revision;
		observation.Tick = tick;
		for (TargetSnapshot& target : observation.Targets)
			target.ObservationRevision = revision;
	}
}

int main()
{
	try
	{
		InteractionActionController controller;
		AutomationCommand acquire = ActorCommand(CommandKind::AcquireItem);
		ObservationSnapshot initial = Observation(1, 10);
		initial.Targets[0].Interactable = false;
		Check(controller.Start(acquire, initial), "valid acquisition was rejected");
		ObservationSnapshot request = Observation(2, 11);
		request.Targets[0].Interactable = false;
		InteractionActionStep step = controller.Update(request);
		Check(step.State == CommandState::Running && !step.RequestInteraction,
			"out-of-range acquisition requested stock interaction");
		request = Observation(3, 12);
		step = controller.Update(request);
		Check(step.State == CommandState::Running && step.RequestInteraction,
			"acquisition did not request one stock interaction");
		ObservationSnapshot unchanged = Observation(4, 13);
		step = controller.Update(unchanged);
		Check(step.State == CommandState::Running && !step.RequestInteraction,
			"unchanged proximity was mistaken for acquisition or repeated interaction");
		ObservationSnapshot owned = Observation(5, 14);
		owned.Targets[0].OwnerIdentity = owned.PlayerIdentity;
		owned.Targets[0].Interactable = false;
		owned.Targets[0].Acquirable = false;
		step = controller.Update(owned);
		Check(step.State == CommandState::Succeeded && !controller.IsActive(),
			"exact ownership transfer did not complete acquisition");

		Check(controller.Start(acquire, initial), "second acquisition start failed");
		(void)controller.Update(Observation(2, 11));
		ObservationSnapshot missing = Observation(3, 12);
		missing.Targets.clear();
		step = controller.Update(missing);
		Check(step.State == CommandState::Failed,
			"missing target without tombstone was accepted as acquisition");

		Check(controller.Start(acquire, initial), "tombstone acquisition start failed");
		(void)controller.Update(Observation(2, 11));
		ObservationSnapshot tombstone = Observation(3, 12);
		tombstone.Targets[0].Deleted = true;
		tombstone.Targets[0].Interactable = false;
		tombstone.Targets[0].Acquirable = false;
		TargetSnapshot inventory = Target(3);
		inventory.Identity = "actor:201:NanoKey1";
		inventory.OwnerIdentity = tombstone.PlayerIdentity;
		inventory.Interactable = false;
		inventory.Acquirable = false;
		tombstone.Targets.push_back(inventory);
		step = controller.Update(tombstone);
		Check(step.State == CommandState::Succeeded,
			"tombstone plus matching inventory increase did not complete acquisition");

		ObservationSnapshot stackedInitial = Observation(1, 10);
		stackedInitial.Targets[0].Identity = "actor:100:Ammo10mm1";
		stackedInitial.Targets[0].ClassName = "DeusEx.Ammo10mm";
		stackedInitial.Targets[0].InventoryResource =
			TargetSnapshot::InventoryResourceSnapshot{
				TargetSnapshot::InventoryResourceKind::AmmoAmount, 10 };
		TargetSnapshot existingAmmo = stackedInitial.Targets[0];
		existingAmmo.Identity = "actor:201:Ammo10mm0";
		existingAmmo.OwnerIdentity = stackedInitial.PlayerIdentity;
		existingAmmo.StateToken = "Idle2";
		existingAmmo.InventoryResource->Value = 20;
		existingAmmo.Interactable = false;
		existingAmmo.Acquirable = false;
		stackedInitial.Targets.push_back(existingAmmo);
		AutomationCommand stackedAcquire = acquire;
		stackedAcquire.Id = "acquire.ammo-stack";
		stackedAcquire.Target = TargetSelector{ 1, "actor:100:Ammo10mm1",
			"DeusEx.Ammo10mm" };
		Check(controller.Start(stackedAcquire, stackedInitial),
			"stacked-ammo acquisition start failed");
		ObservationSnapshot stackedRequest = stackedInitial;
		stackedRequest.Revision = 2;
		stackedRequest.Tick = 11;
		for (TargetSnapshot& snapshot : stackedRequest.Targets)
			snapshot.ObservationRevision = 2;
		step = controller.Update(stackedRequest);
		Check(step.State == CommandState::Running && step.RequestInteraction,
			"stacked-ammo acquisition did not request stock interaction");
		ObservationSnapshot mergedWithoutIncrease = stackedRequest;
		mergedWithoutIncrease.Revision = 3;
		mergedWithoutIncrease.Tick = 12;
		for (TargetSnapshot& snapshot : mergedWithoutIncrease.Targets)
			snapshot.ObservationRevision = 3;
		mergedWithoutIncrease.Targets[0].Deleted = true;
		mergedWithoutIncrease.Targets[0].Interactable = false;
		mergedWithoutIncrease.Targets[0].Acquirable = false;
		step = controller.Update(mergedWithoutIncrease);
		Check(step.State == CommandState::Running,
			"target tombstone without an ammo increase completed stacked acquisition");
		ObservationSnapshot substitutedResource = mergedWithoutIncrease;
		substitutedResource.Revision = 4;
		substitutedResource.Tick = 13;
		for (TargetSnapshot& snapshot : substitutedResource.Targets)
			snapshot.ObservationRevision = 4;
		substitutedResource.Targets[1].InventoryResource->Kind =
			TargetSnapshot::InventoryResourceKind::NumCopies;
		substitutedResource.Targets[1].InventoryResource->Value = 30;
		step = controller.Update(substitutedResource);
		Check(step.State == CommandState::Running,
			"a substituted inventory resource kind completed stacked acquisition");
		ObservationSnapshot unrelatedIncrease = substitutedResource;
		unrelatedIncrease.Revision = 5;
		unrelatedIncrease.Tick = 14;
		for (TargetSnapshot& snapshot : unrelatedIncrease.Targets)
			snapshot.ObservationRevision = 5;
		unrelatedIncrease.Targets[1].InventoryResource->Kind =
			TargetSnapshot::InventoryResourceKind::AmmoAmount;
		unrelatedIncrease.Targets[1].InventoryResource->Value = 31;
		step = controller.Update(unrelatedIncrease);
		Check(step.State == CommandState::Running,
			"non-target ammo increase completed stacked acquisition");
		ObservationSnapshot mergedAmmo = unrelatedIncrease;
		mergedAmmo.Revision = 6;
		mergedAmmo.Tick = 15;
		for (TargetSnapshot& snapshot : mergedAmmo.Targets)
			snapshot.ObservationRevision = 6;
		mergedAmmo.Targets[1].InventoryResource->Value = 30;
		step = controller.Update(mergedAmmo);
		Check(step.State == CommandState::Succeeded &&
			step.Reason.find("ammo_amount increase observed from 20 to 30") !=
				std::string::npos,
			"target tombstone plus exact owned-ammo delta did not complete acquisition");

		auto RequestStackedAcquisition = [&](const char* startMessage)
		{
			Check(controller.Start(stackedAcquire, stackedInitial), startMessage);
			ObservationSnapshot requestObservation = stackedInitial;
			SetObservationVersion(requestObservation, 2, 11);
			InteractionActionStep requestStep = controller.Update(requestObservation);
			Check(requestStep.State == CommandState::Running &&
				requestStep.RequestInteraction,
				"resource acquisition did not request stock interaction");
			return requestObservation;
		};
		auto TombstoneTarget = [](ObservationSnapshot& observation)
		{
			observation.Targets[0].Deleted = true;
			observation.Targets[0].Interactable = false;
			observation.Targets[0].Acquirable = false;
		};
		auto ReplacementStack = [&](uint64_t revision,
			std::optional<TargetSnapshot::InventoryResourceSnapshot> resource)
		{
			TargetSnapshot replacement = existingAmmo;
			replacement.ObservationRevision = revision;
			replacement.Identity = "actor:202:Ammo10mmReplacement";
			replacement.InventoryResource = resource;
			return replacement;
		};

		ObservationSnapshot wrongAmount = RequestStackedAcquisition(
			"wrong-amount replacement acquisition start failed");
		SetObservationVersion(wrongAmount, 3, 12);
		TombstoneTarget(wrongAmount);
		wrongAmount.Targets.push_back(ReplacementStack(3,
			TargetSnapshot::InventoryResourceSnapshot{
				TargetSnapshot::InventoryResourceKind::AmmoAmount, 11 }));
		step = controller.Update(wrongAmount);
		Check(step.State == CommandState::Running,
			"same-class count increase bypassed the exact resource amount proof");
		ObservationSnapshot exactReplacement = wrongAmount;
		SetObservationVersion(exactReplacement, 4, 13);
		exactReplacement.Targets.back().InventoryResource->Value = 10;
		step = controller.Update(exactReplacement);
		Check(step.State == CommandState::Succeeded &&
			step.Reason.find("ammo_amount increase observed from 20 to 30") !=
				std::string::npos,
			"exact replacement stack did not satisfy resource proof when class count rose");

		ObservationSnapshot wrongKind = RequestStackedAcquisition(
			"wrong-kind replacement acquisition start failed");
		SetObservationVersion(wrongKind, 3, 12);
		TombstoneTarget(wrongKind);
		wrongKind.Targets.push_back(ReplacementStack(3,
			TargetSnapshot::InventoryResourceSnapshot{
				TargetSnapshot::InventoryResourceKind::NumCopies, 10 }));
		step = controller.Update(wrongKind);
		Check(step.State == CommandState::Running,
			"same-class count increase bypassed the exact resource-kind proof");

		ObservationSnapshot missingResource = RequestStackedAcquisition(
			"resource-less replacement acquisition start failed");
		SetObservationVersion(missingResource, 3, 12);
		TombstoneTarget(missingResource);
		missingResource.Targets.push_back(ReplacementStack(3, std::nullopt));
		step = controller.Update(missingResource);
		Check(step.State == CommandState::Running,
			"resource-less same-class replacement bypassed resource proof");

		ObservationSnapshot noOwnedStack = stackedInitial;
		noOwnedStack.Targets.pop_back();
		Check(controller.Start(stackedAcquire, noOwnedStack),
			"zero-baseline resource acquisition start failed");
		ObservationSnapshot zeroBaselineRequest = noOwnedStack;
		SetObservationVersion(zeroBaselineRequest, 2, 11);
		step = controller.Update(zeroBaselineRequest);
		Check(step.State == CommandState::Running && step.RequestInteraction,
			"zero-baseline resource acquisition did not request stock interaction");
		ObservationSnapshot zeroToTen = zeroBaselineRequest;
		SetObservationVersion(zeroToTen, 3, 12);
		TombstoneTarget(zeroToTen);
		zeroToTen.Targets.push_back(ReplacementStack(3,
			TargetSnapshot::InventoryResourceSnapshot{
				TargetSnapshot::InventoryResourceKind::AmmoAmount, 10 }));
		step = controller.Update(zeroToTen);
		Check(step.State == CommandState::Succeeded &&
			step.Reason.find("ammo_amount increase observed from 0 to 10") !=
				std::string::npos,
			"exact zero-baseline resource replacement did not complete acquisition");

		ObservationSnapshot resourceOwnership = RequestStackedAcquisition(
			"resource ownership-transfer acquisition start failed");
		SetObservationVersion(resourceOwnership, 3, 12);
		resourceOwnership.Targets[0].OwnerIdentity = resourceOwnership.PlayerIdentity;
		resourceOwnership.Targets[0].Interactable = false;
		resourceOwnership.Targets[0].Acquirable = false;
		step = controller.Update(resourceOwnership);
		Check(step.State == CommandState::Succeeded &&
			step.Reason == "exact target ownership transferred to the player",
			"resource-bearing exact-target ownership transfer regressed");

		AutomationCommand interact = ActorCommand(CommandKind::Interact);
		Check(controller.Start(interact, initial), "valid interaction was rejected");
		(void)controller.Update(Observation(2, 11));
		ObservationSnapshot transitioned = Observation(3, 12);
		transitioned.Targets[0].StateToken = "Open";
		step = controller.Update(transitioned);
		Check(step.State == CommandState::Succeeded,
			"target state transition did not complete interaction");

		ObservationSnapshot eventInitial = initial;
		eventInitial.Targets[0].EventName = "TrainingDoor";
		TargetSnapshot door = Target(1);
		door.Identity = "actor:300:DeusExMover0";
		door.ClassName = "DeusEx.DeusExMover";
		door.TagName = "TrainingDoor";
		door.EventName.clear();
		door.StateToken = "TriggerToggle";
		door.Interactable = false;
		door.Acquirable = false;
		eventInitial.Targets.push_back(door);
		Check(controller.Start(interact, eventInitial), "event interaction was rejected");
		ObservationSnapshot eventRequest = eventInitial;
		eventRequest.Revision = 2;
		eventRequest.Tick = 11;
		for (TargetSnapshot& snapshot : eventRequest.Targets)
			snapshot.ObservationRevision = 2;
		eventRequest.Targets[0].Interactable = true;
		step = controller.Update(eventRequest);
		Check(step.State == CommandState::Running && step.RequestInteraction,
			"event interaction did not request stock input");
		ObservationSnapshot jitteredDoor = eventRequest;
		jitteredDoor.Revision = 3;
		jitteredDoor.Tick = 12;
		for (TargetSnapshot& snapshot : jitteredDoor.Targets)
			snapshot.ObservationRevision = 3;
		jitteredDoor.Targets[1].Location.Y += 0.0005;
		step = controller.Update(jitteredDoor);
		Check(step.State == CommandState::Running,
			"sub-threshold event receiver jitter completed interaction");
		ObservationSnapshot movedDoor = eventRequest;
		movedDoor.Revision = 4;
		movedDoor.Tick = 13;
		for (TargetSnapshot& snapshot : movedDoor.Targets)
			snapshot.ObservationRevision = 4;
		movedDoor.Targets[1].Location.Y += 64.0;
		step = controller.Update(movedDoor);
		Check(step.State == CommandState::Succeeded,
			"correlated event receiver movement did not complete interaction");

		ObservationSnapshot unrelatedInitial = eventInitial;
		unrelatedInitial.Targets[1].TagName = "AnotherDoor";
		Check(controller.Start(interact, unrelatedInitial), "unrelated interaction was rejected");
		ObservationSnapshot unrelatedRequest = unrelatedInitial;
		unrelatedRequest.Revision = 2;
		unrelatedRequest.Tick = 11;
		for (TargetSnapshot& snapshot : unrelatedRequest.Targets)
			snapshot.ObservationRevision = 2;
		unrelatedRequest.Targets[0].Interactable = true;
		(void)controller.Update(unrelatedRequest);
		ObservationSnapshot movedUnrelated = unrelatedRequest;
		movedUnrelated.Revision = 3;
		movedUnrelated.Tick = 12;
		for (TargetSnapshot& snapshot : movedUnrelated.Targets)
			snapshot.ObservationRevision = 3;
		movedUnrelated.Targets[1].Location.Y += 64.0;
		step = controller.Update(movedUnrelated);
		Check(step.State == CommandState::Running,
			"unrelated actor movement was mistaken for interaction success");

		AutomationCommand wait;
		wait.Id = "wait.two-ticks";
		wait.Kind = CommandKind::Wait;
		wait.IssuedTick = 10;
		wait.DeadlineTick = 20;
		wait.WaitTicks = 2;
		Check(controller.Start(wait, initial), "valid wait was rejected");
		Check(controller.Update(Observation(2, 11)).State == CommandState::Running,
			"wait completed too early");
		Check(controller.Update(Observation(3, 12)).State == CommandState::Succeeded,
			"wait did not complete at its exact tick");

		Check(controller.Start(wait, initial), "wait restart for abort failed");
		AutomationCommand abort;
		abort.Id = "abort.wait";
		abort.Kind = CommandKind::Abort;
		abort.IssuedTick = 10;
		abort.DeadlineTick = 15;
		abort.AbortCommandId = "another-command";
		InteractionActionStep cancelled;
		Check(!controller.Abort(abort, 11, 2, cancelled) && controller.IsActive(),
			"wrong-command abort cancelled active work");
		abort.AbortCommandId = wait.Id;
		Check(controller.Abort(abort, 11, 2, cancelled) &&
			cancelled.State == CommandState::Cancelled && !controller.IsActive(),
			"bounded abort did not cancel the named active command");

		ObservationSnapshot stale = initial;
		Check(controller.Start(acquire, initial), "stale-observation setup failed");
		step = controller.Update(stale);
		Check(step.State == CommandState::Failed,
			"replayed observation revision was accepted");
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
	return 0;
}
