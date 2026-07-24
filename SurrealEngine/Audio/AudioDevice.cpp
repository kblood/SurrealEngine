
#include "Precomp.h"
#include "AudioDevice.h"
#include "AudioGainPolicy.h"
#include "AudioSource.h"
#include "AudioStreamBufferQueue.h"
#include "Engine.h"
#include "Native/NObject.h"
#include "UObject/UActor.h"
#include "UObject/USound.h"
#include <mutex>
#include "Utils/Exception.h"
#include <map>
#include <cmath>
#include <limits>
#include <queue>
#include <thread>
#include <chrono>
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#define UU_PER_METER 43

class ALSoundSource
{
public:
	ALSoundSource()
	{
		alGenSources(1, &id);
		if (alGetError() != AL_NO_ERROR)
			Exception::Throw("Failed to generate AL source");

		alSourcef(id, AL_ROLLOFF_FACTOR, 1.1f);
	}

	~ALSoundSource()
	{
		Stop();
		alSourcei(id, AL_BUFFER, 0);
		alDeleteSources(1, &id);
	}

	void Play()
	{
		alSourcePlay(id);
		if (alGetError() != AL_NO_ERROR)
			Exception::Throw("Failed to play AL source");
	}

	void Stop()
	{
		alSourceStop(id);
	}

	bool IsPlaying()
	{
		if (!alIsSource(id))
			return false;

		ALint state;
		alGetSourcei(id, AL_SOURCE_STATE, &state);
		return state == AL_PLAYING;
	}

	USound* GetSound()
	{
		return sound;
	}

	void SetDopplerFactor(float newDopplerFactor)
	{
		if (dopplerFactor != newDopplerFactor)
		{
			dopplerFactor = newDopplerFactor;
			alSourcef(id, AL_DOPPLER_FACTOR, dopplerFactor);
		}
	}

	void SetPosition(vec3& newPosition)
	{
		position.x = newPosition.x;
		position.y = newPosition.y;
		position.z = -newPosition.z;
		alSourcefv(id, AL_POSITION, &position[0]);
	}

	void SetRadius(float newRadius)
	{
		if (radius != newRadius)
		{
			radius = newRadius;
			alSourcef(id, AL_MAX_DISTANCE, radius);
			alSourcef(id, AL_REFERENCE_DISTANCE, 0.1f * radius);
		}
	}

	void SetVelocity(vec3& newVelocity)
	{
		velocity.x = newVelocity.x;
		velocity.y = newVelocity.y;
		velocity.z = -newVelocity.z;
		alSourcefv(id, AL_VELOCITY, &velocity[0]);
	}

	void SetSound(USound* newSound)
	{
		if (sound != newSound)
		{
			sound = newSound;
			alSourcei(id, AL_BUFFER, 0);
			alGetError();
			alSourcei(id, AL_BUFFER, (ALint)(ptrdiff_t)sound->handle);

			if (sound->loopInfo.Looped)
				alSourcei(id, AL_LOOPING, AL_TRUE);
			else
				alSourcei(id, AL_LOOPING, AL_FALSE);
		}
	}

	void SetSpatial(bool bSpatial)
	{
		if (bIs3d != bSpatial)
		{
			bIs3d = bSpatial;
#ifdef AL_SOURCE_SPATIALIZE_SOFT
			alSourcei(id, AL_SOURCE_SPATIALIZE_SOFT, bIs3d);
#endif
		}
	}

	void SetVolume(float newVolume)
	{
		if (gain.SetSourceVolume(newVolume))
		{
			ApplyGain();
		}
	}

	void SetGlobalVolume(float newGlobalVolume)
	{
		if (gain.SetGlobalVolume(newGlobalVolume))
		{
			ApplyGain();
		}
	}

	void SetPitch(float newPitch)
	{
		if (pitch != newPitch)
		{
			pitch = newPitch;
			alSourcef(id, AL_PITCH, pitch);
		}
	}

