// SurrealVideo (FFmpeg-backed) isn't built under Emscripten - see
// The minimal browser platform has no video decoder. Movies just
// decode nothing and VideoPlayer's caller treats an Error result as "no
// video available", matching how a map with no movies configured behaves.
#include "../../../SurrealVideo/SurrealVideo.h"

namespace
{
	class NullVideoDecoder : public IVideoDecoder
	{
	public:
		VideoDecoderResult Decode(const void* data, size_t size) override { return VideoDecoderResult::Error; }
		int GetWidth() override { return 0; }
		int GetHeight() override { return 0; }
		const uint32_t* GetPixels() override { return nullptr; }
		void Release() override { delete this; }
	};

	class NullAudioDecoder : public IAudioDecoder
	{
	public:
		AudioDecoderResult Decode(const void* data, size_t size) override { return AudioDecoderResult::Error; }
		const int16_t* GetSamples() override { return nullptr; }
		int GetSampleCount() override { return 0; }
		void Release() override { delete this; }
	};
}

IVideoDecoder* CreateVideoDecoder()
{
	return new NullVideoDecoder();
}

IAudioDecoder* CreateAudioDecoder(int channels, int block_align)
{
	return new NullAudioDecoder();
}
