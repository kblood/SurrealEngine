#include <emscripten.h>

namespace
{
	int CallbackCount = 0;

	EM_JS(void, PublishCallbackCount, (int count), {
		globalThis.webXRMainLoopCallbackCount = count;
	});

	void EngineMainLoopCallback(void*)
	{
		++CallbackCount;
		PublishCallbackCount(CallbackCount);
	}
}

extern "C" EMSCRIPTEN_KEEPALIVE int Probe_PauseAndSuspend()
{
	emscripten_pause_main_loop();
	EM_ASM({ globalThis.webXRMainLoopSuspended = true; });
	emscripten_sleep(300);
	EM_ASM({ globalThis.webXRMainLoopSuspended = false; });
	emscripten_resume_main_loop();
	return CallbackCount;
}

int main()
{
	PublishCallbackCount(CallbackCount);
	emscripten_set_main_loop_arg(EngineMainLoopCallback, nullptr, 0, 0);
	return 0;
}
