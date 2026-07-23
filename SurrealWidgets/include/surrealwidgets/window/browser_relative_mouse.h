#pragma once

void SetBrowserRelativeMouseBridgeActive(bool active) noexcept;
bool IsBrowserRelativeMouseBridgeActive() noexcept;

constexpr bool ShouldForwardSDLRawMouseMotion(bool cursorLocked, bool browserBridgeActive)
{
	return cursorLocked && !browserBridgeActive;
}
