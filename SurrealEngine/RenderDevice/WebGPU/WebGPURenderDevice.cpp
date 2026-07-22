
#include "Precomp.h"
#include "WebGPURenderDevice.h"
#include <surrealwidgets/core/widget.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <limits>

// Single live instance, tracked for the Surreal_GetWebGPU*() stat exports
// below (Engine.cpp's Surreal_GetTickCount/Surreal_RequestQuit pattern) -
// there is only ever one RenderDevice for the process's lifetime.
static WebGPURenderDevice* CurrentDevice = nullptr;

WebGPURenderDevice::WebGPURenderDevice(Widget* viewport)
{
	CurrentDevice = this;
	Viewport = viewport;
	Context = std::make_unique<WebGPUContext>();
	Pipelines = std::make_unique<WebGPUPipelineCache>(Context.get(), Context->SurfaceFormat, DepthFormat);
	PipelineColorFormat = Context->SurfaceFormat;
	Samplers = std::make_unique<WebGPUSamplerCache>(Context.get());
	Textures = std::make_unique<WebGPUTextureManager>(this);
	Uploads = std::make_unique<WebGPUUploadManager>(this);

	SceneVertices.resize(VertexBufferCapacity);
	SceneIndexes.resize(IndexBufferCapacity);

	WGPUBufferDescriptor vbDesc = {};
	vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
	vbDesc.size = VertexBufferCapacity * sizeof(WebGPUSceneVertex);
	VertexBuffer = wgpuDeviceCreateBuffer(Context->Device, &vbDesc);

	WGPUBufferDescriptor ibDesc = {};
	ibDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
	ibDesc.size = IndexBufferCapacity * sizeof(uint32_t);
	IndexBuffer = wgpuDeviceCreateBuffer(Context->Device, &ibDesc);

	WGPUBufferDescriptor ubDesc = {};
	ubDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	ubDesc.size = sizeof(WebGPUSceneUniforms);
	UniformBuffer = wgpuDeviceCreateBuffer(Context->Device, &ubDesc);

	CreateUniformBindGroup();
}

void WebGPURenderDevice::CreateUniformBindGroup()
{
	if (UniformsBindGroup)
	{
		wgpuBindGroupRelease(UniformsBindGroup);
		UniformsBindGroup = nullptr;
	}
	WGPUBindGroupEntry uniformEntry = {};
	uniformEntry.binding = 0;
	uniformEntry.buffer = UniformBuffer;
	uniformEntry.offset = 0;
	uniformEntry.size = sizeof(WebGPUSceneUniforms);

	WGPUBindGroupDescriptor uniformsBindGroupDesc = {};
	uniformsBindGroupDesc.layout = Pipelines->UniformsBindGroupLayout;
	uniformsBindGroupDesc.entryCount = 1;
	uniformsBindGroupDesc.entries = &uniformEntry;
	UniformsBindGroup = wgpuDeviceCreateBindGroup(Context->Device, &uniformsBindGroupDesc);
}

WebGPURenderDevice::~WebGPURenderDevice()
{
	if (CurrentDevice == this)
		CurrentDevice = nullptr;

	if (FramePass) { wgpuRenderPassEncoderRelease(FramePass); FramePass = nullptr; }
	if (FrameEncoder) { wgpuCommandEncoderRelease(FrameEncoder); FrameEncoder = nullptr; }
	if (CurrentSurfaceView && !ExternalPresentationActive) { wgpuTextureViewRelease(CurrentSurfaceView); }
	CurrentSurfaceView = nullptr;
	if (CurrentSurfaceTexture) { wgpuTextureRelease(CurrentSurfaceTexture); CurrentSurfaceTexture = nullptr; }
	if (UniformsBindGroup) wgpuBindGroupRelease(UniformsBindGroup);
	if (UniformBuffer) wgpuBufferRelease(UniformBuffer);
	if (IndexBuffer) wgpuBufferRelease(IndexBuffer);
	if (VertexBuffer) wgpuBufferRelease(VertexBuffer);
	if (DepthView) wgpuTextureViewRelease(DepthView);
	if (DepthTexture) wgpuTextureRelease(DepthTexture);
}

void WebGPURenderDevice::ConfigureDepthBuffer(int width, int height)
{
	if (width == DepthWidth && height == DepthHeight)
		return;

	if (DepthView) { wgpuTextureViewRelease(DepthView); DepthView = nullptr; }
	if (DepthTexture) { wgpuTextureRelease(DepthTexture); DepthTexture = nullptr; }

	WGPUTextureDescriptor texDesc = {};
	texDesc.usage = WGPUTextureUsage_RenderAttachment;
	texDesc.dimension = WGPUTextureDimension_2D;
	texDesc.size = { (uint32_t)width, (uint32_t)height, 1 };
	texDesc.format = DepthFormat;
	texDesc.mipLevelCount = 1;
	texDesc.sampleCount = 1;
	DepthTexture = wgpuDeviceCreateTexture(Context->Device, &texDesc);

	WGPUTextureViewDescriptor viewDesc = {};
	viewDesc.format = DepthFormat;
	viewDesc.dimension = WGPUTextureViewDimension_2D;
	viewDesc.baseMipLevel = 0;
	viewDesc.mipLevelCount = 1;
	viewDesc.baseArrayLayer = 0;
	viewDesc.arrayLayerCount = 1;
	viewDesc.aspect = WGPUTextureAspect_All;
	DepthView = wgpuTextureCreateView(DepthTexture, &viewDesc);

	DepthWidth = width;
	DepthHeight = height;
}

