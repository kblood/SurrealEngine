#include "Precomp.h"
#include "Platform/OpenXR/OpenXRProvider.h"
#include "Utils/Logger.h"

#include <sstream>

#if defined(SURREAL_ENABLE_OPENXR)
#include <surrealgpu/vulkaninstance.h>
#define XR_USE_GRAPHICS_API_VULKAN
#if defined(WIN32)
#define XR_USE_PLATFORM_WIN32
#include <unknwn.h>
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cstring>
#endif

struct OpenXRProvider::Impl
{
	bool available = false;
	bool sessionReady = false;
	bool sessionRunning = false;
	bool frameBegun = false;
	std::string lastError;
	int width = 0;
	int height = 0;

#if defined(SURREAL_ENABLE_OPENXR)
	XrInstance instance = XR_NULL_HANDLE;
	XrSystemId system = XR_NULL_SYSTEM_ID;
	XrSession session = XR_NULL_HANDLE;
	XrSpace space = XR_NULL_HANDLE;
	XrSwapchain swapchains[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
	std::vector<XrSwapchainImageVulkanKHR> images[2];
	bool acquired[2] = { false, false };
	uint32_t acquiredIndex[2] = { 0, 0 };
	XrTime predictedDisplayTime = 0;
#endif
};

#if defined(SURREAL_ENABLE_OPENXR)
namespace
{
	std::vector<std::string> SplitExtensions(const std::string& value)
	{
		std::vector<std::string> result;
		std::istringstream stream(value);
		for (std::string item; stream >> item; )
			result.push_back(item);
		return result;
	}

	template<typename T>
	T LoadExtension(XrInstance instance, const char* name)
	{
		PFN_xrVoidFunction function = nullptr;
		if (XR_FAILED(xrGetInstanceProcAddr(instance, name, &function)))
			return nullptr;
		return reinterpret_cast<T>(function);
	}

	std::string ResultMessage(const char* operation, XrResult result)
	{
		return std::string(operation) + " failed (result=" + std::to_string((int)result) + ")";
	}
}
#endif

OpenXRProvider::OpenXRProvider() : impl(std::make_unique<Impl>())
{
#if defined(SURREAL_ENABLE_OPENXR)
	uint32_t extensionCount = 0;
	XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrEnumerateInstanceExtensionProperties", result);
		return;
	}

	std::vector<XrExtensionProperties> extensions(extensionCount, { XR_TYPE_EXTENSION_PROPERTIES });
	result = xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data());
	bool hasVulkanBinding = false;
	for (const XrExtensionProperties& extension : extensions)
		hasVulkanBinding |= std::strcmp(extension.extensionName, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME) == 0;
	if (!hasVulkanBinding)
	{
		impl->lastError = "OpenXR runtime does not advertise XR_KHR_vulkan_enable";
		return;
	}

	const char* enabledExtensions[] = { XR_KHR_VULKAN_ENABLE_EXTENSION_NAME };
	XrInstanceCreateInfo createInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
	createInfo.enabledExtensionCount = 1;
	createInfo.enabledExtensionNames = enabledExtensions;
	std::strncpy(createInfo.applicationInfo.applicationName, "SurrealEngine", XR_MAX_APPLICATION_NAME_SIZE - 1);
	std::strncpy(createInfo.applicationInfo.engineName, "SurrealEngine", XR_MAX_ENGINE_NAME_SIZE - 1);
	createInfo.applicationInfo.applicationVersion = 1;
	createInfo.applicationInfo.engineVersion = 1;
	createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	result = xrCreateInstance(&createInfo, &impl->instance);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrCreateInstance", result);
		return;
	}

	XrSystemGetInfo systemInfo{ XR_TYPE_SYSTEM_GET_INFO };
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	result = xrGetSystem(impl->instance, &systemInfo, &impl->system);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrGetSystem", result);
		return;
	}
	impl->available = true;
