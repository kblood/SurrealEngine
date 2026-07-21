
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

#include <algorithm>
#include <sstream>
#include <cmath>

namespace
{
	std::vector<std::string> SplitSpaceSeparated(const std::string& s)
	{
		std::vector<std::string> result;
		std::istringstream iss(s);
		std::string tok;
		while (iss >> tok)
			result.push_back(tok);
		return result;
	}

	const char* SessionStateName(XrSessionState state)
	{
		switch (state)
		{
		case XR_SESSION_STATE_UNKNOWN: return "UNKNOWN";
		case XR_SESSION_STATE_IDLE: return "IDLE";
		case XR_SESSION_STATE_READY: return "READY";
		case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
		case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
		case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
		case XR_SESSION_STATE_STOPPING: return "STOPPING";
		case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
		case XR_SESSION_STATE_EXITING: return "EXITING";
		default: return "?";
		}
	}

	// XR_KHR_vulkan_enable's functions are declared behind
	// XR_EXTENSION_PROTOTYPES (which we don't define, matching how the rest
	// of this codebase treats OpenXR), so they must be resolved dynamically
	// via xrGetInstanceProcAddr rather than linked directly - same pattern
	// every OpenXR sample uses for extension functions.
	template<typename T>
	T LoadXRExtFunc(XrInstance instance, const char* name)
	{
		PFN_xrVoidFunction fn = nullptr;
		xrGetInstanceProcAddr(instance, name, &fn);
		return reinterpret_cast<T>(fn);
	}
}

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
	XrSystemId xrSystemId = XR_NULL_SYSTEM_ID;
	result = xrGetSystem(xrInstance, &systemInfo, &xrSystemId);
	if (XR_FAILED(result))
	{
		lastError = "xrGetSystem failed (result=" + std::to_string((int)result) + ") - no HMD form factor available from this runtime";
		LogMessage("OpenXR probe: " + lastError);
		return;
	}
	systemId = (uint64_t)xrSystemId;

	LogMessage("OpenXR probe: xrGetSystem OK, systemId=" + std::to_string((unsigned long long)xrSystemId));
	available = true;
}

VulkanXRSession::~VulkanXRSession()
{
	DestroySwapchains();
	DestroySession();
	if (instance)
		xrDestroyInstance((XrInstance)instance);
}

std::vector<std::string> VulkanXRSession::GetVulkanInstanceExtensions()
{
	if (!available)
		return {};
	XrInstance xrInstance = (XrInstance)instance;

	auto pfnGetReq = LoadXRExtFunc<PFN_xrGetVulkanGraphicsRequirementsKHR>(xrInstance, "xrGetVulkanGraphicsRequirementsKHR");
	auto pfnGetInstExt = LoadXRExtFunc<PFN_xrGetVulkanInstanceExtensionsKHR>(xrInstance, "xrGetVulkanInstanceExtensionsKHR");
	if (!pfnGetReq || !pfnGetInstExt)
	{
		lastError = "could not resolve xrGetVulkanGraphicsRequirementsKHR/xrGetVulkanInstanceExtensionsKHR via xrGetInstanceProcAddr";
		LogMessage("OpenXR: " + lastError);
		return {};
	}

	// Spec requires this be called before vkCreateInstance/vkCreateDevice.
	// We don't otherwise use the reported min/max API version (SurrealGPU's
	// own VulkanInstanceBuilder picks the Vulkan API version), but the call
	// itself is mandatory per XR_KHR_vulkan_enable.
	XrGraphicsRequirementsVulkanKHR reqs = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
	XrResult result = pfnGetReq(xrInstance, (XrSystemId)systemId, &reqs);
	LogMessage("OpenXR: xrGetVulkanGraphicsRequirementsKHR result=" + std::to_string((int)result) +
		" minApiVersionSupported=" + std::to_string(XR_VERSION_MAJOR(reqs.minApiVersionSupported)) + "." + std::to_string(XR_VERSION_MINOR(reqs.minApiVersionSupported)) +
		" maxApiVersionSupported=" + std::to_string(XR_VERSION_MAJOR(reqs.maxApiVersionSupported)) + "." + std::to_string(XR_VERSION_MINOR(reqs.maxApiVersionSupported)));
	if (XR_FAILED(result))
	{
		lastError = "xrGetVulkanGraphicsRequirementsKHR failed (result=" + std::to_string((int)result) + ")";
		return {};
	}

	uint32_t count = 0;
	result = pfnGetInstExt(xrInstance, (XrSystemId)systemId, 0, &count, nullptr);
	if (XR_FAILED(result) || count == 0)
	{
		lastError = "xrGetVulkanInstanceExtensionsKHR (size query) failed (result=" + std::to_string((int)result) + ")";
		LogMessage("OpenXR: " + lastError);
		return {};
	}
	std::string buffer(count, '\0');
	result = pfnGetInstExt(xrInstance, (XrSystemId)systemId, count, &count, buffer.data());
	if (XR_FAILED(result))
	{
		lastError = "xrGetVulkanInstanceExtensionsKHR failed (result=" + std::to_string((int)result) + ")";
		LogMessage("OpenXR: " + lastError);
		return {};
	}
	while (!buffer.empty() && buffer.back() == '\0')
		buffer.pop_back();
	auto list = SplitSpaceSeparated(buffer);
	LogMessage("OpenXR: xrGetVulkanInstanceExtensionsKHR -> \"" + buffer + "\" (" + std::to_string(list.size()) + " extensions)");
	return list;
}