void WebGPURenderDevice::BeginFramePass(bool colorClear, vec4 clearColor, bool depthClear)
{
	WGPUCommandEncoderDescriptor encDesc = {};
	FrameEncoder = wgpuDeviceCreateCommandEncoder(Context->Device, &encDesc);

	WGPURenderPassColorAttachment colorAttachment = {};
	colorAttachment.view = CurrentSurfaceView;
	colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
	colorAttachment.loadOp = colorClear ? WGPULoadOp_Clear : WGPULoadOp_Load;
	colorAttachment.storeOp = WGPUStoreOp_Store;
	colorAttachment.clearValue = { clearColor.r, clearColor.g, clearColor.b, clearColor.a };

	WGPURenderPassDepthStencilAttachment depthAttachment = {};
	depthAttachment.view = DepthView;
	depthAttachment.depthLoadOp = depthClear ? WGPULoadOp_Clear : WGPULoadOp_Load;
	depthAttachment.depthStoreOp = WGPUStoreOp_Store;
	depthAttachment.depthClearValue = 1.0f;
	depthAttachment.depthReadOnly = false;

	WGPURenderPassDescriptor passDesc = {};
	passDesc.colorAttachmentCount = 1;
	passDesc.colorAttachments = &colorAttachment;
	passDesc.depthStencilAttachment = &depthAttachment;
	FramePass = wgpuCommandEncoderBeginRenderPass(FrameEncoder, &passDesc);

	if (HaveViewport)
		wgpuRenderPassEncoderSetViewport(FramePass, ViewportX, ViewportY, ViewportW, ViewportH, ViewportMinDepth, ViewportMaxDepth);
}

void WebGPURenderDevice::EndAndSubmitFramePass()
{
	wgpuRenderPassEncoderEnd(FramePass);
	wgpuRenderPassEncoderRelease(FramePass);
	FramePass = nullptr;

	WGPUCommandBufferDescriptor cmdDesc = {};
	WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(FrameEncoder, &cmdDesc);
	wgpuQueueSubmit(Context->Queue, 1, &cmdBuffer);
	wgpuCommandBufferRelease(cmdBuffer);
	wgpuCommandEncoderRelease(FrameEncoder);
	FrameEncoder = nullptr;
}

void WebGPURenderDevice::Flush(bool AllowPrecache)
{
	DrawBatches();
	Textures->ClearCache();
	if (AllowPrecache)
		PrecacheOnFlip = true;
}

bool WebGPURenderDevice::BeginPresentationLayer(const PresentationLayerDescription& layer)
{
	if (!layer.Enabled)
		return false;
	return layer.Target.IsDefault() ? !ExternalPresentationActive :
		ExternalPresentationActive && layer.Target == ExternalTarget;
}

bool WebGPURenderDevice::BindPresentationTarget(const PresentationTargetBinding& binding)
{
	if (binding.Target.IsDefault() || ExternalPresentationActive || IsLocked ||
		binding.Images.empty() || binding.Images.size() > 2)
		return false;

	WGPUTextureFormat format = WGPUTextureFormat_Undefined;
	for (const PresentationTargetImage& image : binding.Images)
	{
		if (!image.NativeHandle || image.Width <= 0 || image.Height <= 0)
			return false;
		auto* handle = static_cast<WebGPUPresentationImageHandle*>(image.NativeHandle);
		if (!handle->View || handle->Format == WGPUTextureFormat_Undefined)
			return false;
		if (format == WGPUTextureFormat_Undefined)
			format = handle->Format;
		else if (format != handle->Format)
			return false;
	}
	if (!EnsurePipelineColorFormat(format))
		return false;

	ExternalTarget = binding.Target;
	ExternalViews.assign(binding.Images.begin(), binding.Images.end());
	CurrentExternalView = 0;
	ExternalPresentationActive = true;
	return true;
}

void WebGPURenderDevice::UnbindPresentationTarget(PresentationTarget target)
{
	if (!ExternalPresentationActive || target != ExternalTarget || IsLocked)
		return;
	ExternalViews.clear();
	ExternalTarget = {};
	CurrentExternalView = 0;
	ExternalPresentationActive = false;
	EnsurePipelineColorFormat(Context->SurfaceFormat);
}

bool WebGPURenderDevice::EnsurePipelineColorFormat(WGPUTextureFormat format)
{
	if (format == PipelineColorFormat)
		return true;
	if (IsLocked || (format != WGPUTextureFormat_BGRA8Unorm &&
		format != WGPUTextureFormat_RGBA8Unorm && format != WGPUTextureFormat_RGBA16Float))
		return false;

	Pipelines = std::make_unique<WebGPUPipelineCache>(Context.get(), format, DepthFormat);
	PipelineColorFormat = format;
	CreateUniformBindGroup();
	return true;
}

bool WebGPURenderDevice::SelectExternalView(size_t viewIndex)
{
	if (!ExternalPresentationActive || viewIndex >= ExternalViews.size())
		return false;
	if (viewIndex == CurrentExternalView)
		return true;
	if (!IsLocked || !FramePass || !FrameEncoder)
	{
		CurrentExternalView = viewIndex;
		return true;
	}

	DrawBatches();
	EndAndSubmitFramePass();
	CurrentExternalView = viewIndex;
	const PresentationTargetImage& image = ExternalViews[viewIndex];
	auto* handle = static_cast<WebGPUPresentationImageHandle*>(image.NativeHandle);
	CurrentSurfaceView = handle->View;
	CurrentSizeX = image.Width;
	CurrentSizeY = image.Height;
	ConfigureDepthBuffer(CurrentSizeX, CurrentSizeY);
	HaveViewport = false;
	BeginFramePass(/*colorClear=*/true, CurrentClearColor, /*depthClear=*/true);

	SceneVertexPos = 0;
	SceneIndexPos = 0;
	UploadedVertexPos = 0;
	UploadedIndexPos = 0;
	Batch = WebGPUDrawBatchEntry();
	QueuedBatches.clear();
	Stats.BuffersUsed++;
	return true;
}

bool WebGPURenderDevice::BeginPresentationView(PresentationTarget target, size_t viewIndex)
{
	if (target.IsDefault())
		return !ExternalPresentationActive;
	return target == ExternalTarget && SelectExternalView(viewIndex);
}

