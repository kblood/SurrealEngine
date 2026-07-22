#pragma once

#include "Platform/OpenXR/OpenXRView.h"
#include "RenderDevice/Vulkan/VulkanGraphicsBinding.h"

#include <memory>
#include <string>

struct OpenXRPoseSnapshot
{
	bool Valid = false;
	vec3 PositionMeters = vec3(0.0f);
	float OrientationX = 0.0f;
	float OrientationY = 0.0f;
	float OrientationZ = 0.0f;
	float OrientationW = 1.0f;
};

// Provider-neutral semantic controller state. Index 0 is the left user path
// and index 1 is the right user path; Oculus-specific X/A naming is confined
// to the suggested binding table in the provider implementation.
struct OpenXRControllerSnapshot
{
	bool Connected = false;
	bool ActionsActive = false;
	float StickX = 0.0f;
	float StickY = 0.0f;
	float Trigger = 0.0f;
	float Grip = 0.0f;
	bool PrimaryButton = false;
	bool SecondaryButton = false;
	bool MenuButton = false;
	bool StickClick = false;
	OpenXRPoseSnapshot GripPose;
	OpenXRPoseSnapshot AimPose;
};

struct OpenXRInputSnapshot
{
	OpenXRControllerSnapshot Controllers[2];
};

// Optional native OpenXR provider. Its public surface contains no OpenXR or
// Vulkan declarations, so the engine can compile and run without the SDK.
class OpenXRProvider : public VulkanGraphicsBinding
{
public:
	static constexpr uint32_t ProjectionTargetSlot = 1;

	OpenXRProvider();
	~OpenXRProvider() override;

	bool IsAvailable() const;
	bool IsSessionReady() const;
	bool IsSessionRunning() const;
	const std::string& LastError() const;

	std::vector<std::string> GetVulkanInstanceExtensions() override;
	bool ResolveVulkanDevice(void* instance, void** physicalDevice, std::vector<std::string>& deviceExtensions) override;
	bool OnVulkanDeviceCreated(void* instance, void* physicalDevice, void* device, uint32_t queueFamilyIndex, uint32_t queueIndex) override;

	bool PollEvents();
	bool WaitBeginAndLocate(bool& shouldRender, OpenXREyeView eyes[2]);
	bool SyncInput(OpenXRInputSnapshot& snapshot);
	void* AcquireSwapchainImage(int eye);
	void ReleaseSwapchainImage(int eye);
	void EndFrame(bool submitLayer, const OpenXREyeView eyes[2]);
	int SwapchainWidth() const;
	int SwapchainHeight() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