bool VulkanXRSession::ResolveVulkanDevice(void* vkInstance, void** outPhysicalDevice, std::vector<std::string>& outDeviceExtensions)
{
	if (!available)
		return false;
	XrInstance xrInstance = (XrInstance)instance;

	auto pfnGetDevExt = LoadXRExtFunc<PFN_xrGetVulkanDeviceExtensionsKHR>(xrInstance, "xrGetVulkanDeviceExtensionsKHR");
	auto pfnGetGfxDevice = LoadXRExtFunc<PFN_xrGetVulkanGraphicsDeviceKHR>(xrInstance, "xrGetVulkanGraphicsDeviceKHR");
	if (!pfnGetDevExt || !pfnGetGfxDevice)
	{
		lastError = "could not resolve xrGetVulkanDeviceExtensionsKHR/xrGetVulkanGraphicsDeviceKHR via xrGetInstanceProcAddr";
		LogMessage("OpenXR: " + lastError);
		return false;
	}

	VkPhysicalDevice physDevice = VK_NULL_HANDLE;
	XrResult result = pfnGetGfxDevice(xrInstance, (XrSystemId)systemId, (VkInstance)vkInstance, &physDevice);
	LogMessage("OpenXR: xrGetVulkanGraphicsDeviceKHR result=" + std::to_string((int)result));
	if (XR_FAILED(result) || physDevice == VK_NULL_HANDLE)
	{
		lastError = "xrGetVulkanGraphicsDeviceKHR failed (result=" + std::to_string((int)result) + ")";
		return false;
	}
	*outPhysicalDevice = (void*)physDevice;

	uint32_t count = 0;
	result = pfnGetDevExt(xrInstance, (XrSystemId)systemId, 0, &count, nullptr);
	if (XR_FAILED(result))
	{
		lastError = "xrGetVulkanDeviceExtensionsKHR (size query) failed (result=" + std::to_string((int)result) + ")";
		LogMessage("OpenXR: " + lastError);
		return false;
	}
	std::string buffer(count, '\0');
	if (count > 0)
	{
		result = pfnGetDevExt(xrInstance, (XrSystemId)systemId, count, &count, buffer.data());
		if (XR_FAILED(result))
		{
			lastError = "xrGetVulkanDeviceExtensionsKHR failed (result=" + std::to_string((int)result) + ")";
			LogMessage("OpenXR: " + lastError);
			return false;
		}
	}
	while (!buffer.empty() && buffer.back() == '\0')
		buffer.pop_back();
	outDeviceExtensions = SplitSpaceSeparated(buffer);
	LogMessage("OpenXR: xrGetVulkanDeviceExtensionsKHR -> \"" + buffer + "\" (" + std::to_string(outDeviceExtensions.size()) + " extensions)");
	return true;
}

bool VulkanXRSession::CreateSession(void* vkInstance, void* vkPhysicalDevice, void* vkDevice, uint32_t queueFamilyIndex, uint32_t queueIndex)
{
	if (!available)
	{
		lastError = "no OpenXR instance/system available";
		return false;
	}
	XrInstance xrInstance = (XrInstance)instance;

	XrGraphicsBindingVulkanKHR binding = { XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR };
	binding.instance = (VkInstance)vkInstance;
	binding.physicalDevice = (VkPhysicalDevice)vkPhysicalDevice;
	binding.device = (VkDevice)vkDevice;
	binding.queueFamilyIndex = queueFamilyIndex;
	binding.queueIndex = queueIndex;

	XrSessionCreateInfo createInfo = { XR_TYPE_SESSION_CREATE_INFO };
	createInfo.next = &binding;
	createInfo.systemId = (XrSystemId)systemId;

	XrSession xrSession = XR_NULL_HANDLE;
	XrResult result = xrCreateSession(xrInstance, &createInfo, &xrSession);
	LogMessage("OpenXR: xrCreateSession result=" + std::to_string((int)result));
	if (XR_FAILED(result))
	{
		lastError = "xrCreateSession failed (result=" + std::to_string((int)result) + ")";
		return false;
	}
	session = (void*)xrSession;

	XrReferenceSpaceCreateInfo spaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	spaceInfo.poseInReferenceSpace.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
	spaceInfo.poseInReferenceSpace.position = { 0.0f, 0.0f, 0.0f };
	XrSpace space = XR_NULL_HANDLE;
	result = xrCreateReferenceSpace(xrSession, &spaceInfo, &space);
	LogMessage("OpenXR: xrCreateReferenceSpace(LOCAL) result=" + std::to_string((int)result));
	if (XR_FAILED(result))
	{
		lastError = "xrCreateReferenceSpace failed (result=" + std::to_string((int)result) + ")";
		xrDestroySession(xrSession);
		session = nullptr;
		return false;
	}
	appSpace = (void*)space;

	sessionRunning = false;
	lastLoggedState = (int)XR_SESSION_STATE_UNKNOWN;
	return true;
}