#else
	impl->lastError = "this build was compiled without SURREAL_ENABLE_OPENXR";
#endif
}

OpenXRProvider::~OpenXRProvider()
{
#if defined(SURREAL_ENABLE_OPENXR)
	if (impl->session && impl->frameBegun)
	{
		for (int eye = 0; eye < 2; eye++)
			ReleaseSwapchainImage(eye);
		EndFrame(false, nullptr);
	}
	for (int eye = 0; eye < 2; eye++)
	{
		if (impl->swapchains[eye])
			xrDestroySwapchain(impl->swapchains[eye]);
	}
	if (impl->space)
		xrDestroySpace(impl->space);
	if (impl->session)
		xrDestroySession(impl->session);
	if (impl->instance)
		xrDestroyInstance(impl->instance);
#endif
}

bool OpenXRProvider::IsAvailable() const { return impl->available; }
bool OpenXRProvider::IsSessionReady() const { return impl->sessionReady; }
bool OpenXRProvider::IsSessionRunning() const { return impl->sessionRunning; }
const std::string& OpenXRProvider::LastError() const { return impl->lastError; }
int OpenXRProvider::SwapchainWidth() const { return impl->width; }
int OpenXRProvider::SwapchainHeight() const { return impl->height; }

std::vector<std::string> OpenXRProvider::GetVulkanInstanceExtensions()
{
#if defined(SURREAL_ENABLE_OPENXR)
	if (!impl->available)
		return {};
	auto requirements = LoadExtension<PFN_xrGetVulkanGraphicsRequirementsKHR>(impl->instance, "xrGetVulkanGraphicsRequirementsKHR");
	auto getExtensions = LoadExtension<PFN_xrGetVulkanInstanceExtensionsKHR>(impl->instance, "xrGetVulkanInstanceExtensionsKHR");
	if (!requirements || !getExtensions)
	{
		impl->lastError = "OpenXR Vulkan instance requirement functions are unavailable";
		return {};
	}
	XrGraphicsRequirementsVulkanKHR graphicsRequirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
	XrResult result = requirements(impl->instance, impl->system, &graphicsRequirements);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrGetVulkanGraphicsRequirementsKHR", result);
		return {};
	}
	uint32_t size = 0;
	result = getExtensions(impl->instance, impl->system, 0, &size, nullptr);
	if (XR_FAILED(result) || size == 0)
	{
		impl->lastError = ResultMessage("xrGetVulkanInstanceExtensionsKHR", result);
		return {};
	}
	std::string value(size, '\0');
	result = getExtensions(impl->instance, impl->system, size, &size, value.data());
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrGetVulkanInstanceExtensionsKHR", result);
		return {};
	}
	return SplitExtensions(value);
#else
	return {};
#endif
}

bool OpenXRProvider::ResolveVulkanDevice(void* instance, void** physicalDevice, std::vector<std::string>& deviceExtensions)
{
	if (physicalDevice)
		*physicalDevice = nullptr;
#if defined(SURREAL_ENABLE_OPENXR)
	if (!impl->available || !instance || !physicalDevice)
		return false;
	auto getDevice = LoadExtension<PFN_xrGetVulkanGraphicsDeviceKHR>(impl->instance, "xrGetVulkanGraphicsDeviceKHR");
	auto getExtensions = LoadExtension<PFN_xrGetVulkanDeviceExtensionsKHR>(impl->instance, "xrGetVulkanDeviceExtensionsKHR");
	if (!getDevice || !getExtensions)
	{
		impl->lastError = "OpenXR Vulkan device requirement functions are unavailable";
		return false;
	}
	VkPhysicalDevice device = VK_NULL_HANDLE;
	XrResult result = getDevice(impl->instance, impl->system, (VkInstance)instance, &device);
	if (XR_FAILED(result) || !device)
	{
		impl->lastError = ResultMessage("xrGetVulkanGraphicsDeviceKHR", result);
		return false;
	}
	uint32_t size = 0;
	result = getExtensions(impl->instance, impl->system, 0, &size, nullptr);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrGetVulkanDeviceExtensionsKHR", result);
		return false;
	}
	std::string value(size, '\0');
	if (size > 0)
		result = getExtensions(impl->instance, impl->system, size, &size, value.data());
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrGetVulkanDeviceExtensionsKHR", result);
		return false;
	}
	*physicalDevice = (void*)device;
	deviceExtensions = SplitExtensions(value);
	return true;
