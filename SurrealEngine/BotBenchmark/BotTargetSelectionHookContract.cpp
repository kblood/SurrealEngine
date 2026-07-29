#include "BotTargetSelectionHookContract.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>

namespace BotTargetSelectionHookContract
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
	}

	std::string ValidateSetEnemySignature(const SignatureShape& signature)
	{
		const ParameterShape expectedParameter{ "NewEnemy", ValueKind::Object, "Pawn" };
		const ParameterShape expectedReturn{ "ReturnValue", ValueKind::Boolean };
		if (!EqualInsensitive(signature.FunctionName, "SetEnemy"))
			return "unexpected function name '" + signature.FunctionName + "'";
		if (signature.Parameters.size() != 1)
		{
			std::ostringstream out;
			out << signature.FunctionName << " has " << signature.Parameters.size()
				<< " parameters; expected 1";
			return out.str();
		}
		if (!Matches(signature.Parameters.front(), expectedParameter))
			return signature.FunctionName + " parameter 0 does not match 'NewEnemy'";
		if (!signature.ReturnValue || !Matches(*signature.ReturnValue, expectedReturn))
			return signature.FunctionName + " return value does not match";
		return {};
	}
}