void WebGPURenderDevice::Lock(vec4 InFlashScale, vec4 InFlashFog, vec4 ScreenClear, uint8_t* InHitData, int* InHitSize)
{
	if (InHitSize)
		*InHitSize = 0;

	Stats.DrawCalls = 0;
	Stats.ComplexSurfaces = 0;
	Stats.GouraudPolygons = 0;
	Stats.Tiles = 0;
	Stats.Uploads = 0;
	Stats.RectUploads = 0;
	Stats.BuffersUsed = 0;

	CurrentSizeX = ExternalPresentationActive ? ExternalViews[0].Width : Viewport->GetNativePixelWidth();
	CurrentSizeY = ExternalPresentationActive ? ExternalViews[0].Height : Viewport->GetNativePixelHeight();

	nulltex = Textures->GetNullTexture();

	ConfigureDepthBuffer(CurrentSizeX, CurrentSizeY);
	if (ExternalPresentationActive)
	{
		CurrentExternalView = 0;
		CurrentSurfaceTexture = nullptr;
		CurrentSurfaceView = static_cast<WebGPUPresentationImageHandle*>(ExternalViews[0].NativeHandle)->View;
	}
	else
	{
		Context->ConfigureSurface(CurrentSizeX, CurrentSizeY);
		WGPUSurfaceTexture surfaceTexture = {};
		wgpuSurfaceGetCurrentTexture(Context->Surface, &surfaceTexture);
		CurrentSurfaceTexture = surfaceTexture.texture;

		WGPUTextureViewDescriptor viewDesc = {};
		viewDesc.format = Context->SurfaceFormat;
		viewDesc.dimension = WGPUTextureViewDimension_2D;
		viewDesc.baseMipLevel = 0;
		viewDesc.mipLevelCount = 1;
		viewDesc.baseArrayLayer = 0;
		viewDesc.arrayLayerCount = 1;
		viewDesc.aspect = WGPUTextureAspect_All;
		CurrentSurfaceView = wgpuTextureCreateView(CurrentSurfaceTexture, &viewDesc);
	}

	CurrentClearColor = ScreenClear;
	HaveViewport = false;
	BeginFramePass(/*colorClear=*/true, ScreenClear, /*depthClear=*/true);

	FlashScale = InFlashScale;
	FlashFog = InFlashFog;

	SceneVertexPos = 0;
	SceneIndexPos = 0;
	UploadedVertexPos = 0;
	UploadedIndexPos = 0;
	Batch = WebGPUDrawBatchEntry();
	QueuedBatches.clear();

	IsLocked = true;
}

void WebGPURenderDevice::Unlock(bool Blit)
{
	(void)Blit; // No off-screen buffer to blit from - see class comment (M2 scope trim).

	if (!IsLocked)
		return;

	DrawBatches();
	EndAndSubmitFramePass();

	// No wgpuSurfacePresent() call: on Emscripten, a canvas-backed WGPUSurface
	// presents implicitly once the current requestAnimationFrame callback
	// returns control to the browser. wgpuSurfacePresent() is hard-
	// unimplemented in this port (always aborts) - confirmed via a
	// standalone spike before writing this backend.
	if (!ExternalPresentationActive)
		wgpuTextureViewRelease(CurrentSurfaceView);
	CurrentSurfaceView = nullptr;
	if (CurrentSurfaceTexture)
		wgpuTextureRelease(CurrentSurfaceTexture);
	CurrentSurfaceTexture = nullptr;

	Batch = WebGPUDrawBatchEntry();
	HaveViewport = false;
	IsLocked = false;
}

void WebGPURenderDevice::PushHit(const uint8_t* Data, int Count)
{
	// Hit-testing is editor-only functionality, already excluded from the
	// Emscripten build (see WebGPUShaders.h). No-op here.
}

void WebGPURenderDevice::PopHit(int Count, bool bForce)
{
}

void WebGPURenderDevice::ReadPixels(FColor* Pixels)
{
	// WebGPU buffer/texture readback is async-only (mapAsync) and this
	// engine's ReadPixels() is a synchronous pure virtual; Asyncify is
	// deliberately not used in this build. Render verification instead uses a JS-side canvas
	// capture (see web/smoke_test_webgpu.py) rather than this in-engine
	// path, which only backs the in-game screenshot console command -
	// unimplemented for now under WebGPU.
	if (Pixels)
		std::memset(Pixels, 0, (size_t)Viewport->GetNativePixelWidth() * (size_t)Viewport->GetNativePixelHeight() * sizeof(FColor));
}

vec4 WebGPURenderDevice::ApplyInverseGamma(vec4 color)
{
	if (IsOrtho)
		return color;
	float brightness = clamp(Brightness * 2.0f, 0.05f, 2.99f);
	float gammaRed = std::max(brightness + GammaOffset + GammaOffsetRed, 0.001f);
	float gammaGreen = std::max(brightness + GammaOffset + GammaOffsetGreen, 0.001f);
	float gammaBlue = std::max(brightness + GammaOffset + GammaOffsetBlue, 0.001f);
	return vec4(pow(color.r, gammaRed), pow(color.g, gammaGreen), pow(color.b, gammaBlue), color.a);
}

void WebGPURenderDevice::UpdateSceneUniforms(const mat4& objectToProjection)
{
	WebGPUSceneUniforms uniforms;
	std::memcpy(uniforms.ObjectToProjection, objectToProjection.matrix, sizeof(float) * 16);
	wgpuQueueWriteBuffer(Context->Queue, UniformBuffer, 0, &uniforms, sizeof(uniforms));
}

