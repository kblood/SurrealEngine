#pragma once

#include <webgpu/webgpu.h>

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
};

struct WebGPUScenePipelineState
{
	WGPURenderPipeline Pipeline = nullptr;
	float MinDepth = 0.1f;
	float MaxDepth = 1.0f;
};

// Direct structural port of D3D11RenderDevice::CreateScenePass's pipeline
// table: 33 fixed-slot pipelines keyed by the same PolyFlags bit arithmetic
// (GetPipeline()), plus separate line/point pipelines. Unlike D3D11 - which
// sets blend/depth state per draw call on an immediate context - WebGPU
// bakes blend/depth/topology into the WGPURenderPipeline object itself, so
// every (blend, depth, alphaTest) combination needs its own pipeline object
// created up front.
class WebGPUPipelineCache
{
public:
	WebGPUPipelineCache(WebGPUContext* context, WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat);
	~WebGPUPipelineCache();

	WebGPUScenePipelineState* GetPipeline(uint32_t PolyFlags);

	WGPUBindGroupLayout UniformsBindGroupLayout = nullptr;
	WGPUBindGroupLayout TexturesBindGroupLayout = nullptr;

	WebGPUScenePipelineState Pipelines[33];
	WebGPUScenePipelineState LinePipeline[2];
	WebGPUScenePipelineState PointPipeline[2];

private:
	WGPURenderPipeline CreatePipeline(WebGPUContext* context, WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat, WGPUPrimitiveTopology topology, WGPUBlendState blend, uint32_t colorWriteMask, bool depthWrite, bool alphaTest);

	WGPUShaderModule ShaderModule = nullptr;
	WGPUPipelineLayout PipelineLayout = nullptr;
};