	void DoLoop()
	{
		if (sound->loopInfo.Looped)
		{
#ifdef __EMSCRIPTEN__
			// Emscripten's Web Audio OpenAL shim can report a fractional sample
			// position. Its alGetSourcei wrapper asserts before converting that
			// value in diagnostic builds, so query the same offset as a float in
			// browsers and keep the native integer path everywhere else.
			ALfloat offset;
			alGetSourcef(id, AL_SAMPLE_OFFSET, &offset);
#else
			ALint offset;
			alGetSourcei(id, AL_SAMPLE_OFFSET, &offset);
#endif
			if (offset >= sound->loopInfo.LoopEnd)
				alSourcei(id, AL_SAMPLE_OFFSET, (ALint)sound->loopInfo.LoopStart);
		}
	}

	ALuint id = -1;

private:
	void ApplyGain()
	{
		alSourcef(id, AL_GAIN, gain.EffectiveGain());
	}

	UActor* actor = nullptr;
	USound* sound = nullptr;
	vec3 position = vec3(0.0f);
	vec3 velocity = vec3(0.0f);
	float radius = 0.0f;
	AudioGainPolicy gain;
	float pitch = 0.0f;
	float dopplerFactor = 0.0f;
	bool bIs3d = false;
};

// TODO list:
//  Sound looping
//  Positional audio
//  Music fade in/out
//  Music crossfade
//  EFX effects
//  Support for real EAX hardware
//  (maybe) A3D style sound tracing

class OpenALAudioDevice : public AudioDevice
{
public:
	OpenALAudioDevice(int inFrequency, int numVoices, int inMusicBufferCount, int inMusicBufferSize)
	{
		frequency = inFrequency;
		musicBufferCount = inMusicBufferCount;
		musicBufferSize = inMusicBufferSize;

		musicBuffer.resize(musicBufferSize * musicBufferCount);
		musicQueue.Resize(musicBufferCount);

		for (int i = 0; i < musicBufferCount; i++)
		{
			musicQueue.Push(&musicBuffer[musicBufferSize * i]);
			musicQueue.Pop();
		}

		// Init OpenAL
		// TODO: Add device enumeration
		alDevice = alcOpenDevice(NULL);
		if (alDevice == nullptr)
			Exception::Throw("Failed to initialize OpenAL device");

		const ALCint ctxAttribs[] =
		{
			ALC_FREQUENCY, inFrequency,
			ALC_REFRESH, 60,
			ALC_SYNC, ALC_FALSE
		};

		alContext = alcCreateContext(alDevice, NULL);
		if (alContext == nullptr)
			Exception::Throw("Failed to initialize OpenAL context");

		if (alcMakeContextCurrent(alContext) == ALC_FALSE)
			Exception::Throw("Failed to make OpenAL context current");

		// init listener state
		ALfloat listenerOri[] = { 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f };
		alListener3f(AL_POSITION, 0, 0, 0.0f);
		alListener3f(AL_VELOCITY, 0, 0, 0);
		alListenerfv(AL_ORIENTATION, listenerOri);
#if defined(AL_METERS_PER_UNIT) && !defined(__EMSCRIPTEN__)
		alListenerf(AL_METERS_PER_UNIT, 1.f / UU_PER_METER);
#endif

		alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
		alSpeedOfSound(343.3f / (1.0f / UU_PER_METER));

		// Init sound sources
		alcGetIntegerv(alDevice, ALC_MONO_SOURCES, 1, &monoSources);
		alcGetIntegerv(alDevice, ALC_STEREO_SOURCES, 1, &stereoSources);
	#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
		LogMessage("[asyncify-stage] OpenAL reported mono=" + std::to_string(monoSources) +
			" stereo=" + std::to_string(stereoSources) + " requested=" + std::to_string(numVoices));
	#endif
	#ifdef __EMSCRIPTEN__
		// Emscripten's WebAudio-backed OpenAL reports INT_MAX for its virtual
		// source limits. That means "not hardware-limited", not that the engine
		// should allocate billions of ALSoundSource objects.
		const ALint requestedSources = std::max<ALint>(1, numVoices);
		if (monoSources <= 0 || monoSources == std::numeric_limits<ALint>::max())
			monoSources = requestedSources;
		else
			monoSources = std::min(monoSources, requestedSources);
		// Do not let a non-source initialization error make the first successfully
		// generated source look like it failed.
		while (alGetError() != AL_NO_ERROR) { }
	#endif

		// TODO: how do we prioritize mono vs stereo source count?
		sources.resize(monoSources);
	#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
		LogMessage("[asyncify-stage] OpenAL allocated voices=" + std::to_string(monoSources));
	#endif

		// init music source/buffer
		alGenSources(1, &alMusicSource);
#ifdef AL_SOURCE_SPATIALIZE_SOFT
		alSourcei(alMusicSource, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE);
#endif

		alMusicBuffers.resize(musicBufferCount);
		alGenBuffers(musicBufferCount, &alMusicBuffers[0]);

		// Emscripten's OpenAL implementation is a WebAudio JavaScript shim.
		// Keep all its calls on the browser thread; native retains its decoder thread.
#ifndef __EMSCRIPTEN__
		musicThreadData.thread = std::thread([this]() { MusicThreadMain(); });
#endif
	}