void WebGPURenderDevice::EndFlash()
{
	if (FlashScale != vec4(0.5f, 0.5f, 0.5f, 0.0f) || FlashFog != vec4(0.0f, 0.0f, 0.0f, 0.0f))
	{
		DrawBatches(true);

		vec4 color(FlashFog.x, FlashFog.y, FlashFog.z, 1.0f - std::min(FlashScale.x * 2.0f, 1.0f));

		UpdateSceneUniforms(mat4::identity());

		SetPipeline(PF_Highlighted);
		SetDescriptorSet(0);

		auto alloc = ReserveVertices(4, 6);
		if (alloc.vptr)
		{
			WebGPUSceneVertex* vptr = alloc.vptr;
			uint32_t* iptr = alloc.iptr;
			uint32_t vpos = alloc.vpos;

			auto setVert = [&](WebGPUSceneVertex& v, float x, float y)
			{
				v.Flags = 0;
				v.Position[0] = x; v.Position[1] = y; v.Position[2] = 0.0f;
				v.TexCoord[0] = 0.0f; v.TexCoord[1] = 0.0f;
				v.TexCoord2[0] = 0.0f; v.TexCoord2[1] = 0.0f;
				v.TexCoord3[0] = 0.0f; v.TexCoord3[1] = 0.0f;
				v.TexCoord4[0] = 0.0f; v.TexCoord4[1] = 0.0f;
				v.Color[0] = color.r; v.Color[1] = color.g; v.Color[2] = color.b; v.Color[3] = color.a;
			};
			setVert(vptr[0], -1.0f, -1.0f);
			setVert(vptr[1], 1.0f, -1.0f);
			setVert(vptr[2], 1.0f, 1.0f);
			setVert(vptr[3], -1.0f, 1.0f);

			iptr[0] = vpos; iptr[1] = vpos + 1; iptr[2] = vpos + 2;
			iptr[3] = vpos; iptr[4] = vpos + 2; iptr[5] = vpos + 3;

			UseVertices(4, 6);
		}

		DrawBatches(true);
		if (CurrentFrame)
			SetSceneNode(CurrentFrame);
	}
}

void WebGPURenderDevice::SetSceneNode(FSceneNode* Frame)
{
	DrawBatches(true);

	CurrentFrame = Frame;
	Aspect = Frame->FY / Frame->FX;
	RProjZ = (float)std::tan(radians(Frame->FovAngle) * 0.5);
	RFX2 = 2.0f * RProjZ / Frame->FX;
	RFY2 = 2.0f * RProjZ * Aspect / Frame->FY;

	ViewportX = (float)Frame->XB;
	ViewportY = (float)(CurrentSizeY - Frame->YB - Frame->Y);
	ViewportW = (float)Frame->X;
	ViewportH = (float)Frame->Y;
	ViewportMinDepth = 0.1f;
	ViewportMaxDepth = 1.0f;
	HaveViewport = true;
	wgpuRenderPassEncoderSetViewport(FramePass, ViewportX, ViewportY, ViewportW, ViewportH, ViewportMinDepth, ViewportMaxDepth);

	UpdateSceneUniforms(Frame->Projection * Frame->WorldToView * Frame->ObjectToWorld);
}

void WebGPURenderDevice::PrecacheTexture(FTextureInfo& Info, uint32_t PolyFlags)
{
	PolyFlags = ApplyPrecedenceRules(PolyFlags);
	Textures->GetTexture(&Info, !!(PolyFlags & PF_Masked));
}

bool WebGPURenderDevice::SupportsTextureFormat(TextureFormat Format)
{
	return Uploads->SupportsTextureFormat(Format);
}

void WebGPURenderDevice::UpdateTextureRect(FTextureInfo& Info, int U, int V, int UL, int VL)
{
	Textures->UpdateTextureRect(&Info, U, V, UL, VL);
}

void WebGPURenderDevice::ClearZ()
{
	// WebGPU can only clear depth via a render pass's loadOp, so this forces
	// a submit boundary (preserving already-drawn color via loadOp=Load) and
	// reopens the pass with a fresh depth clear.
	DrawBatches(true, /*clearDepthOnReopen=*/true);
}

WebGPURenderDevice::VertexReserveInfo WebGPURenderDevice::ReserveVertices(size_t vcount, size_t icount)
{
	if (SceneVertexPos + vcount > VertexBufferCapacity || SceneIndexPos + icount > IndexBufferCapacity)
	{
		if (vcount > VertexBufferCapacity || icount > IndexBufferCapacity)
			return { nullptr, nullptr, 0 };

		DrawBatches(true);
	}

	return { SceneVertices.data() + SceneVertexPos, SceneIndexes.data() + SceneIndexPos, (uint32_t)SceneVertexPos };
}

void WebGPURenderDevice::SetPipeline(WebGPUScenePipelineState* pipeline)
{
	if (pipeline != Batch.Pipeline)
	{
		AddDrawBatch();
		Batch.Pipeline = pipeline;
	}
}

void WebGPURenderDevice::SetPipeline(uint32_t PolyFlags)
{
	WebGPUScenePipelineState* pipeline = Pipelines->GetPipeline(PolyFlags);
	if (pipeline != Batch.Pipeline)
	{
		AddDrawBatch();
		Batch.Pipeline = pipeline;
	}
}

void WebGPURenderDevice::SetDescriptorSet(uint32_t PolyFlags, WebGPUCachedTexture* tex, bool clamp)
{
	if (!tex) tex = nulltex;

	uint32_t samplermode = 0;
	if (PolyFlags & PF_NoSmooth) samplermode |= 1;
	if (clamp) samplermode |= 2;
	samplermode |= (tex->DummyMipmapCount << 2);

	if (Batch.Tex != tex || Batch.TexSamplerMode != samplermode || Batch.Lightmap != nulltex || Batch.Detailtex != nulltex || Batch.DetailtexSamplerMode != 0 || Batch.Macrotex != nulltex || Batch.MacrotexSamplerMode != 0)
	{
		AddDrawBatch();
		Batch.Tex = tex;
		Batch.Lightmap = nulltex;
		Batch.Detailtex = nulltex;
		Batch.Macrotex = nulltex;
		Batch.TexSamplerMode = samplermode;
		Batch.DetailtexSamplerMode = 0;
		Batch.MacrotexSamplerMode = 0;
	}
}

