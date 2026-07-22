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

// Native, aligned rendering data decoded from the packed WebXR/WASM ABI.
// Keeping this separate from the wire structs prevents packed/unaligned data
// from leaking into the scene renderer.
struct WebXRSceneView
{
	uint32_t Eye = 0;
	uint32_t ArrayLayer = 0;
	int ViewportX = 0;
	int ViewportY = 0;
	int ViewportWidth = 0;
	int ViewportHeight = 0;
	vec3 Location;
	mat4 WorldToView;
	Coords ViewRotation;
	mat4 Projection;
};

// Renderer-local test seam for the deliberately narrow WebXR weapon-overlay
// pass. An eye pass is counted after its world view completes; a weapon call
// is counted only when a current weapon exists and its overlay event is
// dispatched. HUD, console, PreRender, and PostRender are not part of this
// pass.
struct WebXRWeaponOverlayDiagnostics
{
	uint64_t Frames = 0;
	uint64_t EyePasses = 0;
	uint64_t WeaponCalls = 0;
	uint32_t LastFrameExpectedEyePasses = 0;
	uint32_t LastFrameEyePasses = 0;
	uint32_t LastFrameWeaponCalls = 0;
};

// Head-locked projection-layer HUD configuration. The renderer owns the
// default for now; M9's settings UI can drive this narrow seam without
// changing the HUD capture/presentation lifecycle.
struct WebXRHudPlaneSettings
{
	float DistanceUU = 68.8976f;       // 1.75 m at the WebXR 39.3701 UU/m scale
	float HorizontalFovDegrees = 50.0f;
	float AspectRatio = 4.0f / 3.0f;
	float SafeAreaFraction = 0.90f;
};

enum WebXRHudSelfTestBits : uint32_t
{
	WebXRHudSelfTestSingleUpdateStereoPresentation = 1u << 0,
	WebXRHudSelfTestViewportClamping = 1u << 1,
	WebXRHudSelfTestAsymmetricProjection = 1u << 2,
	WebXRHudSelfTestAbsentHud = 1u << 3,
	WebXRHudSelfTestAll = (1u << 4) - 1u
};

struct WebXRHudDiagnostics
{
	uint64_t Frames = 0;
	uint64_t StateUpdates = 0;
	uint64_t EyePresentations = 0;
	uint64_t CapturedCommands = 0;
	uint64_t UnsupportedDraws = 0;
	uint64_t ClampedViewports = 0;
	uint32_t LastFrameExpectedEyePresentations = 0;
	uint32_t LastFrameStateUpdates = 0;
	uint32_t LastFrameEyePresentations = 0;
	uint32_t LastFrameCapturedCommands = 0;
	uint32_t LastFrameUnsupportedDraws = 0;
	uint32_t LastFrameClampedViewports = 0;
	uint32_t SelfTestMask = 0;
	bool SelfTestPassed = false;
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
	// M5 diagnostic/bridge path: renders one already-advanced game state to
	// two layers of the active external target. HUD/menu stereo presentation
	// is deliberately deferred to M9; this proves the world-view split first.
	void DrawGameStereoLayers(float levelTimeElapsed);
	bool DrawGameWebXRViews(float levelTimeElapsed, const WebXRSceneView* views, uint32_t viewCount);
	const WebXRWeaponOverlayDiagnostics& GetWebXRWeaponOverlayDiagnostics() const { return WebXRWeaponOverlayStats; }
	const WebXRHudDiagnostics& GetWebXRHudDiagnostics() const { return WebXRHudStats; }
	const WebXRHudPlaneSettings& GetWebXRHudPlaneSettings() const { return WebXRHudSettings; }
	void SetWebXRHudPlaneSettings(const WebXRHudPlaneSettings& settings);
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
	// View-independent scene work must run once per game frame even when XR
	// renders multiple views. DrawSceneView contains only the work that is
	// intentionally repeated for each eye.
	bool PrepareSceneViews();
	void DrawSceneView(const vec3& location, const mat4& worldToView, const Coords& viewRotation, const ViewportOverride* viewportOverride = nullptr);
	void DrawScene();
	// --debugstereo diagnostic: renders the scene twice (fake-IPD offset
	// camera, left/right halves of the window) with no OpenXR session
	// involved - proves the viewport-override + per-eye view-matrix path
	// visually before real VR session/swapchain code exists.
	// See VR_IMPLEMENTATION_PLAN.md M2 step 5.
	void DrawSceneStereo();
	void DrawSceneStereoLayers();
	bool DrawSceneWebXRViews(const WebXRSceneView* views, uint32_t viewCount);
	bool DrawGameInternal(float levelTimeElapsed, bool layeredStereo, const WebXRSceneView* xrViews = nullptr, uint32_t xrViewCount = 0);

	std::unique_ptr<LightmapTexture> CreateLightmapTexture();

	void UpdateFogmapTexture(uint32_t* texels, UModel* model, const Coords& mapCoords, int lightMap, UZoneInfo* zoneActor);

	void ResetCanvas();
	void PreRender();
	void RenderOverlays();
	bool RenderWebXRWeaponOverlay();
	bool CaptureWebXRHud();
	bool PresentWebXRHud(const WebXRSceneView* views, uint32_t viewCount);
	void SubmitCanvasTile(FTextureInfo& info, float x, float y, float width, float height,
		float u, float v, float uLength, float vLength, float z, vec4 color, vec4 fog, uint32_t flags);
	void SubmitCanvas2DLine(vec4 color, uint32_t flags, vec3 p1, vec3 p2);
	uint32_t RunWebXRHudSelfTest();
	void PostRender();
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
	WebXRWeaponOverlayDiagnostics WebXRWeaponOverlayStats;
	WebXRHudPlaneSettings WebXRHudSettings;
	WebXRHudDiagnostics WebXRHudStats;

	enum class WebXRHudCommandType : uint8_t { Tile, Line2D };
	struct WebXRHudCommand
	{
		WebXRHudCommandType Type = WebXRHudCommandType::Tile;
		// RenderDevice's texture-cache API accepts a mutable descriptor even
		// though replay does not alter the command's layout or presentation data.
		mutable FTextureInfo Texture;
		float X = 0.0f;
		float Y = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;
		float U = 0.0f;
		float V = 0.0f;
		float ULength = 0.0f;
		float VLength = 0.0f;
		float Z = 1.0f;
		vec4 Color = vec4(1.0f);
		vec4 Fog = vec4(0.0f);
		uint32_t Flags = 0;
		vec3 P1;
		vec3 P2;
	};
	Array<WebXRHudCommand> WebXRHudCommands;
	bool WebXRHudCaptureActive = false;
	int WebXRHudLayoutWidth = 1280;
	int WebXRHudLayoutHeight = 960;

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
