#pragma once

#include <string_view>

enum class RenderAPI;

enum class RenderDeviceType
{
	Vulkan,
	D3D11,
	D3D12,
	Null,
	OpenGL,
	WebGPU
};

struct RenderDeviceSelection
{
	RenderDeviceType Type;
	RenderAPI API;
	std::string_view CommandLineName;
	std::string_view DisplayName;
	bool Compiled;
	std::string_view UnavailableReason;
};

const RenderDeviceSelection& GetRenderDeviceSelection(RenderDeviceType type);
const RenderDeviceSelection* FindRenderDeviceSelection(std::string_view name);
