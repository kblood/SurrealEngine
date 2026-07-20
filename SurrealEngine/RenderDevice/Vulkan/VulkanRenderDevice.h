#pragma once

#include "RenderDevice/RenderDevice.h"
#include "UObject/ULevel.h"
#include "UObject/UTexture.h"
#include "CommandBufferManager.h"
#include "BufferManager.h"
#include "DescriptorSetManager.h"
#include "FramebufferManager.h"
#include "RenderPassManager.h"
#include "SamplerManager.h"
#include "ShaderManager.h"
#include "TextureManager.h"
#include "UploadManager.h"
#include "Math/vec.h"
#include "Math/mat.h"
#include "VulkanXRSession.h"

class CachedTexture;

class VulkanRenderDevice : public RenderDevice
{
public:
	// xrSession is null for normal flatscreen play. When set and available
	// (M2 step 4), the constructor calls xrSession->GetVulkanInstanceExtensions()
	// before creating the VkInstance and xrSession->ResolveVulkanDevice()
	// right after, folding the runtime's required instance/device extensions
	// into the existing VulkanInstanceBuilder/VulkanDeviceBuilder
	// RequireExtension calls, and forcing device selection to the exact
	// VkPhysicalDevice OpenXR mandates instead of VulkanDeviceBuilder's own
	// scoring - see VulkanRenderDevice.cpp.
	VulkanRenderDevice(Widget* viewport, VulkanXRSession* xrSession = nullptr);
	~VulkanRenderDevice();

	void Flush(bool AllowPrecache) override;
	void Lock(vec4 FlashScale, vec4 FlashFog, vec4 ScreenClear, uint8_t* HitData, int* HitSize) override;
	void Unlock(bool Blit) override;
	void DrawComplexSurface(FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet) override;
	void DrawGouraudPolygon(FSceneNode* Frame, FTextureInfo& Info, const GouraudVertex* Pts, int NumPts, uint32_t PolyFlags) override;
	void DrawTile(FSceneNode* Frame, FTextureInfo& Info, float X, float Y, float XL, float YL, float U, float V, float UL, float VL, float Z, vec4 Color, vec4 Fog, uint32_t PolyFlags) override;
	void Draw3DLine(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, vec3 OrigP, vec3 OrigQ) override;
	void Draw2DLine(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2) override;
	void Draw2DPoint(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, float X1, float Y1, float X2, float Y2, float Z) override;
	void ClearZ() override;
	void PushHit(const uint8_t* Data, int Count) override;
	void PopHit(int Count, bool bForce) override;
	void ReadPixels(FColor* Pixels) override;
	void EndFlash() override;
	void SetSceneNode(FSceneNode* Frame) override;
	void PrecacheTexture(FTextureInfo& Info, uint32_t PolyFlags) override;

	void SetHitLocation();

	bool SupportsTextureFormat(TextureFormat Format) override;
	void UpdateTextureRect(FTextureInfo& Info, int U, int V, int UL, int VL) override;

	std::shared_ptr<VulkanDevice> Device;

	std::unique_ptr<CommandBufferManager> Commands;

	std::unique_ptr<SamplerManager> Samplers;
	std::unique_ptr<TextureManager> Textures;
	std::unique_ptr<BufferManager> Buffers;
	std::unique_ptr<ShaderManager> Shaders;
	std::unique_ptr<UploadManager> Uploads;

	std::unique_ptr<DescriptorSetManager> DescriptorSets;
	std::unique_ptr<RenderPassManager> RenderPasses;
	std::unique_ptr<FramebufferManager> Framebuffers;

	int VkDeviceIndex = 0;

	void RunBloomPass();
	void BloomStep(VulkanCommandBuffer* cmdbuffer, VulkanPipeline* pipeline, VulkanDescriptorSet* input, VulkanFramebuffer* output, int width, int height, const BloomPushConstants &pushconstants);
	static float ComputeBlurGaussian(float n, float theta);
	static void ComputeBlurSamples(int sampleCount, float blurAmount, float* sampleWeights);

	void DrawPresentTexture(int width, int height);
	PresentPushConstants GetPresentPushConstants();

	// M2 step 8/9: when a VR frame's eye images are pending (set by
	// Engine::Run's XR frame loop via SetPendingXRTargets before calling
	// DrawGame), DrawPresentTexture() additionally blits the just-composited
	// window present image into each eye's OpenXR swapchain image, right
	// after rendering it and before the window's own present transition -
	// see VulkanRenderDevice.cpp. This reuses the normal flatscreen render
	// path unchanged (both eyes currently get an identical mono image - see
	// VR_IMPLEMENTATION_PLAN.md M2 step 8 status notes for why true per-eye
	// stereo composition isn't wired in yet), so it adds no new render pass
	// and cannot regress flatscreen when no target is pending.
	void SetPendingXRTargets(void* leftEyeImage, void* rightEyeImage, int width, int height)
	{
		PendingXRImage[0] = (VkImage)leftEyeImage;
		PendingXRImage[1] = (VkImage)rightEyeImage;
		PendingXRWidth = width;
		PendingXRHeight = height;
	}
	bool HasPendingXRTargets() const { return PendingXRImage[0] != VK_NULL_HANDLE || PendingXRImage[1] != VK_NULL_HANDLE; }
	void ClearPendingXRTargets() { PendingXRImage[0] = VK_NULL_HANDLE; PendingXRImage[1] = VK_NULL_HANDLE; }

