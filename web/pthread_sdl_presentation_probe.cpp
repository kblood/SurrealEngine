#include <SDL2/SDL.h>
#include <emscripten/threading.h>
#include <surrealwidgets/core/widget.h>
#include <surrealwidgets/window/sdlnativehandle.h>

#include <cstdio>
#include <utility>

int main()
{
	if (emscripten_is_main_browser_thread())
	{
		std::fprintf(stderr, "FAIL probe main did not move to a worker\n");
		return 1;
	}
	auto backend = DisplayBackend::TryCreateBackend();
	if (!backend)
	{
		std::fprintf(stderr, "FAIL no SurrealWidgets display backend\n");
		return 2;
	}
	DisplayBackend::Set(std::move(backend));

	{
		Widget window(nullptr, WidgetType::Window, RenderAPI::Bitmap);
		auto* nativeHandle = static_cast<SDLNativeHandle*>(window.GetNativeHandle());
		if (!nativeHandle || !nativeHandle->window)
		{
			std::fprintf(stderr, "FAIL SurrealWidgets did not expose its SDL window\n");
			return 3;
		}
		if (SDL_GetRenderer(nativeHandle->window) != nullptr || SDL_GL_GetCurrentContext() != nullptr)
		{
			std::fprintf(stderr, "FAIL Bitmap widget unexpectedly owns a renderer or GL context\n");
			return 4;
		}

		DisplayWindow::ProcessEvents();
		std::printf("PASS pthread-sdl-presentation worker=1 renderer=0 gl-context=0\n");
	}
	DisplayBackend::Set(nullptr);
	return 0;
}
