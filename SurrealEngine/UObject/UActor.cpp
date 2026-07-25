#include "Precomp.h"
#include "Input/DesktopInputDefaults.h"
#include "UActor.h"
#include "ULevel.h"
#include "UMesh.h"
#include "UTexture.h"
#include "UConSys.h"
#include "USubsystem.h"
#include "ActorMovement.h"
#include "ActorMoveCollisionProbe.h"
#include "PawnFailedNavigationMemory.h"
#include "PawnFallingTwoPlaneSafety.h"
#include "PawnLedgeTransition.h"
#include "PawnPainZoneFallPrediction.h"
#include "PawnWalkingStepPreflight.h"
#include "PawnPainLedgeRecovery.h"
#include "PawnMovementArrival.h"
#include "PawnMoveToward.h"
#include "PawnPathCost.h"
#include "PawnWallAdjustment.h"
#include "PawnWallAdjustRecovery.h"
#include "PawnHazardWaterEgressRouteCertificate.h"
#include "BotAI/HarmfulZoneEscapeGate.h"
#include "BotAI/FallingHazardRecoveryGate.h"
#include "BotAI/HazardSwimEgressGate.h"
#include "BotAI/HazardSwimEgressLiveSteer.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Package/PackageManager.h"
#include "Package/IniProperty.h"
#include "Engine.h"
#include "Render/RenderSubsystem.h"
#include <set>
#include <unordered_map>
#include <unordered_set>

// TODO: Compare behavior more closely with original engine. Might differ depending on game.
static constexpr float stepDownDeltaFactor = 1.3f;

namespace
{
	constexpr int painZoneFallPredictionSteps = 24;
	constexpr int painZoneFallMaxSamplesPerStep = 8;
	constexpr float painZoneFallPredictionDelta = 1.0f / 16.0f;
	constexpr float painLedgeRecoveryDuration = 0.5f;
	constexpr float painLedgeRepeatRadius = 96.0f;
	constexpr float painLedgeRecoveryRadius = 96.0f;
	constexpr float painLedgeDirectionAlignment = 0.5f;
	constexpr float painLedgeRecoveryProbeDistance = 64.0f;
	constexpr float harmfulZoneEscapeProbeDistance = 64.0f;
	constexpr float wallAdjustRepeatWindow = 1.0f;
	constexpr float wallAdjustRepeatRadius = 4.0f;
	constexpr float wallAdjustMinimumSustainedTime = 0.25f;
	constexpr uint32_t wallAdjustMinimumObservations = 2;
	constexpr float wallAdjustSteeringDuration = 0.5f;
	constexpr float wallAdjustRecoveryDistance = 100.0f;
	constexpr float wallAdjustRecoveryEscapeRadius = 96.0f;
	constexpr float wallAdjustUnsafeAlignment = 0.5f;
	constexpr float moveStallProgressRadius = 4.0f;
	constexpr float moveStallDetectionSeconds = 2.0f;
	constexpr float failedNavigationRepeatWindow = 6.0f;
	constexpr float failedNavigationAvoidanceDuration = 4.0f;
	constexpr float failedNavigationEscapeRadius = 96.0f;
	// The move-stall watchdog already requires two seconds without progress, so
	// one eligible detection is sufficient evidence for bounded avoidance.
	constexpr uint8_t failedNavigationRequiredFailures = 1;
	constexpr int32_t failedNavigationFirstHopCost = 4096;
	constexpr float failedNavigationLiftExitLandingRadius = 400.0f;
	constexpr float failedNavigationLiftExitLandingStepHeights = 2.0f;
	constexpr float fallingHazardRecoveryMaximumDuration = 1.0f;
	constexpr float fallingWalkableNormalZ = 0.7f;
	constexpr float walkingStepWalkableNormalZ = 0.7071f;
	constexpr float walkingStepVerticalWallNormalZ = 0.2f;
	constexpr float walkingStepMaximumDelta = 96.0f;
	constexpr float walkingStepMaximumFallSegmentDelta = 4096.0f;
	constexpr float walkingStepMaximumForecastDrop = 4096.0f;

	std::vector<const void*> LiftExitLandingAdjacency(
		ULiftExit* liftExit, const Array<LevelReachSpec>& reachSpecs)
	{
		std::vector<const void*> adjacent;
		auto addNeighbor = [&](int specIndex, bool upstream)
		{
			if (specIndex < 0 || static_cast<size_t>(specIndex) >= reachSpecs.size())
				return;
			const LevelReachSpec& spec = reachSpecs[specIndex];
			if (spec.bPruned)
				return;
			UNavigationPoint* neighbor = nullptr;
			if (upstream && spec.endActor == liftExit)
				neighbor = spec.startActor;
			else if (!upstream && spec.startActor == liftExit)
				neighbor = spec.endActor;
			if (!neighbor || neighbor->bDeleteMe() || UObject::TryCast<ULiftExit>(neighbor)
				|| UObject::TryCast<ULiftCenter>(neighbor))
				return;
			if (std::find(adjacent.begin(), adjacent.end(), neighbor) == adjacent.end())
				adjacent.push_back(neighbor);
		};

		for (int specIndex : liftExit->Paths())
			addNeighbor(specIndex, false);
		for (int specIndex : liftExit->upstreamPaths())
			addNeighbor(specIndex, true);
		return adjacent;
	}

	bool IsHarmfulPainZone(UPawn* pawn, UZoneInfo* zone)
	{
		return zone && zone->bPainZone() && zone->DamageType() != pawn->ReducedDamageType();
	}

	bool IsExactHarmfulZone(UZoneInfo* zone)
	{
		return zone && zone->bPainZone() && zone->DamagePerSec() > 0;
	}

	bool IsSafeFallingHazardRecoveryZone(UZoneInfo* zone)
	{
		return zone && zone->bStatic() && !zone->bWaterZone()
			&& !IsExactHarmfulZone(zone);
	}

	bool IsAutonomousPlayerBot(UPawn* pawn)
	{
		UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(pawn);
		return pawn && PawnMovement::IsAIControlledPlayer(
			pawn->bIsPlayer(), player != nullptr, player && player->Player() != nullptr);
	}

	bool IsStockAutonomousPlayerBot(UPawn* pawn)
	{
		if (!IsAutonomousPlayerBot(pawn))
			return false;
		return (engine->LaunchInfo.IsUnrealTournament() && pawn->IsA("Bot"))
			|| (engine->LaunchInfo.IsUnreal1() && pawn->IsA("Bots"));
	}

	bool IsMovementLatentState(LatentRunState state)
	{
		return state == LatentRunState::MoveTo
			|| state == LatentRunState::MoveToward
			|| state == LatentRunState::StrafeTo
			|| state == LatentRunState::StrafeFacing;
	}

	PawnMovement::MoveStallLatentMode MoveStallLatentModeFor(LatentRunState state)
	{
		switch (state)
		{
		case LatentRunState::MoveTo: return PawnMovement::MoveStallLatentMode::MoveTo;
		case LatentRunState::MoveToward: return PawnMovement::MoveStallLatentMode::MoveToward;
		case LatentRunState::StrafeTo: return PawnMovement::MoveStallLatentMode::StrafeTo;
		case LatentRunState::StrafeFacing: return PawnMovement::MoveStallLatentMode::StrafeFacing;
		default: return PawnMovement::MoveStallLatentMode::Other;
		}
	}

	bool PredictedFallEntersHarmfulPainZone(
		UPawn* pawn, const vec3& initialVelocity, const vec3& acceleration)
	{
		UZoneInfo* startZone = pawn->FootRegion().Zone;
		UZoneInfo* physicsZone = pawn->Region().Zone;
		if (!startZone || !physicsZone)
			return false;

		PawnMovement::FallPredictionState prediction = {
			.Location = pawn->Location(),
			.Velocity = initialVelocity
		};
		const PawnMovement::FallPredictionStep step = {
			.Acceleration = acceleration,
			.Gravity = physicsZone->ZoneGravity(),
			.ZoneVelocity = physicsZone->ZoneVelocity(),
			.GroundSpeed = pawn->GroundSpeed(),
			.TerminalVelocity = physicsZone->ZoneTerminalVelocity(),
			.Elapsed = painZoneFallPredictionDelta
		};
		const TraceFlags traceFlags = {
			.movers = true,
			.world = true
		};
		const vec3 traceExtent(pawn->CollisionRadius(), pawn->CollisionRadius(), pawn->CollisionHeight());
		enum class SegmentZoneResult
		{
			Clear,
			HarmfulPain,
			Unknown
		};
		auto sampleSegment = [&](const vec3& origin, const vec3& segment)
		{
			const int sampleCount = PawnMovement::BoundedFallSegmentSampleCount(
				length(segment), std::max(pawn->MaxStepHeight(), 1.0f),
				painZoneFallMaxSamplesPerStep);
			for (int sampleIndex = 1; sampleIndex <= sampleCount; sampleIndex++)
			{
				const vec3 sample = origin
					+ segment * (static_cast<float>(sampleIndex) / sampleCount);
				const vec3 foot = sample - vec3(0.0f, 0.0f, pawn->CollisionHeight());
				UZoneInfo* sampleZone = pawn->XLevel()->Model->FindRegion(
					foot, pawn->Level()).Zone;
				if (IsHarmfulPainZone(pawn, sampleZone))
					return SegmentZoneResult::HarmfulPain;
				if (sampleZone != startZone)
					return SegmentZoneResult::Unknown;
			}
			return SegmentZoneResult::Clear;
		};

		for (int index = 0; index < painZoneFallPredictionSteps; index++)
		{
			PawnMovement::FallPredictionState next = PawnMovement::PredictFallStep(prediction, step);
			if (!next.Valid)
				return false;

			const CollisionHit hit = pawn->XLevel()->Collision.TraceFirstHit(
				prediction.Location, next.Location, pawn, traceExtent, traceFlags);
			const vec3 segment = (next.Location - prediction.Location) * hit.Fraction;
			const SegmentZoneResult directZoneResult = sampleSegment(prediction.Location, segment);
			if (directZoneResult == SegmentZoneResult::HarmfulPain)
				return true;
			if (directZoneResult == SegmentZoneResult::Unknown)
				return false;
			if (hit.Fraction < 1.0f)
				return false;

			prediction = next;
		}
		return false;
	}

	bool IsFiniteVector(const vec3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	float MaximumAbsoluteComponent(const vec3& value)
	{
		return std::max({ std::abs(value.x), std::abs(value.y), std::abs(value.z) });
	}

	PawnMovement::FallingParityCollisionKind ClassifyFallingParityCollision(
		UPawn* pawn, const CollisionHit& hit)
	{
		using PawnMovement::FallingParityCollisionKind;
		if (!std::isfinite(hit.Fraction) || hit.Fraction < 0.0f || hit.Fraction > 1.0f)
			return FallingParityCollisionKind::Unknown;
		if (hit.Fraction == 1.0f)
			return FallingParityCollisionKind::Clear;
		if (!hit.Actor || hit.Actor == pawn->Level())
			return FallingParityCollisionKind::StaticWorld;
		if (UObject::TryCast<UMover>(hit.Actor))
			return FallingParityCollisionKind::Mover;
		return FallingParityCollisionKind::DynamicActor;
	}

	PawnMovement::FallingHazardCollisionKind ClassifyFallingHazardCollision(
		UPawn* pawn, const CollisionHit& hit)
	{
		using PawnMovement::FallingHazardCollisionKind;
		if (!std::isfinite(hit.Fraction) || hit.Fraction < 0.0f
			|| hit.Fraction > 1.0f)
			return FallingHazardCollisionKind::Unknown;
		if (hit.Fraction == 1.0f)
			return FallingHazardCollisionKind::Clear;
		if (!hit.Actor || hit.Actor == pawn->Level())
			return FallingHazardCollisionKind::StaticWorld;
		if (UObject::TryCast<UMover>(hit.Actor))
			return FallingHazardCollisionKind::Mover;
		return FallingHazardCollisionKind::DynamicActor;
	}

	PawnMovement::WalkingHitWallBlockerKind ClassifyWalkingHitWallBlocker(
		UPawn* pawn, const CollisionHit& hit)
	{
		using PawnMovement::WalkingHitWallBlockerKind;
		if (!std::isfinite(hit.Fraction) || hit.Fraction < 0.0f
			|| hit.Fraction > 1.0f)
		{
			return WalkingHitWallBlockerKind::Unknown;
		}
		if (!hit.Actor || hit.Actor == pawn->Level())
			return WalkingHitWallBlockerKind::StaticWorld;
		if (UObject::TryCast<UMover>(hit.Actor))
			return WalkingHitWallBlockerKind::Mover;
		return WalkingHitWallBlockerKind::DynamicActor;
	}

	PawnMovement::FallingHazardZoneId FallingHazardZoneIdentity(
		const PointRegion& region)
	{
		PawnMovement::FallingHazardZoneId identity;
		if (!region.Zone || region.Zone->Index < 0
			|| static_cast<uint64_t>(region.Zone->Index)
				>= std::numeric_limits<uint32_t>::max())
			return identity;
		identity.Known = true;
		identity.ZoneActorId = static_cast<uint32_t>(region.Zone->Index) + 1;
		identity.ZoneNumber = region.ZoneNumber;
		return identity;
	}

	PawnMovement::FallingHazardForecastZoneObservation
		BuildFallingHazardZoneObservation(UPawn* pawn, const PointRegion& region)
	{
		PawnMovement::FallingHazardForecastZoneObservation observation;
		observation.Identity = FallingHazardZoneIdentity(region);
		if (!region.Zone)
			return observation;
		observation.PainZone = region.Zone->bPainZone();
		observation.DamagePerSecond = region.Zone->DamagePerSec();
		observation.WaterZone = region.Zone->bWaterZone();
		observation.DamageTypeMatchesReduced =
			region.Zone->DamageType() == pawn->ReducedDamageType();
		observation.Gravity = region.Zone->ZoneGravity();
		observation.ZoneVelocity = region.Zone->ZoneVelocity();
		observation.TerminalVelocity = region.Zone->ZoneTerminalVelocity();
		return observation;
	}

	PawnMovement::FallingHazardForecastPointObservation
		BuildFallingHazardPointObservation(UPawn* pawn, const vec3& center)
	{
		PawnMovement::FallingHazardForecastPointObservation observation;
		const PointRegion centerRegion = pawn->XLevel()->Model->FindRegion(
			center, pawn->Level());
		const PointRegion footRegion = pawn->XLevel()->Model->FindRegion(
			center - vec3(0.0f, 0.0f, pawn->CollisionHeight()), pawn->Level());
		const PointRegion headRegion = pawn->XLevel()->Model->FindRegion(
			center + vec3(0.0f, 0.0f, pawn->EyeHeight()), pawn->Level());
		observation.Center = BuildFallingHazardZoneObservation(pawn, centerRegion);
		observation.Foot = BuildFallingHazardZoneObservation(pawn, footRegion);
		observation.Head = BuildFallingHazardZoneObservation(pawn, headRegion);
		observation.Physics = observation.Center;
		return observation;
	}

	PawnMovement::FallingHazardForecastUpdate CompleteFallingHazardForecast(
		UPawn* pawn, const PawnMovement::FallingHazardForecastInput& input)
	{
		using namespace PawnMovement;
		FallingHazardForecastUpdate update = BeginFallingHazardForecast(input);
		std::vector<FallingHazardForecastPathSample> samples;
		for (size_t probeCount = 0;
			!update.Complete && probeCount <= FallingHazardForecastMaximumSegments;
			probeCount++)
		{
			if (!update.Probe.Valid)
				break;
			const CollisionHit hit = pawn->ProbeMoveCollision(
				update.Probe.Origin, update.Probe.Delta, true);
			FallingHazardForecastSweepObservation observation;
			observation.Collision = ClassifyFallingHazardCollision(pawn, hit);
			observation.Fraction = hit.Fraction;
			observation.Normal = hit.Normal;

			const float traveledDistance = length(
				update.Probe.Delta * hit.Fraction);
			size_t sampleCount = 1;
			if (std::isfinite(traveledDistance)
				&& traveledDistance > FallingHazardForecastVectorTolerance)
			{
				const float required = std::ceil(
					traveledDistance / input.MaximumSampleSpacing);
				if (!std::isfinite(required)
					|| required > static_cast<float>(input.MaximumSamples))
				{
					observation.SampleCapExhausted = true;
				}
				else
				{
					sampleCount = static_cast<size_t>(required);
				}
			}
			if (!observation.SampleCapExhausted
				&& (update.State.SampleCount > input.MaximumSamples
					|| sampleCount > input.MaximumSamples
						- update.State.SampleCount))
			{
				observation.SampleCapExhausted = true;
			}
			samples.clear();
			if (!observation.SampleCapExhausted)
			{
				samples.reserve(sampleCount);
				for (size_t sampleIndex = 1; sampleIndex <= sampleCount;
					sampleIndex++)
				{
					const float fraction = static_cast<float>(sampleIndex)
						/ static_cast<float>(sampleCount);
					FallingHazardForecastPathSample sample;
					sample.DistanceAlongSegment = traveledDistance * fraction;
					sample.Zones = BuildFallingHazardPointObservation(pawn,
						update.Probe.Origin
							+ update.Probe.Delta * (hit.Fraction * fraction));
					samples.push_back(std::move(sample));
				}
			}
			observation.Samples = samples;
			update = ObserveFallingHazardForecastSweep(update.State, observation);
		}
		return update;
	}

	uint32_t FallingHazardCallbackMask(uint32_t moveMask)
	{
		using namespace PawnMovement;
		uint32_t mask = 0;
		if (moveMask & (MoveCallbackBasedActor | MoveCallbackEncroachment))
			mask |= FallingHazardEncroachmentCallback;
		if (moveMask & MoveCallbackBump)
			mask |= FallingHazardBumpCallback;
		if (moveMask & (MoveCallbackTouch | MoveCallbackUnTouch))
			mask |= FallingHazardTouchCallback;
		if (moveMask & MoveCallbackRegionChange)
			mask |= FallingHazardRegionCallback;
		if (moveMask & MoveCallbackFootRegionChange)
			mask |= FallingHazardFootZoneCallback;
		if (moveMask & MoveCallbackHeadRegionChange)
			mask |= FallingHazardHeadZoneCallback;
		if (moveMask & MoveCallbackHitWall)
			mask |= FallingHazardHitWallCallback;
		return mask;
	}

	PawnMovement::WalkingStepCollisionKind ClassifyWalkingStepCollision(
		UPawn* pawn, const CollisionHit& hit)
	{
		using PawnMovement::WalkingStepCollisionKind;
		if (!std::isfinite(hit.Fraction) || hit.Fraction < 0.0f || hit.Fraction > 1.0f)
			return WalkingStepCollisionKind::Unknown;
		if (hit.Fraction == 1.0f)
			return WalkingStepCollisionKind::Clear;
		if (!hit.Actor || hit.Actor == pawn->Level())
			return WalkingStepCollisionKind::StaticBsp;
		if (UObject::TryCast<UMover>(hit.Actor))
			return WalkingStepCollisionKind::Mover;
		return WalkingStepCollisionKind::DynamicActor;
	}

	PawnMovement::WalkingStepZoneKind ClassifyWalkingStepZone(UZoneInfo* zone)
	{
		using PawnMovement::WalkingStepZoneKind;
		if (!zone)
			return WalkingStepZoneKind::Unknown;
		if (zone->bPainZone())
			return WalkingStepZoneKind::Pain;
		if (zone->bWaterZone())
			return WalkingStepZoneKind::Water;
		return WalkingStepZoneKind::Safe;
	}

	PawnMovement::WalkingStepSweepObservation ProbeWalkingStepSweep(
		UPawn* pawn, const vec3& origin, const vec3& delta,
		CollisionHit* collisionHit = nullptr,
		bool* scriptVisibleActorContact = nullptr)
	{
		PawnMovement::WalkingStepSweepObservation observation;
		observation.Delta = delta;
		if (!IsFiniteVector(origin) || !IsFiniteVector(delta))
			return observation;
		CollisionHitList tracedHits;
		const CollisionHit hit = pawn->ProbeMoveCollision(
			origin, delta, true, scriptVisibleActorContact ? &tracedHits : nullptr);
		if (collisionHit)
			*collisionHit = hit;
		if (scriptVisibleActorContact)
		{
			*scriptVisibleActorContact = std::any_of(
				tracedHits.begin(), tracedHits.end(), [pawn](const CollisionHit& traced)
				{
					return traced.Actor && traced.Actor != pawn
						&& !traced.Actor->IsBasedOn(pawn)
						&& !pawn->IsBasedOn(traced.Actor);
				});
		}
		observation.Collision = ClassifyWalkingStepCollision(pawn, hit);
		observation.HitNormal = hit.Normal;
		return observation;
	}

	PawnMovement::WalkingFallForecastObservation ForecastWalkingStepFall(
		UPawn* pawn, const vec3& start, const vec3& initialVelocity,
		const vec3& acceleration, UZoneInfo* physicsZone,
		PawnMovement::WalkingStepPreflightDiagnosticRecord* diagnostic)
	{
		using namespace PawnMovement;
		WalkingFallForecastObservation forecast;
		if (!physicsZone || !IsFiniteVector(start) || !IsFiniteVector(initialVelocity)
			|| !IsFiniteVector(acceleration))
			return forecast;

		FallPredictionState prediction = { .Location = start, .Velocity = initialVelocity };
		const FallPredictionStep step = {
			.Acceleration = acceleration,
			.Gravity = physicsZone->ZoneGravity(),
			.ZoneVelocity = physicsZone->ZoneVelocity(),
			.GroundSpeed = pawn->GroundSpeed(),
			.TerminalVelocity = physicsZone->ZoneTerminalVelocity(),
			.Elapsed = painZoneFallPredictionDelta
		};
		for (int stepIndex = 0; stepIndex < painZoneFallPredictionSteps; stepIndex++)
		{
			FallPredictionState next = PredictFallStep(prediction, step);
			if (!next.Valid)
				return forecast;
			const vec3 segment = next.Location - prediction.Location;
			if (!IsFiniteVector(segment) || dot(segment, segment) <= 0.0001f)
				return forecast;

			const CollisionHit hit = pawn->ProbeMoveCollision(
				prediction.Location, segment, true);
			const WalkingStepCollisionKind collision =
				ClassifyWalkingStepCollision(pawn, hit);
			if (collision == WalkingStepCollisionKind::Clear)
			{
				prediction = next;
				continue;
			}

			const vec3 traveled = segment * hit.Fraction;
			if (diagnostic && diagnostic->FallHitCount < diagnostic->FallHitFractions.size())
				diagnostic->FallHitFractions[diagnostic->FallHitCount++] = hit.Fraction;
			const vec3 hitCenter = prediction.Location + traveled;
			forecast.TotalDrop = start.z - hitCenter.z;
			if (collision == WalkingStepCollisionKind::StaticBsp
				&& IsFiniteVector(hit.Normal)
				&& std::abs(hit.Normal.z) <= walkingStepVerticalWallNormalZ)
			{
				if (forecast.ContinuationCount >= forecast.Continuations.size())
					return forecast;
				WalkingFallContinuationObservation& continuation =
					forecast.Continuations[forecast.ContinuationCount++];
				continuation.Collision = collision;
				continuation.SegmentDelta = traveled;
				continuation.HitNormal = hit.Normal;
				prediction.Location = hitCenter;
				prediction.Velocity = next.Velocity
					- hit.Normal * dot(next.Velocity, hit.Normal);
				continue;
			}

			forecast.Complete = true;
			forecast.Landing.Collision = collision;
			forecast.Landing.Normal = hit.Normal;
			if (collision == WalkingStepCollisionKind::StaticBsp)
			{
				UZoneInfo* landingZone = pawn->XLevel()->Model->FindRegion(
					hitCenter - vec3(0.0f, 0.0f, pawn->CollisionHeight()),
					pawn->Level()).Zone;
				forecast.Landing.Zone = ClassifyWalkingStepZone(landingZone);
				if (landingZone)
				{
					forecast.PainDamageImmunityKnown = true;
					forecast.PainDamageImmune = landingZone->bPainZone()
						&& landingZone->DamageType() == pawn->ReducedDamageType();
					forecast.PainDamagePerSecKnown = true;
					forecast.PainDamagePerSec = static_cast<float>(landingZone->DamagePerSec());
				}
			}
			return forecast;
		}
		forecast.TotalDrop = start.z - prediction.Location.z;
		return forecast;
	}
}

UActor* UActor::Spawn(UClass* SpawnClass, std::optional<UActor*> SpawnOwner, std::optional<NameString> SpawnTag, std::optional<vec3> SpawnLocation, std::optional<Rotator> SpawnRotation)
{
	if (!SpawnClass || SpawnClass->ClsFlags & ClassFlags::Abstract)
	{
		LogMessage("Could not spawn class: " + (SpawnClass ? SpawnClass->Name.ToString() : std::string("null")));
		return nullptr;
	}

	vec3 location = SpawnLocation ? *SpawnLocation : Location();
	Rotator rotation = SpawnRotation ? *SpawnRotation : Rotation();

	float radius = SpawnClass->GetDefaultObject<UActor>()->CollisionRadius();
	float height = SpawnClass->GetDefaultObject<UActor>()->CollisionHeight();
	bool bCollideWorld = SpawnClass->GetDefaultObject<UActor>()->bCollideWorld();
	bool bCollideWhenPlacing = SpawnClass->GetDefaultObject<UActor>()->bCollideWhenPlacing();
	if (bCollideWorld || bCollideWhenPlacing)
	{
		auto result = CheckLocation(location, radius, height, bCollideWorld || bCollideWhenPlacing);
		if (!result.first)
		{
			LogMessage("Could not find usable location when trying to spawn: " + SpawnClass->Name.ToString());
			return nullptr;
		}
		location = result.second;
	}

	// To do: package needs to be grabbed from outer, or the "transient package" if it is None, a virtual package for runtime objects
	// To do: find unique new name in the package
	static std::map<NameString, int> nextIndex;
	NameString name = SpawnClass->Name.ToString() + std::to_string(nextIndex[SpawnClass->Name]++);
	UActor* actor = UObject::Cast<UActor>(engine->LevelPackage->NewObject(name, UObject::Cast<UClass>(SpawnClass), ObjectFlags::Transient, true));

	actor->Outer() = XLevel()->Outer();
	actor->XLevel() = XLevel();
	actor->Level() = Level();
	actor->Tag() = (SpawnTag && !SpawnTag->IsNone()) ? *SpawnTag : SpawnClass->Name;
	actor->bTicked() = bTicked(); // To do: should it tick in the same world tick it was spawned in or wait until the next one?
	actor->Instigator() = Instigator();
	actor->Brush() = nullptr;
	actor->Location() = location;
	actor->OldLocation() = location;
	actor->Rotation() = rotation;
	actor->Region().Zone = actor->Level();
	actor->Index = (int)XLevel()->Actors.size();
	XLevel()->Actors.push_back(actor);
	XLevel()->Collision.AddToCollision(actor);
	XLevel()->Light.AddLight(actor);

	actor->SetOwner(SpawnOwner.has_value() && SpawnOwner.value() ? *SpawnOwner : nullptr);

	if (Level()->bBegunPlay())
	{
		CallEvent(actor, EventName::Spawned);
		CallEvent(actor, EventName::PreBeginPlay);
		CallEvent(actor, EventName::BeginPlay);

		if (actor->bDeleteMe())
		{
			LogMessage("Object deleted itself during Spawn!");
			return nullptr;
		}

		// To do: we need to call EventName::EncroachingOn events here?

		actor->InitActorZone();

		CallEvent(actor, EventName::PostBeginPlay);
		CallEvent(actor, EventName::SetInitialState);
		if (engine->LaunchInfo.IsDeusEx())
			CallEvent(actor, "PostPostBeginPlay");

		actor->InitBase();

		if (engine->LaunchInfo.ue1Version >= 400)
		{
			static bool spawnNotificationLocked = false;
			if (!spawnNotificationLocked)
			{
				struct NotificationLockGuard
				{
					NotificationLockGuard() { spawnNotificationLocked = true; }
					~NotificationLockGuard() { spawnNotificationLocked = false; }
				} lockGuard;

				for (USpawnNotify* notifyObj = Level()->SpawnNotify(); notifyObj != nullptr; notifyObj = notifyObj->Next())
				{
					UClass* cls = notifyObj->ActorClass();
					if (cls && actor->IsA(cls->Name))
						actor = UObject::Cast<UGameInfo>(CallEvent(notifyObj, EventName::SpawnNotification, { ExpressionValue::ObjectValue(actor) }).ToObject());
				}
			}
		}
	}

	return actor;
}

void UActor::InitBase()
{
	if (engine->LaunchInfo.ue1Version > 219)
	{
		NameString attachTag = AttachTag();
		if (!attachTag.IsNone())
		{
			for (UActor* levelActor : XLevel()->Actors)
			{
				if (levelActor && levelActor->Tag() == attachTag)
				{
					levelActor->SetBase(this, false);
				}
			}
			return;
		}
	}

	// Find base for certain types
	bool isDecorationInventoryOrPawn = UObject::TryCast<UDecoration>(this) || UObject::TryCast<UInventory>(this) || UObject::TryCast<UPawn>(this);
	if (isDecorationInventoryOrPawn && !ActorBase() && bCollideWorld() && (Physics() == PHYS_None || Physics() == PHYS_Rotating))
	{
		CollisionHitList hits = XLevel()->Collision.OverlapTest(this);
		if (!hits.empty())
		{
			SetBase(hits.front().Actor, true);
		}
	}

	if (engine->LaunchInfo.ue1Version < 400 && !ActorBase()) // Unreal expects a base to always exist. What about UT? TournamentPlayer seems to indicate not.
	{
		SetBase(Level(), false);
	}
}

std::pair<bool, vec3> UActor::CheckLocation(vec3 location, float radius, float height, bool check)
{
	// Search for a valid spot near the location

	if (!check)
		return { true, location };

	// What is a reasonable size for this grid? what did UE1 do?
	int offset[] = { 0, 1, -1 };
	bool found = false;
	float scale = std::max(radius, height);
	for (int z = 0; z < 3 && !found; z++)
	{
		for (int y = 0; y < 3 && !found; y++)
		{
			for (int x = 0; x < 3 && !found; x++)
			{
				vec3 testlocation = location + vec3(offset[x] * scale, offset[y] * scale, offset[z] * scale);
				CollisionHitList hits = XLevel()->Collision.OverlapTest(testlocation, height, radius, false, true, false);
				if (hits.empty())
				{
					location = testlocation;
					found = true;
				}
			}
		}
	}
	return { found, location };
}

bool UActor::Destroy()
{
	//engine->LogMessage("UActor.Destroy(" + Class->FriendlyName.ToString() + ")");

	if (bStatic() || bNoDelete())
		return false;
	if (bDeleteMe())
		return true;

	bDeleteMe() = true;

	//GotoState({}, {}); // What should happen to function calls after Destroy() has been called? Razor2 calls SetRoll afterwards!
	SetBase(nullptr, true);

	engine->audiodev->ActorDestroyed(this);

	ULevel* level = XLevel();

	RemoveFromBspNode();
	level->Collision.RemoveFromCollision(this);
	level->Light.RemoveLight(this);

	CallEvent(this, EventName::Destroyed);

	if (engine->LaunchInfo.IsUnrealTournament_469())
	{
		for (const auto actor : Touching_UT469())
			if (actor)
				UnTouch(actor);
	}
	else
	{
		for (const auto actor : Touching())
			if (actor)
				UnTouch(actor);
	}


	SetOwner(nullptr);

	while (!ChildActors.empty())
	{
		ChildActors.back()->SetOwner(nullptr);
	}
	while (!BasedActors.empty())
	{
		BasedActors.back()->SetBase(nullptr, true);
	}

	if (Index == -1)
		throw std::runtime_error("Actor index was never set!");
	level->Actors[Index] = nullptr;

	return true;
}

PointRegion UActor::FindRegion(const vec3& offset)
{
	return XLevel()->Model->FindRegion(Location() + offset, Level());
}

void UActor::InitActorZone()
{
	Region() = FindRegion();
	if (Region().Zone->bWaterZone() && !this->IsA("Projectile"))
	{
		SetPhysics(PHYS_Swimming);
		SetBase(nullptr, true);
	}
}

void UActor::UpdateActorZone()
{
	PointRegion oldregion = Region();
	PointRegion newregion = FindRegion();
	const bool regionChanged = oldregion.Zone != newregion.Zone;
	UPawn* pawn = UObject::TryCast<UPawn>(this);

	if (oldregion.Zone && regionChanged)
		CallEvent(oldregion.Zone, EventName::ActorLeaving, { ExpressionValue::ObjectValue(this) });

	Region() = newregion;
	if (pawn && regionChanged)
	{
		pawn->CommitPendingFallingHazardSweepAtCenterBoundary(
			newregion, oldregion.Zone != nullptr);
		pawn->ObserveHarmfulZoneEscapeBoundary(oldregion.Zone, newregion.Zone, false);
	}

	if (newregion.Zone && regionChanged)
	{
		CallEvent(this, EventName::ZoneChange, { ExpressionValue::ObjectValue(newregion.Zone) });
		CallEvent(newregion.Zone, EventName::ActorEntered, { ExpressionValue::ObjectValue(this) });
	}
	if (pawn && regionChanged)
		pawn->RecoverFallingHazardCallbackReturn();

	if (Region().Zone)
	{
		if (Region().Zone->bDestructive() && IsA("Carcass"))
		{
			// If the actor is a Carcass and the zone is marked as bDestructive, destroy it.
			Destroy();
		}
		else if (engine->LaunchInfo.ue1Version > 219 && Owner() == nullptr && Region().Zone->bNoInventory() && IsA("Inventory"))
		{
			// If the new zone is bNoInventory, destroy Inventory that's not owned by anyone (i.e. in pickup state).
			Destroy();
		}
	}
}

void UActor::SetOwner(UActor* newOwner)
{
	if (Owner())
	{
		CallEvent(Owner(), EventName::LostChild, { ExpressionValue::ObjectValue(this) });
		Owner()->RemoveChildActor(this);
	}

	Owner() = newOwner;

	if (Owner())
	{
		CallEvent(Owner(), EventName::GainedChild, { ExpressionValue::ObjectValue(this) });
		Owner()->AddChildActor(this);
	}
}

void UActor::AddChildActor(UActor* actor)
{
	if (actor)
		ChildActors.push_back(actor);
}

void UActor::RemoveChildActor(UActor* actor)
{
	if (!actor)
		return;

	auto it = ChildActors.begin();

	while (it != ChildActors.end())
	{
		if (*it == actor)
		{
			ChildActors.erase(it);
			return;
		}
		it++;
	}
}

void UActor::AddBasedActor(UActor* actor)
{
	if (actor)
		BasedActors.push_back(actor);
}

void UActor::RemoveBasedActor(UActor* actor)
{
	if (!actor)
		return;

	auto it = BasedActors.begin();

	while (it != BasedActors.end())
	{
		if (*it == actor)
		{
			BasedActors.erase(it);
			return;
		}
		it++;
	}
}

void UActor::SetBase(UActor* newBase, bool sendBaseChangeEvent)
{
	if (ActorBase() != newBase)
	{
		if (this->IsBasedOn(newBase))
			return; // don't allow any cycles in the tree

		if (ActorBase() && ActorBase() != Level())
		{
			ActorBase()->RemoveBasedActor(this);
			ActorBase()->StandingCount() = (uint8_t)std::min<size_t>(ActorBase()->BasedActors.size(), 0xff);
			CallEvent(ActorBase(), EventName::Detach, { ExpressionValue::ObjectValue(this) });
		}

		ActorBase() = newBase;

		if (ActorBase() && ActorBase() != Level())
		{
			ActorBase()->AddBasedActor(this);
			ActorBase()->StandingCount() = (uint8_t)std::min<size_t>(ActorBase()->BasedActors.size(), 0xff);
			// Note: in the unlikely case of an actor having > 255 bases, StandingCount() won't be an accurate number.
			CallEvent(ActorBase(), EventName::Attach, { ExpressionValue::ObjectValue(this) });
		}

		if (sendBaseChangeEvent)
			CallEvent(this, EventName::BaseChange);
	}
}

void UActor::RelinkBasedActor()
{
	if (ActorBase() && ActorBase() != Level())
	{
		ActorBase()->AddBasedActor(this);
		ActorBase()->StandingCount() = (uint8_t)std::min<size_t>(ActorBase()->BasedActors.size(), 0xff);
	}
}

void UActor::Tick(float elapsed)
{
	const uint8_t physicsAtTickEntry = Physics();
	TickAnimation(elapsed);
	if (engine->LaunchInfo.IsDeusEx())
		TickBlendAnimation(elapsed);

	if (Role() >= ROLE_SimulatedProxy && IsEventEnabled(EventName::Tick))
	{
		CallEvent(this, EventName::Tick, { ExpressionValue::FloatValue(elapsed) });
	}

	if (StateFrame)
	{
		if (StateFrame->LatentState == LatentRunState::Sleep)
		{
			SleepTimeLeft = std::max(SleepTimeLeft - elapsed, 0.0f);
			if (SleepTimeLeft == 0.0f)
				StateFrame->LatentState = LatentRunState::Continue;
		}
		else if (StateFrame->LatentState == LatentRunState::FinishInterpolation)
		{
			if (!bInterpolating())
				StateFrame->LatentState = LatentRunState::Continue;
		}

		if (Role() >= ROLE_SimulatedProxy && StateFrame->LatentState == LatentRunState::Continue)
		{
			StateFrame->Tick();
		}
	}
	if (UPawn* pawn = UObject::TryCast<UPawn>(this);
		pawn && physicsAtTickEntry != PHYS_Falling
		&& Physics() == PHYS_Falling)
	{
		pawn->QueueFallingHazardForecastSource(
			PawnMovement::FallingHazardForecastSource::ExternalImpulseCommit);
	}

	TickPhysics(elapsed);

	if (TimerRate() > 0.0f) // Role() == ROLE_Authority && RemoteRole() == ROLE_AutonomousProxy
	{
		TimerCounter() += elapsed;
		while (TimerRate() > 0.0f && TimerCounter() > TimerRate())
		{
			TimerCounter() -= TimerRate();
			if (!bTimerLoop())
				TimerRate() = 0.0f;
			CallEvent(this, EventName::Timer);
		}
	}
}

void UActor::TickPhysics(float elapsed)
{
	for (float timeLeft = elapsed; timeLeft > 0.0f && !bDeleteMe(); timeLeft -= 0.02f)
	{
		float physTimeElapsed = std::min(timeLeft, 0.02f);
		UPawn* parityPawn = UObject::TryCast<UPawn>(this);
		if (parityPawn && parityPawn->HasActiveFallingParityRealizedLifecycle()
			&& !parityPawn->HasFallingParityRealizedContinuity())
		{
			parityPawn->FinishFallingParityRealizedTrace(
				PawnMovement::FallingParityRealizedOutcome::ContinuityLost);
		}
		int mode = Physics();
		if (mode != PHYS_None)
		{
			switch (mode)
			{
			case PHYS_Walking: TickWalking(physTimeElapsed); break;
			case PHYS_Falling: TickFalling(physTimeElapsed); break;
			case PHYS_Swimming: TickSwimming(physTimeElapsed); break;
			case PHYS_Flying: TickFlying(physTimeElapsed); break;
			case PHYS_Rotating: break;
			case PHYS_Projectile: TickProjectile(physTimeElapsed); break;
			case PHYS_Rolling: TickRolling(physTimeElapsed); break;
			case PHYS_Interpolating: TickInterpolating(physTimeElapsed); break;
			case PHYS_MovingBrush: TickMovingBrush(physTimeElapsed); break;
			case PHYS_Spider: TickSpider(physTimeElapsed); break;
			case PHYS_Trailer: TickTrailer(physTimeElapsed); break;
			}
			TickRotating(physTimeElapsed); // Rotation logic applies to multiple physics modes and not just PHYS_Rotating
		}

		if (engine->LaunchInfo.ue1Version >= 400)
		{
			if (PendingTouch())
			{
				CallEvent(PendingTouch(), EventName::PostTouch, { ExpressionValue::ObjectValue(this) });
				if (PendingTouch())
				{
					UActor* cur = PendingTouch();
					UActor* next = cur->PendingTouch();
					PendingTouch() = next;
					cur->PendingTouch() = nullptr;
				}
			}
		}

		if (parityPawn && parityPawn->HasActiveFallingParityRealizedLifecycle()
			&& !parityPawn->HasFallingParityRealizedContinuity())
		{
			parityPawn->FinishFallingParityRealizedTrace(
				PawnMovement::FallingParityRealizedOutcome::ContinuityLost);
		}
	}
}

void UActor::TickWalking(float elapsed)
{
	// Only pawns can walk!
	UPawn* pawn = UObject::TryCast<UPawn>(this);
	if (!pawn)
		return;

	if (Region().ZoneNumber == 0)
	{
		CallEvent(this, EventName::FellOutOfWorld);
		return;
	}

	// Save our starting point and state

	OldLocation() = Location();
	bJustTeleported() = false;

	// Update the actor velocity based on the acceleration and zone

	UZoneInfo* zone = Region().Zone;
	// UDecoration* decor = UObject::TryCast<UDecoration>(this);
	UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(this);

	Velocity().z = 0.0f;

	if (dot(Acceleration(), Acceleration()) > 0.0001f)
	{
		float accelRate = pawn->AccelRate();
		if (player && player->bIsWalking())
			accelRate *= 0.3f;

		// Acceleration must never exceed the acceleration rate
		float accelSpeed = length(Acceleration());
		vec3 accelDir = Acceleration() * (1.0f / accelSpeed);
		if (accelSpeed > accelRate)
			Acceleration() = accelDir * accelRate;

		float speed = length(Velocity());
		Velocity() = Velocity() - (Velocity() - accelDir * speed) * (zone->ZoneGroundFriction() * elapsed);
	}
	else
	{
		float speed = length(Velocity());
		if (speed > 0.0f)
		{
			float newSpeed = std::max(speed - speed * zone->ZoneGroundFriction() * 2.0f * elapsed, 0.0f);
			Velocity() = Velocity() * (newSpeed / speed);
		}
	}

	Velocity() = Velocity() + Acceleration() * elapsed;

	float maxSpeed = player ? player->GroundSpeed() : pawn->GroundSpeed() * pawn->DesiredSpeed();
	if (player && player->bIsWalking())
		maxSpeed *= 0.3f;

	float speed = length(Velocity());
	if (speed > 0.0f && speed > maxSpeed)
		Velocity() = Velocity() * (maxSpeed / speed);

	Velocity().z = 0.0f;

	// The classic step up, move and step down algorithm:

	float gravityDirection = zone->ZoneGravity().z > 0.0f ? 1.0f : -1.0f;
	vec3 stepUpDelta(0.0f, 0.0f, -gravityDirection * pawn->MaxStepHeight());
	vec3 stepDownDelta(0.0f, 0.0f, gravityDirection * pawn->MaxStepHeight() * stepDownDeltaFactor);

	// "Step up and move" as long as we have time left and only hitting surfaces with low enough slope that it could be walked
	float timeLeft = elapsed;
	vec3 vel = Velocity() + zone->ZoneVelocity() * elapsed * 25.0f;
	bool isMoving = HasHorizontalMovement(vel.x, vel.y);
	if (isMoving)
	{
		const bool walkingPreflightEnabled =
			engine->IsBotBenchmarkWalkingPreflightEnabled();
		const uint64_t preflightInvocation = walkingPreflightEnabled
			? pawn->BeginWalkingStepPreflightInvocation() : 0;
		for (int iteration = 0; timeLeft > 0.0f && iteration < 5; iteration++)
		{
			bool walkingHitWallDispatched = false;
			bool initialWalkingContactRecorded = false;
			const vec3 iterationStartLocation = Location();
			const float iterationStartTimeLeft = timeLeft;
			vec3 moveDelta = vel * timeLeft;
			if (walkingPreflightEnabled)
			{
				pawn->ObserveWalkingStepPreflightShadow(
					stepUpDelta, moveDelta, stepDownDelta, iteration, preflightInvocation);
			}

			// step up first so we can get past stairs going up
			TryMove(stepUpDelta);

			// try move forward
			CollisionHit hit = TryMove(moveDelta);
			timeLeft -= timeLeft * hit.Fraction;
			moveDelta = vel * timeLeft;

			// move back down to original vertical position
			TryMove(-stepUpDelta);

			if (hit.Fraction < FLT_EPSILON)
			{
				// try move forward once again, in case our head bumped into something while stepped up
				hit = TryMove(moveDelta);
				timeLeft -= timeLeft * hit.Fraction;
			}

			if (hit.Fraction < 1.0f)
			{
				const CollisionHit initialHit = hit;
				const vec3 velocityBeforeCollision = Velocity();
				const float minHitWallBeforeCallback = pawn->MinHitWall();
				const int physicsBeforeCallback = static_cast<int>(Physics());
				const auto initialBlockerBeforeCallback = ClassifyWalkingHitWallBlocker(pawn, initialHit);
				if (player && hit.Actor)
				{
					if (UObject::IsType<UDecoration>(hit.Actor) && UObject::Cast<UDecoration>(hit.Actor)->bPushable() && dot(hit.Normal, moveDelta) < -0.9f)
					{
						// We hit a pushable decoration that is facing our movement direction

						bJustTeleported() = true;
						vel = Velocity() = Velocity() * Mass() / (Mass() + hit.Actor->Mass());
						walkingHitWallDispatched = true;
						CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });
						timeLeft = 0.0f;
					}
					else if (hit.Actor->bCollideActors() && hit.Actor->CollisionHeight() > 0.0f && hit.Actor->CollisionRadius() > 0.0f)
					{
						// TODO: We hit a non-movable actor

					}
				}
				else if (hit.Normal.z < 0.2f && hit.Normal.z > -0.2f)
				{
					// We hit a wall
					walkingHitWallDispatched = true;
					CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });
					const bool fixtureContactLimitReached = pawn->RecordWalkingHitWallDispatch(initialHit,
						velocityBeforeCollision, minHitWallBeforeCallback,
						physicsBeforeCallback,
						PawnMovement::WalkingHitWallContactPhase::PrimaryForward,
						initialBlockerBeforeCallback, true);
					initialWalkingContactRecorded = true;
					if (fixtureContactLimitReached)
						return;

					vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
					if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
					{
						hit = TryMove(alignedDelta);
						timeLeft -= timeLeft * hit.Fraction;
						if (hit.Fraction < 1.0f)
						{
							const CollisionHit secondHit = hit;
							const vec3 secondVelocityBeforeCollision = Velocity();
							const float secondMinHitWallBeforeCallback = pawn->MinHitWall();
							const int secondPhysicsBeforeCallback = static_cast<int>(Physics());
							const auto secondBlockerBeforeCallback =
								ClassifyWalkingHitWallBlocker(pawn, secondHit);
							walkingHitWallDispatched = true;
							CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });
							const bool fixtureContactLimitReached = pawn->RecordWalkingHitWallDispatch(secondHit,
								secondVelocityBeforeCollision, secondMinHitWallBeforeCallback,
								secondPhysicsBeforeCallback,
								PawnMovement::WalkingHitWallContactPhase::AlignedSlide,
								secondBlockerBeforeCallback, true);
							if (fixtureContactLimitReached)
								return;
						}
					}
					else
					{
						timeLeft = 0.0f;
					}
				}
				if (!initialWalkingContactRecorded)
				{
					const bool fixtureContactLimitReached = pawn->RecordWalkingHitWallDispatch(initialHit,
						velocityBeforeCollision, minHitWallBeforeCallback,
						physicsBeforeCallback,
						PawnMovement::WalkingHitWallContactPhase::PrimaryForward,
						initialBlockerBeforeCallback,
						walkingHitWallDispatched);
					if (fixtureContactLimitReached)
						return;
				}
			}

			// Check if unrealscript got us out of walking mode
			if (Physics() != PHYS_Walking)
			{
				if (walkingPreflightEnabled && Physics() == PHYS_Falling)
				{
					pawn->ArmFallingParityRealizedTrace(iteration, preflightInvocation);
					pawn->QueueFallingHazardForecastSource(walkingHitWallDispatched
						? PawnMovement::FallingHazardForecastSource::PostWallDeflectionCommit
						: PawnMovement::FallingHazardForecastSource::CallbackReturnCommit);
				}
				return;
			}

			if (!MadeWalkingIterationProgress(
				iterationStartLocation.x, iterationStartLocation.y, iterationStartLocation.z,
				iterationStartTimeLeft, Location().x, Location().y, Location().z, timeLeft))
				break;

			// Can we reach the ground from here if we step down? (dry run)
			CollisionHit floorHit = TryMove(stepDownDelta, true);
			if (floorHit.Fraction == 1.0f || floorHit.Normal.z < 0.7071f)
			{
				// UE1 gives walking Pawns that may jump one script callback before
				// committing an unsupported step. Script can clear bCanJump to veto it.
				if (PawnMovement::ShouldDispatchMayFall(pawn->bCanJump()))
					CallEvent(pawn, EventName::MayFall);

				// Short-circuit property reads after Destroy: it removes the Pawn from
				// collision immediately, so no stale walking work may follow.
				const bool deleteMe = pawn->bDeleteMe();
				const bool callbackBeganFalling = !deleteMe
					&& pawn->Physics() == PHYS_Falling;
				if (walkingPreflightEnabled && callbackBeganFalling)
				{
					pawn->ArmFallingParityRealizedTrace(iteration, preflightInvocation);
					pawn->QueueFallingHazardForecastSource(
						PawnMovement::FallingHazardForecastSource::CallbackReturnCommit);
				}
				const bool stillWalking = !deleteMe && pawn->Physics() == PHYS_Walking;
				const bool canJump = stillWalking && pawn->bCanJump();
				const PawnMovement::LedgeTransition transition = PawnMovement::ResolveLedgeTransition(
					deleteMe, stillWalking, canJump);
				bool positiveDpsVetoAuthorized = false;
				if (walkingPreflightEnabled
					&& pawn->HasWalkingStepPreflightConfirmation(
						iteration, preflightInvocation))
				{
					positiveDpsVetoAuthorized = pawn->ConfirmWalkingStepPreflightShadow(
						iteration, preflightInvocation, transition);
				}
				if (transition == PawnMovement::LedgeTransition::Abort)
					return;
				bool painZoneVeto = false;
				bool restoreGrounded = transition == PawnMovement::LedgeTransition::RestoreGrounded;
				if (transition == PawnMovement::LedgeTransition::BeginFalling)
				{
					vec3 fallAcceleration = Acceleration();
					const float maxAirAcceleration = engine->LaunchInfo.ue1Version > 219
						? pawn->AirControl() * pawn->AccelRate() : 0.0f;
					const float fallAccelerationLength = length(fallAcceleration);
					if (fallAccelerationLength > maxAirAcceleration)
						fallAcceleration = normalize(fallAcceleration) * maxAirAcceleration;
					UZoneInfo* footZone = pawn->FootRegion().Zone;
					UZoneInfo* physicsZone = pawn->Region().Zone;
					const bool predictedHarmfulPainZone = PredictedFallEntersHarmfulPainZone(
						pawn, pawn->Velocity(), fallAcceleration);
					painZoneVeto = PawnMovement::ShouldVetoPainZoneLedge(
						IsAutonomousPlayerBot(pawn),
						physicsZone && physicsZone->ZoneGravity().z < 0.0f,
						footZone && footZone->bPainZone(), predictedHarmfulPainZone);
					restoreGrounded = painZoneVeto;
				}

				if (restoreGrounded)
				{
					vec2 unsafeDirection = (Location() - iterationStartLocation).xy();
					if (dot(unsafeDirection, unsafeDirection) <= 0.0001f)
						unsafeDirection = Acceleration().xy();
					if (positiveDpsVetoAuthorized && painZoneVeto)
					{
						PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord action;
						action.InvocationToken = preflightInvocation;
						action.WalkingIteration = iteration;
						action.Outcome = PawnMovement::WalkingStepPreflightPositiveDpsVetoOutcome::
							LegacyPainLedgeSuperseded;
						action.LegacyPainLedgeSuperseded = true;
						action.RollbackDelta = iterationStartLocation - Location();
						pawn->QueueWalkingStepPreflightPositiveDpsVetoAction(std::move(action));
					}
					// Backtrack along the movement we just completed so restoration is
					// collision checked rather than teleporting through new obstructions.
					TryMove(iterationStartLocation - Location());
					if (painZoneVeto)
						pawn->RecordPainLedgeVeto(Location(), unsafeDirection);
					Velocity() = vec3(0.0f);
					Acceleration() = vec3(0.0f);
					if (painZoneVeto)
						pawn->MoveTimer() = -1.0f;
					return;
				}

				// The stock pain-ledge guard owns its recovery/replan state. An opt-in
				// preflight action may augment only the cases that it declined; it must
				// never return early and suppress RecordPainLedgeVeto above.
				if (positiveDpsVetoAuthorized)
				{
					PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord action;
					action.InvocationToken = preflightInvocation;
					action.WalkingIteration = iteration;
					const vec3 rollbackDelta = iterationStartLocation - Location();
					action.RollbackDelta = rollbackDelta;
					action.RollbackTestAttempted = true;
					const CollisionHit rollbackTest = TryMove(rollbackDelta, true);
					action.RollbackTestFraction = rollbackTest.Fraction;
					const bool rollbackClear = rollbackTest.Fraction == 1.0f;
					if (rollbackClear)
					{
						action.RollbackActualAttempted = true;
						const CollisionHit rollbackActual = TryMove(rollbackDelta);
						action.RollbackActualFraction = rollbackActual.Fraction;
						if (rollbackActual.Fraction == 1.0f)
						{
						Velocity() = vec3(0.0f);
						Acceleration() = vec3(0.0f);
						pawn->MoveTimer() = -1.0f;
						pawn->RecordWalkingStepPreflightPositiveDpsVetoOutcome(true);
						action.Outcome = PawnMovement::WalkingStepPreflightPositiveDpsVetoOutcome::Applied;
						action.ForcedReplan = true;
						pawn->QueueWalkingStepPreflightPositiveDpsVetoAction(std::move(action));
						return;
						}
					}
					pawn->RecordWalkingStepPreflightPositiveDpsVetoOutcome(false);
					action.Outcome = rollbackClear
						? PawnMovement::WalkingStepPreflightPositiveDpsVetoOutcome::RollbackActualRejected
						: PawnMovement::WalkingStepPreflightPositiveDpsVetoOutcome::RollbackTestRejected;
					pawn->QueueWalkingStepPreflightPositiveDpsVetoAction(std::move(action));
				}

				SetPhysics(PHYS_Falling);
				if (walkingPreflightEnabled && Physics() == PHYS_Falling)
				{
					pawn->ArmFallingParityRealizedTrace(iteration, preflightInvocation);
					pawn->QueueFallingHazardForecastSource(
						PawnMovement::FallingHazardForecastSource::UnsupportedWalkCommit);
				}
				SetBase(nullptr, true);
				return;
			}

			// We could reach the ground. Step down there.
			floorHit = TryMove(stepDownDelta);
			if (floorHit.Fraction != 1.0f)
				SetBase(floorHit.Actor, true);
		}
	}
	else
	{
		// Can we reach the ground from here?
		CollisionHit floorHit = TryMove(stepDownDelta, true);
		if (floorHit.Fraction == 1.0f || floorHit.Normal.z < 0.7071f)
		{
			// No we couldn't. We are falling
			SetPhysics(PHYS_Falling);
			if (engine->IsBotBenchmarkWalkingPreflightEnabled()
				&& Physics() == PHYS_Falling)
			{
				pawn->QueueFallingHazardForecastSource(
					PawnMovement::FallingHazardForecastSource::UnsupportedWalkCommit);
			}
			SetBase(nullptr, true);
		}
	}

	if (!bJustTeleported())
		Velocity() = (Location() - OldLocation()) / elapsed;
	Velocity().z = 0.0f;
}

void UActor::TickFalling(float elapsed)
{
	if (Region().ZoneNumber == 0)
	{
		CallEvent(this, EventName::FellOutOfWorld);
		return;
	}

	UZoneInfo* zone = Region().Zone;
	UDecoration* decor = UObject::TryCast<UDecoration>(this);
	UPawn* pawn = UObject::TryCast<UPawn>(this);
	if (pawn)
		pawn->BeginHazardSwimEgressFallingTick();

	// UnrealScript property references
	vec3& acceleration = Acceleration();
	vec3& velocity = Velocity();
	vec3& oldLocation = OldLocation();
	vec3& location = Location();
	float groundSpeed = 0.0f;

	if (pawn)
	{
		groundSpeed = pawn->GroundSpeed();
		float maxAccel = engine->LaunchInfo.ue1Version > 219 ? pawn->AirControl() * pawn->AccelRate() : 0.0f;
		float accel = length(acceleration);
		if (accel > maxAccel)
			acceleration = normalize(acceleration) * maxAccel;
	}

	float gravityScale = 2.0f;
	float fluidFriction = 0.0f;

	if (decor && decor->bBobbing())
	{
		gravityScale = 1.0f;
	}
	else if (pawn && pawn->FootRegion().Zone->bWaterZone() && velocity.z < 0.0f)
	{
		fluidFriction = pawn->FootRegion().Zone->ZoneFluidFriction();
	}

	PawnMovement::FallingParityTransition realizedParityStep;
	bool observeRealizedParity = pawn
		&& engine->IsBotBenchmarkWalkingPreflightEnabled()
		&& pawn->HasActiveFallingParityRealizedModel();
	if (observeRealizedParity)
	{
		realizedParityStep = PawnMovement::BeginFallingParityStep({
			.Location = location,
			.Velocity = velocity
		}, {
			.Acceleration = acceleration,
			.Gravity = zone->ZoneGravity(),
			.ZoneVelocity = zone->ZoneVelocity(),
			.GroundSpeed = groundSpeed,
			.TerminalVelocity = zone->ZoneTerminalVelocity(),
			.Elapsed = elapsed,
			.WaterPhysics = pawn->FootRegion().Zone
				&& pawn->FootRegion().Zone->bWaterZone(),
			.Bounce = bBounce()
		});
		if (realizedParityStep.Kind
			== PawnMovement::FallingParityTransitionKind::Unknown)
		{
			PawnMovement::FallingParityRealizedRecord evidence;
			evidence.Elapsed = elapsed;
			pawn->RecordFallingParityRealizedStep(
				PawnMovement::FallingParityRealizedOutcome::Unknown, evidence);
			observeRealizedParity = false;
		}
	}

	OldLocation() = Location();
	bJustTeleported() = false;

	vec3 accelVector = acceleration * 1.5f;
	const vec3 gravityVector = gravityScale * zone->ZoneGravity();

	float timeLeft = elapsed;
	for (int iteration = 0; timeLeft > 0.0f && iteration < 8; iteration++)
	{
		const PawnMovement::FallingRetailPhysicsSlice physicsSlice =
			PawnMovement::SelectFallingRetailPhysicsSlice(timeLeft);
		if (!physicsSlice.Valid)
			break;
		const float timeTick = physicsSlice.Elapsed;
		timeLeft = physicsSlice.RemainingTime;
		if (pawn)
		{
			if (pawn->AdvanceFallingHazardRecovery(timeTick))
				accelVector = acceleration * 1.5f;
			pawn->EnsureFallingHazardGeneration(timeTick, acceleration);
		}
		const vec3 iterationOldVelocity = velocity;
		const float fluidFactor = 1.0f - fluidFriction * timeTick;
		vec3 newVelocity = iterationOldVelocity * fluidFactor
			+ (accelVector + gravityVector) * (0.5f * timeTick);

		// Limit air control to direction changes in the XY plane without
		// increasing speed beyond the current ground-speed envelope.
		const vec2 velocity2d = iterationOldVelocity.xy();
		const vec2 newVelocity2d = newVelocity.xy();
		const float curSpeedSquared = dot(velocity2d, velocity2d);
		if (pawn && curSpeedSquared >= groundSpeed * groundSpeed
			&& dot(newVelocity2d, newVelocity2d) > curSpeedSquared)
		{
			const float xySpeed = length(velocity2d);
			newVelocity = vec3(normalize(newVelocity2d) * xySpeed, newVelocity.z);
		}
		velocity = newVelocity;

		float zoneTerminalVelocity = zone->ZoneTerminalVelocity();
		if (dot(velocity, velocity) > zoneTerminalVelocity * zoneTerminalVelocity)
		{
			velocity = normalize(velocity) * zoneTerminalVelocity;
			newVelocity = velocity;
		}

		vec3 moveDelta = (newVelocity + zone->ZoneVelocity() * timeTick * 25.0f) * timeTick;
		float realizedVelocityError = 0.0f;
		float realizedDeltaError = 0.0f;
		if (observeRealizedParity)
		{
			realizedVelocityError = MaximumAbsoluteComponent(
				newVelocity - realizedParityStep.State.Velocity);
			realizedDeltaError = MaximumAbsoluteComponent(
				moveDelta - realizedParityStep.DirectDelta);
		}

		const vec3 iterationStartLocation = location;
		if (pawn)
			pawn->CaptureHazardSwimEgressFallingAnchorBeforePhysicsMove();
		MoveCallbackEvidence realizedMoveCallbacks;
		const bool observeFallingHazard = pawn
			&& pawn->PrepareFallingHazardSweep(
				PawnMovement::FallingHazardSweepLeg::Direct,
				iterationStartLocation, moveDelta, timeTick);
		CollisionHit hit = TryMove(moveDelta, false, true,
			(observeRealizedParity || observeFallingHazard)
				? &realizedMoveCallbacks : nullptr);
		if (pawn && Physics() == PHYS_Swimming)
			pawn->ObserveHazardSwimEgressAfterPhysicsMove();
		bool realizedCallbackBarrier = false;
		if (observeRealizedParity && pawn && !pawn->bDeleteMe())
		{
			PawnMovement::FallingParityRealizedRecord evidence;
			evidence.Elapsed = elapsed;
			evidence.Collision = ClassifyFallingParityCollision(pawn, hit);
			evidence.HitFraction = hit.Fraction;
			evidence.HitNormal = hit.Normal;
			evidence.VelocityError = realizedVelocityError;
			evidence.RequestedDeltaError = realizedDeltaError;
			if (std::isfinite(hit.Fraction))
			{
				const vec3 expectedEndpoint = iterationStartLocation
					+ moveDelta * hit.Fraction;
				evidence.EndpointError = MaximumAbsoluteComponent(
					location - expectedEndpoint);
			}
			else
			{
				evidence.EndpointError = std::numeric_limits<float>::infinity();
			}
			const PawnMovement::FallingParityTransition directResult =
				PawnMovement::ResolveFallingParityDirectSweep(realizedParityStep, {
					.Collision = evidence.Collision,
					.Fraction = hit.Fraction,
					.Normal = hit.Normal
				});
			const bool hitWallCallback = hit.Fraction < 1.0f
				&& !(hit.Actor && hit.Actor->IsA("Pawn"))
				&& (bBounce() || hit.Normal.z <= fallingWalkableNormalZ);
			if (hitWallCallback)
				realizedMoveCallbacks.Mask |= MoveCallbackHitWall;
			evidence.CallbackBarrierMask = realizedMoveCallbacks.Mask;
			PawnMovement::FallingParityRealizedOutcome outcome =
				PawnMovement::FallingParityRealizedOutcome::Unknown;
			if (realizedMoveCallbacks.Any())
			{
				outcome = PawnMovement::FallingParityRealizedOutcome::CallbackBarrier;
				realizedCallbackBarrier = true;
			}
			else if (directResult.Kind
				== PawnMovement::FallingParityTransitionKind::Continue
				|| directResult.Kind
					== PawnMovement::FallingParityTransitionKind::Landed)
			{
				const float tolerance = 0.001f;
				const bool matched = evidence.VelocityError <= tolerance
					&& evidence.RequestedDeltaError <= tolerance
					&& evidence.EndpointError <= tolerance;
				outcome = !matched
					? PawnMovement::FallingParityRealizedOutcome::Mismatch
					: directResult.Kind
						== PawnMovement::FallingParityTransitionKind::Landed
						? PawnMovement::FallingParityRealizedOutcome::MatchedLanding
						: PawnMovement::FallingParityRealizedOutcome::MatchedClear;
			}
			pawn->RecordFallingParityRealizedStep(outcome, evidence);
		}
		if (realizedCallbackBarrier && pawn
			&& !pawn->HasFallingParityRealizedContinuity())
		{
			pawn->FinishFallingParityRealizedTrace(
				PawnMovement::FallingParityRealizedOutcome::ContinuityLost);
		}
		if (pawn && !pawn->bDeleteMe()
			&& engine->IsBotBenchmarkWalkingPreflightEnabled())
			pawn->ObserveFallingParityRealizedPain();
		auto callFallingHitWall = [&](const CollisionHit& wallHit)
			-> std::optional<PawnMovement::FallingHazardForecastContinuationSeed>
		{
			if (wallHit.Actor && wallHit.Actor->IsA("Pawn"))
				return std::nullopt;
			if (pawn)
				pawn->CaptureFallingHazardAlignedCommandWitness(!wallHit.Actor);
			auto continuation = pawn
				? pawn->FinishFallingHazardCallbackBoundary() : std::nullopt;
			CallEvent(this, EventName::HitWall, {
				ExpressionValue::VectorValue(wallHit.Normal),
				ExpressionValue::ObjectValue(wallHit.Actor ? wallHit.Actor : Level())
			});
			if (pawn)
				pawn->RecoverFallingHazardCallbackReturn();
			if (pawn && pawn->HasActiveFallingParityRealizedLifecycle()
				&& !pawn->HasFallingParityRealizedContinuity())
			{
				pawn->FinishFallingParityRealizedTrace(
					PawnMovement::FallingParityRealizedOutcome::ContinuityLost);
			}
			return continuation;
		};

		if (hit.Fraction < 1.0f)
		{
			// Hit the level
			if (bBounce())
			{
				callFallingHitWall(hit);
				vec3 reflectedDelta = reflect(moveDelta, hit.Normal);
				if (pawn)
					pawn->CaptureHazardSwimEgressFallingAnchorBeforePhysicsMove(false);
				hit = TryMove(reflectedDelta);
				if (pawn && Physics() == PHYS_Swimming)
					pawn->ObserveHazardSwimEgressAfterPhysicsMove();
				if (realizedCallbackBarrier && pawn
					&& !pawn->HasFallingParityRealizedContinuity())
				{
					pawn->FinishFallingParityRealizedTrace(
						PawnMovement::FallingParityRealizedOutcome::ContinuityLost);
				}
			}
			else
			{
				if (hit.Normal.z <= fallingWalkableNormalZ)
				{
					auto alignedHazardContinuation = callFallingHitWall(hit);
					// We hit a slope. Try to follow it.
					vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
					if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
					{
						const CollisionHit firstHit = hit;
						if (pawn && alignedHazardContinuation)
						{
							pawn->ArmFallingHazardContinuation(
								PawnMovement::FallingHazardForecastSource::AlignedContinuationCommit,
								*alignedHazardContinuation, timeTick, acceleration);
						}
						MoveCallbackEvidence alignedMoveCallbacks;
						const bool observeAlignedHazard = pawn
							&& pawn->PrepareFallingHazardSweep(
								PawnMovement::FallingHazardSweepLeg::Aligned,
								location, alignedDelta, 0.0f);
						if (pawn)
							pawn->CaptureHazardSwimEgressFallingAnchorBeforePhysicsMove(false);
						hit = TryMove(alignedDelta, false, true,
							observeAlignedHazard ? &alignedMoveCallbacks : nullptr);
						if (pawn && Physics() == PHYS_Swimming)
							pawn->ObserveHazardSwimEgressAfterPhysicsMove();
						if (pawn && !pawn->bDeleteMe()
							&& engine->IsBotBenchmarkWalkingPreflightEnabled())
							pawn->ObserveFallingParityRealizedPain();
						if (realizedCallbackBarrier && pawn
							&& !pawn->HasFallingParityRealizedContinuity())
						{
							pawn->FinishFallingParityRealizedTrace(
								PawnMovement::FallingParityRealizedOutcome::ContinuityLost);
						}
						if (pawn && !firstHit.Actor && !hit.Actor && hit.Fraction < 1.0f
							&& hit.Normal.z <= fallingWalkableNormalZ)
						{
							const vec3 zoneGravity = zone->ZoneGravity();
							pawn->ObserveFallingSeamEscapeShadow(alignedDelta,
								location - iterationStartLocation, firstHit.Normal, hit.Normal,
								zoneGravity.x == 0.0f && zoneGravity.y == 0.0f
									&& zoneGravity.z < 0.0f);
						}
						if (hit.Fraction < 1.0f && hit.Normal.z > fallingWalkableNormalZ)
						{
							if (pawn)
								pawn->FinishFallingHazardLanding(hit);
							PhysLanded(hit.Actor, hit.Normal);
							if (pawn && engine->IsBotBenchmarkWalkingPreflightEnabled()
								&& pawn->HasActiveFallingParityRealizedLifecycle())
							{
								pawn->FinishFallingParityRealizedTrace(
									PawnMovement::FallingParityRealizedOutcome::Landed);
							}
							return;
						}
						if (hit.Fraction < 1.0f)
						{
							auto thirdHazardContinuation = callFallingHitWall(hit);
							const PawnMovement::FallingTwoWallAdjustment adjustment =
								PawnMovement::BuildFallingTwoWallAdjustment(normalize(moveDelta),
									alignedDelta, hit.Normal, firstHit.Normal, hit.Fraction);
							const vec3 adjustedDelta = adjustment.Delta;

							if (pawn && thirdHazardContinuation)
							{
								pawn->ArmFallingHazardContinuation(
									PawnMovement::FallingHazardForecastSource::ThirdMoveContinuationCommit,
									*thirdHazardContinuation, timeTick, acceleration);
							}
							MoveCallbackEvidence adjustedMoveCallbacks;
							const bool observeAdjustedHazard = pawn
								&& pawn->PrepareFallingHazardSweep(
									PawnMovement::FallingHazardSweepLeg::TwoWallAdjusted,
									location, adjustedDelta, 0.0f);
							if (pawn)
								pawn->CaptureHazardSwimEgressFallingAnchorBeforePhysicsMove(false);
							hit = TryMove(adjustedDelta, false, true,
								observeAdjustedHazard ? &adjustedMoveCallbacks : nullptr);
							if (pawn && Physics() == PHYS_Swimming)
								pawn->ObserveHazardSwimEgressAfterPhysicsMove();
							if (pawn && !pawn->bDeleteMe()
								&& engine->IsBotBenchmarkWalkingPreflightEnabled())
								pawn->ObserveFallingParityRealizedPain();
							if (realizedCallbackBarrier && pawn
								&& !pawn->HasFallingParityRealizedContinuity())
							{
								pawn->FinishFallingParityRealizedTrace(
									PawnMovement::FallingParityRealizedOutcome::ContinuityLost);
							}
							if (adjustment.Ditch || hit.Normal.z > fallingWalkableNormalZ)
							{
								if (pawn)
									pawn->FinishFallingHazardLanding(
										hit, adjustment.Ditch);
								PhysLanded(hit.Actor, hit.Normal);
								if (pawn && engine->IsBotBenchmarkWalkingPreflightEnabled()
									&& pawn->HasActiveFallingParityRealizedLifecycle())
								{
									pawn->FinishFallingParityRealizedTrace(
										PawnMovement::FallingParityRealizedOutcome::Landed);
								}
								return;
							}
						}
					}

					// Retail reconstructs horizontal velocity from realized travel while
					// restoring the iteration-start vertical velocity for the remaining pass.
					if (!bBounce() && !bJustTeleported())
					{
						velocity = PawnMovement::ReconstructFallingCollisionVelocity(
							iterationStartLocation, location, timeTick,
							iterationOldVelocity.z);
					}
				}
				else
				{
					if (pawn)
						pawn->FinishFallingHazardLanding(hit);
					PhysLanded(hit.Actor, hit.Normal);
					if (pawn && engine->IsBotBenchmarkWalkingPreflightEnabled()
						&& pawn->HasActiveFallingParityRealizedLifecycle())
					{
						pawn->FinishFallingParityRealizedTrace(
							PawnMovement::FallingParityRealizedOutcome::Landed);
					}
					timeLeft = 0.0f;
				}
			}
		}
	}
}

void UActor::TickSwimming(float elapsed)
{
	// Only pawns can swim!
	UPawn* pawn = UObject::TryCast<UPawn>(this);

	if (!pawn)
		return;
	pawn->ObserveHazardSwimEgressAfterPhysicsMove();
	pawn->CaptureHazardSwimEgressAnchorBeforePhysicsMove();
	pawn->AdvanceHazardSwimEgressLiveSteer();

	if (Region().ZoneNumber == 0)
	{
		CallEvent(this, EventName::FellOutOfWorld);
		return;
	}

	// Save our starting point and state
	OldLocation() = Location();
	bJustTeleported() = false;

	// Update the actor velocity based on the acceleration and zone

	UZoneInfo* zone = Region().Zone;
	// UDecoration* decor = UObject::TryCast<UDecoration>(this);
	UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(this);

	if (dot(Acceleration(), Acceleration()) > 0.0001f)
	{
		float accelRate = pawn->AccelRate() * 0.3f;

		// Acceleration must never exceed the acceleration rate
		float accelSpeed = length(Acceleration());
		vec3 accelDir = Acceleration() * (1.0f / accelSpeed);
		if (accelSpeed > accelRate)
			Acceleration() = accelDir * accelRate;

		float speed = length(Velocity());
		Velocity() = Velocity() - (Velocity() - accelDir * speed) * (zone->ZoneFluidFriction() * elapsed);
	}
	else
	{
		float speed = length(Velocity());
		if (speed > 0.0f)
		{
			float newSpeed = std::max(speed - speed * zone->ZoneFluidFriction() * 2.0f * elapsed, 0.0f);
			Velocity() = Velocity() * (newSpeed / speed);
		}
	}

	Velocity() = Velocity() + Acceleration() * elapsed;

	float maxSpeed = player ? player->WaterSpeed() : pawn->WaterSpeed() * pawn->DesiredSpeed();

	float speed = length(Velocity());
	if (speed > 0.0f && speed > maxSpeed)
		Velocity() = Velocity() * (maxSpeed / speed);

	//float gravityDirection = zone->ZoneGravity().z > 0.0f ? 1.0f : -1.0f;

	float timeLeft = elapsed;
	vec3 vel = Velocity() + zone->ZoneVelocity() * elapsed * 25.0f;
	bool isMoving = HasSpatialMovement(vel.x, vel.y, vel.z);
	if (isMoving)
	{
		for (int iteration = 0; timeLeft > 0.0f && iteration < 5; iteration++)
		{
			vec3 moveDelta = vel * timeLeft;

			CollisionHit hit = TryMove(moveDelta);
			timeLeft -= timeLeft * hit.Fraction;
			moveDelta = vel * timeLeft;

			if (hit.Fraction < 1.0f)
			{
				if (player && UObject::IsType<UDecoration>(hit.Actor) && UObject::Cast<UDecoration>(hit.Actor)->bPushable() && dot(hit.Normal, moveDelta) < -0.9f)
				{
					// We hit a pushable decoration that is facing our movement direction

					bJustTeleported() = true;
					Velocity() = Velocity() * Mass() / (Mass() + hit.Actor->Mass());
					CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });
					timeLeft = 0.0f;
				}
				else
				{
					// We hit a wall

					CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });

					vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
					if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
					{
						hit = TryMove(alignedDelta);
						timeLeft -= timeLeft * hit.Fraction;
						if (hit.Fraction < 1.0f)
						{
							CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });
						}
					}
					else
					{
						timeLeft = 0.0f;
					}
				}
			}
		}
	}

	if (!bJustTeleported())
		Velocity() = (Location() - OldLocation()) / elapsed;

	pawn->ObserveHazardSwimEgressAfterPhysicsMove();

	if (!Region().Zone->bWaterZone())
	{
		// We moved out of water.
		// Give the player a push
		if (Velocity().z > 0.0f)
			Velocity().z = std::max(Velocity().z, (100.0f + length(Velocity().xy())) * 0.5f);
		if (Physics() == PHYS_Swimming)
			SetPhysics(PHYS_Falling);
	}
}

void UActor::TickFlying(float elapsed)
{
	// Only pawns can fly!
	UPawn* pawn = UObject::TryCast<UPawn>(this);
	if (!pawn)
		return;
	pawn->EndHazardSwimEgressSwimSession();

	if (Region().ZoneNumber == 0)
	{
		CallEvent(this, EventName::FellOutOfWorld);
		return;
	}

	// Save our starting point and state

	OldLocation() = Location();
	bJustTeleported() = false;

	// Update the actor velocity based on the acceleration and zone

	UZoneInfo* zone = Region().Zone;
	UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(this);

	if (dot(Acceleration(), Acceleration()) > 0.0001f)
	{
		float accelRate = pawn->AccelRate();

		// Acceleration must never exceed the acceleration rate
		float accelSpeed = length(Acceleration());
		vec3 accelDir = Acceleration() * (1.0f / accelSpeed);
		if (accelSpeed > accelRate)
			Acceleration() = accelDir * accelRate;

		float speed = length(Velocity());
		Velocity() = Velocity() - (Velocity() - accelDir * speed) * (zone->ZoneFluidFriction() * elapsed);
	}
	else
	{
		float speed = length(Velocity());
		if (speed > 0.0f)
		{
			float newSpeed = std::max(speed - speed * zone->ZoneFluidFriction() * 2.0f * elapsed, 0.0f);
			Velocity() = Velocity() * (newSpeed / speed);
		}
	}

	Velocity() = Velocity() + Acceleration() * elapsed;

	float maxSpeed = player ? player->AirSpeed() : pawn->AirSpeed() * pawn->DesiredSpeed();

	float speed = length(Velocity());
	if (speed > 0.0f && speed > maxSpeed)
		Velocity() = Velocity() * (maxSpeed / speed);

	float timeLeft = elapsed;
	vec3 vel = Velocity() + zone->ZoneVelocity() * elapsed * 25.0f;
	bool isMoving = HasSpatialMovement(vel.x, vel.y, vel.z);
	if (isMoving)
	{
		for (int iteration = 0; timeLeft > 0.0f && iteration < 5; iteration++)
		{
			vec3 moveDelta = vel * timeLeft;

			CollisionHit hit = TryMove(moveDelta);
			timeLeft -= timeLeft * hit.Fraction;
			moveDelta = vel * timeLeft;

			if (hit.Fraction < 1.0f)
			{
				// We hit a wall
				CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });

				vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
				if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
				{
					hit = TryMove(alignedDelta);
					timeLeft -= timeLeft * hit.Fraction;
					if (hit.Fraction < 1.0f)
					{
						CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });
					}
				}
				else
				{
					timeLeft = 0.0f;
				}
			}
		}
	}

	if (!bJustTeleported())
		Velocity() = (Location() - OldLocation()) / elapsed;
	Velocity().z = 0.0f;
}

void UActor::TickRotating(float elapsed)
{
	if (bRotateToDesired())
	{
		if (Rotation() != DesiredRotation())
		{
			Rotator rot = Rotation();
			if (bFixedRotationDir())
			{
				rot.Yaw = Rotator::TurnToFixed(rot.Yaw, DesiredRotation().Yaw, (int)(RotationRate().Yaw * elapsed));
				rot.Pitch = Rotator::TurnToFixed(rot.Pitch, DesiredRotation().Pitch, (int)(RotationRate().Pitch * elapsed));
				rot.Roll = Rotator::TurnToFixed(rot.Roll, DesiredRotation().Roll, (int)(RotationRate().Roll * elapsed));
			}
			else
			{
				rot.Yaw = Rotator::TurnToShortest(rot.Yaw, DesiredRotation().Yaw, (int)std::abs(RotationRate().Yaw * elapsed));
				rot.Pitch = Rotator::TurnToShortest(rot.Pitch, DesiredRotation().Pitch, (int)std::abs(RotationRate().Pitch * elapsed));
				rot.Roll = Rotator::TurnToShortest(rot.Roll, DesiredRotation().Roll, (int)std::abs(RotationRate().Roll * elapsed));
			}
			Rotation() = rot;

			if (Rotation() == DesiredRotation())
			{
				CallEvent(this, EventName::EndedRotation);
			}
		}
	}
	else if (bFixedRotationDir())
	{
		Rotation() += RotationRate() * elapsed;
	}
}

void UActor::TickProjectile(float elapsed)
{
	if (Region().ZoneNumber == 0)
	{
		Destroy();
		return;
	}

	UZoneInfo* zone = Region().Zone;
	UProjectile* projectile = UObject::TryCast<UProjectile>(this);
	UPawn* pawn = UObject::TryCast<UPawn>(this);

	if (zone->bWaterZone())
		Velocity() = Velocity() * std::max(1.0f - zone->ZoneFluidFriction() * 0.2f * elapsed, 0.0f);

	Velocity() = Velocity() + Acceleration() * elapsed;

	if (projectile)
	{
		float maxSpeed = projectile->MaxSpeed();
		if (dot(Velocity(), Velocity()) > maxSpeed * maxSpeed)
		{
			Velocity() = normalize(Velocity()) * maxSpeed;
		}
	}

	OldLocation() = Location();
	bJustTeleported() = false;

	CollisionHit hit = TryMove(Velocity() * elapsed);

	if (hit.Fraction < 1.0f && !hit.Actor && !bDeleteMe() && !bJustTeleported())
	{
		CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });
	}

	if (!bBounce() && !bJustTeleported())
		Velocity() = (Location() - OldLocation()) / elapsed;
}

void UActor::TickRolling(float elapsed)
{
	if (Region().ZoneNumber == 0)
	{
		CallEvent(this, EventName::FellOutOfWorld);
		return;
	}

	// Save our starting point and state

	OldLocation() = Location();
	bJustTeleported() = false;

	// Update the actor velocity based on the acceleration and zone

	UZoneInfo* zone = Region().Zone;

	float speed = length(Velocity());
	Velocity() = Velocity() - speed * (normalize(Velocity()) - normalize(Acceleration())) * zone->ZoneGroundFriction() * elapsed;
	Velocity() = Velocity() * (1.0f - zone->ZoneFluidFriction() * elapsed) + Acceleration() * elapsed;


	vec3 moveDelta = (Velocity() + zone->ZoneVelocity() * elapsed * 25.0f) * elapsed;
	CollisionHit hit = TryMove(moveDelta);

	if (hit.Fraction < 1.0f && hit.Normal.z < 0.7071f)
	{
		CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hit.Normal), ExpressionValue::ObjectValue(hit.Actor ? hit.Actor : Level()) });

		vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
		if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
			TryMove(alignedDelta);
	}

	if (Physics() != PHYS_Rolling)
		return;

	float gravityDirection = zone->ZoneGravity().z > 0.0f ? 1.0f : -1.0f;
	constexpr float stepHeightRatio = 25.0f / 47.5f; // not sure what this should be. Appears that humans have a step height of 25.0 and collision height of 47.5, so I guess I'll use that ratio
	vec3 stepDownDelta(0.0f, 0.0f, gravityDirection * stepHeightRatio * CollisionHeight() * stepDownDeltaFactor);

	// Can we reach the ground from here?
	CollisionHit floorHit = TryMove(stepDownDelta, true);
	if (floorHit.Fraction == 1.0f || floorHit.Normal.z < 0.7071f)
	{
		// No we couldn't. We are falling
		SetPhysics(PHYS_Falling);
		SetBase(nullptr, true);
	}
	else
	{
		// We could reach the ground. Step down there.
		floorHit = TryMove(stepDownDelta);
		if (floorHit.Fraction != 1.0f)
			SetBase(floorHit.Actor, true);
	}

	if (!bJustTeleported())
		Velocity() = (Location() - OldLocation()) / elapsed;
}

void UActor::TickInterpolating(float elapsed)
{
	OldLocation() = Location();

	float timeLeft = elapsed;
	while (timeLeft > 0.0f)
	{
		if (PhysRate() == 0.0f || !bInterpolating())
			break;

		UInterpolationPoint* target = UObject::Cast<UInterpolationPoint>(Target());
		UInterpolationPoint* next = target ? target->Next() : nullptr;
		if (!target || !next)
			break;

		float physAlpha = PhysAlpha();

		if (auto pawn = UObject::TryCast<UPlayerPawn>(this))
		{
			if (engine->LaunchInfo.ue1Version > 219)
			{
				pawn->DesiredFlashScale() = mix(target->ScreenFlashScale(), next->ScreenFlashScale(), physAlpha);
				pawn->DesiredFlashFog() = mix(target->ScreenFlashFog(), next->ScreenFlashFog(), physAlpha);
				pawn->FovAngle() = mix(target->FovModifier(), next->FovModifier(), physAlpha) * Class->GetDefaultObject<UPlayerPawn>()->FovAngle();
				pawn->FlashScale() = vec3(pawn->DesiredFlashScale());
				pawn->FlashFog() = pawn->DesiredFlashFog();
			}
		}

		if (engine->LaunchInfo.ue1Version > 219)
			Level()->TimeDilation() = mix(target->GameSpeedModifier(), next->GameSpeedModifier(), physAlpha);

		float rateModifier = mix(target->RateModifier(), next->RateModifier(), physAlpha);
		float physRate = PhysRate() * rateModifier;
		if (physRate == 0.0f)
			break;

		bool interpolateStart = false, interpolateEnd = false;
		physAlpha += physRate * timeLeft;
		if (physRate < 0.0f && physAlpha < 0.0f)
		{
			timeLeft = physAlpha / physRate;
			physAlpha = 0.0f;
			interpolateStart = true;
		}
		else if (physRate > 0.0f && physAlpha > 1.0f)
		{
			timeLeft = (physAlpha - 1.0f) / physRate;
			physAlpha = 1.0f;
			interpolateEnd = true;
		}
		else
		{
			timeLeft = 0.0f;
		}

		UInterpolationPoint* prev = target->Prev();
		UInterpolationPoint* nextnext = next->Next();
		vec3 location;
		Rotator rotation;
		if (prev && nextnext)
		{
			location = spline(prev->Location(), target->Location(), next->Location(), nextnext->Location(), physAlpha);
			rotation = spline(prev->Rotation(), target->Rotation(), next->Rotation(), nextnext->Rotation(), physAlpha);
		}
		else
		{
			location = mix(target->Location(), next->Location(), physAlpha);
			rotation = mix(target->Rotation(), next->Rotation(), physAlpha);
		}

		PhysAlpha() = physAlpha;
		TryMove(location - Location());
		SetRotation(rotation);

		if (auto pawn = UObject::TryCast<UPawn>(this))
		{
			pawn->ViewRotation() = Rotation();
		}

		if (interpolateStart)
		{
			CallEvent(target, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(this) });
			CallEvent(this, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(target) });

			target = target->Prev();
			if (engine->LaunchInfo.ue1Version > 219)
			{
				while (target && target->bSkipNextPath())
					target = target->Prev();
			}

			Target() = target;
			PhysAlpha() = 1.0f;
		}
		else if (interpolateEnd)
		{
			CallEvent(target, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(this) });
			CallEvent(this, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(target) });

			target = target->Next();
			if (engine->LaunchInfo.ue1Version > 219)
			{
				while (target && target->bSkipNextPath())
					target = target->Next();
			}

			Target() = target;
			PhysAlpha() = 0.0f;
		}
	}

	if (elapsed > 0.0f)
		Velocity() = (Location() - OldLocation()) / elapsed;
}

void UActor::TickMovingBrush(float elapsed)
{
	OldLocation() = Location();

	UMover* mover = UObject::TryCast<UMover>(this);
	if (mover)
	{
		float timeLeft = elapsed;
		while (timeLeft > 0.0f)
		{
			if (!bInterpolating())
				break;

			if (PhysRate() <= 0.0f)
				break;

			float physAlpha = PhysAlpha();
			float physRate = PhysRate();

			physAlpha += physRate * timeLeft;
			if (physAlpha > 1.0f)
			{
				timeLeft = (physAlpha - 1.0f) / physRate;
				physAlpha = 1.0f;
			}
			else
			{
				timeLeft = 0.0f;
			}

			float t = physAlpha;
			if (mover->MoverGlideType() == 1/*MV_GlideByTime*/)
				t = smoothstep(0.0f, 1.0f, t);

			int keyIndex = clamp((int)mover->KeyNum(), 0, 7);
			vec3 oldpos = mover->OldPos();
			vec3 basepos = mover->BasePos();
			vec3 keypos = mover->KeyPos()[keyIndex];
			Rotator oldrot = mover->OldRot();
			Rotator baserot = mover->BaseRot();
			Rotator keyrot = mover->KeyRot()[keyIndex];

			vec3 deltapos = basepos + keypos - oldpos;
			vec3 targetPos = oldpos + deltapos * t;

			Rotator targetRotation = oldrot + (baserot + keyrot - oldrot) * t;

			// LogMessage("Moving brush: " + std::to_string(t) + " key=" + std::to_string(keyIndex) +" keypos=(" + std::to_string(keypos.x) + "," + std::to_string(keypos.y) + "," + std::to_string(keypos.z) + ")");

			if (TryMove(targetPos - Location()).Fraction == 1.0f)
			{
				SetRotation(targetRotation);
				PhysAlpha() = physAlpha;

				if (physAlpha == 1.0f)
				{
					bInterpolating() = false;
					CallEvent(this, EventName::InterpolateEnd, { ExpressionValue::ObjectValue(nullptr) });
				}
			}
		}
	}

	if (elapsed > 0.0f)
		Velocity() = (Location() - OldLocation()) / elapsed;
}

void UActor::TickSpider(float elapsed)
{
}

void UActor::TickTrailer(float elapsed)
{
	if (!Owner())
		return;

	vec3 newLocation = Owner()->Location();

	if (engine->LaunchInfo.ue1Version >= 400 && bTrailerPrePivot())
	{
		newLocation += PrePivot();
	}

	SetLocation(newLocation);

	if ((engine->LaunchInfo.ue1Version < 400 || bTrailerSameRotation()) && DrawType() != DT_Sprite)
	{
		SetRotation(Owner()->Rotation());
	}
}

void UActor::PhysLanded(UActor* hitActor, const vec3& hitNormal)
{
	// landed on the floor
	CallEvent(this, EventName::Landed, { ExpressionValue::VectorValue(hitNormal) });

	if (Physics() == PHYS_Falling) // Landed event might have changed the physics mode
	{
		if (UObject::TryCast<UPawn>(this))
		{
			SetPhysics(PHYS_Walking);
			SetBase(hitActor, true);
		}
		else
		{
			SetPhysics(PHYS_None);
			SetBase(hitActor, true);
			Velocity() = vec3(0.0f);
		}
	}
}

void UActor::SetPhysics(uint8_t newPhysics)
{
	Physics() = newPhysics;
}

void UActor::SetCollision(bool newColActors, bool newBlockActors, bool newBlockPlayers)
{
	XLevel()->Collision.RemoveFromCollision(this);
	bCollideActors() = newColActors;
	bBlockActors() = newBlockActors;
	bBlockPlayers() = newBlockPlayers;
	XLevel()->Collision.AddToCollision(this);
}

bool UActor::SetLocation(const vec3& newLocation)
{
	auto result = CheckLocation(newLocation, CollisionRadius(), CollisionHeight(), bCollideWorld() || bCollideWhenPlacing());
	if (!result.first)
		return false;

	XLevel()->Collision.RemoveFromCollision(this);
	XLevel()->Light.RemoveLight(this);
	Location() = result.second;
	XLevel()->Collision.AddToCollision(this);
	XLevel()->Light.AddLight(this);

	if (Level()->bBegunPlay())
	{
		// Send touch notifications for anything at the new location
		for (UActor* actor : XLevel()->Collision.CollidingActors(Location(), CollisionHeight(), CollisionRadius()))
		{
			if (actor != this && !actor->IsBasedOn(this) && !IsBasedOn(actor) && bCollideActors() && actor->bCollideActors())
			{
				Touch(actor);
			}
		}

		// Untouch everything we aren't overlapping anymore
		if (engine->LaunchInfo.IsUnrealTournament_469())
		{
			for (const auto actor : Touching_UT469())
			{
				if (actor && !IsOverlapping(actor))
					UnTouch(actor);
			}
		}
		else
		{
			for (const auto actor : Touching())
			{
				if (actor && !IsOverlapping(actor))
					UnTouch(actor);
			}
		}
	}

	return true;
}

bool UActor::SetRotation(const Rotator& newRotation)
{
	// To do: return false if there isn't room

	Rotator delta = newRotation - Rotation();
	Rotation() = newRotation;
	TurnBasedActors(delta);
	return true;
}

// carried items and actors on movers should rotate with the actor their based on
void UActor::TurnBasedActors(const Rotator& deltaRotation)
{
	if ((deltaRotation.Yaw & 0xffff) == 0)
		return;
	Coords yawRot = Coords::YawRotation(deltaRotation.YawRadians());
	vec3 baseLoc = Location();
	for (size_t i = 0; i < BasedActors.size(); )
	{
		UActor* basedActor = BasedActors[i];
		if (!basedActor) { i++; continue; }
		vec3 basedLoc = basedActor->Location();
		vec3 rotatedOffset = yawRot * (basedLoc - baseLoc);
		basedActor->TryMove((baseLoc + rotatedOffset) - basedLoc, false, false);
		basedActor->SetRotation(basedActor->Rotation() + deltaRotation);
		if (UPawn* pawn = UObject::TryCast<UPawn>(basedActor))
			pawn->ViewRotation().Yaw += deltaRotation.Yaw;
		// UnrealScript events triggered in TryMove can call methods such as SetBase or Destroy, so need to guard while iterating.
		if (i < BasedActors.size() && BasedActors[i] == basedActor)
			i++;
	}
}

bool UActor::SetCollisionSize(float newRadius, float newHeight)
{
	// To do: return false if there isn't room

	XLevel()->Collision.RemoveFromCollision(this);
	CollisionRadius() = newRadius;
	CollisionHeight() = newHeight;
	XLevel()->Collision.AddToCollision(this);
	return true;
}

UObject* UActor::Trace(vec3& hitLocation, vec3& hitNormal, const vec3& traceEnd, const vec3& traceStart, bool bTraceActors, const vec3& extent)
{
	TraceFlags flags;
	flags.movers = true;
	flags.world = true;
	if (bTraceActors)
	{
		flags.pawns = true;
		flags.others = true;
		flags.onlyProjectiles = true;
	}

	// hack?
	if (IsA("ChallengeHUD"))
	{
		flags.zoneChanges = true;
	}

	CollisionHit hit = XLevel()->Collision.TraceFirstHit(traceStart, traceEnd, this, extent, flags);
	hitNormal = hit.Normal;
	hitLocation = traceStart + (traceEnd - traceStart) * hit.Fraction;
	return hit.Actor;
}

UObject* UActor::Trace(vec3& hitLocation, vec3& hitNormal, const vec3& traceEnd, const vec3& traceStart, bool bTraceActors, const vec3& extent, bool bTraceBSP, uint8_t BSPTraceFlags)
{
	LogUnimplemented("Actor.Trace() [U227 - BSPTraceFlags parameter isn't implemented");
	TraceFlags flags;
	flags.movers = true;
	flags.world = bTraceBSP;
	if (bTraceActors)
	{
		flags.pawns = true;
		flags.others = true;
		flags.onlyProjectiles = true;
	}

	// hack?
	if (IsA("ChallengeHUD"))
	{
		flags.zoneChanges = true;
	}

	CollisionHit hit = XLevel()->Collision.TraceFirstHit(traceStart, traceEnd, this, extent, flags);
	hitNormal = hit.Normal;
	hitLocation = traceStart + (traceEnd - traceStart) * hit.Fraction;
	return hit.Actor;
}

bool UActor::FastTrace(const vec3& traceEnd, const vec3& traceStart)
{
	return !XLevel()->Collision.TraceAnyHit(traceStart, traceEnd, this, false, true, false);
}

bool UActor::TraceSurfHitInfo(vec3& Start, vec3& End, vec3* HitLocation, vec3* HitNormal, UTexture* HitTex, int* HitFlags)
{
	const TraceFlags flags = {
		.movers = true,
		.world = true
	};

	const auto hit = XLevel()->Collision.TraceFirstHit(Start, End, this, vec3(), flags);

	if (!hit.Node)
		return false;

	if (HitLocation)
		*HitLocation = Start + (End - Start) * hit.Fraction;

	if (HitNormal)
		*HitNormal = hit.Normal;

	if (HitTex)
		HitTex = XLevel()->Model->Surfaces[hit.Node->Surf].Material;

	if (HitFlags)
		*HitFlags = hit.Node->NodeFlags;

	return true;
}

bool UActor::TraceThisActor(vec3& TraceEnd, vec3 TraceStart, vec3* HitLocation, vec3* HitNormal, std::optional<vec3> Extent)
{
	TraceFlags flags {
		.pawns = true,
		.movers = true,
		.others = true,
		.world = true
	};

	const auto hit = XLevel()->Collision.TraceFirstHit(TraceStart, TraceEnd, this, Extent ? *Extent : vec3(), flags);

	if (!hit.Node && !hit.Actor)
		return false;

	if (HitLocation)
		*HitLocation = TraceStart + (TraceEnd - TraceStart) * hit.Fraction;

	if (HitNormal)
		*HitNormal = hit.Normal;

	return true;
}

bool UActor::IsBasedOn(UActor* other)
{
	for (UActor* cur = other; cur; cur = cur->ActorBase())
	{
		if (cur == this)
		{
			return true;
		}
	}
	return false;
}

bool UActor::IsOwnedBy(UActor* owner)
{
	for (UActor* cur = this; cur; cur = cur->Owner())
	{
		if (cur == owner)
		{
			return true;
		}
	}
	return false;
}

bool UActor::IsOverlapping(UActor* other)
{
	return XLevel()->Collision.IsOverlapping(this, other);
}

CollisionHit UActor::ProbeMoveCollision(const vec3& origin, const vec3& delta,
	bool isOwnBaseBlocking, CollisionHitList* tracedHits)
{
	if (bStatic() || !bMovable())
	{
		CollisionHit hit;
		hit.Fraction = 0.0f;
		return hit;
	}

	if (dot(delta, delta) < 0.00000001f)
		return {};

	CollisionHit blockingHit;
	if (Brush())
		return blockingHit;

	CollisionHitList localHits;
	CollisionHitList& hits = tracedHits ? *tracedHits : localHits;
	hits = XLevel()->Collision.Trace(
		origin, origin + delta, CollisionHeight(), CollisionRadius(), bCollideActors(), bCollideWorld(), false);
	ActorMoveCollisionProbe::BlockingRules rules;
	rules.ConsiderBlocking = bCollideWorld() || bBlockActors() || bBlockPlayers();
	rules.MovingActorUsesPlayerBlocking = UObject::TryCast<UPlayerPawn>(this)
		|| UObject::TryCast<UProjectile>(this);
	rules.MovingActorBlocksActors = bBlockActors();
	rules.MovingActorBlocksPlayers = bBlockPlayers();
	rules.OwnBaseIsBlocking = isOwnBaseBlocking;

	auto selected = ActorMoveCollisionProbe::SelectFirstBlockingHit(
		hits.begin(), hits.end(), rules, [this](const CollisionHit& hit)
		{
			ActorMoveCollisionProbe::HitProperties properties;
			properties.IsWorld = hit.Actor == nullptr;
			properties.HasActor = hit.Actor != nullptr;
			if (hit.Actor)
			{
				properties.HitActorUsesPlayerBlocking = UObject::TryCast<UPlayerPawn>(hit.Actor)
					|| UObject::TryCast<UProjectile>(hit.Actor);
				properties.HitActorBlocksActors = hit.Actor->bBlockActors();
				properties.HitActorBlocksPlayers = hit.Actor->bBlockPlayers();
				properties.HitActorIsBasedOnMovingActor = hit.Actor->IsBasedOn(this);
				properties.MovingActorIsBasedOnHitActor = IsBasedOn(hit.Actor);
			}
			return properties;
		});
	if (selected != hits.end())
		blockingHit = *selected;
	return blockingHit;
}

CollisionHit UActor::TryMove(const vec3& delta, bool dryRun, bool isOwnBaseBlocking,
	MoveCallbackEvidence* callbackEvidence)
{
	UPawn* movingPawn = UObject::TryCast<UPawn>(this);
	struct FallingHazardTryMoveScope
	{
		UPawn* Pawn = nullptr;
		bool Entered = false;
		explicit FallingHazardTryMoveScope(UPawn* pawn) : Pawn(pawn)
		{
			if (Pawn)
				Entered = Pawn->BeginFallingHazardTryMove();
		}
		~FallingHazardTryMoveScope()
		{
			if (Pawn && Entered)
				Pawn->EndFallingHazardTryMove();
		}
	} fallingHazardScope(dryRun ? nullptr : movingPawn);
	if (callbackEvidence)
		callbackEvidence->Mask = 0;
	if (bStatic() || !bMovable())
	{
		if (!dryRun && movingPawn)
			movingPawn->CancelPendingFallingHazardSweep(false);
		CollisionHit hit;
		hit.Fraction = 0.0f;
		return hit;
	}

	if (dot(delta, delta) < 0.00000001f)
	{
		if (!dryRun && movingPawn)
			movingPawn->CancelPendingFallingHazardSweep(false);
		return {};
	}

	bool useBlockPlayers = UObject::TryCast<UPlayerPawn>(this) || UObject::TryCast<UProjectile>(this);
	CollisionHitList hits;
	CollisionHit blockingHit = ProbeMoveCollision(Location(), delta, isOwnBaseBlocking, &hits);

	if (dryRun)
		return blockingHit;

	vec3 actuallyMoved = delta * blockingHit.Fraction;
	vec3 OldLocation = Location();

	XLevel()->Collision.RemoveFromCollision(this);
	XLevel()->Light.RemoveLight(this);
	Location() += actuallyMoved;
	XLevel()->Collision.AddToCollision(this);
	XLevel()->Light.AddLight(this);

	for (size_t i = 0; i < BasedActors.size(); )
	{
		UActor* basedActor = BasedActors[i];
		MoveCallbackEvidence basedActorCallbacks;
		basedActor->TryMove(actuallyMoved, false, false,
			callbackEvidence ? &basedActorCallbacks : nullptr);
		if (callbackEvidence && basedActorCallbacks.Any())
		{
			callbackEvidence->Mask |= MoveCallbackBasedActor;
			callbackEvidence->Mask |= basedActorCallbacks.Mask;
		}
		// UnrealScript events triggered in TryMove can call methods such as SetBase or Destroy, so need to guard while iterating.
		if (i < BasedActors.size() && BasedActors[i] == basedActor)
			i++;
	}

	// Notify actor of encroachment
	if (Brush() && (bBlockPlayers() || bBlockActors() || bCollideActors()))
	{
		Array<UActor*> encroachingActors = XLevel()->Collision.EncroachingActors(this);
		for (UActor* actor : encroachingActors)
		{
			if (actor == this || actor->Brush())
				continue;

			bool isBlocking;
			if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(actor) || UObject::TryCast<UProjectile>(actor))
				isBlocking = actor->bBlockPlayers() && bBlockPlayers();
			else
				isBlocking = actor->bBlockActors() && bBlockActors();

			if (isBlocking)
			{
				if (callbackEvidence)
					callbackEvidence->Mask |= MoveCallbackEncroachment;
				bool stopMovement = CallEvent(this, EventName::EncroachingOn, { ExpressionValue::ObjectValue(actor) }).ToBool();
				if (stopMovement)
				{
					XLevel()->Collision.RemoveFromCollision(this);
					XLevel()->Light.RemoveLight(this);
					Location() = OldLocation;
					XLevel()->Collision.AddToCollision(this);
					XLevel()->Light.AddLight(this);

					CollisionHit hit;
					hit.Fraction = 0.0f;
					if (movingPawn)
						movingPawn->CancelPendingFallingHazardSweep(true);
					return hit;
				}
			}
		}

		for (UActor* actor : encroachingActors)
		{
			if (actor == this)
				continue;

			bool isBlocking;
			if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(actor) || UObject::TryCast<UProjectile>(actor))
				isBlocking = actor->bBlockPlayers() && bBlockPlayers();
			else
				isBlocking = actor->bBlockActors() && bBlockActors();

			if (isBlocking)
			{
				if (callbackEvidence)
					callbackEvidence->Mask |= MoveCallbackEncroachment;
				CallEvent(actor, EventName::EncroachedBy, { ExpressionValue::ObjectValue(this) }).ToBool();
			}
		}
	}

	// Send bump notification if we hit an actor
	if (blockingHit.Actor)
	{
		if (!blockingHit.Actor->IsBasedOn(this))
		{
			if (callbackEvidence)
				callbackEvidence->Mask |= MoveCallbackBump;
			CallEvent(blockingHit.Actor, EventName::Bump, { ExpressionValue::ObjectValue(this) });
			CallEvent(this, EventName::Bump, { ExpressionValue::ObjectValue(blockingHit.Actor) });
		}
	}

	// Send touch notifications for anything we crossed while moving
	for (auto& hit : hits)
	{
		if (hit.Fraction >= blockingHit.Fraction)
			break;

		if (hit.Actor && !hit.Actor->IsBasedOn(this) && !IsBasedOn(hit.Actor) && bCollideActors() && hit.Actor->bCollideActors())
		{
			// We can't touch stuff we are blocked by
			bool isBlocking;
			if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(hit.Actor) || UObject::TryCast<UProjectile>(hit.Actor))
				isBlocking = hit.Actor->bBlockPlayers() && bBlockPlayers();
			else
				isBlocking = hit.Actor->bBlockActors() && bBlockActors();
			if (!isBlocking)
			{
				if (callbackEvidence)
					callbackEvidence->Mask |= MoveCallbackTouch;
				Touch(hit.Actor);
			}
		}
	}

	// Untouch everything we aren't overlapping anymore
	if (engine->LaunchInfo.IsUnrealTournament_469())
	{
		for (const auto actor : Touching_UT469())
			if (actor && !IsOverlapping(actor))
			{
				if (callbackEvidence)
					callbackEvidence->Mask |= MoveCallbackUnTouch;
				UnTouch(actor);
			}
	}
	else
	{
		for (const auto actor : Touching())
			if (actor && !IsOverlapping(actor))
			{
				if (callbackEvidence)
					callbackEvidence->Mask |= MoveCallbackUnTouch;
				UnTouch(actor);
			}
	}

	if (callbackEvidence)
	{
		const PointRegion nextRegion = FindRegion();
		if (Region().Zone != nextRegion.Zone)
			callbackEvidence->Mask |= MoveCallbackRegionChange;
		if (movingPawn)
		{
			const PointRegion nextFoot = FindRegion({
				0.0f, 0.0f, -movingPawn->CollisionHeight() });
			const PointRegion nextHead = FindRegion({
				0.0f, 0.0f, movingPawn->EyeHeight() });
			if (movingPawn->FootRegion().Zone != nextFoot.Zone)
				callbackEvidence->Mask |= MoveCallbackFootRegionChange;
			if (movingPawn->HeadRegion().Zone != nextHead.Zone)
				callbackEvidence->Mask |= MoveCallbackHeadRegionChange;
		}
	}
	if (movingPawn)
	{
		const MoveCallbackEvidence noCallbacks;
		movingPawn->LatchPendingFallingHazardSweepGeometry(
			blockingHit, callbackEvidence ? *callbackEvidence : noCallbacks);
	}
	UpdateActorZone();

	return blockingHit;
}

CollisionHit UActor::TryMoveSmooth(const vec3& delta)
{
	CollisionHit hit = TryMove(delta);
	if (hit.Fraction != 1.0f)
	{
		// We hit a slope. Try to follow it.
		vec3 alignedDelta = (delta - hit.Normal * dot(delta, hit.Normal)) * (1.0f - hit.Fraction);
		if (dot(delta, alignedDelta) >= 0.0f) // Don't end up going backwards
		{
			CollisionHit hit2 = TryMove(alignedDelta);
			return hit2; // XXX: does this break anything?
		}
	}

	return hit;
}

void UActor::Touch(UActor* actor)
{
	// Don't setup touch if any object has been destroyed
	if (bDeleteMe() || actor->bDeleteMe())
		return;

	if (engine->LaunchInfo.IsUnrealTournament_469())
	{
		auto TouchingArray = Touching_UT469();
		auto TouchingArray2 = actor->Touching_UT469();

		// Do nothing if actors are already touching
		for (int i = 0; i < TouchingArray.size(); i++)
		{
			if (TouchingArray[i] == actor)
				return;
		}

		// Only setup touch if we have room in both arrays
		int slot1 = -1, slot2 = -1;
		for (int i = 0; i < TouchingArray.size(); i++)
		{
			if (slot1 == -1 && TouchingArray[i] == nullptr)
				slot1 = i;
			if (slot2 == -1 && TouchingArray2[i] == nullptr)
				slot2 = i;
		}
		if (slot1 == -1 || slot2 == -1)
			return;

		// Setup links first so Destroy or recursive Touch calls always finds the touch binding
		TouchingArray[slot1] = actor;
		TouchEventSent[slot1] = true;
		TouchingArray2[slot2] = this;
		actor->TouchEventSent[slot2] = false;

		// Notify unrealscript for first actor
		CallEvent(this, EventName::Touch, { ExpressionValue::ObjectValue(actor) });

		// Notify unrealscript for second actor
		if (!actor->bDeleteMe())
		{
			for (int i = 0; i < TouchingArray.size(); i++)
			{
				if (TouchingArray2[i] == this && !actor->TouchEventSent[i])
				{
					actor->TouchEventSent[i] = true;
					CallEvent(actor, EventName::Touch, { ExpressionValue::ObjectValue(this) });
					break;
				}
			}
		}
	}
	else
	{
		auto TouchingArray = Touching();
		auto TouchingArray2 = actor->Touching();

		// Do nothing if actors are already touching
		for (int i = 0; i < TouchingArraySize; i++)
		{
			if (TouchingArray[i] == actor)
				return;
		}

		// Only setup touch if we have room in both arrays
		int slot1 = -1, slot2 = -1;
		for (int i = 0; i < TouchingArraySize; i++)
		{
			if (slot1 == -1 && TouchingArray[i] == nullptr)
				slot1 = i;
			if (slot2 == -1 && TouchingArray2[i] == nullptr)
				slot2 = i;
		}
		if (slot1 == -1 || slot2 == -1)
			return;

		// Setup links first so Destroy or recursive Touch calls always finds the touch binding
		TouchingArray[slot1] = actor;
		TouchEventSent[slot1] = true;
		TouchingArray2[slot2] = this;
		actor->TouchEventSent[slot2] = false;

		// Notify unrealscript for first actor
		CallEvent(this, EventName::Touch, { ExpressionValue::ObjectValue(actor) });

		// Notify unrealscript for second actor
		if (!actor->bDeleteMe())
		{
			for (int i = 0; i < TouchingArraySize; i++)
			{
				if (TouchingArray2[i] == this && !actor->TouchEventSent[i])
				{
					actor->TouchEventSent[i] = true;
					CallEvent(actor, EventName::Touch, { ExpressionValue::ObjectValue(this) });
					break;
				}
			}
		}
	}
}

void UActor::UnTouch(UActor* actor)
{
	auto TouchingArray = Touching();
	auto TouchingArray2 = actor->Touching();

	if (!bDeleteMe())
	{
		for (int i = 0; i < TouchingArraySize; i++)
		{
			if (TouchingArray[i] == actor)
			{
				TouchingArray[i] = nullptr;
				if (TouchEventSent[i])
				{
					TouchEventSent[i] = false;
					CallEvent(this, EventName::UnTouch, { ExpressionValue::ObjectValue(actor) });
				}
			}
		}
	}

	if (!actor->bDeleteMe())
	{
		for (int i = 0; i < TouchingArraySize; i++)
		{
			if (TouchingArray2[i] == this)
			{
				TouchingArray2[i] = nullptr;
				if (actor->TouchEventSent[i])
				{
					actor->TouchEventSent[i] = false;
					CallEvent(actor, EventName::UnTouch, { ExpressionValue::ObjectValue(this) });
				}
			}
		}
	}
}

bool UActor::Move(const vec3& delta)
{
	return TryMove(delta).Fraction == 1.0f;
}

bool UActor::MoveSmooth(const vec3& delta)
{
	CollisionHit hit = TryMoveSmooth(delta);
	return hit.Fraction != 1.0f;
}

bool UActor::HasAnim(const NameString& sequence)
{
	return Mesh() && Mesh()->GetSequence(sequence);
}

bool UActor::IsAnimating()
{
	return AnimRate() != 0.0f;
}

bool UActor::IsAnimating_HP(std::optional<NameString> RootBone)
{
	LogUnimplemented("Actor.IsAnimating_HP");
	return IsAnimating();
}

void UActor::FinishAnim()
{
	if (bAnimLoop())
	{
		bAnimLoop() = false;
		bAnimFinished() = false;
	}

	if (StateFrame)
		StateFrame->LatentState = LatentRunState::FinishAnim;
}

void UActor::FinishAnim_HP(std::optional<NameString> RootBone)
{
	LogUnimplemented("Actor.FinishAnim_HP");
	FinishAnim();
}

NameString UActor::GetAnimGroup(const NameString& sequence)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
			return seq->Group;
	}
	return {};
}

// UnrealScript variables controlling animation:
// 
// Tweening means animating (using vertex interpolation) from the last animation's frame to the current animation's first frame
//
// Mesh          - the mesh the animation belongs to
// AnimSequence  - current active animation sequence
// AnimFrame     - how far we've gotten in an animation 0.0 to 1.0 for current animation, negative for interpolation from old animation when tweening
// AnimLast      - end point for AnimFrame (when to stop/loop). It is zero when only tweening (don't play the animation). It is the start of the last frame (1-1/numframes) when playing an animation
// AnimRate      - how far AnimFrame moves in 1 second (AnimFrame += AnimRate * timeElapsed). If negative it is a scale factor used to convert Velocity length to animation speed
// AnimMinRate   - the minimum animation speed when AnimRate is negative (negative AnimRate means it should use length(Velocity) * abs(AnimRate) as the anim speed)
// TweenRate     - how fast to move when AnimFrame is negative (AnimFrame += TweenRate * timeElapsed)
// OldAnimRate   - AnimRate from previous call to PlayAnim/LoopAnim/TweenAnim
// bAnimLoop     - true if the animation should loop when AnimLast is reached
// bAnimNotify   - true if animation notify events should be fired when animating
// bAnimFinished - true if AnimLast was reached and there's no looping

void UActor::PlayAnim(const NameString& sequence, float rate, float tweenTime)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
		{
			SetTweenFromAnimFrame();

			AnimSequence() = sequence;

			if (seq->NumFrames > 1)
			{
				AnimFrame() = tweenTime > 0.0f ? -1.0f / seq->NumFrames : 0.0f;
				AnimLast() = 1.0f - 1.0f / seq->NumFrames;
				AnimRate() = rate * seq->Rate / seq->NumFrames;
				TweenRate() = tweenTime > 0.0f ? 1.0f / (tweenTime * seq->NumFrames) : 0.0f;
				bAnimNotify() = !seq->Notifys.empty();
				OldAnimRate() = AnimRate();
			}
			else
			{
				// Special case for 1 frame animations. Simply keep drawing the animation for 0.1 second (or tween duration, if tweening).

				AnimFrame() = -1.0f;
				AnimLast() = 0.0f;
				AnimRate() = 0.0f;
				TweenRate() = tweenTime > 0.0f ? 1.0f / tweenTime : 10.0f;
				bAnimNotify() = false;
				OldAnimRate() = 0.0f;
				AnimMinRate() = 0.0f;
			}

			bAnimLoop() = false;
			bAnimFinished() = false;
		}
	}
}

void UActor::PlayBlendAnim(const NameString& sequenceName, float rate, float tweenTime, int blendSlot)
{
	LogUnimplemented("Actor.PlayBlendAnim");
	if (blendSlot < 0 || blendSlot > 3)
	{
		LogMessage("Invalid channel for PlayBlendAnim!");
		return;
	}
	if (!Mesh())
	{
		LogMessage("No mesh for PlayBlendAnim");
		return;
	}

	MeshAnimSeq* sequence = Mesh()->GetSequence(sequenceName);
	if (!sequence)
	{
		LogMessage("Sequence not found for PlayBlendAnim");
		return;
	}

	int numFrames = sequence->NumFrames;
	float sequenceRate = sequence->Rate;

	if (BlendAnimSequence()[blendSlot].IsNone())
	{
		tweenTime = 0.0f;
	}

	BlendAnimSequence()[blendSlot] = sequenceName;

	BlendAnimFrame()[blendSlot] = -1.0f / numFrames;

	BlendAnimMinRate()[blendSlot] = (rate * sequenceRate) / numFrames;

	BlendAnimLast()[blendSlot] = 1.0f - (1.0f / numFrames);

	if (BlendAnimLast()[blendSlot] == 0.0f)
	{
		BlendAnimRate()[blendSlot] = 0.0f;
		BlendAnimFrame()[blendSlot] = 0.0f;

		BlendTweenRate()[blendSlot] = (tweenTime <= 0.0f) ? 10.0f : (1.0f / tweenTime);
	}
	else if (tweenTime <= 0.0f)
	{
		if (tweenTime == -1.0f)
		{
			if (BlendAnimMinRate()[blendSlot] <= 0.0f)
			{
				if (BlendAnimMinRate()[blendSlot] == 0.0f)
				{
					BlendTweenRate()[blendSlot] = 1.0f / (numFrames * 0.025f);
				}
				else 
				{
					float speed = length(Velocity());
					float computed = speed * (-BlendAnimMinRate()[blendSlot]);
					float minVal = BlendAnimRate()[blendSlot] * 0.5f;

					BlendTweenRate()[blendSlot] = std::max(computed, minVal);
				}
			}
			else 
			{
				BlendTweenRate()[blendSlot] = BlendAnimMinRate()[blendSlot];
			}
		}
		else 
		{
			BlendTweenRate()[blendSlot] = 0.0f;
			BlendAnimFrame()[blendSlot] = 0.001f;
		}
	}
	else 
	{
		BlendTweenRate()[blendSlot] = 1.0f / (numFrames * tweenTime);
	}
	
	float oldX = SimBlendAnim()[blendSlot].x;
	float oldY = SimBlendAnim()[blendSlot].y;
	float oldZ = SimBlendAnim()[blendSlot].z;
	float oldW = SimBlendAnim()[blendSlot].w;

	SimBlendAnim()[blendSlot].z = BlendAnimFrame()[blendSlot] * 10000.0f;
	SimBlendAnim()[blendSlot].w = BlendAnimRate()[blendSlot] * 10000.0f;
	SimBlendAnim()[blendSlot].x = BlendTweenRate()[blendSlot] * 1000.0f;
	SimBlendAnim()[blendSlot].y = BlendAnimLast()[blendSlot] * 10000.0f;

	if (oldZ == SimBlendAnim()[blendSlot].z && oldW == SimBlendAnim()[blendSlot].w && oldX == SimBlendAnim()[blendSlot].x && oldY == SimBlendAnim()[blendSlot].y)
	{
		SimBlendAnim()[blendSlot].y += 1.0f;
	}

	OldBlendAnimRate()[blendSlot] = BlendAnimRate()[blendSlot];
	BlendAnimMinRate()[blendSlot] = BlendAnimRate()[blendSlot];
}


void UActor::LoopAnim(const NameString& sequence, float rate, float tweenTime, float minRate)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
		{
			if (AnimSequence() == sequence && IsAnimating() && bAnimLoop())
			{
				if (seq->NumFrames > 1)
				{
					AnimRate() = rate * seq->Rate / seq->NumFrames;
					AnimMinRate() = minRate * seq->Rate / seq->NumFrames;
					TweenRate() = tweenTime > 0.0f ? 1.0f / (tweenTime * seq->NumFrames) : 0.0f;
					OldAnimRate() = AnimRate();
				}
			}
			else
			{
				SetTweenFromAnimFrame();

				AnimSequence() = sequence;
				if (seq->NumFrames > 1)
				{
					AnimFrame() = tweenTime > 0.0f ? -1.0f / seq->NumFrames : 0.0f;
					AnimLast() = 1.0f - 1.0f / seq->NumFrames;
					bAnimNotify() = !seq->Notifys.empty();
					AnimRate() = rate * seq->Rate / seq->NumFrames;
					AnimMinRate() = minRate * seq->Rate / seq->NumFrames;
					TweenRate() = tweenTime > 0.0f ? 1.0f / (tweenTime * seq->NumFrames) : 0.0f;
					OldAnimRate() = AnimRate();
				}
				else
				{
					// Special case for 1 frame animations. Simply keep drawing the animation for 0.1 second (or tween duration, if tweening).

					AnimFrame() = -1.0f;
					AnimLast() = 0.0f;
					AnimRate() = 0.0f;
					TweenRate() = tweenTime > 0.0f ? 1.0f / tweenTime : 10.0f;
					bAnimNotify() = false;
					OldAnimRate() = 0.0f;
					AnimMinRate() = 0.0f;
				}
				bAnimFinished() = false;
				bAnimLoop() = true;
			}
		}
	}
}

void UActor::TweenAnim(const NameString& sequence, float tweenTime)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
		{
			SetTweenFromAnimFrame();

			AnimSequence() = sequence;
			AnimFrame() = tweenTime > 0.0f ? -1.0f / seq->NumFrames : 0.0f;
			AnimLast() = 0.0f;
			AnimRate() = 0.0f;
			AnimMinRate() = 0.0f;
			TweenRate() = tweenTime > 0.0f ? 1.0f / (tweenTime * seq->NumFrames) : 0.0f;
			OldAnimRate() = AnimRate();
			bAnimNotify() = false;
			bAnimFinished() = false;
			bAnimLoop() = false;
		}
	}
}

void UActor::TickAnimation(float elapsed)
{
	if (StateFrame && StateFrame->LatentState == LatentRunState::FinishAnim)
	{
		if (!IsAnimating() || AnimFrame() >= AnimLast())
			StateFrame->LatentState = LatentRunState::Continue;
	}

	for (int i = 0; elapsed > 0.0f && i < 10; i++)
	{
		// If AnimFrame is positive we are doing a normal animation. If it is negative we are doing a tween animation.
		float fromAnimTime = AnimFrame();
		if (fromAnimTime >= 0.0f)
		{
			// If AnimRate is positive we are animating at a fixed rate. If it is negative we animate based on velocity (using AnimRate as a speed scale factor)
			float animRate = (AnimRate() >= 0) ? AnimRate() : std::max(AnimMinRate(), -AnimRate() * length(Velocity()));
			if (animRate == 0.0f)
				break;

			// Find what time will we be at the end of the animation
			float toAnimTime = fromAnimTime + animRate * elapsed;

			// Stop at the next notify event, if any
			if (Mesh() && bAnimNotify())
			{
				MeshAnimSeq* seq = Mesh()->GetSequence(AnimSequence());
				if (seq)
				{
					bool foundEvent = false;
					for (const MeshAnimNotify& n : seq->Notifys)
					{
						if (n.Time > fromAnimTime && n.Time <= toAnimTime)
						{
							if (FindEventFunction(this, n.Function))
							{
								toAnimTime = n.Time;
								elapsed -= (toAnimTime - fromAnimTime) / animRate;
								AnimFrame() = toAnimTime;
								foundEvent = true;
								CallEvent(this, n.Function);
								break;
							}
						}
					}
					if (foundEvent)
						continue;
				}
			}

			// Looped animations got their AnimEnd notify event at the AnimLast point, NOT when the loop finishes!
			if (bAnimLoop() && AnimLast() > fromAnimTime && AnimLast() <= toAnimTime)
			{
				toAnimTime = AnimLast();
				elapsed -= (toAnimTime - fromAnimTime) / animRate;
				AnimFrame() = toAnimTime;

				if (StateFrame && StateFrame->LatentState == LatentRunState::FinishAnim)
					StateFrame->LatentState = LatentRunState::Continue;

				CallEvent(this, EventName::AnimEnd);
				continue;
			}

			// Clamp elapsed time to the animation end. This differs for looping animations as they also have to take the last frame into account before looping.
			float animEndTime = bAnimLoop() ? 1.0f : AnimLast();
			if (toAnimTime < fromAnimTime) // This can happen if FinishAnim is called after a looping animation made it past the AnimLast point
			{
				toAnimTime = fromAnimTime;
				animEndTime = fromAnimTime;
				elapsed = 0.0f;
			}
			else if (toAnimTime >= animEndTime)
			{
				elapsed -= (animEndTime - fromAnimTime) / animRate;
				toAnimTime = animEndTime;
			}
			else
			{
				elapsed = 0.0f;
			}

			AnimFrame() = toAnimTime;

			if (toAnimTime == animEndTime)
			{
				if (bAnimLoop())
				{
					AnimFrame() = 0.0f;
				}
				else
				{
					AnimRate() = 0.0f;
					bAnimFinished() = true;
				}
			}

			if (!bAnimLoop() && fromAnimTime < animEndTime && toAnimTime >= animEndTime)
			{
				if (StateFrame && StateFrame->LatentState == LatentRunState::FinishAnim)
					StateFrame->LatentState = LatentRunState::Continue;

				CallEvent(this, EventName::AnimEnd);
			}
		}
		else
		{
			float tweenRate = TweenRate();
			if (tweenRate == 0.0f)
				break;

			float toAnimTime = fromAnimTime + tweenRate * elapsed;

			float animEndTime = 0.0f;
			if (toAnimTime >= animEndTime)
			{
				elapsed -= (animEndTime - fromAnimTime) / tweenRate;
				toAnimTime = animEndTime;
			}
			else
			{
				elapsed = 0.0f;
			}

			AnimFrame() = toAnimTime;

			if (toAnimTime == animEndTime && AnimRate() == 0.0f)
			{
				if (StateFrame && StateFrame->LatentState == LatentRunState::FinishAnim)
					StateFrame->LatentState = LatentRunState::Continue;

				bAnimFinished() = true;
				//engine->LogMessage("CallEvent(AnimEnd) for " + Class->FriendlyName.ToString() + "");
				CallEvent(this, EventName::AnimEnd);
			}
		}
	}
}

void UActor::TickBlendAnimation(float elapsed)
{
	for (int i = 0; elapsed > 0.0f && i < 4; i++)
	{
		if (BlendAnimSequence()[i].IsNone())
			continue;

		if (BlendAnimFrame()[i] >= BlendAnimLast()[i])
			continue;

		float oldFrame = BlendAnimFrame()[i];

		if (BlendAnimFrame()[i] < 0.0f)
		{
			BlendAnimFrame()[i] += elapsed * BlendTweenRate()[i];

			if (BlendAnimFrame()[i] < 0.0f)
				continue;

			BlendAnimFrame()[i] = 0.0f;

			elapsed = (BlendAnimFrame()[i] * elapsed) / (BlendAnimFrame()[i] - oldFrame);
			continue;
		}

		if (BlendAnimRate()[i] < 0.0f)
		{
			float speed = length(Velocity());

			float adjustedRate = -speed * BlendAnimRate()[i];

			float minRate = BlendAnimLast()[i];
			if (adjustedRate > minRate)
				adjustedRate = minRate;

			BlendAnimFrame()[i] += adjustedRate * elapsed;
		}
		else
		{
			BlendAnimFrame()[i] += BlendAnimRate()[i] * elapsed;
		}

		if (BlendAnimFrame()[i] >= BlendAnimLast()[i])
		{
			float endFrame = BlendAnimLast()[i];

			BlendAnimFrame()[i] = endFrame;
			BlendAnimRate()[i] = 0.0f;

			elapsed = ((BlendAnimFrame()[i] - endFrame) * elapsed) / (BlendAnimFrame()[i] - oldFrame);

			if (RemoteRole() < ENetRole::ROLE_SimulatedProxy)
			{
				SimBlendAnim()[i].z = BlendAnimFrame()[i] * 10000.0f;

				float rate = BlendAnimRate()[i] * 5000.0f;
				if (rate > 32767.0f)
					rate = 32767.0f;

				SimBlendAnim()[i].w = rate;
			}
		}
	}
}

void UActor::SetTweenFromAnimFrame()
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(AnimSequence());
		if (seq)
		{
			float animFrame = std::max(AnimFrame(), 0.0f) * seq->NumFrames;
			int frame0 = (int)animFrame;
			int frame1 = frame0 + 1;
			frame0 = frame0 % seq->NumFrames;
			frame1 = frame1 % seq->NumFrames;
			TweenFromAnimFrame.V0 = (seq->StartFrame + frame0) * Mesh()->FrameVerts;
			TweenFromAnimFrame.V1 = (seq->StartFrame + frame1) * Mesh()->FrameVerts;
			TweenFromAnimFrame.T = animFrame - (float)frame0;
		}
		else // For safety. Should never happen.
		{
			TweenFromAnimFrame.V0 = 0;
			TweenFromAnimFrame.V1 = 0;
			TweenFromAnimFrame.T = -1.0f;
		}
	}
}

void UActor::MakeNoise(float loudness)
{
	UPawn* noisePawn = UObject::Cast<UPawn>(Instigator());

	if (!noisePawn || Level()->NetMode() == NM_Client)
		return;

	float currentTime = Level()->TimeSeconds();
	vec3 delta1 = noisePawn->noise1spot() - Location();
	vec3 delta2 = noisePawn->noise2spot() - Location();
	if ((noisePawn->noise1time() > currentTime - 0.2f && dot(delta1, delta1) < 2500.0f && noisePawn->noise1loudness() >= 0.9f * loudness) ||
		(noisePawn->noise2time() > currentTime - 0.2f && dot(delta2, delta2) < 2500.0f && noisePawn->noise2loudness() >= 0.9f * loudness))
	{
		return;
	}

	if (noisePawn->noise1time() < currentTime - 0.18f)
	{
		noisePawn->noise1time() = currentTime;
		noisePawn->noise1spot() = Location();
		noisePawn->noise1loudness() = loudness;
	}
	else if (noisePawn->noise2time() < currentTime - 0.18f)
	{
		noisePawn->noise2time() = currentTime;
		noisePawn->noise2spot() = Location();
		noisePawn->noise2loudness() = loudness;
	}
	else if (dot(delta1, delta1) < 2500.0f)
	{
		noisePawn->noise1time() = currentTime;
		noisePawn->noise1spot() = Location();
		noisePawn->noise1loudness() = loudness;
	}
	else if (noisePawn->noise2loudness() <= loudness)
	{
		noisePawn->noise2time() = currentTime;
		noisePawn->noise2spot() = Location();
		noisePawn->noise2loudness() = loudness;
	}

	for (UPawn* pawn = Level()->PawnList(); pawn != nullptr; pawn = pawn->nextPawn())
	{
		if (pawn != noisePawn && pawn->CanHearNoise(this, loudness))
		{
			CallEvent(pawn, EventName::HearNoise, { ExpressionValue::FloatValue(loudness), ExpressionValue::ObjectValue(this) });
		}
	}
}

bool UActor::PlayerCanSeeMe()
{
	for (UPawn* pawn = Level()->PawnList(); pawn != nullptr; pawn = pawn->nextPawn())
	{
		if (pawn == this)
			continue;

		vec3 L = Location() - pawn->Location();
		float dist2 = dot(L, L);

		// Too far away
		if (dist2 > 500 * 500)
			continue;

		// Without behind view the pawn can only see in a 75 degree cone in front of them
		if (!pawn->bBehindView())
		{
			vec3 viewDirection = Coords::Rotation(pawn->ViewRotation()).XAxis;
			if (dot(viewDirection, L) < 0.2588190451f * dist2)
				continue;
		}

		// Try check for line of sight
		vec3 eyePos = pawn->Location();
		eyePos.z += pawn->BaseEyeHeight();
		if (pawn->FastTrace(Location(), eyePos))
			return true;
	}
	return false;
}

void UActor::UpdateBspInfo()
{
	// Figure out where the actor is visually located in the world
	BBox bbox;
	EDrawType dt = (EDrawType)DrawType();
	if (dt == DT_Mesh && Mesh())
	{
		UMesh* mesh = Mesh();
		Coords rotation = Coords::Rotation(Rotation());
		mat4 objectToWorld = mat4::translate(Location() + PrePivot()) * Coords::Rotation(Rotation()).ToMatrix() * mat4::scale(DrawScale());
		mat4 meshToWorld = objectToWorld * mesh->meshToObject;
		bbox = mesh->BoundingBox.transform(meshToWorld);
	}
	else if ((dt == DT_Sprite || dt == DT_SpriteAnimOnce) && (Texture()))
	{
		vec3 location = Location();
		auto texWidth = Texture()->UsedMipmaps[0].Width;
		auto texHeight = Texture()->UsedMipmaps[0].Height;
		// vec3 extents = vec3(100.0f); // To do: this is wrong. We need the size of a sprite
		vec3 extents = vec3(std::max(texWidth, texHeight) * 0.5f * DrawScale());
		bbox.min = location - extents;
		bbox.max = location + extents;
	}
	else if (dt == DT_Brush && Brush())
	{
		UModel* brush = Brush();
		if (UMover* mover = UObject::TryCast<UMover>(this))
		{
			mat4 objectToWorld = mat4::translate(Location()) * Coords::Rotation(Rotation()).ToMatrix() * mat4::scale(mover->MainScale().Scale) * mat4::translate(-PrePivot());
			bbox = brush->BoundingBox.transform(objectToWorld);
		}
		else
		{
			bbox.min = vec3(0.0f);
			bbox.max = vec3(0.0f);
		}
	}
	else
	{
		bbox.min = vec3(0.0f);
		bbox.max = vec3(0.0f);
	}

	// Is actor still in the bsp tree at the correct location?
	if (!BspInfo.Node || BspInfo.BoundingBox != bbox)
	{
		RemoveFromBspNode();

		BspInfo.BoundingBox = bbox;

		vec3 location = bbox.center();
		vec3 extents = bbox.extents();

		ULevel* level = XLevel();
		BspNode* node = level ? &level->Model->Nodes[0] : nullptr;
		while (node)
		{
			int side = NodeAABBOverlap(location, extents, node);
			if (side == 0 || (side < 0 && node->Front < 0) || (side > 0 && node->Back < 0))
			{
				AddToBspNode(node);
				break;
			}
			else if (side < 0)
			{
				node = &level->Model->Nodes[node->Front];
			}
			else
			{
				node = &level->Model->Nodes[node->Back];
			}
		}
	}
}

void UActor::AddToBspNode(BspNode* node)
{
	BspInfo.Node = node;

	if (node->ActorList)
	{
		node->ActorList->BspInfo.Prev = this;
		BspInfo.Next = node->ActorList;
	}

	node->ActorList = this;
}

void UActor::RemoveFromBspNode()
{
	if (BspInfo.Node)
	{
		if (BspInfo.Next)
		{
			BspInfo.Next->BspInfo.Prev = BspInfo.Prev;
		}
		if (BspInfo.Prev)
		{
			BspInfo.Prev->BspInfo.Next = BspInfo.Next;
		}
		if (BspInfo.Node->ActorList == this)
		{
			BspInfo.Node->ActorList = BspInfo.Next;
		}
		BspInfo.Node = nullptr;
		BspInfo.Prev = nullptr;
		BspInfo.Next = nullptr;
	}
}

// -1 = inside, 0 = intersects, 1 = outside
int UActor::NodeAABBOverlap(const vec3& center, const vec3& extents, BspNode* node)
{
	float e = extents.x * std::abs(node->PlaneX) + extents.y * std::abs(node->PlaneY) + extents.z * std::abs(node->PlaneZ);
	float s = center.x * node->PlaneX + center.y * node->PlaneY + center.z * node->PlaneZ - node->PlaneW;
	if (s - e > 0.0f)
		return -1;
	else if (s + e < 0.0f)
		return 1;
	else
		return 0;
}

UTexture* UActor::GetMultiskin(int index)
{
	if (engine->LaunchInfo.ue1Version > 219 && index >= 0 && index < 8)
		return MultiSkins()[index];
	else
		return nullptr;
}

void UActor::DeusExConBindEvents()
{
	auto mission = UObject::Cast<UConversationList>(engine->GetDeusExMission());
	if (!mission)
		return;

	UClass* clsConListItem = engine->packages->FindClass("ConSys.ConListItem");
	UConListItem* conListItem = nullptr;

	NameString bindName = BindName();
	if (!bindName.IsNone())
	{
		for (UConItem* item = mission->conversations(); item; item = item->Next())
		{
			auto conversation = UObject::Cast<UConversation>(item->ConObject());
			NameString conOwnerName = conversation->conOwnerName();
			if (conOwnerName == bindName)
			{
				NameString name;
				UConListItem* newItem = UObject::Cast<UConListItem>(engine->LevelPackage->NewObject(name, clsConListItem, ObjectFlags::Transient, true));
				newItem->con() = conversation;
				newItem->Next() = conListItem;
				conListItem = newItem;
			}
		}
	}

	NameString barkBindName = BarkBindName();
	if (!barkBindName.IsNone())
	{
		for (UConItem* item = mission->conversations(); item; item = item->Next())
		{
			auto conversation = UObject::Cast<UConversation>(item->ConObject());
			NameString conOwnerName = conversation->conOwnerName();
			if (conOwnerName == barkBindName)
			{
				NameString name;
				UConListItem* newItem = UObject::Cast<UConListItem>(engine->LevelPackage->NewObject(name, clsConListItem, ObjectFlags::Transient, true));
				newItem->con() = conversation;
				newItem->Next() = conListItem;
				conListItem = newItem;
			}
		}
	}

	ConListItems() = conListItem;
}

void UActor::PlayAnim_HP(const NameString& Sequence, std::optional<float> Rate, std::optional<float> TweenTime, std::optional<EAnimType> Type, std::optional<NameString> RootBone)
{
	LogUnimplemented("Actor.PlayAnim_HP");
}

void UActor::LoopAnim_HP(const NameString& Sequence, std::optional<float> Rate, std::optional<float> TweenTime, std::optional<float> MinRate, std::optional<EAnimType> Type, std::optional<NameString> RootBone)
{
	LogUnimplemented("Actor.LoopAnim_HP");
}

BoundingBox UActor::GetWorldCollisionBox(bool bVisual)
{
	LogUnimplemented("Actor.GetWorldCollisionBox");
	return {};
}

vec3 UActor::GetRenderExtent()
{
	LogUnimplemented("Actor.GetRenderExtent");
	return vec3(100.0f);
}

UActor* UActor::CreateAnimChannel(UClass* NewClass, EAnimType Type, const NameString& RootBone, bool bTransient)
{
	auto animChannel = Spawn(NewClass, {}, {}, {}, {});
	LogUnimplemented("Actor.CreateAnimChannel");
	return animChannel;
}

int UActor::BoneNumber(const NameString& Bone)
{
	LogUnimplemented("Actor.BoneNumber");
	return 0;
}

NameString UActor::BoneName(int Bone)
{
	LogUnimplemented("Actor.BoneName");
	return {};
}

vec3 UActor::BonePos(const NameString& Bone)
{
	LogUnimplemented("Actor.BonePos");
	return vec3(0.0f);
}

UTexture* UActor::CreateTextureFromScreenShot(UViewport* vport)
{
	LogUnimplemented("Actor.CreateTextureFromScreenShot");
	return nullptr;
}

UTexture* UActor::CreateTextureFromBMP(const std::string& name, const std::string& filename)
{
	LogUnimplemented("Actor.CreateTextureFromBMP");
	return nullptr;
}

bool UActor::SaveObjectAsFile(const std::string& dir, UObject* object)
{
	LogUnimplemented("Actor.SaveObjectAsFile");
	return false;
}

bool UActor::LoadObjectAsFile(const std::string& dir, UObject* object)
{
	LogUnimplemented("Actor.LoadObjectAsFile");
	return false;
}

bool UActor::SaveGameSaveInfo(const std::string& dir, UObject* object)
{
	LogUnimplemented("Actor.SaveGameSaveInfo");
	return false;
}

bool UActor::LoadGameSaveInfo(const std::string& dir, UObject* object)
{
	LogUnimplemented("Actor.LoadGameSaveInfo");
	return false;
}

bool UActor::IsOSVer2kOrXP()
{
	return true;
}

/////////////////////////////////////////////////////////////////////////////

bool UPawn::ActorReachable(UActor* anActor, bool checkNavpoint)
{
	if (!anActor)
		return false;

	UPawn* aPawn = UObject::TryCast<UPawn>(anActor);

	// If actor is not a pawn we assume we can't reach if they are too far away
	if (!aPawn)
	{
		vec3 delta = anActor->Location() - Location();
		float dist2 = dot(delta, delta);
		if (dist2 > 1000.0f * 1000.0f)
			return false;
	}

	// Navpoints may not be reachable at all according to reachspecs
	if (checkNavpoint)
	{
		// Check if we are trying to reach a navigation point.
		// They can also be hiding in an inventory as an inventory (pickup item) can be linked to a navigation point.
		UNavigationPoint* navPoint = UObject::TryCast<UNavigationPoint>(anActor);
		if (UInventory* inventory = UObject::TryCast<UInventory>(anActor))
			navPoint = inventory->myMarker();

		if (navPoint)
		{
			// Check if the navigation point is theoretically reachable at all according to reachspecs.
			bool couldBeReachable = false;
			float radius = CollisionRadius();
			float height = CollisionHeight();
			for (UNavigationPoint* cur = Level()->NavigationPointList(); cur != nullptr; cur = cur->nextNavigationPoint())
			{
				const auto& specs = XLevel()->ReachSpecs;
				for (int index : cur->Paths())
				{
					if (index < 0 || (size_t)index >= specs.size())
						break;
					const LevelReachSpec& reachSpec = specs[index];

					if (reachSpec.endActor != navPoint)
						continue; // Not a path to this nav point

					if (reachSpec.collisionRadius < radius || reachSpec.collisionHeight < height || reachSpec.bPruned)
						continue; // Skip nav node links that we can't pass through

					if ((reachSpec.endActor->bPlayerOnly() && !bIsPlayer()) || (reachSpec.endActor->bPlayerOnly() && !bIsPlayer()))
						continue; // Skip nav nodes only for the player if we aren't one

					// To do: check reachFlags

					couldBeReachable = true;
					break;
				}

				if (couldBeReachable)
					break;

				for (int index : cur->PrunedPaths())
				{
					if (index < 0 || (size_t)index >= specs.size())
						break;
					const LevelReachSpec& reachSpec = specs[index];

					if (reachSpec.endActor != navPoint)
						continue; // Not a path to this nav point

					if (reachSpec.collisionRadius < radius || reachSpec.collisionHeight < height || reachSpec.bPruned)
						continue; // Skip nav node links that we can't pass through

					if ((reachSpec.endActor->bPlayerOnly() && !bIsPlayer()) || (reachSpec.endActor->bPlayerOnly() && !bIsPlayer()))
						continue; // Skip nav nodes only for the player if we aren't one

					// To do: check reachFlags

					couldBeReachable = true;
					break;
				}

				if (couldBeReachable)
					break;
			}

			if (!couldBeReachable)
				return false;
		}
	}

	// If the actor is in a pain zone and we don't like pain we can't go there
	if (aPawn)
	{
		if (aPawn->FootRegion().Zone->bPainZone() && aPawn->FootRegion().Zone->DamageType() != ReducedDamageType())
			return false;
	}
	else
	{
		if (anActor->Region().Zone->bPainZone() && anActor->Region().Zone->DamageType() != ReducedDamageType())
			return false;
	}

	// If the actor is in the water and we can't swim we can't go there
	if (anActor->Region().Zone->bWaterZone() && !bCanSwim())
		return false;

	vec3 eyePos = Location();
	eyePos.z += BaseEyeHeight();

	// If we can't see the actor we can't go there
	if (!FastTrace(anActor->Location(), eyePos))
		return false;

	// If we can't stand at the actor location we can't go there
	if (!CheckLocation(anActor->Location(), CollisionRadius(), CollisionHeight(), bCollideWorld() || bCollideWhenPlacing()).first)
		return false;

	// Try simulate movement to see if we can get to the actor
	int mode = Physics();
	if (mode == PHYS_Walking)
	{
		// To do: take zone changes into account?

		auto zone = Region().Zone;
		float gravityDirection = zone->ZoneGravity().z > 0.0f ? 1.0f : -1.0f;
		vec3 stepUpDelta(0.0f, 0.0f, -gravityDirection * MaxStepHeight());
		vec3 stepDownDelta(0.0f, 0.0f, gravityDirection * MaxStepHeight() * stepDownDeltaFactor);

		vec3 oldLocation = Location();
		bool reached = false;
		for (int iteration = 0; iteration < 5; iteration++)
		{
			vec3 moveDelta = anActor->Location() - Location();
			moveDelta.z = 0.0f;
			float goalDist2 = dot(moveDelta, moveDelta);
			if (goalDist2 <= 1.0f)
			{
				reached = true;
				break;
			}

			// step up first so we can get past stairs going up
			CollisionHit hit = TryMove(stepUpDelta, true);
			Location() += stepUpDelta * hit.Fraction;

			// move towards goal
			hit = TryMove(moveDelta, true);
			vec3 actuallyMoved = moveDelta * hit.Fraction;
			Location() += actuallyMoved;

			if (hit.Fraction < 1.0f)
			{
				moveDelta = anActor->Location() - Location();
				vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
				if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
				{
					hit = TryMove(alignedDelta, true);
					actuallyMoved = moveDelta * hit.Fraction;
					Location() += actuallyMoved;
				}
				else
				{
					break;
				}
			}

			// move back down to original vertical position
			hit = TryMove(-stepUpDelta, true);
			Location() -= stepUpDelta * hit.Fraction;

			float moveDist2 = dot(actuallyMoved, actuallyMoved);
			if (moveDist2 <= 1.0f)
				break;
		}

		if (reached)
		{
			// Step down + fall to goal
			vec3 moveDelta = anActor->Location() - Location();
			moveDelta.x = 0.0f;
			moveDelta.y = 0.0f;
			if ((moveDelta.z < -0.1f && gravityDirection == -1.0f) || (moveDelta.z > 0.1f && gravityDirection == 1.0f))
			{
				CollisionHit hit = TryMove(moveDelta, true);
				vec3 actuallyMoved = moveDelta * hit.Fraction;
				Location() += actuallyMoved;
			}

			// Did we get there vertically too?
			reached = std::abs(anActor->Location().z - Location().z) <= CollisionHeight();
		}

		Location() = oldLocation;
		return reached;
	}
	else if (mode == PHYS_Flying || mode == PHYS_Swimming)
	{
		// To do: take zone changes into account?

		vec3 oldLocation = Location();
		bool reached = false;
		for (int iteration = 0; iteration < 5; iteration++)
		{
			vec3 moveDelta = anActor->Location() - Location();
			float goalDist2 = dot(moveDelta, moveDelta);
			if (goalDist2 <= 1.0f)
			{
				reached = true;
				break;
			}

			CollisionHit hit = TryMove(moveDelta, true);
			vec3 actuallyMoved = moveDelta * hit.Fraction;
			Location() += actuallyMoved;

			if (hit.Fraction < 1.0f)
			{
				moveDelta = anActor->Location() - Location();
				vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
				if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
				{
					hit = TryMove(alignedDelta, true);
					actuallyMoved = moveDelta * hit.Fraction;
					Location() += actuallyMoved;
				}
				else
				{
					break;
				}
			}

			float moveDist2 = dot(actuallyMoved, actuallyMoved);
			if (moveDist2 <= 1.0f)
				break;
		}

		Location() = oldLocation;
		return reached;
	}
	else
	{
		// Hopefully not a physics mode the bots use when calling ActorReachable
		LogUnimplemented("ActorReachable called for unsupported physics mode");
		return false;
	}
}

bool UPawn::PointReachable(vec3 aPoint)
{
	PointRegion pointRegion = XLevel()->Model->FindRegion(aPoint, Level());

	if (!Region().Zone->bWaterZone() && !bCanSwim() && pointRegion.Zone->bWaterZone())
		return false;
	if (!FootRegion().Zone->bPainZone() && pointRegion.Zone->bPainZone() && pointRegion.Zone->DamageType() != ReducedDamageType())
		return false;

	vec3 eyePos = Location();
	eyePos.z += BaseEyeHeight();
	if (!FastTrace(aPoint, eyePos))
		return false;

	return CheckLocation(aPoint, CollisionRadius(), CollisionHeight(), bCollideWorld() || bCollideWhenPlacing()).first;
}

bool UPawn::PickWallAdjust()
{
	if (IsAutonomousPlayerBot(this))
	{
		WallAdjustCallCountValue++;
		if (WallAdjustRecovery.ReplanPending)
		{
			WallAdjustRecovery = {};
			Acceleration() = vec3(0.0f);
			MoveTimer() = -1.0f;
			WallAdjustForcedReplanCountValue++;
			return false;
		}
		const PawnMovement::WallAdjustObservation observation = PawnMovement::ObserveWallAdjust(
			WallAdjustRecovery, Location(), wallAdjustRepeatWindow, wallAdjustRepeatRadius,
			wallAdjustMinimumSustainedTime, wallAdjustMinimumObservations);
		WallAdjustRecovery = observation.State;
		if (observation.Repeated)
			WallAdjustRepeatCountValue++;
		if (observation.State.SteeringActive)
			return true;
		if (observation.Escalate)
		{
			vec2 approachDirection = Acceleration().xy();
			if (dot(approachDirection, approachDirection) <= 0.0001f)
				approachDirection = (Focus() - Location()).xy();
			if (dot(approachDirection, approachDirection) <= 0.0001f)
				approachDirection = (Destination() - Location()).xy();

			const auto directions = PawnMovement::WallAdjustEscapeDirections(approachDirection);
			std::array<PawnMovement::WallAdjustCandidateProbe, 3> probes;
			const TraceFlags supportTraceFlags = {
				.movers = true,
				.world = true
			};
			const vec3 traceExtent(CollisionRadius(), CollisionRadius(), CollisionHeight());
			const float supportDepth = std::max(MaxStepHeight() * stepDownDeltaFactor, 1.0f);
			for (size_t index = 0; index < directions.size(); index++)
			{
				const vec3 candidateDelta(directions[index] * wallAdjustRecoveryDistance, 0.0f);
				PawnMovement::WallAdjustCandidateProbe& probe = probes[index];
				probe.SweepClear = TryMove(candidateDelta, true).Fraction == 1.0f;
				if (!probe.SweepClear)
					continue;

				const vec3 candidate = Location() + candidateDelta;
				const vec3 supportEnd = candidate - vec3(0.0f, 0.0f, supportDepth);
				const CollisionHit support = XLevel()->Collision.TraceFirstHit(
					candidate, supportEnd, this, traceExtent, supportTraceFlags);
				probe.WalkableSupport = support.Fraction < 1.0f && support.Normal.z >= 0.7071f;
				if (!probe.WalkableSupport)
					continue;

				const vec3 supportedCenter = candidate + (supportEnd - candidate) * support.Fraction;
				UZoneInfo* footZone = XLevel()->Model->FindRegion(
					supportedCenter - vec3(0.0f, 0.0f, CollisionHeight()), Level()).Zone;
				probe.NonPainFootRegion = footZone && !footZone->bPainZone();
			}

			const int selected = PawnMovement::SelectWallAdjustCandidate(probes,
				PawnMovement::WallAdjustCandidateOrder(observation.State.RepeatOrdinal));
			if (selected >= 0)
			{
				WallAdjustRecovery = PawnMovement::BeginWallAdjustSteering(
					WallAdjustRecovery, Location(), approachDirection,
					directions[static_cast<size_t>(selected)], wallAdjustSteeringDuration);
				if (!WallAdjustRecovery.SteeringActive)
				{
					Acceleration() = vec3(0.0f);
					MoveTimer() = -1.0f;
					WallAdjustForcedReplanCountValue++;
					return false;
				}
				bFromWall() = true;
				WallAdjustRecoveryAttemptCountValue++;
				return true;
			}

			Acceleration() = vec3(0.0f);
			MoveTimer() = -1.0f;
			WallAdjustForcedReplanCountValue++;
			return false;
		}
	}
	else
	{
		WallAdjustRecovery = {};
	}

	auto kneeHeight = CollisionHeight() * 0.45f;

	auto forwards = normalize(Acceleration().xy());

	auto afterJumpCollisionHit = TryMove(vec3(forwards, kneeHeight), true);
	if (afterJumpCollisionHit.Fraction == 1.0f)
	{
		// Obstacle can be jumped over. Attempt jumping.
		bFromWall() = false;
		Velocity().z = JumpZ();
		SetPhysics(PHYS_Falling);
		Destination() = Location() + vec3(forwards, kneeHeight);

		return true;
	}

	auto direction = Focus() - Location();
	auto rightSideVec = normalize(cross(direction, vec3(0, 0, 1)));
	auto rightSideDelta = PawnMovement::WallAdjustmentDelta(rightSideVec, CollisionRadius());
	auto rightSideTest = TryMove(rightSideDelta, true);
	if (rightSideTest.Fraction == 1)
	{
		// We can move to right instead
		bFromWall() = true;
		Destination() = Location() + rightSideDelta;

		return true;
	}

	auto leftSideDelta = -rightSideDelta;
	auto leftSideTest = TryMove(leftSideDelta, true);
	if (leftSideTest.Fraction >= 1)
	{
		// We can move to left instead
		bFromWall() = true;
		Destination() = Location() + leftSideDelta;

		return true;
	}

	// Cannot go anywhere from here
	return false;
}

void UPawn::AdvanceWallAdjustRecovery(float elapsed)
{
	const bool validContext = IsAutonomousPlayerBot(this)
		&& !bDeleteMe() && Health() > 0 && Physics() == PHYS_Walking;
	const PawnMovement::WallAdjustRecoveryAdvance advance = PawnMovement::AdvanceWallAdjustRecovery(
		WallAdjustRecovery, Location(), elapsed, wallAdjustRepeatRadius,
		wallAdjustRecoveryEscapeRadius, validContext);
	WallAdjustRecovery = advance.State;
	if (advance.Escaped)
		WallAdjustRecoverySuccessCountValue++;
}

bool UPawn::ApplyWallAdjustRecovery(const vec2& requestedDirection)
{
	const PawnMovement::WallAdjustSteeringRequest request =
		PawnMovement::EvaluateWallAdjustSteeringRequest(
			WallAdjustRecovery, requestedDirection, wallAdjustUnsafeAlignment);
	if (request == PawnMovement::WallAdjustSteeringRequest::Inactive)
		return false;
	if (request == PawnMovement::WallAdjustSteeringRequest::Clear)
	{
		WallAdjustRecovery = {};
		return false;
	}

	Acceleration() = vec3(WallAdjustRecovery.EscapeDirection * AccelRate(), 0.0f);
	return true;
}

vec3 UPawn::EAdjustJump()
{
	WalkingStepExplicitJumpRequested = true;
	UZoneInfo* zone = FootRegion().Zone;
	vec3 gravity = zone ? zone->ZoneGravity() : vec3(0.0f, 0.0f, -980.0f);

	const float dt = 0.05f;
	const float jumpZ = JumpZ();
	vec3 pos = Location();
	vec3 vel = vec3(0.0f, 0.0f, jumpZ);
	float time = 0.0f;
	const float maxSimTime = 5.0f;
	const float targetZ = Location().z;
	while (time < maxSimTime && pos.z < targetZ)
	{
		vel.z += gravity.z * dt;
		pos.z += vel.z * dt;
		time += dt;
		if (pos.z >= targetZ) break;
	}

	vec3 target = Focus();
	if (dot(target - Location(), target - Location()) < 0.001f)
		target = Destination();
	vec3 horizontalDir = normalize(target - Location());
	horizontalDir.z = 0.0f;

	vec3 horizontalVel = horizontalDir * (length(target - Location()) / std::max(time, 0.001f));

	float groundSpeed = GroundSpeed();
	float horizSpeed = length(horizontalVel);
	if (horizSpeed > groundSpeed)
		horizontalVel = horizontalVel * (groundSpeed / horizSpeed);

	return horizontalVel + vec3(0.0f, 0.0f, jumpZ);
}

bool UPawn::LineOfSightTo(UActor* other, bool ignoreDistance)
{
	if (!other)
		return false;

	// Additional 227 checks because of Pawn.SightCheckType being a variable there
	// Since we don't have any 227-only fields added yet, this part remains as a proof of concept
	// if (engine->packages->IsUnreal1_227() &&
	// 	(SightCheckType() == EPawnSightCheck::SEE_None ||
	// 	(SightCheckType() == EPawnSightCheck::SEE_PlayersOnly && !Cast<UPawn>(other)->bIsPlayer())))
	// 	return false;

	if (!ignoreDistance && length(Location() - other->Location()) > SightRadius())
		return false;

	vec3 eye_pos = Location();
	eye_pos.z += BaseEyeHeight();

	auto& origin = other->Location();
	auto top = origin + vec3{ 0.f, 0.f, other->CollisionHeight() / 2 };
	auto bottom = origin - vec3{ 0.f, 0.f, other->CollisionHeight() / 2 };

	return FastTrace(origin, eye_pos) || FastTrace(top, eye_pos) || FastTrace(bottom, eye_pos);
}

bool UPawn::CanSee(UActor* other)
{
	if (!other)
		return false;

	// Two fields to keep in mind of:
	// float SightRadius: Maximum seeing distance
	// float PeripheralVision: Cosine of limits of peripheral vision

	auto& origin = other->Location();
	auto top = origin + vec3{ 0.f, 0.f, other->CollisionHeight() / 2 };
	auto bottom = origin - vec3{ 0.f, 0.f, other->CollisionHeight() / 2 };

	vec3 eye_pos = Location();
	eye_pos.z += BaseEyeHeight();

	// Cannot see if the actor is too far away from the sight radius
	if (length(origin - eye_pos) > SightRadius())
		return false;

	// Cannot see if the actor is outside of the peripheral vision angles
	vec3 orientation = Coords::Rotation(Rotation()).XAxis;

	// Calculate the cosine of the vectors
	// which is basically A dot B / (|A| * |B|), or just the dot products of the normalized versions of A and B
	float cosine = dot(normalize(orientation), normalize(origin));
	// PeripheralVision field is set dynamically during a game session
	// (for an example, see the function UnrealShare.Bots.PreSetMovement())
	// This can be a negative value too, which is probably set to not take it into account
	float peripheralVision = PeripheralVision();

	if (peripheralVision > 0.0f && std::abs(cosine) > peripheralVision)
		return false;

	return FastTrace(origin, eye_pos) || FastTrace(top, eye_pos) || FastTrace(bottom, eye_pos);
}

bool UPawn::CanHearNoise(UActor* source, float loudness)
{
	UPawn* noisePawn = UObject::Cast<UPawn>(source->Instigator());
	if (!noisePawn->bIsPlayer() && (!noisePawn->Enemy() || !noisePawn->Enemy()->bIsPlayer()))
	{
		if (!IsA(source->Class->Name) && !source->IsA(Class->Name))
			return false;
	}
	else if (UObject::TryCast<UPlayerPawn>(this))
	{
		return false;
	}

	vec3 delta = Location() - source->Location();
	float dist2 = dot(delta, delta);

	if (!bIsPlayer() || !Level()->Game()->bTeamGame() || !noisePawn->bIsPlayer() ||
		(engine->LaunchInfo.ue1Version > 219 && (!PlayerReplicationInfo() || !noisePawn->PlayerReplicationInfo() || (PlayerReplicationInfo()->Team() != noisePawn->PlayerReplicationInfo()->Team()))))
	{
		if (dist2 > (4000.0f * 4000.0f) * (loudness * loudness))
			return false;

		float perceived = std::min(1200000.f / dist2, 2.0f);
		Stimulus() = loudness * perceived + Alertness() * std::min(0.5f, perceived);
		if (Stimulus() < HearingThreshold())
			return false;
	}
	else if (dist2 > (4000.0f * 4000.0f) * (loudness * loudness))
	{
		return false;
	}

	return !XLevel()->Collision.TraceAnyHit(source->Location(), Location(), source, false, true, false);
}

void UPawn::ClientHearSound(UActor* actor, int id, USound* sound, const vec3& soundLocation, const vec3& parameters)
{
	LogUnimplemented("UPawn.ClientHearSound()");
}

UActor* UPawn::PickAnyTarget(float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart)
{
	UActor* bestActor = nullptr;
	for (UActor* actor : XLevel()->Actors)
	{
		// We are only looking for targets that isn't a pawn (pawn uses PickTarget if it wants a pawn)
		if (!actor || actor == this || UObject::TryCast<UPawn>(actor) || !actor->bProjTarget())
			continue;

		if (CheckIfBestTarget(actor, bestAim, bestDist, FireDir, projStart))
			bestActor = actor;
	}
	return bestActor;
}

UActor* UPawn::PickTarget(float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart)
{
	UActor* bestActor = nullptr;
	UPlayerReplicationInfo* ourPlayerInfo = engine->LaunchInfo.ue1Version > 219 ? PlayerReplicationInfo() : nullptr;
	bool teamGame = ourPlayerInfo && Level()->Game()->bTeamGame();
	for (UPawn* pawn = Level()->PawnList(); pawn != nullptr; pawn = pawn->nextPawn())
	{
		// Skip dead pawns or ourselves
		if (pawn == this || pawn->Health() > 0)
			continue;

		// Skip team mates
		if (engine->LaunchInfo.ue1Version > 219)
		{
			auto pawnPlayerInfo = pawn->PlayerReplicationInfo();
			if (teamGame && pawnPlayerInfo && ourPlayerInfo->Team() == pawnPlayerInfo->Team())
				continue;
		}

		if (CheckIfBestTarget(pawn, bestAim, bestDist, FireDir, projStart))
			bestActor = pawn;
	}
	return bestActor;
}

bool UPawn::CheckIfBestTarget(UActor* actor, float& bestAim, float& bestDist, const vec3& FireDir, const vec3& projStart)
{
	// Ignore targets behind us
	vec3 delta = actor->Location() - projStart;
	float angle = dot(FireDir, delta);
	if (angle < 0.0f)
		return false;

	// Skip things too far away
	float distance = length(delta);
	if (distance == 0.0f || distance > 2500.0f)
		return false;

	// Skip if we already have a target closer to the direction we are facing
	angle /= distance;
	if (angle < bestAim)
		return false;

	// Skip if we can't see the target
	if (!LineOfSightTo(actor, false))
		return false;

	// OK, this is better than what we have
	bestAim = angle;
	bestDist = distance;
	return true;
}

UNavigationPoint* UPawn::SetRouteCache(const Array<UNavigationPoint*>& points)
{
	if (engine->LaunchInfo.ue1Version > 219)
	{
		auto cache = RouteCache();
		for (size_t i = 0; i < cache.size(); i++)
			cache[i] = (i < points.size()) ? points[i] : nullptr;
	}
	return !points.empty() ? points.front() : nullptr;
}

UActor* UPawn::PathSpecialHandling(const Array<UNavigationPoint*>& bestPath)
{
#if 0
	return SetRouteCache(bestPath);
#else
	IsInPathSpecialHandling = true;
	UActor* oldBestPoint = SetRouteCache(bestPath);
	if (!oldBestPoint)
	{
		IsInPathSpecialHandling = false;
		return nullptr;
	}

	UActor* bestPoint = oldBestPoint;

	if (oldBestPoint->IsEventEnabled(EventName::SpecialHandling))
	{
		bestPoint = UObject::Cast<UActor>(CallEvent(oldBestPoint, EventName::SpecialHandling, { ExpressionValue::ObjectValue(this) }).ToObject());
		if (!bCanDoSpecial())
			bestPoint = nullptr;
		SpecialGoal() = bestPoint;

		if (bestPoint && bestPoint != oldBestPoint)
		{
			if (!ActorReachable(bestPoint))
			{
				bestPoint = UObject::Cast<UActor>(FindPathToward(bestPoint, false));
			}
		}
	}
	else
	{
		if (SpecialGoal() == oldBestPoint)
			SpecialGoal() = nullptr;
	}

	IsInPathSpecialHandling = false;
	return bestPoint;
#endif
}


std::pair<Array<UNavigationPoint*>, int32_t> UPawn::FindPathToEndPoint(UNavigationPoint* start, int maxNodes)
{
	if ((start->bPlayerOnly() && !bIsPlayer()))
		return { {}, 0 };

	// If we can already reach the end point, just go there directly
	if (start->bEndPoint())
		return { { start }, 0 };

	struct Step
	{
		UNavigationPoint* navpoint;
		int prev;
		int32_t distance;
	};

	std::unordered_map<UNavigationPoint*, int32_t> shortestDistance;
	std::set<int> visited;
	Array<Step> steps;
	Array<size_t> stepEnds;
	const Array<LevelReachSpec>& reachSpecs = XLevel()->ReachSpecs;
	
	int radius = (int)CollisionRadius();
	int height = (int)CollisionHeight();

	// Search through the nav node links until we find an end point

	int prevStep = -1;
	UNavigationPoint* current = start;
	while (steps.size() < (size_t)maxNodes)
	{
		if (!current->bEndPoint())
		{
			for (int specIndex : current->upstreamPaths())
			{
				if (specIndex < 0 || (size_t)specIndex >= reachSpecs.size())
					break;
				const LevelReachSpec& reachSpec = reachSpecs[specIndex];

				// Note: startActor instead of endActor because upstreamPaths is the reverse travel direction
				UNavigationPoint* endActor = reachSpec.startActor;

				if (reachSpec.collisionRadius < radius || reachSpec.collisionHeight < height || reachSpec.bPruned)
					continue; // Skip nav node links that we can't pass through

				if ((endActor->bPlayerOnly() && !bIsPlayer()) || (endActor->bPlayerOnly() && !bIsPlayer()))
					continue; // Skip nav nodes only for the player if we aren't one

				// To do: check reachFlags

				// NavigationPoint.cost is populated by ClearPaths(), including a
				// bSpecialCost node's UnrealScript SpecialCost result.
				const int32_t accumulatedCost = prevStep >= 0 ? steps[prevStep].distance : 0;
				const int32_t nodeCost = engine->LaunchInfo.IsKlingonHonorGuard() ? 0 : current->cost();
				const int32_t distance = PawnPath::AccumulateCost(
					accumulatedCost, reachSpec.distance, nodeCost);

				// Is this distance shorter than last time we reached this point?
				int32_t& pointDistance = shortestDistance[endActor];
				if (pointDistance == 0 || distance < pointDistance)
				{
					// Yes. Track this path and reject any future paths going through here that are longer.
					pointDistance = distance;
					if (endActor->bEndPoint())
						stepEnds.push_back(steps.size());
					steps.push_back({ .navpoint = endActor, .prev = prevStep, .distance = distance });
				}
			}
		}

		prevStep++;
		if (prevStep == steps.size())
			break;

		current = steps[prevStep].navpoint;
	}

	if (stepEnds.empty())
		return { {}, 0 };

	std::array<ULiftExit*, 2> avoidedLiftExits = {};
	std::array<std::vector<const void*>, 2> avoidedLiftAdjacency;
	for (size_t entryIndex = 0; entryIndex < FailedNavigationMemory.Entries.size(); entryIndex++)
	{
		const PawnMovement::FailedNavigationMemoryEntry& entry =
			FailedNavigationMemory.Entries[entryIndex];
		if (entry.AvoidanceRemaining <= 0.0f)
			continue;
		UActor* avoidedTarget = const_cast<UActor*>(
			static_cast<const UActor*>(entry.Target));
		avoidedLiftExits[entryIndex] = avoidedTarget && !avoidedTarget->bDeleteMe()
			? UObject::TryCast<ULiftExit>(avoidedTarget) : nullptr;
		if (avoidedLiftExits[entryIndex])
			avoidedLiftAdjacency[entryIndex] = LiftExitLandingAdjacency(
				avoidedLiftExits[entryIndex], reachSpecs);
	}

	std::vector<PawnMovement::FailedNavigationEndpointCandidate> endpointCandidates;
	endpointCandidates.reserve(stepEnds.size());
	for (size_t stepIndex : stepEnds)
	{
		ULiftExit* liftExit = UObject::TryCast<ULiftExit>(steps[stepIndex].navpoint);
		std::array<bool, 2> avoidedEntries = {};
		if (liftExit)
		{
			const std::vector<const void*> adjacency = LiftExitLandingAdjacency(liftExit, reachSpecs);
			const PawnMovement::FailedNavigationLiftExitTopology candidateTopology = {
				.Zone = liftExit->Region().Zone,
				.X = liftExit->Location().x,
				.Y = liftExit->Location().y,
				.Z = liftExit->Location().z,
				.AdjacentLandingNodes = adjacency
			};
			for (size_t entryIndex = 0; entryIndex < avoidedLiftExits.size(); entryIndex++)
			{
				ULiftExit* avoidedLiftExit = avoidedLiftExits[entryIndex];
				if (!avoidedLiftExit)
					continue;
				const PawnMovement::FailedNavigationLiftExitTopology avoidedTopology = {
					.Zone = avoidedLiftExit->Region().Zone,
					.X = avoidedLiftExit->Location().x,
					.Y = avoidedLiftExit->Location().y,
					.Z = avoidedLiftExit->Location().z,
					.AdjacentLandingNodes = avoidedLiftAdjacency[entryIndex]
				};
				avoidedEntries[entryIndex] =
					PawnMovement::AreFailedNavigationLiftExitsOnSameLanding(
						candidateTopology, avoidedTopology,
						failedNavigationLiftExitLandingRadius,
						failedNavigationLiftExitLandingStepHeights * MaxStepHeight());
			}
		}
		endpointCandidates.push_back({
			.Endpoint = steps[stepIndex].navpoint,
			.AvoidedEntries = avoidedEntries,
			.Cost = steps[stepIndex].distance,
			.StableOrder = stepIndex
		});
	}
	const PawnMovement::FailedNavigationEndpointSelection endpointSelection =
		PawnMovement::SelectFailedNavigationEndpoint(
			FailedNavigationMemory, endpointCandidates,
			engine->IsBotBenchmarkFailedNavigationAvoidanceEnabled()
				? failedNavigationFirstHopCost : 0);
	if (!endpointSelection.Found)
		return { {}, 0 };
	FailedNavigationRoutePenaltyApplicationCountValue += endpointSelection.PenaltyApplications;
	const size_t selectedStep = stepEnds[endpointSelection.CandidateIndex];

	// Extract the final path:
	Array<UNavigationPoint*> path;
	int currentStep = (int)selectedStep;
	while (currentStep >= 0)
	{
		const Step& step = steps[currentStep];

		// Skip path parts we already are touching
		float minDist = (float)radius;
		vec3 d = step.navpoint->Location() - Location();
		float heightDiff = step.navpoint->Location().z - Location().z;
		if (dot(d, d) < minDist * minDist && std::abs(heightDiff) < (float)height)
		{
			path.clear();
		}
		else
		{
			path.push_back(step.navpoint);
		}

		currentStep = step.prev;
	}
	path.push_back(start);

	return { path, endpointSelection.AdjustedCost };
}

void UPawn::ClearPaths()
{
	for (UNavigationPoint* cur = Level()->NavigationPointList(); cur; cur = cur->nextNavigationPoint())
	{
		cur->bEndPoint() = false;
		if (!engine->LaunchInfo.IsKlingonHonorGuard())
		{
			if (cur->bSpecialCost())
				cur->cost() = CallEvent(cur, "SpecialCost", { ExpressionValue::ObjectValue(this) }).ToInt();
			else
				cur->cost() = cur->ExtraCost();
		}
	}
}

UObject* UPawn::FindRandomDest()
{
	// Find initial navpoints reachable from our location
	int maxActorReachableCalls = 8; // upper bound for how expensive this can get
	vec3 eyePos = Location();
	eyePos.z += BaseEyeHeight();
	std::vector<UNavigationPoint*> reachablePoints;
	for (UNavigationPoint* navPoint = Level()->NavigationPointList(); navPoint && reachablePoints.size() < maxActorReachableCalls; navPoint = navPoint->nextNavigationPoint())
	{
		if ((navPoint->bPlayerOnly() && !bIsPlayer()) || (navPoint->bPlayerOnly() && !bIsPlayer()))
			continue; // Skip nav nodes only for the player if we aren't one

		float maxDist = 1000.0;
		vec3 d = navPoint->Location() - Location();
		if (dot(d, d) > maxDist * maxDist)
			continue; // Ignore things too far away

		if (!ActorReachable(navPoint))
			continue;

		navPoint->bEndPoint() = true;
		reachablePoints.push_back(navPoint);
	}

	if (reachablePoints.empty())
		return nullptr;

	// Add all navpoints reachable via reachspecs from what we can already reached
	const Array<LevelReachSpec>& reachSpecs = XLevel()->ReachSpecs;
	int radius = (int)CollisionRadius();
	int height = (int)CollisionHeight();
	for (size_t i = 0; i < reachablePoints.size(); i++)
	{
		UNavigationPoint* navPoint = reachablePoints[i];

		for (int specIndex : navPoint->Paths())
		{
			if (specIndex < 0 || (size_t)specIndex >= reachSpecs.size())
				break;
			const LevelReachSpec& reachSpec = reachSpecs[specIndex];

			if (reachSpec.endActor->bEndPoint())
				continue; // Already processed

			if (reachSpec.collisionRadius < radius || reachSpec.collisionHeight < height || reachSpec.bPruned)
				continue; // Skip nav node links that we can't pass through

			if ((reachSpec.endActor->bPlayerOnly() && !bIsPlayer()) || (reachSpec.endActor->bPlayerOnly() && !bIsPlayer()))
				continue; // Skip nav nodes only for the player if we aren't one

			// To do: check reachFlags

			reachSpec.endActor->bEndPoint() = true;
			reachablePoints.push_back(reachSpec.endActor);
		}
	}

	// Pick a random point from our candidates
	float randomValue = rand() / (float)RAND_MAX;
	int index = (int)std::round(randomValue * (float)(reachablePoints.size() - 1));
	return reachablePoints[index];
}

UObject* UPawn::FindPathTo(const vec3& aPoint, bool bSinglePath)
{
	return FindPathToward(FindClosestNavPoint(aPoint), bSinglePath);
}

bool UPawn::MarkReachableNavEndPoints()
{
	int maxActorReachableCalls = 8; // upper bound for how expensive this can get
	int endPointsFound = 0;
	for (UNavigationPoint* navPoint = Level()->NavigationPointList(); navPoint; navPoint = navPoint->nextNavigationPoint())
	{
		navPoint->bEndPoint() = false;

		if (endPointsFound < maxActorReachableCalls)
		{
			if ((navPoint->bPlayerOnly() && !bIsPlayer()) || (navPoint->bPlayerOnly() && !bIsPlayer()))
				continue; // Skip nav nodes only for the player if we aren't one

			float maxDist = 1000.0;
			vec3 d = navPoint->Location() - Location();
			if (dot(d, d) > maxDist * maxDist)
				continue; // Ignore things too far away

			if (!ActorReachable(navPoint))
				continue;

			navPoint->bEndPoint() = true;
			endPointsFound++;
		}
	}

	return endPointsFound > 0;
}

float UPawn::AICanHear(UActor* other, std::optional<float> volume, std::optional<float> radius)
{
	LogUnimplemented("Pawn.AICanHear() [Deus Ex]");
	return 0.0f;
}

float UPawn::AICanSee(UActor* other, std::optional<float> visibility, std::optional<bool> bCheckVisibility, std::optional<bool> bCheckDir, std::optional<bool> bCheckCylinder, std::optional<bool> bCheckLOS)
{
	LogUnimplemented("Pawn.AICanSee() [Deus Ex]");
	return 0.0f;
}

float UPawn::AICanSmell(UActor* other, std::optional<float> smell)
{
	LogUnimplemented("Pawn.AICanSmell() [Deus Ex]");
	return 0.0f;
}

UObject* UPawn::FindPathToward(UObject* anActor, bool singlePath)
{
	if (auto aNavPoint = UObject::TryCast<UNavigationPoint>(anActor))
	{
		if (!MarkReachableNavEndPoints())
			return SetRouteCache({});
		if (!IsInPathSpecialHandling)
			return PathSpecialHandling(FindPathToEndPoint(aNavPoint, 1000).first);
		return SetRouteCache({});
	}
	else if (auto actor = UObject::TryCast<UActor>(anActor))
	{
		return FindPathToward(FindClosestNavPoint(actor->Location()), singlePath);
	}
	else
	{
		return SetRouteCache({});
	}
}

UNavigationPoint* UPawn::FindClosestNavPoint(vec3 location)
{
	// Order nav points by distance
	std::vector<std::pair<UNavigationPoint*, float>> navPoints;
	for (UNavigationPoint* navPoint = Level()->NavigationPointList(); navPoint; navPoint = navPoint->nextNavigationPoint())
	{
		if ((navPoint->bPlayerOnly() && !bIsPlayer()) || (navPoint->bPlayerOnly() && !bIsPlayer()))
			continue; // Skip nav nodes only for the player if we aren't one

		float maxDist = 500;
		vec3 d = navPoint->Location() - location;
		float distsqr = dot(d, d);
		if (distsqr > maxDist * maxDist)
			continue; // Ignore things too far away

		navPoints.push_back({ navPoint, distsqr });
	}

	std::sort(navPoints.begin(), navPoints.end(), [](const auto& a, const auto& b) { return a.second < b.second; });

	size_t maxTraces = 4; // upper bound for how expensive this can get
	navPoints.resize(std::min(navPoints.size(), maxTraces));

	// Find the first reachable nav point
	for (auto& p : navPoints)
	{
		vec3 eyePos = p.first->Location();
		eyePos.z += BaseEyeHeight();
		if (FastTrace(location, eyePos))
			return p.first;
	}
	return nullptr;
}

UObject* UPawn::FindBestInventoryPath(bool predictRespawns, float& outBestWeight)
{
	if (!MarkReachableNavEndPoints())
	{
		outBestWeight = 0.0f;
		return SetRouteCache({});
	}

	float bestWeight = 0.0f;
	UInventorySpot* bestSpot = nullptr;
	Array<UNavigationPoint*> bestPath;

	for (UNavigationPoint* navPoint = Level()->NavigationPointList(); navPoint; navPoint = navPoint->nextNavigationPoint())
	{
		auto invSpot = UObject::TryCast<UInventorySpot>(navPoint);
		if (!invSpot)
			continue;
		auto inv = invSpot->markedItem();
		if (!inv)
			continue;

		if (inv->GetStateName() != "PickUp")
			continue;

		float desire = CallEvent(inv, "BotDesireability", { ExpressionValue::ObjectValue(this) }).ToFloat();
		if (desire > 0.0f)
		{
			auto [path, pathDist] = FindPathToEndPoint(invSpot, 1000);

			// To do: how to take path costs into account?
			//int cost = 0;
			//for (auto nav : path)
			//	cost += nav->cost();

			float distance = std::max((float)pathDist, 1.0f);
			float weight = desire / distance;

			if (!bestSpot || weight > bestWeight)
			{
				bestSpot = invSpot;
				bestWeight = weight;
				bestPath = std::move(path);
			}
		}
	}

	if (bestSpot)
	{
		outBestWeight = bestWeight;
		return PathSpecialHandling(bestPath);
	}
	else
	{
		outBestWeight = 0.0f;
		return SetRouteCache({});
	}
}

void UPawn::InitActorZone()
{
	UActor::InitActorZone();

	FootRegion() = FindRegion({ 0.0f, 0.0f, -CollisionHeight() });
	HeadRegion() = FindRegion({ 0.0f, 0.0f, EyeHeight() });

	if (engine->LaunchInfo.ue1Version > 219 && PlayerReplicationInfo())
		PlayerReplicationInfo()->PlayerZone() = Region().Zone;
}

void UPawn::UpdateActorZone()
{
	UActor::UpdateActorZone();
	CommitPendingFallingHazardSweepAtFootBoundary();

	PointRegion oldfootregion = FootRegion();
	PointRegion newfootregion = FindRegion({ 0.0f, 0.0f, -CollisionHeight() });
	if (oldfootregion.Zone != newfootregion.Zone)
	{
		CallEvent(this, EventName::FootZoneChange, { ExpressionValue::ObjectValue(newfootregion.Zone) });
		RecoverFallingHazardCallbackReturn();
	}

	// FootZoneChange reads FootRegion as the old region, so publish the new region afterward.
	FootRegion() = newfootregion;
	if (oldfootregion.Zone != newfootregion.Zone)
		ObserveHarmfulZoneEscapeBoundary(oldfootregion.Zone, newfootregion.Zone, true);

	PointRegion oldheadregion = HeadRegion();
	PointRegion newheadregion = FindRegion({ 0.0f, 0.0f, EyeHeight() });
	if (oldheadregion.Zone != newheadregion.Zone)
	{
		FinishFallingHazardCallbackBoundary();
		CallEvent(this, EventName::HeadZoneChange, { ExpressionValue::ObjectValue(newheadregion.Zone) });
		RecoverFallingHazardCallbackReturn();
	}

	// HeadZoneChange likewise reads HeadRegion as the old region.
	HeadRegion() = newheadregion;

	if (engine->LaunchInfo.ue1Version > 219 && PlayerReplicationInfo())
		PlayerReplicationInfo()->PlayerZone() = Region().Zone;
}

void UPawn::Tick(float elapsed)
{
	const uint8_t physicsAtPawnTickEntry = Physics();
	UZoneInfo* seamFootZone = FootRegion().Zone;
	if (bDeleteMe() || Health() <= 0 || Physics() != PHYS_Falling
		|| !seamFootZone || seamFootZone->bPainZone() || seamFootZone->bWaterZone())
		FallingSeamEpisode = {};
	if (bDeleteMe() || Health() <= 0)
		WalkingStepPreflightEpisode = {};

	std::array<bool, 2> rememberedNavigationTargetsLive = {};
	for (size_t index = 0; index < FailedNavigationMemory.Entries.size(); index++)
	{
		UActor* rememberedNavigationTarget = const_cast<UActor*>(
			static_cast<const UActor*>(FailedNavigationMemory.Entries[index].Target));
		rememberedNavigationTargetsLive[index] =
			rememberedNavigationTarget && !rememberedNavigationTarget->bDeleteMe();
	}
	FailedNavigationMemory = PawnMovement::AdvanceFailedNavigationMemory(
		FailedNavigationMemory, elapsed, Location().x, Location().y,
		rememberedNavigationTargetsLive, failedNavigationEscapeRadius);
	ObserveMoveStallWatchdog(elapsed);
	AdvancePainLedgeRecovery(elapsed);
	AdvanceWallAdjustRecovery(elapsed);
	MoveTimer() -= elapsed;

	if (StateFrame)
	{
		if (StateFrame->LatentState == LatentRunState::MoveTo)
		{
			TickRotateTo(Focus());
			if (TickMoveTo(Destination(), elapsed))
				StateFrame->LatentState = LatentRunState::Continue;
		}
		else if (StateFrame->LatentState == LatentRunState::MoveToward)
		{
			if (MoveTarget())
			{
				Focus() = MoveTarget()->Location();
				const bool advancedTactics = engine->LaunchInfo.ue1Version >= 436 && bAdvancedTactics();
				const PawnMovement::MoveTowardPoll poll = PawnMovement::PrepareMoveTowardPoll(MoveTarget()->Location(), advancedTactics);
				Destination() = poll.Destination;
				if (poll.ApplyAdvancedTactics)
				{
					const vec3 unalteredDestination = Destination();
					CallEvent(this, EventName::AlterDestination);
					const bool targetUsable = MoveTarget() && !MoveTarget()->bDeleteMe();
					if (PawnMovement::AbortMoveTowardAfterAlterDestination(bDeleteMe(), targetUsable))
					{
						StateFrame->LatentState = LatentRunState::Continue;
						return;
					}

					const vec3 preferredDestination = Destination();
					const vec3 oppositeDestination = PawnMovement::ReflectTacticalDestination(
						unalteredDestination, preferredDestination);
					auto clearForPawnExtent = [this](const vec3& candidate)
					{
						vec3 direction = candidate - Location();
						direction.z = 0.0f;
						const float distance = length(direction);
						if (distance <= 0.0001f)
							return true;
						const vec3 lookahead = direction * (std::min(distance, 100.0f) / distance);
						return TryMove(lookahead, true).Fraction == 1.0f;
					};
					const bool preferredClear = clearForPawnExtent(preferredDestination);
					const bool oppositeClear = !preferredClear && clearForPawnExtent(oppositeDestination);
					Destination() = PawnMovement::SelectTacticalDestination(unalteredDestination,
						preferredDestination, preferredClear, oppositeClear);
				}
				TickRotateTo(Focus());
				if (TickMoveTo(Destination(), elapsed, MoveTarget()))
				{
					StateFrame->LatentState = LatentRunState::Continue;
				}
			}
			else
			{
				StateFrame->LatentState = LatentRunState::Continue;
			}
		}
		else if (StateFrame->LatentState == LatentRunState::StrafeTo)
		{
			TickRotateTo(Focus());
			if (TickMoveTo(Destination(), elapsed))
				StateFrame->LatentState = LatentRunState::Continue;
		}
		else if (StateFrame->LatentState == LatentRunState::StrafeFacing)
		{
			if (engine->LaunchInfo.ue1Version > 219 && FaceTarget())
			{
				TickRotateTo(Focus());
				vec3 oldDest = Destination();
				if (TickMoveTo(Destination(), elapsed))
					StateFrame->LatentState = LatentRunState::Continue;
				Destination() = oldDest;
			}
			else
			{
				StateFrame->LatentState = LatentRunState::Continue;
			}
		}
		else if (StateFrame->LatentState == LatentRunState::TurnTo)
		{
			if (TickRotateTo(Focus()))
				StateFrame->LatentState = LatentRunState::Continue;
		}
		else if (StateFrame->LatentState == LatentRunState::TurnToward)
		{
			if (engine->LaunchInfo.ue1Version > 219 && FaceTarget())
			{
				if (TickRotateTo(FaceTarget()->Location()))
					StateFrame->LatentState = LatentRunState::Continue;
			}
			else
			{
				StateFrame->LatentState = LatentRunState::Continue;
			}
		}
		else if (StateFrame->LatentState == LatentRunState::WaitForLanding)
		{
			if (Physics() != PHYS_Falling)
			{
				StateFrame->LatentState = LatentRunState::Continue;
			}
			else
			{
				// To do: need to send a LongFall event if the fall state lasts long enough
			}
		}
	}

	if (physicsAtPawnTickEntry != PHYS_Falling
		&& Physics() == PHYS_Falling)
	{
		QueueFallingHazardForecastSource(
			PawnMovement::FallingHazardForecastSource::ExternalImpulseCommit);
	}
	UActor::Tick(elapsed);
	WalkingStepExplicitJumpRequested = false;
	if (bDeleteMe() || Health() <= 0)
		WalkingStepPreflightEpisode = {};

	if (bIsPlayer() && Role() >= ROLE_AutonomousProxy)
	{
		if (engine->LaunchInfo.ue1Version < 400 || bViewTarget())
			CallEvent(this, EventName::UpdateEyeHeight, { ExpressionValue::FloatValue(elapsed) });
		else
			ViewRotation() = Rotation();
	}

	if (Weapon())
	{
		Weapon()->Location() = Location();
		Weapon()->UpdateActorZone();
	}

	if (Role() == ROLE_Authority)
	{
		if (PainTime() > 0.0f)
		{
			PainTime() = std::max(PainTime() - elapsed, 0.0f);
			if (PainTime() == 0.0f)
				CallEvent(this, EventName::PainTimer);
		}
		if (SpeechTime() > 0.0f)
		{
			SpeechTime() = std::max(SpeechTime() - elapsed, 0.0f);
			if (SpeechTime() == 0.0f)
				CallEvent(this, EventName::SpeechTimer);
		}
		if (engine->LaunchInfo.ue1Version >= 436 && bAdvancedTactics())
			CallEvent(this, EventName::UpdateTactics, { ExpressionValue::FloatValue(elapsed) });
	}
}

void UPawn::TickRotating(float elapsed)
{
	if (Physics() == PHYS_Spider)
		return;

	bRotateToDesired() = true;
	bFixedRotationDir() = false;

	if (Rotation() == DesiredRotation())
		return;

	Rotator rot = Rotation();

	if ((DesiredRotation().Yaw & 0xffff) != (rot.Yaw & 0xffff))
	{
		rot.Yaw = Rotator::TurnToShortest(rot.Yaw, DesiredRotation().Yaw, (int)std::abs(RotationRate().Yaw * elapsed));
	}

	if ((DesiredRotation().Pitch & 0xffff) != (rot.Pitch & 0xffff))
	{
		rot.Pitch = DesiredRotation().Pitch & 0xffff;
		if (rot.Pitch < 0x8000)
		{
			rot.Pitch = std::max(rot.Pitch, RotationRate().Pitch);
		}
		else if (rot.Pitch < 0x10000 - RotationRate().Pitch)
		{
			rot.Pitch = 0x10000 - RotationRate().Pitch;
		}
	}

	// To do: apply RotationRate().Roll

	Rotation() = rot;

	if (Rotation() == DesiredRotation())
	{
		CallEvent(this, EventName::EndedRotation);
	}
}

bool UPawn::TickRotateTo(const vec3& target)
{
	if (Physics() == PHYS_Spider)
		return true;

	DesiredRotation() = Rotator::FromVector(target - Location());

	if (Physics() == PHYS_Walking && (!MoveTarget() || !MoveTarget()->IsA("Pawn")))
	{
		DesiredRotation().Pitch = 0;
	}

	int doneAngle = 2000;
	return (std::abs(DesiredRotation().Yaw - (Rotation().Yaw & 0xffff)) < doneAngle) || (std::abs(DesiredRotation().Yaw - (Rotation().Yaw & 0xffff)) > 0xffff - doneAngle);
}

void UPawn::ResetHazardSwimEgressObservation()
{
	if (HazardWaterEgressObserver && HazardWaterEgressObserver->HasActiveEpisode()
		&& IsFiniteVector(Location()) && IsFiniteVector(Destination()))
	{
		HazardWaterEgressObserver->Abandon(Location(),
			MoveTarget() ? MoveTarget()->Name.ToString() : std::string(), Destination());
	}
	HazardSwimEgress = {};
	HazardSwimEgressGate.Reset();
}

void UPawn::ResetFallingHazardRecovery()
{
	FallingHazardRecovery = {};
	FallingHazardRecoveryGate.Reset();
}

bool UPawn::AdvanceFallingHazardRecovery(float elapsed)
{
	const bool recoveryEnabled = engine->IsBotBenchmarkFallingHazardRecoveryEnabled();
	if (recoveryEnabled)
		FallingHazardRecoveryAdvanceCountValue++;
	const bool validContext = engine->IsBotBenchmarkFallingHazardRecoveryEnabled()
		&& engine->IsBotBenchmarkWalkingPreflightEnabled()
		&& IsStockAutonomousPlayerBot(this) && Role() == ROLE_Authority
		&& !bDeleteMe() && Health() > 0 && Physics() == PHYS_Falling
		&& FallingHazardObserver && FallingHazardObserver->HasActiveFallEpisode()
		&& !bJustTeleported() && std::isfinite(elapsed) && elapsed > 0.0f;
	if (!validContext)
	{
		if (recoveryEnabled)
		{
			FallingHazardRecoveryContextRejectedCountValue++;
			if (!FallingHazardObserver
				|| !FallingHazardObserver->HasActiveFallEpisode())
			{
				FallingHazardRecoveryNoActiveFallEpisodeCountValue++;
			}
		}
		ResetFallingHazardRecovery();
		return false;
	}

	const uint64_t lifeId = FallingHazardObserver->CurrentLife().Value;
	const uint64_t fallEpisodeId = FallingHazardObserver->CurrentFallEpisode().Value;
	if (lifeId == 0 || fallEpisodeId == 0)
	{
		ResetFallingHazardRecovery();
		return false;
	}
	if (FallingHazardRecovery.AnchorKnown
		&& (FallingHazardRecovery.LifeId != lifeId
			|| FallingHazardRecovery.FallEpisodeId != fallEpisodeId))
	{
		ResetFallingHazardRecovery();
	}

	const std::array<UZoneInfo*, 3> zones = {
		Region().Zone, FootRegion().Zone, HeadRegion().Zone
	};
	const bool safeCurrent = IsFiniteVector(Location())
		&& std::all_of(zones.begin(), zones.end(),
			[](UZoneInfo* zone) { return IsSafeFallingHazardRecoveryZone(zone); });
	if (!FallingHazardRecovery.AnchorKnown && safeCurrent)
	{
		FallingHazardRecovery.AnchorKnown = true;
		FallingHazardRecovery.Anchor = Location();
		FallingHazardRecovery.LifeId = lifeId;
		FallingHazardRecovery.FallEpisodeId = fallEpisodeId;
	}

	if (!FallingHazardRecovery.ActionActive)
	{
		if (!FallingHazardObserver->HasPromotedSingleHarmfulFallPrefix())
		{
			FallingHazardRecoveryNoPrefixCountValue++;
			return false;
		}
		if (!FallingHazardRecovery.PromotionObserved)
		{
			FallingHazardRecovery.PromotionObserved = true;
			FallingHazardRecoveryPromotionCountValue++;
		}
		const BotAI::FallingHazardRecoveryEntry entry = {
			lifeId, fallEpisodeId, true, FallingHazardRecovery.AnchorKnown,
			FallingHazardRecovery.Anchor.x, FallingHazardRecovery.Anchor.y,
			Location().x, Location().y
		};
		const BotAI::FallingHazardRecoveryEligibility eligibility =
			FallingHazardRecoveryGate.Evaluate(entry);
		if (eligibility == BotAI::FallingHazardRecoveryEligibility::Ineligible)
		{
			if (!FallingHazardRecovery.AnchorRejectedObserved)
			{
				FallingHazardRecovery.AnchorRejectedObserved = true;
				FallingHazardRecoveryAnchorRejectedCountValue++;
			}
			return false;
		}
		if (eligibility != BotAI::FallingHazardRecoveryEligibility::Authorized)
			return false;
		FallingHazardRecoveryEligibleCountValue++;
		if (!engine->IsBotBenchmarkFallingHazardRecoveryLiveEnabled())
			return false;
		FallingHazardRecovery.ActionActive = true;
		FallingHazardRecovery.ActiveSeconds = 0.0f;
	}
	if (!engine->IsBotBenchmarkFallingHazardRecoveryLiveEnabled())
	{
		FallingHazardRecovery.ActionActive = false;
		return false;
	}

	if (!safeCurrent
		|| FallingHazardRecovery.ActiveSeconds
			>= fallingHazardRecoveryMaximumDuration)
	{
		if (FallingHazardRecovery.ActiveSeconds
			>= fallingHazardRecoveryMaximumDuration)
			FallingHazardRecoveryTimeoutCountValue++;
		FallingHazardRecovery.ActionActive = false;
		return false;
	}
	const vec3 delta(FallingHazardRecovery.Anchor.x - Location().x,
		FallingHazardRecovery.Anchor.y - Location().y, 0.0f);
	const float distance = length(delta);
	if (!IsFiniteVector(delta) || !std::isfinite(distance)
		|| distance < BotAI::FallingHazardRecoveryMinimumAnchorDistance
		|| distance > BotAI::FallingHazardRecoveryMaximumAnchorDistance)
	{
		FallingHazardRecovery.ActionActive = false;
		return false;
	}
	if (TryMove(delta, true).Fraction != 1.0f)
	{
		FallingHazardRecoveryProbeRejectedCountValue++;
		FallingHazardRecovery.ActionActive = false;
		return false;
	}
	if (FallingHazardRecovery.ActiveSeconds == 0.0f)
		FallingHazardRecoveryLiveApplyCountValue++;
	const float maxAccel = engine->LaunchInfo.ue1Version > 219
		? AirControl() * AccelRate() : 0.0f;
	if (!std::isfinite(maxAccel) || maxAccel <= 0.0f)
	{
		FallingHazardRecovery.ActionActive = false;
		return false;
	}
	Acceleration() = normalize(delta) * maxAccel;
	FallingHazardRecoveryLiveActiveTickCountValue++;
	FallingHazardRecovery.ActiveSeconds += elapsed;
	return true;
}

void UPawn::RecordFallingHazardRecoveryHarmfulEntry()
{
	if (FallingHazardRecovery.ActionActive)
	{
		FallingHazardRecoveryHarmfulEntryCountValue++;
		FallingHazardRecovery.ActionActive = false;
	}
}

void UPawn::EndHazardSwimEgressSwimSession()
{
	ResetHazardSwimEgressObservation();
}

void UPawn::BeginHazardSwimEgressFallingTick()
{
	if (HazardSwimEgress.HarmfulWaterEpisodeActive)
	{
		ObserveHazardSwimEgressLiveSteerShadowDecision(
			BotAI::EvaluateHazardSwimEgressLiveSteer({
				engine->IsBotBenchmarkHazardSwimEgressLiveEnabled(),
				IsStockAutonomousPlayerBot(this) && Role() == ROLE_Authority,
				!bDeleteMe() && Health() > 0, BotAI::HazardSwimEgressPhysics::Falling,
				true, HazardSwimEgress.LiveActionAuthorized,
				HazardSwimEgress.LiveProbeRejected,
				HazardSwimEgress.AnchorKnown && IsFiniteVector(HazardSwimEgress.Anchor),
				HazardSwimEgress.Source == HazardSwimEgressState::AnchorSource::FallingPreMove,
				true, false, 0.0, BotAI::HazardSwimEgressProbe::NotRun }));
	}
	const bool validContext = engine->IsBotBenchmarkHazardSwimEgressEnabled()
		&& IsStockAutonomousPlayerBot(this) && Role() == ROLE_Authority
		&& !bDeleteMe() && Health() > 0 && Physics() == PHYS_Falling;
	if (!validContext)
	{
		ResetHazardSwimEgressObservation();
		return;
	}

	const std::array<UZoneInfo*, 3> zones = {
		Region().Zone, FootRegion().Zone, HeadRegion().Zone
	};
	const bool waterTransit = std::all_of(zones.begin(), zones.end(),
		[](UZoneInfo* zone) { return zone != nullptr; })
		&& std::any_of(zones.begin(), zones.end(),
			[](UZoneInfo* zone) { return zone->bWaterZone(); });
	if (!waterTransit && !HazardSwimEgress.SwimmingSessionObserved)
		ResetHazardSwimEgressObservation();
}

void UPawn::CaptureHazardSwimEgressFallingAnchorBeforePhysicsMove(bool directMove)
{
	const bool validContext = engine->IsBotBenchmarkHazardSwimEgressEnabled()
		&& IsStockAutonomousPlayerBot(this) && Role() == ROLE_Authority
		&& !bDeleteMe() && Health() > 0 && Physics() == PHYS_Falling;
	if (!validContext || !directMove || bJustTeleported())
	{
		if (validContext && !bJustTeleported())
			HazardSwimEgress.TransitionSource =
				PawnMovement::HazardWaterEgressTransitionSource::FallingNonDirectSweep;
		return;
	}
	HazardSwimEgress.TransitionSource =
		PawnMovement::HazardWaterEgressTransitionSource::FallingDirectSweep;

	const std::array<UZoneInfo*, 3> zones = {
		Region().Zone, FootRegion().Zone, HeadRegion().Zone
	};
	const bool safeAnchor = IsFiniteVector(Location())
		&& std::all_of(zones.begin(), zones.end(), [](UZoneInfo* zone)
		{
			return zone && zone->bStatic() && !zone->bWaterZone()
				&& !IsExactHarmfulZone(zone);
		});
	if (!safeAnchor)
		return;

	HazardSwimEgress.Anchor = Location();
	HazardSwimEgress.AnchorKnown = true;
	HazardSwimEgress.Source = HazardSwimEgressState::AnchorSource::FallingPreMove;
	HazardSwimEgressFallingPreMoveAnchorCaptureCountValue++;
}

void UPawn::CaptureHazardSwimEgressAnchorBeforePhysicsMove()
{
	const bool eligibleBot = engine->IsBotBenchmarkHazardSwimEgressEnabled()
		&& IsStockAutonomousPlayerBot(this) && Role() == ROLE_Authority
		&& !bDeleteMe() && Health() > 0;
	if (!eligibleBot)
	{
		ResetHazardSwimEgressObservation();
		return;
	}
	if (Physics() != PHYS_Swimming)
		return;
	if (HazardSwimEgress.TransitionSource
		== PawnMovement::HazardWaterEgressTransitionSource::Unknown)
	{
		HazardSwimEgress.TransitionSource =
			PawnMovement::HazardWaterEgressTransitionSource::SwimmingMotion;
	}

	const std::array<UZoneInfo*, 3> zones = {
		Region().Zone, FootRegion().Zone, HeadRegion().Zone
	};
	const bool safeAnchor = IsFiniteVector(Location())
		&& std::all_of(zones.begin(), zones.end(), [](UZoneInfo* zone)
		{
			return zone && !IsExactHarmfulZone(zone);
		});
	if (safeAnchor)
	{
		HazardSwimEgress.Anchor = Location();
		HazardSwimEgress.AnchorKnown = true;
		HazardSwimEgress.Source = HazardSwimEgressState::AnchorSource::SafeSwimming;
	}
	HazardSwimEgress.SwimmingSessionObserved = true;
}

void UPawn::ObserveHazardSwimEgressAfterPhysicsMove()
{
	const bool eligibleBot = engine->IsBotBenchmarkHazardSwimEgressEnabled()
		&& IsStockAutonomousPlayerBot(this) && Role() == ROLE_Authority
		&& !bDeleteMe() && Health() > 0;
	if (!eligibleBot)
	{
		ResetHazardSwimEgressObservation();
		return;
	}
	if (Physics() != PHYS_Swimming)
		return;

	const std::array<UZoneInfo*, 3> zones = {
		Region().Zone, FootRegion().Zone, HeadRegion().Zone
	};
	UZoneInfo* primaryZone = Region().Zone;
	const bool staticWater = std::all_of(zones.begin(), zones.end(),
		[](UZoneInfo* zone) { return zone && zone->bStatic() && zone->bWaterZone(); });
	const bool exactHarmfulWater = staticWater && IsExactHarmfulZone(primaryZone);
	if (!exactHarmfulWater)
	{
		const bool primaryHarmfulWater = primaryZone && primaryZone->bStatic()
			&& primaryZone->bWaterZone() && IsExactHarmfulZone(primaryZone);
		if (HazardSwimEgress.ActionActive && HazardSwimEgress.LiveActionAuthorized
			&& primaryHarmfulWater)
		{
			// A pawn extent may straddle the water boundary while its primary region remains
			// damaging water. This is not an egress: preserve the already-authorized,
			// collision-probed acceleration overlay until the primary hazard actually clears.
			return;
		}
		if (HazardSwimEgress.HarmfulWaterEpisodeActive)
		{
			ObserveHazardSwimEgressLiveSteerShadowDecision(
				BotAI::EvaluateHazardSwimEgressLiveSteer({
					engine->IsBotBenchmarkHazardSwimEgressLiveEnabled(),
					IsStockAutonomousPlayerBot(this) && Role() == ROLE_Authority,
					!bDeleteMe() && Health() > 0, BotAI::HazardSwimEgressPhysics::Swimming,
					true, HazardSwimEgress.LiveActionAuthorized,
					HazardSwimEgress.LiveProbeRejected,
					HazardSwimEgress.AnchorKnown && IsFiniteVector(HazardSwimEgress.Anchor),
					HazardSwimEgress.Source == HazardSwimEgressState::AnchorSource::FallingPreMove,
					false, false, 0.0, BotAI::HazardSwimEgressProbe::NotRun }));
			if (HazardWaterEgressObserver && IsFiniteVector(Location())
				&& IsFiniteVector(Destination()))
			{
				HazardWaterEgressObserver->FinishEpisode(
					PawnMovement::HazardWaterEgressTerminal::PrimaryZoneCleared,
					Location(), MoveTarget() ? MoveTarget()->Name.ToString() : std::string(),
					Destination());
			}
			HazardSwimEgressExitCountValue++;
			if (HazardSwimEgress.ActionActive)
				HazardSwimEgressLiveSuccessfulExitCountValue++;
		}
		HazardSwimEgress.HarmfulWaterEpisodeActive = false;
		HazardSwimEgress.LiveActionAuthorized = false;
		HazardSwimEgress.ActionActive = false;
		HazardSwimEgressGate.Reset();
		return;
	}

	if (HazardSwimEgress.HarmfulWaterEpisodeActive)
	{
		if (HazardWaterEgressObserver)
			HazardWaterEgressObserver->ObservePosition(Location());
		return;
	}

	HazardSwimEgress.HarmfulWaterEpisodeActive = true;
	HazardSwimEgressEpisodeId++;
	HazardSwimEgressEpisodeCountValue++;
	if (!HazardWaterEgressObserver)
	{
		HazardWaterEgressObserver =
			std::make_unique<PawnMovement::HazardWaterEgressObserver>(Name.ToString());
	}
	PawnMovement::HazardWaterEgressEntry entry;
	entry.LifeId = HazardSwimEgressLifeId;
	entry.EpisodeId = HazardSwimEgressEpisodeId;
	entry.TransitionSource = HazardSwimEgress.TransitionSource;
	entry.AnchorKnown = HazardSwimEgress.AnchorKnown;
	entry.Anchor = HazardSwimEgress.Anchor;
	entry.EntryLocation = Location();
	entry.DamagePerSecond = static_cast<float>(primaryZone->DamagePerSec());
	entry.Destination = Destination();
	if (UActor* moveTarget = MoveTarget())
	{
		entry.MoveTargetName = moveTarget->Name.ToString();
		entry.MoveTargetLocationKnown = IsFiniteVector(moveTarget->Location());
		entry.MoveTargetLocation = moveTarget->Location();
	}
	HazardWaterEgressObserver->BeginEpisode(entry);
	ObserveHazardSwimEgressStaticWalkCertificate();
	if (!HazardSwimEgress.AnchorKnown || !IsFiniteVector(HazardSwimEgress.Anchor))
	{
		HazardSwimEgressNoAnchorRejectedCountValue++;
		return;
	}

	HazardSwimEgressEligibleCountValue++;
	if (HazardSwimEgress.Source == HazardSwimEgressState::AnchorSource::FallingPreMove)
		HazardSwimEgressFallingPreMoveAnchorUseCountValue++;
	const BotAI::HazardSwimEgressEligibility decision =
		HazardSwimEgressGate.Evaluate({ HazardSwimEgressLifeId,
			HazardSwimEgressEpisodeId, true, static_cast<double>(primaryZone->DamagePerSec()), true,
			HazardSwimEgress.Anchor.x, HazardSwimEgress.Anchor.y,
			HazardSwimEgress.Anchor.z, true, true });
	if (decision == BotAI::HazardSwimEgressEligibility::Authorized)
	{
		HazardSwimEgressAuthorizedCountValue++;
		ObserveHazardSwimEgressDirectNavigationCandidates();
		if (HazardWaterEgressObserver
			&& HazardSwimEgress.DirectNavBestCandidateLocationKnown)
		{
			HazardWaterEgressObserver->ObserveCandidate({
				HazardSwimEgress.DirectNavBestCandidateName,
				HazardSwimEgress.DirectNavBestCandidateLocation,
				HazardSwimEgress.DirectNavBestCandidateDistance });
		}
		HazardSwimEgress.LiveActionAuthorized =
			engine->IsBotBenchmarkHazardSwimEgressLiveEnabled()
			&& HazardSwimEgress.Source == HazardSwimEgressState::AnchorSource::FallingPreMove;
	}
	else if (decision == BotAI::HazardSwimEgressEligibility::Debounced)
		HazardSwimEgressDebouncedCountValue++;
}

void UPawn::ObserveHazardSwimEgressDirectNavigationCandidates()
{
	constexpr float maximumCandidateDistance = 1024.0f;
	constexpr size_t maximumCandidates = 32;
	if (!Level())
		return;
	for (UNavigationPoint* candidate = Level()->NavigationPointList(); candidate
		&& HazardSwimEgressDirectNavProbeCountValue < maximumCandidates;
		candidate = candidate->nextNavigationPoint())
	{
		if (candidate->bDeleteMe() || candidate->bPlayerOnly())
			continue;
		UZoneInfo* zone = candidate->Region().Zone;
		const vec3 delta = candidate->Location() - Location();
		if (!zone || !zone->bStatic() || zone->bWaterZone() || IsExactHarmfulZone(zone)
			|| !IsFiniteVector(delta) || length(delta) > maximumCandidateDistance)
		{
			continue;
		}
		HazardSwimEgressDirectNavProbeCountValue++;
		if (TryMove(delta, true).Fraction == 1.0f)
		{
			HazardSwimEgressDirectNavSafeCandidateCountValue++;
			const float distance = length(delta);
			if (distance < HazardSwimEgress.DirectNavBestCandidateDistance)
			{
				HazardSwimEgress.DirectNavBestCandidateDistance = distance;
				HazardSwimEgress.DirectNavBestCandidateName = candidate->Name.ToString();
				HazardSwimEgress.DirectNavBestCandidateLocationKnown = true;
				HazardSwimEgress.DirectNavBestCandidateLocation = candidate->Location();
			}
		}
	}
}

void UPawn::ObserveHazardSwimEgressStaticWalkCertificate()
{
	constexpr float maximumAnchorFirstHopDistance = 2048.0f;
	constexpr size_t maximumNavigationSnapshotNodes = 512;
	constexpr size_t maximumCertificateVisitedNodes = 64;
	if (!HazardWaterEgressObserver || !HazardWaterEgressObserver->HasActiveEpisode()
		|| !HazardSwimEgress.AnchorKnown || !IsFiniteVector(HazardSwimEgress.Anchor)
		|| !Level())
	{
		return;
	}

	PawnMovement::HazardWaterEgressRouteCertificateRequest request;
	request.PawnCollisionRadius = CollisionRadius();
	request.PawnCollisionHeight = CollisionHeight();
	request.MaximumVisitedNodes = maximumCertificateVisitedNodes;
	std::vector<UNavigationPoint*> navigationPoints;
	std::unordered_map<UNavigationPoint*, size_t> navigationIndexes;
	std::unordered_set<UNavigationPoint*> seen;
	for (UNavigationPoint* point = Level()->NavigationPointList(); point
		&& navigationPoints.size() < maximumNavigationSnapshotNodes;
		point = point->nextNavigationPoint())
	{
		if (!seen.insert(point).second)
			break;
		const vec3 delta = point->Location() - HazardSwimEgress.Anchor;
		UZoneInfo* zone = point->Region().Zone;
		const bool staticDry = zone && zone->bStatic() && !zone->bWaterZone()
			&& !IsExactHarmfulZone(zone);
		const bool liftOrTeleport = UObject::TryCast<ULiftExit>(point)
			|| UObject::TryCast<ULiftCenter>(point) || point->IsA("Teleporter");
		PawnMovement::HazardWaterEgressRouteNode node;
		node.Id = point->Name.ToString();
		node.StaticDry = staticDry;
		node.PlayerOnly = point->bPlayerOnly();
		node.LiftOrTeleport = liftOrTeleport;
		node.DirectFirstHopSweepClear = staticDry && !node.PlayerOnly && !liftOrTeleport
			&& IsFiniteVector(delta) && length(delta) <= maximumAnchorFirstHopDistance
			&& ProbeMoveCollision(HazardSwimEgress.Anchor, delta, true).Fraction == 1.0f;
		navigationIndexes[point] = navigationPoints.size();
		navigationPoints.push_back(point);
		request.Nodes.push_back(std::move(node));
	}

	const Array<LevelReachSpec>& reachSpecs = XLevel()->ReachSpecs;
	for (size_t sourceIndex = 0; sourceIndex < navigationPoints.size(); sourceIndex++)
	{
		UNavigationPoint* source = navigationPoints[sourceIndex];
		for (int reachSpecIndex : source->Paths())
		{
			if (reachSpecIndex < 0 || static_cast<size_t>(reachSpecIndex) >= reachSpecs.size())
				break;
			const LevelReachSpec& reachSpec = reachSpecs[reachSpecIndex];
			auto destination = navigationIndexes.find(reachSpec.endActor);
			if (reachSpec.startActor != source || destination == navigationIndexes.end())
				continue;
			request.Edges.push_back({ sourceIndex, destination->second,
				static_cast<float>(reachSpec.distance), static_cast<float>(reachSpec.collisionRadius),
				static_cast<float>(reachSpec.collisionHeight),
				static_cast<uint32_t>(reachSpec.reachFlags), reachSpec.bPruned != 0 });
		}
	}

	const auto certificate =
		PawnMovement::CertifyHazardWaterEgressStaticWalkRoute(request);
	PawnMovement::HazardWaterEgressStaticWalkCertificate observation;
	observation.Result = PawnMovement::HazardWaterEgressRouteCertificateResultName(
		certificate.Result);
	observation.FirstHopKnown = certificate.FirstHopKnown
		&& certificate.FirstHopNode < request.Nodes.size();
	if (observation.FirstHopKnown)
	{
		observation.FirstHopName = request.Nodes[certificate.FirstHopNode].Id;
		observation.FirstHopLocationKnown = true;
		observation.FirstHopLocation = navigationPoints[certificate.FirstHopNode]->Location();
	}
	observation.ContinuationKnown = certificate.ContinuationKnown
		&& certificate.ContinuationNode < request.Nodes.size();
	if (observation.ContinuationKnown)
		observation.ContinuationName = request.Nodes[certificate.ContinuationNode].Id;
	observation.StaticWalkCost = certificate.StaticWalkCost;
	observation.StaticWalkHops = certificate.StaticWalkHops;
	observation.VisitedNodes = certificate.VisitedNodes;
	HazardWaterEgressObserver->ObserveStaticWalkCertificate(observation);
}

void UPawn::AdvanceHazardSwimEgressLiveSteer()
{
	constexpr float minimumAnchorDistance = 8.0f;
	constexpr float maximumAnchorDistance = 512.0f;
	const bool eligibleContext = engine->IsBotBenchmarkHazardSwimEgressEnabled()
		&& engine->IsBotBenchmarkHazardSwimEgressLiveEnabled()
		&& IsStockAutonomousPlayerBot(this) && Role() == ROLE_Authority
		&& !bDeleteMe() && Health() > 0 && Physics() == PHYS_Swimming
		&& HazardSwimEgress.HarmfulWaterEpisodeActive
		&& HazardSwimEgress.LiveActionAuthorized && !HazardSwimEgress.LiveProbeRejected
		&& HazardSwimEgress.AnchorKnown && IsFiniteVector(HazardSwimEgress.Anchor)
		&& HazardSwimEgress.Source == HazardSwimEgressState::AnchorSource::FallingPreMove;
	if (!eligibleContext)
	{
		HazardSwimEgress.ActionActive = false;
		return;
	}

	const std::array<UZoneInfo*, 3> zones = {
		Region().Zone, FootRegion().Zone, HeadRegion().Zone
	};
	UZoneInfo* primaryZone = Region().Zone;
	const bool exactHarmfulWater = primaryZone && primaryZone->bStatic()
		&& primaryZone->bWaterZone() && IsExactHarmfulZone(primaryZone);
	const vec3 delta = HazardSwimEgress.Anchor - Location();
	const float distance = length(delta);
	if (!exactHarmfulWater || !IsFiniteVector(delta) || !std::isfinite(distance)
		|| distance < minimumAnchorDistance || distance > maximumAnchorDistance)
	{
		HazardSwimEgress.ActionActive = false;
		return;
	}

	const bool probeClear = TryMove(delta, true).Fraction == 1.0f;
	ObserveHazardSwimEgressLiveSteerShadowDecision(
		BotAI::EvaluateHazardSwimEgressLiveSteer({
			engine->IsBotBenchmarkHazardSwimEgressLiveEnabled(),
			IsStockAutonomousPlayerBot(this) && Role() == ROLE_Authority,
			!bDeleteMe() && Health() > 0, BotAI::HazardSwimEgressPhysics::Swimming,
			HazardSwimEgress.HarmfulWaterEpisodeActive,
			HazardSwimEgress.LiveActionAuthorized,
			HazardSwimEgress.LiveProbeRejected,
			HazardSwimEgress.AnchorKnown && IsFiniteVector(HazardSwimEgress.Anchor),
			HazardSwimEgress.Source == HazardSwimEgressState::AnchorSource::FallingPreMove,
			exactHarmfulWater, true, distance,
			probeClear ? BotAI::HazardSwimEgressProbe::Clear
				: BotAI::HazardSwimEgressProbe::Blocked }));
	if (!probeClear)
	{
		HazardSwimEgress.LiveProbeRejected = true;
		HazardSwimEgress.ActionActive = false;
		HazardSwimEgressLiveProbeRejectedCountValue++;
		return;
	}

	Acceleration() = normalize(delta) * AccelRate();
	if (!HazardSwimEgress.ActionActive)
		HazardSwimEgressLiveApplyCountValue++;
	HazardSwimEgress.ActionActive = true;
	HazardSwimEgressLiveActiveTickCountValue++;
}

void UPawn::ObserveHazardSwimEgressLiveSteerShadowDecision(
	const BotAI::HazardSwimEgressLiveSteerDecision& decision)
{
	if (decision.Transition == BotAI::HazardSwimEgressLiveSteerTransition::SteerCandidate)
		HazardSwimEgressLiveShadowCandidateCountValue++;
	if (decision.Transition != BotAI::HazardSwimEgressLiveSteerTransition::Terminal)
		return;
	switch (decision.Terminal)
	{
	case BotAI::HazardSwimEgressLiveSteerTerminal::Falling:
		HazardSwimEgressLiveShadowFallingTerminalCountValue++;
		break;
	case BotAI::HazardSwimEgressLiveSteerTerminal::HazardCleared:
		HazardSwimEgressLiveShadowHazardClearedTerminalCountValue++;
		break;
	case BotAI::HazardSwimEgressLiveSteerTerminal::ProbeBlocked:
		HazardSwimEgressLiveShadowProbeBlockedTerminalCountValue++;
		break;
	default:
		break;
	}
}

void UPawn::RecordHazardSwimEgressDeath()
{
	if (HazardWaterEgressObserver && HazardWaterEgressObserver->HasActiveEpisode()
		&& IsFiniteVector(Location()) && IsFiniteVector(Destination()))
	{
		HazardWaterEgressObserver->FinishEpisode(
			PawnMovement::HazardWaterEgressTerminal::DeathBeforeExit, Location(),
			MoveTarget() ? MoveTarget()->Name.ToString() : std::string(), Destination());
	}
	if (HazardSwimEgress.ActionActive && HazardSwimEgress.HarmfulWaterEpisodeActive
		&& Physics() == PHYS_Swimming && !bDeleteMe())
		HazardSwimEgressDeathsBeforeExitCountValue++;
}

const char* UPawn::HazardSwimEgressAnchorSourceName() const
{
	switch (HazardSwimEgress.Source)
	{
	case HazardSwimEgressState::AnchorSource::SafeSwimming:
		return "safe_swimming";
	case HazardSwimEgressState::AnchorSource::FallingPreMove:
		return "falling_pre_move";
	default:
		return "none";
	}
}

void UPawn::ObserveHarmfulZoneEscapeBoundary(UZoneInfo* oldZone, UZoneInfo* newZone,
	bool footBoundary)
{
	const bool oldHarmful = IsExactHarmfulZone(oldZone);
	const bool newHarmful = IsExactHarmfulZone(newZone);
	if (!oldHarmful && newHarmful)
	{
		RecordFallingHazardRecoveryHarmfulEntry();
		if (footBoundary)
			HarmfulZoneEscapeFootEntryCountValue++;
		else
			HarmfulZoneEscapeCenterEntryCountValue++;
	}

	const bool wasHarmful = HarmfulZoneEscape.CenterHarmful
		|| HarmfulZoneEscape.FootHarmful;
	if (footBoundary)
		HarmfulZoneEscape.FootHarmful = newHarmful;
	else
		HarmfulZoneEscape.CenterHarmful = newHarmful;
	const bool isHarmful = HarmfulZoneEscape.CenterHarmful
		|| HarmfulZoneEscape.FootHarmful;

	if (!wasHarmful && isHarmful)
	{
		HarmfulZoneEscapeEpisodeId++;
		const LatentRunState latentState = StateFrame
			? StateFrame->LatentState : LatentRunState::Continue;
		const bool liveMoveTowardTarget = latentState != LatentRunState::MoveToward
			|| (MoveTarget() && !MoveTarget()->bDeleteMe());
		vec2 incomingDirection = Velocity().xy();
		if (!std::isfinite(incomingDirection.x) || !std::isfinite(incomingDirection.y)
			|| dot(incomingDirection, incomingDirection) <= 0.0001f)
			incomingDirection = Acceleration().xy();
		const bool eligible = engine->IsBotBenchmarkHarmfulZoneEscapeEnabled()
			&& IsStockAutonomousPlayerBot(this) && Role() == ROLE_Authority
			&& !bDeleteMe() && Health() > 0 && Physics() == PHYS_Walking
			&& newZone && !newZone->bWaterZone() && StateFrame
			&& IsMovementLatentState(latentState) && liveMoveTowardTarget
			&& std::isfinite(incomingDirection.x) && std::isfinite(incomingDirection.y)
			&& dot(incomingDirection, incomingDirection) > 0.0001f;
		const BotAI::HarmfulZoneEscapeEligibility decision =
			HarmfulZoneEscapeGate.Evaluate({ HarmfulZoneEscapeLifeId,
				HarmfulZoneEscapeEpisodeId, newHarmful, newZone ? newZone->DamagePerSec() : 0.0 });
		if (decision == BotAI::HarmfulZoneEscapeEligibility::Authorized && eligible)
		{
			HarmfulZoneEscape.Active = true;
			HarmfulZoneEscape.RecoveryAttempted = false;
			HarmfulZoneEscape.IncomingDirection = incomingDirection;
			HarmfulZoneEscapeEpisodeCountValue++;
		}
	}
	else if (wasHarmful && !isHarmful)
	{
		if (HarmfulZoneEscape.Active && HarmfulZoneEscape.RecoveryAttempted)
		{
			HarmfulZoneEscapeSuccessfulEscapeCountValue++;
			Acceleration() = vec3(0.0f);
			MoveTimer() = -1.0f;
			MoveStallWatchdog = {};
			HarmfulZoneEscapeForcedReplanCountValue++;
		}
		HarmfulZoneEscape.Active = false;
		HarmfulZoneEscape.RecoveryAttempted = false;
		HarmfulZoneEscapeGate.Reset();
	}
}

bool UPawn::ApplyHarmfulZoneEscape()
{
	if (!HarmfulZoneEscape.Active)
		return false;
	if (!engine->IsBotBenchmarkHarmfulZoneEscapeEnabled()
		|| !IsStockAutonomousPlayerBot(this) || Role() != ROLE_Authority
		|| bDeleteMe() || Health() <= 0 || Physics() != PHYS_Walking)
	{
		HarmfulZoneEscape.Active = false;
		return false;
	}

	if (!HarmfulZoneEscape.RecoveryAttempted)
	{
		HarmfulZoneEscape.RecoveryAttempted = true;
		HarmfulZoneEscapeRecoveryAttemptCountValue++;
	}

	const auto directions = PawnMovement::PainLedgeRecoveryCandidateDirections(
		HarmfulZoneEscape.IncomingDirection);
	std::array<PawnMovement::PainLedgeRecoveryCandidateProbe, 3> probes;
	const TraceFlags supportTraceFlags = {
		.movers = false,
		.world = true
	};
	const vec3 traceExtent(CollisionRadius(), CollisionRadius(), CollisionHeight());
	const float supportDepth = std::max(MaxStepHeight() * stepDownDeltaFactor, 1.0f);
	for (size_t index = 0; index < directions.size(); index++)
	{
		const vec3 candidateDelta(directions[index] * harmfulZoneEscapeProbeDistance, 0.0f);
		PawnMovement::PainLedgeRecoveryCandidateProbe& probe = probes[index];
		probe.SweepClear = TryMove(candidateDelta, true).Fraction == 1.0f;
		if (!probe.SweepClear)
			continue;

		const vec3 candidate = Location() + candidateDelta;
		const vec3 supportEnd = candidate - vec3(0.0f, 0.0f, supportDepth);
		const CollisionHit support = XLevel()->Collision.TraceFirstHit(
			candidate, supportEnd, this, traceExtent, supportTraceFlags);
		probe.WalkableSupport = support.Fraction < 1.0f && support.Normal.z >= 0.7071f;
		if (!probe.WalkableSupport)
			continue;

		const vec3 supportedCenter = candidate + (supportEnd - candidate) * support.Fraction;
		UZoneInfo* footZone = XLevel()->Model->FindRegion(
			supportedCenter - vec3(0.0f, 0.0f, CollisionHeight()), Level()).Zone;
		probe.NonPainFootRegion = footZone && !footZone->bWaterZone()
			&& !IsExactHarmfulZone(footZone);
	}

	const int selected = PawnMovement::SelectPainLedgeRecoveryCandidate(probes);
	if (selected >= 0)
	{
		Acceleration() = vec3(directions[static_cast<size_t>(selected)] * AccelRate(), 0.0f);
		return true;
	}

	HarmfulZoneEscape.Active = false;
	Acceleration() = vec3(0.0f);
	MoveTimer() = -1.0f;
	MoveStallWatchdog = {};
	HarmfulZoneEscapeForcedReplanCountValue++;
	HarmfulZoneEscapeNoSafeCandidateCountValue++;
	return true;
}

bool UPawn::TickMoveTo(const vec3& target, float elapsed, UActor* targetActor)
{
	if (MoveTimer() < 0.0f)
	{
		Acceleration() = vec3(0.0f);
		return true;
	}

	if (Physics() == PHYS_Walking)
	{
		if (ApplyHarmfulZoneEscape())
			return MoveTimer() < 0.0f;
		vec2 delta = target.xy() - Location().xy();
		float distSqr = dot(delta, delta);
		float velocitySqr = dot(Velocity().xy(), Velocity().xy());
		float acceptanceRadius = 1.0f;
		bool targetVerticallyReachable = true;
		if (targetActor)
		{
			const float verticalSeparation = std::abs(targetActor->Location().z - Location().z);
			const bool canTouchTarget = targetActor->bCollideActors();
			const float verticalReach = canTouchTarget
				? CollisionHeight() + targetActor->CollisionHeight() + MaxStepHeight()
				: CollisionHeight();
			targetVerticallyReachable = verticalSeparation <= verticalReach;
			if (targetVerticallyReachable)
				acceptanceRadius = canTouchTarget
					? std::max(CollisionRadius() + targetActor->CollisionRadius(), 1.0f)
					: std::max(CollisionRadius(), 1.0f);
		}
		if (PawnMovement::HasArrived({
			.DistanceSquared = distSqr,
			.SpeedSquared = velocitySqr,
			.Elapsed = elapsed,
			.AcceptanceRadius = acceptanceRadius,
			.VerticallyReachable = targetVerticallyReachable }))
		{
			Acceleration() = vec3(0.0f);
			return true;
		}

		if (!ApplyPainLedgeRecovery(delta) && !ApplyWallAdjustRecovery(delta))
			Acceleration() = vec3(normalize(delta) * AccelRate(), 0.0f);
	}
	else
	{
		vec3 delta = target - Location();
		float distSqr = dot(delta, delta);
		float velocitySqr = dot(Velocity(), Velocity());
		float acceptanceRadius = targetActor
			? (targetActor->bCollideActors()
				? std::max(CollisionRadius() + targetActor->CollisionRadius(), 1.0f)
				: std::max(CollisionRadius(), 1.0f))
			: 1.0f;
		if (PawnMovement::HasArrived({
			.DistanceSquared = distSqr,
			.SpeedSquared = velocitySqr,
			.Elapsed = elapsed,
			.AcceptanceRadius = acceptanceRadius }))
		{
			Acceleration() = vec3(0.0f);
			return true;
		}

		Acceleration() = normalize(delta) * AccelRate();
	}

	return false;
}

void UPawn::ObserveMoveStallWatchdog(float elapsed)
{
	UZoneInfo* footZone = FootRegion().Zone;
	UActor* actorBase = ActorBase();
	UActor* moveTarget = MoveTarget();
	UActor* faceTarget = engine->LaunchInfo.ue1Version > 219 ? FaceTarget() : nullptr;
	const bool moverContext = (actorBase && actorBase->IsA("Mover"))
		|| (moveTarget && moveTarget->IsA("Mover"))
		|| (faceTarget && faceTarget->IsA("Mover"));
	const bool eligibleContext = IsStockAutonomousPlayerBot(this)
		&& Role() == ROLE_Authority && !bDeleteMe() && Health() > 0
		&& Physics() == PHYS_Walking && footZone && !footZone->bPainZone()
		&& !PainLedgeRecovery.Active && !WallAdjustRecovery.Active
		&& !moverContext;
	const bool latentMovementIntent = StateFrame
		&& IsMovementLatentState(StateFrame->LatentState);
	const LatentRunState latentState = StateFrame
		? StateFrame->LatentState : LatentRunState::Continue;
	const PawnMovement::MoveStallLatentMode latentMode = MoveStallLatentModeFor(latentState);
	const bool moveTowardLatent = latentMode == PawnMovement::MoveStallLatentMode::MoveToward;
	UNavigationPoint* navigationTarget = moveTowardLatent
		? UObject::TryCast<UNavigationPoint>(moveTarget) : nullptr;
	const bool liveNavigationTarget = navigationTarget && !navigationTarget->bDeleteMe();
	if (MoveStallRecoveryEpisode.Active)
	{
		PawnMovement::MoveStallRecoveryEpisodeEvent episodeEvent =
			PawnMovement::MoveStallRecoveryEpisodeEvent::None;
		if (bDeleteMe() || Health() <= 0)
			episodeEvent = PawnMovement::MoveStallRecoveryEpisodeEvent::LifeBoundary;
		else if (StateFrame && !latentMovementIntent)
			episodeEvent = PawnMovement::MoveStallRecoveryEpisodeEvent::IntentionalStop;
		else if (!eligibleContext)
			episodeEvent = PawnMovement::MoveStallRecoveryEpisodeEvent::Unknown;
		AdvanceMoveStallRecoveryEpisode(elapsed, episodeEvent);
	}
	const PawnMovement::MoveStallWatchdogObservation observation =
		PawnMovement::ObserveMoveStall(MoveStallWatchdog, Location(), elapsed,
			eligibleContext, latentMovementIntent, moveStallProgressRadius,
			moveStallDetectionSeconds);
	MoveStallWatchdog = observation.State;
	MoveStallEligibleSecondsValue += observation.EligibleSeconds;
	if (observation.EpisodeReset)
		MoveStallEpisodeResetCountValue++;
	if (MoveStallRecoveryEpisode.Active && eligibleContext && latentMovementIntent
		&& observation.EpisodeReset)
	{
		AdvanceMoveStallRecoveryEpisode(0.0f,
			PawnMovement::MoveStallRecoveryEpisodeEvent::Cleared);
	}
	if (observation.Detected)
	{
		MoveStallDetectionCountValue++;
		if (!MoveStallRecoveryEpisode.Active)
		{
			const PawnMovement::MoveStallRecoveryEpisodeUpdate episode =
				PawnMovement::StartMoveStallRecoveryEpisode();
			MoveStallRecoveryEpisode = episode.State;
			MoveStallRecoveryEpisodeCommand = MoveStallWatchdog.Command;
			MoveStallRecoveryEpisodeId++;
			MoveStallRecoveryEpisodeStartCountValue++;
		}
		const PawnMovement::MoveStallRecoveryDecision recovery =
			PawnMovement::SelectMoveStallRecovery({
				.Detected = observation.Detected,
				.LatentMode = latentMode,
				.LiveNavigationMoveToward = liveNavigationTarget,
				.TargetlessMoveToTimeoutEnabled =
					engine->IsBotBenchmarkTargetlessMoveToTimeoutEnabled(),
				.Targetless = moveTarget == nullptr,
				.MoveTimer = MoveTimer(),
				.Location = Location(),
				.Destination = Destination(),
				.AcceptanceRadius = 1.0f
			});
		if (recovery == PawnMovement::MoveStallRecoveryDecision::NavigationReplan)
		{
			UMover* lift = nullptr;
			if (ULiftExit* liftExit = UObject::TryCast<ULiftExit>(navigationTarget))
				lift = liftExit->MyLift();
			const PawnMovement::FailedNavigationEligibility memoryEligibility = {
				.TargetLive = liveNavigationTarget,
				.LiftCenter = UObject::TryCast<ULiftCenter>(navigationTarget) != nullptr,
				.SpecialGoalRedirected = SpecialGoal() && SpecialGoal() != navigationTarget,
				.BasedOnLift = lift && actorBase == lift,
				.LiftInterpolating = lift && lift->bInterpolating(),
				.LiftDelaying = lift && lift->bDelaying(),
				.LiftWaitingForPawn = lift && lift->WaitingPawn() == this
			};
			if (PawnMovement::CanRememberFailedNavigation(memoryEligibility))
			{
				const PawnMovement::FailedNavigationFailureRecord record =
					PawnMovement::RecordFailedNavigation(
						FailedNavigationMemory, navigationTarget, Location().x, Location().y,
						failedNavigationRepeatWindow, failedNavigationAvoidanceDuration,
						failedNavigationRequiredFailures);
				FailedNavigationMemory = record.State;
				if (record.AvoidanceActivated)
					FailedNavigationAvoidanceActivationCountValue++;
			}
			else
			{
				FailedNavigationSafeguardSuppressionCountValue++;
			}
		}
		if (recovery != PawnMovement::MoveStallRecoveryDecision::None)
		{
			Acceleration() = vec3(0.0f);
			MoveTimer() = -1.0f;
			MoveStallWatchdog = {};
			MoveStallForcedReplanCountValue++;
			if (recovery == PawnMovement::MoveStallRecoveryDecision::NavigationReplan)
				MoveStallNavigationForcedReplanCountValue++;
			else if (recovery == PawnMovement::MoveStallRecoveryDecision::TargetlessTimeout)
				MoveStallTargetlessMoveToTimeoutCountValue++;
		}
	}
}

void UPawn::RecordMoveStallCommand()
{
	if (IsStockAutonomousPlayerBot(this))
	{
		const LatentRunState latentState = StateFrame
			? StateFrame->LatentState : LatentRunState::Continue;
		const PawnMovement::MoveStallLatentMode latentMode = MoveStallLatentModeFor(latentState);
		const void* target = nullptr;
		if (latentMode == PawnMovement::MoveStallLatentMode::MoveToward)
			target = MoveTarget();
		else if (latentMode == PawnMovement::MoveStallLatentMode::StrafeFacing
			&& engine->LaunchInfo.ue1Version > 219)
			target = FaceTarget();
		const PawnMovement::MoveStallCommandKey command = {
			latentMode, target, Destination()
		};
		UNavigationPoint* navigationTarget = latentMode
			== PawnMovement::MoveStallLatentMode::MoveToward
			? UObject::TryCast<UNavigationPoint>(MoveTarget()) : nullptr;
		if (MoveStallRecoveryEpisode.Active && navigationTarget
			&& !navigationTarget->bDeleteMe()
			&& !PawnMovement::SameMoveStallCommand(
				MoveStallRecoveryEpisodeCommand, command))
		{
			AdvanceMoveStallRecoveryEpisode(0.0f,
				PawnMovement::MoveStallRecoveryEpisodeEvent::QualifiedNavigationReplan);
		}
		MoveStallWatchdog = PawnMovement::RecordMoveStallCommand(
			MoveStallWatchdog, command);
	}
}

void UPawn::AdvanceMoveStallRecoveryEpisode(float elapsed,
	PawnMovement::MoveStallRecoveryEpisodeEvent event)
{
	if (!MoveStallRecoveryEpisode.Active)
		return;
	const float secondsSinceDetection = std::isfinite(elapsed) && elapsed >= 0.0f
		? MoveStallRecoveryEpisode.SecondsSinceDetection + elapsed
		: 0.0f;
	const PawnMovement::MoveStallRecoveryEpisodeUpdate update =
		PawnMovement::AdvanceMoveStallRecoveryEpisode(
			MoveStallRecoveryEpisode, elapsed, event);
	MoveStallRecoveryEpisode = update.State;
	if (!update.Terminal)
		return;
	MoveStallRecoveryEpisodeCommand = {};
	RecordMoveStallRecoveryEpisodeOutcome(secondsSinceDetection, update.Outcome);
}

void UPawn::RecordMoveStallRecoveryEpisodeOutcome(float secondsSinceDetection,
	PawnMovement::MoveStallRecoveryEpisodeOutcome outcome)
{
	switch (outcome)
	{
	case PawnMovement::MoveStallRecoveryEpisodeOutcome::ClearedWithin2Seconds:
		MoveStallRecoveryClearedWithin2SecondsCountValue++;
		break;
	case PawnMovement::MoveStallRecoveryEpisodeOutcome::ClearedAfter2SecondsWithin5Seconds:
		MoveStallRecoveryClearedAfter2SecondsWithin5SecondsCountValue++;
		break;
	case PawnMovement::MoveStallRecoveryEpisodeOutcome::ReplannedWithin5Seconds:
		MoveStallRecoveryReplannedWithin5SecondsCountValue++;
		break;
	case PawnMovement::MoveStallRecoveryEpisodeOutcome::Missed5SecondDeadline:
		MoveStallRecoveryMissed5SecondDeadlineCountValue++;
		break;
	case PawnMovement::MoveStallRecoveryEpisodeOutcome::ExcludedIntentionalStop:
		MoveStallRecoveryExcludedIntentionalStopCountValue++;
		break;
	case PawnMovement::MoveStallRecoveryEpisodeOutcome::CensoredLifeBoundary:
		MoveStallRecoveryCensoredLifeBoundaryCountValue++;
		break;
	case PawnMovement::MoveStallRecoveryEpisodeOutcome::CensoredRunEnd:
		MoveStallRecoveryCensoredRunEndCountValue++;
		break;
	case PawnMovement::MoveStallRecoveryEpisodeOutcome::Unknown:
		MoveStallRecoveryUnknownCountValue++;
		break;
	case PawnMovement::MoveStallRecoveryEpisodeOutcome::None:
		return;
	}

	static constexpr size_t maximumQueuedRecords = 1024;
	PawnMoveStallRecoveryEpisodeRecord record;
	record.SourcePawnActor = Name.ToString();
	record.Sequence = ++MoveStallRecoveryEpisodeRecordSequence;
	record.LifeId = MoveStallRecoveryLifeId;
	record.EpisodeId = MoveStallRecoveryEpisodeId;
	record.SecondsSinceDetection = secondsSinceDetection;
	record.Outcome = outcome;
	if (MoveStallRecoveryEpisodeRecords.size() < maximumQueuedRecords)
		MoveStallRecoveryEpisodeRecords.push_back(record);
	else
		MoveStallRecoveryEpisodeRecordOverflowCountValue++;
}

std::vector<PawnMoveStallRecoveryEpisodeRecord>
	UPawn::DrainMoveStallRecoveryEpisodeRecords()
{
	std::vector<PawnMoveStallRecoveryEpisodeRecord> records;
	records.swap(MoveStallRecoveryEpisodeRecords);
	return records;
}

void UPawn::EndMoveStallRecoveryLife()
{
	AdvanceMoveStallRecoveryEpisode(0.0f,
		PawnMovement::MoveStallRecoveryEpisodeEvent::LifeBoundary);
	MoveStallWatchdog = {};
	MoveStallRecoveryLifeId++;
}

void UPawn::EndMoveStallRecoveryRun()
{
	AdvanceMoveStallRecoveryEpisode(0.0f,
		PawnMovement::MoveStallRecoveryEpisodeEvent::RunEnd);
	MoveStallWatchdog = {};
}

void UPawn::RecordPainLedgeVeto(const vec3& origin, const vec2& unsafeDirection)
{
	PainLedgeVetoCountValue++;
	const PawnMovement::PainLedgeVetoRecord record = PawnMovement::RecordPainLedgeVeto(
		PainLedgeRecovery, origin, unsafeDirection, painLedgeRecoveryDuration,
		painLedgeRepeatRadius, painLedgeDirectionAlignment);
	PainLedgeRecovery = record.State;
	if (record.Repeat)
		PainLedgeRepeatVetoCountValue++;
}

void UPawn::ObserveFallingSeamEscapeShadow(const vec3& requestedRemainingDelta,
	const vec3& actualDisplacement, const vec3& firstHitNormal,
	const vec3& secondHitNormal, bool normalDownwardGravity)
{
	UZoneInfo* startFootZone = FootRegion().Zone;
	const bool eligible = IsStockAutonomousPlayerBot(this) && Role() == ROLE_Authority
		&& !bDeleteMe() && Health() > 0 && Physics() == PHYS_Falling
		&& startFootZone && !startFootZone->bPainZone() && !startFootZone->bWaterZone();
	if (!eligible)
	{
		FallingSeamEpisode = {};
		return;
	}

	PawnMovement::HorizontalCornerEscapeInput input;
	input.Contact = {
		.RequestedRemainingDelta = requestedRemainingDelta,
		.ActualDisplacement = actualDisplacement,
		.FirstHitNormal = firstHitNormal,
		.SecondHitNormal = secondHitNormal,
		.AutonomousPlayerBot = true,
		.NormalDownwardGravity = normalDownwardGravity
	};
	if (PawnMovement::ResolveFallingTwoPlaneContact(input.Contact).Decision
		!= PawnMovement::FallingTwoPlaneContactDecision::ProbeCreaseSweep)
		return;
	const PawnMovement::FallingSeamEpisodeUpdate episode =
		PawnMovement::UpdateFallingSeamEpisode(FallingSeamEpisode, {
			.Eligible = true,
			.Position = Location().xy(),
			.FirstNormal = firstHitNormal,
			.SecondNormal = secondHitNormal
		});
	FallingSeamEpisode = episode.State;
	if (episode.Started)
		FallingSeamEpisodeCountValue++;
	FallingSeamDetectionCountValue++;

	const PawnMovement::HorizontalCornerEscapeCandidates candidates =
		PawnMovement::BuildHorizontalCornerEscapeCandidates(input);
	if (candidates.Count == 0)
	{
		FallingSeamInvalidGeometryRejectCountValue++;
		return;
	}
	for (size_t candidateIndex = 0; candidateIndex < candidates.Count; candidateIndex++)
	{
		const PawnMovement::HorizontalCornerEscapeCandidate& candidate =
			candidates.Candidates[candidateIndex];
		if (!candidate.Valid)
			continue;
		HorizontalCornerCandidateProbeCountValue++;

		PawnMovement::FallingRecoveryAuthorizationEvidence evidence;
		evidence.SweepResultKnown = true;
		evidence.SweepClear = TryMove(candidate.SweepDelta, true).Fraction == 1.0f;
		if (evidence.SweepClear)
		{
			const TraceFlags supportTraceFlags = {
				.pawns = true,
				.movers = true,
				.others = true,
				.world = true
			};
			const vec3 candidateCenter = Location() + candidate.SweepDelta;
			const float supportDepth = std::max(MaxStepHeight() * stepDownDeltaFactor, 1.0f);
			const vec3 supportEnd = candidateCenter - vec3(0.0f, 0.0f, supportDepth);
			const vec3 traceExtent(CollisionRadius(), CollisionRadius(), CollisionHeight());
			const CollisionHit support = XLevel()->Collision.TraceFirstHit(
				candidateCenter, supportEnd, this, traceExtent, supportTraceFlags);
			evidence.SupportResultKnown = true;
			evidence.WalkableShortSupport = support.Fraction < 1.0f
				&& support.Actor == Level()
				&& support.Normal.z >= input.Contact.WalkableNormalZ;
			if (evidence.WalkableShortSupport)
			{
				const vec3 supportedCenter = candidateCenter
					+ (supportEnd - candidateCenter) * support.Fraction;
				UZoneInfo* supportZone = XLevel()->Model->FindRegion(
					supportedCenter - vec3(0.0f, 0.0f, CollisionHeight()), Level()).Zone;
				evidence.PainResultKnown = supportZone != nullptr;
				evidence.SupportInPainZone = supportZone && supportZone->bPainZone();
			}
		}

		const LatentRunState latentState = StateFrame
			? StateFrame->LatentState : LatentRunState::Continue;
		const bool liveMoveTowardTarget = latentState != LatentRunState::MoveToward
			|| (MoveTarget() && !MoveTarget()->bDeleteMe());
		const bool activeMovementIntentAndTarget = IsMovementLatentState(latentState)
			&& liveMoveTowardTarget;
		if (activeMovementIntentAndTarget)
		{
			const vec3 target = Destination();
			const vec3 currentTargetDelta = target - Location();
			const vec3 candidateTargetDelta = target - (Location() + candidate.SweepDelta);
			const float currentDistance = length(currentTargetDelta);
			const float candidateDistance = length(candidateTargetDelta);
			if (std::isfinite(currentDistance) && std::isfinite(candidateDistance))
			{
				evidence.TargetProgressKnown = true;
				evidence.TargetProgress = currentDistance - candidateDistance;
			}
		}

		switch (PawnMovement::ClassifyHorizontalCornerEscapeDetailed(
			candidate, evidence, activeMovementIntentAndTarget))
		{
		case PawnMovement::HorizontalCornerEscapeDetailedClassification::Authorized:
			HorizontalCornerAuthorizedCandidateCountValue++;
			if (!FallingSeamEpisode.AuthorizationCounted)
			{
				FallingSeamEpisode.AuthorizationCounted = true;
				FallingSeamAuthorizableEpisodeCountValue++;
			}
			break;
		case PawnMovement::HorizontalCornerEscapeDetailedClassification::BlockedSweep:
			HorizontalCornerBlockedSweepCandidateCountValue++;
			break;
		case PawnMovement::HorizontalCornerEscapeDetailedClassification::NoStaticWalkableSupport:
			HorizontalCornerNoStaticWalkableSupportCandidateCountValue++;
			break;
		case PawnMovement::HorizontalCornerEscapeDetailedClassification::PainSupport:
			HorizontalCornerPainSupportCandidateCountValue++;
			break;
		case PawnMovement::HorizontalCornerEscapeDetailedClassification::NoActiveMovementIntentOrTarget:
			HorizontalCornerNoActiveMovementIntentOrTargetCandidateCountValue++;
			break;
		case PawnMovement::HorizontalCornerEscapeDetailedClassification::TrueTargetRegression:
			HorizontalCornerTrueTargetRegressionCandidateCountValue++;
			break;
		case PawnMovement::HorizontalCornerEscapeDetailedClassification::UnknownEvidence:
			HorizontalCornerUnknownEvidenceCandidateCountValue++;
			break;
		case PawnMovement::HorizontalCornerEscapeDetailedClassification::CandidateInvalid:
			break;
		}

		switch (PawnMovement::ClassifyHorizontalCornerEscapeShadow(candidate, evidence))
		{
		case PawnMovement::HorizontalCornerEscapeShadowClassification::Authorized:
			HorizontalCornerAuthorizedEscapeCountValue++;
			return;
		case PawnMovement::HorizontalCornerEscapeShadowClassification::TargetProgressRejected:
			HorizontalCornerTargetProgressRejectCountValue++;
			break;
		case PawnMovement::HorizontalCornerEscapeShadowClassification::UnknownOrUnsafeSupport:
			HorizontalCornerUnknownOrUnsafeSupportCountValue++;
			break;
		case PawnMovement::HorizontalCornerEscapeShadowClassification::CandidateInvalid:
			break;
		}
	}
}

void UPawn::ObserveWalkingStepPreflightShadow(const vec3& stepUpDelta,
	const vec3& forwardDelta, const vec3& stepDownDelta, int walkingIteration,
	uint64_t invocationToken)
{
	WalkingStepPreflightObservedTransactionValid = false;
	if (!IsStockAutonomousPlayerBot(this) || Role() != ROLE_Authority
		|| bDeleteMe() || Health() <= 0 || Physics() != PHYS_Walking)
	{
		WalkingStepPreflightEpisode = {};
		return;
	}

	using namespace PawnMovement;
	WalkingStepPreflightInput input;
	WalkingStepPreflightDiagnosticRecord diagnostic;
	diagnostic.SourcePawnActor = Name.ToString();
	diagnostic.WalkingIteration = walkingIteration;
	diagnostic.LifeGeneration = WalkingStepPreflightLifeGeneration;
	diagnostic.InvocationToken = invocationToken;
	diagnostic.MovementCommandToken = FallingHazardMovementCommandToken;
	diagnostic.PrecommitOrigin = Location();
	diagnostic.SemanticTarget = MoveTarget() && !MoveTarget()->bDeleteMe()
		? MoveTarget()->Name.ToString() : std::string();
	diagnostic.SemanticDestination = Destination();
	WalkingStepPreflightObservedTransactionValid = true;
	WalkingStepPreflightObservedIteration = walkingIteration;
	WalkingStepPreflightObservedInvocation = invocationToken;
	WalkingStepPreflightObservedCorrelation = {
		.SourcePawnActor = diagnostic.SourcePawnActor,
		.LifeGeneration = diagnostic.LifeGeneration,
		.InvocationToken = diagnostic.InvocationToken,
		.WalkingIteration = diagnostic.WalkingIteration
	};
	auto recordProbe = [](WalkingStepPreflightProbeDiagnostic& target,
		const WalkingStepSweepObservation& observation, const CollisionHit& hit)
	{
		target.Collision = observation.Collision;
		target.Fraction = hit.Fraction;
		target.Delta = observation.Delta;
		target.Normal = hit.Normal;
	};
	input.Actor = WalkingStepActorKind::StockAutonomousPlayerBot;
	input.Walking = true;
	input.WalkableNormalZ = walkingStepWalkableNormalZ;
	input.MaximumStepDelta = walkingStepMaximumDelta;
	input.MaximumFallSegmentDelta = walkingStepMaximumFallSegmentDelta;
	input.MaximumForecastDrop = walkingStepMaximumForecastDrop;
	input.MaximumVerticalWallNormalZ = walkingStepVerticalWallNormalZ;
	input.UpwardJumpRequested = WalkingStepExplicitJumpRequested;

	UZoneInfo* startFootZone = FootRegion().Zone;
	UZoneInfo* physicsZone = Region().Zone;
	input.StartSupport.Zone = ClassifyWalkingStepZone(startFootZone);
	if (physicsZone)
	{
		input.GravityKnown = true;
		input.Gravity = physicsZone->ZoneGravity();
	}
	const CollisionHit startSupport = ProbeMoveCollision(Location(), stepDownDelta, true);
	input.StartSupport.Collision = ClassifyWalkingStepCollision(this, startSupport);
	input.StartSupport.Normal = startSupport.Normal;
	diagnostic.StartSupport = {
		.Collision = input.StartSupport.Collision,
		.Fraction = startSupport.Fraction,
		.Delta = stepDownDelta,
		.Normal = startSupport.Normal
	};

	bool scriptVisibleActorContact = false;
	CollisionHit stepUpHit;
	input.StepUp = ProbeWalkingStepSweep(
		this, Location(), stepUpDelta, &stepUpHit, &scriptVisibleActorContact);
	recordProbe(diagnostic.StepUp, input.StepUp, stepUpHit);
	input.CollisionCallbackRequired = scriptVisibleActorContact;
	input.Forward.Delta = forwardDelta;
	input.Forward.Collision = WalkingStepCollisionKind::Clear;
	input.StepDown.Delta = stepDownDelta;
	input.StepDown.Collision = WalkingStepCollisionKind::Clear;
	bool unsupportedEndpoint = false;

	if (input.StepUp.Collision == WalkingStepCollisionKind::Clear)
	{
		const vec3 forwardOrigin = Location() + stepUpDelta;
		CollisionHit forwardHit;
		input.Forward = ProbeWalkingStepSweep(this, forwardOrigin, forwardDelta,
			&forwardHit, &scriptVisibleActorContact);
		recordProbe(diagnostic.Forward, input.Forward, forwardHit);
		input.CollisionCallbackRequired = input.CollisionCallbackRequired
			|| scriptVisibleActorContact;
		if (input.Forward.Collision != WalkingStepCollisionKind::Clear)
			input.CollisionCallbackRequired = true;
		else
		{
			const vec3 raisedEndpoint = forwardOrigin + forwardDelta;
			CollisionHit actualStepDownHit;
			const WalkingStepSweepObservation actualStepDown =
				ProbeWalkingStepSweep(this, raisedEndpoint, -stepUpDelta,
					&actualStepDownHit, &scriptVisibleActorContact);
			recordProbe(diagnostic.ActualStepDown, actualStepDown, actualStepDownHit);
			input.CollisionCallbackRequired = input.CollisionCallbackRequired
				|| scriptVisibleActorContact;
			if (actualStepDown.Collision == WalkingStepCollisionKind::Clear)
			{
				const vec3 endpoint = raisedEndpoint - stepUpDelta;
				diagnostic.PredictedUnsupportedEndpoint = endpoint;
				CollisionHit supportHit;
				input.StepDown = ProbeWalkingStepSweep(
					this, endpoint, stepDownDelta, &supportHit);
				recordProbe(diagnostic.SupportProbe, input.StepDown, supportHit);
				if (input.StepDown.Collision == WalkingStepCollisionKind::Clear)
				{
					unsupportedEndpoint = true;
					WalkingStepPreflightUnsupportedEndpointCountValue++;
					vec3 fallAcceleration = Acceleration();
					const float maximumAirAcceleration = engine->LaunchInfo.ue1Version > 219
						? AirControl() * AccelRate() : 0.0f;
					const float accelerationLength = length(fallAcceleration);
					if (accelerationLength > maximumAirAcceleration
						&& accelerationLength > 0.0f)
						fallAcceleration = normalize(fallAcceleration) * maximumAirAcceleration;
					diagnostic.FallForecastAttempted = true;
					diagnostic.FallForecastOrigin = endpoint;
					diagnostic.FallForecastVelocity = Velocity();
					diagnostic.FallForecastAcceleration = fallAcceleration;
					diagnostic.FallForecastGravityKnown = physicsZone != nullptr;
					diagnostic.FallForecastGravity = physicsZone
						? physicsZone->ZoneGravity() : vec3(0.0f);
					input.FallForecast = ForecastWalkingStepFall(this,
						endpoint, Velocity(), fallAcceleration, physicsZone, &diagnostic);
					diagnostic.FallForecast = input.FallForecast;
				}
			}
			else
			{
				input.StepDown = actualStepDown;
				input.StepDown.Delta = stepDownDelta;
			}
		}
	}

	const WalkingStepPreflightResult result = EvaluateWalkingStepPreflight(input);
	diagnostic.Reason = result.Reason;
	WalkingStepPreflightObservationCountValue++;
	const size_t reasonIndex = static_cast<size_t>(result.Reason);
	if (reasonIndex < WalkingStepPreflightReasonCountValues.size())
		WalkingStepPreflightReasonCountValues[reasonIndex]++;
	const bool authorized = result.Decision
		== WalkingStepPreflightDecision::AuthorizeUnsafeStepVeto;
	if (authorized)
		WalkingStepPreflightProvisionalAuthorizationCountValue++;
	else
		WalkingStepPreflightNoDecisionCountValue++;
	WalkingStepPreflightPendingConfirmation = authorized;
	if (authorized)
	{
		WalkingStepPreflightPendingIteration = walkingIteration;
		WalkingStepPreflightPendingInvocation = invocationToken;
		WalkingStepPreflightPendingLifeGeneration = WalkingStepPreflightLifeGeneration;
		WalkingStepPreflightPendingDiagnostic = diagnostic;
		WalkingStepPreflightPendingInput = input;
		const LatentRunState latentState = StateFrame
			? StateFrame->LatentState : LatentRunState::Continue;
		WalkingStepPreflightPendingSemanticTarget =
			latentState == LatentRunState::MoveToward
			&& MoveTarget() && !MoveTarget()->bDeleteMe() ? MoveTarget() : nullptr;
	}

	if (unsupportedEndpoint || authorized || result.Reason
		== WalkingStepPreflightReason::CollisionCallbackRequiredScriptTransitionUnknown)
	{
		static constexpr size_t maximumQueuedDiagnostics = 1024;
		diagnostic.Sequence = WalkingStepPreflightDiagnosticSequence++;
		if (WalkingStepPreflightDiagnostics.size() < maximumQueuedDiagnostics)
			WalkingStepPreflightDiagnostics.push_back(std::move(diagnostic));
		else
			WalkingStepPreflightDiagnosticOverflowCountValue++;
	}
}

bool UPawn::ConfirmWalkingStepPreflightShadow(int walkingIteration,
	uint64_t invocationToken, PawnMovement::LedgeTransition transition)
{
	using namespace PawnMovement;
	if (!WalkingStepPreflightPendingConfirmation
		|| WalkingStepPreflightPendingIteration != walkingIteration
		|| WalkingStepPreflightPendingInvocation != invocationToken)
		return false;

	WalkingStepPreflightPendingConfirmation = false;
	WalkingStepPreflightDiagnosticRecord diagnostic =
		WalkingStepPreflightPendingDiagnostic;
	diagnostic.Phase = "post_mayfall_confirmation";
	auto queueDiagnostic = [this](WalkingStepPreflightDiagnosticRecord&& record)
	{
		static constexpr size_t maximumQueuedDiagnostics = 1024;
		record.Sequence = WalkingStepPreflightDiagnosticSequence++;
		if (WalkingStepPreflightDiagnostics.size() < maximumQueuedDiagnostics)
			WalkingStepPreflightDiagnostics.push_back(std::move(record));
		else
			WalkingStepPreflightDiagnosticOverflowCountValue++;
	};

	bool confirmed = transition == LedgeTransition::BeginFalling;
	if (transition == LedgeTransition::Abort)
		diagnostic.TransitionOutcome = "abort";
	else if (transition == LedgeTransition::RestoreGrounded)
		diagnostic.TransitionOutcome = "restore_grounded";
	else
		diagnostic.TransitionOutcome = "begin_falling";
	diagnostic.FallForecastAttempted = false;
	diagnostic.FallForecastOrigin = vec3(0.0f);
	diagnostic.FallForecastVelocity = vec3(0.0f);
	diagnostic.FallForecastAcceleration = vec3(0.0f);
	diagnostic.FallForecastGravityKnown = false;
	diagnostic.FallForecastGravity = vec3(0.0f);
	diagnostic.FallForecast = {};
	diagnostic.FallHitFractions = {};
	diagnostic.FallHitCount = 0;

	if (transition == LedgeTransition::Abort || bDeleteMe())
	{
		diagnostic.ActualUnsupportedEndpoint = vec3(0.0f);
		queueDiagnostic(std::move(diagnostic));
		return false;
	}

	diagnostic.ActualUnsupportedEndpoint = Location();

	const vec3 endpointDelta = Location()
		- diagnostic.PredictedUnsupportedEndpoint;
	const bool endpointMatches = IsFiniteVector(endpointDelta)
		&& dot(endpointDelta, endpointDelta) <= 0.25f * 0.25f;
	const bool sameLife = WalkingStepPreflightPendingLifeGeneration
		== WalkingStepPreflightLifeGeneration;
	const bool aliveWalking = !bDeleteMe() && Health() > 0
		&& Physics() == PHYS_Walking;
	const LatentRunState latentState = StateFrame
		? StateFrame->LatentState : LatentRunState::Continue;
	const void* currentSemanticTarget = latentState == LatentRunState::MoveToward
		&& MoveTarget() && !MoveTarget()->bDeleteMe() ? MoveTarget() : nullptr;
	const vec3 destinationDelta = Destination()
		- diagnostic.SemanticDestination;
	const bool semanticMatches = currentSemanticTarget
		== WalkingStepPreflightPendingSemanticTarget
		&& IsFiniteVector(destinationDelta)
		&& (currentSemanticTarget || dot(destinationDelta, destinationDelta) <= 0.25f * 0.25f);
	confirmed = confirmed && endpointMatches && sameLife && aliveWalking && semanticMatches;
	if (!confirmed && diagnostic.TransitionOutcome == "begin_falling")
		diagnostic.TransitionOutcome = "post_callback_evidence_changed";

	if (confirmed)
	{
		UZoneInfo* physicsZone = Region().Zone;
		vec3 fallAcceleration = Acceleration();
		const float maximumAirAcceleration = engine->LaunchInfo.ue1Version > 219
			? AirControl() * AccelRate() : 0.0f;
		const float accelerationLength = length(fallAcceleration);
		if (accelerationLength > maximumAirAcceleration && accelerationLength > 0.0f)
			fallAcceleration = normalize(fallAcceleration) * maximumAirAcceleration;
		diagnostic.FallForecastOrigin = Location();
		diagnostic.FallForecastVelocity = Velocity();
		diagnostic.FallForecastAcceleration = fallAcceleration;
		diagnostic.FallForecastGravityKnown = physicsZone != nullptr;
		diagnostic.FallForecastGravity = physicsZone
			? physicsZone->ZoneGravity() : vec3(0.0f);
		WalkingStepPreflightInput revalidated = WalkingStepPreflightPendingInput;
		revalidated.GravityKnown = physicsZone != nullptr;
		revalidated.Gravity = physicsZone
			? physicsZone->ZoneGravity() : vec3(0.0f);
		diagnostic.FallForecastAttempted = physicsZone != nullptr;
		revalidated.FallForecast = physicsZone
			? ForecastWalkingStepFall(this, Location(), Velocity(),
				fallAcceleration, physicsZone, &diagnostic)
			: WalkingFallForecastObservation{};
		diagnostic.FallForecast = revalidated.FallForecast;
		const WalkingStepPreflightResult revalidatedResult =
			EvaluateWalkingStepPreflight(revalidated);
		diagnostic.Reason = revalidatedResult.Reason;
		confirmed = revalidatedResult.Decision
			== WalkingStepPreflightDecision::AuthorizeUnsafeStepVeto;
		if (!confirmed)
			diagnostic.TransitionOutcome = "post_callback_forecast_rejected";
	}

	bool positiveDpsVetoAuthorized = false;
	if (confirmed)
	{
		WalkingStepPreflightAuthorizationCountValue++;
		const WalkingStepPreflightEpisodeUpdate episode =
			UpdateWalkingStepPreflightEpisode(WalkingStepPreflightEpisode, {
				.Authorized = true,
				.PawnLife = this,
				.PawnLifeGeneration = WalkingStepPreflightLifeGeneration,
				.SupportedOrigin = diagnostic.PrecommitOrigin,
				.SemanticTarget = WalkingStepPreflightPendingSemanticTarget,
				.SemanticDestination = diagnostic.SemanticDestination,
				.OriginRadius = 8.0f,
				.DestinationRadius = 8.0f
			});
		WalkingStepPreflightEpisode = episode.State;
		if (episode.AuthorizationStarted)
			WalkingStepPreflightAuthorizableEpisodeCountValue++;
		if (engine->IsBotBenchmarkWalkingPreflightPositiveDpsVetoEnabled())
		{
			WalkingStepPreflightPositiveDpsVetoEligibleCountValue++;
			if (episode.AuthorizationStarted)
				positiveDpsVetoAuthorized = true;
			else
				WalkingStepPreflightPositiveDpsVetoDebouncedCountValue++;
		}
	}

	queueDiagnostic(std::move(diagnostic));
	return positiveDpsVetoAuthorized;
}

void UPawn::RecordWalkingStepPreflightPositiveDpsVetoOutcome(bool applied)
{
	if (applied)
	{
		WalkingStepPreflightPositiveDpsVetoAppliedCountValue++;
		WalkingStepPreflightPositiveDpsVetoForcedReplanCountValue++;
	}
	else
	{
		WalkingStepPreflightPositiveDpsVetoRollbackRejectedCountValue++;
	}
}

void UPawn::QueueWalkingStepPreflightPositiveDpsVetoAction(
	PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord record)
{
	static constexpr size_t maximumQueuedRecords = 1024;
	record.SourcePawnActor = Name.ToString();
	record.Sequence = WalkingStepPreflightPositiveDpsVetoActionSequence++;
	record.LifeGeneration = WalkingStepPreflightLifeGeneration;
	if (WalkingStepPreflightPositiveDpsVetoActions.size() < maximumQueuedRecords)
		WalkingStepPreflightPositiveDpsVetoActions.push_back(std::move(record));
	else
		WalkingStepPreflightPositiveDpsVetoActionOverflowCountValue++;
}

void UPawn::ArmFallingParityRealizedTrace(int walkingIteration,
	uint64_t invocationToken)
{
	if (!WalkingStepPreflightObservedTransactionValid
		|| WalkingStepPreflightObservedIteration != walkingIteration
		|| WalkingStepPreflightObservedInvocation != invocationToken)
		return;
	const PawnMovement::FallingParityRealizedUpdate update =
		PawnMovement::BeginFallingParityRealizedTrace(
			engine->IsBotBenchmarkWalkingPreflightEnabled(),
			IsStockAutonomousPlayerBot(this), true,
			WalkingStepPreflightObservedCorrelation);
	if (!update.EmitRecord)
		return;
	if (FallingParityRealizedTrace.LifecycleActive)
	{
		FinishFallingParityRealizedTrace(
			PawnMovement::FallingParityRealizedOutcome::ContinuityLost);
	}
	FallingParityRealizedTrace = update.State;
	FallingParityRealizedEpisodeCountValue++;
	static constexpr size_t maximumQueuedRecords = 1024;
	if (FallingParityRealizedRecords.size() < maximumQueuedRecords)
		FallingParityRealizedRecords.push_back(update.Record);
	else
		FallingParityRealizedRecordOverflowCountValue++;
}

void UPawn::RecordFallingParityRealizedStep(
	PawnMovement::FallingParityRealizedOutcome outcome,
	const PawnMovement::FallingParityRealizedRecord& evidence)
{
	const PawnMovement::FallingParityRealizedUpdate update =
		PawnMovement::AdvanceFallingParityRealizedTrace(
			FallingParityRealizedTrace, outcome, evidence);
	if (!update.EmitRecord)
		return;
	FallingParityRealizedTrace = update.State;
	FallingParityRealizedStepCountValue++;
	switch (update.Record.Outcome)
	{
	case PawnMovement::FallingParityRealizedOutcome::MatchedClear:
		FallingParityRealizedMatchedStepCountValue++;
		break;
	case PawnMovement::FallingParityRealizedOutcome::MatchedLanding:
		FallingParityRealizedMatchedLandingStepCountValue++;
		break;
	case PawnMovement::FallingParityRealizedOutcome::Mismatch:
		FallingParityRealizedMismatchCountValue++;
		break;
	case PawnMovement::FallingParityRealizedOutcome::CallbackBarrier:
		FallingParityRealizedCallbackBarrierCountValue++;
		break;
	default:
		FallingParityRealizedUnknownCountValue++;
		break;
	}
	static constexpr size_t maximumQueuedRecords = 1024;
	if (FallingParityRealizedRecords.size() < maximumQueuedRecords)
		FallingParityRealizedRecords.push_back(update.Record);
	else
		FallingParityRealizedRecordOverflowCountValue++;
}

void UPawn::ObserveFallingParityRealizedPain()
{
	UZoneInfo* regionZone = Region().Zone;
	UZoneInfo* footZone = FootRegion().Zone;
	if ((!regionZone || !regionZone->bPainZone())
		&& (!footZone || !footZone->bPainZone()))
		return;
	const PawnMovement::FallingParityRealizedUpdate update =
		PawnMovement::ObserveFallingParityRealizedPain(FallingParityRealizedTrace);
	if (!update.EmitRecord)
		return;
	FallingParityRealizedTrace = update.State;
	FallingParityRealizedPainEntryCountValue++;
	static constexpr size_t maximumQueuedRecords = 1024;
	if (FallingParityRealizedRecords.size() < maximumQueuedRecords)
		FallingParityRealizedRecords.push_back(update.Record);
	else
		FallingParityRealizedRecordOverflowCountValue++;
}

void UPawn::FinishFallingParityRealizedTrace(
	PawnMovement::FallingParityRealizedOutcome outcome)
{
	const PawnMovement::FallingParityRealizedUpdate update =
		PawnMovement::FinishFallingParityRealizedTrace(
			FallingParityRealizedTrace, outcome);
	if (!update.EmitRecord)
		return;
	FallingParityRealizedTrace = update.State;
	if (outcome == PawnMovement::FallingParityRealizedOutcome::Died)
		FallingParityRealizedDeathCountValue++;
	else if (outcome == PawnMovement::FallingParityRealizedOutcome::Landed)
		FallingParityRealizedLandingCountValue++;
	else if (outcome == PawnMovement::FallingParityRealizedOutcome::ContinuityLost)
		FallingParityRealizedContinuityLossCountValue++;
	static constexpr size_t maximumQueuedRecords = 1024;
	if (FallingParityRealizedRecords.size() < maximumQueuedRecords)
		FallingParityRealizedRecords.push_back(update.Record);
	else
		FallingParityRealizedRecordOverflowCountValue++;
}

std::vector<PawnMovement::FallingParityRealizedRecord>
	UPawn::DrainFallingParityRealizedRecords()
{
	std::vector<PawnMovement::FallingParityRealizedRecord> records;
	records.swap(FallingParityRealizedRecords);
	return records;
}

void UPawn::QueueFallingHazardForecastSource(
	PawnMovement::FallingHazardForecastSource source)
{
	if (source == PawnMovement::FallingHazardForecastSource::Unknown
		|| !engine->IsBotBenchmarkWalkingPreflightEnabled()
		|| !IsStockAutonomousPlayerBot(this) || Role() != ROLE_Authority
		|| bDeleteMe() || Health() <= 0)
		return;
	if (!FallingHazardObserver)
	{
		FallingHazardObserver =
			std::make_unique<PawnMovement::FallingHazardRuntimeObserver>(
				Name.ToString());
	}
	if (!FallingHazardObserver->HasActiveFallEpisode())
		FallingHazardObserver->BeginFallEpisode();
	if (FallingHazardObserver->GenerationCapacityExhaustedForLife())
		return;
	FallingHazardQueuedSource = source;
}

void UPawn::EnsureFallingHazardGeneration(float physicsSliceElapsed,
	const vec3& acceleration)
{
	if (!engine->IsBotBenchmarkWalkingPreflightEnabled()
		|| !IsStockAutonomousPlayerBot(this) || Role() != ROLE_Authority
		|| bDeleteMe() || Health() <= 0 || Physics() != PHYS_Falling)
	{
		if (FallingHazardObserver
			&& FallingHazardObserver->HasActiveFallEpisode())
			FallingHazardObserver->AbandonFallEpisode();
		FallingHazardPending = {};
		FallingHazardQueuedSource =
			PawnMovement::FallingHazardForecastSource::Unknown;
		return;
	}

	if (!FallingHazardObserver)
	{
		FallingHazardObserver =
			std::make_unique<PawnMovement::FallingHazardRuntimeObserver>(
				Name.ToString());
	}
	else
	{
		FallingHazardObserver->SetSourcePawnActor(Name.ToString());
	}
	if (!FallingHazardObserver->HasActiveFallEpisode())
		FallingHazardObserver->BeginFallEpisode();
	if (FallingHazardObserver->GenerationCapacityExhaustedForLife())
	{
		FallingHazardPending = {};
		FallingHazardQueuedSource =
			PawnMovement::FallingHazardForecastSource::Unknown;
		return;
	}

	if (FallingHazardObserver->HasActiveGeneration()
		&& FallingHazardQueuedSource
			== PawnMovement::FallingHazardForecastSource::CallbackReturnCommit)
	{
		FallingHazardObserver->FinishCallbackBoundary();
	}
	else if (FallingHazardObserver->HasActiveGeneration()
		&& FallingHazardQueuedSource
			== PawnMovement::FallingHazardForecastSource::ExternalImpulseCommit)
	{
		FallingHazardObserver->FinishExternalImpulseBoundary();
	}

	if (FallingHazardObserver->HasActiveGeneration())
	{
		const PawnMovement::FallingHazardForecastState* forecast =
			FallingHazardObserver->ActiveForecast();
		const size_t observed = FallingHazardObserver->Model()
			.ActiveGeneration.SweptSegmentCount;
		if (forecast && forecast->ExpectedSegmentCount == 0)
			return;
		if (forecast && observed < forecast->ExpectedSegmentCount)
		{
			const PawnMovement::FallingHazardForecastExpectedSegment& expected =
				forecast->ExpectedSegments[observed];
			bool continuous = MaximumAbsoluteComponent(
				Location() - expected.Origin)
				<= PawnMovement::FallingHazardForecastVectorTolerance;
			continuous = continuous && MaximumAbsoluteComponent(
				acceleration - forecast->Input.Acceleration)
				<= PawnMovement::FallingHazardForecastVectorTolerance;
			if (continuous
				&& expected.Leg == PawnMovement::FallingHazardSweepLeg::Direct)
			{
				const auto currentZones =
					BuildFallingHazardPointObservation(this, Location());
				const auto& physics = currentZones.Physics;
				const auto& startingPhysics =
					forecast->Input.StartingZones.Physics;
				const bool samePhysicsZone = physics.Identity.Known
					&& startingPhysics.Identity.Known
					&& physics.Identity.ZoneActorId
						== startingPhysics.Identity.ZoneActorId
					&& physics.Identity.ZoneNumber
						== startingPhysics.Identity.ZoneNumber;
				PawnMovement::FallingParityTransition next =
					PawnMovement::BeginFallingParityStep({
						.Location = Location(),
						.Velocity = Velocity()
					}, {
						.Acceleration = acceleration,
						.Gravity = physics.Gravity,
						.ZoneVelocity = physics.ZoneVelocity,
						.GroundSpeed = GroundSpeed(),
						.TerminalVelocity = physics.TerminalVelocity,
						.Elapsed = physicsSliceElapsed,
						.WaterPhysics = currentZones.Foot.WaterZone,
						.Bounce = bBounce()
					});
				continuous = samePhysicsZone
					&& MaximumAbsoluteComponent(
						physics.Gravity - startingPhysics.Gravity)
						<= PawnMovement::FallingHazardForecastVectorTolerance
					&& MaximumAbsoluteComponent(
						physics.ZoneVelocity - startingPhysics.ZoneVelocity)
						<= PawnMovement::FallingHazardForecastVectorTolerance
					&& std::abs(physics.TerminalVelocity
						- startingPhysics.TerminalVelocity)
						<= PawnMovement::FallingHazardForecastVectorTolerance
					&& next.Kind
						== PawnMovement::FallingParityTransitionKind::ProbeDirectSweep
					&& MaximumAbsoluteComponent(
						next.DirectDelta - expected.RequestedDelta)
						<= PawnMovement::FallingHazardForecastVectorTolerance
					&& std::abs(physicsSliceElapsed
						- expected.ElapsedContribution)
						<= PawnMovement::FallingHazardForecastElapsedTolerance;
			}
			if (continuous)
				return;
			FallingHazardObserver->FinishExternalImpulseBoundary();
			FallingHazardQueuedSource = PawnMovement::
				FallingHazardForecastSource::ExternalImpulseCommit;
		}
		else
		{
			FallingHazardObserver->FinishObservationHorizon();
			FallingHazardQueuedSource = PawnMovement::
				FallingHazardForecastSource::HorizonContinuationCommit;
		}
	}

	using namespace PawnMovement;
	FallingHazardForecastInput input;
	input.State.Location = Location();
	input.State.Velocity = Velocity();
	input.Acceleration = acceleration;
	input.GroundSpeed = GroundSpeed();
	input.PhysicsSliceElapsed = physicsSliceElapsed;
	input.Bounce = bBounce();
	input.StartingZones = BuildFallingHazardPointObservation(this, Location());
	const FallingHazardForecastUpdate forecast =
		CompleteFallingHazardForecast(this, input);
	const FallingHazardForecastSource source =
		FallingHazardQueuedSource != FallingHazardForecastSource::Unknown
			? FallingHazardQueuedSource
			: FallingHazardForecastSource::ExistingFallingCommit;
	FallingHazardObserver->ArmGeneration(source, forecast);
	FallingHazardQueuedSource = FallingHazardForecastSource::Unknown;
}

bool UPawn::PrepareFallingHazardSweep(PawnMovement::FallingHazardSweepLeg leg,
	const vec3& origin, const vec3& delta, float elapsedContribution)
{
	if (!FallingHazardObserver
		|| !FallingHazardObserver->HasActiveGeneration()
		|| FallingHazardPending.Active)
		return false;
	const PawnMovement::FallingHazardForecastState* forecast =
		FallingHazardObserver->ActiveForecast();
	if (!forecast)
		return false;
	const size_t ordinal = FallingHazardObserver->Model()
		.ActiveGeneration.SweptSegmentCount;
	if (ordinal >= forecast->ExpectedSegmentCount)
		return false;
	FallingHazardCallbackContinuation.reset();
	FallingHazardPending.Active = true;
	FallingHazardPending.Leg = leg;
	FallingHazardPending.Origin = origin;
	FallingHazardPending.RequestedDelta = delta;
	FallingHazardPending.ElapsedContribution = elapsedContribution;
	return true;
}

bool UPawn::BeginFallingHazardTryMove()
{
	if (FallingHazardTryMoveDepth == std::numeric_limits<uint32_t>::max())
	{
		CancelPendingFallingHazardSweep(false);
		return false;
	}
	if (FallingHazardPending.Active && FallingHazardPending.MoveDepth != 0)
		FallingHazardPending.ReentrantMoveObserved = true;
	FallingHazardTryMoveDepth++;
	if (FallingHazardPending.Active && FallingHazardPending.MoveDepth == 0)
		FallingHazardPending.MoveDepth = FallingHazardTryMoveDepth;
	return true;
}

void UPawn::EndFallingHazardTryMove()
{
	if (FallingHazardTryMoveDepth == 0)
		return;
	const uint32_t endingDepth = FallingHazardTryMoveDepth--;
	if (FallingHazardPending.Active
		&& FallingHazardPending.MoveDepth == endingDepth)
		CancelPendingFallingHazardSweep(false);
}

void UPawn::LatchPendingFallingHazardSweepGeometry(
	const CollisionHit& hit, const MoveCallbackEvidence& callbacks)
{
	if (!FallingHazardPending.Active
		|| FallingHazardPending.GeometryLatched
		|| FallingHazardPending.MoveDepth != FallingHazardTryMoveDepth)
		return;
	FallingHazardPending.GeometryLatched = true;
	FallingHazardPending.Collision = ClassifyFallingHazardCollision(this, hit);
	FallingHazardPending.HitFraction = hit.Fraction;
	FallingHazardPending.HitNormal = hit.Normal;
	FallingHazardPending.MoveCallbackMask = callbacks.Mask;
	const bool partialHit = std::isfinite(hit.Fraction)
		&& hit.Fraction >= 0.0f && hit.Fraction < 1.0f;
	const bool hitPawn = hit.Actor && hit.Actor->IsA("Pawn");
	if (partialHit && !hitPawn)
	{
		if (FallingHazardPending.Leg
			== PawnMovement::FallingHazardSweepLeg::Direct)
		{
			FallingHazardPending.HitWallCallbackExpected = bBounce()
				|| hit.Normal.z <= fallingWalkableNormalZ;
		}
		else if (FallingHazardPending.Leg
			== PawnMovement::FallingHazardSweepLeg::Aligned)
		{
			FallingHazardPending.HitWallCallbackExpected =
				hit.Normal.z <= fallingWalkableNormalZ;
		}
	}
}

void UPawn::CommitPendingFallingHazardSweepAtCenterBoundary(
	const PointRegion& center, bool actorLeavingCallbackDispatched)
{
	if (!FallingHazardPending.Active
		|| !FallingHazardPending.GeometryLatched
		|| FallingHazardPending.MoveDepth != FallingHazardTryMoveDepth)
		return;
	const PointRegion foot = FindRegion({ 0.0f, 0.0f, -CollisionHeight() });
	const PointRegion head = FindRegion({ 0.0f, 0.0f, EyeHeight() });
	CommitPendingFallingHazardSweep(center, foot, head,
		MoveCallbackRegionChange, actorLeavingCallbackDispatched);
}

void UPawn::CommitPendingFallingHazardSweepAtFootBoundary()
{
	if (!FallingHazardPending.Active
		|| !FallingHazardPending.GeometryLatched
		|| FallingHazardPending.MoveDepth != FallingHazardTryMoveDepth)
		return;
	const PointRegion center = Region();
	const PointRegion foot = FindRegion({ 0.0f, 0.0f, -CollisionHeight() });
	const PointRegion head = FindRegion({ 0.0f, 0.0f, EyeHeight() });
	const uint32_t zoneCallbackMask = FootRegion().Zone != foot.Zone
		? MoveCallbackFootRegionChange : 0;
	CommitPendingFallingHazardSweep(center, foot, head,
		zoneCallbackMask, false);
}

void UPawn::CommitPendingFallingHazardSweep(const PointRegion& center,
	const PointRegion& foot, const PointRegion& head,
	uint32_t zoneCallbackMask, bool callbackAlreadyDispatched)
{
	const FallingHazardPendingSweep pending = FallingHazardPending;
	FallingHazardPending = {};
	if (!FallingHazardObserver
		|| !FallingHazardObserver->HasActiveGeneration()
		|| bDeleteMe() || Health() <= 0)
		return;

	using namespace PawnMovement;
	const FallingHazardForecastState* forecast =
		FallingHazardObserver->ActiveForecast();
	if (pending.HitWallCallbackExpected && forecast)
	{
		const auto& candidate = forecast->Result.Continuation;
		if (candidate.Phase != FallingHazardForecastPhase::FullStep)
			FallingHazardCallbackContinuation = candidate;
	}

	FallingHazardRuntimeSweepObservation observation;
	observation.Segment.Leg = pending.Leg;
	observation.Segment.Origin = pending.Origin;
	observation.Segment.RequestedDelta = pending.RequestedDelta;
	observation.Segment.Collision = pending.Collision;
	observation.Segment.HitFraction = pending.HitFraction;
	observation.Segment.HitNormal = pending.HitNormal;
	observation.Segment.Endpoint = Location();
	observation.Segment.ElapsedContribution = pending.ElapsedContribution;
	const float traveledDistance = length(Location() - pending.Origin);
	const float spacing = forecast
		? forecast->Input.MaximumSampleSpacing
		: FallingHazardForecastMaximumSampleSpacing;
	observation.Segment.SampleCount = std::isfinite(traveledDistance)
		&& traveledDistance > FallingHazardForecastVectorTolerance
		? static_cast<size_t>(std::ceil(traveledDistance / spacing)) : 1;

	observation.HarmfulCenterZoneKnown = center.Zone != nullptr;
	observation.InHarmfulCenterZone = center.Zone && center.Zone->bPainZone()
		&& center.Zone->DamagePerSec() > 0;
	observation.CenterZone = FallingHazardZoneIdentity(center);
	observation.HarmfulFootZoneKnown = foot.Zone != nullptr;
	observation.InHarmfulFootZone = foot.Zone && foot.Zone->bPainZone()
		&& foot.Zone->DamagePerSec() > 0;
	observation.FootZone = FallingHazardZoneIdentity(foot);
	observation.PhysicsZone = FallingHazardZoneIdentity(center);
	observation.RegionWaterKnown = center.Zone != nullptr;
	observation.InRegionWater = center.Zone && center.Zone->bWaterZone();
	observation.FootWaterKnown = foot.Zone != nullptr;
	observation.InFootWater = foot.Zone && foot.Zone->bWaterZone();
	observation.HeadWaterKnown = head.Zone != nullptr;
	observation.InHeadWater = head.Zone && head.Zone->bWaterZone();
	observation.CallbackMaskKnown = !pending.ReentrantMoveObserved;
	const uint32_t priorNonZoneCallbacks = pending.MoveCallbackMask
		& ~(MoveCallbackRegionChange | MoveCallbackFootRegionChange
			| MoveCallbackHeadRegionChange | MoveCallbackHitWall);
	uint32_t moveCallbackMask = priorNonZoneCallbacks | zoneCallbackMask;
	observation.CallbackMask = FallingHazardCallbackMask(moveCallbackMask);
	FallingHazardObserver->ObserveSweep(observation);

	const bool actualCallbackObserved = callbackAlreadyDispatched
		|| priorNonZoneCallbacks != 0 || pending.ReentrantMoveObserved;
	const std::optional<FallingHazardTerminal> terminal =
		FallingHazardObserver->LastCompletedTerminal();
	if (actualCallbackObserved)
	{
		FallingHazardQueuedSource =
			FallingHazardForecastSource::CallbackReturnCommit;
	}
	else if (terminal == FallingHazardTerminal::ContinuityLost)
	{
		FallingHazardQueuedSource =
			FallingHazardForecastSource::ExternalImpulseCommit;
	}
}

void UPawn::RecoverFallingHazardCallbackReturn()
{
	if (!FallingHazardObserver
		|| !FallingHazardObserver->HasActiveFallEpisode()
		|| bDeleteMe() || Health() <= 0)
		return;
	FallingHazardQueuedSource =
		PawnMovement::FallingHazardForecastSource::Unknown;
	FinishFallingHazardCallbackBoundary();
	if (!engine->IsBotBenchmarkWalkingPreflightEnabled()
		|| !IsStockAutonomousPlayerBot(this) || Role() != ROLE_Authority
		|| Physics() != PHYS_Falling
		|| FallingHazardObserver->GenerationCapacityExhaustedForLife())
		return;
	FallingHazardQueuedSource =
		PawnMovement::FallingHazardForecastSource::CallbackReturnCommit;
}

void UPawn::CancelPendingFallingHazardSweep(bool callbackBoundary)
{
	if (!FallingHazardPending.Active)
		return;
	if (FallingHazardPending.MoveDepth != 0
		&& FallingHazardPending.MoveDepth != FallingHazardTryMoveDepth)
		return;
	FallingHazardPending = {};
	FallingHazardCallbackContinuation.reset();
	if (!FallingHazardObserver
		|| !FallingHazardObserver->HasActiveGeneration())
		return;
	if (callbackBoundary)
	{
		FallingHazardObserver->FinishCallbackBoundary();
		FallingHazardQueuedSource =
			PawnMovement::FallingHazardForecastSource::CallbackReturnCommit;
	}
	else
		FallingHazardObserver->FinishGeneration(
			PawnMovement::FallingHazardTerminal::InvalidObservation);
}

std::optional<PawnMovement::FallingHazardForecastContinuationSeed>
	UPawn::FinishFallingHazardCallbackBoundary()
{
	FallingHazardPending = {};
	std::optional<PawnMovement::FallingHazardForecastContinuationSeed>
		continuation = FallingHazardCallbackContinuation;
	FallingHazardCallbackContinuation.reset();
	if (!FallingHazardObserver
		|| !FallingHazardObserver->HasActiveGeneration())
		return continuation;
	if (const PawnMovement::FallingHazardForecastState* forecast =
		FallingHazardObserver->ActiveForecast())
	{
		const auto& candidate = forecast->Result.Continuation;
		if (candidate.Phase
			!= PawnMovement::FallingHazardForecastPhase::FullStep)
			continuation = candidate;
	}
	FallingHazardObserver->FinishCallbackBoundary();
	return continuation;
}

void UPawn::ArmFallingHazardContinuation(
	PawnMovement::FallingHazardForecastSource source,
	const PawnMovement::FallingHazardForecastContinuationSeed& continuation,
	float physicsSliceElapsed, const vec3& acceleration)
{
	if (!FallingHazardObserver
		|| !FallingHazardObserver->HasActiveFallEpisode()
		|| FallingHazardObserver->HasActiveGeneration()
		|| FallingHazardObserver->GenerationCapacityExhaustedForLife()
		|| bDeleteMe() || Health() <= 0 || Physics() != PHYS_Falling
		|| bJustTeleported())
		return;
	using namespace PawnMovement;
	FallingHazardForecastInput input;
	input.Phase = continuation.Phase;
	input.State.Location = Location();
	input.State.Velocity = Velocity();
	input.Acceleration = acceleration;
	input.GroundSpeed = GroundSpeed();
	input.PhysicsSliceElapsed = physicsSliceElapsed;
	input.Bounce = bBounce();
	input.StartingZones = BuildFallingHazardPointObservation(this, Location());
	input.Continuation = continuation;
	const FallingHazardForecastUpdate forecast =
		CompleteFallingHazardForecast(this, input);
	const FallingHazardAlignedCommandProvenance commandProvenance = source
		== FallingHazardForecastSource::AlignedContinuationCommit
		? ConsumeFallingHazardAlignedCommandWitness()
		: FallingHazardAlignedCommandProvenance::NotAlignedContinuation;
	if (FallingHazardObserver->ArmGeneration(source, forecast, commandProvenance))
	{
		FallingHazardQueuedSource =
			PawnMovement::FallingHazardForecastSource::Unknown;
	}
}

void UPawn::RecordFallingHazardMovementCommand()
{
	if (FallingHazardMovementCommandToken
		< std::numeric_limits<uint64_t>::max())
	{
		FallingHazardMovementCommandToken++;
	}
	else
	{
		FallingHazardMovementCommandToken = 0;
		PendingFallingHazardAlignedCommandWitness = {};
	}
}

void UPawn::CaptureFallingHazardAlignedCommandWitness(bool staticWorldCollision)
{
	PendingFallingHazardAlignedCommandWitness = {};
	if (!IsStockAutonomousPlayerBot(this) || Role() != ROLE_Authority
		|| !StateFrame || FallingHazardMovementCommandToken == 0)
	{
		return;
	}
	const LatentRunState latentState = StateFrame->LatentState;
	if (!IsMovementLatentState(latentState) || !IsFiniteVector(Destination())
		|| !IsFiniteVector(Acceleration()))
	{
		return;
	}
	if (latentState == LatentRunState::MoveToward
		&& (!MoveTarget() || MoveTarget()->bDeleteMe()))
	{
		return;
	}
	PendingFallingHazardAlignedCommandWitness.Active = true;
	PendingFallingHazardAlignedCommandWitness.StaticWorldCollision =
		staticWorldCollision;
	PendingFallingHazardAlignedCommandWitness.CommandToken =
		FallingHazardMovementCommandToken;
	PendingFallingHazardAlignedCommandWitness.LatentState =
		static_cast<uint8_t>(latentState);
	PendingFallingHazardAlignedCommandWitness.MoveTarget = MoveTarget();
	PendingFallingHazardAlignedCommandWitness.Destination = Destination();
	PendingFallingHazardAlignedCommandWitness.Acceleration = Acceleration();
}

PawnMovement::FallingHazardAlignedCommandProvenance
	UPawn::ConsumeFallingHazardAlignedCommandWitness()
{
	const FallingHazardAlignedCommandWitness witness =
		PendingFallingHazardAlignedCommandWitness;
	PendingFallingHazardAlignedCommandWitness = {};
	using namespace PawnMovement;
	if (!witness.Active)
		return FallingHazardAlignedCommandProvenance::NoCommandWitness;
	if (!witness.StaticWorldCollision)
		return FallingHazardAlignedCommandProvenance::NonStaticCollision;
	if (!StateFrame || !IsMovementLatentState(StateFrame->LatentState)
		|| !IsFiniteVector(Destination()) || !IsFiniteVector(Acceleration())
		|| (StateFrame->LatentState == LatentRunState::MoveToward
			&& (!MoveTarget() || MoveTarget()->bDeleteMe())))
	{
		return FallingHazardAlignedCommandProvenance::NoLiveMovementCommand;
	}
	if (FallingHazardMovementCommandToken != witness.CommandToken)
		return FallingHazardAlignedCommandProvenance::CommandTokenChanged;
	if (static_cast<uint8_t>(StateFrame->LatentState) != witness.LatentState)
		return FallingHazardAlignedCommandProvenance::LatentStateChanged;
	if (MoveTarget() != witness.MoveTarget)
		return FallingHazardAlignedCommandProvenance::MoveTargetChanged;
	if (MaximumAbsoluteComponent(Destination() - witness.Destination)
		> FallingHazardForecastVectorTolerance)
	{
		return FallingHazardAlignedCommandProvenance::DestinationChanged;
	}
	if (MaximumAbsoluteComponent(Acceleration() - witness.Acceleration)
		> FallingHazardForecastVectorTolerance)
	{
		return FallingHazardAlignedCommandProvenance::AccelerationChanged;
	}
	return FallingHazardAlignedCommandProvenance::IntactCommandButNoActionLead;
}

void UPawn::FinishFallingHazardLanding(const CollisionHit& hit,
	bool ditchSupportUnknown)
{
	if (FallingHazardRecovery.ActionActive)
	{
		const std::array<UZoneInfo*, 3> zones = {
			Region().Zone, FootRegion().Zone, HeadRegion().Zone
		};
		const bool safeLanding = std::all_of(zones.begin(), zones.end(),
			[](UZoneInfo* zone) { return IsSafeFallingHazardRecoveryZone(zone); });
		if (safeLanding)
			FallingHazardRecoverySafeLandingCountValue++;
	}
	ResetFallingHazardRecovery();
	FallingHazardPending = {};
	FallingHazardCallbackContinuation.reset();
	FallingHazardQueuedSource =
		PawnMovement::FallingHazardForecastSource::Unknown;
	if (FallingHazardObserver
		&& FallingHazardObserver->HasActiveFallEpisode())
	{
		FallingHazardObserver->FinishLanding(ditchSupportUnknown
			? PawnMovement::FallingHazardCollisionKind::Unknown
			: ClassifyFallingHazardCollision(this, hit));
	}
}

void UPawn::FinishFallingHazardDeath()
{
	if (FallingHazardRecovery.ActionActive)
		FallingHazardRecoveryDeathCountValue++;
	ResetFallingHazardRecovery();
	FallingHazardPending = {};
	FallingHazardCallbackContinuation.reset();
	FallingHazardQueuedSource =
		PawnMovement::FallingHazardForecastSource::Unknown;
	if (FallingHazardObserver
		&& FallingHazardObserver->HasActiveFallEpisode())
		FallingHazardObserver->FinishDeath();
}

const PawnMovement::FallingHazardRuntimeCounters&
	UPawn::FallingHazardRuntimeCounterValues() const
{
	static const PawnMovement::FallingHazardRuntimeCounters empty;
	return FallingHazardObserver ? FallingHazardObserver->Counters() : empty;
}

std::vector<PawnMovement::FallingHazardDiagnosticRecord>
UPawn::DrainFallingHazardDiagnostics()
{
	return FallingHazardObserver
		? FallingHazardObserver->DrainDiagnostics()
		: std::vector<PawnMovement::FallingHazardDiagnosticRecord>{};
}

std::vector<PawnMovement::HazardWaterEgressDiagnosticRecord>
UPawn::DrainHazardWaterEgressDiagnostics()
{
	return HazardWaterEgressObserver
		? HazardWaterEgressObserver->DrainDiagnostics()
		: std::vector<PawnMovement::HazardWaterEgressDiagnosticRecord>{};
}

uint64_t UPawn::HazardWaterEgressDiagnosticOverflowCount() const
{
	return HazardWaterEgressObserver
		? HazardWaterEgressObserver->OverflowCount() : 0;
}

void UPawn::EndWalkingStepPreflightLife()
{
	EndMoveStallRecoveryLife();
	EndHarmfulZoneEscapeLife();
	ResetFallingHazardRecovery();
	if (HazardWaterEgressObserver && HazardWaterEgressObserver->HasActiveEpisode()
		&& IsFiniteVector(Location()) && IsFiniteVector(Destination()))
	{
		HazardWaterEgressObserver->EndLife(Location(),
			MoveTarget() ? MoveTarget()->Name.ToString() : std::string(), Destination());
	}
	ResetHazardSwimEgressObservation();
	HazardSwimEgressLifeId++;
	HazardSwimEgressEpisodeId = 0;
	if (FallingHazardObserver)
		FallingHazardObserver->EndLife();
	FallingHazardPending = {};
	FallingHazardCallbackContinuation.reset();
	FallingHazardQueuedSource =
		PawnMovement::FallingHazardForecastSource::Unknown;
	WalkingStepPreflightPendingConfirmation = false;
	WalkingStepPreflightObservedTransactionValid = false;
	WalkingStepPreflightEpisode = {};
	FallingParityRealizedTrace = {};
	WalkingStepPreflightLifeGeneration++;
}

void UPawn::EndHarmfulZoneEscapeLife()
{
	HarmfulZoneEscape = {};
	HarmfulZoneEscapeGate.Reset();
	HarmfulZoneEscapeLifeId++;
	HarmfulZoneEscapeEpisodeId = 0;
}

std::vector<PawnMovement::WalkingStepPreflightDiagnosticRecord>
	UPawn::DrainWalkingStepPreflightDiagnostics()
{
	std::vector<PawnMovement::WalkingStepPreflightDiagnosticRecord> diagnostics;
	diagnostics.swap(WalkingStepPreflightDiagnostics);
	return diagnostics;
}

bool UPawn::RecordWalkingHitWallDispatch(const CollisionHit& hit,
	const vec3& velocityBeforeCollision, float minHitWallBeforeCallback,
	int physicsBeforeCallback, PawnMovement::WalkingHitWallContactPhase contactPhase,
	PawnMovement::WalkingHitWallBlockerKind blockerBeforeCallback,
	bool callbackDispatched)
{
	using namespace PawnMovement;
	WalkingHitWallDispatchDiagnosticRecord diagnostic;
	diagnostic.SourcePawnActor = Name.ToString();
	diagnostic.ContactPhase = contactPhase;
	diagnostic.HitNormal = hit.Normal;
	diagnostic.Velocity = velocityBeforeCollision;
	diagnostic.MinHitWall = minHitWallBeforeCallback;
	diagnostic.Decision = EvaluateWalkingHitWallDispatch(hit.Normal,
		velocityBeforeCollision, diagnostic.MinHitWall);
	diagnostic.Blocker = blockerBeforeCallback;
	diagnostic.CallbackDispatched = callbackDispatched;
	diagnostic.PhysicsChangedByCallback = callbackDispatched
		&& static_cast<int>(Physics()) != physicsBeforeCallback;
	diagnostic.PawnDeletedByCallback = callbackDispatched && bDeleteMe();
	if (diagnostic.Decision.Valid)
	{
		WalkingHitWallDispatchObservationCountValue++;
		if (diagnostic.Decision.LegacyVerticalWallBand)
			WalkingHitWallDispatchLegacyZBandCountValue++;
		if (diagnostic.Decision.MinHitWallDispatch)
			WalkingHitWallDispatchMinHitWallCountValue++;
		if (diagnostic.Decision.LegacyVerticalWallBand
			!= diagnostic.Decision.MinHitWallDispatch)
		{
			WalkingHitWallDispatchDisagreementCountValue++;
		}
	}
	if (callbackDispatched)
		WalkingHitWallDispatchCallbackCountValue++;

	static constexpr size_t maximumQueuedDiagnostics = 1024;
	diagnostic.Sequence = WalkingHitWallDispatchDiagnosticSequence++;
	if (WalkingHitWallDispatchDiagnostics.size() < maximumQueuedDiagnostics)
		WalkingHitWallDispatchDiagnostics.push_back(std::move(diagnostic));
	else
		WalkingHitWallDispatchDiagnosticOverflowCountValue++;
	if (WalkingHitWallFixtureContactLimit == 0)
		return false;
	if (WalkingHitWallFixtureContactCount < std::numeric_limits<uint32_t>::max())
		WalkingHitWallFixtureContactCount++;
	return WalkingHitWallFixtureContactCount >= WalkingHitWallFixtureContactLimit;
}

std::vector<PawnMovement::WalkingHitWallDispatchDiagnosticRecord>
	UPawn::DrainWalkingHitWallDispatchDiagnostics()
{
	std::vector<PawnMovement::WalkingHitWallDispatchDiagnosticRecord> diagnostics;
	diagnostics.swap(WalkingHitWallDispatchDiagnostics);
	return diagnostics;
}

std::vector<PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord>
	UPawn::DrainWalkingStepPreflightPositiveDpsVetoActions()
{
	std::vector<PawnMovement::WalkingStepPreflightPositiveDpsVetoActionRecord> actions;
	actions.swap(WalkingStepPreflightPositiveDpsVetoActions);
	return actions;
}

void UPawn::AdvancePainLedgeRecovery(float elapsed)
{
	const bool validContext = !bDeleteMe() && Health() > 0 && Physics() == PHYS_Walking;
	const PawnMovement::PainLedgeRecoveryAdvance advance = PawnMovement::AdvancePainLedgeRecovery(
		PainLedgeRecovery, Location(), elapsed, painLedgeRecoveryRadius, validContext);
	PainLedgeRecovery = advance.State;
	if (advance.Escaped)
		PainLedgeRecoveryEscapeCountValue++;
}

bool UPawn::ApplyPainLedgeRecovery(const vec2& requestedDirection)
{
	const PawnMovement::PainLedgeRecoveryRequest request =
		PawnMovement::EvaluatePainLedgeRecoveryRequest(
			PainLedgeRecovery, requestedDirection, painLedgeDirectionAlignment);
	if (request == PawnMovement::PainLedgeRecoveryRequest::Inactive)
		return false;
	if (request == PawnMovement::PainLedgeRecoveryRequest::Clear)
	{
		PainLedgeRecovery = {};
		return false;
	}

	if (!PainLedgeRecovery.RecoveryAttempted)
	{
		PainLedgeRecovery.RecoveryAttempted = true;
		PainLedgeRecoveryAttemptCountValue++;
	}

	const auto directions = PawnMovement::PainLedgeRecoveryCandidateDirections(
		PainLedgeRecovery.UnsafeDirection);
	std::array<PawnMovement::PainLedgeRecoveryCandidateProbe, 3> probes;
	const TraceFlags supportTraceFlags = {
		.movers = true,
		.world = true
	};
	const vec3 traceExtent(CollisionRadius(), CollisionRadius(), CollisionHeight());
	const float supportDepth = std::max(MaxStepHeight() * stepDownDeltaFactor, 1.0f);

	for (size_t index = 0; index < directions.size(); index++)
	{
		const vec3 candidateDelta(directions[index] * painLedgeRecoveryProbeDistance, 0.0f);
		PawnMovement::PainLedgeRecoveryCandidateProbe& probe = probes[index];
		probe.SweepClear = TryMove(candidateDelta, true).Fraction == 1.0f;
		if (!probe.SweepClear)
			continue;

		const vec3 candidate = Location() + candidateDelta;
		const vec3 supportEnd = candidate - vec3(0.0f, 0.0f, supportDepth);
		const CollisionHit support = XLevel()->Collision.TraceFirstHit(
			candidate, supportEnd, this, traceExtent, supportTraceFlags);
		probe.WalkableSupport = support.Fraction < 1.0f && support.Normal.z >= 0.7071f;
		if (!probe.WalkableSupport)
			continue;

		const vec3 supportedCenter = candidate + (supportEnd - candidate) * support.Fraction;
		UZoneInfo* footZone = XLevel()->Model->FindRegion(
			supportedCenter - vec3(0.0f, 0.0f, CollisionHeight()), Level()).Zone;
		probe.NonPainFootRegion = footZone && !footZone->bPainZone();
	}

	const int selected = PawnMovement::SelectPainLedgeRecoveryCandidate(probes);
	Acceleration() = selected >= 0
		? vec3(directions[static_cast<size_t>(selected)] * AccelRate(), 0.0f)
		: vec3(0.0f);
	return true;
}

void UPawn::MoveTo(const vec3& newDestination, float speed)
{
	MoveTarget() = nullptr;
	bReducedSpeed() = false;
	DesiredSpeed() = clamp(speed, 0.0f, MaxDesiredSpeed());
	Destination() = newDestination;
	Focus() = newDestination;
	SetMoveDuration(newDestination - Location());
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::MoveTo;
	RecordFallingHazardMovementCommand();
	RecordMoveStallCommand();
}

void UPawn::MoveToward(UActor* newTarget, float speed)
{
	if (!newTarget)
		return;

	MoveTarget() = newTarget;
	Destination() = newTarget->Location();
	Focus() = newTarget->Location();
	bReducedSpeed() = false;
	DesiredSpeed() = clamp(speed, 0.0f, MaxDesiredSpeed());
	if (UObject::TryCast<UPawn>(newTarget))
		MoveTimer() = 1.0f;
	else
		SetMoveDuration(newTarget->Location() - Location());
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::MoveToward;
	RecordFallingHazardMovementCommand();
	RecordMoveStallCommand();
}

void UPawn::StrafeFacing(const vec3& newDestination, UActor* newTarget)
{
	if (!newTarget)
		return;

	Destination() = newDestination;
	if (engine->LaunchInfo.ue1Version > 219)
		FaceTarget() = newTarget;
	SetMoveDuration(newDestination - Location());
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::StrafeFacing;
	RecordFallingHazardMovementCommand();
	RecordMoveStallCommand();
}

void UPawn::StrafeTo(const vec3& newDestination, const vec3& newFocus)
{
	MoveTarget() = nullptr;
	bReducedSpeed() = false;
	DesiredSpeed() = bIsPlayer() ? MaxDesiredSpeed() : 0.8f * MaxDesiredSpeed();
	Destination() = newDestination;
	Focus() = newFocus;
	SetMoveDuration(newDestination - Location());
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::StrafeTo;
	RecordFallingHazardMovementCommand();
	RecordMoveStallCommand();
}

void UPawn::TurnTo(const vec3& newFocus)
{
	MoveTarget() = nullptr;
	Focus() = newFocus;
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::TurnTo;
}

void UPawn::TurnToward(UActor* newTarget)
{
	if (!newTarget)
		return;

	if (engine->LaunchInfo.ue1Version > 219)
		FaceTarget() = newTarget;
	Focus() = newTarget->Location();
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::TurnToward;
}

void UPawn::WaitForLanding()
{
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::WaitForLanding;
}

void UPawn::SetMoveDuration(const vec3& deltaMove)
{
	float scale = DesiredSpeed() * GetSpeed();
	MoveTimer() = scale > 0.0f ? 1.0f + 1.3f * length(deltaMove) / scale : 0.5f;
}

float UPawn::GetSpeed()
{
	switch (Physics())
	{
	case PHYS_Walking:
	case PHYS_Falling:
	case PHYS_Spider:
		return GroundSpeed();
	case PHYS_Flying:
		return AirSpeed();
	case PHYS_Swimming:
		return WaterSpeed();
	default:
		return 0.0f;
	}
}


/////////////////////////////////////////////////////////////////////////////

bool UPlayerPawn::IsPressing(uint8_t KeyNum)
{
	return engine->inputComposition.IsControlActive(KeyNum);
}

void UPlayerPawn::PausedInput(float elapsed)
{
	if (Role() >= ROLE_SimulatedProxy && Player() && !UObject::TryCast<UCamera>(this))
	{
		CallEvent(this, EventName::PlayerInput, { ExpressionValue::FloatValue(elapsed) });
	}
}

void UPlayerPawn::Tick(float elapsed)
{
	UPawn::Tick(elapsed);

	if (Role() >= ROLE_SimulatedProxy)
	{
		if (Player() && !UObject::TryCast<UCamera>(this))
		{
			CallEvent(this, EventName::PlayerInput, { ExpressionValue::FloatValue(elapsed) });
			CallEvent(this, EventName::PlayerTick, { ExpressionValue::FloatValue(elapsed) });
		}
	}

	// TODO: is this the correct place to set this?
	aForward() = 0.0f;

	// XXX: we reset this here to prevent infinite runaway, which eventually breaks mouselook
	// however, this might break looking with the controller?
	aTurn() = 0.0f;
	aLookUp() = 0.0f;
}

void UPlayerPawn::TickRotating(float elapsed)
{
	if (Physics() == PHYS_Spider)
		return;

	Rotator rot = Rotation();

	// To do: apply RotationRate().Roll

	Rotation() = rot;
}

void UPlayerPawn::LoadProperties()
{
	bInvertMouse() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bInvertMouse", DesktopInputDefaults::InvertMouse);
	MouseSensitivity() = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "MouseSensitivity", 5.0f);
	// TODO: Handle the array property this class has (WeaponPriority)
	DodgeClickTime() = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "DodgeClickTime", 0.25f);
	Bob() = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "Bob", 0.016f);
	MyAutoAim() = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "MyAutoAim", 1.0f);
	if (!engine->LaunchInfo.IsRune())
		Handedness() = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "Handedness", -1.0f);
	bLookUpStairs() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bLookUpStairs", false);
	bSnapToLevel() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bSnapToLevel", false);
	bAlwaysMouseLook() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bAlwaysMouseLook", true);
	bKeyboardLook() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bKeyboardLook", false);
	if (engine->LaunchInfo.ue1Version > 219)
	{
		bMaxMouseSmoothing() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bMaxMouseSmoothing", false);
		bNoFlash() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bNoFlash", false);
		bNoVoices() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bNoVoices", false);
		bMessageBeep() = IniPropertyConverter<bool>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "bMessageBeep", true);
		// NetSpeed is missing
		// LanSpeed is missing
		MouseSmoothThreshold() = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "MouseSmoothThreshold", 0.16f);
		ngWorldSecret() = IniPropertyConverter<std::string>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "ngWorldSecret", "");

		// SE addition: Store/Load the DefaultFOV setting into/from the user.ini file as well
		DefaultFOV() = IniPropertyConverter<float>::FromIniFile(*engine->packages->GetIniFile("user"), "Engine.PlayerPawn", "MainFOV", 90.0f);
	}
}

void UPlayerPawn::SaveConfig()
{
	UPawn::SaveConfig();

	// Note: the code below may no longer be needed. SaveConfig() on UObject saves all unrealscript variables marked as config or globalconfig

	// Not sure why are PlayerPawn's config fields not saved with the base SaveConfig(), but whatever. 
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bInvertMouse", IniPropertyConverter<bool>::ToString(bInvertMouse()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "MouseSensitivity", IniPropertyConverter<float>::ToString(MouseSensitivity()));
	// TODO: Handle the array property this class has (WeaponPriority)
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "DodgeClickTime", IniPropertyConverter<float>::ToString(DodgeClickTime()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "Bob", IniPropertyConverter<float>::ToString(Bob()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "MyAutoAim", IniPropertyConverter<float>::ToString(MyAutoAim()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "Handedness", IniPropertyConverter<float>::ToString(Handedness()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bLookUpStairs", IniPropertyConverter<bool>::ToString(bLookUpStairs()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bSnapToLevel", IniPropertyConverter<bool>::ToString(bSnapToLevel()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bAlwaysMouseLook", IniPropertyConverter<bool>::ToString(bAlwaysMouseLook()));
	engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bKeyboardLook", IniPropertyConverter<bool>::ToString(bKeyboardLook()));
	if (engine->LaunchInfo.ue1Version > 219)
	{
		engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bMaxMouseSmoothing", IniPropertyConverter<bool>::ToString(bMaxMouseSmoothing()));
		engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bNoFlash", IniPropertyConverter<bool>::ToString(bNoFlash()));
		engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bNoVoices", IniPropertyConverter<bool>::ToString(bNoVoices()));
		engine->packages->SetIniValue("user", "Engine.PlayerPawn", "bMessageBeep", IniPropertyConverter<bool>::ToString(bMessageBeep()));
		// NetSpeed is missing
		// LanSpeed is missing
		engine->packages->SetIniValue("user", "Engine.PlayerPawn", "MouseSmoothThreshold", IniPropertyConverter<float>::ToString(MouseSmoothThreshold()));
		engine->packages->SetIniValue("user", "Engine.PlayerPawn", "ngWorldSecret", ngWorldSecret());

		// SE addition: Store/Load the DefaultFOV setting into/from the user.ini file as well
		engine->packages->SetIniValue("user", "Engine.PlayerPawn", "MainFOV", IniPropertyConverter<float>::ToString(DefaultFOV()));
	}
}

/////////////////////////////////////////////////////////////////////////////

void ULevelInfo::UpdateActorZone()
{
	// No zone events are sent by LevelInfo actors
	Region() = FindRegion();
}

PointRegion ULevelInfo::GetLocZone(const vec3& pos)
{
	return XLevel()->Model->FindRegion(pos, Level());
}

/////////////////////////////////////////////////////////////////////////////

UObject* UDecal::AttachDecal(float traceDistance, vec3 decalDir)
{
	if (!Texture())
		return nullptr;

	vec3 traceDirection = -Coords::Rotation(Rotation()).XAxis;

	CollisionHitList hits = XLevel()->Collision.TraceDecal(to_dvec3(Location()), 0.0f, to_dvec3(traceDirection), traceDistance, false);
	if (hits.empty()) return nullptr;

	UModel* model = XLevel()->Model;

	// Do not attempt to create a decal if we hit a surface that's invisible or a fake backdrop
	auto& hit = hits.front();
	if (!hit.Node || (model->Surfaces[hit.Node->Surf].PolyFlags & (PF_FakeBackdrop | PF_Invisible)) != 0)
		return nullptr;

	vec3 N = hit.Normal;
	vec3 pos = Location() + traceDirection * hit.Fraction;

	if (dot(decalDir, decalDir) < 0.01f) // decalDir specifies which direction the decal texture faces. If its zero use a random direction
	{
		vec3 randomDir;
		while (true)
		{
			randomDir = vec3((float)(std::rand() / (double)RAND_MAX), (float)(std::rand() / (double)RAND_MAX), (float)(std::rand() / (double)RAND_MAX)) * 2.0f - 1.0f;
			if (dot(randomDir, randomDir) >= 1.0f)
				break;
		}
		decalDir = randomDir;
	}

	vec3 ydir = -(decalDir - dot(decalDir, N) * N);
	if (dot(ydir, ydir) < 0.01f)
		ydir = normalize(cross(N, std::abs(N.x) > std::abs(N.y) ? vec3(0.0f, 1.0f, 0.0f) : vec3(1.0f, 0.0f, 0.0f)));
	else
		ydir = normalize(ydir);
	vec3 xdir = cross(N, ydir);

	float usize = (float)Texture()->USize();
	float vsize = (float)Texture()->VSize();
	xdir *= DrawScale() * usize * 0.5f;
	ydir *= DrawScale() * vsize * 0.5f;

	static Array<vec3> positions;
	static Array<vec2> uvs;
	static Array<float> edgeDistances;

	// Walk all nodes in the same plane
	BspNode* polynode = hit.NodeHead;
	while (true)
	{
		positions.clear();
		uvs.clear();

		// Place decal on the surface plane
		positions.push_back(pos - xdir - ydir);
		positions.push_back(pos + xdir - ydir);
		positions.push_back(pos + xdir + ydir);
		positions.push_back(pos - xdir + ydir);
		uvs.push_back({ 0.0f, 0.0f });
		uvs.push_back({ usize, 0.0f });
		uvs.push_back({ usize, vsize });
		uvs.push_back({ 0.0f, vsize });

		// Clip to BSP node shape
		int vertCount = (int)positions.size();
		BspVert* v = &model->Vertices[polynode->VertPool];
		for (int j = 0; j < polynode->NumVertices; j++)
		{
			const vec3& edgeStart = model->Points[v[j > 0 ? j - 1 : polynode->NumVertices - 1].Vertex];
			const vec3& edgeEnd = model->Points[v[j].Vertex];
			vec3 planeN = cross(N, edgeEnd - edgeStart); // Note: not normalized as we don't need it
			vec4 plane(planeN, -dot(edgeEnd, planeN));

			// Find vertex distances to edge plane
			edgeDistances.clear();
			for (int i = 0; i < vertCount; i++)
				edgeDistances.push_back(dot(plane, vec4(positions[i], 1.0f)));

			// Insert points at the edge for any line crossing the plane
			for (int i = 0; i < vertCount; i++)
			{
				float dist = edgeDistances[i];
				float distNext = edgeDistances[(i + 1) % vertCount];
				if ((dist > 0.0f && distNext < 0.0f) || (distNext > 0.0f && dist < 0.0f))
				{
					vec3 p = positions[i];
					vec3 pNext = positions[(i + 1) % vertCount];
					vec2 uv = uvs[i];
					vec2 uvNext = uvs[(i + 1) % vertCount];

					// Ray/plane intersection
					float t = -dot(vec4(p, 1.0f), plane) / dot(plane.xyz(), pNext - p);
					vec3 pInsert = mix(p, pNext, t);
					vec2 uvInsert = mix(uv, uvNext, t);

					int insertAt = i + 1;
					positions.insert(positions.begin() + insertAt, pInsert);
					uvs.insert(uvs.begin() + insertAt, uvInsert);
					edgeDistances.insert(edgeDistances.begin() + insertAt, 0.0f);
					vertCount++;
				}
			}

			// Remove points outside
			int i = 0;
			while (i < vertCount)
			{
				if (edgeDistances[i] < 0.0f)
				{
					positions.erase(positions.begin() + i);
					uvs.erase(uvs.begin() + i);
					edgeDistances.erase(edgeDistances.begin() + i);
					vertCount--;
				}
				else
				{
					i++;
				}
			}
		}

		// Add to decals list if we still got anything left to render
		if (!positions.empty())
		{
			LevelDecal leveldecal;
			leveldecal.Decal = this;
			leveldecal.Positions = positions;
			leveldecal.UVs = uvs;
			polynode->Decals.push_back(leveldecal);
			Nodes.push_back(polynode);
		}

		if (polynode->Plane < 0) break;
		polynode = &model->Nodes[polynode->Plane];
	}

	return Level();
}

void UDecal::DetachDecal()
{
	for (BspNode* node : Nodes)
	{
		auto& decals = node->Decals;
		auto it = decals.begin();
		while (it != decals.end())
		{
			auto& leveldecal = *it;
			if (leveldecal.Decal == this)
				it = decals.erase(it);
			else
				++it;
		}
	}
	Nodes.clear();
}

/////////////////////////////////////////////////////////////////////////////

void UWarpZoneInfo::Warp(vec3& Loc, vec3& Vel, Rotator& R)
{
	vec3 origin = WarpCoords().Origin;
	mat3 rotate = WarpCoords().ToMatrix();
	mat3 invrotate = mat3::transpose(rotate);

	// Transform from warp space:
	Loc = (invrotate * Loc) + origin;
	Vel = invrotate * Vel;

	// Rotate the rotator
	Rotator newRotation = Rotator::FromVector(rotate * (Coords::Rotation(R).ToMatrix() * vec4(1.0f, 0.0f, 0.0f, 1.0f)).xyz());
	R.Yaw = newRotation.Yaw;
	R.Pitch = newRotation.Pitch;
}

void UWarpZoneInfo::UnWarp(vec3& Loc, vec3& Vel, Rotator& R)
{
	vec3 origin = WarpCoords().Origin;
	mat3 rotate = WarpCoords().ToMatrix();
	mat3 invrotate = mat3::transpose(rotate);

	// Transform to warp space:
	Loc = rotate * (Loc - origin);
	Vel = rotate * Vel;

	// Rotate the rotator
	Rotator newRotation = Rotator::FromVector(invrotate * (Coords::Rotation(R).ToMatrix() * vec4(1.0f, 0.0f, 0.0f, 1.0f)).xyz());
	R.Yaw = newRotation.Yaw;
	R.Pitch = newRotation.Pitch;
}

/////////////////////////////////////////////////////////////////////////////

void UPakPathNodeIterator::BuildPath(vec3& start, vec3& end)
{
	LogUnimplemented("PathNodeIterator.BuildPath()");
	NodeIndex() = 0;
}

void UPakPathNodeIterator::CheckUPak()
{
	// What does this even check?
}

UNavigationPoint* UPakPathNodeIterator::GetFirst()
{
	LogUnimplemented("PathNodeIterator.GetFirst()");
	//return NodePath().front();
	return nullptr;
}

UNavigationPoint* UPakPathNodeIterator::GetPrevious()
{
	LogUnimplemented("PathNodeIterator.GetPrevious()");
	// if (NodeIndex() > 0)
	// 	NodeIndex()--;
	// return NodePath()[NodeIndex()];
	return nullptr;
}

UNavigationPoint* UPakPathNodeIterator::GetCurrent()
{
	LogUnimplemented("PathNodeIterator.GetCurrent()");
	//return NodePath()[NodeIndex()];
	return nullptr;
}

UNavigationPoint* UPakPathNodeIterator::GetNext()
{
	LogUnimplemented("PathNodeIterator.GetNext()");
	// if (NodeIndex() < NodeCount() - 1)
	// 	NodeIndex()++;
	// return NodePath()[NodeIndex()];
	return nullptr;
}

UNavigationPoint* UPakPathNodeIterator::GetLast()
{
	LogUnimplemented("PathNodeIterator.GetLast()");
	//return NodePath().back();
	return nullptr;
}

UNavigationPoint* UPakPathNodeIterator::GetLastVisible()
{
	LogUnimplemented("PathNodeIterator.GetLastVisible()");
	return nullptr;
}

void UPakPawnPathNodeIterator::SetPawn(UPawn* P)
{
	Pawn() = P;
}

/////////////////////////////////////////////////////////////////////////////

void UPlayerPawnExt::InitRootWindow()
{
	auto dxIni = engine->packages->GetIniFile("System");
	NameString dxRootClassName = dxIni->GetValue("Engine.Engine", "Root", "");
	UClass* cls = engine->packages->FindClass(dxRootClassName);
	if (cls)
	{
		engine->dxRootWindow = UObject::Cast<URootWindow>(engine->packages->GetTransientPackage()->NewObject("dxRootWindow", cls, ObjectFlags::Transient));
		RootWindow() = engine->dxRootWindow;
		engine->dxRootWindow->parentPawn() = this;
		engine->dxRootWindow->bIsVisible() = true;
		engine->dxRootWindow->bIsSensitive() = true;
		engine->dxRootWindow->InitWindow();
	}
}

void UPlayerPawnExt::PreRenderWindows(UCanvas* canvas)
{
	engine->render->PreRenderWindows(canvas);
}

void UPlayerPawnExt::PostRenderWindows(UCanvas* canvas)
{
	engine->render->PostRenderWindows(canvas);
}

////////////////////////////////////////////////////////

void UDeusExPlayer::ConBindEvents()
{
	DeusExConBindEvents();
}

UObject* UDeusExPlayer::CreateDataVaultImageNoteObject()
{
	auto cls = engine->packages->FindClass("DeusEx.DataVaultImageNote");
	return engine->packages->GetTransientPackage()->NewObject("DataVaultImageNote", cls, ObjectFlags::Transient);
}

UObject* UDeusExPlayer::CreateDumpLocationObject()
{
	auto cls = engine->packages->FindClass("DeusEx.DumpLocation");
	return engine->packages->GetTransientPackage()->NewObject("DumpLocation", cls, ObjectFlags::Transient);
}

UObject* UDeusExPlayer::CreateGameDirectoryObject()
{
	if (!m_GameDirectory)
	{
		auto cls = engine->packages->FindClass("DeusEx.GameDirectory");
		m_GameDirectory = Cast<UDXGameDirectory>(engine->packages->GetTransientPackage()->NewObject("GameDirectory", cls, ObjectFlags::Transient));
	}

	return m_GameDirectory;
}

UObject* UDeusExPlayer::CreateHistoryEvent()
{
	auto cls = engine->packages->FindClass("ConSys.ConHistoryEvent");
	return engine->packages->GetTransientPackage()->NewObject("ConHistoryEvent", cls, ObjectFlags::Transient);
}

UObject* UDeusExPlayer::CreateHistoryObject()
{
	auto cls = engine->packages->FindClass("ConSys.ConHistory");
	return Cast<UConHistory>(engine->packages->GetTransientPackage()->NewObject("ConHistory", cls, ObjectFlags::Transient));
}

UObject* UDeusExPlayer::CreateLogObject()
{
	auto cls = engine->packages->FindClass("DeusEx.DeusExLog");
	return engine->packages->GetTransientPackage()->NewObject("DeusExLog", cls, ObjectFlags::Transient);
}

void UDeusExPlayer::DeleteSaveGameFiles(std::optional<std::string> saveDirectory)
{
	LogUnimplemented("DeusExPlayer.DeleteSaveGameFiles");
}

std::string UDeusExPlayer::GetDeusExVersion()
{
	return "1.112fm. Surreal Engine Edition!";
}

void UDeusExPlayer::SaveGame(int saveIndex, std::optional<std::string> saveDesc)
{
	engine->SaveGameInfo.SaveGameSlot = saveIndex;
	engine->SaveGameInfo.SaveGameDescription = *saveDesc;
}

NameString UDeusExPlayer::SetBoolFlagFromString(const std::string& flagNameString, bool bValue)
{
	// Not called directly from script
	LogUnimplemented("DeusExPlayer.SetBoolFlagFromString");
	return {};
}

void UDeusExPlayer::UnloadTexture(UObject* Texture)
{
	// Nothing going on here because SE never unloads textures atm. This is just here so it doesn't throw LogUnimplemented.
}

////////////////////////////////////////////////////////

void UScriptedPawn::AddCarcass(const NameString& CarcassName)
{
	if (NumCarcasses() < 4)
	{
		bool carcassSeen = HaveSeenCarcass(CarcassName);
		if (carcassSeen == false)
		{
			Carcasses()[NumCarcasses()] = CarcassName;
			NumCarcasses() = NumCarcasses() + 1;
		}
	}
}

void UScriptedPawn::ConBindEvents()
{
	DeusExConBindEvents();
}

uint8_t UScriptedPawn::GetAllianceType(const NameString& AllianceName)
{
	auto alliex = AlliancesEx();
	EAllianceType result = EAllianceType::ALLIANCE_Neutral;
	for (int i = 0; i < 16; i++)
	{
		if (alliex[i].AllianceName == AllianceName)
		{
			if ((alliex[i].AllianceLevel < 0.0) || (alliex[i].AllianceAgitation >= 1.0))
			{
				result = EAllianceType::ALLIANCE_Hostile;
			}
			else if (alliex[i].AllianceLevel > 0.0)
			{
				result = EAllianceType::ALLIANCE_Friendly;
			}
			break;
		}
	}

	if (bLikesNeutral() && (result == EAllianceType::ALLIANCE_Neutral))
	{
		result = EAllianceType::ALLIANCE_Friendly;
	}
	if (bReverseAlliances())
	{
		if (result == EAllianceType::ALLIANCE_Friendly)
		{
			return (uint8_t)EAllianceType::ALLIANCE_Hostile;
		}
		if (result == EAllianceType::ALLIANCE_Hostile)
		{
			return (uint8_t)EAllianceType::ALLIANCE_Friendly;
		}
	}
	return (uint8_t)result;
}

uint8_t UScriptedPawn::GetPawnAllianceType(UPawn* QueryPawn)
{
	if (UScriptedPawn* qp = UObject::TryCast<UScriptedPawn>(QueryPawn))
	{
		uint8_t othersAlliance = qp->GetAllianceType(Alliance());
		if (othersAlliance == (uint8_t)EAllianceType::ALLIANCE_Hostile)
		{
			return (uint8_t)EAllianceType::ALLIANCE_Hostile;
		}
	}
	return GetAllianceType(QueryPawn->Alliance());
}

bool UScriptedPawn::HaveSeenCarcass(const NameString& CarcassName)
{
	for (int i = 0; i < NumCarcasses(); i++)
	{
		if (Carcasses()[i] == CarcassName)
		{
			return true;
		}
	}
	return false;
}

bool UScriptedPawn::IsValidEnemy(UPawn* TestEnemy, std::optional<bool> bCheckAlliance)
{
	if(!UObject::TryCast<UScriptedPawn>(TestEnemy) || TestEnemy == this || !bBlockSight() || bDeleteMe() || UObject::TryCast<UScriptedPawn>(TestEnemy)->KillCount() < 1)
		return false;
	if (bCheckAlliance)
	{
		uint8_t retval = GetPawnAllianceType(TestEnemy);
		if (retval != (uint8_t)EAllianceType::ALLIANCE_Hostile)
		{
			return false;
		}
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////

void UDeusExDecoration::ConBindEvents()
{
	DeusExConBindEvents();
}