void WebGPURenderDevice::SetDescriptorSet(uint32_t PolyFlags, const ComplexSurfaceInfo& info)
{
	uint32_t samplermode = 0;
	if (PolyFlags & PF_NoSmooth) samplermode |= 1;
	samplermode |= (info.tex->DummyMipmapCount << 2);

	uint32_t detailsamplermode = info.detailtex->DummyMipmapCount << 2;
	uint32_t macrosamplermode = info.macrotex->DummyMipmapCount << 2;

	if (Batch.Tex != info.tex || Batch.TexSamplerMode != samplermode || Batch.Lightmap != info.lightmap || Batch.Detailtex != info.detailtex || Batch.DetailtexSamplerMode != detailsamplermode || Batch.Macrotex != info.macrotex || Batch.MacrotexSamplerMode != macrosamplermode)
	{
		AddDrawBatch();
		Batch.Tex = info.tex;
		Batch.Lightmap = info.lightmap;
		Batch.Detailtex = info.detailtex;
		Batch.Macrotex = info.macrotex;
		Batch.TexSamplerMode = samplermode;
		Batch.DetailtexSamplerMode = detailsamplermode;
		Batch.MacrotexSamplerMode = macrosamplermode;
	}
}

void WebGPURenderDevice::AddDrawBatch()
{
	if (Batch.SceneIndexStart != SceneIndexPos)
	{
		Batch.SceneIndexEnd = SceneIndexPos;
		QueuedBatches.push_back(Batch);
		Batch.SceneIndexStart = SceneIndexPos;
	}
}

void WebGPURenderDevice::UploadPendingSceneData()
{
	if (SceneVertexPos > UploadedVertexPos)
	{
		size_t count = SceneVertexPos - UploadedVertexPos;
		wgpuQueueWriteBuffer(Context->Queue, VertexBuffer, UploadedVertexPos * sizeof(WebGPUSceneVertex), SceneVertices.data() + UploadedVertexPos, count * sizeof(WebGPUSceneVertex));
		UploadedVertexPos = SceneVertexPos;
	}

	if (SceneIndexPos > UploadedIndexPos)
	{
		size_t count = SceneIndexPos - UploadedIndexPos;
		wgpuQueueWriteBuffer(Context->Queue, IndexBuffer, UploadedIndexPos * sizeof(uint32_t), SceneIndexes.data() + UploadedIndexPos, count * sizeof(uint32_t));
		UploadedIndexPos = SceneIndexPos;
	}
}

void WebGPURenderDevice::DrawBatches(bool submitBoundary, bool clearDepthOnReopen)
{
	AddDrawBatch();

	if (!QueuedBatches.empty())
	{
		UploadPendingSceneData();

		for (const WebGPUDrawBatchEntry& entry : QueuedBatches)
			DrawEntry(entry);
		QueuedBatches.clear();
	}

	if (submitBoundary)
	{
		EndAndSubmitFramePass();
		BeginFramePass(/*colorClear=*/false, CurrentClearColor, clearDepthOnReopen);

		SceneVertexPos = 0;
		SceneIndexPos = 0;
		UploadedVertexPos = 0;
		UploadedIndexPos = 0;
		Stats.BuffersUsed++;
	}

	Batch.SceneIndexStart = SceneIndexPos;
}

