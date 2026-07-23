
#include "Precomp.h"
#include "Utils/Exception.h"
#include "Utils/Logger.h"
#include "Utils/CommandLine.h"
#include "GameApp.h"
#include "GameFolder.h"
#include "Engine.h"
#include "VM/Frame.h"
#include "LauncherSettings.h"
#include "RenderDevice/RenderDeviceSelection.h"
#include "UI/WidgetResourceData.h"
#include "UI/ErrorWindow/ErrorWindow.h"
#include "UI/Launcher/LauncherWindow.h"
#include "Utils/File.h"
#include "Platform/OpenXR/OpenXRProvider.h"
#include "XR/Avatar/AvatarRenderer.h"
#ifdef SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS
#include "Platform/BrowserGameDataMount.h"
#endif
#include <stdexcept>
#include <surrealwidgets/core/theme.h>
#include <surrealwidgets/window/window.h>
#include <iostream>

static std::string SaveFatalErrorReport(const std::string& errorText)
{
	const fs::path reportDirectory = Directory::localAppData() / "SurrealEngine";
	Directory::create(reportDirectory.string());

	const std::string reportFilename = (reportDirectory / "SE-Error-LastRun.txt").string();
	Logger::Get()->SaveLogAsPlaintext(reportFilename);
	File::write_all_text(reportFilename, File::read_all_text(reportFilename) + "\nExecution could not continue.\n" + errorText + "\n");
	return reportFilename;
}

#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
extern "C" void surreal_install_native_call_gate_js();
#endif

