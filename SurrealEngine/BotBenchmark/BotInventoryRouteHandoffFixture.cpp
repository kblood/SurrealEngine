#include "Precomp.h"
#include "BotInventoryRouteHandoffFixture.h"

#include "BotBenchmarkRoster.h"
#include "BotControlledMatch.h"
#include "Collision/TopLevel/CollisionHit.h"
#include "Engine.h"
#include "Runtime/HeadlessDriver.h"
#include "UObject/UActor.h"
#include "UObject/ULevel.h"
#include "UObject/UMesh.h"
#include "Utils/CommandLine.h"
#include "Utils/File.h"
#include "Utils/Logger.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <sstream>

namespace
{
	const vec3 DeathFanAmmo3LaunchAnchor(-148.983383f, -602.711670f, 1384.0f);
	const vec3 DeathFanPathNode73LaunchAnchor(-146.546677f, 743.832153f, 1384.0f);
	constexpr float ImmediateSupportDistance = 64.0f;
	constexpr float DeepSupportDistance = 2048.0f;
	constexpr float FixtureTickSeconds = 0.05f;
	constexpr int LiveNavigationTickLimit = 120;
	constexpr int CorridorSamples = 5;
	constexpr int ZoneSamplesPerCorridorPoint = 64;

	struct CorridorHazardEvidence
	{
		bool UnsupportedSample = false;
		bool HarmfulZoneBelow = false;
	};

	void Fail(BotInventoryRouteHandoffFixtureResult& result, std::string reason)
	{
		if (result.FailureReason.empty())
			result.FailureReason = std::move(reason);
	}

	bool IsSafeWalkingPawn(UPawn* pawn)
	{
		UZoneInfo* zone = pawn ? pawn->FootRegion().Zone : nullptr;
		return pawn && !pawn->bDeleteMe() && pawn->Health() > 0
			&& pawn->Role() == ROLE_Authority && pawn->Physics() == PHYS_Walking
			&& pawn->StateFrame && zone && !zone->bPainZone() && !zone->bWaterZone()
			&& length(zone->ZoneVelocity()) <= 0.001f;
	}

	UActor* FindActor(Engine& engine, const char* name)
	{
		for (UActor* actor : engine.Level->Actors)
		{
			if (actor && !actor->bDeleteMe() && actor->Name.ToString() == name)
				return actor;
		}
		return nullptr;
	}

	bool HasTraversableEdge(UPawn* pawn, UNavigationPoint* start, UNavigationPoint* end)
	{
		if (!pawn || !start || !end)
			return false;
		for (const LevelReachSpec& spec : pawn->XLevel()->ReachSpecs)
		{
			if (spec.startActor == start && spec.endActor == end && !spec.bPruned
				&& spec.collisionRadius >= pawn->CollisionRadius()
				&& spec.collisionHeight >= pawn->CollisionHeight())
			{
				return true;
			}
		}
		return false;
	}

	bool IsWalkableStaticSupport(UPawn* pawn, const CollisionHit& hit)
	{
		return pawn && std::isfinite(hit.Fraction) && hit.Fraction >= 0.0f
			&& hit.Fraction < 1.0f && hit.Actor == pawn->Level()
			&& std::isfinite(hit.Normal.z) && hit.Normal.z >= 0.7071f;
	}

	CorridorHazardEvidence CollectCorridorHazardEvidence(
		UPawn* pawn, const vec3& start, const vec3& end)
	{
		CorridorHazardEvidence evidence;
		if (!pawn || !pawn->XLevel() || !pawn->XLevel()->Model)
			return evidence;
		const vec3 corridor = end - start;
		for (int index = 1; index <= CorridorSamples; index++)
		{
			const vec3 point = start + corridor
				* (static_cast<float>(index) / static_cast<float>(CorridorSamples + 1));
			const CollisionHit immediateSupport = pawn->ProbeMoveCollision(
				point, vec3(0.0f, 0.0f, -ImmediateSupportDistance), true);
			if (IsWalkableStaticSupport(pawn, immediateSupport))
				continue;
			evidence.UnsupportedSample = true;
			for (int zoneIndex = 1; zoneIndex <= ZoneSamplesPerCorridorPoint; zoneIndex++)
			{
				const vec3 below = point + vec3(0.0f, 0.0f,
					-DeepSupportDistance * static_cast<float>(zoneIndex) / ZoneSamplesPerCorridorPoint);
				UZoneInfo* zone = pawn->XLevel()->Model->FindRegion(below, pawn->Level()).Zone;
				if (zone && zone->bPainZone() && zone->DamagePerSec() > 0)
				{
					evidence.HarmfulZoneBelow = true;
					break;
				}
			}
		}
		return evidence;
	}