void WebGPURenderDevice::DrawEntry(const WebGPUDrawBatchEntry& entry)
{
	size_t icount = entry.SceneIndexEnd - entry.SceneIndexStart;
	if (icount == 0)
		return;

	if (ViewportMinDepth != entry.Pipeline->MinDepth || ViewportMaxDepth != entry.Pipeline->MaxDepth)
	{
		ViewportMinDepth = entry.Pipeline->MinDepth;
		ViewportMaxDepth = entry.Pipeline->MaxDepth;
		wgpuRenderPassEncoderSetViewport(FramePass, ViewportX, ViewportY, ViewportW, ViewportH, ViewportMinDepth, ViewportMaxDepth);
	}

	// A fresh bind group is created per draw call rather than cached, for
	// simplicity. The implementation
	// keeps its own internal reference for the command's lifetime, so it is
	// safe to release immediately after the SetBindGroup call below.
	WGPUBindGroupEntry textureEntries[8] = {};
	textureEntries[0] = { nullptr, 0, nullptr, 0, 0, Samplers->Get(entry.TexSamplerMode), nullptr };
	textureEntries[1] = { nullptr, 1, nullptr, 0, 0, Samplers->Get(0), nullptr };
	textureEntries[2] = { nullptr, 2, nullptr, 0, 0, Samplers->Get(entry.MacrotexSamplerMode), nullptr };
	textureEntries[3] = { nullptr, 3, nullptr, 0, 0, Samplers->Get(entry.DetailtexSamplerMode), nullptr };
	textureEntries[4] = { nullptr, 4, nullptr, 0, 0, nullptr, entry.Tex->View };
	textureEntries[5] = { nullptr, 5, nullptr, 0, 0, nullptr, entry.Lightmap->View };
	textureEntries[6] = { nullptr, 6, nullptr, 0, 0, nullptr, entry.Macrotex->View };
	textureEntries[7] = { nullptr, 7, nullptr, 0, 0, nullptr, entry.Detailtex->View };

	WGPUBindGroupDescriptor bindGroupDesc = {};
	bindGroupDesc.layout = Pipelines->TexturesBindGroupLayout;
	bindGroupDesc.entryCount = 8;
	bindGroupDesc.entries = textureEntries;
	WGPUBindGroup texturesBindGroup = wgpuDeviceCreateBindGroup(Context->Device, &bindGroupDesc);

	WGPUColor blendConstant = { entry.BlendConstants[0], entry.BlendConstants[1], entry.BlendConstants[2], entry.BlendConstants[3] };
	wgpuRenderPassEncoderSetBlendConstant(FramePass, &blendConstant);

	wgpuRenderPassEncoderSetPipeline(FramePass, entry.Pipeline->Pipeline);
	wgpuRenderPassEncoderSetBindGroup(FramePass, 0, UniformsBindGroup, 0, nullptr);
	wgpuRenderPassEncoderSetBindGroup(FramePass, 1, texturesBindGroup, 0, nullptr);
	wgpuRenderPassEncoderSetVertexBuffer(FramePass, 0, VertexBuffer, 0, WGPU_WHOLE_SIZE);
	wgpuRenderPassEncoderSetIndexBuffer(FramePass, IndexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
	wgpuRenderPassEncoderDrawIndexed(FramePass, (uint32_t)icount, 1, (uint32_t)entry.SceneIndexStart, 0, 0);

	wgpuBindGroupRelease(texturesBindGroup);

	Stats.DrawCalls++;
}

void WebGPURenderDevice::DrawComplexSurface(FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet)
{
	uint32_t PolyFlags = ApplyPrecedenceRules(Surface.PolyFlags);

	ComplexSurfaceInfo info;
	info.facet = &Facet;
	info.tex = Textures->GetTexture(Surface.Texture, !!(PolyFlags & PF_Masked));
	info.lightmap = Textures->GetTexture(Surface.LightMap, false);
	info.macrotex = Textures->GetTexture(Surface.MacroTexture, false);
	info.detailtex = Textures->GetTexture(Surface.DetailTexture, false);
	info.fogmap = (Surface.FogMap && Surface.FogMap->NumMips > 0 && !Surface.FogMap->Mips[0].Data.empty()) ?
		Textures->GetTexture(Surface.FogMap, false) : nulltex;
	info.editorcolor = nullptr;

	if (Surface.DetailTexture && Surface.FogMap) info.detailtex = nulltex;

	if (info.fogmap != nulltex)
		info.detailtex = info.fogmap;

	SetPipeline(PolyFlags);
	SetDescriptorSet(PolyFlags, info);

	DrawComplexSurfaceFaces(info);

	Stats.ComplexSurfaces++;
}

void WebGPURenderDevice::DrawComplexSurfaceFaces(const ComplexSurfaceInfo& info)
{
	if (info.facet->VertexCount < 3)
		return;

	uint32_t flags = 0;
	if (info.lightmap != nulltex) flags |= 1;
	if (info.macrotex != nulltex) flags |= 2;
	if (info.detailtex != nulltex && info.fogmap == nulltex) flags |= 4;
	if (info.fogmap != nulltex) flags |= 8;
	if (LightMode == 1) flags |= 64;

	vec3 xaxis = info.facet->MapCoords.XAxis;
	vec3 yaxis = info.facet->MapCoords.YAxis;
	float UDot = dot(xaxis, info.facet->MapCoords.Origin);
	float VDot = dot(yaxis, info.facet->MapCoords.Origin);

	float UPan = UDot + info.tex->PanX;
	float VPan = VDot + info.tex->PanY;
	float LMUPan = UDot + info.lightmap->PanX - 0.5f * info.lightmap->UScale;
	float LMVPan = VDot + info.lightmap->PanY - 0.5f * info.lightmap->VScale;
	float MacroUPan = UDot + info.macrotex->PanX;
	float MacroVPan = VDot + info.macrotex->PanY;
	float DetailUPan = UDot + (info.fogmap == nulltex ? info.detailtex->PanX : info.fogmap->PanX - 0.5f * info.fogmap->UScale);
	float DetailVPan = VDot + (info.fogmap == nulltex ? info.detailtex->PanY : info.fogmap->PanY - 0.5f * info.fogmap->VScale);

	float UMult = info.tex->UMult;
	float VMult = info.tex->VMult;
	float LMUMult = info.lightmap->UMult;
	float LMVMult = info.lightmap->VMult;
	float MacroUMult = info.macrotex->UMult;
	float MacroVMult = info.macrotex->VMult;
	float DetailUMult = info.fogmap == nulltex ? info.detailtex->UMult : info.fogmap->UMult;
	float DetailVMult = info.fogmap == nulltex ? info.detailtex->VMult : info.fogmap->VMult;

	vec4 color = info.editorcolor ? *info.editorcolor : vec4(1.0f);

	auto pts = info.facet->Vertices;
	uint32_t vcount = info.facet->VertexCount;
	uint32_t icount = (vcount - 2) * 3;

	auto alloc = ReserveVertices(vcount, icount);
	if (alloc.vptr)
	{
		WebGPUSceneVertex* vptr = alloc.vptr;
		uint32_t* iptr = alloc.iptr;
		uint32_t vpos = alloc.vpos;

		for (uint32_t i = 0; i < vcount; i++)
		{
			vec3 point = pts[i];
			float u = dot(xaxis, point);
			float v = dot(yaxis, point);

			vptr->Flags = flags;
			vptr->Position[0] = point.x;
			vptr->Position[1] = point.y;
			vptr->Position[2] = point.z;
			vptr->TexCoord[0] = (u - UPan) * UMult;
			vptr->TexCoord[1] = (v - VPan) * VMult;
			vptr->TexCoord2[0] = (u - LMUPan) * LMUMult;
			vptr->TexCoord2[1] = (v - LMVPan) * LMVMult;
			vptr->TexCoord3[0] = (u - MacroUPan) * MacroUMult;
			vptr->TexCoord3[1] = (v - MacroVPan) * MacroVMult;
			vptr->TexCoord4[0] = (u - DetailUPan) * DetailUMult;
			vptr->TexCoord4[1] = (v - DetailVPan) * DetailVMult;
			vptr->Color[0] = color.r;
			vptr->Color[1] = color.g;
			vptr->Color[2] = color.b;
			vptr->Color[3] = color.a;
			vptr++;
		}

		for (uint32_t i = vpos + 2; i < vpos + vcount; i++)
		{
			*(iptr++) = vpos;
			*(iptr++) = i - 1;
			*(iptr++) = i;
		}

		UseVertices(vcount, icount);
	}
}

void WebGPURenderDevice::DrawGouraudPolygon(FSceneNode* Frame, FTextureInfo& Info, const GouraudVertex* Pts, int NumPts, uint32_t PolyFlags)
{
	if (NumPts < 3) return; // This can apparently happen!!

	PolyFlags = ApplyPrecedenceRules(PolyFlags);

	WebGPUCachedTexture* tex = Textures->GetTexture(&Info, !!(PolyFlags & PF_Masked));

	SetPipeline(PolyFlags);
	SetDescriptorSet(PolyFlags, tex);

	float UMult = tex->UMult;
	float VMult = tex->VMult;

	uint32_t flags = (PolyFlags & (PF_RenderFog | PF_Translucent | PF_Modulated)) == PF_RenderFog ? 16 : 0;
	if ((PolyFlags & (PF_Translucent | PF_Modulated)) == 0 && LightMode == 2) flags |= 32;

	bool modulated = (PolyFlags & PF_Modulated) != 0;

	auto alloc = ReserveVertices(NumPts, (NumPts - 2) * 3);
	if (alloc.vptr)
	{
		WebGPUSceneVertex* vptr = alloc.vptr;
		uint32_t* iptr = alloc.iptr;
		uint32_t vpos = alloc.vpos;

		for (int i = 0; i < NumPts; i++)
		{
			const GouraudVertex* P = Pts + i;
			WebGPUSceneVertex& vertex = vptr[i];
			vertex.Flags = flags;
			vertex.Position[0] = P->Point.x;
			vertex.Position[1] = P->Point.y;
			vertex.Position[2] = P->Point.z;
			vertex.TexCoord[0] = P->UV.x * UMult;
			vertex.TexCoord[1] = P->UV.y * VMult;
			vertex.TexCoord2[0] = P->Fog.x;
			vertex.TexCoord2[1] = P->Fog.y;
			vertex.TexCoord3[0] = P->Fog.z;
			vertex.TexCoord3[1] = P->Fog.w;
			vertex.TexCoord4[0] = 0.0f;
			vertex.TexCoord4[1] = 0.0f;
			if (modulated)
			{
				vertex.Color[0] = 1.0f;
				vertex.Color[1] = 1.0f;
				vertex.Color[2] = 1.0f;
			}
			else
			{
				vertex.Color[0] = P->Light.x;
				vertex.Color[1] = P->Light.y;
				vertex.Color[2] = P->Light.z;
			}
			vertex.Color[3] = 1.0f;
		}

		uint32_t vstart = vpos;
		uint32_t vcount = (uint32_t)NumPts;
		for (uint32_t i = vstart + 2; i < vstart + vcount; i++)
		{
			*(iptr++) = vstart;
			*(iptr++) = i - 1;
			*(iptr++) = i;
		}

		UseVertices(NumPts, (NumPts - 2) * 3);
	}

	Stats.GouraudPolygons++;
}

void WebGPURenderDevice::DrawTile(FSceneNode* Frame, FTextureInfo& Info, float X, float Y, float XL, float YL, float U, float V, float UL, float VL, float Z, vec4 Color, vec4 Fog, uint32_t PolyFlags)
{
	PolyFlags = ApplyPrecedenceRules(PolyFlags);

	if (PolyFlags == PF_SubpixelFont)
	{
		AddDrawBatch();
		Batch.BlendConstants[0] = Color.x;
		Batch.BlendConstants[1] = Color.y;
		Batch.BlendConstants[2] = Color.z;
		Batch.BlendConstants[3] = Color.w;
		Color = vec4(1.0f);
	}

	WebGPUCachedTexture* tex = Textures->GetTexture(&Info, !!(PolyFlags & PF_Masked));
	float UMult = tex->UMult;
	float VMult = tex->VMult;
	float u0 = U * UMult;
	float v0 = V * VMult;
	float u1 = (U + UL) * UMult;
	float v1 = (V + VL) * VMult;
	bool clamp = (u0 >= 0.0f && u1 <= 1.00001f && v0 >= 0.0f && v1 <= 1.00001f);

	SetPipeline(PolyFlags);
	SetDescriptorSet(PolyFlags, tex, clamp);

	auto alloc = ReserveVertices(4, 6);
	if (alloc.vptr)
	{
		WebGPUSceneVertex* vptr = alloc.vptr;
		uint32_t* iptr = alloc.iptr;
		uint32_t vpos = alloc.vpos;

		float r, g, b, a;
		if (PolyFlags & PF_Modulated)
		{
			r = 1.0f;
			g = 1.0f;
			b = 1.0f;
		}
		else
		{
			r = Color.x;
			g = Color.y;
			b = Color.z;
		}
		a = 1.0f;

		float rfx2z = RFX2 * Z;
		float rfy2z = RFY2 * Z;
		X -= Frame->FX2;
		Y -= Frame->FY2;
		XL += X;
		YL += Y;
		U *= UMult;
		UL = U + UL * UMult;
		V *= VMult;
		VL = V + VL * VMult;

		auto setVert = [&](WebGPUSceneVertex& v, float x, float y, float u, float vv)
		{
			v.Flags = 0;
			v.Position[0] = rfx2z * x; v.Position[1] = rfy2z * y; v.Position[2] = Z;
			v.TexCoord[0] = u; v.TexCoord[1] = vv;
			v.TexCoord2[0] = 0.0f; v.TexCoord2[1] = 0.0f;
			v.TexCoord3[0] = 0.0f; v.TexCoord3[1] = 0.0f;
			v.TexCoord4[0] = 0.0f; v.TexCoord4[1] = 0.0f;
			v.Color[0] = r; v.Color[1] = g; v.Color[2] = b; v.Color[3] = a;
		};

		setVert(vptr[0], X, Y, U, V);
		setVert(vptr[1], XL, Y, UL, V);
		setVert(vptr[2], XL, YL, UL, VL);
		setVert(vptr[3], X, YL, U, VL);

		iptr[0] = vpos; iptr[1] = vpos + 1; iptr[2] = vpos + 2;
		iptr[3] = vpos; iptr[4] = vpos + 2; iptr[5] = vpos + 3;

		UseVertices(4, 6);
	}

	Stats.Tiles++;
}

void WebGPURenderDevice::Draw3DLine(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2)
{
	if (IsOrtho)
	{
		P1.x = (P1.x) / Frame->Zoom + Frame->FX2;
		P1.y = (P1.y) / Frame->Zoom + Frame->FY2;
		P1.z = 1;
		P2.x = (P2.x) / Frame->Zoom + Frame->FX2;
		P2.y = (P2.y) / Frame->Zoom + Frame->FY2;
		P2.z = 1;

		if (std::abs(P2.x - P1.x) + std::abs(P2.y - P1.y) >= 0.2)
		{
			Draw2DLine(Frame, Color, LineFlags, P1, P2);
		}
		else if (IsOrthoLowDetail)
		{
			Draw2DPoint(Frame, Color, LINE_None, P1.x - 1, P1.y - 1, P1.x + 1, P1.y + 1, P1.z);
		}
	}
	else
	{
		bool occlude = !!(LineFlags & LINE_DepthCued);
		SetPipeline(&Pipelines->LinePipeline[occlude]);
		SetDescriptorSet(PF_Highlighted);
		vec4 color = ApplyInverseGamma(vec4(Color.x, Color.y, Color.z, 1.0f));

		auto alloc = ReserveVertices(2, 2);
		if (alloc.vptr)
		{
			WebGPUSceneVertex* vptr = alloc.vptr;
			uint32_t* iptr = alloc.iptr;
			uint32_t vpos = alloc.vpos;

			vptr[0] = { 0, { P1.x, P1.y, P1.z }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { color.r, color.g, color.b, color.a } };
			vptr[1] = { 0, { P2.x, P2.y, P2.z }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { color.r, color.g, color.b, color.a } };

			iptr[0] = vpos;
			iptr[1] = vpos + 1;

			UseVertices(2, 2);
		}
	}
}

void WebGPURenderDevice::Draw2DLine(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2)
{
	bool occlude = !!(LineFlags & LINE_DepthCued);
	SetPipeline(&Pipelines->LinePipeline[occlude]);
	SetDescriptorSet(PF_Highlighted);
	vec4 color = ApplyInverseGamma(vec4(Color.x, Color.y, Color.z, 1.0f));

	auto alloc = ReserveVertices(2, 2);
	if (alloc.vptr)
	{
		WebGPUSceneVertex* vptr = alloc.vptr;
		uint32_t* iptr = alloc.iptr;
		uint32_t vpos = alloc.vpos;

		vptr[0] = { 0, { RFX2 * P1.z * (P1.x - Frame->FX2), RFY2 * P1.z * (P1.y - Frame->FY2), P1.z }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { color.r, color.g, color.b, color.a } };
		vptr[1] = { 0, { RFX2 * P2.z * (P2.x - Frame->FX2), RFY2 * P2.z * (P2.y - Frame->FY2), P2.z }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { color.r, color.g, color.b, color.a } };

		iptr[0] = vpos;
		iptr[1] = vpos + 1;

		UseVertices(2, 2);
	}
}

void WebGPURenderDevice::Draw2DPoint(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, float X1, float Y1, float X2, float Y2, float Z)
{
	bool occlude = !!(LineFlags & LINE_DepthCued);
	SetPipeline(&Pipelines->PointPipeline[occlude]);
	SetDescriptorSet(PF_Highlighted);
	vec4 color = ApplyInverseGamma(vec4(Color.x, Color.y, Color.z, 1.0f));

	auto alloc = ReserveVertices(4, 6);
	if (alloc.vptr)
	{
		WebGPUSceneVertex* vptr = alloc.vptr;
		uint32_t* iptr = alloc.iptr;
		uint32_t vpos = alloc.vpos;

		vptr[0] = { 0, { RFX2 * Z * (X1 - Frame->FX2 - 0.5f), RFY2 * Z * (Y1 - Frame->FY2 - 0.5f), Z }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { color.r, color.g, color.b, color.a } };
		vptr[1] = { 0, { RFX2 * Z * (X2 - Frame->FX2 + 0.5f), RFY2 * Z * (Y1 - Frame->FY2 - 0.5f), Z }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { color.r, color.g, color.b, color.a } };
		vptr[2] = { 0, { RFX2 * Z * (X2 - Frame->FX2 + 0.5f), RFY2 * Z * (Y2 - Frame->FY2 + 0.5f), Z }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { color.r, color.g, color.b, color.a } };
		vptr[3] = { 0, { RFX2 * Z * (X1 - Frame->FX2 - 0.5f), RFY2 * Z * (Y2 - Frame->FY2 + 0.5f), Z }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { color.r, color.g, color.b, color.a } };

		iptr[0] = vpos;
		iptr[1] = vpos + 1;
		iptr[2] = vpos + 2;
		iptr[3] = vpos;
		iptr[4] = vpos + 2;
		iptr[5] = vpos + 3;

		UseVertices(4, 6);
	}
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/em_js.h>

// The pinned emdawnwebgpu API only offers uncaptured-error reporting via
// WGPUDeviceDescriptor.uncapturedErrorCallbackInfo at device-creation time
// (no post-hoc wgpuDeviceSetUncapturedErrorCallback in this version), but
// the WGPUDevice here is created in JS before main() runs (see
// WebGPUContext.h) - too early for C++ to install a callback. JS's own
// GPUDevice 'uncapturederror' event isn't subject to that limitation, so
// index_webgpu.html counts errors into window.surrealWebGPUErrorCount and
// this just reads it back, matching Surreal_GetTickCount's ccall shape so
// the smoke test can query all M2 stats uniformly.
EM_JS(int, JS_GetWebGPUErrorCount, (), {
	return (typeof window.surrealWebGPUErrorCount === "number") ? window.surrealWebGPUErrorCount : 0;
});

extern "C"
{
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGPUErrorCount()
	{
		return JS_GetWebGPUErrorCount();
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGPUDrawCalls()
	{
		return CurrentDevice ? CurrentDevice->Stats.DrawCalls : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGPUTextureCount()
	{
		return (CurrentDevice && CurrentDevice->Textures) ? CurrentDevice->Textures->GetTexturesInCache() : 0;
	}

	// Temporary diagnostics for the DrawComplexSurface/DrawGouraudPolygon
	// orientation investigation (Docs/VR/PLAN.md) - lets the scratch harness
	// confirm which draw function actually produced the on-screen content
	// being inspected, instead of guessing from draw-call count alone.
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGPUGouraudPolygons()
	{
		return CurrentDevice ? CurrentDevice->Stats.GouraudPolygons : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGPUComplexSurfaces()
	{
		return CurrentDevice ? CurrentDevice->Stats.ComplexSurfaces : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGPUTiles()
	{
		return CurrentDevice ? CurrentDevice->Stats.Tiles : 0;
	}
}
#endif
