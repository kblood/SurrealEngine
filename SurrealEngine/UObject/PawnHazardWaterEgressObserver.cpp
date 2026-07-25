#include "PawnHazardWaterEgressObserver.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
	bool IsFinite(const vec3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y)
			&& std::isfinite(value.z);
	}

	float Distance(const vec3& first, const vec3& second)
	{
		return length(first - second);
	}
}

namespace PawnMovement
{
	HazardWaterEgressObserver::HazardWaterEgressObserver(
		std::string sourcePawnActor)
		: SourcePawnActor(std::move(sourcePawnActor))
	{
	}

	bool HazardWaterEgressObserver::BeginEpisode(const HazardWaterEgressEntry& entry)
	{
		if (Active || SourcePawnActor.empty() || entry.LifeId == 0
			|| entry.EpisodeId == 0 || !IsFinite(entry.EntryLocation)
			|| !std::isfinite(entry.DamagePerSecond))
		{
			return false;
		}
		Current = {};
		Current.SourcePawnActor = SourcePawnActor;
		Current.Sequence = NextSequence++;
		Current.Entry = entry;
		Active = true;
		if (entry.MoveTargetLocationKnown && IsFinite(entry.MoveTargetLocation))
		{
			const float distance = Distance(entry.EntryLocation,
				entry.MoveTargetLocation);
			if (std::isfinite(distance))
			{
				Current.TargetDistanceKnown = true;
				Current.EntryTargetDistance = distance;
				Current.MinimumTargetDistance = distance;
				Current.TerminalTargetDistance = distance;
				LastTargetDistance = distance;
			}
		}
		return true;
	}

	bool HazardWaterEgressObserver::ObserveCandidate(
		const HazardWaterEgressCandidate& candidate)
	{
		if (!Active || Current.CandidateKnown || candidate.Name.empty()
			|| !IsFinite(candidate.Location) || !std::isfinite(candidate.EntryDistance)
			|| candidate.EntryDistance < 0.0f)
		{
			return false;
		}
		Current.CandidateKnown = true;
		Current.Candidate = candidate;
		Current.CandidateDistanceKnown = true;
		Current.MinimumCandidateDistance = candidate.EntryDistance;
		Current.TerminalCandidateDistance = candidate.EntryDistance;
		LastCandidateDistance = candidate.EntryDistance;
		return true;
	}

	bool HazardWaterEgressObserver::ObservePosition(const vec3& position)
	{
		if (!Active || !IsFinite(position))
			return false;
		bool observed = false;
		constexpr float significantDistanceChange = 0.01f;
		if (Current.CandidateDistanceKnown)
		{
			const float distance = Distance(position, Current.Candidate.Location);
			if (std::isfinite(distance))
			{
				if (distance + significantDistanceChange < LastCandidateDistance)
					Current.CandidateProgressSamples++;
				else if (distance > LastCandidateDistance + significantDistanceChange)
					Current.CandidateRegressionSamples++;
				Current.MinimumCandidateDistance = std::min(
					Current.MinimumCandidateDistance, distance);
				Current.TerminalCandidateDistance = distance;
				LastCandidateDistance = distance;
				observed = true;
			}
		}
		if (Current.TargetDistanceKnown)
		{
			const float distance = Distance(position, Current.Entry.MoveTargetLocation);
			if (std::isfinite(distance))
			{
				if (distance + significantDistanceChange < LastTargetDistance)
					Current.TargetProgressSamples++;
				else if (distance > LastTargetDistance + significantDistanceChange)
					Current.TargetRegressionSamples++;
				Current.MinimumTargetDistance = std::min(
					Current.MinimumTargetDistance, distance);
				Current.TerminalTargetDistance = distance;
				LastTargetDistance = distance;
				observed = true;
			}
		}
		return observed;
	}

	bool HazardWaterEgressObserver::FinishEpisode(HazardWaterEgressTerminal terminal,
		const vec3& location, std::string moveTargetName, const vec3& destination)
	{
		if (!Active || !IsFinite(location) || !IsFinite(destination))
			return false;
		ObservePosition(location);
		Current.Terminal = terminal;
		Current.TerminalLocation = location;
		Current.TerminalMoveTargetName = std::move(moveTargetName);
		Current.TerminalDestination = destination;
		Queue(std::move(Current));
		Current = {};
		Active = false;
		LastCandidateDistance = 0.0f;
		LastTargetDistance = 0.0f;
		return true;
	}

	void HazardWaterEgressObserver::EndLife(const vec3& location,
		std::string moveTargetName, const vec3& destination)
	{
		FinishEpisode(HazardWaterEgressTerminal::LifeReset, location,
			std::move(moveTargetName), destination);
	}

	void HazardWaterEgressObserver::Abandon(const vec3& location,
		std::string moveTargetName, const vec3& destination)
	{
		FinishEpisode(HazardWaterEgressTerminal::EpisodeAbandoned, location,
			std::move(moveTargetName), destination);
	}

	std::vector<HazardWaterEgressDiagnosticRecord>
		HazardWaterEgressObserver::DrainDiagnostics()
	{
		std::vector<HazardWaterEgressDiagnosticRecord> diagnostics;
		diagnostics.swap(Diagnostics);
		return diagnostics;
	}

	void HazardWaterEgressObserver::Queue(HazardWaterEgressDiagnosticRecord record)
	{
		if (Diagnostics.size() < HazardWaterEgressMaximumQueuedDiagnostics)
			Diagnostics.push_back(std::move(record));
		else
			Overflows++;
	}

	const char* HazardWaterEgressTransitionSourceName(
		HazardWaterEgressTransitionSource source)
	{
		switch (source)
		{
		case HazardWaterEgressTransitionSource::FallingDirectSweep:
			return "falling_direct_sweep";
		case HazardWaterEgressTransitionSource::FallingNonDirectSweep:
			return "falling_non_direct_sweep";
		case HazardWaterEgressTransitionSource::SwimmingMotion:
			return "swimming_motion";
		default:
			return "unknown";
		}
	}

	const char* HazardWaterEgressTerminalName(HazardWaterEgressTerminal terminal)
	{
		switch (terminal)
		{
		case HazardWaterEgressTerminal::PrimaryZoneCleared:
			return "primary_zone_cleared";
		case HazardWaterEgressTerminal::DeathBeforeExit:
			return "death_before_exit";
		case HazardWaterEgressTerminal::LifeReset:
			return "life_reset";
		default:
			return "episode_abandoned";
		}
	}
}