	~OpenALAudioDevice()
	{
#ifndef __EMSCRIPTEN__
		std::unique_lock lock(musicThreadData.mutex);
		musicThreadData.exitFlag = true;
		lock.unlock();
		musicThreadData.thread.join();
#endif

		alSourceStop(alMusicSource);
		alDeleteSources(1, &alMusicSource);

		alDeleteBuffers((ALsizei)alMusicBuffers.size(), &alMusicBuffers[0]);

		sources.clear();

		for (USound* sound : sounds)
		{
			alDeleteBuffers(1, (ALuint*)&sound->handle);
		}

		alcDestroyContext(alContext);
		alcCloseDevice(alDevice);
	}

	int GetTotalChannels() override
	{
		return (int)sources.size();
	}

	void AddSound(USound* sound) override
	{
		sounds.push_back(sound);

		ALenum format = AL_FORMAT_MONO_FLOAT32;
		if (sound->channels == 2)
			format = AL_FORMAT_STEREO_FLOAT32;

		ALuint id;
		alGenBuffers(1, &id);
		alBufferData(id, format, sound->samples.data(), (ALsizei)(sound->samples.size()*sizeof(sound->samples[0])), sound->frequency);
		alError = alGetError();
		if (alError != AL_NO_ERROR)
			Exception::Throw("Failed to buffer sound data for " + sound->Name.ToString());

		sound->handle = (void*)(ptrdiff_t)id;
	}

	void RemoveSound(USound* sound) override
	{
		auto it = sounds.begin();
		while (it != sounds.end())
		{
			if (*it == sound)
			{
				// TODO: find sources playing this sound and stop them?
				sounds.erase(it);
				alDeleteBuffers(1, reinterpret_cast<const ALuint*>(&sound->handle));
				return;
			}
		}
	}

	bool IsPlaying(int channel) override
	{
		ALSoundSource& source = sources[channel];
		return source.IsPlaying();
	}

	void PlayMusic(std::unique_ptr<AudioSource> source) override
	{
#ifdef __EMSCRIPTEN__
		StopBrowserMusic();
		browserMusic = std::move(source);
#else
		std::unique_lock lock(musicThreadData.mutex);
		musicThreadData.music = std::move(source);
		musicThreadData.musicUpdate = true;
#endif
	}

	void PlaySound(int channel, USound* sound, vec3& location, float volume, float radius, float pitch) override
	{
		if (!std::isfinite(volume) || volume < 0.0f || !std::isfinite(pitch) || channel >= sources.size())
			Exception::Throw("Invalid PlaySound arguments");

		ALSoundSource& source = sources[channel];
		if (source.IsPlaying())
		{
			LogMessage("Attempted to play sound on active channel " + std::to_string(channel));
			return;
		}

		source.SetSound(sound);
		source.SetPosition(location);
		source.SetVolume(volume);
		source.SetGlobalVolume(globalSoundVolume);
		source.SetRadius(radius);
		source.SetPitch(pitch);
		source.SetSpatial(true);
		source.Play();
	}

	void UpdateSound(int channel, USound* sound, vec3& location, float volume, float radius, float pitch) override
	{
		if (!std::isfinite(volume) || volume < 0.0f || !std::isfinite(pitch) || channel >= sources.size())
			Exception::Throw("Invalid PlaySound arguments");

		ALSoundSource& source = sources[channel];

		source.SetPosition(location);
		source.SetVolume(volume);
		source.SetGlobalVolume(globalSoundVolume);
		source.SetRadius(radius);
		source.SetPitch(pitch);

		source.DoLoop();
	}

