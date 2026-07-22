#pragma once

#include "Platform/OpenXR/OpenXRView.h"
#include "RenderDevice/Vulkan/VulkanGraphicsBinding.h"
#include "XR/XRCommon.h"

#include <memory>
#include <string>

// Optional native OpenXR provider. Its public surface contains no OpenXR or
// Vulkan declarations, so the engine can compile and run without the SDK.
class OpenXRProvider : public VulkanGraphicsBinding, public IXRHapticSink
{
public:
	static constexpr uint32_t ProjectionTargetSlot = 1;

	OpenXRProvider();
	~OpenXRProvider() override;

	bool IsAvailable() const;
	bool IsSessionReady() const;
	bool IsSessionRunning() const;
	const XRSessionState& SessionState() const;
	const std::string& LastError() const;

	std::vector<std::string> GetVulkanInstanceExtensions() override;
	bool ResolveVulkanDevice(void* instance, void** physicalDevice, std::vector<std::string>& deviceExtensions) override;
	bool OnVulkanDeviceCreated(void* instance, void* physicalDevice, void* device, uint32_t queueFamilyIndex, uint32_t queueIndex) override;

	bool PollEvents();
	bool WaitBeginAndLocate(bool& shouldRender, OpenXREyeView eyes[2], XRSpaceSamples& spaces);
	bool SyncInput(XRSpaceSamples& spaces, XRControllerSnapshot& controllers);
	bool SubmitHaptic(const XRHapticRequest& request) override;
	void* AcquireSwapchainImage(int eye);
	void ReleaseSwapchainImage(int eye);
	void EndFrame(bool submitLayer, const OpenXREyeView eyes[2]);
	int SwapchainWidth() const;
	int SwapchainHeight() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
