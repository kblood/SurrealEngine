
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
	const bool noActivate = std::find(args.begin(), args.end(), "--noactivate") != args.end();
	std::string logFile;
	auto backend = DisplayBackend::TryCreateBackend();
	DisplayBackend::Set(std::move(backend));
	InitWidgetResources();
	WidgetTheme::SetTheme(std::make_unique<DarkWidgetTheme>());

	try
	{
		CommandLine cmd(args);
		commandline = &cmd;
		logFile = commandline->GetArg("", "--logfile");

		if (ErrorWindow::CheckCrashReporter())
			return 0;

		if (commandline->HasArg("-h", "--help"))
		{
			std::cout << "SurrealEngine [--url=<mapname>] [--engineversion=X] [--nosound] [--noactivate] [--autostart] [--logfile=<path>] [Path to game folder]\n";
			return 0;
		}

		int selectedGameIndex = -1;
		if (commandline->HasArg("", "--autostart"))
		{
			GameFolderSelection::UpdateList();
			if (GameFolderSelection::Games.size() != 1)
				Exception::Throw("--autostart requires exactly one detected game folder");
			selectedGameIndex = 0;
		}
		else
		{
			selectedGameIndex = LauncherWindow::ExecModal();
		}
		if (selectedGameIndex >= 0)
		{
			GameLaunchInfo info = GameFolderSelection::GetLaunchInfo(selectedGameIndex);
			Engine engine(info);
			engine.Run();
		}
	}
	catch (const std::exception& e)
	{
		if (noActivate)
		{
			LogMessage(std::string("Fatal error: ") + e.what());
			Logger::Get()->SaveLogAsPlaintext(logFile.empty() ? (Directory::localAppData() / "SurrealEngine/SE-Log-LastRun.txt").string() : logFile);
			std::cerr << "SurrealEngine error: " << e.what() << '\n';
		}
		else
			ErrorWindow::ExecModal(e.what(), Logger::Get()->GetLog());
	}

	DeinitWidgetResources();
	return 0;
}
