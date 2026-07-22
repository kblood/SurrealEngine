#include "Platform/WebXR/WebXRUICompositor.h"

#ifdef __EMSCRIPTEN__

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	WGPUStringView ToStringView(const char* value)
	{
		return { value, std::strlen(value) };
	}

	struct CompositeVertex
	{
		vec4 Clip;
		vec2 UV;
	};

	struct SurfaceTexture
	{
		PresentationTarget Target;
		int Width = 0;
		int Height = 0;
		WGPUTexture Texture = nullptr;
		WGPUTextureView View = nullptr;
		WebGPUPresentationImageHandle Handle;
	};

	struct CompositorState
	{
		WGPUDevice Device = nullptr;
		WGPUTextureFormat Format = WGPUTextureFormat_Undefined;
		SurfaceTexture Surfaces[3] = {
			{ WebXR::CinematicSurfaceTarget, 1280, 720 },
			{ WebXR::LoadingSurfaceTarget, 1024, 768 },
			{ WebXR::MenuSurfaceTarget, 1024, 768 }
		};
		WGPUBindGroupLayout TextureLayout = nullptr;
		WGPUPipelineLayout PipelineLayout = nullptr;
		WGPUShaderModule Shader = nullptr;
		WGPURenderPipeline Pipeline = nullptr;
		WGPUSampler Sampler = nullptr;

		SurfaceTexture* Find(PresentationTarget target)
		{
			for (SurfaceTexture& surface : Surfaces)
				if (surface.Target == target)
					return &surface;
			return nullptr;
		}

		void Release()
		{
			for (SurfaceTexture& surface : Surfaces)
			{
				if (surface.View) wgpuTextureViewRelease(surface.View);
				if (surface.Texture) wgpuTextureRelease(surface.Texture);
				surface.View = nullptr;
				surface.Texture = nullptr;
				surface.Handle = {};
			}
			if (Sampler) wgpuSamplerRelease(Sampler);
			if (Pipeline) wgpuRenderPipelineRelease(Pipeline);
			if (Shader) wgpuShaderModuleRelease(Shader);
			if (PipelineLayout) wgpuPipelineLayoutRelease(PipelineLayout);
			if (TextureLayout) wgpuBindGroupLayoutRelease(TextureLayout);
			Sampler = nullptr;
			Pipeline = nullptr;
			Shader = nullptr;
			PipelineLayout = nullptr;
			TextureLayout = nullptr;
			Device = nullptr;
			Format = WGPUTextureFormat_Undefined;
		}

		bool Ensure(WGPUDevice device, WGPUTextureFormat format)
		{
			if (Device == device && Format == format && Pipeline)
				return true;
			Release();
			Device = device;
			Format = format;

			for (SurfaceTexture& surface : Surfaces)
			{
				WGPUTextureDescriptor textureDescription = {};
				textureDescription.dimension = WGPUTextureDimension_2D;
				textureDescription.size = { static_cast<uint32_t>(surface.Width),
					static_cast<uint32_t>(surface.Height), 1 };
				textureDescription.format = format;
				textureDescription.mipLevelCount = 1;
				textureDescription.sampleCount = 1;
				textureDescription.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
				surface.Texture = wgpuDeviceCreateTexture(device, &textureDescription);
				if (!surface.Texture)
				{
					Release();
					return false;
				}
				WGPUTextureViewDescriptor viewDescription = {};
				viewDescription.format = format;
				viewDescription.dimension = WGPUTextureViewDimension_2D;
				viewDescription.baseMipLevel = 0;
				viewDescription.mipLevelCount = 1;
				viewDescription.baseArrayLayer = 0;
				viewDescription.arrayLayerCount = 1;
				viewDescription.aspect = WGPUTextureAspect_All;
				surface.View = wgpuTextureCreateView(surface.Texture, &viewDescription);
				if (!surface.View)
				{
					Release();
					return false;
				}
				surface.Handle = { surface.View, format };
			}

			WGPUBindGroupLayoutEntry entries[2] = {};
			entries[0].binding = 0;
			entries[0].visibility = WGPUShaderStage_Fragment;
			entries[0].sampler.type = WGPUSamplerBindingType_Filtering;
			entries[1].binding = 1;
			entries[1].visibility = WGPUShaderStage_Fragment;
			entries[1].texture.sampleType = WGPUTextureSampleType_Float;
			entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
			WGPUBindGroupLayoutDescriptor bindGroupLayoutDescription = {};
			bindGroupLayoutDescription.entryCount = 2;
			bindGroupLayoutDescription.entries = entries;
			TextureLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDescription);
			WGPUPipelineLayoutDescriptor pipelineLayoutDescription = {};
			pipelineLayoutDescription.bindGroupLayoutCount = 1;
			pipelineLayoutDescription.bindGroupLayouts = &TextureLayout;
			PipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDescription);

			const char* shaderSource = R"(
