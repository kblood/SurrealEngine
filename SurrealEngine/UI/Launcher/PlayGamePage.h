#pragma once

#include <surrealwidgets/core/widget.h>

// #define EXTRAARGS

class LauncherWindow;
class TextLabel;
class ListView;
class LineEdit;
class CheckboxLabel;
struct GameLaunchInfo;

class PlayGamePage : public Widget
{
public:
	PlayGamePage(LauncherWindow* launcher);

	int GetSelectedGame();
	void UpdateList();
	void Save();

private:
	void OnSetFocus() override;
	void OnGamesListActivated();
	void UpdateSelectionSummary();

	LauncherWindow* Launcher = nullptr;

	TextLabel* WelcomeLabel = nullptr;
	TextLabel* SelectLabel = nullptr;
	TextLabel* SelectionSummary = nullptr;
	CheckboxLabel* LaunchInVR = nullptr;
	CheckboxLabel* SkipVRIntro = nullptr;
#if defined(EXTRAARGS)
	TextLabel* ParametersLabel = nullptr;
#endif
	ListView* GamesList = nullptr;
#if defined(EXTRAARGS)
	LineEdit* ParametersEdit = nullptr;
#endif
};
