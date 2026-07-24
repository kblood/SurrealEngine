#include "BotBenchmarkDeathAttributionHookContract.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>

namespace BotBenchmarkDeathAttribution
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

		HookParameterShape Parameter(const char* name, HookValueKind kind,
			const char* objectClass = "")
		{
			return { name, kind, objectClass };
		}

		HookSignatureShape Expected(HookPoint point)
		{
			switch (point)
			{
			case HookPoint::TakeDamage:
				return { "TakeDamage", {
					Parameter("Damage", HookValueKind::Integer),
					Parameter("instigatedBy", HookValueKind::Object, "Pawn"),
					Parameter("hitlocation", HookValueKind::Vector),
					Parameter("momentum", HookValueKind::Vector),
					Parameter("damageType", HookValueKind::Name),
				} };
			case HookPoint::AddVelocity:
				return { "AddVelocity", { Parameter("NewVelocity", HookValueKind::Vector) } };
			case HookPoint::Killed:
				return { "Killed", {
					Parameter("Killer", HookValueKind::Object, "Pawn"),
					Parameter("Other", HookValueKind::Object, "Pawn"),
					Parameter("damageType", HookValueKind::Name),
				} };
			case HookPoint::PainTimer:
				return { "PainTimer" };
			case HookPoint::FellOutOfWorld:
				return { "FellOutOfWorld" };
			case HookPoint::TakeFallingDamage:
				return { "TakeFallingDamage" };
			case HookPoint::MoverEncroachingOn:
				return { "EncroachingOn", {
					Parameter("Other", HookValueKind::Object, "Actor"),
				}, Parameter("ReturnValue", HookValueKind::Boolean) };
			case HookPoint::Landed:
				return { "Landed", { Parameter("HitNormal", HookValueKind::Vector) } };
			}
			return {};
		}

		bool Matches(const HookParameterShape& actual, const HookParameterShape& expected)
		{
			return EqualInsensitive(actual.Name, expected.Name) && actual.Kind == expected.Kind &&
				EqualInsensitive(actual.ObjectClass, expected.ObjectClass);
		}
	}

	std::string ValidateHookContract(HookPoint point, const HookSignatureShape& signature)
	{
		const HookSignatureShape expected = Expected(point);
		if (!EqualInsensitive(signature.FunctionName, expected.FunctionName))
			return "unexpected function name '" + signature.FunctionName + "'";
		if (signature.Parameters.size() != expected.Parameters.size())
		{
			std::ostringstream out;
			out << signature.FunctionName << " has " << signature.Parameters.size()
				<< " parameters; expected " << expected.Parameters.size();
			return out.str();
		}
		for (size_t index = 0; index < expected.Parameters.size(); index++)
		{
			if (!Matches(signature.Parameters[index], expected.Parameters[index]))
			{
				std::ostringstream out;
				out << signature.FunctionName << " parameter " << index << " does not match '"
					<< expected.Parameters[index].Name << "'";
				return out.str();
			}
		}
		if (signature.ReturnValue.has_value() != expected.ReturnValue.has_value())
			return signature.FunctionName + " return contract does not match";
		if (expected.ReturnValue && !Matches(*signature.ReturnValue, *expected.ReturnValue))
			return signature.FunctionName + " return value does not match";
		return {};
	}
}
