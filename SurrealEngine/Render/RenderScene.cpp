
#include "Precomp.h"
#include "RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "GameWindow.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include "VisibleFrame.h"

void RenderSubsystem::DrawScene()
{
	if (!engine->Level)
		return;

	Light.FogFrameCounter++;
	TextureFrameCounter++;

	// Make sure all actors are at the right location in the BSP
	for (UActor* actor : engine->Level->Actors)
	{
		if (actor)
			actor->UpdateBspInfo();
	}

	mat4 worldToView = Coords::ViewToRenderDev().ToMatrix() * Coords::Rotation(engine->CameraRotation).Inverse().ToMatrix() * Coords::Location(engine->CameraLocation).ToMatrix();
	MainFrame.Process(engine->CameraLocation, worldToView, Coords::Rotation(engine->CameraRotation));
	MainFrame.Draw();
	MainFrame.DrawCoronas();
}

void RenderSubsystem::DrawSceneStereo()
{
	if (!engine->Level)
		return;

	Light.FogFrameCounter++;
	TextureFrameCounter++;

	for (UActor* actor : engine->Level->Actors)
	{
		if (actor)
			actor->UpdateBspInfo();
	}

	Coords rotation = Coords::Rotation(engine->CameraRotation);
	Coords invRotation = rotation.Inverse();

	// Debug-only fake half-IPD (UE1 units, ~1 unit = 1/32 inch => ~32
	// units = ~1 inch each way, ~2 inch/~5cm total separation). Not
	// calibrated to any real headset - this mode exists purely to prove
	// the viewport-override + per-eye worldToView + asymmetric-projection
	// plumbing renders two visibly different (parallax-shifted) views,
	// ahead of a real OpenXR session ever existing.
	const float halfIPD = 32.0f;

	// Zero-parallax convergence distance for the debug asymmetric frustum
	// shear below - arbitrary mid-range pick, not derived from any real
	// depth budget. Real OpenXR per-eye projections come directly from
	// xrLocateViews's fov angles instead of this approximation.
	const float convergence = 500.0f;

	int fullX = engine->viewport->ViewportX();
	int fullY = engine->viewport->ViewportY();
	int fullWidth = engine->viewport->ViewportWidth();
	int fullHeight = engine->viewport->ViewportHeight();
	int halfWidth = fullWidth / 2;

	for (int eye = 0; eye < 2; eye++)
	{
		float sign = (eye == 0) ? -1.0f : 1.0f;
		vec3 eyeLocation = engine->CameraLocation + rotation.YAxis * (halfIPD * sign);
		mat4 worldToView = Coords::ViewToRenderDev().ToMatrix() * invRotation.ToMatrix() * Coords::Location(eyeLocation).ToMatrix();

		ViewportOverride vp;
		vp.XB = fullX + (eye == 0 ? 0 : halfWidth);
		vp.YB = fullY;
		vp.X = halfWidth;
		vp.Y = fullHeight;

		// Parallel-axis cameras (no toe-in, worldToView above is a pure
		// translation) + an off-axis (asymmetric) frustum that shears
		// toward the opposite eye so both frustums converge on the same
		// point at `convergence` distance. This is the physically-correct
		// stereo method and the same shape of asymmetry a real per-eye
		// OpenXR projection has, proving FSceneNode::ProjectionOverride
		// end-to-end.
		float aspect = (float)vp.Y / (float)vp.X;
		float rProjZ = (float)std::tan(radians(engine->CameraFovAngle) * 0.5f);
		float frustumShift = -sign * halfIPD * (1.0f / convergence);
		mat4 projection = mat4::frustum(-rProjZ + frustumShift, rProjZ + frustumShift, -aspect * rProjZ, aspect * rProjZ, 1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);
		vp.Projection = &projection;

		MainFrame.Process(eyeLocation, worldToView, rotation, false, 0, {}, vec4(0.0f, 0.0f, 0.0f, 1.0f), &vp);
		MainFrame.Draw();
		MainFrame.DrawCoronas();
	}
}

void RenderSubsystem::SetPendingVREyes(const vec3 loc[2], const Coords rot[2], const float fov[2][4])
{
	PendingVR = true;
	for (int eye = 0; eye < 2; eye++)
	{
		VREyeLocation[eye] = loc[eye];
		VREyeRotation[eye] = rot[eye];
		for (int i = 0; i < 4; i++)
			VREyeFov[eye][i] = fov[eye][i];
	}
}

