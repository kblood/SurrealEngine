#pragma once

// Minimal OpenXR instance probe (M2 step 1/4 of VR_IMPLEMENTATION_PLAN.md).
// Deliberately kept separate from VulkanRenderDevice.cpp so flatscreen play
// is completely unaffected when --vr/--probexr isn't passed. This does not
// yet create a session or swapchain - it only proves the OpenXR loader
// links and that xrCreateInstance/xrGetSystem succeed (or fail cleanly with
// a logged reason, e.g. no active OpenXR runtime registered).

#include <string>

class VulkanXRSession
{
public:
	VulkanXRSession();
	~VulkanXRSession();

	// True once xrCreateInstance + xrGetSystem both succeeded.
	bool IsAvailable() const { return available; }
	const std::string& LastError() const { return lastError; }

private:
	bool available = false;
	std::string lastError;
	void* instance = nullptr; // XrInstance, stored as void* to keep this header openxr.h-free
};
