#pragma once

#include "Math/vec.h"
#include "Math/mat.h"
#include "Math/coords.h"

#include "UObject/UTexture.h"

#include <surrealwidgets/core/canvas.h>
#include <surrealwidgets/core/rect.h>

class UTexture;
class UActor;
class Widget;
enum class RenderAPI;

// The engine's historical projection/screen-space convention needs one Y
// reflection when submitted to a WebGPU canvas. Browser-provided XRView
// projections for a WebGPU-compatible session do not. Carry this explicitly;
// neither the projection matrix nor the render-target shape identifies it.
enum class WebGPUClipSpaceYConvention : uint8_t
{
	EngineProjection = 0,
	NativeWebGPUProjectionLayer = 1
};

inline float WebGPUClipSpaceYSign(WebGPUClipSpaceYConvention convention)
{
	return convention == WebGPUClipSpaceYConvention::NativeWebGPUProjectionLayer ? 1.0f : -1.0f;
}

inline float WebGPUFramebufferYFromNDC(WebGPUClipSpaceYConvention convention, float ndcY)
{
	// Native WebGPU framebuffers have a top-left origin. EngineProjection is
	// reflected later in WGSL, so CPU-side screen bounds mirror the same choice.
	return convention == WebGPUClipSpaceYConvention::NativeWebGPUProjectionLayer ?
		(1.0f - ndcY) * 0.5f : (ndcY + 1.0f) * 0.5f;
}

struct FSceneNode
{
	int XB, YB; // viewport top left
	int X, Y; // viewport size
	float FX, FY;
	float FX2, FY2;
	Widget* Viewport = nullptr;
	float FovAngle;

	mat4 ObjectToWorld;
	mat4 WorldToView;
	mat4 Projection;

	// When true, SetSceneNode uses Projection as-is (e.g. an asymmetric
	// per-eye OpenXR projection) instead of deriving a symmetric frustum
	// from FovAngle. See VR_IMPLEMENTATION_PLAN.md M2 step 6.
	bool ProjectionOverride = false;
	WebGPUClipSpaceYConvention ClipSpaceYConvention = WebGPUClipSpaceYConvention::EngineProjection;

	vec4 NearClip = vec4(0.0f, 0.0f, 1.0f, -1.0f);
	float Zoom = 1.0f;
};

struct GouraudVertex
{
	vec3 Point;
	vec3 Light;
	vec2 UV;
	vec4 Fog;
};

struct FSurfaceFacet
{
	Coords MapCoords;
	vec3* Vertices;
	uint32_t VertexCount;
};

struct FColor
{
	FColor() = default;
	FColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) : R(r), G(g), B(b), A(a) { }

	uint8_t R, G, B, A;
};

enum class TextureFormat : uint32_t;

struct FTextureInfo
{
	uint64_t CacheID = 0;
	bool bRealtimeChanged = false;

	UTexture* Texture = nullptr;
	float UScale = 1.0f;
	float VScale = 1.0f;
	vec2 Pan = { 0.0f };

	// to do: give these correct values
	TextureFormat Format = {};
	int USize = 1;
	int VSize = 1;
	int NumMips = 0;
	UnrealMipmap* Mips = nullptr;
	FColor* Palette = nullptr;
};

inline float GetUMult(const FTextureInfo& Info) { return 1.0f / (Info.UScale * Info.USize); }
inline float GetVMult(const FTextureInfo& Info) { return 1.0f / (Info.VScale * Info.VSize); }

struct FSurfaceInfo
{
	uint32_t PolyFlags = 0;
	FTextureInfo* Texture = nullptr;
	FTextureInfo* LightMap = nullptr;
	FTextureInfo* MacroTexture = nullptr;
	FTextureInfo* DetailTexture = nullptr;
	FTextureInfo* FogMap = nullptr;
};

class OutputDevice
{
public:
	void Log(const std::string& text) { }
};

class RenderDevice
{
public:
	static std::unique_ptr<RenderDevice> Create(Widget* viewport, RenderAPI renderAPI);

	RenderDevice();
	virtual ~RenderDevice() = default;

