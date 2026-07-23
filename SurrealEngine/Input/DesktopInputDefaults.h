#pragma once

#include <map>
#include <string>

namespace DesktopInputDefaults
{
	inline constexpr bool InvertMouse = false;

	// Replaces only a recognized legacy arrow-key layout (or a completely empty
	// layout). Custom user bindings are left unchanged.
	bool ApplyModernMovement(std::map<std::string, std::string>& bindings);

	// Applies the one-time movement and mouse migration as one profile update.
	// The saved inversion preference is left unchanged for custom layouts.
	bool ApplyModernControls(std::map<std::string, std::string>& bindings,
		std::string& invertMouse);
}
