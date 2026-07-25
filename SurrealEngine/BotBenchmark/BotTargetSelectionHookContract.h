#pragma once

#include <optional>
#include <string>
#include <vector>

namespace BotTargetSelectionHookContract
{
	enum class ValueKind
	{
		Object,
		Boolean,
		Unknown,
	};

	struct ParameterShape
	{
		std::string Name;
		ValueKind Kind = ValueKind::Unknown;
		std::string ObjectClass;

		bool operator==(const ParameterShape&) const = default;
	};

	struct SignatureShape
	{
		std::string FunctionName;
		std::vector<ParameterShape> Parameters;
		std::optional<ParameterShape> ReturnValue;
	};

	// Returns an empty string only for the supported SetEnemy signature.
	// Callers must fail closed on any other result.
	std::string ValidateSetEnemySignature(const SignatureShape& signature);
}
