#include "RenderDevice/Vulkan/VulkanPresentationTargets.h"

#include <cstdlib>
#include <iostream>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << message << '\n';
			std::exit(1);
		}
	}

	PresentationTargetBinding Binding(uint32_t slot, uintptr_t handle,
		int width = 1024, int height = 768, bool flipHorizontal = false)
	{
		PresentationTargetBinding binding;
		binding.Target = { slot };
		binding.Images.push_back({ reinterpret_cast<void*>(handle), width, height,
			flipHorizontal });
		return binding;
	}
}

int main()
{
	VulkanPresentationTargets targets;
	for (uint32_t slot = 2; slot <= 5; slot++)
		Check(targets.Bind(Binding(slot, slot)), "independent UI target was rejected");
	for (uint32_t slot = 2; slot <= 5; slot++)
	{
		const PresentationTargetImage* image = targets.Find({ slot });
		Check(image && image->NativeHandle == reinterpret_cast<void*>(static_cast<uintptr_t>(slot)),
			"independent UI target was not retained by slot");
	}

	Check(!targets.Bind(Binding(0, 10)), "default target was accepted as an external UI target");
	Check(!targets.Bind(Binding(1, 11)), "stereo target was accepted as a single-image UI target");
	Check(!targets.Bind(Binding(2, 12)), "duplicate live binding replaced an acquired image");
	Check(!targets.Bind(Binding(6, 0)), "null image was accepted");
	Check(!targets.Bind(Binding(6, 6, 0, 768)), "invalid extent was accepted");

	targets.Unbind({ 3 });
	Check(!targets.Find({ 3 }), "unbound UI image remained live");
	Check(targets.Bind(Binding(3, 33, 1280, 720)), "released UI slot could not be rebound");
	const PresentationTargetImage* rebound = targets.Find({ 3 });
	Check(rebound && rebound->Width == 1280 && rebound->Height == 720,
		"rebound UI extent was not retained");
	targets.Unbind({ 3 });
	Check(targets.Bind(Binding(3, 34, 1280, 720, true)) &&
		targets.Find({ 3 })->FlipHorizontal,
		"provider image orientation was not retained");

	targets.Clear();
	for (uint32_t slot = 2; slot <= 5; slot++)
		Check(!targets.Find({ slot }), "clear retained a provider-owned image");

	std::cout << "Vulkan presentation target tests passed\n";
	return 0;
}