void VulkanXRSession::DestroySession()
{
	// M-A: action spaces are independent objects (not owned by the action
	// set), so they must be destroyed explicitly - unlike the actions
	// themselves, which xrDestroyActionSet below takes care of.
	if (leftGripSpace) { xrDestroySpace((XrSpace)leftGripSpace); leftGripSpace = nullptr; }
	if (rightGripSpace) { xrDestroySpace((XrSpace)rightGripSpace); rightGripSpace = nullptr; }
	if (leftAimSpace) { xrDestroySpace((XrSpace)leftAimSpace); leftAimSpace = nullptr; }
	if (rightAimSpace) { xrDestroySpace((XrSpace)rightAimSpace); rightAimSpace = nullptr; }

	if (actionSet)
	{
		// Destroying the action set also destroys every XrAction created
		// from it, per spec - no need to individually destroy each action.
		xrDestroyActionSet((XrActionSet)actionSet);
		actionSet = nullptr;
	}
	actionsReady = false;
	leftStickAction = rightStickAction = nullptr;
	leftTriggerAction = rightTriggerAction = nullptr;
	leftGripAction = rightGripAction = nullptr;
	leftXAction = leftYAction = rightAAction = rightBAction = nullptr;
	leftMenuAction = leftStickClickAction = rightStickClickAction = nullptr;
	leftGripPoseAction = rightGripPoseAction = leftAimPoseAction = rightAimPoseAction = nullptr;

	if (appSpace)
	{
		xrDestroySpace((XrSpace)appSpace);
		appSpace = nullptr;
	}
	if (session)
	{
		xrDestroySession((XrSession)session);
		session = nullptr;
	}
	sessionRunning = false;
}

bool VulkanXRSession::PollEvents()
{
	if (!instance)
		return false;
	XrInstance xrInstance = (XrInstance)instance;
	XrSession xrSession = (XrSession)session;

	while (true)
	{
		XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };
		XrResult result = xrPollEvent(xrInstance, &event);
		if (result == XR_EVENT_UNAVAILABLE)
			break;
		if (XR_FAILED(result))
		{
			LogMessage("OpenXR: xrPollEvent failed (result=" + std::to_string((int)result) + ")");
			break;
		}

		if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
		{
			const auto& stateEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
			lastLoggedState = (int)stateEvent.state;
			LogMessage(std::string("OpenXR: session state -> ") + SessionStateName(stateEvent.state));

			if (stateEvent.state == XR_SESSION_STATE_READY && xrSession)
			{
				XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
				beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				XrResult beginResult = xrBeginSession(xrSession, &beginInfo);
				LogMessage("OpenXR: xrBeginSession result=" + std::to_string((int)beginResult));
				sessionRunning = XR_SUCCEEDED(beginResult);
			}
			else if (stateEvent.state == XR_SESSION_STATE_STOPPING && xrSession)
			{
				XrResult endResult = xrEndSession(xrSession);
				LogMessage("OpenXR: xrEndSession result=" + std::to_string((int)endResult));
				sessionRunning = false;
			}
			else if (stateEvent.state == XR_SESSION_STATE_EXITING || stateEvent.state == XR_SESSION_STATE_LOSS_PENDING)
			{
				sessionRunning = false;
				return false;
			}
		}
		else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
		{
			LogMessage("OpenXR: instance loss pending");
			sessionRunning = false;
			return false;
		}
		else if (event.type == XR_TYPE_EVENT_DATA_EVENTS_LOST)
		{
			const auto& lostEvent = *reinterpret_cast<const XrEventDataEventsLost*>(&event);
			LogMessage("OpenXR: " + std::to_string(lostEvent.lostEventCount) + " events lost");
		}
	}
	return true;
}

