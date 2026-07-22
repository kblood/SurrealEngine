#include "RenderDevice/RenderDeviceSelection.h"
#include "RenderDevice/ClipSpaceConversion.h"

#include <surrealwidgets/window/window.h>

#include <iostream>
#include <cmath>

namespace
{
	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}
}

int main()
{
	const auto* webgpu = FindRenderDeviceSelection("webgpu");
	if (!webgpu || webgpu->Type != RenderDeviceType::WebGPU || webgpu->API != RenderAPI::WebGPU)
		return Fail("webgpu did not resolve to the WebGPU render API");

	const auto* webgl2 = FindRenderDeviceSelection("webgl2");
	if (!webgl2 || webgl2->Type != RenderDeviceType::OpenGL || webgl2->API != RenderAPI::OpenGL)
		return Fail("webgl2 did not resolve to the OpenGL render API");
	if (webgl2->Compiled)
		return Fail("webgl2 must not be advertised before a render device exists");
	if (webgl2->UnavailableReason.empty())
		return Fail("webgl2 must explain why it is unavailable");

	const auto* opengl = FindRenderDeviceSelection("opengl");
	if (opengl != webgl2)
		return Fail("opengl alias did not resolve to the WebGL2 selection");

	const auto* nullRenderer = FindRenderDeviceSelection("null");
	if (!nullRenderer || !nullRenderer->Compiled || nullRenderer->API != RenderAPI::Bitmap)
		return Fail("null renderer must always be available");

	if (FindRenderDeviceSelection("not-a-renderer"))
		return Fail("unknown renderer unexpectedly resolved");

	// Asymmetric WebXR projection: the X/Y offsets must survive while only
	// the clip-space Z row is converted from [-1, 1] to [0, 1].
	const std::array<float, 16> projection =
	{
		2.0f, 0.0f, 0.2f, 0.4f,
		0.0f, 3.0f, -0.6f, 0.8f,
		0.25f, -0.5f, -1.2f, -1.0f,
		0.1f, -0.2f, -0.2f, 0.0f
	};
	const auto converted = ConvertProjectionDepthMinusOneToOneToZeroToOne(projection);
	for (size_t column = 0; column < 4; column++)
	{
		for (size_t row = 0; row < 4; row++)
		{
			const size_t index = column * 4 + row;
			const float expected = row == 2
				? 0.5f * (projection[index] + projection[column * 4 + 3])
				: projection[index];
			if (std::abs(converted[index] - expected) > 0.00001f)
				return Fail("projection depth conversion changed the wrong matrix element");
		}
	}

	return 0;
}
