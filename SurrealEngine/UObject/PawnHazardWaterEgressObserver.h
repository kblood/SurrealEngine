#pragma once

#include "Math/vec.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace PawnMovement
{
	constexpr size_t HazardWaterEgressMaximumQueuedDiagnostics = 128;

	enum class HazardWaterEgressTransitionSource : uint8_t
	{
		Unknown,
		FallingDirectSweep,
		FallingNonDirectSweep,
		SwimmingMotion,
	};

	enum class HazardWaterEgressTerminal : uint8_t
	{
		PrimaryZoneCleared,
		DeathBeforeExit,
		LifeReset,
		EpisodeAbandoned,
	};

	struct HazardWaterEgressEntry
	{
		uint64_t LifeId = 0;
		uint64_t EpisodeId = 0;
		HazardWaterEgressTransitionSource TransitionSource =
			HazardWaterEgressTransitionSource::Unknown;
		bool AnchorKnown = false;
		vec3 Anchor = vec3(0.0f);
		vec3 EntryLocation = vec3(0.0f);
		float DamagePerSecond = 0.0f;
		std::string MoveTargetName;
		bool MoveTargetLocationKnown = false;
		vec3 MoveTargetLocation = vec3(0.0f);
		vec3 Destination = vec3(0.0f);
	};

	struct HazardWaterEgressCandidate
	{
		std::string Name;
		vec3 Location = vec3(0.0f);
		float EntryDistance = 0.0f;
	};

	struct HazardWaterEgressDiagnosticRecord
	{
		std::string SourcePawnActor;
		uint64_t Sequence = 0;
		HazardWaterEgressEntry Entry;
		bool CandidateKnown = false;
		HazardWaterEgressCandidate Candidate;
		bool CandidateDistanceKnown = false;
		float MinimumCandidateDistance = 0.0f;
		float TerminalCandidateDistance = 0.0f;
		uint64_t CandidateProgressSamples = 0;
		uint64_t CandidateRegressionSamples = 0;
		bool TargetDistanceKnown = false;
		float EntryTargetDistance = 0.0f;
		float MinimumTargetDistance = 0.0f;
		float TerminalTargetDistance = 0.0f;
		uint64_t TargetProgressSamples = 0;
		uint64_t TargetRegressionSamples = 0;
		HazardWaterEgressTerminal Terminal =
			HazardWaterEgressTerminal::EpisodeAbandoned;
		vec3 TerminalLocation = vec3(0.0f);
		std::string TerminalMoveTargetName;
		vec3 TerminalDestination = vec3(0.0f);
	};

	class HazardWaterEgressObserver
	{
	public:
		explicit HazardWaterEgressObserver(std::string sourcePawnActor = {});

		bool BeginEpisode(const HazardWaterEgressEntry& entry);
		bool ObserveCandidate(const HazardWaterEgressCandidate& candidate);
		bool ObservePosition(const vec3& position);
		bool FinishEpisode(HazardWaterEgressTerminal terminal,
			const vec3& location, std::string moveTargetName,
			const vec3& destination);
		void EndLife(const vec3& location, std::string moveTargetName,
			const vec3& destination);
		void Abandon(const vec3& location, std::string moveTargetName,
			const vec3& destination);

		bool HasActiveEpisode() const { return Active; }
		uint64_t OverflowCount() const { return Overflows; }
		std::vector<HazardWaterEgressDiagnosticRecord> DrainDiagnostics();

	private:
		void Queue(HazardWaterEgressDiagnosticRecord record);

		std::string SourcePawnActor;
		uint64_t NextSequence = 1;
		uint64_t Overflows = 0;
		bool Active = false;
		HazardWaterEgressDiagnosticRecord Current;
		float LastCandidateDistance = 0.0f;
		float LastTargetDistance = 0.0f;
		std::vector<HazardWaterEgressDiagnosticRecord> Diagnostics;
	};

	const char* HazardWaterEgressTransitionSourceName(
		HazardWaterEgressTransitionSource source);
	const char* HazardWaterEgressTerminalName(HazardWaterEgressTerminal terminal);
}