	virtual void Flush(bool AllowPrecache) = 0;
	virtual bool Exec(std::string Cmd, OutputDevice& Ar) { return false; }
	virtual void Lock(vec4 FlashScale, vec4 FlashFog, vec4 ScreenClear, uint8_t* HitData, int* HitSize) = 0;
	virtual void Unlock(bool Blit) = 0;
	virtual void DrawComplexSurface(FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet) = 0;
	virtual void DrawGouraudPolygon(FSceneNode* Frame, FTextureInfo& Info, const GouraudVertex* Pts, int NumPts, uint32_t PolyFlags) = 0;
	virtual void DrawTile(FSceneNode* Frame, FTextureInfo& Info, float X, float Y, float XL, float YL, float U, float V, float UL, float VL, float Z, vec4 Color, vec4 Fog, uint32_t PolyFlags) = 0;
	virtual void Draw3DLine(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2) = 0;
	virtual void Draw2DLine(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2) = 0;
	virtual void Draw2DPoint(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, float X1, float Y1, float X2, float Y2, float Z) = 0;
	virtual void ClearZ() = 0;
	virtual void PushHit(const uint8_t* Data, int Count) = 0;
	virtual void PopHit(int Count, bool bForce) = 0;
	virtual void ReadPixels(FColor* Pixels) = 0;
	virtual void EndFlash() = 0;
	virtual void SetSceneNode(FSceneNode* Frame) = 0;
	virtual void PrecacheTexture(FTextureInfo& Info, uint32_t PolyFlags) = 0;
	virtual bool SupportsTextureFormat(TextureFormat Format) = 0;
	virtual void UpdateTextureRect(FTextureInfo& Info, int U, int V, int UL, int VL) = 0;

	// XR projection textures commonly store one eye per array layer. Backends
	// that support browser/runtime-owned layered targets override this to end
	// the current eye pass and reopen it on another layer. Ordinary canvas and
	// native swapchain backends leave the default unsupported result unchanged.
	virtual bool SelectExternalRenderTargetView(uint32_t arrayLayer, int viewportX, int viewportY, int viewportWidth, int viewportHeight) { return false; }
	virtual bool SelectExternalRenderTargetLayer(uint32_t arrayLayer) { return false; }

	bool ParseCommand(std::string* cmd, const std::string& keyword) { return false; }

	Widget* Viewport = nullptr;
	bool PrecacheOnFlip = false;
	float Brightness = 0.5f;

	// When set (>0), the scene render target is pinned to this size instead
	// of auto-resizing to the OS window's client area every Lock() call.
	// Used by VR to size the scene buffer to the OpenXR swapchain
	// resolution, which is independent of (and usually larger than) the
	// desktop mirror window. See VR_IMPLEMENTATION_PLAN.md M2 step 8.
	int FixedRenderWidth = 0;
	int FixedRenderHeight = 0;
	void SetFixedRenderSize(int width, int height) { FixedRenderWidth = width; FixedRenderHeight = height; }
	int GetRenderWidth() const;
	int GetRenderHeight() const;

	// 2D rendering
	bool IsOrtho = false;
	bool IsOrthoLowDetail = false;

	// For editor hit testing
	int HitX = 0;
	int HitY = 0;
	int HitWidth = 0;
	int HitHeight = 0;

	// Settings
	bool UseVSync = true;
	float GammaOffset = 0.0f;
	float GammaOffsetRed = 0.0f;
	float GammaOffsetGreen = 0.0f;
	float GammaOffsetBlue = 0.0f;
	uint8_t LinearBrightness = 128; // 0.0f;
	uint8_t Contrast = 128; // 1.0f;
	uint8_t Saturation = 255; // 1.0f;
	int GrayFormula = 1;
	bool Hdr = false;
	uint8_t HdrScale = 128;
	bool Bloom = false;
	uint8_t BloomAmount = 128;
	float LODBias = -0.5f;
	uint8_t AntialiasMode = 2; // 4x multisample
	uint8_t GammaMode = 0;
	uint8_t LightMode = 0;
	bool GammaCorrectScreenshots = true;
	bool UseDebugLayer = false;
};

class RenderDeviceTexture : public CanvasTexture
{
public:
	FTextureInfo Info;
	UnrealMipmap Mip;
};

class RenderDeviceCanvas : public Canvas
{
public:
	RenderDeviceCanvas(RenderDevice* device);

	void begin(const Colorf& color) override;
	void end() override;

	void begin3d() override;
	void end3d() override;

	RenderDevice* GetRenderDevice() { return device; }

protected:
	std::unique_ptr<CanvasTexture> createTexture(int width, int height, const void* pixels, ImageFormat format) override;
	void drawLineAntialiased(float x0, float y0, float x1, float y1, Colorf color) override;
	void fillTile(float x, float y, float width, float height, Colorf color) override;
	void drawTile(CanvasTexture* texture, float x, float y, float width, float height, float u, float v, float uvwidth, float uvheight, Colorf color) override;
	void drawGlyph(CanvasTexture* texture, float x, float y, float width, float height, float u, float v, float uvwidth, float uvheight, Colorf color) override;

private:
	RenderDevice* device = nullptr;
	FSceneNode frame;
};
