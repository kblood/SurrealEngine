#include "Precomp.h"
#include "WebXRMuzzleOrigin.h"

#include <cctype>
#include <limits>

namespace WebXRMuzzleOrigin
{
	namespace
	{
		bool EqualsIgnoreCase(std::string_view left, std::string_view right)
		{
			if (left.size() != right.size())
				return false;
			for (size_t index = 0; index < left.size(); index++)
			{
				const auto a = static_cast<unsigned char>(left[index]);
				const auto b = static_cast<unsigned char>(right[index]);
				if (std::tolower(a) != std::tolower(b))
					return false;
			}
			return true;
		}

		bool IsGlobalCall(std::string_view declaringState, std::string_view functionName,
			std::string_view expectedFunction)
		{
			return declaringState.empty() && EqualsIgnoreCase(functionName, expectedFunction);
		}

		bool IsNear(const vec3& left, const vec3& right, float epsilon = 0.0001f)
		{
			return std::fabs(left.x - right.x) <= epsilon &&
				std::fabs(left.y - right.y) <= epsilon &&
				std::fabs(left.z - right.z) <= epsilon;
		}
	}

	bool IsFiniteVector(const vec3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) &&
			std::isfinite(value.z);
	}

	RequestRejection ValidateRequest(const RequestFacts& facts)
	{
		if (!facts.LocalPawn)
			return RequestRejection::RemotePawn;
		if (!facts.CurrentWeapon)
			return RequestRejection::StaleCurrentWeapon;
		if (!facts.WeaponOwnedByPawn)
			return RequestRejection::WrongOwner;
		if (!facts.QualifiedPath)
			return RequestRejection::UnqualifiedPath;
		if (!facts.PoseTracked)
			return RequestRejection::UntrackedPose;
		if (!facts.PoseFinite)
			return RequestRejection::NonFinitePose;
		if (!facts.Calibrated)
			return RequestRejection::MissingCalibration;
		if (!facts.ProductionEnabled)
			return RequestRejection::ProductionDisabled;
		return RequestRejection::None;
	}

	StockHitscanPolicy ClassifyStockHitscanRequest(
		std::string_view runtimePackage, std::string_view runtimeClass,
		std::string_view declaringPackage, std::string_view declaringClass,
		std::string_view declaringState, std::string_view functionName)
	{
		if (!EqualsIgnoreCase(runtimePackage, "Botpack") || !declaringState.empty() ||
			!EqualsIgnoreCase(declaringPackage, "Botpack"))
			return StockHitscanPolicy::None;

		auto exact = [&](std::string_view expectedRuntimeClass,
			std::string_view expectedDeclaringClass)
		{
			return EqualsIgnoreCase(runtimeClass, expectedRuntimeClass) &&
				EqualsIgnoreCase(declaringClass, expectedDeclaringClass) &&
				EqualsIgnoreCase(functionName, "TraceFire");
		};

		if (exact("Enforcer", "Enforcer"))
			return StockHitscanPolicy::Enforcer;
		if (exact("Minigun2", "Minigun2"))
			return StockHitscanPolicy::Minigun2;
		if (exact("SniperRifle", "SniperRifle"))
			return StockHitscanPolicy::SniperRifle;
		if (exact("ShockRifle", "ShockRifle"))
			return StockHitscanPolicy::ShockRifle;
		// SuperShockRifle inherits Botpack.ShockRifle.TraceFire.
		if (exact("SuperShockRifle", "ShockRifle"))
			return StockHitscanPolicy::SuperShockRifle;
		if (exact("ChainSaw", "ChainSaw"))
			return StockHitscanPolicy::ChainSawTrace;
		if (EqualsIgnoreCase(runtimeClass, "ChainSaw") &&
			EqualsIgnoreCase(declaringClass, "ChainSaw") &&
			EqualsIgnoreCase(functionName, "Slash"))
			return StockHitscanPolicy::ChainSawSlash;
		return StockHitscanPolicy::None;
	}

	bool IsExactTraceShotSink(std::string_view packageName,
		std::string_view className, std::string_view declaringState,
		std::string_view functionName)
	{
		return EqualsIgnoreCase(packageName, "Engine") &&
			EqualsIgnoreCase(className, "Pawn") &&
			IsGlobalCall(declaringState, functionName, "TraceShot");
	}

	bool IsExactCalcDrawOffsetResult(std::string_view packageName,
		std::string_view className, std::string_view declaringState,
		std::string_view functionName)
	{
		return EqualsIgnoreCase(packageName, "Engine") &&
			EqualsIgnoreCase(className, "Inventory") &&
			IsGlobalCall(declaringState, functionName, "CalcDrawOffset");
	}

	bool TryTranslateEndpoints(const vec3& stockStart, const vec3& stockEnd,
		const vec3& desiredStart, EndpointTranslation& result)
	{
		if (!IsFiniteVector(stockStart) || !IsFiniteVector(stockEnd) ||
			!IsFiniteVector(desiredStart))
			return false;
		const vec3 delta = desiredStart - stockStart;
		const vec3 translatedEnd = stockEnd + delta;
		if (!IsFiniteVector(delta) || !IsFiniteVector(translatedEnd))
			return false;
		result = { desiredStart, translatedEnd, delta };
		return true;
	}

	bool TryTranslateLocation(const vec3& stockLocation, const vec3& desiredLocation,
		vec3& translatedLocation, vec3& delta)
	{
		if (!IsFiniteVector(stockLocation) || !IsFiniteVector(desiredLocation))
			return false;
		delta = desiredLocation - stockLocation;
		if (!IsFiniteVector(delta))
			return false;
		translatedLocation = stockLocation + delta;
		return IsFiniteVector(translatedLocation);
	}

	bool RunSelfTest()
	{
		using Policy = StockHitscanPolicy;
		using Reject = RequestRejection;

		if (ClassifyStockHitscanRequest("Botpack", "Enforcer", "Botpack", "Enforcer", {}, "TraceFire") != Policy::Enforcer ||
			ClassifyStockHitscanRequest("botpack", "MINIGUN2", "BOTPACK", "minigun2", {}, "tracefire") != Policy::Minigun2 ||
			ClassifyStockHitscanRequest("Botpack", "SniperRifle", "Botpack", "SniperRifle", {}, "TraceFire") != Policy::SniperRifle ||
			ClassifyStockHitscanRequest("Botpack", "ShockRifle", "Botpack", "ShockRifle", {}, "TraceFire") != Policy::ShockRifle ||
			ClassifyStockHitscanRequest("Botpack", "SuperShockRifle", "Botpack", "ShockRifle", {}, "TraceFire") != Policy::SuperShockRifle ||
			ClassifyStockHitscanRequest("Botpack", "ChainSaw", "Botpack", "ChainSaw", {}, "TraceFire") != Policy::ChainSawTrace ||
			ClassifyStockHitscanRequest("Botpack", "ChainSaw", "Botpack", "ChainSaw", {}, "Slash") != Policy::ChainSawSlash)
			return false;

		const struct
		{
			std::string_view RuntimePackage, RuntimeClass, DeclaringPackage,
				DeclaringClass, State, Function;
		} rejectedPaths[] = {
			{ "ExampleMod", "Enforcer", "Botpack", "Enforcer", {}, "TraceFire" },
			{ "Botpack", "Enforcer", "ExampleMod", "Enforcer", {}, "TraceFire" },
			{ "Botpack", "Enforcer", "Botpack", "SharedName", {}, "TraceFire" },
			{ "Botpack", "Enforcer", "Engine", "Weapon", {}, "TraceFire" },
			{ "Botpack", "ShockRifle", "Botpack", "ShockRifle", {}, "ProjectileFire" },
			{ "Botpack", "SuperShockRifle", "Botpack", "SuperShockRifle", {}, "TraceFire" },
			{ "Botpack", "ChainSaw", "Botpack", "ChainSaw", "Firing", "TraceFire" }
		};
		for (const auto& path : rejectedPaths)
		{
			if (ClassifyStockHitscanRequest(path.RuntimePackage, path.RuntimeClass,
				path.DeclaringPackage, path.DeclaringClass, path.State,
				path.Function) != Policy::None)
				return false;
		}

		if (!IsExactTraceShotSink("Engine", "Pawn", {}, "TraceShot") ||
			IsExactTraceShotSink("Botpack", "Pawn", {}, "TraceShot") ||
			IsExactTraceShotSink("Engine", "Pawn", "Firing", "TraceShot") ||
			!IsExactCalcDrawOffsetResult("Engine", "Inventory", {}, "CalcDrawOffset") ||
			IsExactCalcDrawOffsetResult("Engine", "Weapon", {}, "CalcDrawOffset"))
			return false;

		RequestFacts facts{ true, true, true, true, true, true, true, true };
		if (ValidateRequest(facts) != Reject::None)
			return false;
		struct RejectionCase { bool RequestFacts::*Field; Reject Expected; };
		const RejectionCase rejectionCases[] = {
			{ &RequestFacts::LocalPawn, Reject::RemotePawn },
			{ &RequestFacts::CurrentWeapon, Reject::StaleCurrentWeapon },
			{ &RequestFacts::WeaponOwnedByPawn, Reject::WrongOwner },
			{ &RequestFacts::QualifiedPath, Reject::UnqualifiedPath },
			{ &RequestFacts::PoseTracked, Reject::UntrackedPose },
			{ &RequestFacts::PoseFinite, Reject::NonFinitePose },
			{ &RequestFacts::Calibrated, Reject::MissingCalibration },
			{ &RequestFacts::ProductionEnabled, Reject::ProductionDisabled }
		};
		for (const RejectionCase& rejection : rejectionCases)
		{
			RequestFacts rejected = facts;
			rejected.*(rejection.Field) = false;
			if (ValidateRequest(rejected) != rejection.Expected)
				return false;
		}

		EndpointTranslation translated;
		const vec3 stockStart(8.0f, -4.0f, 2.0f);
		const vec3 stockEnd(72.0f, 12.0f, -6.0f);
		const vec3 desiredStart(16.0f, 20.0f, 10.0f);
		if (!TryTranslateEndpoints(stockStart, stockEnd, desiredStart, translated) ||
			translated.Start != desiredStart ||
			translated.End - translated.Start != stockEnd - stockStart ||
			translated.Delta != desiredStart - stockStart)
			return false;

		// XYZ, YZ-only, and no-FireOffset constructions all converge at the
		// final sink. Translation is deliberately independent of how the stock
		// start was assembled.
		const vec3 owner(100.0f, 200.0f, 300.0f);
		const vec3 draw(4.0f, 8.0f, 16.0f);
		const vec3 xyz(32.0f, -12.0f, 6.0f);
		const vec3 yz(0.0f, -12.0f, 6.0f);
		const vec3 desired(512.0f, 256.0f, 128.0f);
		for (const vec3& fireOffset : { xyz, yz, vec3(0.0f) })
		{
			const vec3 start = owner + draw + fireOffset;
			const vec3 end = start + vec3(1024.0f, -128.0f, 64.0f);
			if (!TryTranslateEndpoints(start, end, desired, translated) ||
				translated.Start != desired || translated.End - translated.Start != end - start)
				return false;
		}

		// Negative proof for the rejected sibling shortcut: replacing only
		// CalcDrawOffset with desired-owner leaves the caller's rotated
		// FireOffset in place, yielding desired+FireOffset rather than desired.
		const vec3 siblingActual = owner + (desired - owner) + xyz;
		if (siblingActual == desired || siblingActual != desired + xyz)
			return false;
		const vec3 enforcerScaled = xyz * 0.35f;
		const vec3 enforcerSiblingActual = owner + (desired - owner) + enforcerScaled;
		if (IsNear(enforcerSiblingActual, desired) ||
			!IsNear(enforcerSiblingActual, desired + enforcerScaled))
			return false;

		vec3 translatedLocation;
		vec3 delta;
		if (!TryTranslateLocation(stockStart, desiredStart, translatedLocation, delta) ||
			translatedLocation != desiredStart || delta != desiredStart - stockStart)
			return false;

		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float infinity = std::numeric_limits<float>::infinity();
		return !TryTranslateEndpoints(vec3(nan, 0.0f, 0.0f), stockEnd, desiredStart, translated) &&
			!TryTranslateEndpoints(stockStart, vec3(0.0f, infinity, 0.0f), desiredStart, translated) &&
			!TryTranslateEndpoints(stockStart, stockEnd, vec3(0.0f, 0.0f, nan), translated) &&
			!TryTranslateLocation(stockStart, vec3(infinity, 0.0f, 0.0f), translatedLocation, delta);
	}
}