// M3: real per-eye VR rendering. Structurally identical to DrawSceneStereo
// (same split-viewport-of-one-buffer approach, same ViewportOverride +
// asymmetric mat4::frustum plumbing proven there) but every per-eye value
// is real, tracked OpenXR data computed in Engine::Run() instead of a fake
// debug IPD - see the doc comment above Engine::Run()'s XR frame loop for
// the OpenXR-to-UE1 axis/scale conversion this pose data went through.
void RenderSubsystem::DrawSceneVR()
{
	PendingVR = false;

	if (!engine->Level)
		return;

	Light.FogFrameCounter++;
	TextureFrameCounter++;

	for (UActor* actor : engine->Level->Actors)
	{
		if (actor)
			actor->UpdateBspInfo();
	}

	int fullX = engine->viewport->ViewportX();
	int fullY = engine->viewport->ViewportY();
	// Use the render device's actual target size (the OpenXR swapchain
	// resolution once VR is active - see Engine.cpp's SetFixedRenderSize()
	// call after CreateSwapchains() - not the desktop mirror window's size)
	// so the per-eye viewport matches the real framebuffer instead of
	// cropping/underfilling it. Same Device->GetRenderWidth()/Height()
	// convention VisibleFrame::SetupSceneFrame() already uses.
	int fullWidth = Device->GetRenderWidth();
	int fullHeight = Device->GetRenderHeight();
	int halfWidth = fullWidth / 2;

	// 2026-07-22 (VR_SCREEN_QUAD_PLAN_2026-07-22.md Phase 2): the Entry-map
	// When this frame has a successfully acquired quad target, the Entry-map
	// boot flythrough or pause/escape menu is rendered once,
	// monoscopically, into the standalone offscreen quad target and
	// presented as a real world-anchored OpenXR quad layer instead of a
	// HUD-style per-eye sub-rect (see RenderSubsystem::DrawGame()'s
	// DrawEntryQuad()/DrawMenuQuad() call, made in a separate Lock/Unlock
	// cycle after this function's caller returns) - so neither case has any
	// per-eye content left to render here; this stereo pass just blanks and
	// returns. RenderSubsystem::DrawGame()'s Device->Lock() call already
	// cleared this frame's target to black before DrawSceneVR() was called,
	// so leaving it untouched gives the quad layer a blank backdrop instead
	// of a live or frozen world behind it. VREyeFrame[] is still refreshed
	// via the plain SetVRHudFrame() overload (no MainFrame::Process()/
	// Draw()) so RenderOverlaysVR(), which runs unconditionally right after
	// this function returns, restores a current-frame rect instead of one
	// stale from before Entry/menu activated.
	//
	// If acquiring the quad image failed, vrQuadFrameReady remains false and
	// this function deliberately renders the ordinary stereo scene/menu as a
	// usable fallback instead of leaving the player in a black frame.
	//
	// JUDGMENT CALL / behavior change: this also now blanks real gameplay
	// (inEntryMap false) whenever bShowMenu() is true, where the pre-Phase-2
	// code fell through to the live per-eye stereo world render instead (the
	// world stayed visible, live, behind the 2D menu overlay). Matches this
	// plan's design for the Entry+menu combination; not independently
	// verified in-headset for the "menu opened mid-real-gameplay" case.
	bool releaseProjectionMenu = engine->vrReleaseMenuOnly && engine->IsVRScreenUIActive();
	if (engine->vrQuadFrameReady)
	{
		DrawVRMenuTrackedOverlay();
		return;
	}
	if (releaseProjectionMenu)
	{
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
		return;
	}

	for (int eye = 0; eye < 2; eye++)
	{
		Coords rotation = VREyeRotation[eye];
		Coords invRotation = rotation.Inverse();
		mat4 worldToView = Coords::ViewToRenderDev().ToMatrix() * invRotation.ToMatrix() * Coords::Location(VREyeLocation[eye]).ToMatrix();

		ViewportOverride vp;
		vp.XB = fullX + (eye == 0 ? 0 : halfWidth);
		vp.YB = fullY;
		vp.X = halfWidth;
		vp.Y = fullHeight;

		// angleLeft/angleDown are negative per the OpenXR spec. Horizontal
		// is a direct l/r pass-through (renderdev eye space is x-right, no
		// flip). Vertical is NOT a direct pass-through: renderdev eye space
		// is y-DOWN (Coords::ViewToRenderDev(), Math/coords.h) while
		// mat4::frustum()'s bottom/top follow the GL convention (bottom ->
		// NDC -1 -> Vulkan framebuffer TOP row). Passing (d, u) as
		// (bottom, top) therefore puts angleDown's extent at the
		// framebuffer top and angleUp's at the bottom - backwards whenever
		// angleUp != |angleDown| (true on essentially every real HMD) - and
		// disagrees with the FOV metadata EndFrame() submits to the
		// compositor, which is what produced the "world geometry warps as
		// you turn your head" bug. bottom/top must be (-u, -d) instead so
		// the framebuffer top row gets angleUp's extent. Root-caused
		// 2026-07-20, see Docs/VR/FABLE_ANALYSIS_2026-07-20.md section 3;
		// gated on the one-shot FOV log in VulkanXRSession::LocateViews()
		// confirming angleUp != |angleDown| on the actual runtime before
		// this was applied. Fixed here without re-verifying in headset yet
		// - do that before trusting this comment over a real test.
		float l = std::tan(VREyeFov[eye][0]);
		float r = std::tan(VREyeFov[eye][1]);
		float u = std::tan(VREyeFov[eye][2]);
		float d = std::tan(VREyeFov[eye][3]);
		mat4 projection = mat4::frustum(l, r, -u, -d, 1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);
		vp.Projection = &projection;

		MainFrame.Process(VREyeLocation[eye], worldToView, rotation, false, 0, {}, vec4(0.0f, 0.0f, 0.0f, 1.0f), &vp);
		VREyeFrame[eye] = MainFrame.Frame;
		MainFrame.Draw();
		MainFrame.DrawCoronas();
	}
}

