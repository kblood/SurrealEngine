
#include "Precomp.h"
#include "AudioDevice.h"
#include "AudioSource.h"
#include "Engine.h"
#include "Native/NObject.h"
#include "UObject/UActor.h"
#include "UObject/USound.h"
#include <mutex>
#include "Utils/Exception.h"
#include <map>
#include <cmath>
#include <queue>
#include <thread>
#include <chrono>
#include <limits>
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#define UU_PER_METER 43

namespace
{
	// Sanitized values in the coordinate system already used by OpenAL sound
	// sources. Keep this backend detail out of the public listener API.
	struct OpenALListenerPose
	{
		vec3 Position = vec3(0.0f);
		vec3 Forward = vec3(1.0f, 0.0f, 0.0f);
		vec3 Up = vec3(0.0f, 0.0f, -1.0f);
		vec3 Velocity = vec3(0.0f);
	};

	constexpr float ListenerVectorEpsilon = 0.000001f;

	bool IsFiniteAudioVector(const vec3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	bool IsNear(const vec3& value, const vec3& expected, float epsilon = 0.0001f)
	{
		return length(value - expected) <= epsilon;
	}
}

static AudioListenerPose MakeAudioListenerPose(const vec3& position, const Rotator& rotation, const vec3& velocity)
{
	const Coords basis = Coords::Rotation(rotation);
	AudioListenerPose result;
	result.Position = position;
	result.Forward = basis.XAxis;
	result.Up = basis.ZAxis;
	result.Velocity = velocity;
	return result;
}

static vec3 ConvertUE1AudioVectorToOpenAL(const vec3& value)
{
	return vec3(value.x, value.y, -value.z);
}

static bool TryConvertUE1ListenerPoseToOpenAL(const AudioListenerPose& source, OpenALListenerPose& target)
{
	if (!IsFiniteAudioVector(source.Position) || !IsFiniteAudioVector(source.Forward) ||
		!IsFiniteAudioVector(source.Up) || !IsFiniteAudioVector(source.Velocity))
		return false;

	const float forwardLength = length(source.Forward);
	if (!std::isfinite(forwardLength) || forwardLength <= ListenerVectorEpsilon)
		return false;
	const vec3 forward = source.Forward / forwardLength;
	const vec3 orthogonalUp = source.Up - forward * dot(source.Up, forward);
	const float upLength = length(orthogonalUp);
	if (!std::isfinite(upLength) || upLength <= ListenerVectorEpsilon)
		return false;

	target.Position = ConvertUE1AudioVectorToOpenAL(source.Position);
	target.Forward = ConvertUE1AudioVectorToOpenAL(forward);
	target.Up = ConvertUE1AudioVectorToOpenAL(orthogonalUp / upLength);
	target.Velocity = ConvertUE1AudioVectorToOpenAL(source.Velocity);
	return true;
}

static bool RunAudioListenerPoseMathSelfTest()
{
	OpenALListenerPose converted;
	AudioListenerPose identity;
	identity.Position = vec3(1.0f, -2.0f, 3.0f);
	identity.Velocity = vec3(-4.0f, 5.0f, -6.0f);
	if (!TryConvertUE1ListenerPoseToOpenAL(identity, converted) ||
		!IsNear(converted.Position, vec3(1.0f, -2.0f, -3.0f)) ||
		!IsNear(converted.Forward, vec3(1.0f, 0.0f, 0.0f)) ||
		!IsNear(converted.Up, vec3(0.0f, 0.0f, -1.0f)) ||
		!IsNear(converted.Velocity, vec3(-4.0f, 5.0f, 6.0f)))
		return false;

	// A full yaw/pitch/roll pose must retain an orthonormal forward/up pair;
	// this specifically guards against reducing the listener to a zero-roll
	// Rotator::FromVector representation.
	const AudioListenerPose rotated = MakeAudioListenerPose(
		vec3(11.0f, 22.0f, 33.0f), Rotator(5000, 12000, -7000), vec3(2.0f, 3.0f, 4.0f));
	if (!TryConvertUE1ListenerPoseToOpenAL(rotated, converted) ||
		std::fabs(length(converted.Forward) - 1.0f) > 0.0001f ||
		std::fabs(length(converted.Up) - 1.0f) > 0.0001f ||
		std::fabs(dot(converted.Forward, converted.Up)) > 0.0001f)
		return false;

	// Non-unit and slightly skew input is accepted and orthonormalized.
	AudioListenerPose skewed;
	skewed.Forward = vec3(4.0f, 0.0f, 0.0f);
	skewed.Up = vec3(0.25f, 0.0f, 3.0f);
	if (!TryConvertUE1ListenerPoseToOpenAL(skewed, converted) ||
		!IsNear(converted.Forward, vec3(1.0f, 0.0f, 0.0f)) ||
		!IsNear(converted.Up, vec3(0.0f, 0.0f, -1.0f)))
		return false;

	AudioListenerPose invalid = identity;
	invalid.Forward = vec3(0.0f);
	if (TryConvertUE1ListenerPoseToOpenAL(invalid, converted))
		return false;
	invalid = identity;
	invalid.Up = invalid.Forward;
	if (TryConvertUE1ListenerPoseToOpenAL(invalid, converted))
		return false;
	invalid = identity;
	invalid.Position.x = std::numeric_limits<float>::quiet_NaN();
	return !TryConvertUE1ListenerPoseToOpenAL(invalid, converted);
}

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
		position = ConvertUE1AudioVectorToOpenAL(newPosition);
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
		velocity = ConvertUE1AudioVectorToOpenAL(newVelocity);
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
			alSourcei(id, AL_SOURCE_SPATIALIZE_SOFT, bIs3d);
		}
	}

	void SetVolume(float newVolume)
	{
		if (volume != newVolume)
		{
			volume = newVolume;
			alSourcef(id, AL_GAIN, volume * globalVolume);
			alSourcef(id, AL_MAX_GAIN, volume * globalVolume);
		}
	}

	void SetGlobalVolume(float newGlobalVolume)
	{
		globalVolume = newGlobalVolume;
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
			ALint offset;
			alGetSourcei(id, AL_SAMPLE_OFFSET, &offset);
			if (offset >= sound->loopInfo.LoopEnd)
				alSourcei(id, AL_SAMPLE_OFFSET, (ALint)sound->loopInfo.LoopStart);
		}
	}

	ALuint id = -1;