bool VulkanXRSession::CreateSwapchains()
{
	if (!session)
	{
		lastError = "no session";
		return false;
	}
	XrInstance xrInstance = (XrInstance)instance;
	XrSession xrSession = (XrSession)session;

	uint32_t viewCount = 0;
	XrResult result = xrEnumerateViewConfigurationViews(xrInstance, (XrSystemId)systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
	LogMessage("OpenXR: xrEnumerateViewConfigurationViews (count) result=" + std::to_string((int)result) + " viewCount=" + std::to_string(viewCount));
	if (XR_FAILED(result) || viewCount < 2)
	{
		lastError = "xrEnumerateViewConfigurationViews did not report a stereo (>=2 view) configuration";
		return false;
	}
	std::vector<XrViewConfigurationView> views(viewCount, XrViewConfigurationView{ XR_TYPE_VIEW_CONFIGURATION_VIEW });
	result = xrEnumerateViewConfigurationViews(xrInstance, (XrSystemId)systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, views.data());
	if (XR_FAILED(result))
	{
		lastError = "xrEnumerateViewConfigurationViews failed (result=" + std::to_string((int)result) + ")";
		return false;
	}

	swapchainWidth = (int)views[0].recommendedImageRectWidth;
	swapchainHeight = (int)views[0].recommendedImageRectHeight;
	LogMessage("OpenXR: recommended per-eye swapchain size " + std::to_string(swapchainWidth) + "x" + std::to_string(swapchainHeight));

	uint32_t formatCount = 0;
	result = xrEnumerateSwapchainFormats(xrSession, 0, &formatCount, nullptr);
	if (XR_FAILED(result) || formatCount == 0)
	{
		lastError = "xrEnumerateSwapchainFormats failed (result=" + std::to_string((int)result) + ")";
		return false;
	}
	std::vector<int64_t> formats(formatCount);
	xrEnumerateSwapchainFormats(xrSession, formatCount, &formatCount, formats.data());

	// Prefer a standard 8-bit UNORM colour format compatible with a plain
	// vkCmdBlitImage from the engine's already gamma/brightness-corrected
	// present image (VulkanRenderDevice::Unlock); fall back to whatever the
	// runtime lists first if none of these are offered.
	static const VkFormat preferred[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB };
	int64_t chosenFormat = formats[0];
	for (VkFormat pref : preferred)
	{
		auto it = std::find(formats.begin(), formats.end(), (int64_t)pref);
		if (it != formats.end())
		{
			chosenFormat = (int64_t)pref;
			break;
		}
	}
	LogMessage("OpenXR: chosen swapchain format=" + std::to_string(chosenFormat) + " (of " + std::to_string(formatCount) + " reported)");

	for (int eye = 0; eye < 2; eye++)
	{
		XrSwapchainCreateInfo swapchainInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
		swapchainInfo.format = chosenFormat;
		swapchainInfo.sampleCount = 1;
		swapchainInfo.width = (uint32_t)swapchainWidth;
		swapchainInfo.height = (uint32_t)swapchainHeight;
		swapchainInfo.faceCount = 1;
		swapchainInfo.arraySize = 1;
		swapchainInfo.mipCount = 1;

		XrSwapchain sc = XR_NULL_HANDLE;
		result = xrCreateSwapchain(xrSession, &swapchainInfo, &sc);
		LogMessage("OpenXR: xrCreateSwapchain eye=" + std::to_string(eye) + " result=" + std::to_string((int)result));
		if (XR_FAILED(result))
		{
			lastError = "xrCreateSwapchain failed (result=" + std::to_string((int)result) + ")";
			DestroySwapchains();
			return false;
		}
		swapchain[eye] = (void*)sc;

		uint32_t imageCount = 0;
		xrEnumerateSwapchainImages(sc, 0, &imageCount, nullptr);
		std::vector<XrSwapchainImageVulkanKHR> images(imageCount, XrSwapchainImageVulkanKHR{ XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });
		result = xrEnumerateSwapchainImages(sc, imageCount, &imageCount, (XrSwapchainImageBaseHeader*)images.data());
		LogMessage("OpenXR: xrEnumerateSwapchainImages eye=" + std::to_string(eye) + " result=" + std::to_string((int)result) + " count=" + std::to_string(imageCount));
		if (XR_FAILED(result))
		{
			lastError = "xrEnumerateSwapchainImages failed (result=" + std::to_string((int)result) + ")";
			DestroySwapchains();
			return false;
		}
		swapchainImages[eye].clear();
		for (auto& img : images)
			swapchainImages[eye].push_back((void*)img.image);
	}

	return true;
}

void VulkanXRSession::DestroySwapchains()
{
	for (int eye = 0; eye < 2; eye++)
	{
		if (swapchain[eye])
		{
			xrDestroySwapchain((XrSwapchain)swapchain[eye]);
			swapchain[eye] = nullptr;
		}
		swapchainImages[eye].clear();
	}
}

bool VulkanXRSession::WaitAndBeginFrame(bool& outShouldRender)
{
	outShouldRender = false;
	if (!session)
		return false;
	XrSession xrSession = (XrSession)session;

	XrFrameWaitInfo waitInfo = { XR_TYPE_FRAME_WAIT_INFO };
	XrFrameState frameState = { XR_TYPE_FRAME_STATE };
	XrResult result = xrWaitFrame(xrSession, &waitInfo, &frameState);
	if (XR_FAILED(result))
	{
		LogMessage("OpenXR: xrWaitFrame failed (result=" + std::to_string((int)result) + ")");
		return false;
	}
	lastPredictedDisplayTime = (double)frameState.predictedDisplayTime;
	predictedDisplayPeriod = frameState.predictedDisplayPeriod;
	outShouldRender = frameState.shouldRender != XR_FALSE;

	XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
	result = xrBeginFrame(xrSession, &beginInfo);
	if (XR_FAILED(result))
	{
		LogMessage("OpenXR: xrBeginFrame failed (result=" + std::to_string((int)result) + ")");
		return false;
	}
	return true;
}

bool VulkanXRSession::LocateViews(VREyePose outEyes[2])
{
	if (!session || !appSpace)
		return false;
	XrSession xrSession = (XrSession)session;

	XrViewLocateInfo locateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locateInfo.displayTime = (XrTime)lastPredictedDisplayTime;
	locateInfo.space = (XrSpace)appSpace;

	XrViewState viewState = { XR_TYPE_VIEW_STATE };
	XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
	uint32_t viewCount = 0;
	XrResult result = xrLocateViews(xrSession, &locateInfo, &viewState, 2, &viewCount, views);
	if (XR_FAILED(result) || viewCount < 2)
	{
		LogMessage("OpenXR: xrLocateViews failed (result=" + std::to_string((int)result) + ", viewCount=" + std::to_string(viewCount) + ")");
		return false;
	}

	for (int eye = 0; eye < 2; eye++)
	{
		outEyes[eye].posX = views[eye].pose.position.x;
		outEyes[eye].posY = views[eye].pose.position.y;
		outEyes[eye].posZ = views[eye].pose.position.z;
		outEyes[eye].qx = views[eye].pose.orientation.x;
		outEyes[eye].qy = views[eye].pose.orientation.y;
		outEyes[eye].qz = views[eye].pose.orientation.z;
		outEyes[eye].qw = views[eye].pose.orientation.w;
		outEyes[eye].angleLeft = views[eye].fov.angleLeft;
		outEyes[eye].angleRight = views[eye].fov.angleRight;
		outEyes[eye].angleUp = views[eye].fov.angleUp;
		outEyes[eye].angleDown = views[eye].fov.angleDown;
	}

	// One-shot diagnostic (2026-07-20 rendering investigation): log the real
	// per-eye FOV in degrees, eye positions, tracked IPD and swapchain size.
	// This is what decides whether the DrawSceneVR() vertical-frustum flip
	// (angleUp vs |angleDown|) is actually the world-geometry-warp bug, or
	// masked on this runtime - see Docs/VR/FABLE_ANALYSIS_2026-07-20.md.
	if (!loggedFirstLocateViews)
	{
		loggedFirstLocateViews = true;
		auto deg = [](float rad) { return rad * (180.0f / 3.14159265359f); };
		float dx = outEyes[1].posX - outEyes[0].posX;
		float dy = outEyes[1].posY - outEyes[0].posY;
		float dz = outEyes[1].posZ - outEyes[0].posZ;
		float ipd = std::sqrt(dx * dx + dy * dy + dz * dz);
		for (int eye = 0; eye < 2; eye++)
		{
			LogMessage("OpenXR diag: eye=" + std::to_string(eye) +
				" angleL=" + std::to_string(deg(outEyes[eye].angleLeft)) +
				" angleR=" + std::to_string(deg(outEyes[eye].angleRight)) +
				" angleU=" + std::to_string(deg(outEyes[eye].angleUp)) +
				" angleD=" + std::to_string(deg(outEyes[eye].angleDown)) +
				" (|angleD|-angleU=" + std::to_string(std::fabs(deg(outEyes[eye].angleDown)) - deg(outEyes[eye].angleUp)) + " deg)" +
				" pos=(" + std::to_string(outEyes[eye].posX) + "," + std::to_string(outEyes[eye].posY) + "," + std::to_string(outEyes[eye].posZ) + ")");
		}
		LogMessage("OpenXR diag: tracked IPD=" + std::to_string(ipd) + "m, swapchain=" + std::to_string(swapchainWidth) + "x" + std::to_string(swapchainHeight) + " per eye");
	}

	return true;
}

void* VulkanXRSession::AcquireSwapchainImage(int eye)
{
	if (eye < 0 || eye > 1 || !swapchain[eye])
		return nullptr;
	XrSwapchain sc = (XrSwapchain)swapchain[eye];

	XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	uint32_t index = 0;
	XrResult result = xrAcquireSwapchainImage(sc, &acquireInfo, &index);
	if (XR_FAILED(result))
	{
		LogMessage("OpenXR: xrAcquireSwapchainImage eye=" + std::to_string(eye) + " failed (result=" + std::to_string((int)result) + ")");
		return nullptr;
	}
	acquiredIndex[eye] = index;

	XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	waitInfo.timeout = XR_INFINITE_DURATION;
	result = xrWaitSwapchainImage(sc, &waitInfo);
	if (XR_FAILED(result))
	{
		LogMessage("OpenXR: xrWaitSwapchainImage eye=" + std::to_string(eye) + " failed (result=" + std::to_string((int)result) + ")");
		return nullptr;
	}

	if (index >= swapchainImages[eye].size())
		return nullptr;
	return swapchainImages[eye][index];
}

void VulkanXRSession::ReleaseSwapchainImage(int eye)
{
	if (eye < 0 || eye > 1 || !swapchain[eye])
		return;
	XrSwapchain sc = (XrSwapchain)swapchain[eye];
	XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
	XrResult result = xrReleaseSwapchainImage(sc, &releaseInfo);
	if (XR_FAILED(result))
		LogMessage("OpenXR: xrReleaseSwapchainImage eye=" + std::to_string(eye) + " failed (result=" + std::to_string((int)result) + ")");
}

// Composes the projection layer from the SAME poses/fovs xrLocateViews
// reported this frame (per spec, the layer's per-view pose/fov should match
// what was used to render - see LocateViews's doc comment in the header for
// why the *rendered image content* doesn't yet actually vary with these
// poses; the layer submission below is still spec-correct metadata either
// way, since the compositor uses it to reproject/timewarp regardless of
// what produced the pixels).
void VulkanXRSession::EndFrame(bool submitLayer, const VREyePose eyes[2])
{
	if (!session)
		return;
	XrSession xrSession = (XrSession)session;

	XrCompositionLayerProjectionView projViews[2] = {};
	XrCompositionLayerProjection layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	const XrCompositionLayerBaseHeader* layers[1] = {};
	uint32_t layerCount = 0;

	if (submitLayer)
	{
		for (int eye = 0; eye < 2; eye++)
		{
			projViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			projViews[eye].pose.position = { eyes[eye].posX, eyes[eye].posY, eyes[eye].posZ };
			projViews[eye].pose.orientation = { eyes[eye].qx, eyes[eye].qy, eyes[eye].qz, eyes[eye].qw };
			projViews[eye].fov = { eyes[eye].angleLeft, eyes[eye].angleRight, eyes[eye].angleUp, eyes[eye].angleDown };
			projViews[eye].subImage.swapchain = (XrSwapchain)swapchain[eye];
			projViews[eye].subImage.imageRect.offset = { 0, 0 };
			projViews[eye].subImage.imageRect.extent = { swapchainWidth, swapchainHeight };
			projViews[eye].subImage.imageArrayIndex = 0;
		}

		layer.space = (XrSpace)appSpace;
		layer.viewCount = 2;
		layer.views = projViews;
		layers[0] = (const XrCompositionLayerBaseHeader*)&layer;
		layerCount = 1;
	}

	XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
	endInfo.displayTime = (XrTime)lastPredictedDisplayTime;
	endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endInfo.layerCount = layerCount;
	endInfo.layers = layerCount ? layers : nullptr;

	XrResult result = xrEndFrame(xrSession, &endInfo);
	if (XR_FAILED(result))
		LogMessage("OpenXR: xrEndFrame failed (result=" + std::to_string((int)result) + ")");
}

bool VulkanXRSession::CreateActions()
{
	if (!available || !session)
	{
		lastError = "no OpenXR instance/session available";
		return false;
	}
	XrInstance xrInstance = (XrInstance)instance;
	XrSession xrSession = (XrSession)session;

	XrActionSetCreateInfo setInfo = { XR_TYPE_ACTION_SET_CREATE_INFO };
	strncpy_s(setInfo.actionSetName, "gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
	strncpy_s(setInfo.localizedActionSetName, "Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
	setInfo.priority = 0;
	XrActionSet xrActionSet = XR_NULL_HANDLE;
	XrResult result = xrCreateActionSet(xrInstance, &setInfo, &xrActionSet);
	LogMessage("OpenXR: xrCreateActionSet result=" + std::to_string((int)result));
	if (XR_FAILED(result))
	{
		lastError = "xrCreateActionSet failed (result=" + std::to_string((int)result) + ")";
		return false;
	}
	actionSet = (void*)xrActionSet;

	auto makeAction = [&](XrActionType type, const char* name, const char* localized) -> XrAction
	{
		XrActionCreateInfo info = { XR_TYPE_ACTION_CREATE_INFO };
		strncpy_s(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
		strncpy_s(info.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
		info.actionType = type;
		XrAction action = XR_NULL_HANDLE;
		XrResult r = xrCreateAction(xrActionSet, &info, &action);
		if (XR_FAILED(r))
			LogMessage(std::string("OpenXR: xrCreateAction(") + name + ") failed (result=" + std::to_string((int)r) + ")");
		return action;
	};

	XrAction leftStick = makeAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "left_stick", "Left Thumbstick");
	XrAction rightStick = makeAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "right_stick", "Right Thumbstick");
	XrAction leftTrigger = makeAction(XR_ACTION_TYPE_FLOAT_INPUT, "left_trigger", "Left Trigger");
	XrAction rightTrigger = makeAction(XR_ACTION_TYPE_FLOAT_INPUT, "right_trigger", "Right Trigger");
	XrAction leftGrip = makeAction(XR_ACTION_TYPE_FLOAT_INPUT, "left_grip", "Left Grip");
	XrAction rightGrip = makeAction(XR_ACTION_TYPE_FLOAT_INPUT, "right_grip", "Right Grip");
	XrAction leftX = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "left_x", "X Button");
	XrAction leftY = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "left_y", "Y Button");
	XrAction rightA = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "right_a", "A Button");
	XrAction rightB = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "right_b", "B Button");
	XrAction leftMenu = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "left_menu", "Menu Button");
	XrAction leftStickClick = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "left_stick_click", "Left Stick Click");
	XrAction rightStickClick = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "right_stick_click", "Right Stick Click");

	// M-A: per-hand grip pose (fist orientation, for holding/viewmodel
	// anchoring) and aim pose (pointing ray) - OpenXR defines these as two
	// separate poses per hand, reported with different orientations by
	// Touch controllers (see Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's
	// "Muzzle vs grip offset" section). Must be created here, before
	// xrAttachSessionActionSets below - attach is once-per-session, so
	// pose actions can't be bolted on afterward.
	XrAction leftGripPose = makeAction(XR_ACTION_TYPE_POSE_INPUT, "left_grip_pose", "Left Grip Pose");
	XrAction rightGripPose = makeAction(XR_ACTION_TYPE_POSE_INPUT, "right_grip_pose", "Right Grip Pose");
	XrAction leftAimPose = makeAction(XR_ACTION_TYPE_POSE_INPUT, "left_aim_pose", "Left Aim Pose");
	XrAction rightAimPose = makeAction(XR_ACTION_TYPE_POSE_INPUT, "right_aim_pose", "Right Aim Pose");

	leftStickAction = (void*)leftStick;
	rightStickAction = (void*)rightStick;
	leftTriggerAction = (void*)leftTrigger;
	rightTriggerAction = (void*)rightTrigger;
	leftGripAction = (void*)leftGrip;
	rightGripAction = (void*)rightGrip;
	leftXAction = (void*)leftX;
	leftYAction = (void*)leftY;
	rightAAction = (void*)rightA;
	rightBAction = (void*)rightB;
	leftMenuAction = (void*)leftMenu;
	leftStickClickAction = (void*)leftStickClick;
	rightStickClickAction = (void*)rightStickClick;
	leftGripPoseAction = (void*)leftGripPose;
	rightGripPoseAction = (void*)rightGripPose;
	leftAimPoseAction = (void*)leftAimPose;
	rightAimPoseAction = (void*)rightAimPose;

	auto suggest = [&](const char* profilePath, std::vector<std::pair<XrAction, const char*>> bindings)
	{
		XrPath profile = XR_NULL_PATH;
		if (XR_FAILED(xrStringToPath(xrInstance, profilePath, &profile)))
			return;
		std::vector<XrActionSuggestedBinding> suggested;
		suggested.reserve(bindings.size());
		for (auto& b : bindings)
		{
			if (!b.first)
				continue;
			XrPath p = XR_NULL_PATH;
			if (XR_SUCCEEDED(xrStringToPath(xrInstance, b.second, &p)))
				suggested.push_back({ b.first, p });
		}
		XrInteractionProfileSuggestedBinding sb = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
		sb.interactionProfile = profile;
		sb.countSuggestedBindings = (uint32_t)suggested.size();
		sb.suggestedBindings = suggested.data();
		XrResult r = xrSuggestInteractionProfileBindings(xrInstance, &sb);
		LogMessage(std::string("OpenXR: suggest bindings ") + profilePath + " result=" + std::to_string((int)r) + " (count=" + std::to_string(suggested.size()) + ")");
	};

	// Meta/Oculus Touch controllers (Quest 2/3/Pro) - the primary target.
	// Touch has no physical trigger "click", only the analog /value used
	// here (and /touch, unused) - "trigger pressed" is a threshold the
	// caller applies to the float value.
	suggest("/interaction_profiles/oculus/touch_controller", {
		{ leftStick, "/user/hand/left/input/thumbstick" },
		{ rightStick, "/user/hand/right/input/thumbstick" },
		{ leftTrigger, "/user/hand/left/input/trigger/value" },
		{ rightTrigger, "/user/hand/right/input/trigger/value" },
		{ leftGrip, "/user/hand/left/input/squeeze/value" },
		{ rightGrip, "/user/hand/right/input/squeeze/value" },
		{ leftX, "/user/hand/left/input/x/click" },
		{ leftY, "/user/hand/left/input/y/click" },
		{ rightA, "/user/hand/right/input/a/click" },
		{ rightB, "/user/hand/right/input/b/click" },
		{ leftMenu, "/user/hand/left/input/menu/click" },
		{ leftStickClick, "/user/hand/left/input/thumbstick/click" },
		{ rightStickClick, "/user/hand/right/input/thumbstick/click" },
		{ leftGripPose, "/user/hand/left/input/grip/pose" },
		{ rightGripPose, "/user/hand/right/input/grip/pose" },
		{ leftAimPose, "/user/hand/left/input/aim/pose" },
		{ rightAimPose, "/user/hand/right/input/aim/pose" },
	});

	// khr/simple_controller fallback, for any runtime that doesn't
	// advertise a Touch profile - only a single trigger + menu click per
	// hand exist there, so every other button action stays unbound (reads
	// as its zero default via GetControllerState()). simple_controller
	// DOES define grip/pose and aim/pose for both hands per spec, though,
	// so the M-A pose actions still bind here.
	suggest("/interaction_profiles/khr/simple_controller", {
		{ leftTrigger, "/user/hand/left/input/select/click" },
		{ rightTrigger, "/user/hand/right/input/select/click" },
		{ leftMenu, "/user/hand/left/input/menu/click" },
		{ leftGripPose, "/user/hand/left/input/grip/pose" },
		{ rightGripPose, "/user/hand/right/input/grip/pose" },
		{ leftAimPose, "/user/hand/left/input/aim/pose" },
		{ rightAimPose, "/user/hand/right/input/aim/pose" },
	});

	XrSessionActionSetsAttachInfo attachInfo = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &xrActionSet;
	result = xrAttachSessionActionSets(xrSession, &attachInfo);
	LogMessage("OpenXR: xrAttachSessionActionSets result=" + std::to_string((int)result));
	if (XR_FAILED(result))
	{
		lastError = "xrAttachSessionActionSets failed (result=" + std::to_string((int)result) + ")";
		return false;
	}

	actionsReady = true;

	// M-A: action spaces for the four pose actions, created now that the
	// action set is attached (xrCreateActionSpace requires an attached
	// action). Identity pose-in-action-space (no offset) - xrLocateSpace
	// against the app space below then reports exactly the grip/aim pose
	// the runtime tracks. Non-fatal per pose if creation fails (logged,
	// left null) - LocateHandPoses() just reports that specific pose as
	// invalid rather than failing the whole session/action setup, matching
	// this codebase's existing "degrade gracefully" pattern for controller
	// input (see VRControllerState::actionsActive's doc comment).
	auto makeActionSpace = [&](XrAction action, const char* label) -> void*
	{
		if (!action)
			return nullptr;
		XrActionSpaceCreateInfo spaceInfo = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
		spaceInfo.action = action;
		spaceInfo.subactionPath = XR_NULL_PATH;
		spaceInfo.poseInActionSpace.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
		spaceInfo.poseInActionSpace.position = { 0.0f, 0.0f, 0.0f };
		XrSpace space = XR_NULL_HANDLE;
		XrResult r = xrCreateActionSpace(xrSession, &spaceInfo, &space);
		LogMessage(std::string("OpenXR: xrCreateActionSpace(") + label + ") result=" + std::to_string((int)r));
		if (XR_FAILED(r))
			return nullptr;
		return (void*)space;
	};

	leftGripSpace = makeActionSpace(leftGripPose, "left_grip_pose");
	rightGripSpace = makeActionSpace(rightGripPose, "right_grip_pose");
	leftAimSpace = makeActionSpace(leftAimPose, "left_aim_pose");
	rightAimSpace = makeActionSpace(rightAimPose, "right_aim_pose");

	return true;
}

