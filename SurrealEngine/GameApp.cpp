
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

	bool autoplayMode = false;
	for (const std::string& arg : args)
	{
		if (arg == "--autoplay")
			autoplayMode = true;
	}

	try
	{
		CommandLine cmd(args);
		commandline = &cmd;

		if (ErrorWindow::CheckCrashReporter())
			return 0;

		if (commandline->HasArg("-h", "--help"))
		{
			std::cout << "SurrealEngine [--url=<mapname>] [--engineversion=X] [--autoplay] [--exec=<consolecommand>] [Path to game folder]\n";
			return 0;
		}

		if (commandline->HasArg("", "--autoplay"))
		{
			// Non-interactive launch for scripted verification: skip the
			// launcher GUI and run the first game resolved from the
			// command-line folder / search list.
			GameFolderSelection::UpdateList();
			if (!GameFolderSelection::Games.empty())
			{
				GameLaunchInfo info = GameFolderSelection::GetLaunchInfo(0);
				Engine engine(info);
				engine.Run();
			}
			else
			{
				std::cout << "--autoplay: no UE1 game found\n";
			}
		}
		else
		{
			int selectedGameIndex = LauncherWindow::ExecModal();
			if (selectedGameIndex >= 0)
			{
				GameLaunchInfo info = GameFolderSelection::GetLaunchInfo(selectedGameIndex);
				Engine engine(info);
				engine.Run();
			}
		}
	}
	catch (const std::exception& e)
	{
		if (autoplayMode)
		{
			// Non-interactive runs must never block on a modal error dialog.
			std::cerr << "Unhandled Exception: " << e.what() << std::endl;
		}
		else
		{
			ErrorWindow::ExecModal(e.what(), Logger::Get()->GetLog());
		}
	}

	DeinitWidgetResources();
	return 0;
}