#else
	return false;
#endif
}

bool OpenXRProvider::OnVulkanDeviceCreated(void* instance, void* physicalDevice, void* device, uint32_t queueFamilyIndex, uint32_t queueIndex)
{
#if defined(SURREAL_ENABLE_OPENXR)
	if (!impl->available)
		return false;
	XrGraphicsBindingVulkanKHR binding{ XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR };
	binding.instance = (VkInstance)instance;
	binding.physicalDevice = (VkPhysicalDevice)physicalDevice;
	binding.device = (VkDevice)device;
	binding.queueFamilyIndex = queueFamilyIndex;
	binding.queueIndex = queueIndex;
	XrSessionCreateInfo sessionInfo{ XR_TYPE_SESSION_CREATE_INFO };
	sessionInfo.next = &binding;
	sessionInfo.systemId = impl->system;
	XrResult result = xrCreateSession(impl->instance, &sessionInfo, &impl->session);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrCreateSession", result);
		return false;
	}
	XrReferenceSpaceCreateInfo spaceInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
	result = xrCreateReferenceSpace(impl->session, &spaceInfo, &impl->space);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrCreateReferenceSpace", result);
		return false;
	}

	uint32_t viewCount = 0;
	result = xrEnumerateViewConfigurationViews(impl->instance, impl->system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
	if (XR_FAILED(result) || viewCount < 2)
	{
		impl->lastError = "OpenXR runtime did not provide two stereo views";
		return false;
	}
	std::vector<XrViewConfigurationView> views(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
	result = xrEnumerateViewConfigurationViews(impl->instance, impl->system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, views.data());
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrEnumerateViewConfigurationViews", result);
		return false;
	}
	impl->width = (int)views[0].recommendedImageRectWidth;
	impl->height = (int)views[0].recommendedImageRectHeight;

	uint32_t formatCount = 0;
	result = xrEnumerateSwapchainFormats(impl->session, 0, &formatCount, nullptr);
	std::vector<int64_t> formats(formatCount);
	if (XR_SUCCEEDED(result) && formatCount > 0)
		result = xrEnumerateSwapchainFormats(impl->session, formatCount, &formatCount, formats.data());
	if (XR_FAILED(result) || formats.empty())
	{
		impl->lastError = ResultMessage("xrEnumerateSwapchainFormats", result);
		return false;
	}
	const VkFormat preferred[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB };
	int64_t format = formats[0];
	for (VkFormat candidate : preferred)
	{
		if (std::find(formats.begin(), formats.end(), (int64_t)candidate) != formats.end())
		{
			format = candidate;
			break;
		}
	}
	for (int eye = 0; eye < 2; eye++)
	{
		XrSwapchainCreateInfo swapchainInfo{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
		swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
		swapchainInfo.format = format;
		swapchainInfo.sampleCount = 1;
		swapchainInfo.width = (uint32_t)impl->width;
		swapchainInfo.height = (uint32_t)impl->height;
		swapchainInfo.faceCount = 1;
		swapchainInfo.arraySize = 1;
		swapchainInfo.mipCount = 1;
		result = xrCreateSwapchain(impl->session, &swapchainInfo, &impl->swapchains[eye]);
		if (XR_FAILED(result))
		{
			impl->lastError = ResultMessage("xrCreateSwapchain", result);
			return false;
		}
		uint32_t imageCount = 0;
		result = xrEnumerateSwapchainImages(impl->swapchains[eye], 0, &imageCount, nullptr);
		if (XR_FAILED(result) || imageCount == 0)
		{
			impl->lastError = ResultMessage("xrEnumerateSwapchainImages", result);
			return false;
		}
		impl->images[eye].assign(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });
		result = xrEnumerateSwapchainImages(impl->swapchains[eye], imageCount, &imageCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(impl->images[eye].data()));
		if (XR_FAILED(result))
		{
			impl->lastError = ResultMessage("xrEnumerateSwapchainImages", result);
			return false;
		}
	}
	impl->sessionReady = true;
	return true;
#else
	return false;
#endif
}

