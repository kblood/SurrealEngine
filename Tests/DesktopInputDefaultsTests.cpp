#include "Input/DesktopInputDefaults.h"

#include <cstdlib>
#include <iostream>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << message << '\n';
			std::exit(1);
		}
	}
}

int main()
{
	std::map<std::string, std::string> classic = {
		{ "Up", "MoveForward" }, { "Down", "MoveBackward" },
		{ "Left", "StrafeLeft" }, { "Right", "StrafeRight" },
		{ "W", "" }, { "A", "" }, { "S", "Axis aUp Speed=+300.0" },
		{ "D", "" }
	};
	Check(DesktopInputDefaults::ApplyModernMovement(classic),
		"classic defaults were not recognized");
	Check(classic["W"] == "MoveForward" && classic["S"] == "MoveBackward" &&
		classic["A"] == "StrafeLeft" && classic["D"] == "StrafeRight",
		"classic defaults were not replaced with WASD");
	Check(classic["Up"] == "MoveForward" && classic["Left"] == "StrafeLeft",
		"secondary arrow bindings were removed");

	std::map<std::string, std::string> olderSurrealProfile = {
		{ "Up", "MoveForward" }, { "Down", "MoveBackward" },
		{ "Left", "StrafeLeft" }, { "Right", "StrafeRight" },
		{ "W", "Fire" }, { "A", "" }, { "S", "Axis aUp Speed=+300.0" },
		{ "D", "" }
	};
	Check(DesktopInputDefaults::ApplyModernMovement(olderSurrealProfile) &&
		olderSurrealProfile["W"] == "MoveForward" &&
		olderSurrealProfile["S"] == "MoveBackward",
		"older SurrealEngine profile was not migrated to WASD");

	std::map<std::string, std::string> empty;
	Check(DesktopInputDefaults::ApplyModernMovement(empty) &&
		empty["W"] == "MoveForward" && empty["S"] == "MoveBackward",
		"empty configuration did not receive modern movement defaults");

	std::map<std::string, std::string> custom = {
		{ "Up", "MoveForward" }, { "Down", "MoveBackward" },
		{ "Left", "StrafeLeft" }, { "Right", "StrafeRight" },
		{ "W", "Jump" }, { "S", "Axis aUp Speed=+300.0" }
	};
	Check(!DesktopInputDefaults::ApplyModernMovement(custom) &&
		custom["W"] == "Jump" && custom["S"] == "Axis aUp Speed=+300.0",
		"custom user bindings were overwritten");
	Check(!DesktopInputDefaults::InvertMouse,
		"fresh desktop configurations must not invert mouse look");

	std::cout << "Desktop input default tests passed\n";
	return 0;
}
