#include "UObject/PawnFallingHazardForecast.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
	int Failures = 0;

	struct OwnedObservation : PawnMovement::FallingHazardForecastSweepObservation
	{
		std::vector<PawnMovement::FallingHazardForecastPathSample> Storage;

		OwnedObservation() = default;
		OwnedObservation(const OwnedObservation&) = delete;
		OwnedObservation& operator=(const OwnedObservation&) = delete;
		OwnedObservation(OwnedObservation&& other) noexcept
			: PawnMovement::FallingHazardForecastSweepObservation(
				std::move(other)), Storage(std::move(other.Storage))
		{
			Samples = Storage;
		}
		OwnedObservation& operator=(OwnedObservation&& other) noexcept
		{
			static_cast<PawnMovement::FallingHazardForecastSweepObservation&>(*this) =
				std::move(other);
			Storage = std::move(other.Storage);
			Samples = Storage;
			return *this;
		}

		void Resize(size_t size)
		{
			Storage.resize(size);
			Samples = Storage;
		}
	};

	void Check(bool condition, const std::string& message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			Failures++;
		}
	}

	PawnMovement::FallingHazardZoneId Zone(uint32_t actor, uint32_t number = 0)
	{
		return { .Known = true, .ZoneActorId = actor, .ZoneNumber = number };
	}

	PawnMovement::FallingHazardForecastZoneObservation SafeZone(
		uint32_t actor = 100)
	{
		return {
			.Identity = Zone(actor),
			.Gravity = vec3(0.0f, 0.0f, -950.0f),
			.TerminalVelocity = 2500.0f
		};
	}

	PawnMovement::FallingHazardForecastPointObservation SafePoint()
	{
		return {
			.Center = SafeZone(),
			.Foot = SafeZone(),
			.Head = SafeZone(),
			.Physics = SafeZone()
		};
	}

	PawnMovement::FallingHazardForecastInput DefaultInput()
	{
		return {
			.State = {
				.Location = vec3(0.0f),
				.Velocity = vec3(100.0f, 20.0f, -50.0f)
			},
			.GroundSpeed = 400.0f,
			.PhysicsSliceElapsed = 0.02f,
			.StartingZones = SafePoint()
		};
	}

	OwnedObservation Observation(
		const PawnMovement::FallingHazardForecastProbeRequest& request,
		PawnMovement::FallingHazardCollisionKind collision =
			PawnMovement::FallingHazardCollisionKind::Clear,
		float fraction = 1.0f, const vec3& normal = vec3(0.0f))
	{
		using namespace PawnMovement;
		OwnedObservation result;
		result.Collision = collision;
		result.Fraction = fraction;
		result.Normal = normal;
		const float distance = length(request.Delta * fraction);
		const size_t sampleCount = distance <= FallingHazardForecastVectorTolerance
			? 1u : static_cast<size_t>(std::ceil(
				distance / FallingHazardForecastMaximumSampleSpacing));
		result.Resize(sampleCount);
		for (size_t index = 0; index < result.Storage.size(); index++)
		{
			result.Storage[index].DistanceAlongSegment = distance
				* static_cast<float>(index + 1)
				/ static_cast<float>(result.Storage.size());
			result.Storage[index].Zones = SafePoint();
		}
		return result;
	}

	vec3 NormalWithZ(float z)
	{
		return vec3(std::sqrt(1.0f - z * z), 0.0f, z);
	}

	void TestHarmfulEndpointAndPainMetadata()
	{
		using namespace PawnMovement;
		const auto begun = BeginFallingHazardForecast(DefaultInput());
		Check(!begun.Complete && begun.Probe.Valid
			&& begun.Probe.Leg == FallingHazardSweepLeg::Direct,
			"a valid full forecast requests a direct sweep");
		auto observation = Observation(begun.Probe);
		auto& endpoint = observation.Storage.back().Zones;
		endpoint.Foot = SafeZone(42);
		endpoint.Foot.PainZone = true;
		endpoint.Foot.DamagePerSecond = 20;
		endpoint.Foot.DamageTypeMatchesReduced = true;
		endpoint.Foot.WaterZone = true;
		endpoint.Physics = SafeZone(77);
		endpoint.Physics.Gravity.z = -400.0f;
		endpoint.Center.PainZone = true;
		endpoint.Center.DamagePerSecond = 1;
		endpoint.Head.PainZone = true;
		endpoint.Head.DamagePerSecond = 1;
		const auto result = ObserveFallingHazardForecastSweep(
			begun.State, observation);
		Check(result.Complete
			&& result.Result.Classification
				== FallingHazardForecast::HarmfulPainObserved
			&& result.Result.Reason
				== FallingHazardForecastReason::HarmfulFootPainAtEndpoint
			&& std::abs(result.Result.Elapsed - 0.02f)
				< FallingHazardForecastElapsedTolerance,
			"positive-DPS foot pain at the swept endpoint is harmful");
		Check(result.Result.ExpectedHarmfulFootZone.ZoneActorId == 42
			&& result.Result.ExpectedHarmfulFootZone.ZoneNumber == 0
			&& result.Result.ExpectedHarmfulPhysicsZone.ZoneActorId == 77,
			"harmful endpoint preserves a newly entered physics-zone identity");
		Check(result.Result.ExpectedHarmfulWaterEntry
			&& result.Result.ExpectedHarmfulDamageTypeMatchesReduced
			&& !result.Result.ExpectedBotAvoidanceRelevant
			&& std::abs(result.Result.FirstHarmfulPainDepth - 1.0f) < 0.0001f,
			"simultaneous water, reduced-damage policy, and foot/center/head depth stay separate");

		auto centerWater = Observation(begun.Probe);
		auto& centerWaterEndpoint =
			centerWater.Storage.back().Zones;
		centerWaterEndpoint.Foot = SafeZone(43);
		centerWaterEndpoint.Foot.PainZone = true;
		centerWaterEndpoint.Foot.DamagePerSecond = 5;
		centerWaterEndpoint.Center.WaterZone = true;
		const auto centerWaterResult = ObserveFallingHazardForecastSweep(
			begun.State, centerWater);
		Check(centerWaterResult.Result.Classification
				== FallingHazardForecast::HarmfulPainObserved
			&& !centerWaterResult.Result.ExpectedHarmfulWaterEntry,
			"expected harmful water uses foot-water truth, not center/head depth");

		auto zeroDamage = Observation(begun.Probe,
			FallingHazardCollisionKind::StaticWorld, 0.5f,
			vec3(0.0f, 0.0f, 1.0f));
		auto& zeroDamageEndpoint =
			zeroDamage.Storage.back().Zones.Foot;
		zeroDamageEndpoint.PainZone = true;
		zeroDamageEndpoint.DamagePerSecond = 0;
		const auto zeroDamageResult = ObserveFallingHazardForecastSweep(
			begun.State, zeroDamage);
		Check(zeroDamageResult.Result.Classification
			== FallingHazardForecast::NoHarmfulPainObserved,
			"bPainZone without positive damage per second is not harmful truth");
	}

	void TestStrictStaticLandingThreshold()
	{
		using namespace PawnMovement;
		const auto begun = BeginFallingHazardForecast(DefaultInput());
		auto exact = Observation(begun.Probe,
			FallingHazardCollisionKind::StaticWorld, 0.5f,
			NormalWithZ(FallingParityWalkableNormalZ));
		const auto exactResult = ObserveFallingHazardForecastSweep(
			begun.State, exact);
		Check(exactResult.Complete
			&& exactResult.Result.Classification == FallingHazardForecast::Unknown
			&& exactResult.Result.Reason
				== FallingHazardForecastReason::HitWallCallbackRequired
			&& exactResult.Result.Continuation.Phase
				== FallingHazardForecastPhase::AlignedContinuation,
			"a static normal at exactly 0.7 is a callback boundary, not a landing");

		auto above = Observation(begun.Probe,
			FallingHazardCollisionKind::StaticWorld, 0.5f,
			NormalWithZ(0.7001f));
		const auto aboveResult = ObserveFallingHazardForecastSweep(
			begun.State, above);
		Check(aboveResult.Complete
			&& aboveResult.Result.Classification
				== FallingHazardForecast::NoHarmfulPainObserved
			&& aboveResult.Result.Reason
				== FallingHazardForecastReason::NoHarmfulPainAtStaticLanding,
			"a static normal above 0.7 is the only ordinary landing proof");
	}

	void TestAlignedAndThirdContinuationSeeds()
	{
		using namespace PawnMovement;
		const auto begun = BeginFallingHazardForecast(DefaultInput());
		const auto first = ObserveFallingHazardForecastSweep(begun.State,
			Observation(begun.Probe, FallingHazardCollisionKind::StaticWorld,
				0.25f, vec3(0.0f, 1.0f, 0.0f)));
		Check(first.Result.Reason
			== FallingHazardForecastReason::HitWallCallbackRequired
			&& std::abs(first.Result.Continuation.PrechargedElapsed - 0.02f)
				< FallingHazardForecastElapsedTolerance
			&& length(first.Result.Continuation.ExpectedContinuationOrigin
				- first.State.ExpectedSegments[0].Endpoint) < 0.0001f,
			"the first predicted wall returns an aligned seed with charged elapsed");

		auto alignedInput = DefaultInput();
		alignedInput.Phase = FallingHazardForecastPhase::AlignedContinuation;
		alignedInput.State.Location = first.State.ExpectedSegments[0].Endpoint;
		alignedInput.Continuation = first.Result.Continuation;
		const auto aligned = BeginFallingHazardForecast(alignedInput);
		Check(!aligned.Complete && aligned.Probe.Leg == FallingHazardSweepLeg::Aligned
			&& aligned.Probe.ElapsedContribution == 0.0f,
			"aligned continuation reuses the charged retail slice");
		const auto second = ObserveFallingHazardForecastSweep(aligned.State,
			Observation(aligned.Probe, FallingHazardCollisionKind::StaticWorld,
				0.5f, vec3(1.0f, 0.0f, 0.0f)));
		Check(second.Result.Reason
			== FallingHazardForecastReason::HitWallCallbackRequired
			&& second.Result.Continuation.Phase
				== FallingHazardForecastPhase::ThirdContinuation,
			"the second predicted wall returns a typed third-move seed");

		auto thirdInput = DefaultInput();
		thirdInput.Phase = FallingHazardForecastPhase::ThirdContinuation;
		thirdInput.State.Location = second.State.ExpectedSegments[0].Endpoint;
		thirdInput.Continuation = second.Result.Continuation;
		const auto third = BeginFallingHazardForecast(thirdInput);
		const FallingTwoWallAdjustment expected = BuildFallingTwoWallAdjustment(
			thirdInput.Continuation.DesiredDirection,
			thirdInput.Continuation.PendingDelta,
			thirdInput.Continuation.SecondHitNormal,
			thirdInput.Continuation.FirstHitNormal,
			thirdInput.Continuation.SecondHitFraction);
		Check(!third.Complete
			&& third.Probe.Leg == FallingHazardSweepLeg::TwoWallAdjusted
			&& length(third.Probe.Delta - expected.Delta) < 0.0001f,
			"third continuation uses the exact retail TwoWallAdjust delta");
		const auto continued = ObserveFallingHazardForecastSweep(
			third.State, Observation(third.Probe));
		Check(!continued.Complete
			&& continued.Probe.Leg == FallingHazardSweepLeg::Direct
			&& continued.State.ExpectedSegmentCount == 1,
			"a clear third move reconstructs velocity and continues with a new direct slice");

		const auto laterBegin = BeginFallingHazardForecast(DefaultInput());
		const auto clear = ObserveFallingHazardForecastSweep(
			laterBegin.State, Observation(laterBegin.Probe));
		const auto laterWall = ObserveFallingHazardForecastSweep(clear.State,
			Observation(clear.Probe, FallingHazardCollisionKind::StaticWorld,
				0.5f, vec3(0.0f, 1.0f, 0.0f)));
		Check(std::abs(laterWall.Result.Elapsed - 0.04f)
				< FallingHazardForecastElapsedTolerance
			&& std::abs(laterWall.Result.Continuation.PrechargedElapsed - 0.02f)
				< FallingHazardForecastElapsedTolerance,
			"a callback generation precharges only its consumed retail slice");

		auto displaced = alignedInput;
		displaced.State.Location.x += 1.0f;
		Check(BeginFallingHazardForecast(displaced).Result.Reason
			== FallingHazardForecastReason::InvalidInput,
			"a callback displacement invalidates stale continuation geometry");

		for (auto invalid : { alignedInput, thirdInput })
		{
			invalid.State.Valid = false;
			Check(BeginFallingHazardForecast(invalid).Result.Reason
				== FallingHazardForecastReason::InvalidInput,
				"continuations require a valid falling physics state");
		}
		auto invalidElapsed = alignedInput;
		invalidElapsed.PhysicsSliceElapsed = 0.0f;
		Check(BeginFallingHazardForecast(invalidElapsed).Result.Reason
			== FallingHazardForecastReason::InvalidInput,
			"continuations reject invalid retail physics elapsed time");
	}

	PawnMovement::FallingHazardForecastInput DitchInput()
	{
		using namespace PawnMovement;
		auto input = DefaultInput();
		input.Phase = FallingHazardForecastPhase::ThirdContinuation;
		input.Continuation.Phase = FallingHazardForecastPhase::ThirdContinuation;
		input.Continuation.PrechargedElapsed = 0.02f;
		input.Continuation.IterationStartLocation = vec3(0.0f);
		input.Continuation.IterationOldVelocityZ = -50.0f;
		input.Continuation.PendingDelta = vec3(0.0f, 8.0f, 0.0f);
		input.Continuation.DesiredDirection = vec3(0.0f, 1.0f, 0.0f);
		input.Continuation.FirstHitNormal = normalize(vec3(1.0f, 0.0f, 0.5f));
		input.Continuation.SecondHitNormal = normalize(vec3(-1.0f, 0.0f, 0.5f));
		input.Continuation.SecondHitFraction = 0.25f;
		return input;
	}

	void TestDitchRequiresIndependentStaticWalkableSupport()
	{
		using namespace PawnMovement;
		const auto begun = BeginFallingHazardForecast(DitchInput());
		auto noSupport = Observation(begun.Probe);
		const auto unknown = ObserveFallingHazardForecastSweep(
			begun.State, noSupport);
		Check(unknown.Result.Reason
			== FallingHazardForecastReason::DitchSupportUnknown,
			"retail ditch detection alone is not landing proof");

		const auto retry = BeginFallingHazardForecast(DitchInput());
		auto moverSupport = Observation(retry.Probe);
		moverSupport.IndependentSupportKnown = true;
		moverSupport.IndependentSupportCollision = FallingHazardCollisionKind::Mover;
		moverSupport.IndependentSupportNormal = vec3(0.0f, 0.0f, 1.0f);
		Check(ObserveFallingHazardForecastSweep(retry.State, moverSupport).Result.Reason
			== FallingHazardForecastReason::NonStaticDitchSupport,
			"mover support cannot validate a ditch landing");

		const auto steepRetry = BeginFallingHazardForecast(DitchInput());
		auto steepSupport = Observation(steepRetry.Probe);
		steepSupport.IndependentSupportKnown = true;
		steepSupport.IndependentSupportCollision =
			FallingHazardCollisionKind::StaticWorld;
		steepSupport.IndependentSupportNormal =
			NormalWithZ(FallingParityWalkableNormalZ);
		Check(ObserveFallingHazardForecastSweep(
			steepRetry.State, steepSupport).Result.Reason
			== FallingHazardForecastReason::NonWalkableDitchSupport,
			"static ditch support at exactly 0.7 is still non-walkable");

		const auto supported = BeginFallingHazardForecast(DitchInput());
		auto support = Observation(supported.Probe);
		support.IndependentSupportKnown = true;
		support.IndependentSupportCollision =
			FallingHazardCollisionKind::StaticWorld;
		support.IndependentSupportNormal = vec3(0.0f, 0.0f, 1.0f);
		const auto landed = ObserveFallingHazardForecastSweep(
			supported.State, support);
		Check(landed.Result.Classification
			== FallingHazardForecast::NoHarmfulPainObserved,
			"independent static walkable support validates a ditch landing");
	}

	void TestTransientWaterAndZoneChangesFailOpen()
	{
		using namespace PawnMovement;
		auto input = DefaultInput();
		input.Phase = FallingHazardForecastPhase::AlignedContinuation;
		input.Continuation.Phase = input.Phase;
		input.Continuation.PrechargedElapsed = 0.02f;
		input.Continuation.IterationStartLocation = vec3(0.0f);
		input.Continuation.IterationOldVelocityZ = -50.0f;
		input.Continuation.PendingDelta = vec3(50.0f, 0.0f, 0.0f);
		input.Continuation.DesiredDirection = vec3(1.0f, 0.0f, 0.0f);
		input.Continuation.FirstHitNormal = vec3(0.0f, 1.0f, 0.0f);

		const auto transientBegin = BeginFallingHazardForecast(input);
		auto transient = Observation(transientBegin.Probe);
		transient.Storage[0].Zones.Foot = SafeZone(42);
		transient.Storage[0].Zones.Foot.PainZone = true;
		transient.Storage[0].Zones.Foot.DamagePerSecond = 5;
		const auto transientResult = ObserveFallingHazardForecastSweep(
			transientBegin.State, transient);
		Check(transientResult.Result.Reason
			== FallingHazardForecastReason::TransientHarmfulPain
			&& transientResult.Result.TransientHarmfulPainObserved,
			"intermediate pain that is absent at the endpoint is diagnostic Unknown");

		const auto endpointBegin = BeginFallingHazardForecast(input);
		auto endpoint = Observation(endpointBegin.Probe);
		endpoint.Storage[0].Zones.Foot = SafeZone(42);
		endpoint.Storage[0].Zones.Foot.PainZone = true;
		endpoint.Storage[0].Zones.Foot.DamagePerSecond = 5;
		auto& finalFoot = endpoint.Storage.back().Zones.Foot;
		finalFoot = SafeZone(43);
		finalFoot.PainZone = true;
		finalFoot.DamagePerSecond = 5;
		const auto endpointResult = ObserveFallingHazardForecastSweep(
			endpointBegin.State, endpoint);
		Check(endpointResult.Result.Classification
			== FallingHazardForecast::HarmfulPainObserved
			&& endpointResult.Result.TransientHarmfulPainObserved,
			"an intermediate harmful sample remains diagnostic when the endpoint is also harmful");

		const auto waterBegin = BeginFallingHazardForecast(input);
		auto water = Observation(waterBegin.Probe);
		water.Storage[0].Zones.Foot.WaterZone = true;
		Check(ObserveFallingHazardForecastSweep(waterBegin.State, water).Result.Reason
			== FallingHazardForecastReason::WaterBeforeHarm,
			"water before a harmful endpoint invalidates dry falling integration");

		const auto physicsBegin = BeginFallingHazardForecast(input);
		auto physics = Observation(physicsBegin.Probe);
		physics.Storage[0].Zones.Physics.Identity = Zone(101);
		Check(ObserveFallingHazardForecastSweep(
			physicsBegin.State, physics).Result.Reason
			== FallingHazardForecastReason::PhysicsZoneChanged,
			"a changed physics-zone identity fails open");

		const auto environmentBegin = BeginFallingHazardForecast(input);
		auto environment = Observation(environmentBegin.Probe);
		environment.Storage[0].Zones.Physics.Gravity.z = -900.0f;
		Check(ObserveFallingHazardForecastSweep(
			environmentBegin.State, environment).Result.Reason
			== FallingHazardForecastReason::PhysicsEnvironmentChanged,
			"changed gravity fails open even within one physics-zone identity");

		const auto unknownBegin = BeginFallingHazardForecast(input);
		auto unknown = Observation(unknownBegin.Probe);
		unknown.Storage[0].Zones.Head.Identity.Known = false;
		Check(ObserveFallingHazardForecastSweep(
			unknownBegin.State, unknown).Result.Reason
			== FallingHazardForecastReason::UnknownZoneSample,
			"unknown center/foot/head/physics identity fails open");

		auto invalidStart = input;
		invalidStart.StartingZones.Physics.Identity.ZoneActorId = 0;
		Check(BeginFallingHazardForecast(invalidStart).Result.Reason
			== FallingHazardForecastReason::InvalidInput,
			"a known zone requires a positive deterministic actor identity");

		const auto invalidSampleBegin = BeginFallingHazardForecast(input);
		auto invalidSample = Observation(invalidSampleBegin.Probe);
		invalidSample.Storage[0].Zones.Head.Identity.ZoneActorId = 0;
		Check(ObserveFallingHazardForecastSweep(
			invalidSampleBegin.State, invalidSample).Result.Reason
			== FallingHazardForecastReason::UnknownZoneSample,
			"sampled zones reject identities the telemetry schema cannot encode");

		const auto callbackBegin = BeginFallingHazardForecast(input);
		auto callback = Observation(callbackBegin.Probe);
		callback.Storage[0].Zones.Center.Identity = Zone(102);
		Check(ObserveFallingHazardForecastSweep(
			callbackBegin.State, callback).Result.Reason
			== FallingHazardForecastReason::ZoneTransitionRequiresCallback,
			"a nonharmful point-zone transition requires callback reforecasting");
	}

	void TestCapsUnstableContactsAndCoverage()
	{
		using namespace PawnMovement;
		for (const auto collision : {
			FallingHazardCollisionKind::Mover,
			FallingHazardCollisionKind::DynamicActor })
		{
			const auto begun = BeginFallingHazardForecast(DefaultInput());
			const auto result = ObserveFallingHazardForecastSweep(begun.State,
				Observation(begun.Probe, collision, 0.5f, vec3(1.0f, 0.0f, 0.0f)));
			Check(result.Result.Reason == (collision == FallingHazardCollisionKind::Mover
				? FallingHazardForecastReason::MoverCollisionUnknown
				: FallingHazardForecastReason::DynamicCollisionUnknown),
				"mover and dynamic contacts fail open distinctly");
		}
		const auto harmfulMoverBegin = BeginFallingHazardForecast(DefaultInput());
		auto harmfulMover = Observation(harmfulMoverBegin.Probe,
			FallingHazardCollisionKind::Mover, 0.5f, vec3(1.0f, 0.0f, 0.0f));
		auto& harmfulMoverFoot =
			harmfulMover.Storage.back().Zones.Foot;
		harmfulMoverFoot = SafeZone(42);
		harmfulMoverFoot.PainZone = true;
		harmfulMoverFoot.DamagePerSecond = 5;
		Check(ObserveFallingHazardForecastSweep(
			harmfulMoverBegin.State, harmfulMover).Result.Reason
			== FallingHazardForecastReason::MoverCollisionUnknown,
			"even observed endpoint pain on a mover cannot prove static-world attribution");

		auto horizonInput = DefaultInput();
		horizonInput.MaximumElapsed = 0.02f;
		const auto horizonBegin = BeginFallingHazardForecast(horizonInput);
		Check(ObserveFallingHazardForecastSweep(horizonBegin.State,
			Observation(horizonBegin.Probe)).Result.Reason
			== FallingHazardForecastReason::ElapsedHorizonExceeded,
			"elapsed horizon stops before another direct segment");

		auto segmentInput = DefaultInput();
		segmentInput.MaximumSegments = 1;
		const auto segmentBegin = BeginFallingHazardForecast(segmentInput);
		Check(ObserveFallingHazardForecastSweep(segmentBegin.State,
			Observation(segmentBegin.Probe)).Result.Reason
			== FallingHazardForecastReason::SegmentCapExceeded,
			"the explicit swept-segment cap fails open");

		auto pathInput = DefaultInput();
		pathInput.MaximumPathDistance = 1.0f;
		const auto pathBegin = BeginFallingHazardForecast(pathInput);
		Check(ObserveFallingHazardForecastSweep(pathBegin.State,
			Observation(pathBegin.Probe)).Result.Reason
			== FallingHazardForecastReason::PathDistanceCapExceeded,
			"the path-distance cap fails open");

		const auto coverageBegin = BeginFallingHazardForecast(DefaultInput());
		auto coverage = Observation(coverageBegin.Probe);
		coverage.Resize(0);
		Check(ObserveFallingHazardForecastSweep(
			coverageBegin.State, coverage).Result.Reason
			== FallingHazardForecastReason::IncompleteSampleCoverage,
			"missing endpoint sampling fails open");

		const auto sampleBegin = BeginFallingHazardForecast(DefaultInput());
		auto exhausted = Observation(sampleBegin.Probe);
		exhausted.SampleCapExhausted = true;
		Check(ObserveFallingHazardForecastSweep(
			sampleBegin.State, exhausted).Result.Reason
			== FallingHazardForecastReason::SampleCapExceeded,
			"adapter sample-cap exhaustion is explicit");

		const auto invalidBegin = BeginFallingHazardForecast(DefaultInput());
		auto invalid = Observation(invalidBegin.Probe);
		invalid.Resize(FallingHazardForecastMaximumSamples + 1);
		Check(ObserveFallingHazardForecastSweep(
			invalidBegin.State, invalid).Result.Reason
			== FallingHazardForecastReason::SampleCapExceeded,
			"oversized adapter sample views stop before buffer access");

		auto totalSampleInput = DefaultInput();
		totalSampleInput.MaximumSamples = 1;
		const auto totalSampleBegin = BeginFallingHazardForecast(totalSampleInput);
		const auto secondSample = ObserveFallingHazardForecastSweep(
			totalSampleBegin.State, Observation(totalSampleBegin.Probe));
		Check(!secondSample.Complete,
			"one valid sample may consume the complete configured total budget");
		Check(ObserveFallingHazardForecastSweep(secondSample.State,
			Observation(secondSample.Probe)).Result.Reason
			== FallingHazardForecastReason::SampleCapExceeded,
			"the explicit total sample cap applies across segments");

		auto tinySpacingInput = DefaultInput();
		tinySpacingInput.MaximumSampleSpacing =
			std::numeric_limits<float>::denorm_min();
		const auto tinySpacingBegin = BeginFallingHazardForecast(tinySpacingInput);
		FallingHazardForecastSweepObservation tinySpacingObservation;
		tinySpacingObservation.Collision = FallingHazardCollisionKind::Clear;
		tinySpacingObservation.Fraction = 1.0f;
		Check(ObserveFallingHazardForecastSweep(
			tinySpacingBegin.State, tinySpacingObservation).Result.Reason
			== FallingHazardForecastReason::SampleCapExceeded,
			"pathological sample spacing cannot overflow the required count");

		auto horizonStressInput = DefaultInput();
		horizonStressInput.State.Velocity = vec3(1.0f, 0.0f, 0.0f);
		horizonStressInput.StartingZones.Physics.Gravity = vec3(0.0f);
		auto horizonStress = BeginFallingHazardForecast(horizonStressInput);
		for (size_t index = 0; index < 300 && !horizonStress.Complete; index++)
		{
			auto observation = Observation(horizonStress.Probe);
			for (auto& sample : observation.Storage)
				sample.Zones.Physics.Gravity = vec3(0.0f);
			horizonStress = ObserveFallingHazardForecastSweep(
				horizonStress.State, observation);
		}
		Check(horizonStress.Complete
			&& horizonStress.Result.Reason
				== FallingHazardForecastReason::ElapsedHorizonExceeded
			&& horizonStress.Result.SegmentCount == 200
			&& std::abs(horizonStress.Result.Elapsed - 4.0f) < 0.00001f,
			"the four-second horizon accepts exactly 200 retail slices");

		auto segmentStressInput = horizonStressInput;
		segmentStressInput.PhysicsSliceElapsed = 0.015f;
		auto segmentStress = BeginFallingHazardForecast(segmentStressInput);
		for (size_t index = 0; index < 300 && !segmentStress.Complete; index++)
		{
			auto observation = Observation(segmentStress.Probe);
			for (auto& sample : observation.Storage)
				sample.Zones.Physics.Gravity = vec3(0.0f);
			segmentStress = ObserveFallingHazardForecastSweep(
				segmentStress.State, observation);
		}
		Check(segmentStress.Complete
			&& segmentStress.Result.Reason
				== FallingHazardForecastReason::SegmentCapExceeded
			&& segmentStress.Result.SegmentCount
				== FallingHazardForecastMaximumSegments,
			"the 256-segment boundary stops without indexing past fixed storage");
	}

	void TestDeterministicExpectedSegmentComparison()
	{
		using namespace PawnMovement;
		const auto begun = BeginFallingHazardForecast(DefaultInput());
		const auto landed = ObserveFallingHazardForecastSweep(begun.State,
			Observation(begun.Probe, FallingHazardCollisionKind::StaticWorld,
				0.5f, vec3(0.0f, 0.0f, 1.0f)));
		const auto expected = landed.State.ExpectedSegments[0];
		auto close = expected;
		close.Endpoint.x += 0.0009f;
		close.ElapsedContribution += 0.0000009f;
		Check(FallingHazardForecastExpectedSegmentMatches(expected, close),
			"endpoint .001 and elapsed 1e-6 tolerances accept deterministic rounding");
		close.Endpoint.x += 0.0002f;
		Check(!FallingHazardForecastExpectedSegmentMatches(expected, close),
			"expected-segment comparison rejects endpoint drift beyond .001");
	}
}

int main()
{
	TestHarmfulEndpointAndPainMetadata();
	TestStrictStaticLandingThreshold();
	TestAlignedAndThirdContinuationSeeds();
	TestDitchRequiresIndependentStaticWalkableSupport();
	TestTransientWaterAndZoneChangesFailOpen();
	TestCapsUnstableContactsAndCoverage();
	TestDeterministicExpectedSegmentComparison();
	if (Failures == 0)
		std::cout << "Pawn falling hazard forecast tests passed\n";
	return Failures == 0 ? 0 : 1;
}