	void StopSound(int channel) override
	{
		if (channel >= sources.size())
			Exception::Throw("Invalid StopSound arguments");

		sources[channel].Stop();
	}

	std::string getALErrorString()
	{
		switch (alGetError())
		{
		case AL_NO_ERROR:
			return "AL_NO_ERROR";
		case AL_INVALID_NAME:
			return "AL_INVALID_NAME";
		case AL_INVALID_ENUM:
			return "AL_INVALID_ENUM";
		case AL_INVALID_VALUE:
			return "AL_INVALID_VALUE";
		case AL_INVALID_OPERATION:
			return "AL_INVALID_OPERATION";
		case AL_OUT_OF_MEMORY:
			return "AL_OUT_OF_MEMORY";
		default:
			return "AL_UNKNOWN_ERROR";
		}
	}

	void PlayMusicBuffer(AudioSource* music)
	{
		int format = (music->GetChannels() == 1) ? AL_FORMAT_MONO_FLOAT32 : AL_FORMAT_STEREO_FLOAT32;
		int freq = music->GetFrequency();

		ALenum error;
		for (int i = 0; i < musicBufferCount; i++)
		{
			alBufferData(alMusicBuffers[i], format, musicQueue.Pop(), musicBufferSize*4, freq);
			if ((error = alGetError()) != AL_NO_ERROR)
				Exception::Throw("alBufferData failed in PlayMusicBuffer: " + getALErrorString());

			alSourceQueueBuffers(alMusicSource, 1, &alMusicBuffers[i]);
			if (alGetError() != AL_NO_ERROR)
				Exception::Throw("alSourceQueueBuffers failed in PlayMusicBuffer: " + getALErrorString());
		}

		alSourcePlay(alMusicSource);
		if (alGetError() != AL_NO_ERROR)
			Exception::Throw("alSourcePlay failed in PlayMusicBuffer: " + getALErrorString());
	}

	void UpdateMusicBuffer(AudioSource* music)
	{
		ALint status;
		alGetSourcei(alMusicSource, AL_BUFFERS_PROCESSED, &status);

		while (status)
		{
			ALuint buffer;
			alSourceUnqueueBuffers(alMusicSource, 1, &buffer);

			int format = (music->GetChannels() == 1) ? AL_FORMAT_MONO_FLOAT32 : AL_FORMAT_STEREO_FLOAT32;
			int freq = music->GetFrequency();

			alBufferData(buffer, format, musicQueue.Pop(), musicBufferSize*4, freq);
			if (alGetError() != AL_NO_ERROR)
				Exception::Throw("alBufferData failed in UpdateMusicBuffer: " + getALErrorString());

			alSourceQueueBuffers(alMusicSource, 1, &buffer);
			if (alGetError() != AL_NO_ERROR)
				Exception::Throw("alSourceQueueBuffers failed in UpdateMusicBuffer: " + getALErrorString());

			status--;
		}

		alGetSourcei(alMusicSource, AL_SOURCE_STATE, &status);
		if (status == AL_STOPPED)
		{
			alSourcePlay(alMusicSource);
			if (alGetError() != AL_NO_ERROR)
				Exception::Throw("alSourcePlay failed in PlayMusicBuffer: " + getALErrorString());
		}
	}

	void SetMusicVolume(float volume) override
	{
		alSourcef(alMusicSource, AL_GAIN, volume);
	}

	void SetSoundVolume(float volume) override
	{
		globalSoundVolume = volume;

		for (auto& soundSource : sources)
			soundSource.SetGlobalVolume(globalSoundVolume);
	}

