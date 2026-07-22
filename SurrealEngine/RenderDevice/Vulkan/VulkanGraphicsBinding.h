#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Optional Vulkan consumers can contribute runtime requirements before the
// instance/device are created and receive the final handles afterward. This
// keeps RenderDevice and GameWindow independent of OpenXR types.
class VulkanGraphicsBinding
{
public:
	virtual ~VulkanGraphicsBinding() = default;
	virtual std::vector<std::string> GetVulkanInstanceExtensions() = 0;
	virtual bool ResolveVulkanDevice(void* instance, void** physicalDevice, std::vector<std::string>& deviceExtensions) = 0;
	virtual bool OnVulkanDeviceCreated(void* instance, void* physicalDevice, void* device, uint32_t queueFamilyIndex, uint32_t queueIndex) = 0;
};
