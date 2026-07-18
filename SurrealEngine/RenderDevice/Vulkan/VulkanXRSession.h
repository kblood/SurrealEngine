#pragma once

// Minimal OpenXR instance probe (M2 step 1/4 of VR_IMPLEMENTATION_PLAN.md).
// Deliberately kept separate from VulkanRenderDevice.cpp so flatscreen play
// is completely unaffected when --vr/--probexr isn't passed. This does not
// yet create a session or swapchain - it only proves the OpenXR loader
// links and that xrCreateInstance/xrGetSystem succeed (or fail cleanly with
// a logged reason, e.g. no active OpenXR runtime registered).

#include <string>
#include <vector>

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

// Vulkan instance/device requirements an OpenXR runtime imposes on us, to be
// threaded into VulkanRenderDevice's constructor (see M2 step 4/10 of
// VR_IMPLEMENTATION_PLAN.md). We enable1 (XR_KHR_vulkan_enable): the runtime
// tells us what to require via xrGetVulkanInstanceExtensionsKHR /
// xrGetVulkanDeviceExtensionsKHR / xrGetVulkanGraphicsDeviceKHR, and we still
// do our own vkCreateInstance/vkCreateDevice through the existing
// VulkanInstanceBuilder/VulkanDeviceBuilder pattern - this struct is just the
// carrier for those runtime-provided requirements. Kept Vulkan-header-free
// (physicalDevice as void*, itself just an opaque pointer typedef) so this
// header doesn't drag <vulkan/vulkan.h> into files that don't need it.
struct VulkanXRInitOverrides
{
	std::vector<std::string> instanceExtensions;
	std::vector<std::string> deviceExtensions;

	// The exact physical device OpenXR requires (xrGetVulkanGraphicsDeviceKHR) -
	// it must back the HMD compositor, so on a multi-GPU system we can't let
	// VulkanDeviceBuilder's own scoring pick a different one. Null means "no
	// requirement", e.g. before a session exists yet.
	void* physicalDevice = nullptr;
};
