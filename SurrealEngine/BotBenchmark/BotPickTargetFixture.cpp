#include "Precomp.h"
#include "BotPickTargetFixture.h"

#include "BotBenchmarkRoster.h"
#include "BotControlledMatch.h"
#include "Engine.h"
#include "Runtime/HeadlessDriver.h"
#include "UObject/UActor.h"
#include "UObject/UClass.h"
#include "UObject/ULevel.h"
#include "Utils/CommandLine.h"
#include "Utils/File.h"
#include "Utils/Logger.h"

#include <cmath>
#include <filesystem>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
	void Fail(BotPickTargetFixtureResult& result, std::string reason)
	{
		if (result.FailureReason.empty())
			result.FailureReason = std::move(reason);
	}

	std::string ResultText(const BotPickTargetFixtureResult& result)
	{
		std::ostringstream out;
		out << "schema=surreal-bot-pick-target-fixture-v1\n"
			<< "ran=" << (result.Ran ? "true" : "false") << "\n"
			<< "passed=" << (result.Passed ? "true" : "false") << "\n"
			<< "visible_living_candidate=" << (result.VisibleLivingCandidate ? "true" : "false") << "\n"
			<< "selected_living_candidate=" << (result.SelectedLivingCandidate ? "true" : "false") << "\n"
			<< "caller_actor=" << result.CallerActor << "\n"
			<< "candidate_actor=" << result.CandidateActor << "\n"
			<< "selected_actor=" << result.SelectedActor << "\n"
			<< "selected_class=" << result.SelectedClass << "\n"
			<< "failure_reason=" << result.FailureReason << "\n";
		return out.str();
	}

	class BotPickTargetFixtureDriver final : public HeadlessDriver
	{
	public:
		explicit BotPickTargetFixtureDriver(Engine& engine) : EngineRef(engine) {}

		HeadlessDriverConfig GetConfig() const override { return { .MaxTicks = 1 }; }

		void Start() override
		{
			BotPickTargetFixtureConfig config;
			config.URL = commandline ? commandline->GetArg("", "--botbench-url") : std::string();
			const std::string difficulty = commandline
				? commandline->GetArg("", "--botbench-difficulty") : std::string();
			if (!difficulty.empty())
			{
				try { config.ExternalSkill = std::stoi(difficulty); }
				catch (const std::exception&)
				{
					Result.FailureReason = "PickTarget fixture difficulty must be an integer";
					Complete = true;
					return;
				}
			}
			Result = BotPickTargetFixture::Run(EngineRef, config);
			const std::string output = commandline
				? commandline->GetArg("", "--botbench-output") : std::string();
			if (!output.empty())
			{
				std::filesystem::create_directories(output);
				File::write_all_text((std::filesystem::path(output)
					/ "pick-target-fixture-result.txt").string(), ResultText(Result));
			}
			LogMessage("PickTarget fixture result: "
				+ std::string(Result.Passed ? "passed" : "failed"));
			Complete = true;
		}

		bool IsComplete() const override { return Complete; }
		void Tick(const DeterministicFrameTime&) override {}
		int Finish(const HeadlessRunSummary&) override { return Result.Passed ? 0 : 1; }

	private:
		Engine& EngineRef;
		BotPickTargetFixtureResult Result;
		bool Complete = false;
	};
}

BotPickTargetFixtureResult BotPickTargetFixture::Run(
	Engine& engine, const BotPickTargetFixtureConfig& config)
{
	BotPickTargetFixtureResult result;
	UPawn* caller = nullptr;
	UPawn* candidate = nullptr;
	vec3 originalCallerLocation;
	vec3 originalCandidateLocation;
	try
	{
		if (config.URL.empty())
			throw std::runtime_error("PickTarget fixture URL is required");
		const BotBenchmarkRoster roster = BotBenchmarkRoster::Parse(
			std::string("2"), {}, {}, config.ExternalSkill);
		const BotControlledMatchResult match = BotControlledMatch::Setup(engine, config.URL, roster);
		if (match.Participants.size() != 2 || !match.Participants[0].Pawn || !match.Participants[1].Pawn)
			throw std::runtime_error("controlled PickTarget fixture did not create two live bots");
		caller = match.Participants[0].Pawn;
		candidate = match.Participants[1].Pawn;
		result.CallerActor = caller->Name.ToString();
		result.CandidateActor = candidate->Name.ToString();
		if (caller->Health() <= 0 || candidate->Health() <= 0)
			throw std::runtime_error("controlled PickTarget fixture requires living bots");
		originalCallerLocation = caller->Location();
		originalCandidateLocation = candidate->Location();

		std::vector<UActor*> starts;
		for (UActor* actor : engine.Level->Actors)
		{
			if (actor && !actor->bDeleteMe() && actor->IsA("PlayerStart"))
				starts.push_back(actor);
		}
		if (starts.size() < 2)
			throw std::runtime_error("PickTarget fixture requires two PlayerStart actors");

		bool visiblePair = false;
		for (UActor* callerStart : starts)
		{
			if (!caller->SetLocation(callerStart->Location()))
				continue;
			caller->UpdateActorZone();
			for (UActor* candidateStart : starts)
			{
				if (candidateStart == callerStart ||
					length(candidateStart->Location() - caller->Location()) <= 1.0f ||
					!candidate->SetLocation(candidateStart->Location()))
					continue;
				candidate->UpdateActorZone();
				const vec3 delta = candidate->Location() - caller->Location();
				if (length(delta) > 0.0f && length(delta) <= 2500.0f &&
					caller->LineOfSightTo(candidate, false))
				{
					visiblePair = true;
					break;
				}
			}
			if (visiblePair)
				break;
		}
		if (!visiblePair)
			throw std::runtime_error("PickTarget fixture could not establish a visible PlayerStart pair");
		result.VisibleLivingCandidate = true;

		const vec3 fireDir = normalize(candidate->Location() - caller->Location());
		float bestAim = 0.999f;
		float bestDist = std::numeric_limits<float>::max();
		UActor* selected = caller->PickTarget(bestAim, bestDist, fireDir, caller->Location());
		result.SelectedActor = selected ? selected->Name.ToString() : std::string();
		result.SelectedClass = selected && selected->Class
			? selected->Class->Name.ToString() : std::string();
		result.SelectedLivingCandidate = selected == candidate && candidate->Health() > 0;
		if (!result.SelectedLivingCandidate)
			throw std::runtime_error("PickTarget fixture did not select the visible living candidate");
		result.Ran = true;
		result.Passed = true;
	}
	catch (const std::exception& error)
	{
		Fail(result, error.what());
	}

	if (caller && !caller->bDeleteMe())
	{
		caller->SetLocation(originalCallerLocation);
		caller->UpdateActorZone();
	}
	if (candidate && !candidate->bDeleteMe())
	{
		candidate->SetLocation(originalCandidateLocation);
		candidate->UpdateActorZone();
	}
	return result;
}

void RegisterBotPickTargetFixtureDriver(HeadlessDriverRegistry& registry)
{
	if (!registry.Contains("bot-pick-target-fixture"))
	{
		registry.Register("bot-pick-target-fixture", [](Engine& engine)
		{
			return std::make_unique<BotPickTargetFixtureDriver>(engine);
		});
	}
}
