
#include "Precomp.h"
#include "RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "GameWindow.h"
#include "UObject/USubsystem.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include "Utils/CommandLine.h"

RenderSubsystem::RenderSubsystem(RenderDevice* renderdevice) : Device(renderdevice)
{
	SetWebXRHudPlaneSettings(WebXRHudSettings);
	WebXRHudStats.SelfTestMask = RunWebXRHudSelfTest();
	WebXRHudStats.SelfTestPassed = WebXRHudStats.SelfTestMask == WebXRHudSelfTestAll;
}

void RenderSubsystem::SetWebXRHudPlaneSettings(const WebXRHudPlaneSettings& settings)
{
	// Keep script- or UI-provided values finite and inside deliberately broad
	// comfort bounds. Headset-specific policy belongs to the later settings UI;
	// the renderer only guarantees a valid plane and safe viewport here.
	WebXRHudSettings.Enabled = settings.Enabled;
	WebXRHudSettings.DistanceUU = std::isfinite(settings.DistanceUU) ? std::clamp(settings.DistanceUU,
		WebXRHudPlaneSettings::MinimumDistanceUU, WebXRHudPlaneSettings::MaximumDistanceUU) :
		WebXRHudPlaneSettings::DefaultDistanceUU;
	WebXRHudSettings.HorizontalFovDegrees = std::isfinite(settings.HorizontalFovDegrees) ?
		std::clamp(settings.HorizontalFovDegrees, WebXRHudPlaneSettings::MinimumHorizontalFovDegrees,
			WebXRHudPlaneSettings::MaximumHorizontalFovDegrees) :
		WebXRHudPlaneSettings::DefaultHorizontalFovDegrees;
	WebXRHudSettings.AspectRatio = std::isfinite(settings.AspectRatio) ? std::clamp(settings.AspectRatio,
		WebXRHudPlaneSettings::MinimumAspectRatio, WebXRHudPlaneSettings::MaximumAspectRatio) :
		WebXRHudPlaneSettings::DefaultAspectRatio;
	WebXRHudSettings.SafeAreaFraction = std::isfinite(settings.SafeAreaFraction) ?
		std::clamp(settings.SafeAreaFraction, WebXRHudPlaneSettings::MinimumSafeAreaFraction,
			WebXRHudPlaneSettings::MaximumSafeAreaFraction) :
		WebXRHudPlaneSettings::DefaultSafeAreaFraction;
}

void RenderSubsystem::DrawGame(float levelTimeElapsed)
{
	DrawGameInternal(levelTimeElapsed, false);
}

void RenderSubsystem::DrawGameStereoLayers(float levelTimeElapsed)
{
	DrawGameInternal(levelTimeElapsed, true);
}

bool RenderSubsystem::DrawGameWebXRViews(float levelTimeElapsed, const WebXRSceneView* views, uint32_t viewCount)
{
	return DrawGameInternal(levelTimeElapsed, true, views, viewCount);
}

bool RenderSubsystem::DrawGameInternal(float levelTimeElapsed, bool layeredStereo, const WebXRSceneView* xrViews, uint32_t xrViewCount)
{
	LevelTimeElapsed = levelTimeElapsed;
	AutoUV += levelTimeElapsed * 64.0f;
	AmbientGlowTime = std::fmod(AmbientGlowTime + 0.8f * levelTimeElapsed, 1.0f);
	AmbientGlowAmount = 0.20f + 0.20f * std::sin(radians(AmbientGlowTime * 360.0f));

	Stats.Frames = 0;
	Stats.Surfaces = 0;
	Stats.Actors = 0;

	vec3 flashScale = 0.5f;
	vec3 flashFog = vec3(0.0f, 0.0f, 0.0f);

	UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(engine->CameraActor);
	if (player)
	{
		flashScale = player->FlashScale();
		flashFog = player->FlashFog();
	}

	Device->Brightness = engine->client->Brightness;
	Device->Lock(vec4(flashScale, 1.0f), vec4(flashFog, 1.0f), vec4(0.0f), nullptr, nullptr);

	ResetCanvas();
	if (!layeredStereo)
		PreRender();

	const bool drawWorld = engine->LaunchInfo.ue1Version <= 219 || engine->console->bNoDrawWorld() == false;
	// WebXR still needs a projection-layer frame when a full-screen console or
	// menu suppresses the 3D world. Its player/console PostRender state is
	// captured once and replayed to both eyes by DrawSceneWebXRViews.
	if (xrViews)
	{
		if (!DrawSceneWebXRViews(xrViews, xrViewCount, drawWorld))
		{
			Device->Unlock(false);
			return false;
		}
	}
	else if (drawWorld)
	{
		if (layeredStereo)
			DrawSceneStereoLayers();
		else if (commandline && commandline->HasArg("", "--debugstereo"))
			DrawSceneStereo();
		else
			DrawScene();
		if (!layeredStereo)
		{
			RenderOverlays();
			if (engine->LaunchInfo.IsDeusEx())
				PostRenderFlash();
			Device->EndFlash();
		}
	}

	if (!layeredStereo)
		PostRender();

	Device->Unlock(true);
	return true;
}