	std::string ResultText(const BotInventoryRouteHandoffFixtureResult& result)
	{
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << "schema=surreal-bot-inventory-route-handoff-fixture-v7\n"
			<< "ran=" << (result.Ran ? "true" : "false") << "\n"
			<< "passed=" << (result.Passed ? "true" : "false") << "\n"
			<< "safe_walking_anchor=" << (result.SafeWalkingAnchor ? "true" : "false") << "\n"
			<< "direct_marker_rejected=" << (result.DirectMarkerRejected ? "true" : "false") << "\n"
			<< "graph_first_hop_selected=" << (result.GraphFirstHopSelected ? "true" : "false") << "\n"
			<< "safe_marker_reachable=" << (result.SafeMarkerReachable ? "true" : "false") << "\n"
			<< "graph_fallback_exists=" << (result.GraphFallbackExists ? "true" : "false") << "\n"
			<< "unsupported_corridor_sample=" << (result.UnsupportedCorridorSample ? "true" : "false") << "\n"
			<< "harmful_zone_below_corridor=" << (result.HarmfulZoneBelowCorridor ? "true" : "false") << "\n"
			<< "navigation_anchor_safe=" << (result.NavigationAnchorSafe ? "true" : "false") << "\n"
			<< "direct_navigation_reachable=" << (result.DirectNavigationReachable ? "true" : "false") << "\n"
			<< "navigation_graph_first_hop_selected=" << (result.NavigationGraphFirstHopSelected ? "true" : "false") << "\n"
			<< "navigation_fallback_route_exists=" << (result.NavigationFallbackRouteExists ? "true" : "false") << "\n"
			<< "navigation_fallback_endpoint_direct_reachable=" << (result.NavigationFallbackEndpointDirectReachable ? "true" : "false") << "\n"
			<< "navigation_fallback_endpoint_unsupported_corridor_sample=" << (result.NavigationFallbackEndpointUnsupportedCorridorSample ? "true" : "false") << "\n"
			<< "navigation_fallback_endpoint_harmful_zone_below_corridor=" << (result.NavigationFallbackEndpointHarmfulZoneBelowCorridor ? "true" : "false") << "\n"
			<< "navigation_unsupported_corridor_sample=" << (result.NavigationUnsupportedCorridorSample ? "true" : "false") << "\n"
			<< "navigation_harmful_zone_below_corridor=" << (result.NavigationHarmfulZoneBelowCorridor ? "true" : "false") << "\n"
			<< "stationary_navigation_anchor_falling_observed=" << (result.StationaryNavigationAnchorFallingObserved ? "true" : "false") << "\n"
			<< "live_navigation_move_toward_armed=" << (result.LiveNavigationMoveTowardArmed ? "true" : "false") << "\n"
			<< "live_navigation_falling_observed=" << (result.LiveNavigationFallingObserved ? "true" : "false") << "\n"
			<< "live_navigation_harmful_entry_observed=" << (result.LiveNavigationHarmfulEntryObserved ? "true" : "false") << "\n"
			<< "pawn_actor=" << result.PawnActor << "\n"
			<< "marker_actor=" << result.MarkerActor << "\n"
			<< "inventory_actor=" << result.InventoryActor << "\n"
			<< "safe_marker_actor=" << result.SafeMarkerActor << "\n"
			<< "selected_first_hop_actor=" << result.SelectedFirstHopActor << "\n"
			<< "navigation_actor=" << result.NavigationActor << "\n"
			<< "navigation_first_hop_actor=" << result.NavigationFirstHopActor << "\n"
			<< "navigation_fallback_first_hop_actor=" << result.NavigationFallbackFirstHopActor << "\n"
			<< "navigation_fallback_endpoint_actor=" << result.NavigationFallbackEndpointActor << "\n"
			<< "graph_edge_count=" << result.GraphEdgeCount << "\n"
			<< "immediate_support_samples=" << result.ImmediateSupportSamples << "\n"
			<< "unsupported_samples=" << result.UnsupportedSamples << "\n"
			<< "harmful_below_samples=" << result.HarmfulBelowSamples << "\n"
			<< "direct_marker_rejects=" << result.DirectMarkerRejects << "\n"
			<< "navigation_candidate_count=" << result.NavigationCandidateCount << "\n"
			<< "navigation_candidate_direct_reachable_count=" << result.NavigationCandidateDirectReachableCount << "\n"
			<< "navigation_candidate_safe_direct_reachable_count=" << result.NavigationCandidateSafeDirectReachableCount << "\n"
			<< "navigation_candidate_unsafe_direct_reachable_count=" << result.NavigationCandidateUnsafeDirectReachableCount << "\n"
			<< "live_navigation_ticks=" << result.LiveNavigationTicks << "\n"
			<< "first_safe_navigation_candidate_actor=" << result.FirstSafeNavigationCandidateActor << "\n"
			<< "first_unsafe_navigation_candidate_actor=" << result.FirstUnsafeNavigationCandidateActor << "\n"
			<< "first_harmful_below_distance=" << result.FirstHarmfulBelowDistance << "\n"
			<< "failure_reason=" << result.FailureReason << "\n";
		return out.str();
	}

