#include "Platform/WebXR/WebXRFramePhase.h"

bool WebXR::FramePhaseState::BeginPrepare()
{
	if (phase != FramePhase::Idle)
		return false;
	phase = FramePhase::Prepared;
	return true;
}

bool WebXR::FramePhaseState::MarkRendered()
{
	if (phase != FramePhase::Prepared)
		return false;
	phase = FramePhase::Rendered;
	return true;
}

bool WebXR::FramePhaseState::Complete()
{
	if (phase == FramePhase::Idle)
		return false;
	phase = FramePhase::Idle;
	return true;
}

void WebXR::FramePhaseState::Reset()
{
	phase = FramePhase::Idle;
}
