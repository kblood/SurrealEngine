#pragma once

#include <surrealwidgets/core/widget.h>

class CheckboxLabel;
class Dropdown;
class PushButton;
class TextLabel;

// Native XR options supported by the provider-neutral OpenXR integration.
// Unsupported controls from the older Farantir VR subsystem are deliberately
// not exposed: every control on this page has a live consumer in this build.
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

	TextLabel* AvatarLabel = nullptr;
	CheckboxLabel* FullBodyAvatar = nullptr;
	TextLabel* AvatarDescription = nullptr;

	PushButton* ResetButton = nullptr;
};
