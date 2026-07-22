#pragma once

#include "Math/vec.h"
#include <cstdint>
#include <string_view>

namespace WebXRMuzzleOrigin
{
	enum class StockHitscanPolicy : uint32_t
	{
		None,
		Enforcer,
		Minigun2,
		SniperRifle,
		ShockRifle,
		SuperShockRifle,
		ChainSawTrace,
		ChainSawSlash
	};

	enum class RequestRejection : uint32_t
	{
		None,
		RemotePawn,
		StaleCurrentWeapon,
		WrongOwner,
		UnqualifiedPath,
		UntrackedPose,
		NonFinitePose,
		MissingCalibration,
		ProductionDisabled
	};

	struct RequestFacts
	{
		bool LocalPawn = false;
		bool CurrentWeapon = false;
		bool WeaponOwnedByPawn = false;
		bool QualifiedPath = false;
		bool PoseTracked = false;
		bool PoseFinite = false;
		bool Calibrated = false;
		bool ProductionEnabled = false;
	};

	struct EndpointTranslation
	{
		vec3 Start = vec3(0.0f);
		vec3 End = vec3(0.0f);
		vec3 Delta = vec3(0.0f);
	};

	bool IsFiniteVector(const vec3& value);
	RequestRejection ValidateRequest(const RequestFacts& facts);
	StockHitscanPolicy ClassifyStockHitscanRequest(
		std::string_view runtimePackage, std::string_view runtimeClass,
		std::string_view declaringPackage, std::string_view declaringClass,
		std::string_view declaringState, std::string_view functionName);
	bool IsExactTraceShotSink(std::string_view packageName,
		std::string_view className, std::string_view declaringState,
		std::string_view functionName);
	bool IsExactCalcDrawOffsetResult(std::string_view packageName,
		std::string_view className, std::string_view declaringState,
		std::string_view functionName);
	bool TryTranslateEndpoints(const vec3& stockStart, const vec3& stockEnd,
		const vec3& desiredStart, EndpointTranslation& result);
	bool TryTranslateLocation(const vec3& stockLocation, const vec3& desiredLocation,
		vec3& translatedLocation, vec3& delta);
	bool RunSelfTest();
}