void RenderSubsystem::DrawEditorViewport()
{
	Device->Brightness = engine->client->Brightness;
	DrawScene();
}

void RenderSubsystem::DrawVideoFrame(FTextureInfo* frame, FTextureInfo* background)
{
	vec3 flashScale = 0.5f;
	vec3 flashFog = vec3(0.0f, 0.0f, 0.0f);
	Device->Brightness = 0.4f;// engine->client->Brightness;
	Device->Lock(vec4(flashScale, 1.0f), vec4(flashFog, 1.0f), vec4(0.0f), nullptr, nullptr);
	ResetCanvas();
	Device->SetSceneNode(&Canvas.Frame);

	float sizeX = (float)(int)(engine->viewport->ViewportWidth() / (float)Canvas.uiscale);
	float sizeY = (float)(int)(engine->viewport->ViewportHeight() / (float)Canvas.uiscale);

	Rectf clipBox = Rectf::xywh(0.0f, 0.0f, sizeX, sizeY);
	Rectf dest = clipBox;

	if (frame)
	{
		Rectf src = Rectf::xywh(0.0f, 0.0f, (float)frame->USize, (float)frame->VSize);
		DrawTile(*frame, dest, src, clipBox, 1.0f, vec4(1.0f), vec4(0.0f), PF_TwoSided);
	}

	if (background)
	{
		Rectf src = Rectf::xywh(0.0f, 0.0f, (float)background->USize, (float)background->VSize);
		DrawTile(*background, dest, src, clipBox, 1.0f, vec4(1.0f), vec4(0.0f), PF_TwoSided | PF_Highlighted);
	}

	Device->EndFlash();
	Device->Unlock(true);
}

void RenderSubsystem::UpdateTexture(UTexture* tex)
{
	if (tex && tex->FrameCounter != TextureFrameCounter)
	{
		tex->Update(LevelTimeElapsed);
		tex->FrameCounter = TextureFrameCounter;
	}
}

void RenderSubsystem::UpdateTextureInfo(FTextureInfo& info, BspSurface& surface, UTexture* texture, float ZoneUPanSpeed, float ZoneVPanSpeed)
{
	UpdateTextureInfo(info, texture);

	info.Pan.x = -(float)surface.PanU;
	info.Pan.y = -(float)surface.PanV;
	if (surface.PolyFlags & PF_AutoUPan) info.Pan.x -= AutoUV * ZoneUPanSpeed;
	if (surface.PolyFlags & PF_AutoVPan) info.Pan.y -= AutoUV * ZoneVPanSpeed;
}

void RenderSubsystem::UpdateTextureInfo(FTextureInfo& info, const Poly& poly, UTexture* texture, float ZoneUPanSpeed, float ZoneVPanSpeed)
{
	UpdateTextureInfo(info, texture);

	info.Pan.x = -(float)poly.PanU;
	info.Pan.y = -(float)poly.PanV;
	if (poly.PolyFlags & PF_AutoUPan) info.Pan.x -= AutoUV * ZoneUPanSpeed;
	if (poly.PolyFlags & PF_AutoVPan) info.Pan.y -= AutoUV * ZoneVPanSpeed;
}

void RenderSubsystem::UpdateTextureInfo(FTextureInfo& info, UTexture* texture)
{
	info.Texture = texture;
	info.CacheID = (uint64_t)(ptrdiff_t)texture;

	if (!info.Texture)
		return;

	info.UScale = texture->DrawScale();
	info.VScale = texture->DrawScale();
	info.Format = texture->UsedFormat;
	info.Mips = texture->UsedMipmaps.data();
	info.NumMips = (int)texture->UsedMipmaps.size();
	info.USize = texture->USize();
	info.VSize = texture->VSize();
	if (texture->Palette())
		info.Palette = (FColor*)texture->Palette()->Colors.data();

	info.bRealtimeChanged = texture->TextureModified;
	if (texture->TextureModified)
		texture->TextureModified = false;
}

void RenderSubsystem::OnMapLoaded()
{
	Device->Flush(true);

	Light.FogBalls.clear();
	Light.lmtextures.clear();
	Light.fogtextures.clear();

	std::set<UActor*> lightset;
	for (UActor* light : engine->Level->Model->Lights)
	{
		if (light)
			lightset.insert(light);
	}

	for (UActor* light : lightset)
	{
		if (light->VolumeRadius() != 0)
			Light.FogBalls.push_back(light);
	}
}
