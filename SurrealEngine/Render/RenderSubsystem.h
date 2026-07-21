#pragma once

#include "VisibleFrame.h"
#include "Lightmap/LightmapBuilder.h"

class RenderDevice;
class UWindow;
class UFont;

struct LightmapTexture
{
	TextureFormat Format;
	UnrealMipmap Mip;
};

class RenderSubsystem
{
public:
	RenderSubsystem(RenderDevice* renderdevice);

	void PreRenderWindows(UCanvas* canvas);
	void PostRenderWindows(UCanvas* canvas);
	void DrawWindowInfo(UFont* font, UWindow* window, int depth, float& curY);
	void DrawWindow(UWindow* window, float offsetX, float offsetY);
	void ResetWindowGC(UWindow* window, float offsetX, float offsetY);

	void DrawEditorViewport();
	void DrawVideoFrame(FTextureInfo* frame, FTextureInfo* background);

	void DrawGame(float levelTimeElapsed);
	void OnMapLoaded();

	// M3: real per-eye OpenXR pose/fov for this frame, set by Engine::Run()
	// right before DrawGame() when a VR frame is being rendered. Consumed
	// once by DrawSceneVR() inside that same DrawGame() call and cleared
	// automatically - mirrors VulkanRenderDevice::SetPendingXRTargets's
	// pending-state pattern. fov[eye] = {angleLeft, angleRight, angleUp,
	// angleDown} in radians, straight from xrLocateViews.
	void SetPendingVREyes(const vec3 loc[2], const Coords rot[2], const float fov[2][4]);

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
	void DrawScene();
	// --debugstereo diagnostic: renders the scene twice (fake-IPD offset
	// camera, left/right halves of the window) with no OpenXR session
	// involved - proves the viewport-override + per-eye view-matrix path
	// visually before real VR session/swapchain code exists.
	// See VR_IMPLEMENTATION_PLAN.md M2 step 5.
	void DrawSceneStereo();

	// M3: real per-eye VR rendering - same split-viewport/asymmetric-
	// projection machinery as DrawSceneStereo, but driven by the real
	// pose/fov SetPendingVREyes() was given instead of a fake debug IPD.
	void DrawSceneVR();
	bool PendingVR = false;
	vec3 VREyeLocation[2] = {};
	Coords VREyeRotation[2] = { Coords::Identity(), Coords::Identity() };
	float VREyeFov[2][4] = {};

	// M3: snapshot of MainFrame.Frame as DrawSceneVR() left it after each
	// eye's MainFrame.Process() call. Canvas.DrawActor() (used by e.g. the
	// weapon viewmodel's RenderOverlays/PostRender script code) renders via
	// the single shared MainFrame.Frame rather than Canvas.Frame - restoring
	// the matching eye's snapshot before invoking overlay/postrender events
	// in RenderOverlaysVR()/PostRenderVR() keeps that draw path from always
	// silently reusing whichever eye was processed last (eye 1).
	FSceneNode VREyeFrame[2];

	std::unique_ptr<LightmapTexture> CreateLightmapTexture();

	void UpdateFogmapTexture(uint32_t* texels, UModel* model, const Coords& mapCoords, int lightMap, UZoneInfo* zoneActor);

	void ResetCanvas();
	void PreRender();
	void RenderOverlays();
	// M3: HUD/console overlay equivalent of DrawSceneVR - RenderOverlays()
	// draws into Canvas.Frame's full window-wide rect with no per-eye split,
	// so anything positioned relative to screen center lands right on the
	// seam between the two eye halves and reads as smeared/doubled. This
	// redraws the same overlay event into each eye's half in turn, using a
	// half-width Canvas.Frame + matching canvas ClipX/SizeX so UnrealScript
	// HUD layout math (which reads Canvas.ClipX/SizeX) sees the narrower
	// width, then restores the full-window canvas state afterward.
	void RenderOverlaysVR();
	// M4 (2026-07-20): computes and applies a per-eye HUD sub-rect for
	// `eye` within `fullFrame`, replacing the old "centered half-viewport"
	// split. Shared by RenderOverlaysVR()/PostRenderVR() - see the doc
	// comment above its definition in RenderCanvas.cpp for the tan-space
	// derivation and Docs/VR/FABLE_ANALYSIS_2026-07-20.md sections 1-2.
	void SetVRHudFrame(int eye, const FSceneNode& fullFrame);
	void PostRender();
	// M3: same per-eye split as RenderOverlaysVR(), for PostRender() - UT99's
	// actual visible HUD (health/ammo/messages) renders from PlayerPawn's
	// PostRender event, not RenderOverlays, so this is the hook that was
	// still producing the split-down-the-middle HUD text even after
	// RenderOverlaysVR() was added.
	void PostRenderVR();
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
};
