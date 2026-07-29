
#include "Precomp.h"
#include "WebGPUPipelineCache.h"
#include "WebGPUContext.h"
#include "WebGPUShaders.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include <cstddef>

static WGPUStringView ToStringView(const std::string& s) { return WGPUStringView{ s.data(), s.size() }; }
static WGPUStringView ToStringView(const char* s) { return WGPUStringView{ s, strlen(s) }; }

WebGPUPipelineCache::WebGPUPipelineCache(WebGPUContext* context, WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat)
{
	// Group 0: the per-frame uniform buffer (vertex stage only).
	WGPUBindGroupLayoutEntry uniformEntry = {};
	uniformEntry.binding = 0;
	uniformEntry.visibility = WGPUShaderStage_Vertex;
	uniformEntry.buffer.type = WGPUBufferBindingType_Uniform;

	WGPUBindGroupLayoutDescriptor uniformsLayoutDesc = {};
	uniformsLayoutDesc.entryCount = 1;
	uniformsLayoutDesc.entries = &uniformEntry;
	UniformsBindGroupLayout = wgpuDeviceCreateBindGroupLayout(context->Device, &uniformsLayoutDesc);

	// Group 1: the D3D11-style fixed 4 sampler + 4 texture slots (diffuse,
	// lightmap, macro, detail), fragment stage only.
	WGPUBindGroupLayoutEntry textureEntries[8] = {};
	for (uint32_t i = 0; i < 4; i++)
	{
		textureEntries[i].binding = i;
		textureEntries[i].visibility = WGPUShaderStage_Fragment;
		textureEntries[i].sampler.type = WGPUSamplerBindingType_Filtering;
	}
	for (uint32_t i = 0; i < 4; i++)
	{
		textureEntries[4 + i].binding = 4 + i;
		textureEntries[4 + i].visibility = WGPUShaderStage_Fragment;
		textureEntries[4 + i].texture.sampleType = WGPUTextureSampleType_Float;
		textureEntries[4 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
	}

	WGPUBindGroupLayoutDescriptor texturesLayoutDesc = {};
	texturesLayoutDesc.entryCount = 8;
	texturesLayoutDesc.entries = textureEntries;
	TexturesBindGroupLayout = wgpuDeviceCreateBindGroupLayout(context->Device, &texturesLayoutDesc);

	WGPUBindGroupLayout layouts[2] = { UniformsBindGroupLayout, TexturesBindGroupLayout };
	WGPUPipelineLayoutDescriptor layoutDesc = {};
	layoutDesc.bindGroupLayoutCount = 2;
	layoutDesc.bindGroupLayouts = layouts;
	PipelineLayout = wgpuDeviceCreatePipelineLayout(context->Device, &layoutDesc);

	std::string wgsl = WebGPUShaders::GetSceneShaderSource();
	WGPUShaderSourceWGSL wgslDesc = {};
	wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
	wgslDesc.code = ToStringView(wgsl);
	WGPUShaderModuleDescriptor shaderDesc = {};
	shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
	ShaderModule = wgpuDeviceCreateShaderModule(context->Device, &shaderDesc);

	for (int i = 0; i < 33; i++)
	{
		int blendCase = (i < 32) ? (i & 3) : -1; // -1 = PF_SubpixelFont (index 32)

		WGPUBlendState blend = {};
		blend.color.operation = WGPUBlendOperation_Add;
		blend.alpha.operation = WGPUBlendOperation_Add;
		switch (blendCase)
		{
		case 0: // PF_Translucent
			blend.color.srcFactor = WGPUBlendFactor_One;
			blend.color.dstFactor = WGPUBlendFactor_OneMinusSrc;
			blend.alpha.srcFactor = WGPUBlendFactor_One;
			blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
			break;
		case 1: // PF_Modulated
			blend.color.srcFactor = WGPUBlendFactor_Dst;
			blend.color.dstFactor = WGPUBlendFactor_Src;
			blend.alpha.srcFactor = WGPUBlendFactor_DstAlpha;
			blend.alpha.dstFactor = WGPUBlendFactor_SrcAlpha;
			break;
		case 2: // PF_Highlighted
			blend.color.srcFactor = WGPUBlendFactor_One;
			blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
			blend.alpha.srcFactor = WGPUBlendFactor_One;
			blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
			break;
		case 3:
			blend.color.srcFactor = WGPUBlendFactor_One;
			blend.color.dstFactor = WGPUBlendFactor_Zero;
			blend.alpha.srcFactor = WGPUBlendFactor_One;
			blend.alpha.dstFactor = WGPUBlendFactor_Zero;
			break;
		default: // PF_SubpixelFont
			blend.color.srcFactor = WGPUBlendFactor_Constant;
			blend.color.dstFactor = WGPUBlendFactor_OneMinusSrc;
			blend.alpha.srcFactor = WGPUBlendFactor_Constant;
			blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
			break;
		}

		uint32_t colorWriteMask;
		if (i < 32)
			colorWriteMask = (i & 4) ? WGPUColorWriteMask_None : WGPUColorWriteMask_All; // PF_Invisible
		else
			colorWriteMask = WGPUColorWriteMask_All;

		bool depthWrite = (i < 32) ? ((i & 8) != 0) : false; // PF_Occlude
		bool alphaTest = (i < 32) ? ((i & 16) != 0) : true;  // PF_Masked, or always for PF_SubpixelFont

		Pipelines[i].Pipeline = CreatePipeline(context, colorFormat, depthFormat, WGPUPrimitiveTopology_TriangleList, blend, colorWriteMask, depthWrite, alphaTest);
	}

	for (int i = 0; i < 2; i++)
	{
		WGPUBlendState blend = {};
		blend.color.operation = WGPUBlendOperation_Add;
		blend.color.srcFactor = WGPUBlendFactor_One;
		blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
		blend.alpha.operation = WGPUBlendOperation_Add;
		blend.alpha.srcFactor = WGPUBlendFactor_One;
		blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

		LinePipeline[i].Pipeline = CreatePipeline(context, colorFormat, depthFormat, WGPUPrimitiveTopology_LineList, blend, WGPUColorWriteMask_All, true, false);
		PointPipeline[i].Pipeline = CreatePipeline(context, colorFormat, depthFormat, WGPUPrimitiveTopology_TriangleList, blend, WGPUColorWriteMask_All, true, false);

		if (i == 0)
		{
			LinePipeline[i].MinDepth = 0.0f;
			LinePipeline[i].MaxDepth = 0.1f;
			PointPipeline[i].MinDepth = 0.0f;
			PointPipeline[i].MaxDepth = 0.1f;
		}
	}
}

WGPURenderPipeline WebGPUPipelineCache::CreatePipeline(WebGPUContext* context, WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat, WGPUPrimitiveTopology topology, WGPUBlendState blend, uint32_t colorWriteMask, bool depthWrite, bool alphaTest)
{
	WGPUVertexAttribute attributes[7] = {};
	attributes[0] = { nullptr, WGPUVertexFormat_Uint32, offsetof(WebGPUSceneVertex, Flags), 0 };
	attributes[1] = { nullptr, WGPUVertexFormat_Float32x3, offsetof(WebGPUSceneVertex, Position), 1 };
	attributes[2] = { nullptr, WGPUVertexFormat_Float32x2, offsetof(WebGPUSceneVertex, TexCoord), 2 };
	attributes[3] = { nullptr, WGPUVertexFormat_Float32x2, offsetof(WebGPUSceneVertex, TexCoord2), 3 };
	attributes[4] = { nullptr, WGPUVertexFormat_Float32x2, offsetof(WebGPUSceneVertex, TexCoord3), 4 };
	attributes[5] = { nullptr, WGPUVertexFormat_Float32x2, offsetof(WebGPUSceneVertex, TexCoord4), 5 };
	attributes[6] = { nullptr, WGPUVertexFormat_Float32x4, offsetof(WebGPUSceneVertex, Color), 6 };

	WGPUVertexBufferLayout vertexBufferLayout = {};
	vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
	vertexBufferLayout.arrayStride = sizeof(WebGPUSceneVertex);
	vertexBufferLayout.attributeCount = 7;
	vertexBufferLayout.attributes = attributes;

	WGPUConstantEntry alphaTestConstant = {};
	alphaTestConstant.key = ToStringView("alphaTest");
	alphaTestConstant.value = alphaTest ? 1.0 : 0.0;

	WGPUColorTargetState colorTarget = {};
	colorTarget.format = colorFormat;
	colorTarget.blend = &blend;
	colorTarget.writeMask = colorWriteMask;

	WGPUFragmentState fragment = {};
	fragment.module = ShaderModule;
	fragment.entryPoint = ToStringView("fs_main");
	fragment.constantCount = 1;
	fragment.constants = &alphaTestConstant;
	fragment.targetCount = 1;
	fragment.targets = &colorTarget;

	WGPUDepthStencilState depthStencil = {};
	depthStencil.format = depthFormat;
	depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
	depthStencil.depthCompare = WGPUCompareFunction_LessEqual;
	depthStencil.stencilReadMask = 0xffffffff;
	depthStencil.stencilWriteMask = 0xffffffff;

	WGPURenderPipelineDescriptor pipelineDesc = {};
	pipelineDesc.layout = PipelineLayout;
	pipelineDesc.vertex.module = ShaderModule;
	pipelineDesc.vertex.entryPoint = ToStringView("vs_main");
	pipelineDesc.vertex.bufferCount = 1;
	pipelineDesc.vertex.buffers = &vertexBufferLayout;
	pipelineDesc.primitive.topology = topology;
	pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
	pipelineDesc.primitive.cullMode = WGPUCullMode_None;
	pipelineDesc.depthStencil = &depthStencil;
	pipelineDesc.multisample.count = 1;
	pipelineDesc.multisample.mask = 0xffffffff;
	pipelineDesc.fragment = &fragment;

	return wgpuDeviceCreateRenderPipeline(context->Device, &pipelineDesc);
}

WebGPUPipelineCache::~WebGPUPipelineCache()
{
	for (auto& p : Pipelines) if (p.Pipeline) wgpuRenderPipelineRelease(p.Pipeline);
	for (auto& p : LinePipeline) if (p.Pipeline) wgpuRenderPipelineRelease(p.Pipeline);
	for (auto& p : PointPipeline) if (p.Pipeline) wgpuRenderPipelineRelease(p.Pipeline);
	if (ShaderModule) wgpuShaderModuleRelease(ShaderModule);
	if (PipelineLayout) wgpuPipelineLayoutRelease(PipelineLayout);
	if (TexturesBindGroupLayout) wgpuBindGroupLayoutRelease(TexturesBindGroupLayout);
	if (UniformsBindGroupLayout) wgpuBindGroupLayoutRelease(UniformsBindGroupLayout);
}

WebGPUScenePipelineState* WebGPUPipelineCache::GetPipeline(uint32_t PolyFlags)
{
	int index;
	if (PolyFlags & PF_Translucent)
		index = 0;
	else if (PolyFlags & PF_Modulated)
		index = 1;
	else if (PolyFlags & PF_Highlighted)
		index = 2;
	else
		index = 3;

	if (PolyFlags & PF_Invisible)
		index |= 4;
	if (PolyFlags & PF_Occlude)
		index |= 8;
	if (PolyFlags & PF_Masked)
		index |= 16;

	if (PolyFlags == PF_SubpixelFont)
		index = 32;

	return &Pipelines[index];
}