bool OpenXRProvider::PollEvents()
{
#if defined(SURREAL_ENABLE_OPENXR)
	if (!impl->instance)
		return false;
	for (;;)
	{
		XrEventDataBuffer event{ XR_TYPE_EVENT_DATA_BUFFER };
		XrResult result = xrPollEvent(impl->instance, &event);
		if (result == XR_EVENT_UNAVAILABLE)
			return true;
		if (XR_FAILED(result))
		{
			impl->lastError = ResultMessage("xrPollEvent", result);
			return false;
		}
		if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
		{
			auto& state = *reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
			if (state.state == XR_SESSION_STATE_READY)
			{
				XrSessionBeginInfo beginInfo{ XR_TYPE_SESSION_BEGIN_INFO };
				beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				XrResult beginResult = xrBeginSession(impl->session, &beginInfo);
				impl->sessionRunning = XR_SUCCEEDED(beginResult);
				if (XR_FAILED(beginResult))
					impl->lastError = ResultMessage("xrBeginSession", beginResult);
			}
			else if (state.state == XR_SESSION_STATE_STOPPING)
			{
				xrEndSession(impl->session);
				impl->sessionRunning = false;
			}
			else if (state.state == XR_SESSION_STATE_EXITING || state.state == XR_SESSION_STATE_LOSS_PENDING)
			{
				impl->sessionRunning = false;
				return false;
			}
		}
		else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
		{
			impl->sessionRunning = false;
			return false;
		}
	}
#else
	return false;
#endif
}

bool OpenXRProvider::WaitBeginAndLocate(bool& shouldRender, OpenXREyeView eyes[2])
{
	shouldRender = false;
#if defined(SURREAL_ENABLE_OPENXR)
	if (!impl->sessionRunning || impl->frameBegun)
		return false;
	XrFrameWaitInfo waitInfo{ XR_TYPE_FRAME_WAIT_INFO };
	XrFrameState frameState{ XR_TYPE_FRAME_STATE };
	XrResult result = xrWaitFrame(impl->session, &waitInfo, &frameState);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrWaitFrame", result);
		return false;
	}
	XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
	result = xrBeginFrame(impl->session, &beginInfo);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrBeginFrame", result);
		return false;
	}
	impl->frameBegun = true;
	impl->predictedDisplayTime = frameState.predictedDisplayTime;
	shouldRender = frameState.shouldRender != XR_FALSE;
	if (!shouldRender)
		return true;

	XrViewLocateInfo locateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locateInfo.displayTime = impl->predictedDisplayTime;
	locateInfo.space = impl->space;
	XrViewState viewState{ XR_TYPE_VIEW_STATE };
	XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
	uint32_t count = 0;
	result = xrLocateViews(impl->session, &locateInfo, &viewState, 2, &count, views);
	if (XR_FAILED(result) || count < 2)
	{
		impl->lastError = ResultMessage("xrLocateViews", result);
		shouldRender = false;
		return true;
	}
	for (int eye = 0; eye < 2; eye++)
	{
		eyes[eye].PositionMeters = { views[eye].pose.position.x, views[eye].pose.position.y, views[eye].pose.position.z };
		eyes[eye].OrientationX = views[eye].pose.orientation.x;
		eyes[eye].OrientationY = views[eye].pose.orientation.y;
		eyes[eye].OrientationZ = views[eye].pose.orientation.z;
		eyes[eye].OrientationW = views[eye].pose.orientation.w;
		eyes[eye].AngleLeft = views[eye].fov.angleLeft;
		eyes[eye].AngleRight = views[eye].fov.angleRight;
		eyes[eye].AngleUp = views[eye].fov.angleUp;
		eyes[eye].AngleDown = views[eye].fov.angleDown;
	}
	return true;