	class BotInventoryRouteHandoffFixtureDriver final : public HeadlessDriver
	{
	public:
		explicit BotInventoryRouteHandoffFixtureDriver(Engine& engine) : EngineRef(engine) {}

		HeadlessDriverConfig GetConfig() const override { return { .MaxTicks = 1 }; }

		void Start() override
		{
			BotInventoryRouteHandoffFixtureConfig config;
			config.URL = commandline ? commandline->GetArg("", "--botbench-url") : std::string();
			const std::string difficulty = commandline
				? commandline->GetArg("", "--botbench-difficulty") : std::string();
			if (!difficulty.empty())
			{
				try { config.ExternalSkill = std::stoi(difficulty); }
				catch (const std::exception&)
				{
					Result.FailureReason = "inventory route fixture difficulty must be an integer";
					Complete = true;
					return;
				}
			}
			Result = BotInventoryRouteHandoffFixture::Run(EngineRef, config);
			const std::string output = commandline
				? commandline->GetArg("", "--botbench-output") : std::string();
			if (!output.empty())
			{
				std::filesystem::create_directories(output);
				File::write_all_text((std::filesystem::path(output)
					/ "inventory-route-handoff-fixture-result.txt").string(), ResultText(Result));
			}
			LogMessage("Inventory route handoff fixture result: "
				+ std::string(Result.Passed ? "passed" : "failed"));
			Complete = true;
		}

		bool IsComplete() const override { return Complete; }
		void Tick(const DeterministicFrameTime&) override {}
		int Finish(const HeadlessRunSummary&) override { return Result.Passed ? 0 : 1; }

	private:
		Engine& EngineRef;
		BotInventoryRouteHandoffFixtureResult Result;
		bool Complete = false;
	};
}

