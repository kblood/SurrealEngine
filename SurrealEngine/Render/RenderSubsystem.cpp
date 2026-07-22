
#include "Precomp.h"
#include "RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "GameWindow.h"
#include "UObject/USubsystem.h"
#include "VM/ScriptCall.h"
#include "Engine.h"

RenderSubsystem::RenderSubsystem(RenderDevice* renderdevice) : Device(renderdevice), XRUIBinding(*this)
{
}

void RenderSubsystem::DrawGame(float levelTimeElapsed, const ViewFamily& viewFamily)
{
	UpdateXRUISurfaceVisibility();
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
	if (BeginPresentationLayer(viewFamily.Presentation, PresentationLayer::UserInterface))
	{
		PreRender();
		EndPresentationLayer(viewFamily.Presentation, PresentationLayer::UserInterface);
	}

	if (engine->LaunchInfo.ue1Version <= 219 || engine->console->bNoDrawWorld() == false)
	{
		if (BeginPresentationLayer(viewFamily.Presentation, PresentationLayer::World))
		{
			DrawScene(viewFamily);
			EndPresentationLayer(viewFamily.Presentation, PresentationLayer::World);
		}
		if (BeginPresentationLayer(viewFamily.Presentation, PresentationLayer::WeaponOverlay))
		{
			RenderOverlays();
			if (engine->LaunchInfo.IsDeusEx())
				PostRenderFlash();
			EndPresentationLayer(viewFamily.Presentation, PresentationLayer::WeaponOverlay);
		}
		Device->EndFlash();
	}

	if (BeginPresentationLayer(viewFamily.Presentation, PresentationLayer::UserInterface))
	{
		PostRender();
		EndPresentationLayer(viewFamily.Presentation, PresentationLayer::UserInterface);
	}

	XRUIBinding.Replay(XRUICanvasReplayContext::Game);

	Device->Unlock(true);
}

void RenderSubsystem::UpdateXRUISurfaceVisibility()
{
	const bool menuActive = IsXRUIMenuActive();
	XRUIBinding.SetHudActive(engine->IsStartupIntroActive() && !menuActive);
	XRUIBinding.SetMenuActive(menuActive);
	if (menuActive)
		engine->CompleteStartupIntro();
}

void RenderSubsystem::DrawEditorViewport()
{
	Device->Brightness = engine->client->Brightness;
	DrawScene();
}

void RenderSubsystem::DrawVideoFrame(FTextureInfo* frame, FTextureInfo* background)
{
	DrawVideoFrame(frame, background, {});
}

void RenderSubsystem::DrawVideoFrame(FTextureInfo* frame, FTextureInfo* background, const PresentationPlan& presentation)
{
	vec3 flashScale = 0.5f;
	vec3 flashFog = vec3(0.0f, 0.0f, 0.0f);
	Device->Brightness = 0.4f;// engine->client->Brightness;
	Device->Lock(vec4(flashScale, 1.0f), vec4(flashFog, 1.0f), vec4(0.0f), nullptr, nullptr);
	ResetCanvas();
	if (BeginPresentationLayer(presentation, PresentationLayer::Cinematic))
	{
		Device->SetSceneNode(&Canvas.Frame);
		DrawVideoContents(frame, background);
		Device->EndFlash();
		EndPresentationLayer(presentation, PresentationLayer::Cinematic);
	}
	else
	{
		Device->EndFlash();
	}

	XRUICinematicFrame = frame;
	XRUICinematicBackground = background;
	XRUIBinding.Replay(XRUICanvasReplayContext::Cinematic);
	XRUICinematicFrame = nullptr;
	XRUICinematicBackground = nullptr;
	Device->Unlock(true);
}

void RenderSubsystem::DrawVideoContents(FTextureInfo* frame, FTextureInfo* background)
{
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
}

bool RenderSubsystem::BeginPresentationLayer(const PresentationPlan& presentation, PresentationLayer layer)
{
	return Device->BeginPresentationLayer(presentation.GetLayer(layer));
}

void RenderSubsystem::EndPresentationLayer(const PresentationPlan& presentation, PresentationLayer layer)
{
	Device->EndPresentationLayer(presentation.GetLayer(layer));
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
