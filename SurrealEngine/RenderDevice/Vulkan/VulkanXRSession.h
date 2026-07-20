#pragma once

// OpenXR session/swapchain/frame-loop wrapper (M2 steps 2/4/8/9 of
// VR_IMPLEMENTATION_PLAN.md). Deliberately kept separate from
// VulkanRenderDevice.cpp so flatscreen play is completely unaffected when
// --vr isn't passed - this header is included by Engine.cpp (which must NOT
// drag in <vulkan/vulkan.h> or <openxr/openxr.h>), so every function here
// uses opaque void* handles / plain PODs instead of real Vulkan/OpenXR
// types. The .cpp has the real types and does the real work.

#include <string>
#include <vector>
#include <cstdint>

// A single eye's tracked pose + field of view, as reported by xrLocateViews.
// Position is in meters, in the session's reference space (LOCAL). The
// quaternion and position are in OpenXR's convention (right-handed, +Y up,
// +X right, -Z forward) - NOT remapped into UE1's axis convention. Currently
// used for logging/diagnostics only - see VulkanXRSession.cpp's LocateViews
// doc comment for why it is not yet wired into the rendered view.
struct VREyePose
{
	float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
	float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
	float angleLeft = 0.0f, angleRight = 0.0f, angleUp = 0.0f, angleDown = 0.0f; // radians
};

class VulkanXRSession
{
public:
	VulkanXRSession();
	~VulkanXRSession();

	// True once xrCreateInstance + xrGetSystem both succeeded. Does NOT
	// imply a session exists yet - see HasSession()/CreateSession().
	bool IsAvailable() const { return available; }
	const std::string& LastError() const { return lastError; }

	// ---- M2 step 4: Vulkan instance/device requirement resolution ----

	// Must be called BEFORE vkCreateInstance (spec requirement). Returns the
	// runtime's required Vulkan instance extensions, already split on
	// spaces, ready to feed into VulkanInstanceBuilder::RequireExtensions().
	// Empty on failure (check LastError()).
	std::vector<std::string> GetVulkanInstanceExtensions();

	// Must be called AFTER VkInstance exists (spec requirement - the exact
	// physical device is only resolvable given a live instance). Fills
	// outPhysicalDevice (a VkPhysicalDevice cast to void*) and
	// outDeviceExtensions (split on spaces). Returns false on failure.
	bool ResolveVulkanDevice(void* vkInstance, void** outPhysicalDevice, std::vector<std::string>& outDeviceExtensions);

	// ---- M2 step 2: session lifecycle ----

	// Creates the XrSession bound to the live Vulkan device via
	// XrGraphicsBindingVulkanKHR, and a LOCAL reference space. Logs every
	// XrResult. Returns false on failure (check LastError()).
	bool CreateSession(void* vkInstance, void* vkPhysicalDevice, void* vkDevice, uint32_t queueFamilyIndex, uint32_t queueIndex);
	void DestroySession();
	bool HasSession() const { return session != nullptr; }

	// Pumps xrPollEvent, logs every XrEventDataSessionStateChanged
	// transition, and calls xrBeginSession/xrEndSession at the appropriate
	// transitions per the OpenXR state machine. Returns false once the
	// session has reached XR_SESSION_STATE_EXITING/LOSS_PENDING (caller
	// should stop the XR frame loop, though the flatscreen game loop can
	// keep running).
	bool PollEvents();

	// True once xrBeginSession has been called (session ready for
	// xrWaitFrame/xrBeginFrame/xrEndFrame per spec).
	bool IsSessionRunning() const { return sessionRunning; }

	int GetLastSessionState() const { return lastLoggedState; } // raw XrSessionState value, for the caller's own logging

	// ---- M2 step 8: swapchains + frame loop ----

	// Queries the recommended per-eye swapchain extent and creates one
	// swapchain per eye (stereo view configuration). Must be called after
	// CreateSession(). Returns false on failure.
	bool CreateSwapchains();
	void DestroySwapchains();
	int GetSwapchainWidth() const { return swapchainWidth; }
	int GetSwapchainHeight() const { return swapchainHeight; }

	// xrWaitFrame + xrBeginFrame. Returns false on a hard failure (caller
	// should stop the XR loop). outShouldRender reflects XrFrameState's
	// shouldRender - xrEndFrame must still be called (with no layers) even
	// when false, to keep the frame loop balanced.
	bool WaitAndBeginFrame(bool& outShouldRender);

	// xrLocateViews for the current frame. Returns false on failure.
	bool LocateViews(VREyePose outEyes[2]);

	// xrAcquireSwapchainImage + xrWaitSwapchainImage for the given eye
	// (0=left, 1=right). Returns the VkImage (cast to void*), or nullptr on
	// failure.
	void* AcquireSwapchainImage(int eye);

	// xrReleaseSwapchainImage for the given eye.
	void ReleaseSwapchainImage(int eye);

	// xrEndFrame with a single projection layer covering both eyes (or no
	// layers if submitLayer is false - still required to keep the frame
	// loop balanced).
	void EndFrame(bool submitLayer, const VREyePose eyes[2]);

private:
	bool available = false;
	std::string lastError;
	void* instance = nullptr; // XrInstance
	uint64_t systemId = 0; // XrSystemId

	void* session = nullptr; // XrSession
	void* appSpace = nullptr; // XrSpace
	bool sessionRunning = false;
	int lastLoggedState = 0; // XR_SESSION_STATE_UNKNOWN

	int swapchainWidth = 0;
	int swapchainHeight = 0;
	void* swapchain[2] = { nullptr, nullptr }; // XrSwapchain, one per eye
	std::vector<void*> swapchainImages[2]; // VkImage per swapchain image, one vector per eye
	uint32_t acquiredIndex[2] = { 0, 0 };

	double lastPredictedDisplayTime = 0.0;
	int64_t predictedDisplayPeriod = 0;
};
