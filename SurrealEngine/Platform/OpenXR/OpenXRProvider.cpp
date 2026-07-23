#include "Precomp.h"
#include "Platform/OpenXR/OpenXRProvider.h"
#include "Platform/OpenXR/OpenXRExtensions.h"
#include "RenderDevice/RenderDevice.h"
#include "Utils/Logger.h"
#include "XR/XRInputDiagnostics.h"

#include <limits>

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
	bool frameBegun = false;
	XRSessionState sessionState;
	XRInputDiagnosticsAccumulator inputDiagnostics;
	std::string lastError;
	int width = 0;
	int height = 0;
	RenderDevice* uiRenderDevice = nullptr;

#if defined(SURREAL_ENABLE_OPENXR)
	XrInstance instance = XR_NULL_HANDLE;
	XrSystemId system = XR_NULL_SYSTEM_ID;
	XrSession session = XR_NULL_HANDLE;
	XrSpace space = XR_NULL_HANDLE;
	XrSpace viewSpace = XR_NULL_HANDLE;
	XrSwapchain swapchains[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
	std::vector<XrSwapchainImageVulkanKHR> images[2];
	XrPath handPaths[2] = { XR_NULL_PATH, XR_NULL_PATH };
	XrActionSet actionSet = XR_NULL_HANDLE;
	XrAction stickAction = XR_NULL_HANDLE;
	XrAction triggerAction = XR_NULL_HANDLE;
	XrAction gripAction = XR_NULL_HANDLE;
	XrAction primaryButtonAction = XR_NULL_HANDLE;
	XrAction secondaryButtonAction = XR_NULL_HANDLE;
	XrAction menuButtonAction = XR_NULL_HANDLE;
	XrAction stickClickAction = XR_NULL_HANDLE;
	XrAction gripPoseAction = XR_NULL_HANDLE;
	XrAction aimPoseAction = XR_NULL_HANDLE;
	XrAction hapticAction = XR_NULL_HANDLE;
	XrSpace gripSpaces[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
	XrSpace aimSpaces[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
	bool actionsReady = false;
	uint32_t inputSyncFailureLogs = 0;
	uint32_t profileQueryFailureLogs[2] = { 0, 0 };
	uint32_t actionQueryFailureLogs[2] = { 0, 0 };
	bool acquired[2] = { false, false };
	uint32_t acquiredIndex[2] = { 0, 0 };
	XrTime predictedDisplayTime = 0;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	int64_t colorFormat = 0;
	uint32_t maxLayerCount = 0;
	struct UISwapchain
	{
		XRUICanvasCaptureDescriptor Descriptor;
		XrSwapchain Handle = XR_NULL_HANDLE;
		std::vector<XrSwapchainImageVulkanKHR> Images;
		bool Acquired = false;
		bool Bound = false;
		uint32_t ImageIndex = 0;
	};
	std::array<UISwapchain, 4> uiSwapchains;
	bool uiTargetsAllocated = false;
	bool uiFrameActive = false;
	std::array<XrCompositionLayerQuad, 4> uiQuads;
	uint32_t uiQuadCount = 0;
#endif
};

#if defined(SURREAL_ENABLE_OPENXR)
namespace
{
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

	bool LocatePose(XrSpace inputSpace, XrSpace baseSpace, XrTime time, XRPose& output)
	{
		output = {};
		if (!inputSpace || !baseSpace || !time)
			return false;
		XrSpaceLocation location{ XR_TYPE_SPACE_LOCATION };
		if (XR_FAILED(xrLocateSpace(inputSpace, baseSpace, time, &location)))
			return false;
		constexpr XrSpaceLocationFlags required = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
		if ((location.locationFlags & required) != required)
			return false;
		output.Valid = true;
		output.Position = { location.pose.position.x, location.pose.position.y, location.pose.position.z };
		output.Orientation = { location.pose.orientation.x, location.pose.orientation.y, location.pose.orientation.z, location.pose.orientation.w };
		return true;
	}

	const char* LifecycleName(XRSessionLifecycle lifecycle)
	{
		switch (lifecycle)
		{
		case XRSessionLifecycle::Inactive: return "inactive";
		case XRSessionLifecycle::Starting: return "starting";
		case XRSessionLifecycle::Running: return "running";
		case XRSessionLifecycle::Stopping: return "stopping";
		case XRSessionLifecycle::Lost: return "lost";
		}
		return "unknown";
	}

	const char* FocusName(XRSessionFocus focus)
	{
		switch (focus)
		{
		case XRSessionFocus::Unavailable: return "unavailable";
		case XRSessionFocus::Visible: return "visible";
		case XRSessionFocus::Focused: return "focused";
		}
		return "unknown";
	}

	std::string AvailabilityNames(uint32_t mask, bool buttons)
	{
		std::string names;
		auto append = [&](const char* name)
		{
			if (!names.empty())
				names += ',';
			names += name;
		};
		if (buttons)
		{
			if (mask & XRInputButtonSelect) append("select");
			if (mask & XRInputButtonSqueeze) append("squeeze");
			if (mask & XRInputButtonPrimary) append("primary");
			if (mask & XRInputButtonSecondary) append("secondary");
			if (mask & XRInputButtonThumbstickClick) append("stick-click");
			if (mask & XRInputButtonMenu) append("menu");
		}
		else if (mask & XRInputAxisThumbstick)
		{
			append("thumbstick");
		}
		return names.empty() ? "none" : names;
	}

	void LogInputDiagnosticEvents(const std::vector<XRInputDiagnosticEvent>& events)
	{
		for (const XRInputDiagnosticEvent& event : events)
		{
			if (event.Type == XRInputDiagnosticEventType::SessionState)
			{
				std::string message = "[openxr-input] session lifecycle=";
				message += LifecycleName(event.Session.Lifecycle);
				message += " focus=";
				message += FocusName(event.Session.Focus);
				if (!event.Initial)
				{
					message += " previous_lifecycle=";
					message += LifecycleName(event.PreviousSession.Lifecycle);
					message += " previous_focus=";
					message += FocusName(event.PreviousSession.Focus);
				}
				LogMessage(message);
			}
			else if (event.Type == XRInputDiagnosticEventType::HandAvailability)
			{
				std::string message = "[openxr-input] hand=";
				message += event.Hand == XRHand::Left ? "left" : "right";
				message += event.Connected ? " connected=yes" : " connected=no";
				message += event.Availability.ProfileOrBindingAvailable ?
					" profile_or_binding=yes" : " profile_or_binding=no";
				message += " buttons=" + AvailabilityNames(event.Availability.ButtonMask, true);
				message += " axes=" + AvailabilityNames(event.Availability.AxisMask, false);
				LogMessage(message);
			}
			else
			{
				std::string message = "[openxr-input] hand=";
				message += event.Hand == XRHand::Left ? "left" : "right";
				message += " activity trigger_edges=" + std::to_string(event.Counters.PrimaryTriggerPressEdges);
				message += " menu_back_edges=" + std::to_string(event.Counters.MenuBackPressEdges);
				message += " nonzero_stick_samples=" + std::to_string(event.Counters.NonzeroThumbstickSamples);
				LogMessage(message);
			}
		}
	}

	void LogProviderInputFailure(const char* stage, uint32_t& emitted)
	{
		constexpr uint32_t MaxProviderFailureEvents = 4;
		if (emitted >= MaxProviderFailureEvents)
			return;
		emitted++;
		LogMessage(std::string("[openxr-input] provider_failure stage=") + stage +
			" occurrence=" + std::to_string(emitted));
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
	ReleaseSurfaceTargets();
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
		if (impl->gripSpaces[eye])
			xrDestroySpace(impl->gripSpaces[eye]);
		if (impl->aimSpaces[eye])
			xrDestroySpace(impl->aimSpaces[eye]);
	}
	if (impl->viewSpace)
		xrDestroySpace(impl->viewSpace);
	if (impl->space)
		xrDestroySpace(impl->space);
	if (impl->session)
		xrDestroySession(impl->session);
	if (impl->actionSet)
		xrDestroyActionSet(impl->actionSet);
	if (impl->instance)
		xrDestroyInstance(impl->instance);
#endif
}

bool OpenXRProvider::IsAvailable() const { return impl->available; }
bool OpenXRProvider::IsSessionReady() const { return impl->sessionReady; }
bool OpenXRProvider::IsSessionRunning() const { return impl->sessionState.IsRunning(); }
const XRSessionState& OpenXRProvider::SessionState() const { return impl->sessionState; }
const std::string& OpenXRProvider::LastError() const { return impl->lastError; }
int OpenXRProvider::SwapchainWidth() const { return impl->width; }
int OpenXRProvider::SwapchainHeight() const { return impl->height; }

void OpenXRProvider::SetUIRenderDevice(RenderDevice* renderDevice)
{
	impl->uiRenderDevice = renderDevice;
}

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
	uint32_t capacity = 0;
	result = getExtensions(impl->instance, impl->system, 0, &capacity, nullptr);
	if (XR_FAILED(result) || capacity == 0)
	{
		impl->lastError = ResultMessage("xrGetVulkanInstanceExtensionsKHR", result);
		return {};
	}
	std::string value(capacity, '\0');
	uint32_t returnedSize = capacity;
	result = getExtensions(impl->instance, impl->system, capacity, &returnedSize, value.data());
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrGetVulkanInstanceExtensionsKHR", result);
		return {};
	}
	return ParseOpenXRExtensionList(value.data(), std::min<size_t>(returnedSize, value.size()));
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
	uint32_t capacity = 0;
	result = getExtensions(impl->instance, impl->system, 0, &capacity, nullptr);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrGetVulkanDeviceExtensionsKHR", result);
		return false;
	}
	std::string value(capacity, '\0');
	uint32_t returnedSize = capacity;
	if (capacity > 0)
		result = getExtensions(impl->instance, impl->system, capacity, &returnedSize, value.data());
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrGetVulkanDeviceExtensionsKHR", result);
		return false;
	}
	*physicalDevice = (void*)device;
	deviceExtensions = ParseOpenXRExtensionList(value.data(), std::min<size_t>(returnedSize, value.size()));
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
	impl->physicalDevice = binding.physicalDevice;
	XrSessionCreateInfo sessionInfo{ XR_TYPE_SESSION_CREATE_INFO };
	sessionInfo.next = &binding;
	sessionInfo.systemId = impl->system;
	XrResult result = xrCreateSession(impl->instance, &sessionInfo, &impl->session);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrCreateSession", result);
		return false;
	}
	XrSystemProperties systemProperties{ XR_TYPE_SYSTEM_PROPERTIES };
	result = xrGetSystemProperties(impl->instance, impl->system, &systemProperties);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrGetSystemProperties", result);
		return false;
	}
	impl->maxLayerCount = systemProperties.graphicsProperties.maxLayerCount;
	XrReferenceSpaceCreateInfo spaceInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
	result = xrCreateReferenceSpace(impl->session, &spaceInfo, &impl->space);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrCreateReferenceSpace", result);
		return false;
	}
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	result = xrCreateReferenceSpace(impl->session, &spaceInfo, &impl->viewSpace);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrCreateReferenceSpace(VIEW)", result);
		return false;
	}

	// One semantic action is shared by the left/right subaction paths. This
	// keeps hand policy out of the provider and gives the adapter two fully
	// independent InputSourceId contributors.
	xrStringToPath(impl->instance, "/user/hand/left", &impl->handPaths[0]);
	xrStringToPath(impl->instance, "/user/hand/right", &impl->handPaths[1]);
	XrActionSetCreateInfo actionSetInfo{ XR_TYPE_ACTION_SET_CREATE_INFO };
	std::strncpy(actionSetInfo.actionSetName, "surreal_input", XR_MAX_ACTION_SET_NAME_SIZE - 1);
	std::strncpy(actionSetInfo.localizedActionSetName, "Surreal Engine Input", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
	result = xrCreateActionSet(impl->instance, &actionSetInfo, &impl->actionSet);
	if (XR_SUCCEEDED(result))
	{
		auto makeAction = [&](XrActionType type, const char* name, const char* localized) -> XrAction
		{
			XrActionCreateInfo info{ XR_TYPE_ACTION_CREATE_INFO };
			info.actionType = type;
			std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
			std::strncpy(info.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
			info.countSubactionPaths = 2;
			info.subactionPaths = impl->handPaths;
			XrAction action = XR_NULL_HANDLE;
			XrResult createResult = xrCreateAction(impl->actionSet, &info, &action);
			if (XR_FAILED(createResult))
				LogMessage(ResultMessage((std::string("xrCreateAction(") + name + ")").c_str(), createResult));
			return action;
		};

		impl->stickAction = makeAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick");
		impl->triggerAction = makeAction(XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger");
		impl->gripAction = makeAction(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Squeeze");
		impl->primaryButtonAction = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "primary_button", "Primary Button");
		impl->secondaryButtonAction = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary_button", "Secondary Button");
		impl->menuButtonAction = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "menu_button", "Menu Button");
		impl->stickClickAction = makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_click", "Thumbstick Click");
		impl->gripPoseAction = makeAction(XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip Pose");
		impl->aimPoseAction = makeAction(XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim Pose");
		impl->hapticAction = makeAction(XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Haptic Output");

		auto suggest = [&](const char* profileName, const std::vector<std::pair<XrAction, const char*>>& bindings)
		{
			XrPath profile = XR_NULL_PATH;
			if (XR_FAILED(xrStringToPath(impl->instance, profileName, &profile)))
				return;
			std::vector<XrActionSuggestedBinding> suggested;
			for (const auto& binding : bindings)
			{
				XrPath path = XR_NULL_PATH;
				if (binding.first && XR_SUCCEEDED(xrStringToPath(impl->instance, binding.second, &path)))
					suggested.push_back({ binding.first, path });
			}
			XrInteractionProfileSuggestedBinding info{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
			info.interactionProfile = profile;
			info.countSuggestedBindings = (uint32_t)suggested.size();
			info.suggestedBindings = suggested.data();
			XrResult suggestResult = xrSuggestInteractionProfileBindings(impl->instance, &info);
			if (XR_FAILED(suggestResult))
				LogMessage(ResultMessage("xrSuggestInteractionProfileBindings", suggestResult));
		};

		suggest("/interaction_profiles/oculus/touch_controller", {
			{ impl->stickAction, "/user/hand/left/input/thumbstick" },
			{ impl->stickAction, "/user/hand/right/input/thumbstick" },
			{ impl->triggerAction, "/user/hand/left/input/trigger/value" },
			{ impl->triggerAction, "/user/hand/right/input/trigger/value" },
			{ impl->gripAction, "/user/hand/left/input/squeeze/value" },
			{ impl->gripAction, "/user/hand/right/input/squeeze/value" },
			{ impl->primaryButtonAction, "/user/hand/left/input/x/click" },
			{ impl->primaryButtonAction, "/user/hand/right/input/a/click" },
			{ impl->secondaryButtonAction, "/user/hand/left/input/y/click" },
			{ impl->secondaryButtonAction, "/user/hand/right/input/b/click" },
			{ impl->menuButtonAction, "/user/hand/left/input/menu/click" },
			{ impl->stickClickAction, "/user/hand/left/input/thumbstick/click" },
			{ impl->stickClickAction, "/user/hand/right/input/thumbstick/click" },
			{ impl->gripPoseAction, "/user/hand/left/input/grip/pose" },
			{ impl->gripPoseAction, "/user/hand/right/input/grip/pose" },
			{ impl->aimPoseAction, "/user/hand/left/input/aim/pose" },
			{ impl->aimPoseAction, "/user/hand/right/input/aim/pose" },
			{ impl->hapticAction, "/user/hand/left/output/haptic" },
			{ impl->hapticAction, "/user/hand/right/output/haptic" },
		});
		suggest("/interaction_profiles/khr/simple_controller", {
			{ impl->triggerAction, "/user/hand/left/input/select/click" },
			{ impl->triggerAction, "/user/hand/right/input/select/click" },
			{ impl->menuButtonAction, "/user/hand/left/input/menu/click" },
			{ impl->menuButtonAction, "/user/hand/right/input/menu/click" },
			{ impl->gripPoseAction, "/user/hand/left/input/grip/pose" },
			{ impl->gripPoseAction, "/user/hand/right/input/grip/pose" },
			{ impl->aimPoseAction, "/user/hand/left/input/aim/pose" },
			{ impl->aimPoseAction, "/user/hand/right/input/aim/pose" },
			{ impl->hapticAction, "/user/hand/left/output/haptic" },
			{ impl->hapticAction, "/user/hand/right/output/haptic" },
		});

		XrSessionActionSetsAttachInfo attachInfo{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
		attachInfo.countActionSets = 1;
		attachInfo.actionSets = &impl->actionSet;
		result = xrAttachSessionActionSets(impl->session, &attachInfo);
		if (XR_SUCCEEDED(result))
		{
			for (int hand = 0; hand < 2; hand++)
			{
					auto makeSpace = [&](XrAction action, XrSpace& output)
					{
						if (!action)
							return;
					XrActionSpaceCreateInfo info{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
						info.action = action;
						info.subactionPath = impl->handPaths[hand];
						info.poseInActionSpace.orientation.w = 1.0f;
						XrResult spaceResult = xrCreateActionSpace(impl->session, &info, &output);
						if (XR_FAILED(spaceResult))
							LogMessage(ResultMessage("xrCreateActionSpace", spaceResult));
					};
				makeSpace(impl->gripPoseAction, impl->gripSpaces[hand]);
				makeSpace(impl->aimPoseAction, impl->aimSpaces[hand]);
			}
			impl->actionsReady = true;
		}
		else
		{
			LogMessage(ResultMessage("xrAttachSessionActionSets", result) + "; continuing without XR controller input");
		}
	}
	else
	{
		LogMessage(ResultMessage("xrCreateActionSet", result) + "; continuing without XR controller input");
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
	impl->colorFormat = format;
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
			impl->sessionState = { XRSessionLifecycle::Lost, XRSessionFocus::Unavailable };
			LogInputDiagnosticEvents(impl->inputDiagnostics.ObserveSession(impl->sessionState));
			return false;
		}
		if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
		{
			auto& state = *reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
			switch (state.state)
			{
			case XR_SESSION_STATE_IDLE:
				impl->sessionState = { XRSessionLifecycle::Inactive, XRSessionFocus::Unavailable };
				break;
			case XR_SESSION_STATE_READY:
			{
				impl->sessionState = { XRSessionLifecycle::Starting, XRSessionFocus::Unavailable };
				XrSessionBeginInfo beginInfo{ XR_TYPE_SESSION_BEGIN_INFO };
				beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				XrResult beginResult = xrBeginSession(impl->session, &beginInfo);
				if (XR_SUCCEEDED(beginResult))
					impl->sessionState = { XRSessionLifecycle::Running, XRSessionFocus::Unavailable };
				else
				{
					impl->lastError = ResultMessage("xrBeginSession", beginResult);
					impl->sessionState = { XRSessionLifecycle::Lost, XRSessionFocus::Unavailable };
					return false;
				}
				break;
			}
			case XR_SESSION_STATE_SYNCHRONIZED:
				impl->sessionState = { XRSessionLifecycle::Running, XRSessionFocus::Unavailable };
				break;
			case XR_SESSION_STATE_VISIBLE:
				impl->sessionState = { XRSessionLifecycle::Running, XRSessionFocus::Visible };
				break;
			case XR_SESSION_STATE_FOCUSED:
				impl->sessionState = { XRSessionLifecycle::Running, XRSessionFocus::Focused };
				break;
			case XR_SESSION_STATE_STOPPING:
			{
				impl->sessionState = { XRSessionLifecycle::Stopping, XRSessionFocus::Unavailable };
				XrResult endResult = xrEndSession(impl->session);
				if (XR_FAILED(endResult))
					impl->lastError = ResultMessage("xrEndSession", endResult);
				break;
			}
			case XR_SESSION_STATE_EXITING:
			case XR_SESSION_STATE_LOSS_PENDING:
				impl->sessionState = { XRSessionLifecycle::Lost, XRSessionFocus::Unavailable };
				LogInputDiagnosticEvents(impl->inputDiagnostics.ObserveSession(impl->sessionState));
				return false;
			default:
				break;
			}
			LogInputDiagnosticEvents(impl->inputDiagnostics.ObserveSession(impl->sessionState));
		}
		else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
		{
			impl->sessionState = { XRSessionLifecycle::Lost, XRSessionFocus::Unavailable };
			LogInputDiagnosticEvents(impl->inputDiagnostics.ObserveSession(impl->sessionState));
			return false;
		}
	}
#else
	return false;
#endif
}

bool OpenXRProvider::WaitBeginAndLocate(bool& shouldRender, OpenXREyeView eyes[2], XRSpaceSamples& spaces)
{
	shouldRender = false;
	spaces = {};
#if defined(SURREAL_ENABLE_OPENXR)
	if (!impl->sessionState.IsRunning() || impl->frameBegun)
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
	LocatePose(impl->viewSpace, impl->space, impl->predictedDisplayTime, spaces.Head);
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

bool OpenXRProvider::SyncInput(XRSpaceSamples& spaces, XRControllerSnapshot& controllers)
{
	controllers = {};
	spaces.Aim = {};
	spaces.Grip = {};
#if defined(SURREAL_ENABLE_OPENXR)
	if (!impl->sessionState.IsRunning() || !impl->actionsReady)
		return false;

	XrActiveActionSet activeSet{};
	activeSet.actionSet = impl->actionSet;
	XrActionsSyncInfo syncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };
	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeSet;
	XrResult result = xrSyncActions(impl->session, &syncInfo);
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrSyncActions", result);
		LogProviderInputFailure("sync-actions", impl->inputSyncFailureLogs);
		return false;
	}

	std::array<XRInputHandAvailability, XRHandCount> availability;
	for (int hand = 0; hand < 2; hand++)
	{
		const XRHand xrHand = hand == 0 ? XRHand::Left : XRHand::Right;
		XRHandControllerState& controller = controllers.ForHand(xrHand);
		bool anyActionActive = false;
		bool actionQueryFailed = false;
		XrInteractionProfileState profile{ XR_TYPE_INTERACTION_PROFILE_STATE };
		XrResult profileResult = xrGetCurrentInteractionProfile(
			impl->session, impl->handPaths[hand], &profile);
		if (XR_SUCCEEDED(profileResult))
		{
			controller.Connected = profile.interactionProfile != XR_NULL_PATH;
			availability[hand].ProfileOrBindingAvailable = controller.Connected;
		}
		else
		{
			LogProviderInputFailure(hand == 0 ? "interaction-profile-left" : "interaction-profile-right",
				impl->profileQueryFailureLogs[hand]);
		}

		auto getInfo = [&](XrAction action)
		{
			XrActionStateGetInfo info{ XR_TYPE_ACTION_STATE_GET_INFO };
			info.action = action;
			info.subactionPath = impl->handPaths[hand];
			return info;
		};
		auto getFloat = [&](XrAction action, uint32_t availabilityBit)
		{
			if (!action)
				return 0.0f;
			XrActionStateFloat state{ XR_TYPE_ACTION_STATE_FLOAT };
			XrActionStateGetInfo info = getInfo(action);
			XrResult stateResult = xrGetActionStateFloat(impl->session, &info, &state);
			actionQueryFailed |= XR_FAILED(stateResult);
			if (XR_SUCCEEDED(stateResult) && state.isActive)
			{
				anyActionActive = true;
				availability[hand].ButtonMask |= availabilityBit;
				return state.currentState;
			}
			return 0.0f;
		};
		auto getButton = [&](XrAction action, uint32_t availabilityBit)
		{
			if (!action)
				return false;
			XrActionStateBoolean state{ XR_TYPE_ACTION_STATE_BOOLEAN };
			XrActionStateGetInfo info = getInfo(action);
			XrResult stateResult = xrGetActionStateBoolean(impl->session, &info, &state);
			actionQueryFailed |= XR_FAILED(stateResult);
			if (XR_SUCCEEDED(stateResult) && state.isActive)
			{
				anyActionActive = true;
				availability[hand].ButtonMask |= availabilityBit;
				return state.currentState != XR_FALSE;
			}
			return false;
		};
		auto poseActive = [&](XrAction action)
		{
			if (!action)
				return false;
			XrActionStatePose state{ XR_TYPE_ACTION_STATE_POSE };
			XrActionStateGetInfo info = getInfo(action);
			XrResult stateResult = xrGetActionStatePose(impl->session, &info, &state);
			actionQueryFailed |= XR_FAILED(stateResult);
			bool active = XR_SUCCEEDED(stateResult) && state.isActive;
			anyActionActive |= active;
			return active;
		};

		if (impl->stickAction)
		{
			XrActionStateVector2f state{ XR_TYPE_ACTION_STATE_VECTOR2F };
			XrActionStateGetInfo info = getInfo(impl->stickAction);
			XrResult stateResult = xrGetActionStateVector2f(impl->session, &info, &state);
			actionQueryFailed |= XR_FAILED(stateResult);
			if (XR_SUCCEEDED(stateResult) && state.isActive)
			{
				anyActionActive = true;
				availability[hand].AxisMask |= XRInputAxisThumbstick;
				controller.Thumbstick = { state.currentState.x, state.currentState.y };
			}
		}
		controller.Select.Value = getFloat(impl->triggerAction, XRInputButtonSelect);
		controller.Select.Pressed = controller.Select.Value > 0.0f;
		controller.Squeeze.Value = getFloat(impl->gripAction, XRInputButtonSqueeze);
		controller.Squeeze.Pressed = controller.Squeeze.Value > 0.0f;
		controller.Primary.Pressed = getButton(impl->primaryButtonAction, XRInputButtonPrimary);
		controller.Primary.Value = controller.Primary.Pressed ? 1.0f : 0.0f;
		controller.Secondary.Pressed = getButton(impl->secondaryButtonAction, XRInputButtonSecondary);
		controller.Secondary.Value = controller.Secondary.Pressed ? 1.0f : 0.0f;
		controller.Menu.Pressed = getButton(impl->menuButtonAction, XRInputButtonMenu);
		controller.Menu.Value = controller.Menu.Pressed ? 1.0f : 0.0f;
		controller.ThumbstickClick.Pressed = getButton(impl->stickClickAction, XRInputButtonThumbstickClick);
		controller.ThumbstickClick.Value = controller.ThumbstickClick.Pressed ? 1.0f : 0.0f;
		bool gripActive = poseActive(impl->gripPoseAction);
		bool aimActive = poseActive(impl->aimPoseAction);
		controller.Connected |= anyActionActive;
		availability[hand].ProfileOrBindingAvailable |= anyActionActive;

		if (gripActive)
			LocatePose(impl->gripSpaces[hand], impl->space, impl->predictedDisplayTime, spaces.GripFor(xrHand));
		if (aimActive)
			LocatePose(impl->aimSpaces[hand], impl->space, impl->predictedDisplayTime, spaces.AimFor(xrHand));
		if (actionQueryFailed)
			LogProviderInputFailure(hand == 0 ? "action-state-left" : "action-state-right",
				impl->actionQueryFailureLogs[hand]);
	}
	LogInputDiagnosticEvents(impl->inputDiagnostics.ObserveInput(
		impl->sessionState, controllers, availability));
	return true;
#else
	return false;
#endif
}

bool OpenXRProvider::SubmitHaptic(const XRHapticRequest& request)
{
#if defined(SURREAL_ENABLE_OPENXR)
	if (!IsValidXRHapticRequest(request) || !impl->sessionState.AcceptsInput() || !impl->hapticAction)
		return false;

	XrHapticActionInfo actionInfo{ XR_TYPE_HAPTIC_ACTION_INFO };
	actionInfo.action = impl->hapticAction;
	actionInfo.subactionPath = impl->handPaths[XRHandIndex(request.Hand)];

	XrHapticVibration vibration{ XR_TYPE_HAPTIC_VIBRATION };
	const double durationNanoseconds = std::max(1.0, std::min(
		static_cast<double>(request.DurationSeconds) * 1000000000.0,
		static_cast<double>(std::numeric_limits<XrDuration>::max())));
	vibration.duration = static_cast<XrDuration>(durationNanoseconds);
	vibration.frequency = request.FrequencyHz > 0.0f ? request.FrequencyHz : XR_FREQUENCY_UNSPECIFIED;
	vibration.amplitude = request.Amplitude;

	XrResult result = xrApplyHapticFeedback(impl->session, &actionInfo,
		reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
	if (XR_FAILED(result))
	{
		impl->lastError = ResultMessage("xrApplyHapticFeedback", result);
		return false;
	}
	return true;
#else
	(void)request;
	return false;
#endif
}

bool OpenXRProvider::AllocateSurfaceTargets(
	const std::array<XRUICanvasCaptureDescriptor, 4>& descriptors)
{
#if defined(SURREAL_ENABLE_OPENXR)
	ReleaseSurfaceTargets();
	if (!impl->sessionReady || !impl->session || !impl->uiRenderDevice ||
		!impl->physicalDevice || !impl->colorFormat)
	{
		impl->lastError = "OpenXR UI composition requires a ready Vulkan session and render device";
		return false;
	}
	if (impl->maxLayerCount < 5)
	{
		impl->lastError = "OpenXR runtime does not support projection plus four UI layers";
		return false;
	}
	VkFormatProperties properties{};
	vkGetPhysicalDeviceFormatProperties(impl->physicalDevice,
		static_cast<VkFormat>(impl->colorFormat), &properties);
	if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0)
	{
		impl->lastError = "OpenXR swapchain format cannot receive Vulkan UI blits";
		return false;
	}

	for (size_t index = 0; index < descriptors.size(); index++)
	{
		const XRUICanvasCaptureDescriptor& descriptor = descriptors[index];
		if (!descriptor.HasValidCanvas() || descriptor.Target.IsDefault())
		{
			impl->lastError = "OpenXR UI descriptor has an invalid canvas or target slot";
			ReleaseSurfaceTargets();
			return false;
		}
		auto& target = impl->uiSwapchains[index];
		target.Descriptor = descriptor;
		XrSwapchainCreateInfo info{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
		info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
			XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
		info.format = impl->colorFormat;
		info.sampleCount = 1;
		info.width = static_cast<uint32_t>(descriptor.Surface.PixelWidth);
		info.height = static_cast<uint32_t>(descriptor.Surface.PixelHeight);
		info.faceCount = 1;
		info.arraySize = 1;
		info.mipCount = 1;
		XrResult result = xrCreateSwapchain(impl->session, &info, &target.Handle);
		if (XR_FAILED(result))
		{
			impl->lastError = ResultMessage("xrCreateSwapchain(UI)", result);
			ReleaseSurfaceTargets();
			return false;
		}
		uint32_t imageCount = 0;
		result = xrEnumerateSwapchainImages(target.Handle, 0, &imageCount, nullptr);
		if (XR_FAILED(result) || imageCount == 0)
		{
			impl->lastError = ResultMessage("xrEnumerateSwapchainImages(UI)", result);
			ReleaseSurfaceTargets();
			return false;
		}
		target.Images.assign(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });
		result = xrEnumerateSwapchainImages(target.Handle, imageCount, &imageCount,
			reinterpret_cast<XrSwapchainImageBaseHeader*>(target.Images.data()));
		if (XR_FAILED(result))
		{
			impl->lastError = ResultMessage("xrEnumerateSwapchainImages(UI)", result);
			ReleaseSurfaceTargets();
			return false;
		}
	}
	impl->uiTargetsAllocated = true;
	return true;
#else
	(void)descriptors;
	return false;
#endif
}

bool OpenXRProvider::BeginSurfaceFrame(const XRUICanvasReplayFrame& frame,
	const std::array<XRUIPointerFeedback, XRHandCount>& feedback,
	const OpenXRUICompositionSpace& compositionSpace)
{
	(void)feedback;
#if defined(SURREAL_ENABLE_OPENXR)
	if (!impl->uiTargetsAllocated || impl->uiFrameActive || !impl->frameBegun ||
		!impl->sessionState.IsRunning() || !compositionSpace.Valid ||
		frame.Items.size() > impl->uiSwapchains.size())
	{
		impl->lastError = "OpenXR UI frame contract is not ready or valid";
		return false;
	}

	struct PlannedSurface
	{
		Impl::UISwapchain* Target = nullptr;
		const XRUICanvasReplayItem* Item = nullptr;
		OpenXRUIQuadPose Pose;
	};
	std::array<PlannedSurface, 4> planned{};
	std::array<bool, 4> used{};
	size_t plannedCount = 0;
	int lastOrder = std::numeric_limits<int>::min();
	for (const XRUICanvasReplayItem& item : frame.Items)
	{
		if (item.Surface.DepthMode != XRUISurfaceDepthMode::IgnoreWorldDepth ||
			item.Surface.CompositionOrder < lastOrder)
		{
			impl->lastError = "OpenXR UI frame is not ordered or requests scene depth";
			return false;
		}
		lastOrder = item.Surface.CompositionOrder;
		size_t targetIndex = impl->uiSwapchains.size();
		for (size_t index = 0; index < impl->uiSwapchains.size(); index++)
		{
			if (impl->uiSwapchains[index].Descriptor.Surface.Kind ==
				item.Surface.Descriptor.Kind)
			{
				targetIndex = index;
				break;
			}
		}
		if (targetIndex == impl->uiSwapchains.size() || used[targetIndex] ||
			!ConvertXRUISurfacePoseToOpenXRLocal(item.Surface.Pose,
				compositionSpace, planned[plannedCount].Pose))
		{
			impl->lastError = "OpenXR UI frame has a duplicate, unknown, or invalid surface pose";
			return false;
		}
		used[targetIndex] = true;
		planned[plannedCount].Target = &impl->uiSwapchains[targetIndex];
		planned[plannedCount].Item = &item;
		plannedCount++;
	}

	impl->uiQuadCount = 0;
	impl->uiFrameActive = true;
	for (size_t index = 0; index < plannedCount; index++)
	{
		auto& target = *planned[index].Target;
		XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
		XrResult result = xrAcquireSwapchainImage(target.Handle, &acquireInfo,
			&target.ImageIndex);
		if (XR_FAILED(result))
		{
			impl->lastError = ResultMessage("xrAcquireSwapchainImage(UI)", result);
			EndSurfaceFrame(false);
			return false;
		}
		target.Acquired = true;
		XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
		waitInfo.timeout = XR_INFINITE_DURATION;
		result = xrWaitSwapchainImage(target.Handle, &waitInfo);
		if (XR_FAILED(result) || target.ImageIndex >= target.Images.size())
		{
			impl->lastError = ResultMessage("xrWaitSwapchainImage(UI)", result);
			EndSurfaceFrame(false);
			return false;
		}
		PresentationTargetBinding binding;
		binding.Target = target.Descriptor.Target;
		binding.Images.push_back({
			reinterpret_cast<void*>(target.Images[target.ImageIndex].image),
			target.Descriptor.Surface.PixelWidth,
			target.Descriptor.Surface.PixelHeight,
			true });
		if (!impl->uiRenderDevice->BindPresentationTarget(binding))
		{
			impl->lastError = "Vulkan renderer rejected an OpenXR UI target binding";
			EndSurfaceFrame(false);
			return false;
		}
		target.Bound = true;

		const XRUISurfaceFrameItem& surface = planned[index].Item->Surface;
		const OpenXRUIQuadPose& pose = planned[index].Pose;
		XrCompositionLayerQuad& quad = impl->uiQuads[impl->uiQuadCount++];
		quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
		quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
			XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
		quad.space = impl->space;
		quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		quad.subImage.swapchain = target.Handle;
		quad.subImage.imageRect.extent = {
			target.Descriptor.Surface.PixelWidth,
			target.Descriptor.Surface.PixelHeight };
		quad.pose.position = { pose.PositionMeters.x, pose.PositionMeters.y,
			pose.PositionMeters.z };
		quad.pose.orientation = { pose.OrientationX, pose.OrientationY,
			pose.OrientationZ, pose.OrientationW };
		quad.size = {
			surface.Descriptor.PhysicalWidth / compositionSpace.WorldUnitsPerMeter,
			surface.Descriptor.PhysicalHeight() / compositionSpace.WorldUnitsPerMeter };
	}
	return true;
#else
	(void)frame;
	(void)compositionSpace;
	return false;
#endif
}

bool OpenXRProvider::EndSurfaceFrame(bool rendered)
{
#if defined(SURREAL_ENABLE_OPENXR)
	if (!impl->uiFrameActive)
		return false;
	bool completed = true;
	for (auto it = impl->uiSwapchains.rbegin(); it != impl->uiSwapchains.rend(); ++it)
	{
		if (it->Bound && impl->uiRenderDevice)
			impl->uiRenderDevice->UnbindPresentationTarget(it->Descriptor.Target);
		it->Bound = false;
		if (it->Acquired)
		{
			XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
			XrResult result = xrReleaseSwapchainImage(it->Handle, &releaseInfo);
			if (XR_FAILED(result))
			{
				impl->lastError = ResultMessage("xrReleaseSwapchainImage(UI)", result);
				completed = false;
			}
			it->Acquired = false;
		}
	}
	if (!rendered || !completed)
		impl->uiQuadCount = 0;
	impl->uiFrameActive = false;
	return completed;
#else
	(void)rendered;
	return false;
#endif
}

void OpenXRProvider::ReleaseSurfaceTargets()
{
#if defined(SURREAL_ENABLE_OPENXR)
	EndSurfaceFrame(false);
	impl->uiQuadCount = 0;
	for (auto& target : impl->uiSwapchains)
	{
		if (target.Handle)
			xrDestroySwapchain(target.Handle);
		target = {};
	}
	impl->uiTargetsAllocated = false;
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
	if (impl->uiFrameActive)
		EndSurfaceFrame(false);
	XrCompositionLayerProjectionView projectionViews[2] = {};
	XrCompositionLayerProjection projection{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	const XrCompositionLayerBaseHeader* layers[5] = {};
	uint32_t layerCount = 0;
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
		layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
		for (uint32_t index = 0; index < impl->uiQuadCount; index++)
			layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
				&impl->uiQuads[index]);
	}
	XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
	endInfo.displayTime = impl->predictedDisplayTime;
	endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endInfo.layerCount = layerCount;
	endInfo.layers = endInfo.layerCount ? layers : nullptr;
	XrResult result = xrEndFrame(impl->session, &endInfo);
	if (XR_FAILED(result))
		impl->lastError = ResultMessage("xrEndFrame", result);
	impl->frameBegun = false;
	impl->uiQuadCount = 0;
#endif
}
