#pragma once

#include <optional>
#include <string>
#include <vector>

namespace BotWarnTargetHookContract
{
	enum class ValueKind
	{
		Object,
		Float,
		Vector,
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

	// Returns an empty string only for the retail WarnTarget / TryToDuck shapes.
	// The benchmark observer must remain disabled for every other script contract.
	std::string ValidateWarnTargetSignature(const SignatureShape& signature);
	std::string ValidateTryToDuckSignature(const SignatureShape& signature);
}
