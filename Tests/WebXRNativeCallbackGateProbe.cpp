#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstdint>

extern "C" void surreal_install_native_call_gate_js();

namespace
{
	SDL_TimerID CanceledTimer = 0;
	int TimerCount = 0;
	EM_JS(void, RecordEntry, (int id), {
		globalThis.webXRNativeCallbackEntries ||= [];
		globalThis.webXRNativeCallbackEntries.push(id);
	});

	bool KeyCallback(int, const EmscriptenKeyboardEvent*, void* data) { RecordEntry(static_cast<int>(reinterpret_cast<intptr_t>(data))); return false; }
	bool MouseCallback(int, const EmscriptenMouseEvent*, void* data) { RecordEntry(static_cast<int>(reinterpret_cast<intptr_t>(data))); return false; }
	bool WheelCallback(int, const EmscriptenWheelEvent*, void* data) { RecordEntry(static_cast<int>(reinterpret_cast<intptr_t>(data))); return false; }
	bool FocusCallback(int, const EmscriptenFocusEvent*, void* data) { RecordEntry(static_cast<int>(reinterpret_cast<intptr_t>(data))); return false; }
	bool VisibilityCallback(int, const EmscriptenVisibilityChangeEvent*, void* data) { RecordEntry(static_cast<int>(reinterpret_cast<intptr_t>(data))); return false; }
	bool TouchCallback(int, const EmscriptenTouchEvent*, void* data) { RecordEntry(static_cast<int>(reinterpret_cast<intptr_t>(data))); return false; }
	bool GamepadCallback(int, const EmscriptenGamepadEvent*, void* data) { RecordEntry(static_cast<int>(reinterpret_cast<intptr_t>(data))); return false; }

	uint32_t TimerCallback(uint32_t, void*)
	{
		RecordEntry(TimerCount++ == 0 ? 18 : 20);
		return TimerCount == 1 ? 20 : 0;
	}

	uint32_t CanceledTimerCallback(uint32_t, void*) { RecordEntry(21); return 0; }
	int SDLEventWatch(void*, SDL_Event*) { RecordEntry(100); return 1; }

	void* Id(int value) { return reinterpret_cast<void*>(static_cast<intptr_t>(value)); }
}

extern "C"
{
	EMSCRIPTEN_KEEPALIVE int Probe_StartTimer()
	{
		const SDL_TimerID timer = SDL_AddTimer(40, TimerCallback, nullptr);
		CanceledTimer = SDL_AddTimer(45, CanceledTimerCallback, nullptr);
		if (!timer) EM_ASM({ globalThis.webXRNativeCallbackTimerError = UTF8ToString($0); }, SDL_GetError());
		return timer != 0 && CanceledTimer != 0;
	}

	EMSCRIPTEN_KEEPALIVE int Probe_CancelDeferredTimer() { return SDL_RemoveTimer(CanceledTimer); }

	EMSCRIPTEN_KEEPALIVE int Probe_PollSDLEvents()
	{
		int count = 0;
		SDL_Event event;
		while (SDL_PollEvent(&event)) ++count;
		return count;
	}

	EMSCRIPTEN_KEEPALIVE int Probe_Suspend()
	{
		EM_ASM({ globalThis.webXRNativeCallbackSuspended = true; });
		emscripten_sleep(250);
		EM_ASM({ globalThis.webXRNativeCallbackSuspended = false; });
		return 1;
	}
}

int main()
{
	surreal_install_native_call_gate_js();
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) return 1;
	if (!SDL_CreateWindow("native callback gate probe", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 320, 200, 0)) return 2;
	SDL_AddEventWatch(SDLEventWatch, nullptr);

	const char* canvas = "#canvas";
	emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, Id(1), false, KeyCallback);
	emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, Id(2), false, KeyCallback);
	emscripten_set_keypress_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, Id(3), false, KeyCallback);
	emscripten_set_mousedown_callback(canvas, Id(4), false, MouseCallback);
	emscripten_set_mouseup_callback(canvas, Id(5), false, MouseCallback);
	emscripten_set_mousemove_callback(canvas, Id(6), false, MouseCallback);
	emscripten_set_mouseenter_callback(canvas, Id(7), false, MouseCallback);
	emscripten_set_mouseleave_callback(canvas, Id(8), false, MouseCallback);
	emscripten_set_wheel_callback(canvas, Id(9), false, WheelCallback);
	emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, Id(10), false, FocusCallback);
	emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, Id(11), false, FocusCallback);
	emscripten_set_visibilitychange_callback(Id(12), false, VisibilityCallback);
	emscripten_set_touchstart_callback(canvas, Id(13), false, TouchCallback);
	emscripten_set_touchend_callback(canvas, Id(14), false, TouchCallback);
	emscripten_set_touchmove_callback(canvas, Id(15), false, TouchCallback);
	emscripten_set_touchcancel_callback(canvas, Id(16), false, TouchCallback);
	emscripten_set_gamepadconnected_callback(Id(17), false, GamepadCallback);
	emscripten_set_gamepaddisconnected_callback(Id(19), false, GamepadCallback);
	RecordEntry(0);
	return 0;
}
