#pragma once

#include "Platform/OpenXR/OpenXRUIRuntime.h"
#include "Platform/OpenXR/OpenXRView.h"
#include "RenderDevice/Vulkan/VulkanGraphicsBinding.h"
#include "XR/XRCommon.h"

#include <memory>
#include <string>

class RenderDevice;

// Optional native OpenXR provider. Its public surface contains no OpenXR or
// Vulkan declarations, so the engine can compile and run without the SDK.
class OpenXRProvider : public VulkanGraphicsBinding, public IXRHapticSink
	, public OpenXRUICompositionSink
{
public:
	static constexpr uint32_t ProjectionTargetSlot = 1;
	static constexpr std::array<PresentationTarget, 2> UIVisualTargets = {
		PresentationTarget{ 6 }, PresentationTarget{ 7 } };

	OpenXRProvider();
	~OpenXRProvider() override;

	bool IsAvailable() const;
	bool IsSessionReady() const;
	bool IsSessionRunning() const;
	bool SupportsUIVisualOverlay() const;
	const XRSessionState& SessionState() const;
	const std::string& LastError() const;

	std::vector<std::string> GetVulkanInstanceExtensions() override;
	bool ResolveVulkanDevice(void* instance, void** physicalDevice, std::vector<std::string>& deviceExtensions) override;
	bool OnVulkanDeviceCreated(void* instance, void* physicalDevice, void* device, uint32_t queueFamilyIndex, uint32_t queueIndex) override;

	bool PollEvents();
	bool WaitBeginAndLocate(bool& shouldRender, OpenXREyeView eyes[2], XRSpaceSamples& spaces);
	bool SyncInput(XRSpaceSamples& spaces, XRControllerSnapshot& controllers);
	bool SubmitHaptic(const XRHapticRequest& request) override;
	void SetUIRenderDevice(RenderDevice* renderDevice);
	bool AllocateSurfaceTargets(
		const std::array<XRUICanvasCaptureDescriptor, 4>& descriptors) override;
	bool BeginSurfaceFrame(const XRUICanvasReplayFrame& frame,
		const std::array<XRUIPointerFeedback, XRHandCount>& feedback,
		const OpenXRUICompositionSpace& compositionSpace) override;
	bool EndSurfaceFrame(bool rendered) override;
	void ReleaseSurfaceTargets() override;
	void* AcquireSwapchainImage(int eye);
	void ReleaseSwapchainImage(int eye);
	void EndFrame(bool submitLayer, const OpenXREyeView eyes[2]);
	int SwapchainWidth() const;
	int SwapchainHeight() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
