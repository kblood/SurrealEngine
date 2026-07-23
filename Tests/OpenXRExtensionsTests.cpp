#include "Platform/OpenXR/OpenXRExtensions.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static void Check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << message << '\n';
		std::exit(1);
	}
}

int main()
{
	const char runtimeDeviceExtensions[] =
		"VK_KHR_dedicated_allocation VK_KHR_external_semaphore_win32\0";
	auto parsed = ParseOpenXRExtensionList(
		runtimeDeviceExtensions, sizeof(runtimeDeviceExtensions));
	Check(parsed.size() == 2, "NUL-terminated runtime list did not produce two extensions");
	Check(parsed[0] == "VK_KHR_dedicated_allocation", "first runtime extension was changed");
	Check(parsed[1] == "VK_KHR_external_semaphore_win32", "trailing NUL leaked into the final extension");
	Check(parsed[1].size() == std::string("VK_KHR_external_semaphore_win32").size(),
		"final runtime extension contains hidden bytes");

	const char padded[] = "  VK_A\tVK_B\r\n";
	parsed = ParseOpenXRExtensionList(padded, sizeof(padded) - 1);
	Check(parsed == std::vector<std::string>({ "VK_A", "VK_B" }),
		"trailing whitespace at end of buffer produced an empty extension");

	const char embeddedNul[] = { 'V', 'K', '_', 'A', '\0', 'V', 'K', '_', 'B' };
	parsed = ParseOpenXRExtensionList(embeddedNul, sizeof(embeddedNul));
	Check(parsed == std::vector<std::string>({ "VK_A" }),
		"parser did not stop at the first NUL terminator");

	parsed = ParseOpenXRExtensionList("VK_ONLY", 7);
	Check(parsed == std::vector<std::string>({ "VK_ONLY" }),
		"non-NUL-terminated extension list was not accepted");
	Check(ParseOpenXRExtensionList(nullptr, 0).empty(), "null empty input was not accepted");
	return 0;
}
