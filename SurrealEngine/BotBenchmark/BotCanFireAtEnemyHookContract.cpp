#include "BotCanFireAtEnemyHookContract.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string_view>

namespace
{
	bool EqualInsensitive(std::string_view left, std::string_view right)
	{
		return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](char a, char b)
		{
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		});
	}
}

namespace BotCanFireAtEnemyHookContract
{
	std::string ValidateCanFireAtEnemySignature(const SignatureShape& signature)
	{
		if (!EqualInsensitive(signature.FunctionName, "CanFireAtEnemy"))
			return "unexpected function name '" + signature.FunctionName + "'";
		if (!signature.Parameters.empty())
			return "CanFireAtEnemy must not have parameters";
		if (!signature.ReturnsBoolean)
			return "CanFireAtEnemy must return bool";
		return {};
	}

	std::string ValidateTraceSignature(const TraceSignatureShape& signature)
	{
		if (!EqualInsensitive(signature.FunctionName, "Trace"))
			return "unexpected nested function name '" + signature.FunctionName + "'";
		static constexpr std::string_view expected[] = {
			"vector", "vector", "vector", "vector", "bool", "vector" };
		if (signature.Parameters.size() != std::size(expected))
			return "Trace must have exactly six parameters";
		for (size_t index = 0; index < std::size(expected); index++)
		{
			if (!EqualInsensitive(signature.Parameters[index], expected[index]))
				return "Trace parameter " + std::to_string(index) + " did not match the UT99 vector/vector/vector/vector/bool/vector contract";
		}
		if (!signature.ReturnsObject)
			return "Trace must return an actor object";
		return {};
	}
}
