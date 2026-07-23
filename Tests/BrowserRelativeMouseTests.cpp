#include "Platform/Browser/BrowserRelativeMouse.h"
#include <surrealwidgets/window/browser_relative_mouse.h>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

static void Require(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << message << '\n';
		std::exit(1);
	}
}

int main()
{
	BrowserRelativeMouseAccumulator accumulator;
	accumulator.Add(9, -4);
	accumulator.Add(-3, 11);
	BrowserRelativeMouseDelta delta = accumulator.Drain();
	Require(delta.X == 6 && delta.Y == 7, "relative deltas must preserve sum and sign");

	delta = accumulator.Drain();
	Require(delta.X == 0 && delta.Y == 0, "a drained delta must not be delivered twice");

	accumulator.Add(20, -12);
	accumulator.Reset();
	delta = accumulator.Drain();
	Require(delta.X == 0 && delta.Y == 0, "reset must discard pending relative motion");

	constexpr int threadCount = 4;
	constexpr int additionsPerThread = 5000;
	std::vector<std::thread> threads;
	for (int thread = 0; thread < threadCount; thread++)
	{
		threads.emplace_back([&accumulator]() {
			for (int index = 0; index < additionsPerThread; index++)
				accumulator.Add(1, -1);
		});
	}
	for (std::thread& thread : threads)
		thread.join();
	delta = accumulator.Drain();
	Require(delta.X == threadCount * additionsPerThread &&
		delta.Y == -threadCount * additionsPerThread,
		"concurrent browser callbacks must accumulate without lost deltas");

	SetBrowserRelativeMouseBridgeActive(false);
	Require(ShouldForwardSDLRawMouseMotion(true, IsBrowserRelativeMouseBridgeActive()),
		"requested capture must retain SDL raw motion until pointer lock is active");
	SetBrowserRelativeMouseBridgeActive(true);
	Require(!ShouldForwardSDLRawMouseMotion(true, IsBrowserRelativeMouseBridgeActive()),
		"actual browser pointer lock must suppress duplicate SDL raw motion");
	SetBrowserRelativeMouseBridgeActive(false);
	Require(ShouldForwardSDLRawMouseMotion(true, IsBrowserRelativeMouseBridgeActive()),
		"pointer-lock loss must restore SDL raw-motion fallback");
	Require(!ShouldForwardSDLRawMouseMotion(false, false),
		"unlocked SDL motion remains absolute");

	std::cout << "PASS: browser relative deltas sum atomically, drain once, reset, and de-duplicate SDL\n";
	return 0;
}
