#include "UObject/DXSavePath.h"

#include <iostream>
#include <string>

static int failures = 0;

static void Check(const std::string& actual, const std::string& expected)
{
	if (actual != expected)
	{
		std::cerr << "FAILED: expected " << expected << ", got " << actual << '\n';
		failures++;
	}
}

int main()
{
	Check(FormatDXSaveFolder(0), "Save0000");
	Check(FormatDXSaveFolder(1), "Save0001");
	Check(FormatDXSaveFolder(42), "Save0042");
	Check(FormatDXSaveFolder(9999), "Save9999");
	Check(FormatDXSaveFolder(-1), "QuickSave");

	if (failures == 0)
		std::cout << "All Deus Ex save-path tests passed.\n";
	return failures == 0 ? 0 : 1;
}
