#include "Render/Presentation.h"
#include "Render/ViewFamily.h"

#include <cstdlib>
#include <iostream>

static void Check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << message << '\n';
		std::exit(1);
	}
}

int main()
{
	PresentationPlan plan;
	auto defaultWorld = plan.GetLayer(PresentationLayer::World);
	Check(defaultWorld.Enabled, "unconfigured layers must be enabled");
	Check(defaultWorld.Target.IsDefault(), "unconfigured layers must use the window target");

	plan.SetLayer(PresentationLayer::UserInterface, { 2 });
	plan.SetLayer(PresentationLayer::World, { 1 }, false);
	plan.SetLayer(PresentationLayer::UserInterface, { 3 });
	Check(plan.Layers.size() == 2, "updating a layer must not add a duplicate");
	Check(plan.GetLayer(PresentationLayer::UserInterface).Target.Slot == 3, "layer target update was lost");
	Check(!plan.GetLayer(PresentationLayer::World).Enabled, "disabled layer was re-enabled");
	PresentationTargetBinding binding;
	binding.Target = { 7 };
	binding.Images.push_back({ reinterpret_cast<void*>(1), 1200, 1200 });
	binding.Images.push_back({ reinterpret_cast<void*>(2), 1200, 1200 });
	Check(binding.Target.Slot == 7 && binding.Images.size() == 2, "opaque presentation target binding was not retained");
	Check(binding.Images[1].Width == 1200 && binding.Images[1].NativeHandle == reinterpret_cast<void*>(2), "presentation image metadata was not retained");

	ViewDescription center;
	center.Location = vec3(10.0f, 20.0f, 30.0f);
	center.Rotation = Coords::Identity();
	center.Viewport = { 7, 9, 101, 60 };
	center.ApplyGameViewport = true;
	ViewFamily stereo = CreateSideBySideDiagnosticViewFamily(center, 4.0f);
	Check(stereo.Views.size() == 2, "diagnostic must create two valid views");
	Check(stereo.Views[0].Viewport.X == 7 && stereo.Views[0].Viewport.Width == 50, "left diagnostic viewport is incorrect");
	Check(stereo.Views[1].Viewport.X == 57 && stereo.Views[1].Viewport.Width == 51, "right diagnostic viewport is incorrect");
	Check(stereo.Views[0].Location == vec3(10.0f, 18.0f, 30.0f), "left eye offset is incorrect");
	Check(stereo.Views[1].Location == vec3(10.0f, 22.0f, 30.0f), "right eye offset is incorrect");
	Check(!stereo.Views[0].ApplyGameViewport && !stereo.Views[1].ApplyGameViewport, "game viewport crop must be disabled for diagnostic views");

	center.Viewport.Width = 1;
	ViewFamily fallback = CreateSideBySideDiagnosticViewFamily(center);
	Check(fallback.Views.size() == 1, "invalid split must safely retain one view");

	return 0;
}
