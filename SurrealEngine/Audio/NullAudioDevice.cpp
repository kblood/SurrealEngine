
#include "Precomp.h"
#include "NullAudioDevice.h"

void NullAudioDevice::AddSound(USound* sound)
{
}

void NullAudioDevice::RemoveSound(USound* sound)
{
}

bool NullAudioDevice::IsPlaying(int channel)
{
	return false;
}

int NullAudioDevice::GetTotalChannels()
{
	return 0;
}

void NullAudioDevice::PlaySound(int channel, USound* sound, vec3& location, float volume, float radius, float pitch)
{
}

void NullAudioDevice::PlayMusic(std::unique_ptr<AudioSource> source)
{
}

void NullAudioDevice::UpdateSound(int channel, USound* sound, vec3& location, float volume, float radius, float pitch)
{
}

void NullAudioDevice::StopSound(int channel)
{
}

void NullAudioDevice::SetMusicVolume(float volume)
{
}

void NullAudioDevice::SetSoundVolume(float volume)
{
}

void NullAudioDevice::Update()
{
}

#ifdef __EMSCRIPTEN__
std::unique_ptr<AudioDevice> AudioDevice::Create(int frequency, int numVoices, int musicBufferCount, int musicBufferSize)
{
	return std::make_unique<NullAudioDevice>();
}
#endif
