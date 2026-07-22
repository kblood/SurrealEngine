#include <emscripten.h>
#include <emscripten/em_asm.h>
#include <emscripten/threading.h>
#include <emscripten/threading_legacy.h>

#include <cstdint>
#include <cstdio>

namespace
{
	pthread_t ApplicationThread = {};

	int ThreadKind()
	{
		if (emscripten_is_main_browser_thread())
			return 1;
		if (ApplicationThread && pthread_equal(pthread_self(), ApplicationThread))
			return 2;
		return 3;
	}

	void CompleteOnEngineThread(int sequence)
	{
		const int threadKind = ThreadKind();
		const int sawBrowserGlobal = EM_ASM_INT({
			return globalThis.surrealProxyProbeMarker === 0x51a7c0de ? 1 : 0;
		});
		MAIN_THREAD_ASYNC_EM_ASM({
			if (typeof globalThis.surrealProxyProbeComplete === 'function')
				globalThis.surrealProxyProbeComplete($0, $1, $2, performance.now());
		}, sequence, threadKind, sawBrowserGlobal);
	}

	void Idle()
	{
	}
}

extern "C"
{
	EMSCRIPTEN_KEEPALIVE int Probe_CallingThreadKind()
	{
		return ThreadKind();
	}

	EMSCRIPTEN_KEEPALIVE int Probe_SubmitToEngineThread(int sequence)
	{
		if (!ApplicationThread)
			return 0;
		emscripten_dispatch_to_thread_async(ApplicationThread,
			EM_FUNC_SIG_VI, CompleteOnEngineThread, nullptr, sequence);
		return 1;
	}
}

int main()
{
	ApplicationThread = pthread_self();
	printf("READY proxy-to-pthread engine-thread-kind=%d\n", ThreadKind());
	emscripten_set_main_loop(Idle, 0, 0);
	return 0;
}
