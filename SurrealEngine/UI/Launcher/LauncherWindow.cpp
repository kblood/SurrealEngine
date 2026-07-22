
#include "Precomp.h"
#include "LauncherWindow.h"
#include "LauncherBanner.h"
#include "LauncherButtonbar.h"
#include "PlayGamePage.h"
#include "VideoSettingsPage.h"
// #include "AudioSettingsPage.h"
#include "GameFoldersPage.h"
#include "LauncherSettings.h"
#include "GameFolder.h"
#include "Utils/Logger.h"
#include <surrealwidgets/core/resourcedata.h>
#include <surrealwidgets/window/window.h>
#include <surrealwidgets/widgets/tabwidget/tabwidget.h>
#include <surrealwidgets/widgets/layout/vboxlayout.h>

int LauncherWindow::ExecModal()
{
	Size screenSize = GetScreenSize();
	double windowWidth = 1024.0;
	double windowHeight = 750.0;

	auto launcher = std::make_unique<LauncherWindow>();
	launcher->SetFrameGeometry((screenSize.width - windowWidth) * 0.5, (screenSize.height - windowHeight) * 0.5, windowWidth, windowHeight);
	launcher->Show();

	DisplayWindow::RunLoop();

	return launcher->ExecResult;
}

LauncherWindow::LauncherWindow() : Widget(nullptr, WidgetType::Window)
{
	SetWindowTitle("Surreal Engine");
	SetWindowIcon({
		Image::LoadResource("surreal-engine-icon-16.png"),
		Image::LoadResource("surreal-engine-icon-24.png"),
		Image::LoadResource("surreal-engine-icon-32.png"),
		Image::LoadResource("surreal-engine-icon-48.png"),
		Image::LoadResource("surreal-engine-icon-64.png"),
		Image::LoadResource("surreal-engine-icon-128.png"),
		Image::LoadResource("surreal-engine-icon-256.png")
		});

	Banner = new LauncherBanner(this);
	Pages = new TabWidget(this);
	Buttonbar = new LauncherButtonbar(this);

	PlayGame = new PlayGamePage(this);
	GraphicsSettings = new VideoSettingsPage(this);
	// AudioSettings = new AudioSettingsPage(this);
	GameFolders = new GameFoldersPage(this);

	Pages->AddTab(PlayGame, "Games");
	Pages->AddTab(GameFolders, "Folders");
	Pages->AddTab(GraphicsSettings, "Video Settings");
	// Pages->AddTab(AudioSettings, "Audio Settings");

	Pages->SetCurrentWidget(PlayGame);
	PlayGame->SetFocus();

	auto mainLayout = new VBoxLayout();

	mainLayout->AddWidget(Banner);
	mainLayout->AddWidget(Pages);
	mainLayout->AddWidget(Buttonbar);

	mainLayout->SetGapHeight(0);

	SetLayout(mainLayout);
}

void LauncherWindow::Save()
{
	PlayGame->Save();
	GraphicsSettings->Save();
	// AudioSettings->Save();
	GameFolders->Save();
	LauncherSettings::Get().Save();
}

void LauncherWindow::GamesListChanged()
{
	PlayGame->UpdateList();
}

void LauncherWindow::Start()
{
	// The Play button is shared by every tab. A row selected on Folders is a
	// search path, not a game selection; launching from there used the stale
	// row on Games (usually index 0), making an Unreal folder appear to launch
	// UT99. First press now returns to the authoritative Games list so the user
	// must see and confirm the actual game row that will launch.
	if (Pages->GetCurrentWidget() != PlayGame)
	{
		Pages->SetCurrentWidget(PlayGame);
		PlayGame->SetFocus();
		return;
	}

	Save();
	ExecResult = PlayGame->GetSelectedGame();
	if (ExecResult >= 0 && ExecResult < (int)GameFolderSelection::Games.size())
	{
		const GameLaunchInfo& selected = GameFolderSelection::Games[ExecResult];
		LogMessage("Launcher confirmed row " + std::to_string(ExecResult) + ": " + selected.gameName +
			" " + selected.gameVersionString + " root=" + selected.gameRootFolder);
	}
	DisplayWindow::ExitLoop();
}

void LauncherWindow::Exit()
{
	Save();
	ExecResult = -1;
	DisplayWindow::ExitLoop();
}

void LauncherWindow::OnClose()
{
	Exit();
}
