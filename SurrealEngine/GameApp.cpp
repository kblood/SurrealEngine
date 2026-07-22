
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
#include "RenderDevice/Vulkan/VulkanXRSession.h"
#include <stdexcept>
#include <surrealwidgets/core/theme.h>
#include <surrealwidgets/window/window.h>
#include <iostream>

namespace
{
	void SaveDiagnosticLog()
	{
		fs::path logPath = Directory::localAppData() / "SurrealEngine/SE-Log-LastRun.txt";
		std::string requestedPath = commandline->GetArg("", "--logfile");
		if (!requestedPath.empty())
			logPath = fs::absolute(fs::path(requestedPath));
		if (logPath.has_parent_path())
			fs::create_directories(logPath.parent_path());
		LogMessage("Diagnostic log: " + logPath.string());
		Logger::Get()->SaveLogAsPlaintext(logPath.string());
	}
}

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
			std::cout << "SurrealEngine [--url=<mapname>] [--engineversion=X] [--autoplay] [--probexr] [--debugstereo] [--vr] [--vr-startmenu] [--vr-quadmenu] [--vr-lefthand] [--vr-no-menu-laser] [--vr-no-menu-controllers] [--logfile=<path>] [--debugvrhands] [--debugvrfire] [--debugvrtwohand] [--debugvrdualenforcer] [Path to game folder]\n";
			return 0;
		}

		if (commandline->HasArg("", "--probexr"))
		{
			// Non-interactive OpenXR diagnostic: no window, no game launch.
			// Just proves the loader links and reports whether a usable
			// OpenXR runtime + Vulkan graphics binding is currently
			// registered on this machine. See VR_IMPLEMENTATION_PLAN.md M2.
			VulkanXRSession probe;
			if (probe.IsAvailable())
				std::cout << "--probexr: OpenXR instance + HMD system OK\n";
			else
				std::cout << "--probexr: unavailable - " << probe.LastError() << "\n";
			SaveDiagnosticLog();
			DeinitWidgetResources();
			return 0;
		}

		if (commandline->HasArg("", "--autoplay"))
		{
			// Non-interactive launch: skip the launcher GUI entirely and run the
			// first game resolved from the command-line folder / search list.
			GameFolderSelection::UpdateList();
			if (!GameFolderSelection::Games.empty())
			{
				GameLaunchInfo info = GameFolderSelection::GetLaunchInfo(0);
				LogMessage("Launch selection: game=" + info.gameName + " root=" + info.gameRootFolder +
					" url=" + (info.url.empty() ? std::string("<default>") : info.url) +
					" noEntryMap=" + std::to_string(info.noEntryMap));
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
		ErrorWindow::ExecModal(e.what(), Logger::Get()->GetLog());
	}

	DeinitWidgetResources();
	return 0;
}