	void Update() override
	{
#ifdef __EMSCRIPTEN__
		PumpBrowserMusic();
#endif
		UActor* listener = engine->CameraActor;
		if (listener)
		{
			// Update listener properties
			vec3& location = listener->Location();
			vec3& velocity = listener->Velocity();

			vec3 at, left, up;
			Coords::Rotation(listener->Rotation()).GetAxes(at, left, up);

			ALfloat listenerOri[6] = { up.x, up.y, -up.z, -at.x, -at.y, at.z };
			alListener3f(AL_POSITION, location.x, location.y, -location.z);
			alListener3f(AL_VELOCITY, velocity.x, velocity.y, -velocity.z);
			alListenerfv(AL_ORIENTATION, listenerOri);
		}
	}

#ifdef __EMSCRIPTEN__
	void StopBrowserMusic()
	{
		alSourceStop(alMusicSource);
		ALint queued = 0;
		alGetSourcei(alMusicSource, AL_BUFFERS_QUEUED, &queued);
		while (queued-- > 0)
		{
			ALuint buffer = 0;
			alSourceUnqueueBuffers(alMusicSource, 1, &buffer);
		}
		musicQueue.Clear();
		browserMusicPlaying = false;
		browserMusic.reset();
	}

	void PumpBrowserMusic()
	{
		if (!browserMusic)
			return;
		while (musicQueue.Size() < static_cast<size_t>(musicBufferCount))
		{
			browserMusic->ReadSamples(musicQueue.GetNextFree(), musicBufferSize);
			musicQueue.Push(musicQueue.GetNextFree());
		}
		if (!browserMusicPlaying)
		{
			PlayMusicBuffer(browserMusic.get());
			browserMusicPlaying = true;
		}
		else if (musicQueue.Size() > 0)
		{
			UpdateMusicBuffer(browserMusic.get());
		}
	}
#endif

	void MusicThreadMain()
	{
		std::unique_ptr<AudioSource> currentMusic;
		bool musicPlaying = false;

		while (true)
		{
			// Lock the mutex. Grab the data we need from the main thread. Then unlock.
			std::unique_lock lock(musicThreadData.mutex);
			if (musicThreadData.exitFlag)
				break;
			if (musicThreadData.musicUpdate)
			{
				if (musicPlaying)
				{
					alSourceStop(alMusicSource);
					alSourceUnqueueBuffers(alMusicSource, (ALsizei)alMusicBuffers.size(), &alMusicBuffers[0]);
					musicPlaying = false;
				}
				currentMusic = std::move(musicThreadData.music);
				musicThreadData.musicUpdate = false;
			}
			lock.unlock();
			// Never touch anything from musicThreadData after this point.

			if (currentMusic)
			{
				while (musicQueue.Size() < musicBufferCount)
				{
					// render a chunk of music
					currentMusic->ReadSamples(musicQueue.GetNextFree(), musicBufferSize);
					musicQueue.Push(musicQueue.GetNextFree());
				}

				if (!musicPlaying)
				{
					PlayMusicBuffer(currentMusic.get());
					musicPlaying = true;
				}
				else if (musicQueue.Size() > 0)
				{
					UpdateMusicBuffer(currentMusic.get());
				}

				// TODO: music fade in/out
			}
			else
			{
				musicPlaying = false;
			}
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(5ms);
		}
	}

	ALCdevice* alDevice = nullptr;
	ALCcontext* alContext = nullptr;
	ALenum alError = 0;
	ALuint alMusicSource = 0;
	Array<ALuint> alMusicBuffers;
	Array<ALSoundSource> sources;
	ALint monoSources = 0;
	ALint stereoSources = 0;

	int frequency = 48000;
	Array<USound*> sounds;

	// Note: variables changed in musicThreadData *must* be done within a mutex lock to be thread safe
	struct
	{
		std::mutex mutex;
		std::thread thread;
		bool exitFlag = false;
		std::unique_ptr<AudioSource> music;
		bool musicUpdate = false;
	} musicThreadData;

	AudioStreamBufferQueue<float*> musicQueue;
	int musicBufferCount = 0;
	int musicBufferSize = 0;
	Array<float> musicBuffer;
	float currentMusicVolume = 0.0f;
	float targetMusicVolume = 0.0f;
	float fadeRate = 0.0f;
	float globalSoundVolume = 1.0f;
#ifdef __EMSCRIPTEN__
	std::unique_ptr<AudioSource> browserMusic;
	bool browserMusicPlaying = false;
#endif
};

std::unique_ptr<AudioDevice> AudioDevice::Create(int frequency, int numVoices, int musicBufferCount, int musicBufferSize)
{
	return std::make_unique<OpenALAudioDevice>(frequency, numVoices, musicBufferCount, musicBufferSize);
}
