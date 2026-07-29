#include "Precomp.h"
#include "NTextLogWindow.h"
#include "VM/NativeFunc.h"
#include "Packages/Extension/Windows/Text/UTextLogWindow.h"

void NTextLogWindow::RegisterFunctions()
{
	RegisterVMNativeFunc_1("TextLogWindow", "AddLog", &NTextLogWindow::AddLog, 1570);
	RegisterVMNativeFunc_0("TextLogWindow", "ClearLog", &NTextLogWindow::ClearLog, 1571);
	RegisterVMNativeFunc_1("TextLogWindow", "PauseLog", &NTextLogWindow::PauseLog, 1573);
	RegisterVMNativeFunc_1("TextLogWindow", "SetTextTimeout", &NTextLogWindow::SetTextTimeout, 1572);
}

void NTextLogWindow::AddLog(UObject* Self, const std::string& NewText)
{
	UTextLogWindow* textlog = UObject::Cast<UTextLogWindow>(Self);
	textlog->AddLog(NewText, textlog->TextColor());
}

void NTextLogWindow::ClearLog(UObject* Self)
{
	UTextLogWindow* textlog = UObject::Cast<UTextLogWindow>(Self);
	textlog->ClearLog();
}

void NTextLogWindow::PauseLog(UObject* Self, bool bNewPauseState)
{
	UTextLogWindow* textlog = UObject::Cast<UTextLogWindow>(Self);
	textlog->PauseLog(bNewPauseState);
}

void NTextLogWindow::SetTextTimeout(UObject* Self, float newTimeout)
{
	UTextLogWindow* textlog = UObject::Cast<UTextLogWindow>(Self);
	textlog->SetTextTimeout(newTimeout);
}
