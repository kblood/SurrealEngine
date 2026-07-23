#pragma once

#include <surrealwidgets/core/widget.h>

class CheckboxLabel;
class Dropdown;
class PushButton;
class TextLabel;

// Only exposes options with qualified consumers in the unified engine. The
// experimental full-body avatar remains isolated on its feature branch.
class VRSettingsPage : public Widget
{
public:
	explicit VRSettingsPage(Widget* parent);
	void Save();

private:
	void OnResetButtonClicked();

	TextLabel* GeneralLabel = nullptr;
	CheckboxLabel* EnableOpenXR = nullptr;
	TextLabel* DominantHandLabel = nullptr;
	Dropdown* DominantHand = nullptr;
	PushButton* ResetButton = nullptr;
};
