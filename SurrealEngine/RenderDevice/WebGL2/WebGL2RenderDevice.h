#pragma once

#include "RenderDevice/RenderDevice.h"
#include "WebGL2Context.h"

#include <cstdint>
#include <memory>

class WebGL2RenderDevice : public RenderDevice
{
public:
	explicit WebGL2RenderDevice(Widget* viewport);
	~WebGL2RenderDevice() override;

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

	WebGL2Context* GetContext() const { return context.get(); }
	uint32_t FrameCount() const { return frameCount; }
	uint32_t SuppressedFrameCount() const { return suppressedFrameCount; }
	uint32_t UnsupportedDrawCount() const { return unsupportedDrawCount; }
	uint32_t ErrorCount() const { return errorCount; }
	int DrawingBufferWidth() const { return currentWidth; }
	int DrawingBufferHeight() const { return currentHeight; }

private:
	bool EnsureReady();
	void InitializeGeneration();
	void CountErrors();
	void CountUnsupportedDraw();

	std::unique_ptr<WebGL2Context> context;
	uint32_t initializedGeneration = 0;
	uint32_t frameCount = 0;
	uint32_t suppressedFrameCount = 0;
	uint32_t unsupportedDrawCount = 0;
	uint32_t errorCount = 0;
	int currentWidth = 0;
	int currentHeight = 0;
	bool locked = false;
};
