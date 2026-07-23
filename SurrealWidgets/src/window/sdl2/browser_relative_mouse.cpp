#include <surrealwidgets/window/browser_relative_mouse.h>
#include <atomic>

static std::atomic<bool> BrowserRelativeMouseBridgeActive = false;

void SetBrowserRelativeMouseBridgeActive(bool active) noexcept
{
	BrowserRelativeMouseBridgeActive.store(active, std::memory_order_release);
}

bool IsBrowserRelativeMouseBridgeActive() noexcept
{
	return BrowserRelativeMouseBridgeActive.load(std::memory_order_acquire);
}
