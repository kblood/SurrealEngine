#include "Precomp.h"
#include "RenderDeviceSelection.h"

#include <surrealwidgets/window/window.h>

namespace
{
#ifdef __EMSCRIPTEN__
	constexpr bool VulkanCompiled = false;
	constexpr bool WebGPUCompiled = true;
#else
	constexpr bool VulkanCompiled = true;
	constexpr bool WebGPUCompiled = false;
#endif

#if defined(__EMSCRIPTEN__) && defined(SURREAL_WEBGL2_RENDERER)
	constexpr bool WebGL2Compiled = true;
#else
	constexpr bool WebGL2Compiled = false;
#endif

#ifdef WIN32
	constexpr bool D3D11Compiled = true;
#else
	constexpr bool D3D11Compiled = false;
#endif

	constexpr RenderDeviceSelection Selections[] =
	{
		{ RenderDeviceType::Vulkan, RenderAPI::Vulkan, "vulkan", "Vulkan", VulkanCompiled,
			"The Vulkan render device is not compiled for this platform." },
		{ RenderDeviceType::D3D11, RenderAPI::D3D11, "d3d11", "Direct3D 11", D3D11Compiled,
			"The Direct3D 11 render device is only compiled for Windows." },
		{ RenderDeviceType::D3D12, RenderAPI::D3D12, "d3d12", "Direct3D 12", false,
			"SurrealEngine does not currently contain a Direct3D 12 render device." },
		{ RenderDeviceType::Null, RenderAPI::Bitmap, "null", "Null", true, {} },
		{ RenderDeviceType::OpenGL, RenderAPI::OpenGL, "webgl2", "WebGL 2", WebGL2Compiled,
			"The experimental WebGL 2 render device is not compiled in this build." },
		{ RenderDeviceType::WebGPU, RenderAPI::WebGPU, "webgpu", "WebGPU", WebGPUCompiled,
			"The WebGPU render device is only compiled for Emscripten browser builds." }
	};
}

const RenderDeviceSelection& GetRenderDeviceSelection(RenderDeviceType type)
{
	for (const auto& selection : Selections)
	{
		if (selection.Type == type)
			return selection;
	}

	return Selections[0];
}

const RenderDeviceSelection* FindRenderDeviceSelection(std::string_view name)
{
	for (const auto& selection : Selections)
	{
		if (selection.CommandLineName == name)
			return &selection;
	}

	// "opengl" is a useful native spelling for the same future backend.
	if (name == "opengl")
		return &GetRenderDeviceSelection(RenderDeviceType::OpenGL);

	return nullptr;
}
