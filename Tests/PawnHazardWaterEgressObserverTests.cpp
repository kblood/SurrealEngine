#include "UObject/PawnHazardWaterEgressObserver.h"

#include <cstdlib>
#include <iostream>

namespace
{
	using namespace PawnMovement;

	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			std::exit(1);
		}
	}

	HazardWaterEgressEntry Entry(uint64_t episode = 1)
	{
		HazardWaterEgressEntry entry;
		entry.LifeId = 3;
		entry.EpisodeId = episode;
		entry.TransitionSource = HazardWaterEgressTransitionSource::FallingDirectSweep;
		entry.AnchorKnown = true;
		entry.Anchor = vec3(1.0f, 2.0f, 3.0f);
		entry.EntryLocation = vec3(0.0f, 0.0f, 0.0f);
		entry.DamagePerSecond = 20.0f;
		entry.MoveTargetName = "PathNode144";
		entry.MoveTargetLocationKnown = true;
		entry.MoveTargetLocation = vec3(100.0f, 0.0f, 0.0f);
		entry.Destination = vec3(100.0f, 0.0f, 0.0f);
		return entry;
	}
}

int main()
{
	HazardWaterEgressObserver observer("Bot2");
	Check(observer.BeginEpisode(Entry()), "entry must start an episode");
	Check(!observer.BeginEpisode(Entry(2)), "episodes cannot overlap");
	Check(observer.ObserveStaticWalkCertificate({
		"certified_static_walk_continuation", true, "PathNode12", true,
		"PathNode13", 42.0f, 1, 2 }),
		"a static-walk certificate must be retained once");
	Check(!observer.ObserveStaticWalkCertificate({
		"no_static_walk_continuation", false, "", false, "", 0.0f, 0, 1 }),
		"the certificate witness must remain immutable");
	Check(observer.ObserveCandidate({ "PathNode12", vec3(10.0f, 0.0f, 0.0f), 10.0f }),
		"first safe candidate must be retained");
	Check(!observer.ObserveCandidate({ "PathNode13", vec3(20.0f, 0.0f, 0.0f), 20.0f }),
		"candidate witness must remain immutable");
	Check(observer.ObservePosition(vec3(25.0f, 0.0f, 0.0f)),
		"target-distance progress must be observed");
	Check(observer.ObservePosition(vec3(20.0f, 0.0f, 0.0f)),
		"additional target-distance progress must be observed");
	Check(observer.ObservePosition(vec3(30.0f, 0.0f, 0.0f)),
		"target-distance regression must be observed");
	Check(observer.FinishEpisode(HazardWaterEgressTerminal::PrimaryZoneCleared,
		vec3(35.0f, 0.0f, 0.0f), "PathNode145", vec3(110.0f, 0.0f, 0.0f)),
		"exit must finish an active episode");
	auto diagnostics = observer.DrainDiagnostics();
	Check(diagnostics.size() == 1, "completed episode must emit one witness");
	const auto& exited = diagnostics.front();
	Check(exited.Entry.MoveTargetName == "PathNode144"
		&& exited.TerminalMoveTargetName == "PathNode145",
		"entry target must survive later target changes");
	Check(exited.CandidateKnown && exited.Candidate.Name == "PathNode12",
		"first candidate witness must be retained");
	Check(exited.StaticWalkCertificate.Result == "certified_static_walk_continuation"
		&& exited.StaticWalkCertificate.ContinuationName == "PathNode13",
		"static-walk certificate evidence must survive the terminal record");
	Check(exited.CandidateProgressSamples == 1
		&& exited.CandidateRegressionSamples == 3,
		"candidate progress signs must include terminal observation");
	Check(exited.TargetProgressSamples == 3 && exited.TargetRegressionSamples == 1,
		"progress signs must include terminal observation");
	Check(exited.Terminal == HazardWaterEgressTerminal::PrimaryZoneCleared,
		"exit terminal must be exact");

	Check(observer.BeginEpisode(Entry(2)), "next episode must start");
	observer.EndLife(vec3(0.0f), "", vec3(0.0f));
	diagnostics = observer.DrainDiagnostics();
	Check(diagnostics.size() == 1
		&& diagnostics.front().Terminal == HazardWaterEgressTerminal::LifeReset,
		"life reset must close active episode");

	for (size_t index = 0; index < HazardWaterEgressMaximumQueuedDiagnostics + 1; index++)
	{
		Check(observer.BeginEpisode(Entry(3 + index)), "overflow episode must start");
		Check(observer.FinishEpisode(HazardWaterEgressTerminal::DeathBeforeExit,
			vec3(0.0f), "", vec3(0.0f)), "overflow episode must finish");
	}
	diagnostics = observer.DrainDiagnostics();
	Check(diagnostics.size() == HazardWaterEgressMaximumQueuedDiagnostics
		&& observer.OverflowCount() == 1, "queue must be bounded with an exact overflow count");
	return 0;
}
