#pragma once

#include <cstdint>
#include <string>

namespace PawnMovement
{
	// Read-only witness for a native MoveTo or MoveToward dispatch. The native
	// movement primitive remains the sole owner of command state.
	struct MovementCommandProvenanceObservation
	{
		uint64_t Sequence = 0;
		uint64_t CommandToken = 0;
		uint64_t LifeId = 0;
		uint64_t NativeTick = 0;
		int32_t SourceActorIndex = -1;
		uint64_t CallerInvocationToken = 0;
		std::string CallerClass;
		std::string CallerFunction;
		std::string Kind;
		bool TargetKnown = false;
		int32_t TargetActorIndex = -1;
		const void* TargetAddress = nullptr;
		std::string TargetName;
		std::string TargetClass;
		bool RouteHeadKnown = false;
		int32_t RouteHeadActorIndex = -1;
		std::string RouteHeadName;
		std::string RouteHeadClass;
		bool LastNativePathCommitKnown = false;
		bool LastNativePathCommitCacheClear = false;
		uint64_t LastNativePathCommitSequence = 0;
		int32_t LastNativePathCommitFirstReachSpecIndex = -1;
		bool LastNativePathCommitRouteHeadKnown = false;
		int32_t LastNativePathCommitRouteHeadActorIndex = -1;
		std::string LastNativePathCommitRouteHeadName;
		std::string LastNativePathCommitRouteHeadClass;
		bool IntegrityValid = false;
	};
}