// Build a normal asymmetric per-eye perspective frame, but render only
// tracked controller proxies and the main-hand pointer ray. The main target
// was cleared to transparent by DrawGame's Device->Lock(), and no world/HUD
// draw occurs here. VulkanXRSession submits these eye images as a source-alpha
// projection layer AFTER the opaque menu quad, yielding real stereo geometry
// at the tracked poses without letting the game world cover the menu.
void RenderSubsystem::DrawVRMenuTrackedOverlay()
{
	PendingVR = false;
	if (!engine->Level)
		return;

	int fullX = engine->viewport->ViewportX();
	int fullY = engine->viewport->ViewportY();
	int fullWidth = Device->GetRenderWidth();
	int fullHeight = Device->GetRenderHeight();
	int halfWidth = fullWidth / 2;

	// WhiteTexture is an Unreal 227-only LevelInfo property and is not safe
	// to access in UT99. DefaultTexture exists in the common UE1 LevelInfo
	// layout and provides a reliable opaque texture for these tinted proxies.
	UTexture* solidTexture = engine->LevelInfo ? engine->LevelInfo->DefaultTexture() : nullptr;
	FTextureInfo solidInfo;
	if (solidTexture)
	{
		UpdateTexture(solidTexture);
		UpdateTextureInfo(solidInfo, solidTexture);
	}

	auto drawLine = [&](FSceneNode* frame, vec4 color, const vec3& a, const vec3& b)
	{
		Device->Draw3DLine(frame, color, 0, a, b);
	};

	auto drawBox = [&](FSceneNode* frame, const vec3& center, const Coords& axes, const vec3& halfSize, vec3 color)
	{
		vec3 corners[8];
		for (int i = 0; i < 8; i++)
		{
			float sx = (i & 1) ? 1.0f : -1.0f;
			float sy = (i & 2) ? 1.0f : -1.0f;
			float sz = (i & 4) ? 1.0f : -1.0f;
			corners[i] = center + axes.XAxis * (sx * halfSize.x) + axes.YAxis * (sy * halfSize.y) + axes.ZAxis * (sz * halfSize.z);
		}

		if (solidTexture)
		{
			static const int faces[6][4] =
			{
				{ 0, 2, 3, 1 }, { 4, 5, 7, 6 },
				{ 0, 1, 5, 4 }, { 2, 6, 7, 3 },
				{ 0, 4, 6, 2 }, { 1, 3, 7, 5 }
			};
			for (const auto& face : faces)
			{
				GouraudVertex vertices[4];
				for (int i = 0; i < 4; i++)
				{
					vertices[i].Point = corners[face[i]];
					vertices[i].Light = color;
					vertices[i].UV = vec2(0.0f);
					vertices[i].Fog = vec4(0.0f);
				}
				Device->DrawGouraudPolygon(frame, solidInfo, vertices, 4, PF_TwoSided | PF_Unlit | PF_NoSmooth);
			}
		}

		// Bright wire edges keep the proxy readable even if the game's white
		// utility texture is unavailable and the solid faces are skipped.
		vec3 bright(clamp(color.x * 1.5f, 0.0f, 1.0f), clamp(color.y * 1.5f, 0.0f, 1.0f), clamp(color.z * 1.5f, 0.0f, 1.0f));
		vec4 edgeColor(bright, 1.0f);
		static const int edges[12][2] =
		{
			{0,1},{0,2},{0,4},{1,3},{1,5},{2,3},
			{2,6},{3,7},{4,5},{4,6},{5,7},{6,7}
		};
		for (const auto& edge : edges)
			drawLine(frame, edgeColor, corners[edge[0]], corners[edge[1]]);
	};

	auto drawController = [&](FSceneNode* frame, const Engine::VRHandState& hand, bool main)
	{
		if (!hand.valid || !engine->vrMenuControllersEnabled)
			return;
		vec3 color = main ? vec3(0.08f, 0.45f, 1.0f) : vec3(1.0f, 0.32f, 0.05f);
		// Two oriented solids form a recognizable controller body and grip at
		// true tracked scale (~13 cm body, ~10 cm handle).
		drawBox(frame, hand.gripPos + hand.gripCoords.XAxis * 1.7f + hand.gripCoords.ZAxis * 0.5f,
			hand.gripCoords, vec3(2.6f, 1.55f, 1.05f), color);
		drawBox(frame, hand.gripPos - hand.gripCoords.XAxis * 0.35f - hand.gripCoords.ZAxis * 1.75f,
			hand.gripCoords, vec3(1.05f, 1.0f, 2.05f), color * 0.72f);

		// Small orientation/button marks make controller roll and handedness
		// immediately visible instead of looking like an unlabelled block.
		vec3 top = hand.gripPos + hand.gripCoords.XAxis * 2.0f + hand.gripCoords.ZAxis * 1.65f;
		vec4 mark(main ? 0.25f : 1.0f, 1.0f, main ? 0.35f : 0.2f, 1.0f);
		drawLine(frame, mark, top - hand.gripCoords.YAxis * 0.55f, top + hand.gripCoords.YAxis * 0.55f);
		drawLine(frame, mark, top - hand.gripCoords.XAxis * 0.55f, top + hand.gripCoords.XAxis * 0.55f);
	};

	for (int eye = 0; eye < 2; eye++)
	{
		Coords rotation = VREyeRotation[eye];
		Coords invRotation = rotation.Inverse();
		mat4 worldToView = Coords::ViewToRenderDev().ToMatrix() * invRotation.ToMatrix() * Coords::Location(VREyeLocation[eye]).ToMatrix();

		ViewportOverride vp;
		vp.XB = fullX + (eye == 0 ? 0 : halfWidth);
		vp.YB = fullY;
		vp.X = halfWidth;
		vp.Y = fullHeight;
		float l = std::tan(VREyeFov[eye][0]);
		float r = std::tan(VREyeFov[eye][1]);
		float u = std::tan(VREyeFov[eye][2]);
		float d = std::tan(VREyeFov[eye][3]);
		mat4 projection = mat4::frustum(l, r, -u, -d, 1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);
		vp.Projection = &projection;

		MainFrame.Process(VREyeLocation[eye], worldToView, rotation, false, 0, {}, vec4(0.0f, 0.0f, 0.0f, 1.0f), &vp);
		VREyeFrame[eye] = MainFrame.Frame;
		Device->SetSceneNode(&MainFrame.Frame);

		drawController(&MainFrame.Frame, engine->OffHand(), false);
		drawController(&MainFrame.Frame, engine->MainHand(), true);

		const Engine::VRHandState& mainHand = engine->MainHand();
		if (engine->vrMenuLaserEnabled && mainHand.valid && engine->vrMenuRayVisible)
		{
			vec3 start = mainHand.aimPos;
			vec3 end = engine->vrMenuRayEndUE;
			vec3 direction = normalize(end - start);
			vec3 side = cross(direction, vec3(0.0f, 0.0f, 1.0f));
			if (dot(side, side) < 0.0001f)
				side = cross(direction, vec3(0.0f, 1.0f, 0.0f));
			side = normalize(side) * 0.10f;
			vec3 up = normalize(cross(side, direction)) * 0.10f;
			vec4 beamColor = engine->vrMenuRayHitsPanel ? vec4(0.08f, 1.0f, 0.22f, 1.0f) : vec4(1.0f, 0.22f, 0.05f, 1.0f);
			drawLine(&MainFrame.Frame, beamColor, start, end);
			drawLine(&MainFrame.Frame, beamColor, start + side, end + side);
			drawLine(&MainFrame.Frame, beamColor, start - side, end - side);
			drawLine(&MainFrame.Frame, beamColor, start + up, end + up);
			drawLine(&MainFrame.Frame, beamColor, start - up, end - up);

			// A small 3D hit marker lies on the actual quad plane endpoint.
			float arm = 0.65f;
			drawLine(&MainFrame.Frame, beamColor, end - engine->vrQuadRightUE * arm, end + engine->vrQuadRightUE * arm);
			drawLine(&MainFrame.Frame, beamColor, end - engine->vrQuadUpUE * arm, end + engine->vrQuadUpUE * arm);
		}
	}
}

