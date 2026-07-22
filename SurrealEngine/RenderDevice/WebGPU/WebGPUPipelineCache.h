#pragma once

#include <webgpu/webgpu.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

class WebGPUContext;

struct WebGPUSceneVertex
{
	uint32_t Flags;
	float Position[3];
	float TexCoord[2];
	float TexCoord2[2];
	float TexCoord3[2];
	float TexCoord4[2];
	float Color[4];
};

struct WebGPUSceneUniforms
{
	float ObjectToProjection[16];
	float ClipSpaceYSign;
	float Padding[3];
};

static_assert(sizeof(WebGPUSceneUniforms) == 80);

struct WebGPUScenePipelineState
{
	WGPURenderPipeline Pipeline = nullptr;
	float MinDepth = 0.1f;
	float MaxDepth = 1.0f;
};

// Direct structural port of D3D11RenderDevice::CreateScenePass's pipeline
// table: 33 fixed-slot pipelines keyed by the same PolyFlags bit arithmetic
// (GetPipeline()), plus separate line/point pipelines, replicated lazily for
// each canvas/WebXR color format. Unlike D3D11 - which
// sets blend/depth state per draw call on an immediate context - WebGPU
// bakes blend/depth/topology into the WGPURenderPipeline object itself, so
// every (blend, depth, alphaTest) combination needs its own pipeline object
// created up front. See WEBXR_IMPLEMENTATION_PLAN.md M2.
class WebGPUPipelineCache
{
public:
	WebGPUPipelineCache(WebGPUContext* context, WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat);
	~WebGPUPipelineCache();

	// WebXR projection layers are allowed to use a color format that differs
	// from the canvas. All format families share these layouts and the shader,
	// so texture/uniform bind groups remain valid when the active target changes.
	bool EnsureColorFormat(WGPUTextureFormat colorFormat);
	bool SelectColorFormat(WGPUTextureFormat colorFormat);
	WebGPUScenePipelineState* GetPipeline(uint32_t PolyFlags);
	WebGPUScenePipelineState* GetLinePipeline(bool occlude);
	WebGPUScenePipelineState* GetPointPipeline(bool occlude);
	size_t GetColorFormatCount() const { return ColorFormatFamilies.size(); }

	WGPUBindGroupLayout UniformsBindGroupLayout = nullptr;
	WGPUBindGroupLayout TexturesBindGroupLayout = nullptr;

private:
	struct PipelineFamily
	{
		WebGPUScenePipelineState Pipelines[33];
		WebGPUScenePipelineState LinePipeline[2];
		WebGPUScenePipelineState PointPipeline[2];
	};

	PipelineFamily* GetOrCreateFamily(WGPUTextureFormat colorFormat);
	void ReleaseFamily(PipelineFamily& family);
	WGPURenderPipeline CreatePipeline(WebGPUContext* context, WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat, WGPUPrimitiveTopology topology, WGPUBlendState blend, uint32_t colorWriteMask, bool depthWrite, bool alphaTest);

	WebGPUContext* Context = nullptr;
	WGPUTextureFormat DepthFormat = WGPUTextureFormat_Undefined;
	std::unordered_map<uint32_t, std::unique_ptr<PipelineFamily>> ColorFormatFamilies;
	PipelineFamily* ActiveFamily = nullptr;
	WGPUShaderModule ShaderModule = nullptr;
	WGPUPipelineLayout PipelineLayout = nullptr;
};
