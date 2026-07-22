#include <AL/al.h>
#include <AL/alc.h>
#include <emscripten.h>
#include <cmath>
#include <cstdint>
#include <vector>

extern "C" {
int surreal_browser_audio_resume_js();
int surreal_browser_audio_suspend_js();
int surreal_browser_audio_shutdown_js();
void surreal_browser_audio_set_output_js(float, int);
int surreal_browser_audio_state_js();
int surreal_browser_audio_current_time_ms_js();
}

static ALCdevice* device;
static ALCcontext* context;
static ALuint sources[2];
static ALuint buffers[3];

static std::vector<int16_t> tone(float hz, int samples)
{
	std::vector<int16_t> pcm(samples);
	for (int i = 0; i < samples; i++) pcm[i] = static_cast<int16_t>(std::sin(i * hz * 6.283185307f / 48000.0f) * 8000.0f);
	return pcm;
}

extern "C" {
EMSCRIPTEN_KEEPALIVE int Probe_Start()
{
	if (!context) return 0;
	alGetError();
	auto sfx = tone(440.0f, 48000);
	auto musicA = tone(220.0f, 48000);
	auto musicB = tone(330.0f, 48000);
	alBufferData(buffers[0], AL_FORMAT_MONO16, sfx.data(), int(sfx.size() * sizeof(int16_t)), 48000);
	alBufferData(buffers[1], AL_FORMAT_MONO16, musicA.data(), int(musicA.size() * sizeof(int16_t)), 48000);
	alBufferData(buffers[2], AL_FORMAT_MONO16, musicB.data(), int(musicB.size() * sizeof(int16_t)), 48000);
	if (alGetError() != AL_NO_ERROR) return -1;
	alSourcei(sources[0], AL_BUFFER, buffers[0]);
	if (alGetError() != AL_NO_ERROR) return -2;
	alSourceQueueBuffers(sources[1], 2, &buffers[1]);
	if (alGetError() != AL_NO_ERROR) return -3;
	alSourcePlay(sources[0]);
	alSourcePlay(sources[1]);
	return alGetError() == AL_NO_ERROR ? 1 : -4;
}
EMSCRIPTEN_KEEPALIVE int Probe_PlayingSources()
{
	int playing = 0;
	for (ALuint source : sources) { ALint state = 0; alGetSourcei(source, AL_SOURCE_STATE, &state); playing += state == AL_PLAYING; }
	return playing;
}
EMSCRIPTEN_KEEPALIVE int Probe_QueuedMusicBuffers()
{
	ALint queued = 0; alGetSourcei(sources[1], AL_BUFFERS_QUEUED, &queued); return queued;
}
EMSCRIPTEN_KEEPALIVE int Surreal_ResumeBrowserAudio() { return surreal_browser_audio_resume_js(); }
EMSCRIPTEN_KEEPALIVE int Surreal_SuspendBrowserAudio() { return surreal_browser_audio_suspend_js(); }
EMSCRIPTEN_KEEPALIVE int Surreal_ShutdownBrowserAudio() { return surreal_browser_audio_shutdown_js(); }
EMSCRIPTEN_KEEPALIVE void Surreal_SetBrowserAudioOutput(float volume, int muted) { surreal_browser_audio_set_output_js(volume, muted); }
EMSCRIPTEN_KEEPALIVE int Surreal_GetBrowserAudioState() { return surreal_browser_audio_state_js(); }
EMSCRIPTEN_KEEPALIVE int Surreal_GetBrowserAudioCurrentTimeMs() { return surreal_browser_audio_current_time_ms_js(); }
}

int main()
{
	device = alcOpenDevice(nullptr);
	context = device ? alcCreateContext(device, nullptr) : nullptr;
	if (!context || !alcMakeContextCurrent(context)) return 1;
	alGenSources(2, sources);
	alGenBuffers(3, buffers);
	return alGetError() == AL_NO_ERROR ? 0 : 2;
}
