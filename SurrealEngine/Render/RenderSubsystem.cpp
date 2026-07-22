
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
}

void RenderSubsystem::DrawGame(float levelTimeElapsed)
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
	PreRender();

	bool isVR = PendingVR;
	bool dedicatedQuadFrame = isVR && engine->vrQuadFrameReady;
	bool releaseProjectionMenu = isVR && engine->vrReleaseMenuOnly && engine->IsVRScreenUIActive() && !dedicatedQuadFrame;
	bool bNoDrawWorld = engine->console->bNoDrawWorld();
	bool worldDrawnThisFrame = false;

	if (engine->LaunchInfo.ue1Version <= 219 || bNoDrawWorld == false)
	{
		worldDrawnThisFrame = true;
		if (isVR)
			DrawSceneVR();
		else if (commandline && commandline->HasArg("", "--debugstereo"))
			DrawSceneStereo();
		else
			DrawScene();
		if (isVR)
		{
			if (!dedicatedQuadFrame && !releaseProjectionMenu)
				RenderOverlaysVR();
		}
		else
			RenderOverlays();
		if (engine->LaunchInfo.IsDeusEx())
			PostRenderFlash();
		// A full-screen flash would turn the transparent menu-hand projection
		// layer opaque and cover the compositor quad. Normal gameplay keeps the
		// original flash path unchanged.
		if (!dedicatedQuadFrame)
			Device->EndFlash();
	}
	else if (isVR)
	{
		if (dedicatedQuadFrame)
		{
			// bNoDrawWorld is expected for UT's UWindow menu, but the menu still
			// needs a real stereo frame for tracked controllers and its laser.
			// This consumes PendingVR and draws only transparent-overlay geometry.
			DrawVRMenuTrackedOverlay();
		}
		else
		{
		// 2026-07-22 (VR_SCREEN_QUAD_PLAN_2026-07-22.md Phase 1 item 2): world
		// drawing suppressed (fullscreen console/menu) but a VR frame is
		// active - `isVR` must stay authoritative here regardless of
		// bNoDrawWorld(), or every VR menu/HUD containment path (PostRenderVR)
		// silently falls back to full-frame flatscreen PostRender() instead
		// (CODEX_ANALYSIS_2026-07-22.md's P0/P1 - the leading explanation for
		// "no containment at all" once the compiled front-end sets this
		// flag). DrawSceneVR() is what draws the world, so it's skipped on
		// purpose here - but VREyeFrame[] would otherwise be left stale from
		// whichever earlier frame last drew the world, and PendingVR would
		// leak into next frame's DrawSceneVR() call. Consume PendingVR and
		// refresh VREyeFrame[] with a plain per-eye HUD rect, same
		// SetVRHudFrame()-only pattern DrawSceneVR()'s inEntryMap+bShowMenu()
		// early-out already uses (RenderScene.cpp) - duplicated narrowly here
		// rather than touching that Phase-2-adjacent branch.
		PendingVR = false;

		FSceneNode fullFrame = Canvas.Frame;
		int fullSizeX = engine->canvas->SizeX();
		int fullSizeY = engine->canvas->SizeY();
		float fullClipX = engine->canvas->ClipX();
		float fullClipY = engine->canvas->ClipY();

		for (int eye = 0; eye < 2; eye++)
		{
			SetVRHudFrame(eye, fullFrame);
			VREyeFrame[eye] = Canvas.Frame;
		}

		Canvas.Frame = fullFrame;
		engine->canvas->CurX() = 0.0f;
		engine->canvas->CurY() = 0.0f;
		engine->canvas->ClipX() = fullClipX;
		engine->canvas->ClipY() = fullClipY;
		engine->canvas->SizeX() = fullSizeX;
		engine->canvas->SizeY() = fullSizeY;
		}
	}
	// else: non-VR with bNoDrawWorld true - isVR is already false, falls
	// straight through to the flatscreen PostRender() below unchanged.

	// 2026-07-22 diagnostic (VR_SCREEN_QUAD_PLAN_2026-07-22.md Phase 1 item 1):
	// the DrawGame branch choice above lives entirely in local state, invisible
	// to Engine.cpp's "VR menu diag" log - log it on any change so a real-
	// headset run can tell which path a frame actually took (world drawn vs.
	// skipped, VR vs. flatscreen PostRender) instead of inferring it from
	// bShowMenu() alone.
	{
		static bool prevIsVR = false;
		static bool prevBNoDrawWorld = false;
		static bool prevWorldDrawn = true;
		static bool prevDedicatedQuadFrame = false;
		if (isVR != prevIsVR || bNoDrawWorld != prevBNoDrawWorld || worldDrawnThisFrame != prevWorldDrawn || dedicatedQuadFrame != prevDedicatedQuadFrame)
		{
			LogMessage(std::string("VR menu diag: DrawGame branch isVR=") + (isVR ? "true" : "false") +
				" bNoDrawWorld=" + (bNoDrawWorld ? "true" : "false") +
				" worldDrawn=" + (worldDrawnThisFrame ? "true" : "false") +
				" dedicatedQuad=" + (dedicatedQuadFrame ? "true" : "false"));
			prevIsVR = isVR;
			prevBNoDrawWorld = bNoDrawWorld;
			prevWorldDrawn = worldDrawnThisFrame;
			prevDedicatedQuadFrame = dedicatedQuadFrame;
		}
	}

	if (isVR && !dedicatedQuadFrame)
		PostRenderVR();
	else if (!isVR)
		PostRender();

	Device->Unlock(true);

	// 2026-07-22 (VR_SCREEN_QUAD_PLAN_2026-07-22.md Phase 2): Entry-map/menu
	// quad content - rendered in its own separate Lock/Draw/Unlock cycle
	// (Device->LockQuadTarget()/UnlockQuadTarget(), its own render pass/
	// present pass/submit - see VulkanRenderDevice::EnsureQuadTarget())
	// rather than interleaved into the main scene's Lock(...)/Unlock(true)
	// above, so this doesn't have to restructure Unlock()'s public contract
	// for every existing caller. Costs one extra blocking submit+wait per
	// frame while Entry/menu is active - not performance-critical, since
	// this is never real, time-critical gameplay. showingMenu takes
	// priority over inEntryMap (matches DrawSceneVR()'s old comment: hitting
	// Escape during the Entry map replaces the flythrough outright rather
	// than overlaying the menu on top of it).
	if (dedicatedQuadFrame)
	{
		bool showingMenu = engine->IsVRScreenUIActive();
		if (showingMenu)
			DrawMenuQuad();
		else if (engine->inEntryMap)
			DrawEntryQuad();
	}
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

	// M3 follow-up: PendingVR/SetPendingVREyes is the same one-shot
	// "was a VR eye pose given for this draw" convention DrawGame() uses via
	// DrawSceneVR() - consumed (cleared) here instead, since no DrawSceneVR()
	// call happens along this path to do it. Engine::PlayVideo() calls
	// SetPendingVREyes() immediately before each DrawVideoFrame() call, same
	// 1:1 pairing DrawGame() has with Run()'s XR frame block.
	bool isVR = PendingVR;
	PendingVR = false;

	if (isVR && frame)
	{
		// Video playback has no world scene to split across eyes - reuse
		// SetVRHudFrame()'s per-eye placement/convergence math (already
		// shared by RenderOverlaysVR()/PostRenderVR() for the HUD) with a
		// much wider FOV and farther depth so a legible "virtual screen"
		// fuses comfortably instead of the HUD's compact sizing. aspectYtoX
		// comes from the actual decoded frame so the screen matches the
		// video's real aspect ratio rather than assuming 4:3.
		FSceneNode fullFrame = Canvas.Frame;
		int fullSizeX = engine->canvas->SizeX();
		int fullSizeY = engine->canvas->SizeY();
		float fullClipX = engine->canvas->ClipX();
		float fullClipY = engine->canvas->ClipY();

		float aspectYtoX = (frame->USize > 0) ? (float)frame->VSize / (float)frame->USize : 0.75f;

		for (int eye = 0; eye < 2; eye++)
		{
			SetVRHudFrame(eye, fullFrame, 45.0f, aspectYtoX, 170.0f);
			Device->SetSceneNode(&Canvas.Frame);

			float sizeX = (float)engine->canvas->SizeX();
			float sizeY = (float)engine->canvas->SizeY();
			Rectf clipBox = Rectf::xywh(0.0f, 0.0f, sizeX, sizeY);
			Rectf dest = clipBox;

			Rectf src = Rectf::xywh(0.0f, 0.0f, (float)frame->USize, (float)frame->VSize);
			DrawTile(*frame, dest, src, clipBox, 1.0f, vec4(1.0f), vec4(0.0f), PF_TwoSided);

			if (background)
			{
				Rectf bgSrc = Rectf::xywh(0.0f, 0.0f, (float)background->USize, (float)background->VSize);
				DrawTile(*background, dest, bgSrc, clipBox, 1.0f, vec4(1.0f), vec4(0.0f), PF_TwoSided | PF_Highlighted);
			}
		}

		Canvas.Frame = fullFrame;
		engine->canvas->CurX() = 0.0f;
		engine->canvas->CurY() = 0.0f;
		engine->canvas->ClipX() = fullClipX;
		engine->canvas->ClipY() = fullClipY;
		engine->canvas->SizeX() = fullSizeX;
		engine->canvas->SizeY() = fullSizeY;
		Device->SetSceneNode(&Canvas.Frame);
	}
	else
	{
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
