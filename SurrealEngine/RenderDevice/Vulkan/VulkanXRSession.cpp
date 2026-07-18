
#include "Precomp.h"
#include "VulkanXRSession.h"
#include "Utils/Logger.h"
#include <surrealgpu/vulkaninstance.h>

#define XR_USE_GRAPHICS_API_VULKAN
#ifdef WIN32
#define XR_USE_PLATFORM_WIN32
#include <unknwn.h>
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

VulkanXRSession::VulkanXRSession()
{
	uint32_t extCount = 0;
	XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
	if (XR_FAILED(result))
	{
		lastError = "xrEnumerateInstanceExtensionProperties failed (result=" + std::to_string((int)result) + ") - is an OpenXR runtime registered? (Virtual Desktop / SteamVR / etc.)";
		LogMessage("OpenXR probe: " + lastError);
		return;
	}

	std::vector<XrExtensionProperties> extensions(extCount, { XR_TYPE_EXTENSION_PROPERTIES });
	xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, extensions.data());

	bool hasVulkan2 = false, hasVulkan = false;
	for (auto& ext : extensions)
	{
		std::string name = ext.extensionName;
		if (name == XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME) hasVulkan2 = true;
		if (name == XR_KHR_VULKAN_ENABLE_EXTENSION_NAME) hasVulkan = true;
	}
	LogMessage("OpenXR probe: runtime reports " + std::to_string(extCount) + " extensions, XR_KHR_vulkan_enable2=" + (hasVulkan2 ? "yes" : "no") + " XR_KHR_vulkan_enable=" + (hasVulkan ? "yes" : "no"));

	// Prefer XR_KHR_vulkan_enable (legacy "enable1") over enable2: it fits
	// SurrealGPU's existing VulkanInstanceBuilder/VulkanDeviceBuilder
	// query-then-build pattern (xrGetVulkanInstanceExtensionsKHR /
	// xrGetVulkanGraphicsDeviceKHR feed into our own vkCreateInstance/
	// vkCreateDevice calls), whereas enable2 wants to take over instance/
	// device creation itself via xrCreateVulkanInstanceKHR/
	// xrCreateVulkanDeviceKHR. See VR_IMPLEMENTATION_PLAN.md M2 step 4.
	std::vector<const char*> wantedExtensions;
	if (hasVulkan)
		wantedExtensions.push_back(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
	else if (hasVulkan2)
		wantedExtensions.push_back(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
	else
	{
		lastError = "runtime does not advertise a Vulkan graphics binding extension";
		LogMessage("OpenXR probe: " + lastError);
		return;
	}

	XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
	createInfo.enabledExtensionCount = (uint32_t)wantedExtensions.size();
	createInfo.enabledExtensionNames = wantedExtensions.data();
	strncpy_s(createInfo.applicationInfo.applicationName, "SurrealEngine", XR_MAX_APPLICATION_NAME_SIZE - 1);
	createInfo.applicationInfo.applicationVersion = 1;
	strncpy_s(createInfo.applicationInfo.engineName, "SurrealEngine", XR_MAX_ENGINE_NAME_SIZE - 1);
	createInfo.applicationInfo.engineVersion = 1;
	createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

	XrInstance xrInstance = XR_NULL_HANDLE;
	result = xrCreateInstance(&createInfo, &xrInstance);
	if (XR_FAILED(result))
	{
		lastError = "xrCreateInstance failed (result=" + std::to_string((int)result) + ")";
		LogMessage("OpenXR probe: " + lastError);
		return;
	}
	instance = (void*)xrInstance;

	XrInstanceProperties props = { XR_TYPE_INSTANCE_PROPERTIES };
	xrGetInstanceProperties(xrInstance, &props);
	LogMessage("OpenXR probe: instance created OK, runtime=\"" + std::string(props.runtimeName) + "\"");

	XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrSystemId systemId = XR_NULL_SYSTEM_ID;
	result = xrGetSystem(xrInstance, &systemInfo, &systemId);
	if (XR_FAILED(result))
	{
		lastError = "xrGetSystem failed (result=" + std::to_string((int)result) + ") - no HMD form factor available from this runtime";
		LogMessage("OpenXR probe: " + lastError);
		return;
	}

	LogMessage("OpenXR probe: xrGetSystem OK, systemId=" + std::to_string((unsigned long long)systemId));
	available = true;
}

VulkanXRSession::~VulkanXRSession()
{
	if (instance)
		xrDestroyInstance((XrInstance)instance);
}