private:
	UActor* actor = nullptr;
	USound* sound = nullptr;
	vec3 position = vec3(0.0f);
	vec3 velocity = vec3(0.0f);
	float radius = 0.0f;
	float volume = 0.0f;
	float globalVolume = 1.0f; // Comes from Audio Subsystem
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
		if (!RunAudioListenerPoseMathSelfTest())
			Exception::Throw("Audio listener pose conversion self-test failed");

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
		// Identity UE1 listener basis after the shared (x, y, -z) reflection.
		ALfloat listenerOri[] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f };
		alListener3f(AL_POSITION, 0, 0, 0.0f);
		alListener3f(AL_VELOCITY, 0, 0, 0);
		alListenerfv(AL_ORIENTATION, listenerOri);
#ifndef __EMSCRIPTEN__
		// Emscripten's OpenAL/Web Audio shim does not implement this extension
		// enum and leaves a sticky AL_INVALID_ENUM that poisons the next checked
		// operation. Browser spatial values below already use Unreal units.
		alListenerf(AL_METERS_PER_UNIT, 1.f / UU_PER_METER);
#endif

		alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
		alSpeedOfSound(343.3f / (1.0f / UU_PER_METER));

		// Init sound sources
		alcGetIntegerv(alDevice, ALC_MONO_SOURCES, 1, &monoSources);
		alcGetIntegerv(alDevice, ALC_STEREO_SOURCES, 1, &stereoSources);

		// Emscripten reports ALC_MONO_SOURCES as INT_MAX because its Web Audio
		// implementation has no fixed hardware source cap. Allocate the engine's
		// configured voice count rather than attempting an enormous vector.
#ifdef __EMSCRIPTEN__
		sources.resize(numVoices);
#else
		// TODO: how do we prioritize mono vs stereo source count?
		sources.resize(monoSources);
