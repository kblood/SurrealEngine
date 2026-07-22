
#include "Precomp.h"
#include "Utils/Exception.h"
#include "Utils/Logger.h"
#include "Utils/CommandLine.h"
#include "GameApp.h"
#include "GameFolder.h"
#include "Engine.h"
#include "BotBenchmark.h"
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
	CommandLine cmd(args);
	commandline = &cmd;
	const bool benchmarkRequested = BotBenchmark::Requested(cmd);
	bool widgetResourcesInitialized = false;

	// Benchmark mode must be able to run on CI machines without a display.
	// Parse it before creating a DisplayBackend or any widget resources.
	if (!benchmarkRequested)
	{
		auto backend = DisplayBackend::TryCreateBackend();
		DisplayBackend::Set(std::move(backend));
		InitWidgetResources();
		WidgetTheme::SetTheme(std::make_unique<DarkWidgetTheme>());
		widgetResourcesInitialized = true;
	}

	int result = 0;
	try
	{
		if (!benchmarkRequested && ErrorWindow::CheckCrashReporter())
			return 0;

		if (commandline->HasArg("-h", "--help"))
		{
			std::cout << "SurrealEngine [--url=<mapname>] [--engineversion=X] [--autoplay] [--probexr] [--debugstereo] [--botbench[=scenario]] [--botbench-output=dir] [--botbench-seed=N] [--botbench-ticks=N] [--botbench-skill=0..7] [Path to game folder]\n";
			if (widgetResourcesInitialized)
				DeinitWidgetResources();
			return 0;
		}

		if (benchmarkRequested)
		{
			BotBenchmark::Configure(cmd);
			GameFolderSelection::UpdateList();
			if (GameFolderSelection::Games.empty())
			{
				BotBenchmark::Get().Fail("No UE1 game found", 3);
				return BotBenchmark::Get().GetExitCode();
			}

			GameLaunchInfo info = GameFolderSelection::GetLaunchInfo(0);
			info.noEntryMap = true;
			info.url = BotBenchmark::Get().GetConfig().URL;
			BotBenchmark::Emit("launch_resolved", { { "game", info.gameName }, { "version", info.gameVersionString } });
			Engine benchmarkEngine(info);
			benchmarkEngine.Run();
			return benchmarkEngine.GetRunExitCode();
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
		if (benchmarkRequested)
		{
			std::cerr << "Bot benchmark failed: " << e.what() << '\n';
			if (BotBenchmark::IsActive())
			{
				BotBenchmark::Get().Fail(e.what(), 4);
				result = BotBenchmark::Get().GetExitCode();
			}
			else
			{
				result = 4;
			}
		}
		else
		{
			ErrorWindow::ExecModal(e.what(), Logger::Get()->GetLog());
		}
	}

	if (widgetResourcesInitialized)
		DeinitWidgetResources();
	return result;
}
