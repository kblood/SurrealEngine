#include "Automation/ShadowReplayAdapter.h"

#include <iostream>
#include <string>
#include <utility>

using namespace Automation;

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

	void Check(const ShadowReplayProjection& result, const std::string& message)
	{
		Check(static_cast<bool>(result),
			message + (result.Error.empty() ? std::string() : ": " + result.Error));
	}

	ObservationSnapshot Observation()
	{
		ObservationSnapshot observation;
		observation.Revision = 7;
		observation.Tick = 10;
		observation.PlayerIdentity = "actor:DeusEx.JCDentonMale:JCDentonMale0#0";
		observation.PlayerPosition = { 0.0, 0.0, 0.0 };

		TargetSnapshot light;
		light.ObservationRevision = 7;
		light.Identity = "actor:Engine.Light:Light155#0";
		light.ClassName = "Engine.Light";
		light.Location = { 100.0, 0.0, 0.0 };
		light.Reachable = true;
		observation.Targets.push_back(light);

		TargetSnapshot item;
		item.ObservationRevision = 7;
		item.Identity = "actor:DeusEx.Ammo10mm:Ammo10mm0#0";
		item.ClassName = "DeusEx.Ammo10mm";
		item.Location = { 120.0, 0.0, 0.0 };
		item.Reachable = true;
		item.Interactable = true;
		item.Acquirable = true;
		observation.Targets.push_back(item);

		TargetSnapshot switchTarget;
		switchTarget.ObservationRevision = 7;
		switchTarget.Identity = "actor:DeusEx.Switch1:Switch1#0";
		switchTarget.ClassName = "DeusEx.Switch1";
		switchTarget.Location = { 140.0, 0.0, 0.0 };
		switchTarget.Interactable = true;
		observation.Targets.push_back(switchTarget);
		return observation;
	}

	ShadowReplayCandidate ActorCandidate(CommandKind kind, std::string identity,
		std::string className)
	{
		ShadowReplayCandidate candidate;
		candidate.Kind = kind;
		candidate.ObservationRevision = 7;
		candidate.Target = ShadowReplayTarget{ std::move(identity), std::move(className) };
		candidate.ArrivalRadius = kind == CommandKind::WalkToActor ?
			ShadowWalkArrivalRadius : ShadowActorActionArrivalRadius;
		return candidate;
	}

	void TestValidProjections()
	{
		ObservationSnapshot observation = Observation();
		ShadowReplayCandidate walk = ActorCandidate(CommandKind::WalkToActor,
			"actor:Engine.Light:Light155#0", "Engine.Light");
		ShadowReplayProjection first = ProjectShadowReplayCandidate(
			walk, observation, "shadow-replay-1", 90);
		ShadowReplayProjection second = ProjectShadowReplayCandidate(
			walk, observation, "shadow-replay-1", 90);
		Check(first && second, "reachable actor walk projects to valid commands");
		Check(first.Command && first.Command->IssuedTick == 10 &&
			first.Command->DeadlineTick == 100 && first.Command->ArrivalRadius == 40.0,
			"actor walk projection preserves deterministic ticks and radius");
		Check(first.Command && second.Command &&
			CommandDigest(*first.Command) == CommandDigest(*second.Command),
			"repeated actor projection has a stable command digest");
		Check(first.Command && CommandDigest(*first.Command) == "fnv1a64:2d277f74fcb0eeae",
			"actor projection matches the canonical command digest");

		ShadowReplayCandidate acquire = ActorCandidate(CommandKind::AcquireItem,
			"actor:DeusEx.Ammo10mm:Ammo10mm0#0", "DeusEx.Ammo10mm");
		Check(ProjectShadowReplayCandidate(acquire, observation, "shadow-replay-2", 120),
			"acquirable target projects through the acquire capability gate");

		ShadowReplayCandidate interact = ActorCandidate(CommandKind::Interact,
			"actor:DeusEx.Switch1:Switch1#0", "DeusEx.Switch1");
		Check(ProjectShadowReplayCandidate(interact, observation, "shadow-replay-3", 120),
			"interactable target projects through the interaction capability gate");

		ShadowReplayCandidate wait;
		wait.Kind = CommandKind::Wait;
		wait.ObservationRevision = 7;
		wait.WaitTicks = 60;
		ShadowReplayProjection waitProjection = ProjectShadowReplayCandidate(
			wait, observation, "shadow-replay-4", 60);
		Check(waitProjection && waitProjection.Command &&
			waitProjection.Command->WaitTicks == 60 && !waitProjection.Command->Target,
			"bounded wait projects without an actor target");
	}

	void TestAuthorityAndShapeRejections()
	{
		ObservationSnapshot observation = Observation();
		ShadowReplayCandidate walk = ActorCandidate(CommandKind::WalkToActor,
			"actor:Engine.Light:Light155#0", "Engine.Light");
		walk.DispatchAuthorized = true;
		Check(ProjectShadowReplayCandidate(walk, observation, "shadow-replay-1", 90).Status ==
			ShadowReplayStatus::DispatchFlagSet,
			"a candidate claiming dispatch authority is rejected");
		walk.DispatchAuthorized = false;
		walk.ObservationRevision = 6;
		Check(ProjectShadowReplayCandidate(walk, observation, "shadow-replay-1", 90).Status ==
			ShadowReplayStatus::StaleObservation,
			"stale observation revisions are rejected");
		walk.ObservationRevision = 7;
		walk.ArrivalRadius = ShadowActorActionArrivalRadius;
		Check(ProjectShadowReplayCandidate(walk, observation, "shadow-replay-1", 90).Status ==
			ShadowReplayStatus::InvalidCandidate,
			"candidate-controlled actor-walk radii are rejected");
		walk.ArrivalRadius = ShadowWalkArrivalRadius;
		walk.Target->ExpectedClass.clear();
		Check(ProjectShadowReplayCandidate(walk, observation, "shadow-replay-1", 90).Status ==
			ShadowReplayStatus::InvalidCandidate,
			"shadow targets cannot omit the class binding");

		ShadowReplayCandidate unsupported;
		unsupported.Kind = CommandKind::Abort;
		unsupported.ObservationRevision = 7;
		Check(ProjectShadowReplayCandidate(unsupported, observation, "shadow-replay-1", 90).Status ==
			ShadowReplayStatus::UnsupportedKind,
			"abort cannot be projected from a shadow proposal");

		ShadowReplayCandidate wait;
		wait.Kind = CommandKind::Wait;
		wait.ObservationRevision = 7;
		wait.WaitTicks = MaximumShadowReplayWaitTicks + 1;
		Check(ProjectShadowReplayCandidate(wait, observation, "shadow-replay-1", 700).Status ==
			ShadowReplayStatus::InvalidCandidate,
			"shadow wait remains bounded independently of the lease");
	}

	void TestTargetAndObservationRejections()
	{
		ObservationSnapshot observation = Observation();
		ShadowReplayCandidate classMismatch = ActorCandidate(CommandKind::WalkToActor,
			"actor:Engine.Light:Light155#0", "DeusEx.Switch1");
		ShadowReplayProjection mismatch = ProjectShadowReplayCandidate(
			classMismatch, observation, "shadow-replay-1", 90);
		Check(mismatch.Status == ShadowReplayStatus::TargetRejected &&
			mismatch.TargetStatus == TargetResolutionStatus::ClassMismatch,
			"class substitution is rejected by target resolution");

		ShadowReplayCandidate acquireLight = ActorCandidate(CommandKind::AcquireItem,
			"actor:Engine.Light:Light155#0", "Engine.Light");
		ShadowReplayProjection notAcquirable = ProjectShadowReplayCandidate(
			acquireLight, observation, "shadow-replay-1", 90);
		Check(notAcquirable.Status == ShadowReplayStatus::TargetRejected &&
			notAcquirable.TargetStatus == TargetResolutionStatus::NotAcquirable,
			"action capability substitution is rejected by target resolution");

		observation.Targets.push_back(observation.Targets.front());
		Check(ProjectShadowReplayCandidate(acquireLight, observation, "shadow-replay-1", 90).Status ==
			ShadowReplayStatus::InvalidObservation,
			"malformed observations are rejected before projection");
	}
}

int main()
{
	TestValidProjections();
	TestAuthorityAndShapeRejections();
	TestTargetAndObservationRejections();
	if (Failures == 0)
		std::cout << "Shadow replay adapter tests passed\n";
	return Failures == 0 ? 0 : 1;
}
