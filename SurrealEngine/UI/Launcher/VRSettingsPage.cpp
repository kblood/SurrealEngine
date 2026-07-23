#include "Precomp.h"
#include "VRSettingsPage.h"
#include "LauncherSettings.h"
#include <surrealwidgets/widgets/checkboxlabel/checkboxlabel.h>
#include <surrealwidgets/widgets/dropdown/dropdown.h>
#include <surrealwidgets/widgets/pushbutton/pushbutton.h>
#include <surrealwidgets/widgets/textlabel/textlabel.h>
#include <surrealwidgets/widgets/layout/hboxlayout.h>
#include <surrealwidgets/widgets/layout/vboxlayout.h>

VRSettingsPage::VRSettingsPage(Widget* parent)
	: Widget(parent)
{
	GeneralLabel = new TextLabel(this);
	EnableOpenXR = new CheckboxLabel(this);
	DominantHandLabel = new TextLabel(this);
	DominantHand = new Dropdown(this);
	ResetButton = new PushButton(this);

	GeneralLabel->SetText("Virtual reality:");
#ifdef SURREAL_ENABLE_OPENXR
	EnableOpenXR->SetText("Enable native OpenXR when starting a game");
#else
	EnableOpenXR->SetText("Enable native OpenXR (unavailable in this build)");
	EnableOpenXR->SetDisabled(true);
#endif
	DominantHandLabel->SetText("Weapon and pointer hand");
	DominantHand->AddItem("Right");
	DominantHand->AddItem("Left");
	ResetButton->SetText("Reset VR options to defaults");

	auto& settings = LauncherSettings::Get();
#ifdef SURREAL_ENABLE_OPENXR
	EnableOpenXR->SetChecked(settings.XR.Enabled);
#else
	EnableOpenXR->SetChecked(false);
#endif
	DominantHand->SetSelectedItem(settings.XR.DominantHand == XRHand::Left ? 1 : 0);
	ResetButton->OnClick = [this]() { OnResetButtonClicked(); };

	auto handLayout = new HBoxLayout();
	handLayout->AddWidget(DominantHandLabel);
	handLayout->AddWidget(DominantHand);
	handLayout->AddStretch();
	auto resetLayout = new HBoxLayout();
	resetLayout->AddWidget(ResetButton);
	resetLayout->AddStretch();
	auto mainLayout = new VBoxLayout();
	mainLayout->AddWidget(GeneralLabel);
	mainLayout->AddWidget(EnableOpenXR);
	mainLayout->AddLayout(handLayout);
	mainLayout->AddLayout(resetLayout);
	mainLayout->AddStretch();
	SetLayout(mainLayout);
}

void VRSettingsPage::Save()
{
	auto& settings = LauncherSettings::Get();
	settings.XR.Enabled = EnableOpenXR->GetChecked();
	settings.XR.DominantHand = DominantHand->GetSelectedItem() == 1 ?
		XRHand::Left : XRHand::Right;
}

void VRSettingsPage::OnResetButtonClicked()
{
	EnableOpenXR->SetChecked(false);
	DominantHand->SetSelectedItem(0);
}
