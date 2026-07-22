#pragma once

// Browser-side state reported by Surreal_GetWebAudioState. These values are
// intentionally plain integers so JavaScript harnesses can query them through
// Module.ccall without needing a generated binding layer.
enum class BrowserAudioState
{
	Unavailable = 0,
	Suspended = 1,
	Running = 2,
	Closed = 3
};
