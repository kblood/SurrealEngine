#pragma once

#include "VisibleFrame.h"
#include "ViewFamily.h"
#include "XRUISurfaceEngineBinding.h"
#include "Lightmap/LightmapBuilder.h"
#include "XR/XRUIVisuals.h"

class RenderDevice;
class UWindow;
class UFont;

struct LightmapTexture
{
	TextureFormat Format;
	UnrealMipmap Mip;
};

class RenderSubsystem : private XRUISurfaceEngineHost
{
public:
	RenderSubsystem(RenderDevice* renderdevice);
	XRUISurfaceEngineBinding& XRUISurfaces() { return XRUIBinding; }
	bool IsXRUIMenuActive() const;
	void UpdateXRUISurfaceVisibility();
	void SetDirectHudPresentation(bool active) { DirectHudPresentationActive = active; }
	int CalculateCanvasUIScale() const;
	void SetXRUIVisualOverlay(const XRUIVisualFrame& frame,
		const std::array<PresentationTarget, 2>& targets);
	void ResetXRPresentationState();
	uint32_t XRUIVisualViewMask() const { return LastXRUIVisualViewMask; }
	uint32_t XRUIVisualHandCount() const { return LastXRUIVisualHandCount; }

	void PreRenderWindows(UCanvas* canvas);
	void PostRenderWindows(UCanvas* canvas);
	void DrawWindowInfo(UFont* font, UWindow* window, int depth, float& curY);
	void DrawWindow(UWindow* window, float offsetX, float offsetY);
	void ResetWindowGC(UWindow* window, float offsetX, float offsetY);

	void DrawEditorViewport();
	void DrawVideoFrame(FTextureInfo* frame, FTextureInfo* background);
	void DrawVideoFrame(FTextureInfo* frame, FTextureInfo* background, const PresentationPlan& presentation);

	void DrawGame(float levelTimeElapsed, const ViewFamily& viewFamily);
	void OnMapLoaded();

	void DrawActor(UActor* actor, bool WireFrame, bool ClearZ);
	void DrawClippedActor(UActor* actor, bool WireFrame, int X, int Y, int XB, int YB, bool ClearZ);
	void DrawTile(UTexture* Tex, float x, float y, float XL, float YL, float U, float V, float UL, float VL, float Z, vec4 color, vec4 fog, uint32_t flags);
	void DrawTileClipped(UTexture* Tex, float orgX, float orgY, float curX, float curY, float XL, float YL, float U, float V, float UL, float VL, float Z, vec4 color, vec4 fog, uint32_t flags, float clipX, float clipY);
	void DrawText(UFont* font, vec4 color, float orgX, float orgY, float& curX, float& curY, float& curXL, float& curYL, bool newlineAtEnd, const std::string& text, uint32_t polyflags, bool center, float spaceX = 0.0f, float spaceY = 0.0f, float clipX = 100000.0f, float clipY = 100000.0f, bool noDraw = false);
	void DrawTextClipped(UFont* font, vec4 color, float orgX, float orgY, float curX, float curY, const std::string& text, uint32_t polyflags, bool checkHotKey, float clipX, float clipY, bool center);
	vec2 GetTextSize(UFont* font, const std::string& text, float spaceX = 0.0f, float spaceY = 0.0f);

	void DrawTile(FTextureInfo& Info, float X, float Y, float XL, float YL, float U, float V, float UL, float VL, float Z, vec4 Color, vec4 Fog, uint32_t PolyFlags);
	void Draw2DLine(vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2);
	void Draw3DLine(vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2);
	void UpdateTexture(UTexture* tex);

	bool ShowTimedemoStats = false;
	bool ShowRenderStats = false;
	bool ShowCollisionDebug = false;
	bool ShowMultiViewDiagnostic = false;

	int TextureFrameCounter = 0;
	int FrameCounter = 0;

	vec3* GetTempVertexBuffer(size_t count)
	{
		if (VertexBuffer.size() < count)
			VertexBuffer.resize(count);
		return VertexBuffer.data();
	}

	GouraudVertex* GetTempGouraudVertexBuffer(size_t count)
	{
		if (GouraudVertexBuffer.size() < count)
			GouraudVertexBuffer.resize(count);
		return GouraudVertexBuffer.data();
	}

	FTextureInfo GetBrushLightmap(UMover* mover, const Poly& poly, UZoneInfo* zoneActor, UModel* model);
	FTextureInfo GetSurfaceLightmap(BspSurface& surface, UZoneInfo* zoneActor, UModel* model);
	FTextureInfo GetLightmap(UModel* model, int lightmapIndex, const Coords& coords, UZoneInfo* zoneActor);

	FTextureInfo GetBrushFogmap(UMover* mover, const Poly& poly, UZoneInfo* zoneActor, UModel* model);
	FTextureInfo GetSurfaceFogmap(BspSurface& surface, UZoneInfo* zoneActor, UModel* model);
	FTextureInfo GetFogmap(UModel* model, int lightmapIndex, const Coords& coords, UZoneInfo* zoneActor);

	vec3 GetVertexLight(UActor* actor, const vec3& location, const vec3& normal, bool unlit, UZoneInfo* zoneActor);
	vec4 GetVertexFog(UActor* actor, const vec3& location);