struct VertexInput {
    @location(0) clip: vec4<f32>,
    @location(1) uv: vec2<f32>,
};
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};
@group(0) @binding(0) var uiSampler: sampler;
@group(0) @binding(1) var uiTexture: texture_2d<f32>;
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4<f32>(input.clip.x, -input.clip.y, input.clip.z, input.clip.w);
    output.uv = input.uv;
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return textureSample(uiTexture, uiSampler, input.uv);
}
)";
			WGPUShaderSourceWGSL wgsl = {};
			wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
			wgsl.code = ToStringView(shaderSource);
			WGPUShaderModuleDescriptor shaderDescription = {};
			shaderDescription.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl);
			Shader = wgpuDeviceCreateShaderModule(device, &shaderDescription);

			WGPUVertexAttribute attributes[2] = {};
			attributes[0] = { nullptr, WGPUVertexFormat_Float32x4, offsetof(CompositeVertex, Clip), 0 };
			attributes[1] = { nullptr, WGPUVertexFormat_Float32x2, offsetof(CompositeVertex, UV), 1 };
			WGPUVertexBufferLayout vertexLayout = {};
			vertexLayout.arrayStride = sizeof(CompositeVertex);
			vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
			vertexLayout.attributeCount = 2;
			vertexLayout.attributes = attributes;

			WGPUBlendState blend = {};
			blend.color.operation = WGPUBlendOperation_Add;
			blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
			blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
			blend.alpha.operation = WGPUBlendOperation_Add;
			blend.alpha.srcFactor = WGPUBlendFactor_One;
			blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
			WGPUColorTargetState colorTarget = {};
			colorTarget.format = format;
			colorTarget.blend = &blend;
			colorTarget.writeMask = WGPUColorWriteMask_All;
			WGPUFragmentState fragment = {};
			fragment.module = Shader;
			fragment.entryPoint = ToStringView("fs_main");
			fragment.targetCount = 1;
			fragment.targets = &colorTarget;
			WGPURenderPipelineDescriptor pipelineDescription = {};
			pipelineDescription.layout = PipelineLayout;
			pipelineDescription.vertex.module = Shader;
			pipelineDescription.vertex.entryPoint = ToStringView("vs_main");
			pipelineDescription.vertex.bufferCount = 1;
			pipelineDescription.vertex.buffers = &vertexLayout;
			pipelineDescription.primitive.topology = WGPUPrimitiveTopology_TriangleList;
			pipelineDescription.primitive.frontFace = WGPUFrontFace_CCW;
			pipelineDescription.primitive.cullMode = WGPUCullMode_None;
			pipelineDescription.multisample.count = 1;
			pipelineDescription.multisample.mask = 0xffffffff;
			pipelineDescription.fragment = &fragment;
			Pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDescription);

			WGPUSamplerDescriptor samplerDescription = {};
			samplerDescription.addressModeU = WGPUAddressMode_ClampToEdge;
			samplerDescription.addressModeV = WGPUAddressMode_ClampToEdge;
			samplerDescription.addressModeW = WGPUAddressMode_ClampToEdge;
			samplerDescription.magFilter = WGPUFilterMode_Linear;
			samplerDescription.minFilter = WGPUFilterMode_Linear;
			samplerDescription.mipmapFilter = WGPUMipmapFilterMode_Nearest;
			samplerDescription.maxAnisotropy = 1;
			Sampler = wgpuDeviceCreateSampler(device, &samplerDescription);
			if (!TextureLayout || !PipelineLayout || !Shader || !Pipeline || !Sampler)
			{
				Release();
				return false;
			}
			return true;
		}
	};

	CompositorState State;

	void AppendSurfaceVertices(const XRUICanvasReplayItem& item, const ViewDescription& view,
		std::vector<CompositeVertex>& vertices)
	{
		const float halfWidth = item.Surface.Descriptor.PhysicalWidth * 0.5f;
		const float halfHeight = item.Surface.Descriptor.PhysicalHeight() * 0.5f;
		const XRUISurfacePose& pose = item.Surface.Pose;
		const vec3 corners[4] = {
			pose.Center - pose.Right * halfWidth + pose.Up * halfHeight,
			pose.Center + pose.Right * halfWidth + pose.Up * halfHeight,
			pose.Center + pose.Right * halfWidth - pose.Up * halfHeight,
			pose.Center - pose.Right * halfWidth - pose.Up * halfHeight
		};
		vec4 clip[4];
		for (size_t index = 0; index < 4; index++)
		{
			clip[index] = view.Projection * view.WorldToView * vec4(corners[index], 1.0f);
			if (clip[index].w <= 0.0001f)
				return;
		}
		const CompositeVertex quad[6] = {
			{ clip[0], vec2(0.0f, 0.0f) }, { clip[1], vec2(1.0f, 0.0f) },
			{ clip[2], vec2(1.0f, 1.0f) }, { clip[0], vec2(0.0f, 0.0f) },
			{ clip[2], vec2(1.0f, 1.0f) }, { clip[3], vec2(0.0f, 1.0f) }
		};
		vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
	}
}

