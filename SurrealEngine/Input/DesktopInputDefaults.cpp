#include "Input/DesktopInputDefaults.h"

#include <string_view>

namespace DesktopInputDefaults
{
	namespace
	{
		const std::string& Binding(const std::map<std::string, std::string>& bindings,
			const char* key)
		{
			static const std::string empty;
			auto it = bindings.find(key);
			return it != bindings.end() ? it->second : empty;
		}

		bool IsLegacyUpBinding(std::string_view value)
		{
			return value == "Axis aUp Speed=+300.0" ||
				value == "Axis aUp Speed=300.0";
		}

		bool IsLegacyWBinding(std::string_view value)
		{
			// Older SurrealEngine profiles could save Fire on W while retaining
			// the otherwise untouched arrow-key movement layout.
			return value.empty() || value == "Fire";
		}
	}

	bool ApplyModernMovement(std::map<std::string, std::string>& bindings)
	{
		const bool modernKeysUntouched = IsLegacyWBinding(Binding(bindings, "W")) &&
			Binding(bindings, "A").empty() && Binding(bindings, "D").empty() &&
			(Binding(bindings, "S").empty() || IsLegacyUpBinding(Binding(bindings, "S")));
		const bool classicArrows = Binding(bindings, "Up") == "MoveForward" &&
			Binding(bindings, "Down") == "MoveBackward" &&
			Binding(bindings, "Left") == "StrafeLeft" &&
			Binding(bindings, "Right") == "StrafeRight";
		const bool emptyMovement = Binding(bindings, "Up").empty() &&
			Binding(bindings, "Down").empty() && Binding(bindings, "Left").empty() &&
			Binding(bindings, "Right").empty() && Binding(bindings, "S").empty();
		if (!modernKeysUntouched || (!classicArrows && !emptyMovement))
			return false;

		bindings["W"] = "MoveForward";
		bindings["A"] = "StrafeLeft";
		bindings["S"] = "MoveBackward";
		bindings["D"] = "StrafeRight";
		return true;
	}
}
