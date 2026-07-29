#pragma once

#include "RenderDevice/RenderDevice.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "WebGPUContext.h"
#include "WebGPUPipelineCache.h"
#include "WebGPUSamplerCache.h"
#include "WebGPUTextureManager.h"
#include "WebGPUUploadManager.h"
#include "WebGPUCachedTexture.h"
#include "Math/mat.h"
#include <memory>
#include <map>
#include <vector>

struct WebGPUDrawBatchEntry
{
	size_t SceneIndexStart = 0;
	size_t SceneIndexEnd = 0;
	WebGPUScenePipelineState* Pipeline = nullptr;
	WebGPUCachedTexture* Tex = nullptr;
	WebGPUCachedTexture* Lightmap = nullptr;
	WebGPUCachedTexture* Detailtex = nullptr;
	WebGPUCachedTexture* Macrotex = nullptr;
	uint32_t TexSamplerMode = 0;
	uint32_t DetailtexSamplerMode = 0;
	uint32_t MacrotexSamplerMode = 0;
	float BlendConstants[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
};

// Opaque payload stored in PresentationTargetImage::NativeHandle by the
// WebXR/WebGPU provider. Other render backends define their own payload.
struct WebGPUPresentationImageHandle
{
	WGPUTextureView View = nullptr;
	WGPUTextureFormat Format = WGPUTextureFormat_Undefined;
};

// WebGPU RenderDevice backend for the Emscripten browser platform. Mirrors
// D3D11RenderDevice's traditional fixed-texture-slot draw model rather than
// VulkanRenderDevice's bindless one - core WebGPU has no stable equivalent of
// Vulkan's update-after-bind variable-count descriptor arrays. See
// the existing RenderDevice contract.
//
// Deliberate M2 scope trims vs D3D11RenderDevice (all documented at their
// point of use in the .cpp): no post-processing, bloom, HDR, multisampling, or
// hit-testing (PushHit/PopHit are no-ops - editor-only, already excluded
// from the Emscripten build), no synchronous screenshot readback (WebGPU
// buffer mapping is async-only and the RenderDevice API has no async bridge).
//
// Structural deviation from D3D11's per-draw immediate-context model: WebGPU
// only guarantees a buffer write is visible to commands submitted *after*
// the write, not to commands already encoded-but-unsubmitted at an earlier
// buffer generation. So unlike D3D11 (where re-mapping the vertex buffer or
// updating the scene constant buffer mid-frame is safe because the
// immediate context executes prior Draw calls before the update instruction
// runs), this backend must end the current render pass and submit a command
// buffer *before* it is safe to overwrite the vertex/index buffers or the
// uniform buffer - see DrawBatches()'s submitBoundary parameter.
class WebGPURenderDevice : public RenderDevice
{
public:
	WebGPURenderDevice(Widget* viewport);
	~WebGPURenderDevice() override;

	void Flush(bool AllowPrecache) override;
	void Lock(vec4 FlashScale, vec4 FlashFog, vec4 ScreenClear, uint8_t* HitData, int* HitSize) override;
	void Unlock(bool Blit) override;
	void DrawComplexSurface(FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet) override;
	void DrawGouraudPolygon(FSceneNode* Frame, FTextureInfo& Info, const GouraudVertex* Pts, int NumPts, uint32_t PolyFlags) override;
	void DrawTile(FSceneNode* Frame, FTextureInfo& Info, float X, float Y, float XL, float YL, float U, float V, float UL, float VL, float Z, vec4 Color, vec4 Fog, uint32_t PolyFlags) override;
	void Draw3DLine(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2) override;
	void Draw2DLine(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2) override;
	void Draw2DPoint(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, float X1, float Y1, float X2, float Y2, float Z) override;
	void ClearZ() override;
	void PushHit(const uint8_t* Data, int Count) override;
	void PopHit(int Count, bool bForce) override;
	void ReadPixels(FColor* Pixels) override;
	void EndFlash() override;
	void SetSceneNode(FSceneNode* Frame) override;
	void PrecacheTexture(FTextureInfo& Info, uint32_t PolyFlags) override;
	bool SupportsTextureFormat(TextureFormat Format) override;
	void UpdateTextureRect(FTextureInfo& Info, int U, int V, int UL, int VL) override;
	bool BeginPresentationLayer(const PresentationLayerDescription& layer) override;
	bool BindPresentationTarget(const PresentationTargetBinding& binding) override;
	void UnbindPresentationTarget(PresentationTarget target) override;
	bool BeginPresentationView(PresentationTarget target, size_t viewIndex) override;
	bool IsExternalPresentationActive() const { return ExternalPresentationActive; }

	std::unique_ptr<WebGPUContext> Context;
	WebGPUPipelineCache* Pipelines = nullptr;
	std::unique_ptr<WebGPUSamplerCache> Samplers;
	std::unique_ptr<WebGPUTextureManager> Textures;
	std::unique_ptr<WebGPUUploadManager> Uploads;

