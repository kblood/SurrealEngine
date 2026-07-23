#include "XRLaunchPolicy.h"

bool ResolveOpenXRLaunchRequest(bool launcherEnabled, bool commandLineEnable,
	bool commandLineDisable)
{
	if (commandLineDisable)
		return false;
	if (commandLineEnable)
		return true;
	return launcherEnabled;
}
