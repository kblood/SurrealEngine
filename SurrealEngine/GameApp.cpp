
#include "Precomp.h"
#include "Utils/Exception.h"
#include "Utils/Logger.h"
#include "Utils/CommandLine.h"
#include "GameApp.h"
#include "GameFolder.h"
#include "Engine.h"
#include "UI/WidgetResourceData.h"
#include "UI/ErrorWindow/ErrorWindow.h"
#include "UI/Launcher/LauncherWindow.h"
#include "Utils/File.h"
#include "Platform/OpenXR/OpenXRProvider.h"
#include <stdexcept>
#include <surrealwidgets/core/theme.h>
#include <surrealwidgets/window/window.h>
#include <iostream>

int GameApp::main(Array<std::string> args)
{
	auto backend = DisplayBackend::TryCreateBackend();
	DisplayBackend::Set(std::move(backend));
	InitWidgetResources();
	WidgetTheme::SetTheme(std::make_unique<DarkWidgetTheme>());

	try
	{
		CommandLine cmd(args);
		commandline = &cmd;

		if (ErrorWindow::CheckCrashReporter())
			return 0;

		if (commandline->HasArg("-h", "--help"))
		{
			std::cout << "SurrealEngine [--url=<mapname>] [--engineversion=X] [--openxr] [--probexr] [Path to game folder]\n";
			return 0;
		}
		if (commandline->HasArg("", "--probexr"))
		{
			OpenXRProvider probe;
			std::cout << (probe.IsAvailable() ? "OpenXR runtime and HMD are available\n" : "OpenXR unavailable: " + probe.LastError() + "\n");
			DeinitWidgetResources();
			return probe.IsAvailable() ? 0 : 1;
		}

		int selectedGameIndex = LauncherWindow::ExecModal();
		if (selectedGameIndex >= 0)
		{
			GameLaunchInfo info = GameFolderSelection::GetLaunchInfo(selectedGameIndex);
			Engine engine(info);
			engine.Run();
		}
	}
	catch (const std::exception& e)
	{
		ErrorWindow::ExecModal(e.what(), Logger::Get()->GetLog());
	}

	DeinitWidgetResources();
	return 0;
}
