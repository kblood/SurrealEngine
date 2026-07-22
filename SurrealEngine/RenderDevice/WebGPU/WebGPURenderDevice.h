#pragma once

#include "RenderDevice/RenderDevice.h"
#include "UObject/ULevel.h"
#include "WebGPUContext.h"
#include "WebGPUPipelineCache.h"
#include "WebGPUSamplerCache.h"
#include "WebGPUTextureManager.h"
#include "WebGPUUploadManager.h"
#include "WebGPUCachedTexture.h"
#include "Math/mat.h"
#include <memory>
#include <unordered_map>
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

// WebGPU RenderDevice backend for the Emscripten/WebXR port (M2). Mirrors
// D3D11RenderDevice's traditional fixed-texture-slot draw model rather than
// VulkanRenderDevice's bindless one - core WebGPU has no stable equivalent of
// Vulkan's update-after-bind variable-count descriptor arrays. See
// WEBXR_IMPLEMENTATION_PLAN.md M2.
//
// Deliberate M2 scope trims vs D3D11RenderDevice (all documented at their
// point of use in the .cpp): no post-process/present pass (renders straight
// into the swapchain surface), no bloom, no HDR, no multisampling, no
// hit-testing (PushHit/PopHit are no-ops - editor-only, already excluded
// from the Emscripten build), no synchronous screenshot readback (WebGPU
// buffer mapping is async-only and Asyncify is deliberately not used here).
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
	bool SelectExternalRenderTargetLayer(uint32_t arrayLayer) override;
	void PrecacheTexture(FTextureInfo& Info, uint32_t PolyFlags) override;
	bool SupportsTextureFormat(TextureFormat Format) override;
	void UpdateTextureRect(FTextureInfo& Info, int U, int V, int UL, int VL) override;

	// Bind groups retain references to their texture views. Texture cache
	// eviction must therefore release any dependent groups before destroying
	// the views themselves. Content-only updates use the targeted form so a
	// future upload path that replaces a view remains safe too.
	void ClearTextureBindGroupCache();
	void InvalidateTextureBindGroups(WebGPUCachedTexture* texture);

	// Begin a frame backed by a browser/runtime-owned texture. Ownership of the
	// imported WGPUTexture wrapper transfers to this device on success and is
	// retained until EndExternalRenderTargetFrame(); the JavaScript GPUTexture
	// itself remains browser/WebXR-owned. textureWidth/textureHeight are the
	// full attachment extent, not an individual eye viewport.
	bool BeginExternalRenderTargetFrame(WGPUTexture texture, int textureWidth, int textureHeight, uint32_t initialArrayLayer = 0);

	// Select an array layer and physical viewport for the next eye. This may be
	// called before Lock(), or while locked between eye draws. A locked switch
	// flushes/submits the old eye pass before reopening color/depth attachments
	// for the new layer. The viewport must fit within the full texture extent.
	bool SelectExternalRenderTargetView(uint32_t arrayLayer, int viewportX, int viewportY, int viewportWidth, int viewportHeight) override;

	// Finish external-frame ownership. Must be called after Unlock(); releases
	// the imported wrapper and restores the canvas render size. Returns false
	// while locked or when no external frame is active.
	bool EndExternalRenderTargetFrame();

	// Compatibility one-shot used by the existing browser interop diagnostic.
	// Equivalent to begin + full-size view selection and automatically ends the
	// external frame from Unlock().
	bool QueueExternalRenderTarget(WGPUTexture texture, uint32_t arrayLayer, int width, int height);
	int GetExternalRenderTargetFrames() const { return ExternalRenderTargetFrames; }
	int GetLastExternalRenderTargetDrawCalls() const { return LastExternalRenderTargetDrawCalls; }
	WGPUTextureFormat GetExternalRenderTargetFormat() const { return ExternalColorFormat; }
	WGPUTextureFormat GetLastExternalRenderTargetFormat() const { return LastExternalColorFormat; }
	size_t GetPipelineColorFormatCount() const { return Pipelines ? Pipelines->GetColorFormatCount() : 0; }
	int GetExternalRenderTargetState() const
	{
		return (IsLocked ? 1 : 0) |
			(ExternalFrameActive ? 2 : 0) |
			(ExternalFrameTexture ? 4 : 0) |
			(UsingExternalRenderTarget ? 8 : 0);
	}

	std::unique_ptr<WebGPUContext> Context;
	std::unique_ptr<WebGPUPipelineCache> Pipelines;
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
		int BufferRollovers = 0;
		int BindGroupsCreated = 0;
		int BindGroupCacheHits = 0;
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
	WGPUTextureView CreateExternalRenderTargetView(uint32_t arrayLayer) const;
	bool SwitchLockedExternalRenderTargetView(uint32_t arrayLayer, int viewportX, int viewportY, int viewportWidth, int viewportHeight);
	void BeginFramePass(bool colorClear, vec4 clearColor, bool depthClear);
	void EndAndSubmitFramePass();

	void SetPipeline(WebGPUScenePipelineState* pipeline);
	void SetPipeline(uint32_t PolyFlags);
	void SetDescriptorSet(uint32_t PolyFlags, WebGPUCachedTexture* tex = nullptr, bool clamp = false);
	void SetDescriptorSet(uint32_t PolyFlags, const ComplexSurfaceInfo& info);

	void AddDrawBatch();
	void DrawBatches(bool submitBoundary = false, bool clearDepthOnReopen = false);
	void DrawEntry(const WebGPUDrawBatchEntry& entry);
	WGPUBindGroup GetTextureBindGroup(const WebGPUDrawBatchEntry& entry);
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

	struct TextureBindGroupKey
	{
		WebGPUCachedTexture* Tex = nullptr;
		WebGPUCachedTexture* Lightmap = nullptr;
		WebGPUCachedTexture* Detailtex = nullptr;
		WebGPUCachedTexture* Macrotex = nullptr;
		uint32_t TexSamplerMode = 0;
		uint32_t DetailtexSamplerMode = 0;
		uint32_t MacrotexSamplerMode = 0;

		bool operator==(const TextureBindGroupKey& other) const;
	};

	struct TextureBindGroupKeyHash
	{
		size_t operator()(const TextureBindGroupKey& key) const;
	};

	std::unordered_map<TextureBindGroupKey, WGPUBindGroup, TextureBindGroupKeyHash> TextureBindGroups;

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

	WGPUTexture DepthTexture = nullptr;
	WGPUTextureView DepthView = nullptr;
	int DepthWidth = 0;
	int DepthHeight = 0;

	WGPUCommandEncoder FrameEncoder = nullptr;
	WGPURenderPassEncoder FramePass = nullptr;
	WGPUTexture CurrentSurfaceTexture = nullptr;
	WGPUTextureView CurrentSurfaceView = nullptr;
	vec4 CurrentClearColor = vec4(0.0f);

	WGPUTexture ExternalFrameTexture = nullptr;
	WGPUTextureFormat ExternalColorFormat = WGPUTextureFormat_Undefined;
	WGPUTextureFormat LastExternalColorFormat = WGPUTextureFormat_Undefined;
	WGPUTextureFormat ActiveColorFormat = WGPUTextureFormat_BGRA8Unorm;
	uint32_t ExternalArrayLayer = 0;
	int ExternalTextureWidth = 0;
	int ExternalTextureHeight = 0;
	int ExternalViewportX = 0;
	int ExternalViewportY = 0;
	int ExternalViewportWidth = 0;
	int ExternalViewportHeight = 0;
	int SavedFixedRenderWidth = 0;
	int SavedFixedRenderHeight = 0;
	int ExternalFrameDrawCalls = 0;
	int ExternalFrameLocks = 0;
	bool ExternalFrameActive = false;
	bool AutoEndExternalFrame = false;
	bool UsingExternalRenderTarget = false;
	int ExternalRenderTargetFrames = 0;
	int LastExternalRenderTargetDrawCalls = 0;

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
