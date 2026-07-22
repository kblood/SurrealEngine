#include "Video/VideoFrameScheduler.h"
#include "Video/VideoPlayer.h"
#include "Audio/AudioSource.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	class SyntheticVideoPlayer : public VideoPlayer
	{
	public:
		int GetFrameIndexForTime(float timestamp) override
		{
			return static_cast<int>(std::floor(timestamp * 2.0f));
		}

		bool Decode() override
		{
			decodeCalls++;
			if (decodedFrames >= 3)
				return false;
			decodedFrames++;
			frameAvailable = true;
			return true;
		}

		UnrealMipmap* NextVideoFrame() override
		{
			if (!frameAvailable)
				return nullptr;
			frameAvailable = false;
			return reinterpret_cast<UnrealMipmap*>(static_cast<uintptr_t>(decodedFrames));
		}

		std::unique_ptr<AudioSource> GetAudio() override { return {}; }

		int decodeCalls = 0;
		int decodedFrames = 0;
		bool frameAvailable = false;
	};
}

int main()
{
	SyntheticVideoPlayer player;
	VideoFrameScheduler scheduler(&player);

	Require(scheduler.Advance(0.0f) == VideoFrameStep::FrameReady, "first frame was not decoded");
	Require(scheduler.FrameIndex() == 0 && player.decodeCalls == 1, "first frame accounting is wrong");
	Require(scheduler.Advance(0.2f) == VideoFrameStep::Waiting, "scheduler advanced before the next frame time");
	Require(scheduler.Advance(0.3f) == VideoFrameStep::FrameReady, "second frame was not decoded on time");
	Require(scheduler.FrameIndex() == 1 && player.decodeCalls == 2, "second frame accounting is wrong");
	Require(scheduler.Advance(-1.0f) == VideoFrameStep::Waiting, "negative elapsed time changed playback");
	Require(scheduler.Advance(1.0f) == VideoFrameStep::Finished, "end of stream was not reported");
	Require(scheduler.FrameIndex() == 2 && player.decodeCalls == 4, "catch-up/end accounting is wrong");
	Require(scheduler.Advance(1.0f) == VideoFrameStep::Finished && player.decodeCalls == 4,
		"finished scheduler decoded again");

	std::cout << "Video frame scheduler tests passed\n";
	return 0;
}
