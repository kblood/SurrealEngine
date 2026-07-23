#include "XR/XRLaunchPolicy.h"

#include <iostream>
#include <stdexcept>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}
}

int main()
{
	Check(!ResolveOpenXRLaunchRequest(false, false, false),
		"OpenXR must remain off by default");
	Check(ResolveOpenXRLaunchRequest(true, false, false),
		"launcher OpenXR option was ignored");
	Check(ResolveOpenXRLaunchRequest(false, true, false),
		"--openxr did not override the launcher default");
	Check(!ResolveOpenXRLaunchRequest(true, true, true),
		"--no-openxr did not take precedence");

	Check(!ShouldUpdateXRAvatar(false, false),
		"avatar work must remain off by default");
	Check(ShouldUpdateXRAvatar(true, false),
		"player-facing avatar option did not activate sampling");
	Check(ShouldUpdateXRAvatar(false, true),
		"avatar diagnostics did not activate sampling");

	std::cout << "XR launch policy tests passed\n";
	return 0;
}
