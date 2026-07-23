#pragma once

#include "Input/XRInputAdapter.h"
#include "Platform/WebXR/WebXRInputAdapter.h"
#include "XR/XRStartupIntroRoute.h"

namespace WebXR
{
	using StartupIntroFireControl = XRStartupIntroFireControl;
	using StartupIntroFireEvent = XRStartupIntroFireEvent;
	using StartupIntroTriggerRoute = XRStartupIntroTriggerRoute;

	class InputRuntime
	{
	public:
		explicit InputRuntime(XRInputBindings bindings = XRInputBindings::ConventionalUE1());

		void Apply(const AdaptedInputSnapshot& snapshot, bool gameplayInputEnabled,
			XRInputTarget& target);
		void Reset(XRInputTarget& target);

	private:
		XRInputAdapter adapter;
	};
}

extern "C"
{
	int Surreal_ApplyWebXRInputSnapshot();
}
