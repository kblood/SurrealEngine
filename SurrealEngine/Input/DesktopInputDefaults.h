#pragma once

#include <map>
#include <string>

namespace DesktopInputDefaults
{
	inline constexpr bool InvertMouse = false;

	// Replaces only an untouched classic arrow-key layout (or a completely
	// empty layout). Custom user bindings are left unchanged.
	bool ApplyModernMovement(std::map<std::string, std::string>& bindings);
}