int GameApp::main(Array<std::string> args)
{
#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
	surreal_install_native_call_gate_js();
#endif
	auto backend = DisplayBackend::TryCreateBackend();
	DisplayBackend::Set(std::move(backend));
#ifndef __EMSCRIPTEN__
	// SurrealEngine.pk3 (widget fonts/icons) isn't preloaded for M1 - no UI
	// is ever rendered (NullRenderDevice, Launcher skipped), so leave the
	// resourcedata_emscripten.cpp default ResourceLoader in place instead of
	// having ResourceLoaderPK3 throw on a file that doesn't exist.
	InitWidgetResources();
#endif
	WidgetTheme::SetTheme(std::make_unique<DarkWidgetTheme>());
	int result = 0;
	bool autoplay = false;

	try
	{
		CommandLine cmd(args);
		commandline = &cmd;
		autoplay = commandline->HasArg("", "--autoplay");

#ifdef SURREAL_WEB_EXPERIMENTAL_WASMFS_OPFS
		if (!BrowserGameDataMount::MountConfigured())
			throw std::runtime_error("Browser game-data mount failed: " + BrowserGameDataMount::LastError());
#endif

		if (ErrorWindow::CheckCrashReporter())
			return 0;

		if (commandline->HasArg("-h", "--help"))
		{
			std::cout << "SurrealEngine [--url=<mapname>] [--engineversion=X] [--autoplay] [--render=webgpu|webgl2|null] [--openxr|--no-openxr] [--vr-lefthand] [--probexr] [--headless-driver=<name>] [--botbench-url=<url>] [--botbench-output=<dir>] [--botbench-seed=N] [--botbench-ticks=N] [--botbench-fixed-delta=S] [--botbench-difficulty=0..7] [--avatar-autorig-debug] [--avatar-ik-synthetic] [--avatar-cull-head-debug] [--minimized-window] [--mute-audio] [Path to game folder]\n";
			return 0;
		}

		if (commandline->HasArg("", "--avatar-autorig-debug"))
		{
			// Diagnostic hook: logs auto-rig joint labels for the level's
			// pawn meshes at map load (see RenderSubsystem::OnMapLoaded), and
			// draws the local player's auto-rigged avatar beside their normal
			// render each frame, driven by IK from a live head/hand sample
			// (real OpenXR, or --avatar-ik-synthetic) when one is available.
			AvatarRenderer::SetDiagnosticsEnabled(true);
		}
		if (commandline->HasArg("", "--avatar-cull-head-debug"))
		{
			// M4 verification-only hook: forces the --avatar-autorig-debug
			// side-by-side draw to also cull Head/Neck triangles, so a real
			// run can produce evidence for the head-culling triangle-count
			// claim. No effect without --avatar-autorig-debug.
			AvatarRenderer::SetCullHeadDebugEnabled(true);
		}
		if (commandline->HasArg("", "--probexr"))
		{
			OpenXRProvider probe;
			std::cout << (probe.IsAvailable() ? "OpenXR runtime and HMD are available\n" : "OpenXR unavailable: " + probe.LastError() + "\n");
			DeinitWidgetResources();
			return probe.IsAvailable() ? 0 : 1;
		}

#ifdef __EMSCRIPTEN__
		const std::string rendererName = commandline->GetArg("", "--render");
		if (!rendererName.empty())
		{
			const auto* selection = FindRenderDeviceSelection(rendererName);
			if (!selection)
				Exception::Throw("Unknown render backend: " + rendererName);
			if (!selection->Compiled)
				Exception::Throw(std::string(selection->UnavailableReason));

			LauncherSettings::Get().RenderDevice.Type = selection->Type;
		}
#endif

		if (autoplay)
		{
			// Non-interactive launch: skip the launcher GUI entirely and run the
			// first game resolved from the command-line folder / search list.
			GameFolderSelection::UpdateList();
			if (!GameFolderSelection::Games.empty())
			{
				GameLaunchInfo info = GameFolderSelection::GetLaunchInfo(0);
#ifdef __EMSCRIPTEN__
				// emscripten_set_main_loop_arg registers the RAF callback and
				// returns immediately (simulate_infinite_loop=0, matching
				// QuakeQuest's main_web.c) - engine must outlive this stack
				// frame, so give it static storage duration instead of letting
				// it be destroyed when GameApp::main() returns.
				static Engine engine(info);
#else
				Engine engine(info);
#endif
				engine.Run();
				result = engine.GetRunExitCode();
			}
			else
			{
				std::cout << "--autoplay: no UE1 game found\n";
			}
		}
#ifndef __EMSCRIPTEN__
		else
		{
			// The Launcher's modal event pump is unavailable in browser builds.
			int selectedGameIndex = LauncherWindow::ExecModal();
			if (selectedGameIndex >= 0)
			{
				GameLaunchInfo info = GameFolderSelection::GetLaunchInfo(selectedGameIndex);
				Engine engine(info);
				engine.Run();
				result = engine.GetRunExitCode();
			}
		}
#else
		else
		{
			std::cout << "--autoplay is required (the Launcher GUI is not available in this build)\n";
		}
#endif
	}
	catch (const std::exception& e)
	{
#ifndef __EMSCRIPTEN__
		std::string errorText = e.what();
		std::string scriptCallstack = Frame::GetCallstack();
		if (!scriptCallstack.empty())
			errorText += "\n\nUnrealScript call stack:\n" + scriptCallstack;
		SaveFatalErrorReport(errorText);
		if (autoplay)
			result = 1;
		else
			ErrorWindow::ExecModal(errorText, Logger::Get()->GetLog());
#else
		// ErrorWindow::ExecModal's DisplayWindow::RunLoop() is the same
		// blocking modal event pump the Launcher can't use here - just log.
		// Dump the full log too: LogMessage() only buffers into Logger, it
		// never writes to stdout, so without this only the final exception
		// text would be visible.
		std::cout << "Fatal error: " << e.what() << "\n";
		for (const auto& line : Logger::Get()->GetLog())
			std::cout << "[" << line.Source << "] " << line.Text << "\n";
#endif
	}

#ifndef __EMSCRIPTEN__
	DeinitWidgetResources();
#endif
	return result;
}
