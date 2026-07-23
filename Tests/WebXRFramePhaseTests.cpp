#include "Platform/WebXR/WebXRFramePhase.h"

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
	WebXR::FramePhaseState phase;
	Check(phase.Phase() == WebXR::FramePhase::Idle,
		"new WebXR frame phase was not idle");
	Check(!phase.MarkRendered(), "WebXR rendered without prepared simulation");
	Check(phase.BeginPrepare(), "WebXR simulation preparation was rejected");
	Check(!phase.BeginPrepare(), "WebXR allowed overlapping simulation preparation");
	Check(phase.MarkRendered(), "WebXR current-pose render was rejected");
	Check(!phase.MarkRendered(), "WebXR rendered one prepared state twice");
	Check(phase.Complete(), "WebXR rendered frame did not complete");
	Check(phase.Phase() == WebXR::FramePhase::Idle,
		"completed WebXR frame did not return to idle");
	Check(phase.BeginPrepare() && phase.Complete(),
		"WebXR could not discard an unrendered prepared frame during teardown");
	phase.Reset();
	Check(phase.Phase() == WebXR::FramePhase::Idle && !phase.Complete(),
		"WebXR reset retained frame ownership");
	std::cout << "WebXR frame phase tests passed\n";
	return 0;
}