BotInventoryRouteHandoffFixtureResult BotInventoryRouteHandoffFixture::Run(
	Engine& engine, const BotInventoryRouteHandoffFixtureConfig& config)
{
	BotInventoryRouteHandoffFixtureResult result;
	UPawn* pawn = nullptr;
	vec3 originalLocation;
	vec3 originalVelocity;
	vec3 originalAcceleration;
	uint8_t originalPhysics = PHYS_None;
	bool originalInventoryMarkerSafetyEnabled = false;
	bool originalTickEnabled = false;
	bool originalUpdateTacticsEnabled = false;
	LatentRunState originalLatentState = LatentRunState::Continue;
	UActor* originalMoveTarget = nullptr;
	float originalMoveTimer = 0.0f;
	try
	{
		if (config.URL.empty() || config.URL.find("DmDeathFan") == std::string::npos)
			throw std::runtime_error("inventory route fixture requires a DmDeathFan URL");
		const BotBenchmarkRoster roster = BotBenchmarkRoster::Parse(
			std::string("1"), {}, {}, config.ExternalSkill);
		const BotControlledMatchResult match = BotControlledMatch::Setup(engine, config.URL, roster);
		if (match.Participants.size() != 1 || !match.Participants.front().Pawn)
			throw std::runtime_error("controlled inventory fixture did not create one live bot");
		pawn = match.Participants.front().Pawn;
		originalInventoryMarkerSafetyEnabled = engine.IsBotBenchmarkInventoryMarkerDirectReachSafetyEnabled();
		engine.SetBotBenchmarkInventoryMarkerDirectReachSafetyEnabled(true);
		result.PawnActor = pawn->Name.ToString();
		originalLocation = pawn->Location();
		originalVelocity = pawn->Velocity();
		originalAcceleration = pawn->Acceleration();
		originalPhysics = pawn->Physics();
		originalTickEnabled = pawn->IsEventEnabled(EventName::Tick);
		originalUpdateTacticsEnabled = pawn->IsEventEnabled(EventName::UpdateTactics);
		originalLatentState = pawn->StateFrame->LatentState;
		originalMoveTarget = pawn->MoveTarget();
		originalMoveTimer = pawn->MoveTimer();

		UActor* inventory = FindActor(engine, "ASMDAmmo3");
		UNavigationPoint* marker = UObject::TryCast<UNavigationPoint>(FindActor(engine, "InventorySpot43"));
		UNavigationPoint* safeMarker = UObject::TryCast<UNavigationPoint>(FindActor(engine, "InventorySpot41"));
		UNavigationPoint* pathNode27 = UObject::TryCast<UNavigationPoint>(FindActor(engine, "PathNode27"));
		UNavigationPoint* pathNode55 = UObject::TryCast<UNavigationPoint>(FindActor(engine, "PathNode55"));
		UNavigationPoint* pathNode54 = UObject::TryCast<UNavigationPoint>(FindActor(engine, "PathNode54"));
		UNavigationPoint* pathNode73 = UObject::TryCast<UNavigationPoint>(FindActor(engine, "PathNode73"));
		if (!inventory || !marker || !safeMarker || !pathNode27 || !pathNode55 || !pathNode54 || !pathNode73)
			throw std::runtime_error("fixture map is missing the expected DeathFan inventory route actors");
		result.InventoryActor = inventory->Name.ToString();
		result.MarkerActor = marker->Name.ToString();
		result.SafeMarkerActor = safeMarker->Name.ToString();
		result.NavigationActor = pathNode73->Name.ToString();

		if (!pawn->SetLocation(DeathFanAmmo3LaunchAnchor))
			throw std::runtime_error("fixture could not set the recorded DeathFan launch anchor");
		pawn->Velocity() = vec3(0.0f);
		pawn->Acceleration() = vec3(0.0f);
		pawn->UpdateActorZone();
		pawn->SetPhysics(PHYS_Walking);
		if (!IsSafeWalkingPawn(pawn))
			throw std::runtime_error("recorded DeathFan launch anchor is not a safe walking context");
		result.SafeWalkingAnchor = true;

		result.DirectMarkerRejected = !pawn->ActorReachable(marker, true);
		result.DirectMarkerRejects = pawn->InventoryMarkerDirectReachRejectCount();
		if (!result.DirectMarkerRejected)
			throw std::runtime_error("fixture did not reject the unsupported harmful inventory marker");
		if (result.DirectMarkerRejects != 1)
			throw std::runtime_error("fixture did not record exactly one inventory marker direct-reach rejection");
		UObject* selectedFirstHop = pawn->FindPathToward(marker, false);
		result.SelectedFirstHopActor = selectedFirstHop
			? selectedFirstHop->Name.ToString() : std::string();
		result.GraphFirstHopSelected = selectedFirstHop && selectedFirstHop != marker
			&& UObject::TryCast<UNavigationPoint>(selectedFirstHop) != nullptr;
		if (!result.GraphFirstHopSelected)
			throw std::runtime_error("fixture did not select a graph first hop after rejection");

		const std::array<std::pair<UNavigationPoint*, UNavigationPoint*>, 3> route = {{
			{ pathNode27, pathNode55 },
			{ pathNode55, pathNode54 },
			{ pathNode54, marker }
		}};
		for (const auto& edge : route)
		{
			if (HasTraversableEdge(pawn, edge.first, edge.second))
				result.GraphEdgeCount++;
		}
		result.GraphFallbackExists = result.GraphEdgeCount == route.size();
		if (!result.GraphFallbackExists)
			throw std::runtime_error("fixture map no longer has the recorded three-edge fallback route");

		const vec3 corridor = marker->Location() - DeathFanAmmo3LaunchAnchor;
		TraceFlags flags;
		flags.world = true;
		for (int index = 1; index <= CorridorSamples; index++)
		{
			const vec3 point = DeathFanAmmo3LaunchAnchor
				+ corridor * (static_cast<float>(index) / static_cast<float>(CorridorSamples + 1));
			const CollisionHit immediateSupport = pawn->ProbeMoveCollision(
				point, vec3(0.0f, 0.0f, -ImmediateSupportDistance), true);
			if (IsWalkableStaticSupport(pawn, immediateSupport))
			{
				result.ImmediateSupportSamples++;
				continue;
			}
			result.UnsupportedSamples++;
			const CollisionHit deepSupport = pawn->XLevel()->Collision.TraceFirstHit(
				point, point + vec3(0.0f, 0.0f, -DeepSupportDistance), pawn,
				vec3(pawn->CollisionRadius(), pawn->CollisionRadius(), pawn->CollisionHeight()), flags);
			if (IsWalkableStaticSupport(pawn, deepSupport))
				result.UnsupportedCorridorSample = true;
			for (int zoneIndex = 1; zoneIndex <= ZoneSamplesPerCorridorPoint; zoneIndex++)
			{
				const vec3 below = point + vec3(0.0f, 0.0f,
					-DeepSupportDistance * static_cast<float>(zoneIndex) / ZoneSamplesPerCorridorPoint);
				UZoneInfo* zone = pawn->XLevel()->Model->FindRegion(below, pawn->Level()).Zone;
				if (zone && zone->bPainZone() && zone->DamagePerSec() > 0)
				{
					result.HarmfulBelowSamples++;
					result.HarmfulZoneBelowCorridor = true;
					const float distance = DeepSupportDistance
						* static_cast<float>(zoneIndex) / ZoneSamplesPerCorridorPoint;
					if (result.FirstHarmfulBelowDistance == 0.0f
						|| distance < result.FirstHarmfulBelowDistance)
					{
						result.FirstHarmfulBelowDistance = distance;
					}
					break;
				}
			}
		}
		if (!result.UnsupportedCorridorSample || !result.HarmfulZoneBelowCorridor)
			throw std::runtime_error("recorded direct corridor lacks the expected unsupported harmful drop evidence");
		result.SafeMarkerReachable = pawn->ActorReachable(safeMarker, true);
		if (!result.SafeMarkerReachable)
			throw std::runtime_error("fixture could not preserve a safe direct inventory pickup");

		if (!pawn->SetLocation(DeathFanPathNode73LaunchAnchor))
			throw std::runtime_error("fixture could not set the recorded PathNode73 launch anchor");
		pawn->Velocity() = vec3(0.0f);
		pawn->Acceleration() = vec3(0.0f);
		pawn->UpdateActorZone();
		pawn->SetPhysics(PHYS_Walking);
		if (!IsSafeWalkingPawn(pawn))
			throw std::runtime_error("recorded PathNode73 launch anchor is not a safe walking context");
		result.NavigationAnchorSafe = true;
		result.DirectNavigationReachable = pawn->ActorReachable(pathNode73, true);
		if (!result.DirectNavigationReachable)
			throw std::runtime_error("fixture does not reproduce direct PathNode73 reachability");
		UObject* navigationFirstHop = pawn->FindPathToward(pathNode73, false);
		result.NavigationFirstHopActor = navigationFirstHop
			? navigationFirstHop->Name.ToString() : std::string();
		result.NavigationGraphFirstHopSelected = navigationFirstHop && navigationFirstHop != pathNode73
			&& UObject::TryCast<UNavigationPoint>(navigationFirstHop) != nullptr;
		if (!pawn->MarkReachableNavEndPoints())
			throw std::runtime_error("fixture could not establish normal reachable endpoints for PathNode73");
		const bool originalNavigationEndpoint = pathNode73->bEndPoint();
		pathNode73->bEndPoint() = false;
		const PawnPathEndPointResult navigationFallback = pawn->FindPathToEndPoint(pathNode73, 1000);
		pathNode73->bEndPoint() = originalNavigationEndpoint;
		if (!navigationFallback.Points.empty() && navigationFallback.Points.front()
			&& navigationFallback.Points.front() != pathNode73)
		{
			result.NavigationFallbackRouteExists = true;
			result.NavigationFallbackFirstHopActor =
				navigationFallback.Points.front()->Name.ToString();
			result.NavigationFallbackEndpointActor = result.NavigationFallbackFirstHopActor;
			result.NavigationFallbackEndpointDirectReachable = pawn->ActorReachable(
				navigationFallback.Points.front(), true);
			const CorridorHazardEvidence fallbackEndpointEvidence = CollectCorridorHazardEvidence(
				pawn, DeathFanPathNode73LaunchAnchor,
				navigationFallback.Points.front()->Location());
			result.NavigationFallbackEndpointUnsupportedCorridorSample =
				fallbackEndpointEvidence.UnsupportedSample;
			result.NavigationFallbackEndpointHarmfulZoneBelowCorridor =
				fallbackEndpointEvidence.HarmfulZoneBelow;
		}
		if (!result.NavigationFallbackRouteExists)
			throw std::runtime_error("excluding PathNode73 did not expose a finite graph fallback route");
		const vec3 navigationCorridor = pathNode73->Location() - DeathFanPathNode73LaunchAnchor;
		for (int index = 1; index <= CorridorSamples; index++)
		{
			const vec3 point = DeathFanPathNode73LaunchAnchor + navigationCorridor
				* (static_cast<float>(index) / static_cast<float>(CorridorSamples + 1));
			const CollisionHit immediateSupport = pawn->ProbeMoveCollision(
				point, vec3(0.0f, 0.0f, -ImmediateSupportDistance), true);
			if (IsWalkableStaticSupport(pawn, immediateSupport))
				continue;
			result.NavigationUnsupportedCorridorSample = true;
			for (int zoneIndex = 1; zoneIndex <= ZoneSamplesPerCorridorPoint; zoneIndex++)
			{
				const vec3 below = point + vec3(0.0f, 0.0f,
					-DeepSupportDistance * static_cast<float>(zoneIndex) / ZoneSamplesPerCorridorPoint);
				UZoneInfo* zone = pawn->XLevel()->Model->FindRegion(below, pawn->Level()).Zone;
				if (zone && zone->bPainZone() && zone->DamagePerSec() > 0)
				{
					result.NavigationHarmfulZoneBelowCorridor = true;
					break;
				}
			}
		}
		if (!result.NavigationUnsupportedCorridorSample || !result.NavigationHarmfulZoneBelowCorridor)
			throw std::runtime_error("PathNode73 direct corridor lacks unsupported harmful-drop evidence");

		for (UNavigationPoint* navPoint = pawn->Level()->NavigationPointList(); navPoint;
			navPoint = navPoint->nextNavigationPoint())
		{
			const vec3 delta = navPoint->Location() - DeathFanPathNode73LaunchAnchor;
			if (dot(delta, delta) > 1000.0f * 1000.0f)
				continue;
			result.NavigationCandidateCount++;
			if (!pawn->ActorReachable(navPoint))
				continue;
			result.NavigationCandidateDirectReachableCount++;
			const CorridorHazardEvidence evidence = CollectCorridorHazardEvidence(
				pawn, DeathFanPathNode73LaunchAnchor, navPoint->Location());
			if (evidence.UnsupportedSample && evidence.HarmfulZoneBelow)
			{
				result.NavigationCandidateUnsafeDirectReachableCount++;
				if (result.FirstUnsafeNavigationCandidateActor.empty())
					result.FirstUnsafeNavigationCandidateActor = navPoint->Name.ToString();
			}
			else
			{
				result.NavigationCandidateSafeDirectReachableCount++;
				if (result.FirstSafeNavigationCandidateActor.empty())
					result.FirstSafeNavigationCandidateActor = navPoint->Name.ToString();
			}
		}

		pawn->DisableEvent(ToNameString(EventName::Tick));
		pawn->DisableEvent(ToNameString(EventName::UpdateTactics));
		pawn->Velocity() = vec3(0.0f);
		pawn->Acceleration() = vec3(0.0f);
		pawn->SetPhysics(PHYS_Walking);
		pawn->Tick(FixtureTickSeconds);
		result.StationaryNavigationAnchorFallingObserved = pawn->Physics() == PHYS_Falling;
		if (!pawn->SetLocation(DeathFanPathNode73LaunchAnchor))
			throw std::runtime_error("fixture could not restore the PathNode73 launch anchor for live movement");
		pawn->Velocity() = vec3(0.0f);
		pawn->Acceleration() = vec3(0.0f);
		pawn->UpdateActorZone();
		pawn->SetPhysics(PHYS_Walking);
		pawn->MoveToward(pathNode73, 1.0f);
		result.LiveNavigationMoveTowardArmed = pawn->StateFrame
			&& pawn->StateFrame->LatentState == LatentRunState::MoveToward
			&& pawn->MoveTarget() == pathNode73;
		if (!result.LiveNavigationMoveTowardArmed)
			throw std::runtime_error("fixture could not arm a direct live PathNode73 MoveToward command");
		for (int tick = 0; tick < LiveNavigationTickLimit; tick++)
		{
			pawn->Tick(FixtureTickSeconds);
			result.LiveNavigationTicks++;
			if (pawn->Physics() == PHYS_Falling)
				result.LiveNavigationFallingObserved = true;
			UZoneInfo* footZone = pawn->FootRegion().Zone;
			if (footZone && footZone->bPainZone() && footZone->DamagePerSec() > 0)
			{
				result.LiveNavigationHarmfulEntryObserved = true;
				break;
			}
			if (result.LiveNavigationFallingObserved)
				break;
		}
		if (!result.LiveNavigationFallingObserved)
			throw std::runtime_error("direct live PathNode73 MoveToward did not enter falling physics");
		result.Ran = true;
		result.Passed = true;
	}
	catch (const std::exception& error)
	{
		Fail(result, error.what());
	}

	if (pawn && !pawn->bDeleteMe())
	{
		pawn->Velocity() = originalVelocity;
		pawn->Acceleration() = originalAcceleration;
		pawn->SetPhysics(originalPhysics);
		pawn->MoveTarget() = originalMoveTarget;
		pawn->MoveTimer() = originalMoveTimer;
		if (pawn->StateFrame)
			pawn->StateFrame->LatentState = originalLatentState;
		if (originalTickEnabled)
			pawn->EnableEvent(ToNameString(EventName::Tick));
		else
			pawn->DisableEvent(ToNameString(EventName::Tick));
		if (originalUpdateTacticsEnabled)
			pawn->EnableEvent(ToNameString(EventName::UpdateTactics));
		else
			pawn->DisableEvent(ToNameString(EventName::UpdateTactics));
		pawn->SetLocation(originalLocation);
		pawn->UpdateActorZone();
	}
	engine.SetBotBenchmarkInventoryMarkerDirectReachSafetyEnabled(
		originalInventoryMarkerSafetyEnabled);
	return result;
}

void RegisterBotInventoryRouteHandoffFixtureDriver(HeadlessDriverRegistry& registry)
{
	if (!registry.Contains("bot-inventory-route-handoff-fixture"))
	{
		registry.Register("bot-inventory-route-handoff-fixture", [](Engine& engine)
		{
			return std::make_unique<BotInventoryRouteHandoffFixtureDriver>(engine);
		});
	}
}