void VulkanXRSession::SyncActions()
{
	if (!actionsReady || !session)
		return;
	XrSession xrSession = (XrSession)session;

	XrActiveActionSet activeSet = {};
	activeSet.actionSet = (XrActionSet)actionSet;
	activeSet.subactionPath = XR_NULL_PATH;

	XrActionsSyncInfo syncInfo = { XR_TYPE_ACTIONS_SYNC_INFO };
	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeSet;
	lastSyncResult = (int)xrSyncActions(xrSession, &syncInfo);
}

void VulkanXRSession::GetControllerState(VRControllerState& outState)
{
	outState = VRControllerState();
	if (!actionsReady || !session)
		return;
	XrSession xrSession = (XrSession)session;

	bool anyActive = false;

	auto getFloat = [&](void* action) -> float
	{
		if (!action)
			return 0.0f;
		XrActionStateFloat state = { XR_TYPE_ACTION_STATE_FLOAT };
		XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
		info.action = (XrAction)action;
		if (XR_SUCCEEDED(xrGetActionStateFloat(xrSession, &info, &state)) && state.isActive)
		{
			anyActive = true;
			return state.currentState;
		}
		return 0.0f;
	};
	auto getBool = [&](void* action) -> bool
	{
		if (!action)
			return false;
		XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };
		XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
		info.action = (XrAction)action;
		if (XR_SUCCEEDED(xrGetActionStateBoolean(xrSession, &info, &state)) && state.isActive)
		{
			anyActive = true;
			return state.currentState != XR_FALSE;
		}
		return false;
	};
	auto getVec2 = [&](void* action, float& x, float& y)
	{
		x = 0.0f;
		y = 0.0f;
		if (!action)
			return;
		XrActionStateVector2f state = { XR_TYPE_ACTION_STATE_VECTOR2F };
		XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
		info.action = (XrAction)action;
		if (XR_SUCCEEDED(xrGetActionStateVector2f(xrSession, &info, &state)) && state.isActive)
		{
			anyActive = true;
			x = state.currentState.x;
			y = state.currentState.y;
		}
	};

	getVec2(leftStickAction, outState.leftStickX, outState.leftStickY);
	getVec2(rightStickAction, outState.rightStickX, outState.rightStickY);
	outState.leftTrigger = getFloat(leftTriggerAction);
	outState.rightTrigger = getFloat(rightTriggerAction);
	outState.leftGrip = getFloat(leftGripAction);
	outState.rightGrip = getFloat(rightGripAction);
	outState.leftX = getBool(leftXAction);
	outState.leftY = getBool(leftYAction);
	outState.rightA = getBool(rightAAction);
	outState.rightB = getBool(rightBAction);
	outState.leftMenu = getBool(leftMenuAction);
	outState.leftStickClick = getBool(leftStickClickAction);
	outState.rightStickClick = getBool(rightStickClickAction);
	outState.actionsActive = anyActive;
}

