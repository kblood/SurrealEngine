#pragma once

#include <cstdint>
#include <string>

namespace PawnMovement
{
	enum class DirectReachCommandCallerOrigin
	{
		ScriptActorReachable,
		PathSpecialHandling,
		FindPathToEndPoint,
		FindRandomDest,
		Unknown
	};

	enum class DirectReachCommandRejectReason
	{
		NullActor,
		Distance,
		NavpointReachSpec,
		PainZone,
		Water,
		Trace,
		CheckLocation,
		WalkSimulation,
		UnsupportedPhysics,
		Reached
	};

	inline const char* DirectReachCommandCallerOriginName(DirectReachCommandCallerOrigin origin)
	{
		switch (origin)
		{
		case DirectReachCommandCallerOrigin::ScriptActorReachable: return "script_actor_reachable";
		case DirectReachCommandCallerOrigin::PathSpecialHandling: return "path_special_handling";
		case DirectReachCommandCallerOrigin::FindPathToEndPoint: return "find_path_to_end_point";
		case DirectReachCommandCallerOrigin::FindRandomDest: return "find_random_dest";
		case DirectReachCommandCallerOrigin::Unknown: return "unknown";
		}
		return "unknown";
	}

	inline const char* DirectReachCommandRejectReasonName(DirectReachCommandRejectReason reason)
	{
		switch (reason)
		{
		case DirectReachCommandRejectReason::NullActor: return "null_actor";
		case DirectReachCommandRejectReason::Distance: return "distance";
		case DirectReachCommandRejectReason::NavpointReachSpec: return "navpoint_reachspec";
		case DirectReachCommandRejectReason::PainZone: return "pain_zone";
		case DirectReachCommandRejectReason::Water: return "water";
		case DirectReachCommandRejectReason::Trace: return "trace";
		case DirectReachCommandRejectReason::CheckLocation: return "check_location";
		case DirectReachCommandRejectReason::WalkSimulation: return "walk_simulation";
		case DirectReachCommandRejectReason::UnsupportedPhysics: return "unsupported_physics";
		case DirectReachCommandRejectReason::Reached: return "reached";
		}
		return "unknown";
	}

	// This record is intentionally native-only until the benchmark driver
	// resolves its live actor pointer into a same-tick command witness.
	struct DirectReachCommandObservation
	{
		uint64_t Sequence = 0;
		uint64_t LifeId = 0;
		uint64_t NativeTick = 0;
		int32_t TargetActorIndex = -1;
		const void* TargetAddress = nullptr;
		std::string TargetName;
		std::string TargetClass;
		bool TargetIsInventory = false;
		bool MarkerKnown = false;
		bool MarkerLive = false;
		int32_t MarkerActorIndex = -1;
		const void* MarkerAddress = nullptr;
		std::string MarkerName;
		std::string MarkerClass;
		bool Reached = false;
		bool CheckNavpoint = false;
		DirectReachCommandCallerOrigin CallerOrigin =
			DirectReachCommandCallerOrigin::Unknown;
		DirectReachCommandRejectReason RejectReason =
			DirectReachCommandRejectReason::WalkSimulation;
		bool ResolvedWallSlide = false;
		int WalkingSimulationIterations = 0;
	};
}
