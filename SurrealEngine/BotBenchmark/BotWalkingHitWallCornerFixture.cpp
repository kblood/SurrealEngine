#include "Precomp.h"
#include "BotWalkingHitWallCornerFixture.h"

#include "BotControlledMatch.h"
#include "BotBenchmarkRoster.h"
#include "Collision/TopLevel/CollisionHit.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Runtime/HeadlessDriver.h"
#include "UObject/UActor.h"
#include "UObject/UClass.h"
#include "UObject/ULevel.h"
#include "Utils/CommandLine.h"
#include "Utils/File.h"
#include "Utils/Logger.h"
#include "VM/CallHooks.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <optional>
#include <sstream>
#include <utility>

namespace
{
	constexpr float FloorNormalMinimum = 0.95f;
	constexpr float MaximumFloorHeightDelta = 16.0f;
	constexpr float BlockerRadius = 48.0f;
	constexpr float BlockerHeight = 128.0f;
	constexpr float FirstBlockerForward = 128.0f;
	constexpr float FirstBlockerLateral = 56.0f;
	constexpr float FixtureAccelerationRate = 2048.0f;

	bool IsFinite(const vec3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	bool IsHorizontalWall(const CollisionHit& hit)
	{
		return hit.Fraction >= 0.0f && hit.Fraction < 1.0f && IsFinite(hit.Normal)
			&& std::abs(hit.Normal.z) < 0.2f;
	}

	std::optional<float> FloorHeight(UPawn* pawn, const vec3& location)
	{
		TraceFlags flags;
		flags.world = true;
		const vec3 from = location + vec3(0.0f, 0.0f, 64.0f);
		const vec3 to = location - vec3(0.0f, 0.0f, 512.0f);
		const CollisionHit hit = pawn->XLevel()->Collision.TraceFirstHit(
			from, to, pawn,
			vec3(pawn->CollisionRadius(), pawn->CollisionRadius(), pawn->CollisionHeight()),
			flags);
		if (hit.Fraction >= 1.0f || hit.Actor != pawn->Level()
			|| !IsFinite(hit.Normal) || hit.Normal.z < FloorNormalMinimum)
		{
			return {};
		}
		return from.z + (to.z - from.z) * hit.Fraction;
	}

	bool HasFlatSupport(UPawn* pawn, const vec3& start, const vec3& forward)
	{
		std::optional<float> firstHeight;
		for (const float distance : { 0.0f, 96.0f, 192.0f, 288.0f })
		{
			const std::optional<float> height = FloorHeight(pawn, start + forward * distance);
			if (!height)
				return false;
			if (!firstHeight)
				firstHeight = height;
			else if (std::abs(*height - *firstHeight) > MaximumFloorHeightDelta)
				return false;
		}
		return true;
	}

	UActor* SpawnBlocker(UPawn* pawn, UClass* blockerClass, const vec3& location)
	{
		UActor* blocker = pawn->Spawn(blockerClass, {}, {}, location, Rotator());
		if (!blocker || blocker->bDeleteMe())
			return nullptr;
		blocker->SetCollisionSize(BlockerRadius, BlockerHeight);
		blocker->SetCollision(true, true, true);
		blocker->bHidden() = true;
		return blocker;
	}

	void DestroyFixtureActor(UActor*& actor)
	{
		if (actor && !actor->bDeleteMe())
			actor->Destroy();
		actor = nullptr;
	}

	bool IsFixtureContact(const PawnMovement::WalkingHitWallDispatchDiagnosticRecord& contact,
		PawnMovement::WalkingHitWallContactPhase phase)
	{
		return contact.ContactPhase == phase
			&& contact.Blocker == PawnMovement::WalkingHitWallBlockerKind::DynamicActor
			&& contact.CallbackDispatched && contact.Decision.Valid
			&& !contact.PhysicsChangedByCallback && !contact.PawnDeletedByCallback;
	}

	void Fail(BotWalkingHitWallCornerFixtureResult& result, std::string reason)
	{
		if (result.FailureReason.empty())
			result.FailureReason = std::move(reason);
	}

	std::string FixtureResultText(const BotWalkingHitWallCornerFixtureResult& result)
	{
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << "schema=surreal-bot-walking-hitwall-corner-fixture-v1\n"
			<< "ran=" << (result.Ran ? "true" : "false") << "\n"
			<< "passed=" << (result.Passed ? "true" : "false") << "\n"
			<< "flat_path_verified=" << (result.FlatPathVerified ? "true" : "false") << "\n"
			<< "exact_blockers_verified=" << (result.ExactBlockersVerified ? "true" : "false") << "\n"
			<< "playerstart_candidates=" << result.PlayerStartCandidates << "\n"
			<< "flat_start_candidates=" << result.FlatStartCandidates << "\n"
			<< "first_blocker_spawns=" << result.FirstBlockerSpawns << "\n"
			<< "primary_contact_candidates=" << result.PrimaryContactCandidates << "\n"
			<< "primary_clear_candidates=" << result.PrimaryClearCandidates << "\n"
			<< "primary_other_contact_candidates=" << result.PrimaryOtherContactCandidates << "\n"
			<< "last_primary_fraction=" << result.LastPrimaryFraction << "\n"
			<< "last_primary_normal_z=" << result.LastPrimaryNormalZ << "\n"
			<< "slide_length_candidates=" << result.SlideLengthCandidates << "\n"
			<< "verified_corner_candidates=" << result.VerifiedCornerCandidates << "\n"
			<< "hook_enter_calls=" << result.HookEnterCalls << "\n"
			<< "suppressed_hitwall_calls=" << result.SuppressedHitWallCalls << "\n"
			<< "hook_contact_count=" << result.HookContacts.size() << "\n"
			<< "diagnostic_count=" << result.Diagnostics.size() << "\n"
			<< "failure_reason=" << result.FailureReason << "\n";
		for (size_t index = 0; index < result.Diagnostics.size(); index++)
		{
			const auto& diagnostic = result.Diagnostics[index];
			out << "diagnostic_" << index << "_phase="
				<< PawnMovement::WalkingHitWallContactPhaseName(diagnostic.ContactPhase) << "\n"
			<< "diagnostic_" << index << "_callback_dispatched="
			<< (diagnostic.CallbackDispatched ? "true" : "false") << "\n";
		}
		for (size_t index = 0; index < result.HookFunctionNames.size(); index++)
			out << "hook_function_" << index << "=" << result.HookFunctionNames[index] << "\n";
		return out.str();
	}

	class BotWalkingHitWallCornerFixtureDriver final : public HeadlessDriver
	{
	public:
		explicit BotWalkingHitWallCornerFixtureDriver(Engine& engine)
			: EngineRef(engine)
		{
		}

		HeadlessDriverConfig GetConfig() const override
		{
			HeadlessDriverConfig config;
			config.MaxTicks = 1;
			return config;
		}

		void Start() override
		{
			BotWalkingHitWallCornerFixtureConfig config;
			config.URL = commandline ? commandline->GetArg("", "--botbench-url") : std::string();
			const std::string difficulty = commandline
				? commandline->GetArg("", "--botbench-difficulty") : std::string();
			if (!difficulty.empty())
			{
				try
				{
					config.ExternalSkill = std::stoi(difficulty);
				}
				catch (const std::exception&)
				{
					Result.FailureReason = "corner fixture difficulty must be an integer";
					Complete = true;
					return;
				}
			}
			Result = BotWalkingHitWallCornerFixture::Run(EngineRef, config);
			const std::string output = commandline
				? commandline->GetArg("", "--botbench-output") : std::string();
			const std::string resultText = FixtureResultText(Result);
			if (!output.empty())
			{
				const std::filesystem::path path(output);
				std::filesystem::create_directories(path);
				File::write_all_text((path / "corner-fixture-result.txt").string(), resultText);
			}
			LogMessage("Walking HitWall corner fixture result: "
				+ std::string(Result.Passed ? "passed" : "failed")
				+ "; diagnostics=" + std::to_string(Result.Diagnostics.size())
				+ "; callbacks=" + std::to_string(Result.SuppressedHitWallCalls));
			Complete = true;
		}

		bool IsComplete() const override { return Complete; }
		void Tick(const DeterministicFrameTime&) override {}
		int Finish(const HeadlessRunSummary&) override { return Result.Passed ? 0 : 1; }

	private:
		Engine& EngineRef;
		BotWalkingHitWallCornerFixtureResult Result;
		bool Complete = false;
	};
}

BotWalkingHitWallCornerFixtureResult BotWalkingHitWallCornerFixture::Run(
	Engine& engine, const BotWalkingHitWallCornerFixtureConfig& config)
{
	BotWalkingHitWallCornerFixtureResult result;
	if (config.URL.empty())
	{
		Fail(result, "corner fixture URL is required");
		return result;
	}
	if (!std::isfinite(config.ElapsedSeconds) || config.ElapsedSeconds <= 0.0f)
	{
		Fail(result, "corner fixture elapsed seconds must be finite and positive");
		return result;
	}

	UActor* firstBlocker = nullptr;
	UActor* secondBlocker = nullptr;
	VMCallHookHandle hookHandle = 0;
	UPawn* fixturePawn = nullptr;
	bool restorePawnBlocking = false;
	bool originalPawnBlockActors = false;
	bool originalPawnBlockPlayers = false;
	float originalPawnAccelRate = 0.0f;
	float originalPawnDesiredSpeed = 0.0f;
	bool originalHitWallEnabled = false;
	bool originalBumpEnabled = false;
	vec3 fixtureForward(0.0f);
	try
	{
		const BotBenchmarkRoster roster = BotBenchmarkRoster::Parse(
			std::string("1"), {}, {}, config.ExternalSkill);
		const BotControlledMatchResult match = BotControlledMatch::Setup(engine, config.URL, roster);
		LogMessage("Walking HitWall corner fixture: controlled match setup complete");
		if (match.Participants.size() != 1 || !match.Participants.front().Pawn
			|| match.Participants.front().Pawn->bDeleteMe())
		{
			Fail(result, "controlled corner fixture did not create exactly one live bot");
			return result;
		}
		UPawn* pawn = match.Participants.front().Pawn;
		fixturePawn = pawn;
		originalPawnAccelRate = pawn->AccelRate();
		originalPawnDesiredSpeed = pawn->DesiredSpeed();
		originalHitWallEnabled = pawn->IsEventEnabled(EventName::HitWall);
		originalBumpEnabled = pawn->IsEventEnabled(EventName::Bump);
		pawn->AccelRate() = FixtureAccelerationRate;
		pawn->DesiredSpeed() = 1.0f;
		originalPawnBlockActors = pawn->bBlockActors();
		originalPawnBlockPlayers = pawn->bBlockPlayers();
		if (!originalPawnBlockActors || !originalPawnBlockPlayers)
		{
			// Stock bots need not block other actors while navigating. The fixture
			// deliberately uses dynamic BlockAll contacts, so enable the symmetric
			// actor-blocking rule only for its native walking invocation.
			pawn->SetCollision(pawn->bCollideActors(), true, true);
			restorePawnBlocking = true;
		}
		if (!engine.packages || !engine.Level)
		{
			Fail(result, "controlled corner fixture has no loaded level or package manager");
			return result;
		}
		UClass* blockerClass = engine.packages->FindClass("Engine.BlockAll");
		if (!blockerClass)
		{
			Fail(result, "controlled corner fixture could not resolve Engine.BlockAll");
			return result;
		}

		const std::array<vec3, 8> headings = {
			vec3(1.0f, 0.0f, 0.0f), vec3(-1.0f, 0.0f, 0.0f),
			vec3(0.0f, 1.0f, 0.0f), vec3(0.0f, -1.0f, 0.0f),
			normalize(vec3(1.0f, 1.0f, 0.0f)), normalize(vec3(1.0f, -1.0f, 0.0f)),
			normalize(vec3(-1.0f, 1.0f, 0.0f)), normalize(vec3(-1.0f, -1.0f, 0.0f)) };

		// Spawning fixture blockers mutates Level->Actors. Snapshot the eligible starts
		// first so that no iterator into the live actor array survives a Spawn/Destroy.
		std::vector<vec3> playerStartLocations;
		for (UActor* startActor : engine.Level->Actors)
		{
			if (!startActor || startActor->bDeleteMe() || !startActor->IsA("PlayerStart"))
				continue;
			playerStartLocations.push_back(startActor->Location());
			result.PlayerStartCandidates++;
		}

		bool fixtureGeometryFound = false;
		for (const vec3& playerStartLocation : playerStartLocations)
		{
			for (const vec3& forward : headings)
			{
				if (!pawn->SetLocation(playerStartLocation))
					continue;
				pawn->UpdateActorZone();
				if (!pawn->Region().Zone || !HasFlatSupport(pawn, pawn->Location(), forward))
					continue;
				result.FlatStartCandidates++;

				const vec3 right(-forward.y, forward.x, 0.0f);
				for (const float side : { -FirstBlockerLateral, FirstBlockerLateral })
				{
					DestroyFixtureActor(firstBlocker);
					DestroyFixtureActor(secondBlocker);
					const vec3 start = pawn->Location();
					firstBlocker = SpawnBlocker(pawn, blockerClass,
						start + forward * FirstBlockerForward + right * side);
					if (!firstBlocker)
						continue;
					result.FirstBlockerSpawns++;

					const float speed = pawn->GroundSpeed() * pawn->DesiredSpeed();
					if (!std::isfinite(speed) || speed <= 0.0f)
						continue;
					// The fixture drives along the chosen heading at the pawn's capped
					// walking speed. The matching TickWalking call below supplies
					// same-direction acceleration, so its friction branch preserves this
					// speed before the capped acceleration update.
					const float retainedSpeed = speed;
					const vec3 firstDelta = forward * retainedSpeed * config.ElapsedSeconds;
					const CollisionHit primary = pawn->ProbeMoveCollision(start, firstDelta);
					result.LastPrimaryFraction = primary.Fraction;
					result.LastPrimaryNormalZ = primary.Normal.z;
					if (primary.Fraction >= 1.0f)
						result.PrimaryClearCandidates++;
					else if (primary.Actor != firstBlocker)
						result.PrimaryOtherContactCandidates++;
					if (!IsHorizontalWall(primary) || primary.Actor != firstBlocker)
						continue;
					result.PrimaryContactCandidates++;

					const float remainingTime = config.ElapsedSeconds * (1.0f - primary.Fraction);
					const vec3 residualDelta = forward * retainedSpeed * remainingTime;
					const vec3 alignedDelta = (residualDelta
						- primary.Normal * dot(residualDelta, primary.Normal))
						* (1.0f - primary.Fraction);
					const float alignedLength = length(alignedDelta);
					if (!IsFinite(alignedDelta) || alignedLength
						<= pawn->CollisionRadius() + BlockerRadius + 8.0f)
					{
						continue;
					}
					result.SlideLengthCandidates++;

					const vec3 primaryLocation = start + firstDelta * primary.Fraction;
					secondBlocker = SpawnBlocker(pawn, blockerClass,
						primaryLocation + normalize(alignedDelta)
							* (pawn->CollisionRadius() + BlockerRadius + 8.0f));
					if (!secondBlocker)
						continue;
					const CollisionHit verifiedPrimary = pawn->ProbeMoveCollision(start, firstDelta);
					const CollisionHit verifiedSlide = pawn->ProbeMoveCollision(primaryLocation, alignedDelta);
					if (!IsHorizontalWall(verifiedPrimary) || verifiedPrimary.Actor != firstBlocker
						|| !IsHorizontalWall(verifiedSlide) || verifiedSlide.Actor != secondBlocker)
					{
						continue;
					}
					result.VerifiedCornerCandidates++;

					result.Start = start;
					result.FirstBlocker = firstBlocker->Location();
					result.SecondBlocker = secondBlocker->Location();
					result.FlatPathVerified = true;
					result.ExactBlockersVerified = true;
					fixtureForward = forward;
					fixtureGeometryFound = true;
					break;
				}
				if (fixtureGeometryFound)
					break;
			}
			if (fixtureGeometryFound)
				break;
		}
		if (!fixtureGeometryFound)
		{
			Fail(result, "could not construct a flat two-BlockAll forced-corner path");
			DestroyFixtureActor(firstBlocker);
			DestroyFixtureActor(secondBlocker);
			return result;
		}
		LogMessage("Walking HitWall corner fixture: forced-corner geometry complete");

		pawn->DrainWalkingHitWallDispatchDiagnostics();
		pawn->EnableEvent(ToNameString(EventName::HitWall));
		pawn->SetPhysics(PHYS_Walking);
		pawn->Acceleration() = fixtureForward * pawn->AccelRate();
		pawn->Velocity() = fixtureForward * pawn->GroundSpeed() * pawn->DesiredSpeed();

		VMCallHook hook;
		hook.Enter = [&result, pawn](UFunction* function, UObject* instance,
			VMCallArguments& arguments) -> VMCallHookCleanup
		{
			result.HookEnterCalls++;
			if (function)
				result.HookFunctionNames.push_back(function->Name.ToString());
			if (!function || instance != pawn)
				return {};
			if (function->Name == "Bump")
			{
				arguments.OverrideResult(ExpressionValue::NothingValue());
				return {};
			}
			if (function->Name != "HitWall")
				return {};
			BotWalkingHitWallCornerFixtureHookContact contact;
			if (arguments.Size() >= 2)
			{
				contact.Normal = arguments.Values()[0].ToVector();
				UObject* wall = arguments.Values()[1].ToObject();
				contact.WallActor = wall ? wall->Name.ToString() : std::string();
			}
			else
			{
				Fail(result, "forced corner HitWall hook received fewer than two arguments");
			}
			result.HookContacts.push_back(std::move(contact));
			arguments.OverrideResult(ExpressionValue::NothingValue());
			result.SuppressedHitWallCalls++;
			return {};
		};
		hookHandle = Frame::CallHooks().Register(std::move(hook));
		LogMessage("Walking HitWall corner fixture: dispatching native walking tick");
		pawn->TickWalking(config.ElapsedSeconds);
		LogMessage("Walking HitWall corner fixture: native walking tick complete");
		Frame::CallHooks().Unregister(hookHandle);
		hookHandle = 0;

		result.Ran = true;
		result.Diagnostics = pawn->DrainWalkingHitWallDispatchDiagnostics();
		if (result.HookContacts.size() != 2 || result.SuppressedHitWallCalls != 2)
			Fail(result, "forced corner did not dispatch exactly two suppressed HitWall callbacks");
		else if (result.Diagnostics.size() != 2)
			Fail(result, "forced corner did not record exactly two per-contact walking diagnostics");
		else if (!IsFixtureContact(result.Diagnostics[0],
			PawnMovement::WalkingHitWallContactPhase::PrimaryForward)
			|| !IsFixtureContact(result.Diagnostics[1],
				PawnMovement::WalkingHitWallContactPhase::AlignedSlide))
		{
			Fail(result, "forced corner diagnostic phases or callback outcomes differ from the native contract");
		}
		else if (result.HookContacts[0].WallActor != firstBlocker->Name.ToString()
			|| result.HookContacts[1].WallActor != secondBlocker->Name.ToString())
		{
			Fail(result, "forced corner callback contacts did not preserve first/second BlockAll identity");
		}
		else if (pawn->Physics() != PHYS_Walking)
		{
			Fail(result, "suppressed forced-corner callback unexpectedly changed pawn physics");
		}
		else
		{
			result.Passed = true;
		}
	}
	catch (const std::exception& error)
	{
		Fail(result, error.what());
	}

	if (hookHandle != 0)
		Frame::CallHooks().Unregister(hookHandle);
	if (restorePawnBlocking && fixturePawn && !fixturePawn->bDeleteMe())
	{
		fixturePawn->SetCollision(fixturePawn->bCollideActors(),
			originalPawnBlockActors, originalPawnBlockPlayers);
	}
	if (fixturePawn && !fixturePawn->bDeleteMe())
	{
		fixturePawn->AccelRate() = originalPawnAccelRate;
		fixturePawn->DesiredSpeed() = originalPawnDesiredSpeed;
		if (originalHitWallEnabled)
			fixturePawn->EnableEvent(ToNameString(EventName::HitWall));
		else
			fixturePawn->DisableEvent(ToNameString(EventName::HitWall));
		if (originalBumpEnabled)
			fixturePawn->EnableEvent(ToNameString(EventName::Bump));
		else
			fixturePawn->DisableEvent(ToNameString(EventName::Bump));
	}
	DestroyFixtureActor(firstBlocker);
	DestroyFixtureActor(secondBlocker);
	return result;
}

void RegisterBotWalkingHitWallCornerFixtureDriver(HeadlessDriverRegistry& registry)
{
	if (!registry.Contains("bot-walking-hitwall-corner-fixture"))
	{
		registry.Register("bot-walking-hitwall-corner-fixture", [](Engine& engine)
		{
			return std::make_unique<BotWalkingHitWallCornerFixtureDriver>(engine);
		});
	}
}