bool WebXR::BindUISurfaceTargets(WebGPURenderDevice* device, WGPUTextureFormat format)
{
	if (!device || !device->Context || !State.Ensure(device->Context->Device, format))
		return false;
	for (SurfaceTexture& surface : State.Surfaces)
	{
		PresentationTargetBinding binding;
		binding.Target = surface.Target;
		binding.Images.push_back({ &surface.Handle, surface.Width, surface.Height });
		if (!device->BindPresentationTarget(binding))
		{
			UnbindUISurfaceTargets(device);
			return false;
		}
	}
	return true;
}

void WebXR::UnbindUISurfaceTargets(WebGPURenderDevice* device)
{
	if (!device)
		return;
	for (SurfaceTexture& surface : State.Surfaces)
		device->UnbindPresentationTarget(surface.Target);
}

bool WebXR::CompositeUISurfaces(WebGPURenderDevice* device, WGPUTextureFormat format,
	const ViewFamily& family, const XRUICanvasReplayFrame& replayFrame,
	const WGPUTextureView* projectionViews, uint32_t projectionViewCount)
{
	if (!device || !device->Context || family.Views.size() != projectionViewCount ||
		!State.Ensure(device->Context->Device, format))
		return false;

	for (uint32_t viewIndex = 0; viewIndex < projectionViewCount; viewIndex++)
	{
		std::vector<CompositeVertex> vertices;
		std::vector<SurfaceTexture*> surfaces;
		for (const XRUICanvasReplayItem& item : replayFrame.Items)
		{
			SurfaceTexture* surface = State.Find(item.Target);
			if (!surface)
				continue;
			const size_t oldSize = vertices.size();
			AppendSurfaceVertices(item, family.Views[viewIndex], vertices);
			if (vertices.size() != oldSize)
				surfaces.push_back(surface);
		}
		if (vertices.empty())
			continue;

		WGPUBufferDescriptor bufferDescription = {};
		bufferDescription.size = vertices.size() * sizeof(CompositeVertex);
		bufferDescription.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
		WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device->Context->Device, &bufferDescription);
		if (!vertexBuffer)
			return false;
		wgpuQueueWriteBuffer(device->Context->Queue, vertexBuffer, 0, vertices.data(), bufferDescription.size);

		WGPUCommandEncoderDescriptor encoderDescription = {};
		WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device->Context->Device, &encoderDescription);
		WGPURenderPassColorAttachment colorAttachment = {};
		colorAttachment.view = projectionViews[viewIndex];
		colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
		colorAttachment.loadOp = WGPULoadOp_Load;
		colorAttachment.storeOp = WGPUStoreOp_Store;
		WGPURenderPassDescriptor passDescription = {};
		passDescription.colorAttachmentCount = 1;
		passDescription.colorAttachments = &colorAttachment;
		WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDescription);
		const ViewRect& viewport = family.Views[viewIndex].Viewport;
		wgpuRenderPassEncoderSetViewport(pass, static_cast<float>(viewport.X),
			static_cast<float>(viewport.Y), static_cast<float>(viewport.Width),
			static_cast<float>(viewport.Height), 0.0f, 1.0f);
		wgpuRenderPassEncoderSetPipeline(pass, State.Pipeline);
		wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, bufferDescription.size);
		for (size_t surfaceIndex = 0; surfaceIndex < surfaces.size(); surfaceIndex++)
		{
			WGPUBindGroupEntry bindEntries[2] = {};
			bindEntries[0].binding = 0;
			bindEntries[0].sampler = State.Sampler;
			bindEntries[1].binding = 1;
			bindEntries[1].textureView = surfaces[surfaceIndex]->View;
			WGPUBindGroupDescriptor bindDescription = {};
			bindDescription.layout = State.TextureLayout;
			bindDescription.entryCount = 2;
			bindDescription.entries = bindEntries;
			WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device->Context->Device, &bindDescription);
			wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
			wgpuRenderPassEncoderDraw(pass, 6, 1, static_cast<uint32_t>(surfaceIndex * 6), 0);
			wgpuBindGroupRelease(bindGroup);
		}
		wgpuRenderPassEncoderEnd(pass);
		wgpuRenderPassEncoderRelease(pass);
		WGPUCommandBufferDescriptor commandDescription = {};
		WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandDescription);
		wgpuQueueSubmit(device->Context->Queue, 1, &commandBuffer);
		wgpuCommandBufferRelease(commandBuffer);
		wgpuCommandEncoderRelease(encoder);
		wgpuBufferRelease(vertexBuffer);
	}
	return true;
}

void WebXR::ResetUICompositor(WebGPURenderDevice* device)
{
	UnbindUISurfaceTargets(device);
	State.Release();
}

#endif
