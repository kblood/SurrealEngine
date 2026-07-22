#pragma once

#include "Render/Presentation.h"

#include <map>

// Tracks provider-owned single-image targets independently from the stereo
// projection target. Vulkan resource creation remains in VulkanRenderDevice;
// this class only validates and retains the opaque per-frame bindings.
class VulkanPresentationTargets
{
public:
	bool Bind(const PresentationTargetBinding& binding);
	void Unbind(PresentationTarget target);
	const PresentationTargetImage* Find(PresentationTarget target) const;
	void Clear();

private:
	std::map<uint32_t, PresentationTargetImage> targets;
};
