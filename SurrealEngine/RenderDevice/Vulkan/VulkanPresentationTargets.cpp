#include "RenderDevice/Vulkan/VulkanPresentationTargets.h"

bool VulkanPresentationTargets::Bind(const PresentationTargetBinding& binding)
{
	if (binding.Target.Slot <= 1 || binding.Images.size() != 1)
		return false;
	const PresentationTargetImage& image = binding.Images[0];
	if (!image.NativeHandle || image.Width <= 0 || image.Height <= 0 ||
		targets.contains(binding.Target.Slot))
		return false;
	targets.emplace(binding.Target.Slot, image);
	return true;
}

void VulkanPresentationTargets::Unbind(PresentationTarget target)
{
	if (target.Slot > 1)
		targets.erase(target.Slot);
}

const PresentationTargetImage* VulkanPresentationTargets::Find(PresentationTarget target) const
{
	auto found = targets.find(target.Slot);
	return found == targets.end() ? nullptr : &found->second;
}

void VulkanPresentationTargets::Clear()
{
	targets.clear();
}