#else
	return false;
#endif
}

void* OpenXRProvider::AcquireSwapchainImage(int eye)
{
#if defined(SURREAL_ENABLE_OPENXR)
	if (eye < 0 || eye > 1 || !impl->frameBegun || !impl->swapchains[eye])
		return nullptr;
	XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	XrResult result = xrAcquireSwapchainImage(impl->swapchains[eye], &acquireInfo, &impl->acquiredIndex[eye]);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrAcquireSwapchainImage", result);
		return nullptr;
	}
	impl->acquired[eye] = true;
	XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	waitInfo.timeout = XR_INFINITE_DURATION;
	result = xrWaitSwapchainImage(impl->swapchains[eye], &waitInfo);
	if (XR_FAILED(result) || impl->acquiredIndex[eye] >= impl->images[eye].size())
	{
		impl->lastError = ResultMessage("xrWaitSwapchainImage", result);
		XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
		xrReleaseSwapchainImage(impl->swapchains[eye], &releaseInfo);
		impl->acquired[eye] = false;
		return nullptr;
	}
	return (void*)impl->images[eye][impl->acquiredIndex[eye]].image;
#else
	return nullptr;
#endif
}

void OpenXRProvider::ReleaseSwapchainImage(int eye)
{
#if defined(SURREAL_ENABLE_OPENXR)
	if (eye < 0 || eye > 1 || !impl->acquired[eye])
		return;
	XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
	XrResult result = xrReleaseSwapchainImage(impl->swapchains[eye], &releaseInfo);
	if (XR_FAILED(result))
		impl->lastError = ResultMessage("xrReleaseSwapchainImage", result);
	impl->acquired[eye] = false;
#endif
}

void OpenXRProvider::EndFrame(bool submitLayer, const OpenXREyeView eyes[2])
{
#if defined(SURREAL_ENABLE_OPENXR)
	if (!impl->frameBegun)
		return;
	XrCompositionLayerProjectionView projectionViews[2] = {};
	XrCompositionLayerProjection projection{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	const XrCompositionLayerBaseHeader* layers[] = { reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection) };
	if (submitLayer && eyes)
	{
		for (int eye = 0; eye < 2; eye++)
		{
			projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			projectionViews[eye].pose.position = { eyes[eye].PositionMeters.x, eyes[eye].PositionMeters.y, eyes[eye].PositionMeters.z };
			projectionViews[eye].pose.orientation = { eyes[eye].OrientationX, eyes[eye].OrientationY, eyes[eye].OrientationZ, eyes[eye].OrientationW };
			projectionViews[eye].fov = { eyes[eye].AngleLeft, eyes[eye].AngleRight, eyes[eye].AngleUp, eyes[eye].AngleDown };
			projectionViews[eye].subImage.swapchain = impl->swapchains[eye];
			projectionViews[eye].subImage.imageRect.extent = { impl->width, impl->height };
		}
		projection.space = impl->space;
		projection.viewCount = 2;
		projection.views = projectionViews;
	}
	XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
	endInfo.displayTime = impl->predictedDisplayTime;
	endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endInfo.layerCount = submitLayer && eyes ? 1u : 0u;
	endInfo.layers = endInfo.layerCount ? layers : nullptr;
	XrResult result = xrEndFrame(impl->session, &endInfo);
	if (XR_FAILED(result))
		impl->lastError = ResultMessage("xrEndFrame", result);
	impl->frameBegun = false;
#endif
}
