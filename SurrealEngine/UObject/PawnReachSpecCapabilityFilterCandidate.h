#pragma once

#include <cstdlib>

// Temporary evaluation gate for enforcing ReachSpec capability flags during route
// search. UnReach.h's FReachSpec::supports() requires every flag an edge carries to
// be present in the pawn's movement mask, but the four route-expansion sites in
// UActor.cpp admit edges without consulting reachFlags at all, so a route may
// contain a link whose movement type the pawn cannot perform. Opt-in via env var
// only, for A/B benchmarking before this becomes the default.
inline bool PawnReachSpecCapabilityFilterCandidateEnabled()
{
	static const bool enabled = std::getenv("SURREAL_REACHSPEC_CAPABILITY_FILTER") != nullptr;
	return enabled;
}
