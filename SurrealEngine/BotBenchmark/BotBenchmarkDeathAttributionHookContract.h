#pragma once

#include <optional>
#include <string>
#include <vector>

namespace BotBenchmarkDeathAttribution
{
	enum class HookPoint
	{
		TakeDamage,
		AddVelocity,
		Killed,
		PainTimer,
		FellOutOfWorld,
		TakeFallingDamage,
		MoverEncroachingOn,
		Landed,
	};

	enum class HookValueKind
	{
		Integer,
		Object,
		Vector,
		Name,
		Boolean,
		Unknown,
	};

	struct HookParameterShape
	{
		std::string Name;
		HookValueKind Kind = HookValueKind::Unknown;
		std::string ObjectClass;

		bool operator==(const HookParameterShape&) const = default;
	};

	struct HookSignatureShape
	{
		std::string FunctionName;
		std::vector<HookParameterShape> Parameters;
		std::optional<HookParameterShape> ReturnValue;
	};

	// Returns an empty string only for an exact, supported UT436/Unreal226b
	// script signature. The caller must fail closed on any other result.
	std::string ValidateHookContract(HookPoint point, const HookSignatureShape& signature);
}