	void UpdateTextureInfo(FTextureInfo& info, BspSurface& surface, UTexture* texture, float ZoneUPanSpeed, float ZoneVPanSpeed);
	void UpdateTextureInfo(FTextureInfo& info, const Poly& poly, UTexture* texture, float ZoneUPanSpeed, float ZoneVPanSpeed);
	void UpdateTextureInfo(FTextureInfo& info, UTexture* texture);

	void UpdateActorLightList(UActor* actor);

	RenderDevice* Device = nullptr;

	struct
	{
		Array<UTexture*> textures;
		UTexture* envmap = nullptr;
	} Mesh;

	struct
	{
		int Frames = 0;
		int Surfaces = 0;
		int Actors = 0;
	} Stats;

	VisibleFrame MainFrame;

private:
	bool BeginXRUICanvasCapture(const XRUICanvasReplayItem& item) override;
	void ReplayXRUICanvas(const XRUICanvasReplayItem& item) override;
	void EndXRUICanvasCapture(const XRUICanvasReplayItem& item) override;
	void MoveXRUICursor(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel) override;
	void PressXRUIPrimary(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel) override;
	void ReleaseXRUIPrimary(const XRUIPointerSource& source, XRUISurfaceKind surface, const Pointf& canvasPixel, bool canceled) override;
	void EndXRUIPointerSession() override;

	void DrawVideoContents(FTextureInfo* frame, FTextureInfo* background);

	bool PrepareSceneViews();
	void DrawSceneView(const ViewDescription& view);
	void DrawScene();
	void DrawScene(const ViewFamily& viewFamily, bool renderWeaponPerView = false,
		bool endFlashPerView = false);
	void DrawXRUIVisualOverlay(const ViewFamily& viewFamily);
	bool BeginPresentationLayer(const PresentationPlan& presentation, PresentationLayer layer);
	void EndPresentationLayer(const PresentationPlan& presentation, PresentationLayer layer);

	std::unique_ptr<LightmapTexture> CreateLightmapTexture();

	void UpdateFogmapTexture(uint32_t* texels, UModel* model, const Coords& mapCoords, int lightMap, UZoneInfo* zoneActor);

	void ResetCanvas();
	void PreRender();
	void RenderOverlays();
	bool RenderXRWeaponOverlay();
	void PostRender();
	bool PostRenderPerViewHud(const ViewFamily& viewFamily);
	void PostRenderFlash();
	void DrawTimedemoStats();
	void DrawCollisionDebug();
	void DrawTile(FTextureInfo& texinfo, const Rectf& dest, const Rectf& src, const Rectf& clipBox, float Z, vec4 color, vec4 fog, uint32_t flags);

	static Array<std::string> FindTextBlocks(const std::string& text);
	void DrawTextBlockRange(float x, float y, const Array<std::string>& textBlocks, size_t start, size_t end, UFont* font, vec4 color, uint32_t polyflags, float spaceX);

	float LevelTimeElapsed = 0.0f;
	float AutoUV = 0.0f;
	float AmbientGlowTime = 0.0f;
	float AmbientGlowAmount = 0.0f;
	bool XRWeaponOverlayActive = false;
	bool DirectHudPresentationActive = false;
	XRUIVisualFrame PendingXRUIVisualFrame;
	std::array<PresentationTarget, 2> PendingXRUIVisualTargets;
	uint32_t LastXRUIVisualViewMask = 0;
	uint32_t LastXRUIVisualHandCount = 0;

	struct
	{
		int uiscale = 1;
		int fps = 0;
		int framesDrawn = 0;
		uint64_t startFPSTime = 0;
		FSceneNode Frame;
	} Canvas;

	struct
	{
		std::map<uint64_t, std::unique_ptr<LightmapTexture>> lmtextures;
		std::map<uint64_t, std::pair<int, std::unique_ptr<LightmapTexture>>> fogtextures;
		Array<UActor*> FogBalls;
		LightmapBuilder Builder;
		int FogFrameCounter = 0;
	} Light;

	Array<vec3> VertexBuffer;
	Array<GouraudVertex> GouraudVertexBuffer;

	XRUISurfaceEngineBinding XRUIBinding;
	FTextureInfo* XRUICinematicFrame = nullptr;
	FTextureInfo* XRUICinematicBackground = nullptr;
	PresentationLayerDescription XRUICaptureLayer;
	struct
	{
		bool Active = false;
		FSceneNode Frame;
		int UIScale = 1;
		float CurX = 0.0f;
		float CurY = 0.0f;
		float ClipX = 0.0f;
		float ClipY = 0.0f;
		int SizeX = 0;
		int SizeY = 0;
		float ConsoleFrameX = 0.0f;
		float ConsoleFrameY = 0.0f;
		bool HasConsoleFrame = false;
		int ViewportX = 0;
		int ViewportY = 0;
		int ViewportWidth = 0;
		int ViewportHeight = 0;
	} XRUICanvasRestore;
	bool XRUIMouseStateSaved = false;
	bool XRUIPreviousMouseAvailable = false;
	bool XRUIPreviousShowMouse = false;
};
