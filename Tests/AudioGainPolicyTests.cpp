#include "Audio/AudioGainPolicy.h"

#include <cmath>
#include <iostream>

namespace
{
	bool Close(float left, float right)
	{
		return std::abs(left - right) < 0.0001f;
	}

	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}
}

int main()
{
	AudioGainPolicy gain;
	if (!Close(gain.EffectiveGain(), 0.0f))
		return Fail("default source gain was not silent");

	if (!gain.SetSourceVolume(1.4f) || !Close(gain.EffectiveGain(), 1.4f))
		return Fail("source volume above one was incorrectly clamped or rejected");
	if (gain.SetSourceVolume(1.4f))
		return Fail("unchanged source volume requested a redundant OpenAL update");

	if (!gain.SetGlobalVolume(0.25f) || !Close(gain.EffectiveGain(), 0.35f))
		return Fail("global volume did not update the effective source gain");
	if (gain.SetGlobalVolume(0.25f))
		return Fail("unchanged global volume requested a redundant OpenAL update");

	if (!gain.SetSourceVolume(0.8f) || !Close(gain.EffectiveGain(), 0.2f))
		return Fail("source-volume changes did not retain the global multiplier");

	return 0;
}
