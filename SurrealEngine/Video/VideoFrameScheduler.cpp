#include "Precomp.h"
#include "VideoFrameScheduler.h"
#include "VideoPlayer.h"
#include <cmath>

VideoFrameScheduler::VideoFrameScheduler(VideoPlayer* player) : player(player)
{
}

VideoFrameStep VideoFrameScheduler::Advance(float elapsedSeconds)
{
	if (finished || !player)
		return VideoFrameStep::Finished;

	if (std::isfinite(elapsedSeconds) && elapsedSeconds > 0.0f)
		timestamp += elapsedSeconds;

	bool changed = false;
	const int targetFrame = player->GetFrameIndexForTime(timestamp);
	while (frameIndex < targetFrame)
	{
		UnrealMipmap* nextFrame = player->NextVideoFrame();
		if (nextFrame)
		{
			currentFrame = nextFrame;
			frameIndex++;
			changed = true;
			continue;
		}

		if (!player->Decode())
		{
			finished = true;
			return VideoFrameStep::Finished;
		}
	}

	return changed ? VideoFrameStep::FrameReady : VideoFrameStep::Waiting;
}
