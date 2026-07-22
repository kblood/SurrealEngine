
#include "Precomp.h"
#include "PlayGamePage.h"
#include "LauncherWindow.h"
#include "LauncherSettings.h"
#include "GameFolder.h"
#include <surrealwidgets/widgets/textlabel/textlabel.h>
#include <surrealwidgets/widgets/listview/listview.h>
#include <surrealwidgets/widgets/lineedit/lineedit.h>
#include <surrealwidgets/widgets/checkboxlabel/checkboxlabel.h>

#include "surrealwidgets/widgets/layout/vboxlayout.h"

PlayGamePage::PlayGamePage(LauncherWindow* launcher) : Widget(nullptr), Launcher(launcher)
{
	WelcomeLabel = new TextLabel(this);
	SelectLabel = new TextLabel(this);
	SelectionSummary = new TextLabel(this);
	LaunchInVR = new CheckboxLabel(this);
	SkipVRIntro = new CheckboxLabel(this);
#if defined(EXTRAARGS)
	ParametersLabel = new TextLabel(this);
#endif
	GamesList = new ListView(this);
#if defined(EXTRAARGS)
	ParametersEdit = new LineEdit(this);
#endif

	GamesList->ShowHeader(true);
	GamesList->SetColumn(0, "Game", 400.0);
	GamesList->SetColumn(1, "Version", 75.0);
	GamesList->SetColumn(2, "Mode", 120.0);
	GamesList->SetColumn(3, "Path", 280.0);

	WelcomeLabel->SetText("Welcome to Surreal Engine!");
	SelectLabel->SetText("Please select a game to play:");
	LaunchInVR->SetText("Launch Unreal / Unreal Tournament in OpenXR VR (forces Vulkan)");
	SkipVRIntro->SetText("Skip intro and open the VR menu (recommended for this release)");

	UpdateList();

	auto& settings = LauncherSettings::Get();
	LaunchInVR->SetChecked(settings.Games.LaunchInVR);
	SkipVRIntro->SetChecked(settings.Games.SkipVRIntro);

	if (settings.Games.LastSelected >= 0 && settings.Games.LastSelected < (int)GamesList->GetItemCount())
	{
		GamesList->SetSelectedItem(settings.Games.LastSelected);
		GamesList->ScrollToItem(settings.Games.LastSelected);
	}

	GamesList->OnActivated = [this]() { OnGamesListActivated(); };
	GamesList->OnChanged = [this](int) { UpdateSelectionSummary(); };
	LaunchInVR->FuncChanged = [this](bool) { UpdateSelectionSummary(); };
	SkipVRIntro->FuncChanged = [this](bool) { UpdateSelectionSummary(); };
	UpdateSelectionSummary();

	auto mainLayout = new VBoxLayout();
	mainLayout->AddWidget(WelcomeLabel);
	mainLayout->AddWidget(SelectLabel);
	mainLayout->AddWidget(SelectionSummary);
	mainLayout->AddWidget(LaunchInVR);
	mainLayout->AddWidget(SkipVRIntro);
#if defined(EXTRAARGS)
	mainLayout->AddWidget(ParametersLabel);
#endif
	mainLayout->AddWidget(GamesList);
#if defined(EXTRAARGS)
	mainLayout->AddWidget(ParametersEdit);
#endif

	SetLayout(mainLayout);
}

void PlayGamePage::UpdateList()
{
	GameFolderSelection::UpdateList();

	// GamesList->Clear(); // To do: add this to surrealwidgets
	while (GamesList->GetItemCount() != 0)
		GamesList->RemoveItem((int)GamesList->GetItemCount() - 1);

	for (auto& info : GameFolderSelection::Games)
		GamesList->AddItem({info.gameName, info.gameVersionString, info.SupportsOpenXRVR() ? "OpenXR / Desktop" : "Desktop", info.gameRootFolder});

	int selected = GamesList->GetSelectedItem();
	if (selected < 0 || selected >= (int)GamesList->GetItemCount())
		GamesList->SetSelectedItem(GamesList->GetItemCount() ? 0 : -1);
	UpdateSelectionSummary();
}

void PlayGamePage::UpdateSelectionSummary()
{
	int selected = GamesList->GetSelectedItem();
	if (selected < 0 || selected >= (int)GameFolderSelection::Games.size())
	{
		SelectionSummary->SetText("Selected game: none - add a game folder on the Folders tab");
		return;
	}

	const GameLaunchInfo& info = GameFolderSelection::Games[selected];
	std::string mode = "Desktop";
	if (LaunchInVR->GetChecked() && info.SupportsOpenXRVR())
		mode = SkipVRIntro->GetChecked() ? "OpenXR VR, intro skipped" : "OpenXR VR, normal intro";
	SelectionSummary->SetText("Selected game: " + info.gameName + " " + info.gameVersionString + " - " + mode);
}

void PlayGamePage::Save()
{
	auto& settings = LauncherSettings::Get();
	if (GamesList->GetSelectedItem() != -1)
		settings.Games.LastSelected = GamesList->GetSelectedItem();
	settings.Games.LaunchInVR = LaunchInVR->GetChecked();
	settings.Games.SkipVRIntro = SkipVRIntro->GetChecked();
}

int PlayGamePage::GetSelectedGame()
{
	return GamesList->GetSelectedItem();
}

void PlayGamePage::OnGamesListActivated()
{
	Launcher->Start();
}

void PlayGamePage::OnSetFocus()
{
	GamesList->SetFocus();
}
