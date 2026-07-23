#pragma once

class AudioGainPolicy
{
public:
	bool SetSourceVolume(float value)
	{
		if (sourceVolume == value)
			return false;
		sourceVolume = value;
		return true;
	}

	bool SetGlobalVolume(float value)
	{
		if (globalVolume == value)
			return false;
		globalVolume = value;
		return true;
	}

	float EffectiveGain() const { return sourceVolume * globalVolume; }

private:
	float sourceVolume = 0.0f;
	float globalVolume = 1.0f;
};