bool VulkanXRSession::LocateHandPoses(VRHandPose outGrip[2], VRHandPose outAim[2])
{
	outGrip[0] = VRHandPose();
	outGrip[1] = VRHandPose();
	outAim[0] = VRHandPose();
	outAim[1] = VRHandPose();
	if (!actionsReady || !session || !appSpace)
		return false;
	XrSpace xrAppSpace = (XrSpace)appSpace;

	// xrLocateSpace's locationFlags only carry POSITION_VALID/
	// ORIENTATION_VALID when the action is currently active AND tracked
	// (per spec, an inactive action's space is never located) - same
	// "silently reads as not-tracked, no error" shape as every other
	// action in GetControllerState() above, so `valid` is the only signal
	// callers get and every caller must check it.
	auto locate = [&](void* space, VRHandPose& out)
	{
		if (!space)
			return;
		XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
		XrResult r = xrLocateSpace((XrSpace)space, xrAppSpace, (XrTime)lastPredictedDisplayTime, &loc);
		if (XR_FAILED(r))
			return;
		constexpr XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
		if ((loc.locationFlags & need) != need)
			return;
		out.valid = true;
		out.posX = loc.pose.position.x;
		out.posY = loc.pose.position.y;
		out.posZ = loc.pose.position.z;
		out.qx = loc.pose.orientation.x;
		out.qy = loc.pose.orientation.y;
		out.qz = loc.pose.orientation.z;
		out.qw = loc.pose.orientation.w;
	};

	locate(leftGripSpace, outGrip[0]);
	locate(rightGripSpace, outGrip[1]);
	locate(leftAimSpace, outAim[0]);
	locate(rightAimSpace, outAim[1]);
	return true;
}