#endif

		// init music source/buffer
		alGenSources(1, &alMusicSource);
		alSourcei(alMusicSource, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE);

		alMusicBuffers.resize(musicBufferCount);
		alGenBuffers(musicBufferCount, &alMusicBuffers[0]);

		// init playback thread
		musicThreadData.thread = std::thread([this]() { MusicThreadMain(); });
	}

	~OpenALAudioDevice()
	{
		std::unique_lock lock(musicThreadData.mutex);
		musicThreadData.exitFlag = true;
		lock.unlock();
		musicThreadData.thread.join();

		alSourceStop(alMusicSource);
		alDeleteSources(1, &alMusicSource);

		alDeleteBuffers((ALsizei)alMusicBuffers.size(), &alMusicBuffers[0]);

		sources.clear();

		for (USound* sound : sounds)
		{
			alDeleteBuffers(1, (ALuint*)&sound->handle);
		}

		alcMakeContextCurrent(nullptr);
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
		std::unique_lock lock(musicThreadData.mutex);
		musicThreadData.music = std::move(source);
		musicThreadData.musicUpdate = true;
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

	void Update(const AudioListenerPose* explicitListenerPose = nullptr) override
	{
		AudioListenerPose fallbackPose;
		const AudioListenerPose* listenerPose = explicitListenerPose;
		OpenALListenerPose converted;
		if (!listenerPose || !TryConvertUE1ListenerPoseToOpenAL(*listenerPose, converted))
		{
			UActor* listener = engine->CameraActor;
			if (!listener)
				return;
			fallbackPose = MakeAudioListenerPose(listener->Location(), listener->Rotation(), listener->Velocity());
			if (!TryConvertUE1ListenerPoseToOpenAL(fallbackPose, converted))
				return;
		}

		const ALfloat listenerOri[6] = {
			converted.Forward.x, converted.Forward.y, converted.Forward.z,
			converted.Up.x, converted.Up.y, converted.Up.z
		};
		alListener3f(AL_POSITION, converted.Position.x, converted.Position.y, converted.Position.z);
		alListener3f(AL_VELOCITY, converted.Velocity.x, converted.Velocity.y, converted.Velocity.z);
		alListenerfv(AL_ORIENTATION, listenerOri);
	}

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

	template<class T> class RingQueue
	{
	public:
		RingQueue()
		{
			Data = nullptr;
			Len = 0;
			Num = 0;
			Current = 0;
		}

		RingQueue(size_t n)
		{
			Data = static_cast<T*>(malloc(sizeof(T) * n));
			Len = n;
			Num = 0;
			Current = 0;
		}

		RingQueue(size_t n, const T& Value)
		{
			Data = static_cast<T*>(malloc(sizeof(T) * n));
			for (int i = 0; i < n; i++)
				Data[i] = Value;

			Len = n;
			Num = 0;
			Current = 0;
		}

		~RingQueue()
		{
			free(Data);
		}

		bool Empty()
		{
			return (Num == 0);
		}

		size_t Size()
		{
			return Num;
		}

		T& Front()
		{
			return Data[Current];
		}

		T& GetNextFree()
		{
			size_t Index = Current + Num;
			if (Index >= Len)
				Index -= Len;

			return Data[Index];
		}

		bool Push(const T& Val)
		{
			if (Num == Len)
				return false;

			GetNextFree() = Val;
			Num++;

			return true;
		}

		bool Push(T& Val)
		{
			if (Num == Len)
				return false;

			GetNextFree() = Val;
			Num++;

			return true;
		}

		T& Pop()
		{
			T& Out = Data[Current];
			if (Num > 0)
			{
				Current++;
				if (Current >= Len)
					Current = 0;

				Num--;
			}
			return Out;
		}

		bool Resize(size_t NewSize)
		{
			T* NewData = static_cast<T*>(realloc(Data, sizeof(T) * NewSize));
			if (NewData == NULL)
				return false;

			Data = NewData;
			Len = NewSize;
			return true;
		}

		void Clear()
		{
			Current = 0;
			Num = 0;
		}

	private:
		T* Data = nullptr;
		size_t Len = 0;
		size_t Current = 0;
		size_t Num = 0;
	};

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

	RingQueue<float*> musicQueue;
	int musicBufferCount = 0;
	int musicBufferSize = 0;
	Array<float> musicBuffer;
	float currentMusicVolume = 0.0f;
	float targetMusicVolume = 0.0f;
	float fadeRate = 0.0f;
	float globalSoundVolume = 1.0f;
};

std::unique_ptr<AudioDevice> AudioDevice::Create(int frequency, int numVoices, int musicBufferCount, int musicBufferSize)
{
	return std::make_unique<OpenALAudioDevice>(frequency, numVoices, musicBufferCount, musicBufferSize);
}
