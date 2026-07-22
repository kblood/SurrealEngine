#pragma once

class VideoPlayer;
class UnrealMipmap;

enum class VideoFrameStep
{
	Waiting,
	FrameReady,
	Finished
};

class VideoFrameScheduler
{
public:
	explicit VideoFrameScheduler(VideoPlayer* player);

	VideoFrameStep Advance(float elapsedSeconds);
	UnrealMipmap* CurrentFrame() const { return currentFrame; }
	float Timestamp() const { return timestamp; }
	int FrameIndex() const { return frameIndex; }

private:
	VideoPlayer* player = nullptr;
	UnrealMipmap* currentFrame = nullptr;
	float timestamp = 0.0f;
	int frameIndex = -1;
	bool finished = false;
};
