#pragma once

#include "UObject/PawnWalkingHitWallDispatch.h"

#include "Math/vec.h"

#include <cstdint>
#include <string>
#include <vector>

class Engine;
class HeadlessDriverRegistry;

// A deliberately narrow native fixture for the walking forward-contact / aligned-slide
// contract. It uses an ordinary controlled bot match, then suppresses only the fixture
// pawn's UnrealScript HitWall body so native callback dispatch and collision ordering can
// be observed without stock state handlers changing the second contact.
struct BotWalkingHitWallCornerFixtureConfig
{
	std::string URL;
	int ExternalSkill = 3;
	float ElapsedSeconds = 0.5f;
};

struct BotWalkingHitWallCornerFixtureHookContact
{
	vec3 Normal;
	std::string WallActor;
};

struct BotWalkingHitWallCornerFixtureResult
{
	bool Ran = false;
	bool Passed = false;
	bool FlatPathVerified = false;
	bool ExactBlockersVerified = false;
	std::string FailureReason;
	vec3 Start;
	vec3 FirstBlocker;
	vec3 SecondBlocker;
	uint64_t SuppressedHitWallCalls = 0;
	std::vector<BotWalkingHitWallCornerFixtureHookContact> HookContacts;
	std::vector<PawnMovement::WalkingHitWallDispatchDiagnosticRecord> Diagnostics;
};

class BotWalkingHitWallCornerFixture
{
public:
	static BotWalkingHitWallCornerFixtureResult Run(
		Engine& engine, const BotWalkingHitWallCornerFixtureConfig& config);
};

void RegisterBotWalkingHitWallCornerFixtureDriver(HeadlessDriverRegistry& registry);
