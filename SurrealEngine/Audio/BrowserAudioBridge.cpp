#include "Precomp.h"
#include "BrowserAudioBridge.h"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

namespace
{
	// Emscripten's OpenAL implementation owns the AudioContext. The public
	// OpenAL API deliberately does not expose that browser object, so this tiny
	// bridge is limited to lifecycle/status operations. All actual mixing,
	// decoding, looping and spatialization remains in AudioDevice.cpp/OpenAL.
	EM_JS(int, GetWebAudioState, (),
	{
		if (typeof AL === 'undefined' || !AL.currentCtx || !AL.currentCtx.audioCtx)
			return 0;

		switch (AL.currentCtx.audioCtx.state)
		{
		case 'suspended': return 1;
		case 'running': return 2;
		case 'closed': return 3;
		default: return 0;
		}
	});

	EM_JS(int, ResumeWebAudio, (),
	{
		if (typeof AL === 'undefined' || !AL.currentCtx || !AL.currentCtx.audioCtx)
			return 0;

		var context = AL.currentCtx.audioCtx;
		if (context.state === 'running')
			return 1;
		if (context.state === 'closed')
			return 0;

		// This function must be entered directly from a trusted click/touch/key or
		// XR-entry callback. Do not defer this resume() call through a timer.
		context.resume().then(function()
		{
			Module['surrealWebAudioResumeCount'] =
				(Module['surrealWebAudioResumeCount'] || 0) + 1;
			Module['surrealWebAudioLastError'] = '';
		}).catch(function(error)
		{
			Module['surrealWebAudioLastError'] = String(error);
		});
		return 1;
	});

	EM_JS(int, GetWebAudioResumeCount, (),
	{
		return Module['surrealWebAudioResumeCount'] || 0;
	});
}

extern "C"
{
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebAudioState()
	{
		return GetWebAudioState();
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_ResumeWebAudio()
	{
		return ResumeWebAudio();
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebAudioResumeCount()
	{
		return GetWebAudioResumeCount();
	}
}

#endif
