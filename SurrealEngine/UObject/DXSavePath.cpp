#include "DXSavePath.h"

#include <iomanip>
#include <sstream>

std::string FormatDXSaveFolder(int32_t slot)
{
	if (slot == -1)
		return "QuickSave";

	std::ostringstream out;
	out << "Save";
	if (slot < 0)
	{
		out << '-';
		out << std::setfill('0') << std::setw(3) << -(int64_t)slot;
	}
	else
	{
		out << std::setfill('0') << std::setw(4) << slot;
	}
	return out.str();
}
