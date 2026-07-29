#pragma once

#include <cstdlib>

// Temporary evaluation gate for a bundle of mover/actor-collision correctness fixes
// (TickWalking's unhandled non-pushable-actor branch, mover-vs-projectile blocking
// asymmetry, mover step/turn safety, EncroachingActors's counter bug, and the
// depenetration/unstick fix for a pawn starting a move already inside another
// actor's cylinder). Opt-in via env var only, for A/B benchmarking before deciding
// whether to keep these unconditionally.
inline bool MoverActorCollisionFidelityCandidateEnabled()
{
	static const bool enabled = std::getenv("SURREAL_MOVER_ACTOR_COLLISION_FIDELITY_CANDIDATE") != nullptr;
	return enabled;
}
