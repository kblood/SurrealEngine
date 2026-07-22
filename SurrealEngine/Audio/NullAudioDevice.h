#pragma once

#include "AudioDevice.h"

// No-op AudioDevice used where there is no real audio backend wired up yet
// (Emscripten M1: getting a real AudioContext initialized headlessly/without
// a user gesture is an orthogonal browser-policy concern, deliberately out of
// scope until browser audio is added).
class NullAudioDevice : public AudioDevice
{
public:
	void AddSound(USound* sound) override;
	void RemoveSound(USound* sound) override;
	bool IsPlaying(int channel) override;
	int GetTotalChannels() override;
	void PlaySound(int channel, USound* sound, vec3& location, float volume, float radius, float pitch) override;
	void PlayMusic(std::unique_ptr<AudioSource> source) override;
	void UpdateSound(int channel, USound* sound, vec3& location, float volume, float radius, float pitch) override;
	void StopSound(int channel) override;
	void SetMusicVolume(float volume) override;
	void SetSoundVolume(float volume) override;
	void Update() override;
};
