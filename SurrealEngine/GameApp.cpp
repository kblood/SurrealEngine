
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
			std::cout << "SurrealEngine [--url=<mapname>] [--engineversion=X] [--autoplay] [--probexr] [--debugstereo] [--vr] [Path to game folder]\n";
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
			Logger::Get()->SaveLogAsPlaintext((Directory::localAppData() / "SurrealEngine/SE-Log-LastRun.txt").string());
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