// 2026-07-22 (VR_SCREEN_QUAD_PLAN_2026-07-22.md Phase 2): renders the
// Entry-map boot flythrough ONCE per frame, monoscopically, into the
// standalone offscreen quad render target (Device->LockQuadTarget()/
// UnlockQuadTarget() - see VulkanRenderDevice::EnsureQuadTarget()), for
// presentation as a real world-anchored OpenXR quad layer instead of the
// old per-eye HUD sub-rect (DrawSceneVR()'s superseded inEntryMap branch -
// see that function's doc comment). Called from RenderSubsystem::DrawGame()
// in its own Lock/Unlock cycle, after DrawSceneVR() has already run this
// same frame (which still does the Level->Actors BSP-info refresh and
// Light.FogFrameCounter/TextureFrameCounter bookkeeping unconditionally, at
// its top, before its now-unified blank branch returns) - so neither is
// repeated here.
//
// Uses the engine's ordinary CameraLocation/CameraRotation scripted pose,
// same as DrawScene(), rather than any head-tracked eye pose - the
// flythrough camera is scripted, not player-controlled. Unlike before
// Phase 2, VREntryFlythroughHalfFovXDeg/AspectYtoX are now the rendered
// camera's OWN field of view (this content is no longer placed as a HUD
// sub-rect at VREntryFlythroughDepthUU - that distance/size instead only
// controls the quad LAYER's physical placement in the headset, computed
// once by Engine.cpp's Run() - see vrQuadActive's doc comment in Engine.h).
void RenderSubsystem::DrawEntryQuad()
{
	if (!engine->Level)
		return;

	Coords rotation = Coords::Rotation(engine->CameraRotation);
	Coords invRotation = rotation.Inverse();
	vec3 location = engine->CameraLocation;
	mat4 worldToView = Coords::ViewToRenderDev().ToMatrix() * invRotation.ToMatrix() * Coords::Location(location).ToMatrix();

	const float halfTanX = std::tan(radians(VREntryFlythroughHalfFovXDeg));
	const float halfTanY = halfTanX * VREntryFlythroughAspectYtoX;
	mat4 projection = mat4::frustum(-halfTanX, halfTanX, -halfTanY, halfTanY, 1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);

	int quadWidth = Device->GetQuadWidth();
	int quadHeight = Device->GetQuadHeight();

	Device->LockQuadTarget(vec4(0.0f, 0.0f, 0.0f, 1.0f));

	ViewportOverride vp;
	vp.XB = 0;
	vp.YB = 0;
	vp.X = quadWidth;
	vp.Y = quadHeight;
	vp.Projection = &projection;

	MainFrame.Process(location, worldToView, rotation, false, 0, {}, vec4(0.0f, 0.0f, 0.0f, 1.0f), &vp);
	MainFrame.Draw();
	MainFrame.DrawCoronas();

	Device->UnlockQuadTarget();
}
