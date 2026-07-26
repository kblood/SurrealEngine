#include "BotWarnTargetHookContract.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>

namespace BotWarnTargetHookContract
{
	namespace
	{
		bool EqualInsensitive(std::string_view left, std::string_view right)
		{
			return left.size() == right.size() &&
				std::equal(left.begin(), left.end(), right.begin(), [](char a, char b)
				{
					return std::tolower(static_cast<unsigned char>(a)) ==
						std::tolower(static_cast<unsigned char>(b));
				});
		}

		bool Matches(const ParameterShape& actual, const ParameterShape& expected)
		{
			return EqualInsensitive(actual.Name, expected.Name) && actual.Kind == expected.Kind &&
				EqualInsensitive(actual.ObjectClass, expected.ObjectClass);
		}

		std::string Validate(const SignatureShape& signature, std::string_view functionName,
			const std::vector<ParameterShape>& expected)
		{
			if (!EqualInsensitive(signature.FunctionName, functionName))
				return "unexpected function name '" + signature.FunctionName + "'";
			if (signature.Parameters.size() != expected.size())
			{
				std::ostringstream out;
				out << signature.FunctionName << " has " << signature.Parameters.size()
					<< " parameters; expected " << expected.size();
				return out.str();
			}
			for (size_t index = 0; index < expected.size(); index++)
			{
				if (!Matches(signature.Parameters[index], expected[index]))
					return signature.FunctionName + " parameter " + std::to_string(index) +
						" does not match '" + expected[index].Name + "'";
			}
			if (signature.ReturnValue)
				return signature.FunctionName + " must not return a value";
			return {};
		}
	}

	std::string ValidateWarnTargetSignature(const SignatureShape& signature)
	{
		return Validate(signature, "WarnTarget", {
			{ "shooter", ValueKind::Object, "Pawn" },
			{ "projSpeed", ValueKind::Float, {} },
			{ "FireDir", ValueKind::Vector, {} },
		});
	}

	std::string ValidateTryToDuckSignature(const SignatureShape& signature)
	{
		return Validate(signature, "TryToDuck", {
			{ "duckDir", ValueKind::Vector, {} },
			{ "bReversed", ValueKind::Boolean, {} },
		});
	}

	bool IsQualifiedWarningDodgeLaunch(const WarningDodgeLaunchEligibility& eligibility)
	{
		return eligibility.NestedWarnTargetExact && eligibility.ReceiverLifeId != 0
			&& eligibility.ReceiverActorIndex >= 0 && eligibility.PostPhysicsFalling
			&& eligibility.FiniteLocation && eligibility.FiniteVelocity
			&& eligibility.FiniteAcceleration;
	}
}
