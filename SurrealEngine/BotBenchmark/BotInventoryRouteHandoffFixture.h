#pragma once

#include <cstdint>
#include <string>

class Engine;
class HeadlessDriverRegistry;

// A map-owned reproduction fixture for the DeathFan inventory handoff and its
// remaining direct-navigation false-positive class. It verifies those direct
// reach decisions without enabling a production behavior policy.
struct BotInventoryRouteHandoffFixtureConfig
{
	std::string URL;
	int ExternalSkill = 3;
};

struct BotInventoryRouteHandoffFixtureResult
{
	bool Ran = false;
	bool Passed = false;
	bool SafeWalkingAnchor = false;
	bool DirectMarkerRejected = false;
	bool GraphFirstHopSelected = false;
	bool SafeMarkerReachable = false;
	bool GraphFallbackExists = false;
	bool UnsupportedCorridorSample = false;
	bool HarmfulZoneBelowCorridor = false;
	bool NavigationAnchorSafe = false;
	bool DirectNavigationReachable = false;
	bool NavigationGraphFirstHopSelected = false;
	bool NavigationFallbackRouteExists = false;
	bool NavigationFallbackEndpointDirectReachable = false;
	bool NavigationFallbackEndpointUnsupportedCorridorSample = false;
	bool NavigationFallbackEndpointHarmfulZoneBelowCorridor = false;
	bool NavigationUnsupportedCorridorSample = false;
	bool NavigationHarmfulZoneBelowCorridor = false;
	bool StationaryNavigationAnchorFallingObserved = false;
	bool LiveNavigationMoveTowardArmed = false;
	bool LiveNavigationFallingObserved = false;
	bool LiveNavigationHarmfulEntryObserved = false;
	std::string FailureReason;
	std::string PawnActor;
	std::string MarkerActor;
	std::string InventoryActor;
	std::string SafeMarkerActor;
	std::string SelectedFirstHopActor;
	std::string NavigationActor;
	std::string NavigationFirstHopActor;
	std::string NavigationFallbackFirstHopActor;
	std::string NavigationFallbackEndpointActor;
	uint64_t GraphEdgeCount = 0;
	uint64_t ImmediateSupportSamples = 0;
	uint64_t UnsupportedSamples = 0;
	uint64_t HarmfulBelowSamples = 0;
	uint64_t DirectMarkerRejects = 0;
	uint64_t NavigationCandidateCount = 0;
	uint64_t NavigationCandidateDirectReachableCount = 0;
	uint64_t NavigationCandidateSafeDirectReachableCount = 0;
	uint64_t NavigationCandidateUnsafeDirectReachableCount = 0;
	uint64_t LiveNavigationTicks = 0;
	float FirstHarmfulBelowDistance = 0.0f;
	std::string FirstSafeNavigationCandidateActor;
	std::string FirstUnsafeNavigationCandidateActor;
};

class BotInventoryRouteHandoffFixture
{
public:
	static BotInventoryRouteHandoffFixtureResult Run(
		Engine& engine, const BotInventoryRouteHandoffFixtureConfig& config);
};

void RegisterBotInventoryRouteHandoffFixtureDriver(HeadlessDriverRegistry& registry);