	struct
	{
		int ComplexSurfaces = 0;
		int GouraudPolygons = 0;
		int Tiles = 0;
		int DrawCalls = 0;
		int Uploads = 0;
		int RectUploads = 0;
	} Stats;

	int GetSettingsMultisample()
	{
		switch (AntialiasMode)
		{
		default:
		case 0: return 0;
		case 1: return 2;
		case 2: return 4;
		}
	}

private:
	void ClearTextureCache();
	void BlitSceneToPostprocess();

	struct VertexReserveInfo
	{
		SceneVertex* vptr;
		uint32_t* iptr;
		uint32_t vpos;
	};

	VertexReserveInfo ReserveVertices(size_t vcount, size_t icount)
	{
		// If buffers are full, flush and wait for room.
		if (SceneVertexPos + vcount > (size_t)BufferManager::SceneVertexBufferSize || SceneIndexPos + icount > (size_t)BufferManager::SceneIndexBufferSize)
		{
			// If the request is larger than our buffers we can't draw this.
			if (vcount > (size_t)BufferManager::SceneVertexBufferSize || icount > (size_t)BufferManager::SceneIndexBufferSize)
				return { nullptr, nullptr, 0 };

			FlushDrawBatchAndWait();
		}

		return { Buffers->SceneVertices + SceneVertexPos, Buffers->SceneIndexes + SceneIndexPos, (uint32_t)SceneVertexPos };
	}

	void FlushDrawBatchAndWait();

	void UseVertices(size_t vcount, size_t icount)
	{
		SceneVertexPos += vcount;
		SceneIndexPos += icount;
	}

	VkViewport viewportdesc = {};

	// See SetPendingXRTargets()/HasPendingXRTargets() above.
	VkImage PendingXRImage[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
	int PendingXRWidth = 0;
	int PendingXRHeight = 0;

	bool UsePrecache = true;
	vec4 FlashScale;
	vec4 FlashFog;
	FSceneNode* CurrentFrame = nullptr;
	float Aspect = 0.0f;
	float RProjZ = 0.0f;
	float RFX2 = 0.0f;
	float RFY2 = 0.0f;
	// Screen-space center used by the RFX2/RFY2 screen->view formulas in
	// DrawTile/Draw2DLine/Draw2DPoint. Equal to Frame->FX2/FY2 for a
	// symmetric (on-axis) projection, but shifts under an asymmetric
	// ProjectionOverride (e.g. a real per-eye OpenXR frustum) - derived
	// directly from Frame->Projection so tiles/coronas/lines stay aligned
	// with the 3D geometry instead of being shifted by the eye offset.
	// See VR_IMPLEMENTATION_PLAN.md M2 step 6 follow-up.
	float ProjCenterX = 0.0f;
	float ProjCenterY = 0.0f;

	bool IsLocked = false;

	void SetPipeline(PipelineState* pipeline);
	ivec4 GetTextureIndexes(uint32_t PolyFlags, CachedTexture* tex, bool clamp = false);
	ivec4 GetTextureIndexes(uint32_t PolyFlags, CachedTexture* tex, CachedTexture* lightmap, CachedTexture* macrotex, CachedTexture* detailtex);
	void DrawBatch(VulkanCommandBuffer* cmdbuffer);
	void SubmitAndWait(bool present, int presentWidth, int presentHeight, bool presentFullscreen);

	vec4 ApplyInverseGamma(vec4 color);

	struct
	{
		size_t SceneIndexStart = 0;
		PipelineState* Pipeline = nullptr;
		float BlendConstants[4] = {};
	} Batch;

	ScenePushConstants pushconstants = {};

	size_t SceneVertexPos = 0;
	size_t SceneIndexPos = 0;

	struct HitQuery
	{
		int Start = 0;
		int Count = 0;
	};

	uint8_t* HitData = nullptr;
	int* HitSize = nullptr;
	std::vector<uint8_t> HitQueryStack;
	std::vector<HitQuery> HitQueries;
	std::vector<uint8_t> HitBuffer;

	int ForceHitIndex = -1;
	HitQuery ForceHit;
};

inline void VulkanRenderDevice::SetPipeline(PipelineState* pipeline)
{
	if (pipeline != Batch.Pipeline)
	{
		DrawBatch(Commands->GetDrawCommands());
		Batch.Pipeline = pipeline;
	}
}

inline ivec4 VulkanRenderDevice::GetTextureIndexes(uint32_t PolyFlags, CachedTexture* tex, bool clamp)
{
	return ivec4(DescriptorSets->GetTextureArrayIndex(PolyFlags, tex, clamp), 0, 0, 0);
}

inline ivec4 VulkanRenderDevice::GetTextureIndexes(uint32_t PolyFlags, CachedTexture* tex, CachedTexture* lightmap, CachedTexture* macrotex, CachedTexture* detailtex)
{
	if (DescriptorSets->IsTextureArrayFull())
	{
		FlushDrawBatchAndWait();
		DescriptorSets->ClearCache();
		Textures->ClearAllBindlessIndexes();
	}

	ivec4 textureBinds;
	textureBinds.x = DescriptorSets->GetTextureArrayIndex(PolyFlags, tex);
	textureBinds.y = DescriptorSets->GetTextureArrayIndex(0, macrotex);
	textureBinds.z = DescriptorSets->GetTextureArrayIndex(0, detailtex);
	textureBinds.w = DescriptorSets->GetTextureArrayIndex(0, lightmap);
	return textureBinds;
}
