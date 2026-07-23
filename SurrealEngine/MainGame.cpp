
#include "Precomp.h"
#include "GameApp.h"
#include "Utils/UTF16.h"
#include <iostream>
#include <vector>
#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
#include <emscripten/emscripten.h>
#endif
#ifdef WIN32
#include <CommCtrl.h>
#include <ObjBase.h>
#include <shellapi.h>
#endif

#ifdef WIN32

#pragma comment(linker, "\"/manifestdependency:type='Win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "Ws2_32.lib")

class InitCOM
{
public:
	InitCOM()
	{
		HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (FAILED(result))
			Exception::Throw("CoInitializeEx(COINIT_APARTMENTTHREADED) failed");
	}

	~InitCOM()
	{
		CoUninitialize();
	}
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	try
	{
		Array<std::string> args;
		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
		if (argv)
		{
			for (int i = 1; i < argc; i++)
				args.push_back(from_utf16(argv[i]));
			LocalFree(argv);
		}

		//SetProcessDPIAware();
		SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
		InitCommonControls();

		WORD winsock_version = MAKEWORD(2, 2);
		WSADATA wsaData;
		int err = WSAStartup(winsock_version, &wsaData);
		if (err != 0)
			Exception::Throw("Failed to initialize winsockets");

		InitCOM initCOM;

		GameApp app;
		return app.main(std::move(args));
	}
	catch (const std::exception& e)
	{
		MessageBox(0, to_utf16(e.what()).c_str(), to_utf16("Unhandled Exception").c_str(), MB_OK | MB_ICONEXCLAMATION);
		return 1;
	}
}

#else

static int RunGame(Array<std::string> args)
{
	try
	{
		GameApp app;
		return app.main(std::move(args));
	}
	catch (const std::exception& e)
	{
		std::cout << e.what() << std::endl;
		return 1;
	}
}

int main(int argc, char** argv)
{
	Array<std::string> args;
	for (int i = 1; i < argc; i++)
		args.push_back(argv[i]);
	return RunGame(std::move(args));
}

#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
extern "C" EMSCRIPTEN_KEEPALIVE int Surreal_StartBrowserGame(const char* map, const char* renderer, int skipIntro)
{
	Array<std::string> args;
	args.push_back("--autoplay");
	if (skipIntro && map && *map)
		args.push_back(std::string("--url=") + map);
	args.push_back(std::string("--render=") + (renderer && *renderer ? renderer : "webgpu"));
	args.push_back("/gamedata");
	return RunGame(std::move(args));
}
#endif

#endif
