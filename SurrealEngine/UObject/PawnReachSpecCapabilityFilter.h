#pragma once

#include <cstdint>
#include <cstdlib>

namespace PawnMovement
{
	// FReachSpec::supports(): every movement flag required by an edge must be
	// present in the pawn's capability mask. A zero requirement is traversable,
	// while an unknown bit is absent from the mask and therefore rejects the edge.
	constexpr bool ReachSpecSupportedByCapabilities(uint32_t reachFlags, uint32_t capabilityMask)
	{
		return (reachFlags & ~capabilityMask) == 0u;
	}

	// Keep the filter opt-in while the preserved benchmark corpus has no edge
	// whose requirements differ from the live bot capability mask.
	inline bool ReachSpecCapabilityFilterEnabled()
	{
		static const bool enabled = std::getenv("SURREAL_REACHSPEC_CAPABILITY_FILTER") != nullptr;
		return enabled;
	}
}
