#pragma once

#include "UObject/PawnWalkingHitWallDispatch.h"

#include "Math/vec.h"

#include <cstdint>
#include <string>
#include <vector>

class Engine;
class HeadlessDriverRegistry;

// A deliberately narrow native fixture for the walking forward-contact / aligned-slide
// contract. It uses an ordinary controlled bot match, enters a reflected stock state
// with a real HitWall body, and observes only the fixture pawn's normal VM returns.
// Bump is disabled only for the fixture tick so it cannot perturb the two HitWall
// contacts.
struct BotWalkingHitWallCornerFixtureConfig
{
	std::string URL;
	std::string FixtureState = "FindAir";
	int ExternalSkill = 3;
	float ElapsedSeconds = 1.0f;
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
	bool StockStateHandlerResolved = false;
	bool ScriptHitWallBodiesExecuted = false;
	bool FixtureBlockersDestroyedAfterSecondReturn = false;
	std::string FailureReason;
	std::string EntryState;
	std::string FixtureState;
	std::string ResolvedHitWallFunction;
	vec3 Start;
	vec3 Heading;
	vec3 FirstBlocker;
	vec3 SecondBlocker;
	uint64_t InterceptedHitWallVMDispatches = 0;
	uint64_t NormalHitWallVMReturns = 0;
	uint64_t DiagnosticOverflowDelta = 0;
	uint64_t HookEnterCalls = 0;
	uint64_t PlayerStartCandidates = 0;
	uint64_t FlatStartCandidates = 0;
	uint64_t FirstBlockerSpawns = 0;
	uint64_t PrimaryContactCandidates = 0;
	uint64_t PrimaryClearCandidates = 0;
	uint64_t PrimaryOtherContactCandidates = 0;
	float LastPrimaryFraction = 1.0f;
	float LastPrimaryNormalZ = 0.0f;
	uint64_t SlideLengthCandidates = 0;
	uint64_t VerifiedCornerCandidates = 0;
	std::vector<BotWalkingHitWallCornerFixtureHookContact> HookContacts;
	std::vector<std::string> HookFunctionNames;
	std::vector<PawnMovement::WalkingHitWallDispatchDiagnosticRecord> Diagnostics;
};

class BotWalkingHitWallCornerFixture
{
public:
	static BotWalkingHitWallCornerFixtureResult Run(
		Engine& engine, const BotWalkingHitWallCornerFixtureConfig& config);
};

void RegisterBotWalkingHitWallCornerFixtureDriver(HeadlessDriverRegistry& registry);
