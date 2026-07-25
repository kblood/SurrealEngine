#pragma once

#include <cstdint>
#include <string>

class Engine;
class HeadlessDriverRegistry;

// A map-owned reproduction fixture for the DeathFan inventory handoff. It
// verifies the specific direct-reach decision that replaces a safe navigation
// route with a direct MoveToward target, without enabling a behavior policy.
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
	std::string FailureReason;
	std::string PawnActor;
	std::string MarkerActor;
	std::string InventoryActor;
	std::string SafeMarkerActor;
	std::string SelectedFirstHopActor;
	uint64_t GraphEdgeCount = 0;
	uint64_t ImmediateSupportSamples = 0;
	uint64_t UnsupportedSamples = 0;
	uint64_t HarmfulBelowSamples = 0;
	uint64_t DirectMarkerRejects = 0;
	float FirstHarmfulBelowDistance = 0.0f;
};

class BotInventoryRouteHandoffFixture
{
public:
	static BotInventoryRouteHandoffFixtureResult Run(
		Engine& engine, const BotInventoryRouteHandoffFixtureConfig& config);
};

void RegisterBotInventoryRouteHandoffFixtureDriver(HeadlessDriverRegistry& registry);
