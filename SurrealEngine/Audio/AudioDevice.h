#pragma once

#include "AudioSource.h"

#include <memory>
#include <mutex>
#include <vector>
#include <thread>
#include "Math/vec.h"

class AudioSource;
class USound;

// A listener pose expressed entirely in UE1 world coordinates. Forward and
// Up need not be perfectly normalized, but they must be finite and linearly
// independent. This keeps WebXR/head tracking out of the backend API while
// retaining full roll (a Rotator::FromVector-only path would lose it).
struct AudioListenerPose
{
	vec3 Position = vec3(0.0f);
	vec3 Forward = vec3(1.0f, 0.0f, 0.0f);
	vec3 Up = vec3(0.0f, 0.0f, 1.0f);
	vec3 Velocity = vec3(0.0f);
};

class AudioDevice
{
public:
	static std::unique_ptr<AudioDevice> Create(int frequency, int numVoices, int musicBufferCount, int musicBufferSize);

	virtual ~AudioDevice() = default;
	virtual void AddSound(USound* sound) = 0;
	virtual void RemoveSound(USound* sound) = 0;
	virtual bool IsPlaying(int channel) = 0;
	virtual int GetTotalChannels() = 0;
	virtual void PlaySound(int channel, USound* sound, vec3& location, float volume, float radius, float pitch) = 0;
	virtual void PlayMusic(std::unique_ptr<AudioSource> source) = 0;
	virtual void UpdateSound(int channel, USound* sound, vec3& location, float volume, float radius, float pitch) = 0;
	virtual void StopSound(int channel) = 0;
	virtual void SetMusicVolume(float volume) = 0;
	virtual void SetSoundVolume(float volume) = 0;
	// A valid explicit pose wins for this update only. nullptr (or an invalid
	// pose) asks the backend to retain its ordinary CameraActor fallback.
	virtual void Update(const AudioListenerPose* listenerPose = nullptr) = 0;
};