	struct
	{
		int ComplexSurfaces = 0;
		int GouraudPolygons = 0;
		int Tiles = 0;
		int DrawCalls = 0;
		int Uploads = 0;
		int RectUploads = 0;
		int BuffersUsed = 0;
	} Stats;

private:
	static const WGPUTextureFormat DepthFormat = WGPUTextureFormat_Depth24Plus;
	static const size_t VertexBufferCapacity = 16 * 1024;
	static const size_t IndexBufferCapacity = 32 * 1024;

	struct ComplexSurfaceInfo
	{
		FSurfaceFacet* facet;
		WebGPUCachedTexture* tex;
		WebGPUCachedTexture* lightmap;
		WebGPUCachedTexture* macrotex;
		WebGPUCachedTexture* detailtex;
		WebGPUCachedTexture* fogmap;
		vec4* editorcolor;
	};
	void DrawComplexSurfaceFaces(const ComplexSurfaceInfo& info);

	void ConfigureDepthBuffer(int width, int height);
	void ConfigureCanvasColorBuffer(int width, int height);
	void PresentCanvas();
	bool EnsurePipelineColorFormat(WGPUTextureFormat format);
	void CreateUniformBindGroup();
	bool SelectExternalTarget(PresentationTarget target, size_t viewIndex);
	bool SelectExternalView(size_t viewIndex);
	void BeginFramePass(bool colorClear, vec4 clearColor, bool depthClear);
	void EndAndSubmitFramePass();

	void SetPipeline(WebGPUScenePipelineState* pipeline);
	void SetPipeline(uint32_t PolyFlags);
	void SetDescriptorSet(uint32_t PolyFlags, WebGPUCachedTexture* tex = nullptr, bool clamp = false);
	void SetDescriptorSet(uint32_t PolyFlags, const ComplexSurfaceInfo& info);

	void AddDrawBatch();
	void DrawBatches(bool submitBoundary = false, bool clearDepthOnReopen = false);
	void DrawEntry(const WebGPUDrawBatchEntry& entry);
	void UploadPendingSceneData();
	void UpdateSceneUniforms(const mat4& objectToProjection);

	vec4 ApplyInverseGamma(vec4 color);

	struct VertexReserveInfo
	{
		WebGPUSceneVertex* vptr;
		uint32_t* iptr;
		uint32_t vpos;
	};
	VertexReserveInfo ReserveVertices(size_t vcount, size_t icount);
	void UseVertices(size_t vcount, size_t icount) { SceneVertexPos += vcount; SceneIndexPos += icount; }

	WebGPUDrawBatchEntry Batch;
	std::vector<WebGPUDrawBatchEntry> QueuedBatches;
	WebGPUCachedTexture* nulltex = nullptr;

	std::vector<WebGPUSceneVertex> SceneVertices;
	std::vector<uint32_t> SceneIndexes;
	size_t SceneVertexPos = 0;
	size_t SceneIndexPos = 0;
	size_t UploadedVertexPos = 0;
	size_t UploadedIndexPos = 0;

	WGPUBuffer VertexBuffer = nullptr;
	WGPUBuffer IndexBuffer = nullptr;
	WGPUBuffer UniformBuffer = nullptr;
	WGPUBindGroup UniformsBindGroup = nullptr;
	std::unique_ptr<WebGPUPipelineCache> CanvasPipelines;
	std::map<WGPUTextureFormat, std::unique_ptr<WebGPUPipelineCache>> PresentationPipelines;
	std::map<WGPUTextureFormat, WGPUBindGroup> UniformBindGroups;

	WGPUTexture DepthTexture = nullptr;
	WGPUTextureView DepthView = nullptr;
	int DepthWidth = 0;
	int DepthHeight = 0;
	WGPUTexture CanvasColorTexture = nullptr;
	WGPUTextureView CanvasColorView = nullptr;
	int CanvasColorWidth = 0;
	int CanvasColorHeight = 0;

	WGPUCommandEncoder FrameEncoder = nullptr;
	WGPURenderPassEncoder FramePass = nullptr;
	WGPUTextureView CurrentSurfaceView = nullptr;
	vec4 CurrentClearColor = vec4(0.0f);
	PresentationTarget ExternalTarget;
	std::vector<PresentationTargetImage> ExternalViews;
	std::map<uint32_t, std::vector<PresentationTargetImage>> ExternalTargets;
	size_t CurrentExternalView = 0;
	WGPUTextureFormat PipelineColorFormat = WGPUTextureFormat_Undefined;
	bool ExternalPresentationActive = false;

	bool HaveViewport = false;
	float ViewportX = 0, ViewportY = 0, ViewportW = 0, ViewportH = 0, ViewportMinDepth = 0.1f, ViewportMaxDepth = 1.0f;

	FSceneNode* CurrentFrame = nullptr;
	float Aspect = 0.0f;
	float RProjZ = 0.0f;
	float RFX2 = 0.0f;
	float RFY2 = 0.0f;

	vec4 FlashScale = vec4(0.0f);
	vec4 FlashFog = vec4(0.0f);

	bool IsLocked = false;
	int CurrentSizeX = 0;
	int CurrentSizeY = 0;
};
