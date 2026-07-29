#pragma once

#include <string>

class Engine;
class HeadlessDriverRegistry;

// Exercises the native UPawn::PickTarget path against two controlled, visible
// living bots. The fixture is map-backed and works through the verified UT436
// and Unreal Gold 226b controlled-match adapters.
struct BotPickTargetFixtureConfig
{
	std::string URL;
	int ExternalSkill = 3;
};

struct BotPickTargetFixtureResult
{
	bool Ran = false;
	bool Passed = false;
	bool VisibleLivingCandidate = false;
	bool SelectedLivingCandidate = false;
	std::string FailureReason;
	std::string CallerActor;
	std::string CandidateActor;
	std::string SelectedActor;
	std::string SelectedClass;
};

class BotPickTargetFixture
{
public:
	static BotPickTargetFixtureResult Run(
		Engine& engine, const BotPickTargetFixtureConfig& config);
};

void RegisterBotPickTargetFixtureDriver(HeadlessDriverRegistry& registry);
