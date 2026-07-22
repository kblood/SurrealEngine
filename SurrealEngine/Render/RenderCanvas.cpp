
#include "Precomp.h"
#include "RenderSubsystem.h"
#include "VisibleMesh.h"
#include "RenderDevice/RenderDevice.h"
#include "UObject/USubsystem.h"
#include "GameWindow.h"
#include "VM/ScriptCall.h"
#include "Engine.h"

void RenderSubsystem::ResetCanvas()
{
	// Scale the UI so it matches what you saw on a 1024x768 CRT monitor for Unreal and other older games.
	// Assume 1280x960 for UT and newer.
	// Use the render device's actual target size, not the desktop viewport's
	// - normally identical (Engine::Run() keeps viewport->SetViewportRect
	// synced to the window every frame), but once VR is active the render
	// target is pinned to the (usually much larger) OpenXR swapchain
	// resolution via RenderDevice::SetFixedRenderSize() - see Engine.cpp -
	// while engine->viewport keeps tracking the small desktop mirror window.
	// Same Device->GetRenderWidth()/Height() convention
	// VisibleFrame::SetupSceneFrame() and DrawSceneVR() already use.
	int vertResolution = engine->LaunchInfo.ue1Version < 400 ? 768 : 960;
	Canvas.uiscale = std::max((Device->GetRenderHeight() + vertResolution / 2) / vertResolution, 1);

	FSceneNode frame;
	Canvas.Frame.XB = 0;
	Canvas.Frame.YB = 0;
	Canvas.Frame.X = Device->GetRenderWidth();
	Canvas.Frame.Y = Device->GetRenderHeight();
	Canvas.Frame.FX = (float)Device->GetRenderWidth();
	Canvas.Frame.FY = (float)Device->GetRenderHeight();
	Canvas.Frame.FX2 = Canvas.Frame.FX * 0.5f;
	Canvas.Frame.FY2 = Canvas.Frame.FY * 0.5f;
	Canvas.Frame.ObjectToWorld = mat4::identity();
	Canvas.Frame.WorldToView = mat4::identity();
	Canvas.Frame.FovAngle = engine->CameraFovAngle;
	float Aspect = Canvas.Frame.FY / Canvas.Frame.FX;
	float RProjZ = (float)std::tan(radians(Canvas.Frame.FovAngle) * 0.5f);
	float RFX2 = 2.0f * RProjZ / Canvas.Frame.FX;
	float RFY2 = 2.0f * RProjZ * Aspect / Canvas.Frame.FY;
	Canvas.Frame.Projection = mat4::frustum(-RProjZ, RProjZ, -Aspect * RProjZ, Aspect * RProjZ, 1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);

	int sizeX = (int)(Device->GetRenderWidth() / (float)Canvas.uiscale);
	int sizeY = (int)(Device->GetRenderHeight() / (float)Canvas.uiscale);
	engine->canvas->CurX() = 0.0f;
	engine->canvas->CurY() = 0.0f;
	if (engine->LaunchInfo.ue1Version > 219)
	{
		engine->console->FrameX() = (float)sizeX;
		engine->console->FrameY() = (float)sizeY;
	}
	engine->canvas->ClipX() = (float)sizeX;
	engine->canvas->ClipY() = (float)sizeY;
	engine->canvas->SizeX() = sizeX;
	engine->canvas->SizeY() = sizeY;
	//engine->viewport->bShowWindowsMouse() = true; // bShowWindowsMouse is set to true by WindowConsole if mouse cursor should be visible
	//engine->viewport->bWindowsMouseAvailable() = true; // if true then RenderUWindow updates mouse pos from (WindowsMouseX,WindowsMouseY), otherwise it uses KeyEvent(IK_MouseX, delta) + KeyEvent(IK_MouseY, delta). Maybe used for windowed mode?
	//engine->viewport->WindowsMouseX() = 10.0f;
	//engine->viewport->WindowsMouseY() = 200.0f;
	CallEvent(engine->canvas, EventName::Reset);
}

void RenderSubsystem::PreRender()
{
	Device->SetSceneNode(&Canvas.Frame);
	CallEvent(engine->console, EventName::PreRender, { ExpressionValue::ObjectValue(engine->canvas) });
	if (engine->viewport->Actor())
		CallEvent(engine->viewport->Actor(), EventName::PreRender, { ExpressionValue::ObjectValue(engine->canvas) });
}

void RenderSubsystem::RenderOverlays()
{
	Device->SetSceneNode(&Canvas.Frame);
	if (engine->viewport->Actor())
	{
		if (engine->LaunchInfo.ue1Version > 219)
		{
			CallEvent(engine->viewport->Actor(), EventName::RenderOverlays, { ExpressionValue::ObjectValue(engine->canvas) });
		}
		else
		{
			UWeapon* weapon = engine->viewport->Actor()->Weapon();
			if (weapon)
			{
				CallEvent(weapon, "InvCalcView", {});
				DrawActor(weapon, false, false);
			}
		}
	}

	// M-B: off-hand placeholder marker - see DrawVROffHandMarker()'s doc
	// comment. Flatscreen path (this function runs whenever the VR stereo
	// path isn't active, including a --debugvrhands run with no working XR
	// session on this build machine) - MainFrame.Frame is the normal
	// single-camera frame here (set by DrawScene()'s MainFrame.Process()
	// just before RenderOverlays() runs), so this draws once, not per-eye.
	DrawVROffHandMarker();

	// 2026-07-21: main-hand marker, only while grip-capture is parking the
	// weapon away from the hand - see DrawVRMainHandMarker()'s doc comment.
	if (engine->vrGripCalibrateActive)
		DrawVRMainHandMarker();
}

// M4 (2026-07-20): places this eye's HUD canvas at a fixed-size "virtual
// screen" centered on the eye's real forward gaze direction, instead of the
// old "centered half-viewport" split. The old approach drew both eyes'
// HUD/crosshair centered in their raw half-viewport; that only fuses into
// one image if "viewport center" maps to the same real-world gaze direction
// in both eyes, which is false for a real OpenXR session (per-eye FOV is
// asymmetric - angleLeft != angleRight etc.) - hence the reported double
// vision, and separately, HUD elements pinned to the full ~90-100 deg
// per-eye FOV landing outside the lens sweet spot. Fixes both by shrinking
// and repositioning the canvas rect:
//   - horizontal: pixel x = halfWidth * (t - tanL) / (tanR - tanL), so
//     t=0 (straight ahead) lands at this eye's real forward pixel, not the
//     viewport's geometric center.
//   - vertical: pixel y = fullHeight * (tanU - t) / (tanU - tanD), the
//     inverse of the corrected DrawSceneVR() vertical frustum mapping (see
//     that function's doc comment) - MUST be applied after that fix, since
//     this formula assumes framebuffer row 0 is angleUp's extent.
//   - convergence: both eyes' centered-on-infinity copies would still only
//     fuse at infinite depth, which is an uncomfortable vergence conflict
//     against nearby world geometry - `shift` pulls each eye's copy toward
//     the other by half the tracked IPD (converted to a tan-space offset at
//     `hudDepthUU`), so the fused HUD sits at a finite, comfortable depth
//     instead.
// See Docs/VR/FABLE_ANALYSIS_2026-07-20.md sections 1-2 for the full
// derivation. IPD is read live from VREyeLocation every call (never a
// constant) since it varies per headset/runtime/IPD-slider setting.
void RenderSubsystem::SetVRHudFrame(int eye, const FSceneNode& fullFrame, float halfFovXDeg, float aspectYtoX, float depthUU)
{
	int fullWidth = fullFrame.X;
	int fullHeight = fullFrame.Y;
	int halfWidth = fullWidth / 2;

	float tanL = std::tan(VREyeFov[eye][0]);
	float tanR = std::tan(VREyeFov[eye][1]);
	float tanU = std::tan(VREyeFov[eye][2]);
	float tanD = std::tan(VREyeFov[eye][3]);

	const float hudHalfTanX = std::tan(radians(halfFovXDeg));
	const float hudHalfTanY = hudHalfTanX * aspectYtoX;
	const float hudDepthUU = depthUU;                    // 1 UU = 1 inch - see Engine.cpp's UUPerMeter

	float ipdUU = length(VREyeLocation[1] - VREyeLocation[0]);
	float shift = (eye == 0 ? 1.0f : -1.0f) * (ipdUU * 0.5f) / hudDepthUU;

	int x0 = (int)std::round(halfWidth * ((-hudHalfTanX + shift) - tanL) / (tanR - tanL));
	int x1 = (int)std::round(halfWidth * ((hudHalfTanX + shift) - tanL) / (tanR - tanL));
	int y0 = (int)std::round(fullHeight * (tanU - hudHalfTanY) / (tanU - tanD));
	int y1 = (int)std::round(fullHeight * (tanU + hudHalfTanY) / (tanU - tanD));

	int hudW = std::max(x1 - x0, 1);
	int hudH = std::max(y1 - y0, 1);

	Canvas.Frame = fullFrame;
	Canvas.Frame.XB = fullFrame.XB + (eye == 0 ? 0 : halfWidth) + x0;
	Canvas.Frame.YB = fullFrame.YB + y0;
	Canvas.Frame.X = hudW;
	Canvas.Frame.Y = hudH;
	Canvas.Frame.FX = (float)hudW;
	Canvas.Frame.FY = (float)hudH;
	Canvas.Frame.FX2 = Canvas.Frame.FX * 0.5f;
	Canvas.Frame.FY2 = Canvas.Frame.FY * 0.5f;

	int hudSizeX = (int)(hudW / (float)Canvas.uiscale);
	int hudSizeY = (int)(hudH / (float)Canvas.uiscale);
	engine->canvas->CurX() = 0.0f;
	engine->canvas->CurY() = 0.0f;
	engine->canvas->ClipX() = (float)hudSizeX;
	engine->canvas->ClipY() = (float)hudSizeY;
	engine->canvas->SizeX() = hudSizeX;
	engine->canvas->SizeY() = hudSizeY;
}

void RenderSubsystem::RenderOverlaysVR()
{
	FSceneNode fullFrame = Canvas.Frame;
	int fullSizeX = engine->canvas->SizeX();
	int fullSizeY = engine->canvas->SizeY();
	float fullClipX = engine->canvas->ClipX();
	float fullClipY = engine->canvas->ClipY();

	for (int eye = 0; eye < 2; eye++)
	{
		SetVRHudFrame(eye, fullFrame);

		// Restore this eye's world-pass scene node so Canvas.DrawActor()
		// (e.g. the weapon viewmodel) picks up the matching camera pose
		// instead of whichever eye MainFrame.Frame was last left at.
		MainFrame.Frame = VREyeFrame[eye];
		Device->SetSceneNode(&Canvas.Frame);
		if (engine->viewport->Actor())
		{
			if (engine->LaunchInfo.ue1Version > 219)
			{
				CallEvent(engine->viewport->Actor(), EventName::RenderOverlays, { ExpressionValue::ObjectValue(engine->canvas) });
			}
			else
			{
				UWeapon* weapon = engine->viewport->Actor()->Weapon();
				if (weapon)
				{
					CallEvent(weapon, "InvCalcView", {});
					DrawActor(weapon, false, false);
				}
			}
		}

		// M-B: off-hand placeholder marker, drawn once per eye (this eye's
		// MainFrame.Frame == VREyeFrame[eye], just restored above) so it
		// appears correctly positioned in both stereo halves - see
		// DrawVROffHandMarker()'s doc comment.
		DrawVROffHandMarker();

		// 2026-07-21: main-hand marker, only while grip-capture is parking
		// the weapon away from the hand - see DrawVRMainHandMarker()'s doc
		// comment. Same per-eye placement reasoning as the off-hand marker
		// above.
		if (engine->vrGripCalibrateActive)
			DrawVRMainHandMarker();
	}

	// Restore full-window canvas state for PostRender() and the next frame's ResetCanvas().
	Canvas.Frame = fullFrame;
	engine->canvas->CurX() = 0.0f;
	engine->canvas->CurY() = 0.0f;
	engine->canvas->ClipX() = fullClipX;
	engine->canvas->ClipY() = fullClipY;
	engine->canvas->SizeX() = fullSizeX;
	engine->canvas->SizeY() = fullSizeY;
	Device->SetSceneNode(&Canvas.Frame);
}

void RenderSubsystem::PostRender()
{
	Device->SetSceneNode(&Canvas.Frame);
	if (engine->viewport->Actor())
		CallEvent(engine->viewport->Actor(), EventName::PostRender, { ExpressionValue::ObjectValue(engine->canvas) });
	CallEvent(engine->console, EventName::PostRender, { ExpressionValue::ObjectValue(engine->canvas) });
	DrawTimedemoStats();

	if (ShowCollisionDebug)
		DrawCollisionDebug();
}

void RenderSubsystem::PostRenderVR()
{
	// UT99's actual visible HUD (health/armor/ammo/message queue) is drawn
	// from PlayerPawn.PostRender, not RenderOverlays - same per-eye split
	// as RenderOverlaysVR(), for the same reason (Canvas.Frame otherwise
	// spans the full window, landing HUD elements on the seam between the
	// two eye halves).
	FSceneNode fullFrame = Canvas.Frame;
	int fullSizeX = engine->canvas->SizeX();
	int fullSizeY = engine->canvas->SizeY();
	float fullClipX = engine->canvas->ClipX();
	float fullClipY = engine->canvas->ClipY();

	UPlayerPawn* playerPawn = engine->viewport->Actor();

	for (int eye = 0; eye < 2; eye++)
	{
		// 2026-07-22 (VR_SCREEN_QUAD_PLAN_2026-07-22.md Phase 2): the menu
		// is now rendered ONCE, monoscopically, by DrawMenuQuad() (called
		// from RenderSubsystem::DrawGame(), in its own Lock/Unlock cycle
		// after this function returns) and presented as a real
		// world-anchored OpenXR quad layer - not duplicated per-eye here via
		// SetVRHudFrame's sub-rect placement like every other VR HUD/HUD-ish
		// content on this codebase. See DrawMenuQuad()'s doc comment.
		SetVRHudFrame(eye, fullFrame);

		MainFrame.Frame = VREyeFrame[eye];
		Device->SetSceneNode(&Canvas.Frame);
		if (playerPawn)
			CallEvent(playerPawn, EventName::PostRender, { ExpressionValue::ObjectValue(engine->canvas) });
		CallEvent(engine->console, EventName::PostRender, { ExpressionValue::ObjectValue(engine->canvas) });
	}

	Canvas.Frame = fullFrame;
	engine->canvas->CurX() = 0.0f;
	engine->canvas->CurY() = 0.0f;
	engine->canvas->ClipX() = fullClipX;
	engine->canvas->ClipY() = fullClipY;
	engine->canvas->SizeX() = fullSizeX;
	engine->canvas->SizeY() = fullSizeY;
	Device->SetSceneNode(&Canvas.Frame);

	DrawTimedemoStats();
	if (ShowCollisionDebug)
		DrawCollisionDebug();
}

// 2026-07-22 (VR_SCREEN_QUAD_PLAN_2026-07-22.md Phase 2): renders the
// pause/escape menu ONCE per frame, monoscopically, into the standalone
// offscreen quad render target, for presentation as a real world-anchored
// OpenXR quad layer instead of the old PostRenderVR() showingMenu branch's
// per-eye HUD sub-rect (see that function's doc comment on why its per-eye
// loop now skips this case entirely). Called from
// RenderSubsystem::DrawGame() in its own Lock/Unlock cycle, after
// PostRenderVR() has already run this same frame.
//
// UT99's actual front-end/pause menu draws from UPlayerPawn.PostRender -
// UT99's own UWindow-based UnrealScript system, engine->dxRootWindow's
// same-named-but-unrelated native sibling is never populated for a UT99
// pawn (see Engine::UpdateVRMenuCursor()'s doc comment for the fuller
// explanation) - so a single CallEvent(playerPawn, PostRender, ...) call
// against the quad's own canvas frame is what actually produces the menu's
// pixels here, replacing the old per-eye-duplicated pair.
void RenderSubsystem::DrawMenuQuad()
{
	UPlayerPawn* playerPawn = engine->viewport->Actor();

	int quadWidth = Device->GetQuadWidth();
	int quadHeight = Device->GetQuadHeight();

	Device->LockQuadTarget(vec4(0.0f, 0.0f, 0.0f, 1.0f));

	FSceneNode fullFrame = Canvas.Frame;
	int fullSizeX = engine->canvas->SizeX();
	int fullSizeY = engine->canvas->SizeY();
	float fullClipX = engine->canvas->ClipX();
	float fullClipY = engine->canvas->ClipY();
	int fullViewportX = engine->viewport->ViewportX();
	int fullViewportY = engine->viewport->ViewportY();
	int fullViewportW = engine->viewport->ViewportWidth();
	int fullViewportH = engine->viewport->ViewportHeight();

	Canvas.Frame.XB = 0;
	Canvas.Frame.YB = 0;
	Canvas.Frame.X = quadWidth;
	Canvas.Frame.Y = quadHeight;
	Canvas.Frame.FX = (float)quadWidth;
	Canvas.Frame.FY = (float)quadHeight;
	Canvas.Frame.FX2 = Canvas.Frame.FX * 0.5f;
	Canvas.Frame.FY2 = Canvas.Frame.FY * 0.5f;

	int sizeX = (int)(quadWidth / (float)Canvas.uiscale);
	int sizeY = (int)(quadHeight / (float)Canvas.uiscale);
	engine->canvas->CurX() = 0.0f;
	engine->canvas->CurY() = 0.0f;
	engine->canvas->ClipX() = (float)sizeX;
	engine->canvas->ClipY() = (float)sizeY;
	engine->canvas->SizeX() = sizeX;
	engine->canvas->SizeY() = sizeY;

	// UT's UnrealScript UWindow lays itself out from Canvas.ClipX/ClipY and
	// Root.GUIScale. The engine canvas later multiplies draw coordinates by
	// Canvas.uiscale, so UpdateVRMenuCursor supplies WindowsMouseX/Y in this
	// canvas's logical space (raw quad pixels / Canvas.uiscale). The viewport
	// rect still points at the raw target for native viewport-dependent work.
	engine->viewport->SetViewportRect(0, 0, quadWidth, quadHeight);

	Device->SetSceneNode(&Canvas.Frame);
	if (playerPawn)
		CallEvent(playerPawn, EventName::PostRender, { ExpressionValue::ObjectValue(engine->canvas) });
	CallEvent(engine->console, EventName::PostRender, { ExpressionValue::ObjectValue(engine->canvas) });
	engine->DispatchVRMenuClickAfterCursorUpdate();
	DrawVRMenuPointerOverlay();

	engine->viewport->SetViewportRect(fullViewportX, fullViewportY, fullViewportW, fullViewportH);

	Canvas.Frame = fullFrame;
	engine->canvas->CurX() = 0.0f;
	engine->canvas->CurY() = 0.0f;
	engine->canvas->ClipX() = fullClipX;
	engine->canvas->ClipY() = fullClipY;
	engine->canvas->SizeX() = fullSizeX;
	engine->canvas->SizeY() = fullSizeY;
	Device->SetSceneNode(&Canvas.Frame);

	Device->UnlockQuadTarget();
}

// The cursor belongs to the menu surface, so draw it directly into the quad
// after UWindow. Tracked controllers and the ray are NOT drawn here: they are
// genuine stereo/world-space geometry from DrawVRMenuTrackedOverlay(), in a
// transparent projection layer composited over this quad.
void RenderSubsystem::DrawVRMenuPointerOverlay()
{
	auto point = [&](vec4 color, float x1, float y1, float x2, float y2)
	{
		Device->Draw2DPoint(&Canvas.Frame, color, 0, x1, y1, x2, y2, 1.0f);
	};

	if (engine->vrMenuCursorVisible)
	{
		// Derive the marker from the exact WindowsMouse values UWindow consumes,
		// transformed back through Canvas.uiscale into raw quad pixels. This
		// makes the dot ground truth for the actual hit-test cursor rather than
		// a separately cached visualization of the plane intersection.
		float x = engine->viewport->WindowsMouseX() * (float)Canvas.uiscale;
		float y = engine->viewport->WindowsMouseY() * (float)Canvas.uiscale;
		vec4 cursorColor = engine->vrMenuCursorFromController ? vec4(0.15f, 1.0f, 0.4f, 1.0f) : vec4(1.0f, 1.0f, 1.0f, 1.0f);
		// A compact dot is enough once the true 3D beam visibly terminates at
		// this exact point; the old large cross looked like another floating
		// controller sprite and obscured menu labels.
		point(vec4(0.0f, 0.0f, 0.0f, 1.0f), x - 7.0f, y - 7.0f, x + 7.0f, y + 7.0f);
		point(cursorColor, x - 4.0f, y - 4.0f, x + 4.0f, y + 4.0f);
	}
}

void RenderSubsystem::PostRenderFlash()
{
	Device->SetSceneNode(&Canvas.Frame);
	if (engine->viewport->Actor())
		CallEvent(engine->viewport->Actor(), "PostRenderFlash", {ExpressionValue::ObjectValue(engine->canvas)});
}

void RenderSubsystem::DrawActor(UActor* actor, bool WireFrame, bool ClearZ)
{
	Device->SetSceneNode(&MainFrame.Frame);
	if (ClearZ)
		Device->ClearZ();

	actor->bHidden() = false;
	VisibleMesh vismesh;
	if (vismesh.DrawMesh(&MainFrame, actor, WireFrame, false))
		vismesh.DrawMesh(&MainFrame, actor, WireFrame, true);
	actor->bHidden() = true;

	Device->SetSceneNode(&Canvas.Frame);
}

void RenderSubsystem::DrawClippedActor(UActor* actor, bool WireFrame, int X, int Y, int XB, int YB, bool ClearZ)
{
	FSceneNode frame;
	frame.XB = XB * Canvas.uiscale;
	frame.YB = YB * Canvas.uiscale;
	frame.X = X * Canvas.uiscale;
	frame.Y = Y * Canvas.uiscale;
	frame.FX = (float)X * Canvas.uiscale;
	frame.FY = (float)Y * Canvas.uiscale;
	frame.FX2 = frame.FX * 0.5f;
	frame.FY2 = frame.FY * 0.5f;
	frame.ObjectToWorld = Coords::ViewToRenderDev().ToMatrix();
	frame.WorldToView = mat4::identity();
	frame.FovAngle = engine->CameraFovAngle;
	float Aspect = frame.FY / frame.FX;
	float RProjZ = (float)std::tan(radians(frame.FovAngle) * 0.5f);
	float RFX2 = 2.0f * RProjZ / frame.FX;
	float RFY2 = 2.0f * RProjZ * Aspect / frame.FY;
	frame.Projection = mat4::frustum(-RProjZ, RProjZ, -Aspect * RProjZ, Aspect * RProjZ, 1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);
	Device->SetSceneNode(&frame);

	if (ClearZ)
		Device->ClearZ();

	actor->bHidden() = false;
	VisibleMesh vismesh;
	if (vismesh.DrawMesh(&MainFrame, actor, WireFrame, false))
		vismesh.DrawMesh(&MainFrame, actor, WireFrame, true);
	actor->bHidden() = true;

	Device->SetSceneNode(&Canvas.Frame);
}

void RenderSubsystem::DrawTile(UTexture* Tex, float x, float y, float XL, float YL, float U, float V, float UL, float VL, float Z, vec4 color, vec4 fog, uint32_t flags)
{
	if (!Tex)
		return;
	UpdateTexture(Tex);
	Tex = Tex->GetAnimTexture();
	UpdateTexture(Tex);

	FTextureInfo texinfo;
	texinfo.CacheID = (uint64_t)(ptrdiff_t)Tex;
	texinfo.Texture = Tex;
	texinfo.Format = texinfo.Texture->UsedFormat;
	texinfo.Mips = Tex->UsedMipmaps.data();
	texinfo.NumMips = (int)Tex->UsedMipmaps.size();
	texinfo.USize = Tex->USize();
	texinfo.VSize = Tex->VSize();
	if (Tex->Palette())
		texinfo.Palette = (FColor*)Tex->Palette()->Colors.data();

	if (Tex->bMasked())
		flags |= PF_Masked;

	Device->DrawTile(&Canvas.Frame, texinfo, x * Canvas.uiscale, y * Canvas.uiscale, XL * Canvas.uiscale, YL * Canvas.uiscale, U, V, UL, VL, Z, color, fog, flags);
}

void RenderSubsystem::DrawTileClipped(UTexture* Tex, float orgX, float orgY, float curX, float curY, float XL, float YL, float U, float V, float UL, float VL, float Z, vec4 color, vec4 fog, uint32_t flags, float clipX, float clipY)
{
	if (!Tex)
		return;
	UpdateTexture(Tex);
	Tex = Tex->GetAnimTexture();
	UpdateTexture(Tex);

	FTextureInfo texinfo;
	texinfo.CacheID = (uint64_t)(ptrdiff_t)Tex;
	texinfo.Texture = Tex;
	texinfo.Format = texinfo.Texture->UsedFormat;
	texinfo.Mips = Tex->UsedMipmaps.data();
	texinfo.NumMips = (int)Tex->UsedMipmaps.size();
	texinfo.USize = Tex->USize();
	texinfo.VSize = Tex->VSize();
	if (Tex->Palette())
		texinfo.Palette = (FColor*)Tex->Palette()->Colors.data();

	if (Tex->bMasked())
		flags |= PF_Masked;

	Rectf clipBox = Rectf::xywh(orgX, orgY, clipX, clipY);
	Rectf dest = Rectf::xywh(orgX + curX, orgY + curY, XL, YL);
	Rectf src = Rectf::xywh(U, V, UL, VL);
	DrawTile(texinfo, dest, src, clipBox, Z, color, fog, flags);
}

Array<std::string> RenderSubsystem::FindTextBlocks(const std::string& text)
{
	// Split text into words, whitespace or newline
	Array<std::string> textBlocks;
	size_t pos = 0;
	while (pos < text.size())
	{
		if (text[pos] == '\n')
		{
			textBlocks.push_back("\n");
			pos++;
		}
		else if (text[pos] == ' ')
		{
			size_t end = std::min(text.find_first_not_of(' ', pos + 1), text.size());
			textBlocks.push_back(text.substr(pos, end - pos));
			pos = end;
		}
		else
		{
			size_t end = std::min(text.find_first_of(" \n", pos + 1), text.size());
			textBlocks.push_back(text.substr(pos, end - pos));
			pos = end;
		}
	}
	return textBlocks;
}

void RenderSubsystem::DrawTextBlockRange(float x, float y, const Array<std::string>& textBlocks, size_t start, size_t end, UFont* font, vec4 color, uint32_t polyflags, float spaceX)
{
	for (size_t i = start; i < end; i++)
	{
		for (char c : textBlocks[i])
		{
			FontGlyph glyph = font->GetGlyph(c);

			if (!glyph.Texture)
				continue;

			FTextureInfo texinfo;
			texinfo.CacheID = (uint64_t)(ptrdiff_t)glyph.Texture;
			texinfo.Texture = glyph.Texture;
			texinfo.Format = texinfo.Texture->UsedFormat;
			texinfo.Mips = glyph.Texture->UsedMipmaps.data();
			texinfo.NumMips = (int)glyph.Texture->UsedMipmaps.size();
			texinfo.USize = glyph.Texture->USize();
			texinfo.VSize = glyph.Texture->VSize();
			if (glyph.Texture->Palette())
				texinfo.Palette = (FColor*)glyph.Texture->Palette()->Colors.data();

			int width = glyph.USize;
			int height = glyph.VSize;
			float StartU = (float)glyph.StartU;
			float StartV = (float)glyph.StartV;
			float USize = (float)glyph.USize;
			float VSize = (float)glyph.VSize;

			Device->DrawTile(&Canvas.Frame, texinfo, x * Canvas.uiscale, y * Canvas.uiscale, (float)width * Canvas.uiscale, (float)height * Canvas.uiscale, StartU, StartV, USize, VSize, 1.0f, color, vec4(0.0f), polyflags);

			x += width + spaceX;
		}
	}
}

void RenderSubsystem::DrawText(UFont* font, vec4 color, float orgX, float orgY, float& curX, float& curY, float& curXL, float& curYL, bool newlineAtEnd, const std::string& text, uint32_t polyflags, bool center, float spaceX, float spaceY, float clipX, float clipY, bool noDraw)
{
	float totalWidth = 0.0f;
	float totalHeight = 0.0f;

	Array<std::string> textBlocks = FindTextBlocks(text);
	size_t lineBegin = 0;
	float lineWidth = 0.0f;
	float lineHeight = 0.0f;
	for (size_t pos = 0; pos < textBlocks.size(); pos++)
	{
		if (textBlocks[pos].front() == '\n')
		{
			if (pos != lineBegin)
			{
				float centerX = 0;
				if (center)
					centerX = std::round((clipX - lineWidth) * 0.5f);
				if (!noDraw)
					DrawTextBlockRange(orgX + curX + centerX, orgY + curY, textBlocks, lineBegin, pos, font, color, polyflags, spaceX);
				curY += lineHeight;
				totalHeight += lineHeight;
				totalWidth = std::max(totalWidth, lineWidth);
			}

			curX = 0;
			lineBegin = pos + 1;
			lineWidth = 0.0f;
			lineHeight = 0.0f;
		}
		else
		{
			vec2 blockSize = GetTextSize(font, textBlocks[pos], spaceX, spaceY);
			if (lineWidth + blockSize.x > clipX)
			{
				float centerX = 0;
				if (center)
					centerX = std::round((clipX - lineWidth) * 0.5f);
				if (!noDraw)
					DrawTextBlockRange(orgX + curX + centerX, orgY + curY, textBlocks, lineBegin, pos, font, color, polyflags, spaceX);

				curX = 0;
				curY += lineHeight;
				totalHeight += lineHeight;
				totalWidth = std::max(totalWidth, lineWidth);

				if (textBlocks[pos].front() == ' ')
				{
					// Ignore whitespace at the beginning of a word wrapped line
					lineBegin = pos + 1;
					lineWidth = 0.0f;
					lineHeight = 0.0f;
				}
				else
				{
					lineBegin = pos;
					lineWidth = blockSize.x;
					lineHeight = blockSize.y;
				}
			}
			else
			{
				lineWidth += blockSize.x;
				lineHeight = std::max(lineHeight, blockSize.y);
			}
		}
	}

	if (lineBegin < textBlocks.size())
	{
		float centerX = 0;
		if (center)
			centerX = std::round((clipX - lineWidth) * 0.5f);
		if (!noDraw)
			DrawTextBlockRange(orgX + curX + centerX, orgY + curY, textBlocks, lineBegin, textBlocks.size(), font, color, polyflags, spaceX);
		curX += centerX + lineWidth;
		curY += lineHeight;
		totalHeight += lineHeight;
		totalWidth = std::max(totalWidth, lineWidth);
	}

	curXL = std::max(curXL, totalWidth);
	curYL = std::max(curYL, totalHeight);

	if (newlineAtEnd)
	{
		curX = 0;
		curY += curYL;
		curXL = 0;
		curYL = 0;
	}
}

void RenderSubsystem::DrawTextClipped(UFont* font, vec4 color, float orgX, float orgY, float curX, float curY, const std::string& text, uint32_t polyflags, bool checkHotKey, float clipX, float clipY, bool center)
{
	FontGlyph uglyph = font->GetGlyph('_');
	int uwidth = uglyph.USize;
	int uheight = uglyph.VSize;
	float uStartU = (float)uglyph.StartU;
	float uStartV = (float)uglyph.StartV;
	float uUSize = (float)uglyph.USize;
	float uVSize = (float)uglyph.VSize;

	Rectf clipBox = Rectf::xywh(orgX, orgY, clipX, clipY);

	float centerX = 0;
	if (center)
		centerX = std::round((clipX - GetTextSize(font, text).x) * 0.5f);

	bool foundAmpersand = false;
	int maxY = 0;
	for (char c : text)
	{
		if (checkHotKey && c == '&' && !foundAmpersand)
		{
			foundAmpersand = true;
		}
		else if (foundAmpersand && c != '&')
		{
			foundAmpersand = false;

			FontGlyph glyph = font->GetGlyph(c);
			if (curX + glyph.USize > (int)clipX)
				break;

			FTextureInfo texinfo;
			texinfo.CacheID = (uint64_t)(ptrdiff_t)glyph.Texture;
			texinfo.Texture = glyph.Texture;
			texinfo.Format = texinfo.Texture->UsedFormat;
			texinfo.Mips = glyph.Texture->UsedMipmaps.data();
			texinfo.NumMips = (int)glyph.Texture->UsedMipmaps.size();
			texinfo.USize = glyph.Texture->USize();
			texinfo.VSize = glyph.Texture->VSize();
			if (glyph.Texture->Palette())
				texinfo.Palette = (FColor*)glyph.Texture->Palette()->Colors.data();

			Rectf dest = Rectf::xywh(orgX + curX + centerX, orgY + curY, (float)glyph.USize, (float)glyph.VSize);
			Rectf src = Rectf::xywh((float)glyph.StartU, (float)glyph.StartV, (float)glyph.USize, (float)glyph.VSize);
			DrawTile(texinfo, dest, src, clipBox, 1.0f, color, vec4(0.0f), polyflags);

			texinfo.CacheID = (uint64_t)(ptrdiff_t)uglyph.Texture;
			texinfo.Texture = uglyph.Texture;

			dest = Rectf::xywh(orgX + curX + (glyph.USize - uwidth) / 2, orgY + curY, (float)uwidth, (float)uheight);
			src = Rectf::xywh(uStartU, uStartV, uUSize, uVSize);
			DrawTile(texinfo, dest, src, clipBox, 1.0f, color, vec4(0.0f), polyflags);

			curX += glyph.USize;
			maxY = std::max(maxY, glyph.VSize);
		}
		else
		{
			foundAmpersand = false;

			FontGlyph glyph = font->GetGlyph(c);
			if (curX + glyph.USize > (int)clipX)
				break;

			FTextureInfo texinfo;
			texinfo.CacheID = (uint64_t)(ptrdiff_t)glyph.Texture;
			texinfo.Texture = glyph.Texture;
			texinfo.Format = texinfo.Texture->UsedFormat;
			texinfo.Mips = glyph.Texture->UsedMipmaps.data();
			texinfo.NumMips = (int)glyph.Texture->UsedMipmaps.size();
			texinfo.USize = glyph.Texture->USize();
			texinfo.VSize = glyph.Texture->VSize();
			if (glyph.Texture->Palette())
				texinfo.Palette = (FColor*)glyph.Texture->Palette()->Colors.data();

			Rectf dest = Rectf::xywh(orgX + curX + centerX, orgY + curY, (float)glyph.USize, (float)glyph.VSize);
			Rectf src = Rectf::xywh((float)glyph.StartU, (float)glyph.StartV, (float)glyph.USize, (float)glyph.VSize);
			DrawTile(texinfo, dest, src, clipBox, 1.0f, color, vec4(0.0f), PF_Highlighted | PF_NoSmooth | PF_Masked);

			curX += glyph.USize;
			maxY = std::max(maxY, glyph.VSize);
		}
	}
}

void RenderSubsystem::DrawTile(FTextureInfo& texinfo, const Rectf& dest, const Rectf& src, const Rectf& clipBox, float Z, vec4 color, vec4 fog, uint32_t flags)
{
	if (dest.left > dest.right || dest.top > dest.bottom)
		return;

	if (dest.left >= clipBox.left && dest.top >= clipBox.top && dest.right <= clipBox.right && dest.bottom <= clipBox.bottom)
	{
		Device->DrawTile(&Canvas.Frame, texinfo, dest.left * Canvas.uiscale, dest.top * Canvas.uiscale, (dest.right - dest.left) * Canvas.uiscale, (dest.bottom - dest.top) * Canvas.uiscale, src.left, src.top, src.right - src.left, src.bottom - src.top, Z, color, fog, flags);
	}
	else
	{
		Rectf d = dest;
		Rectf s = src;

		float scaleX = (s.right - s.left) / (d.right - d.left);
		float scaleY = (s.bottom - s.top) / (d.bottom - d.top);

		if (d.left < clipBox.left)
		{
			s.left += scaleX * (clipBox.left - d.left);
			d.left = clipBox.left;
		}
		if (d.right > clipBox.right)
		{
			s.right += scaleX * (clipBox.right - d.right);
			d.right = clipBox.right;
		}
		if (d.top < clipBox.top)
		{
			s.top += scaleY * (clipBox.top - d.top);
			d.top = clipBox.top;
		}
		if (d.bottom > clipBox.bottom)
		{
			s.bottom += scaleY * (clipBox.bottom - d.bottom);
			d.bottom = clipBox.bottom;
		}

		if (d.left < d.right && d.top < d.bottom)
			Device->DrawTile(&Canvas.Frame, texinfo, d.left * Canvas.uiscale, d.top * Canvas.uiscale, (d.right - d.left) * Canvas.uiscale, (d.bottom - d.top) * Canvas.uiscale, s.left, s.top, s.right - s.left, s.bottom - s.top, Z, color, fog, flags);
	}
}

void RenderSubsystem::Draw2DLine(vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2)
{
	auto uiscale = static_cast<float>(Canvas.uiscale);
	Device->Draw2DLine(&Canvas.Frame, Color, LineFlags, vec3(P1.xy() * uiscale, P1.z), vec3(P2.xy() * uiscale, P2.z));
}

void RenderSubsystem::Draw3DLine(vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2)
{
	Device->Draw3DLine(&Canvas.Frame, Color, LineFlags, P1, P2);
}

// M-B: off-hand placeholder - see the doc comment on the declaration in
// RenderSubsystem.h. Deliberately calls Device->Draw3DLine(&MainFrame.Frame,
// ...) directly rather than the public Draw3DLine() wrapper just above,
// which hardcodes &Canvas.Frame - fine outside the VR HUD split, but wrong
// here: RenderOverlaysVR() has already rewritten Canvas.Frame into a small
// 2D HUD sub-rect (SetVRHudFrame()) by the time this runs, while
// MainFrame.Frame still holds the correct per-eye 3D camera frame
// (VREyeFrame[eye], restored right before the weapon draw above) - the same
// frame DrawActor()/DrawCollisionDebug's nav-path lines use for exactly this
// reason.
void RenderSubsystem::DrawVROffHandMarker()
{
	const Engine::VRHandState& offHand = engine->OffHand();
	if (!offHand.valid)
		return;

	const float armUU = 3.0f; // ~3in cross - small, but visible against typical viewmodel scale
	const vec4 color(1.0f, 1.0f, 0.0f, 1.0f); // yellow - distinct from the weapon and world geometry
	const vec3 center = offHand.gripPos;

	Device->Draw3DLine(&MainFrame.Frame, color, 0, center - offHand.gripCoords.XAxis * armUU, center + offHand.gripCoords.XAxis * armUU);
	Device->Draw3DLine(&MainFrame.Frame, color, 0, center - offHand.gripCoords.YAxis * armUU, center + offHand.gripCoords.YAxis * armUU);
	Device->Draw3DLine(&MainFrame.Frame, color, 0, center - offHand.gripCoords.ZAxis * armUU, center + offHand.gripCoords.ZAxis * armUU);
}

// 2026-07-21: main-hand marker for the grip-calibration flow - see the
// doc comment on the declaration in RenderSubsystem.h. Same
// Device->Draw3DLine(&MainFrame.Frame, ...) direct-call pattern as
// DrawVROffHandMarker() and for the same reason (Canvas.Frame is a 2D HUD
// sub-rect by the time this runs inside RenderOverlaysVR()'s per-eye loop).
//
// Asymmetric on purpose: the forward arm (gripCoords.XAxis, the direction
// the controller aims) is longer and green; the backward stub is short
// and red. A plain symmetric cross (fine for the off-hand, which has no
// "which way is it pointing" question relevant to grabbing it) doesn't
// tell you which end of the line is forward - real-headset feedback
// specifically asked for that distinction here.
void RenderSubsystem::DrawVRMainHandMarker()
{
	const Engine::VRHandState& mainHand = engine->MainHand();
	if (!mainHand.valid)
		return;

	const float forwardUU = 5.0f;
	const float backUU = 1.5f;
	const float armUU = 2.5f;
	const vec3 center = mainHand.gripPos;
	const vec4 forwardColor(0.0f, 1.0f, 0.0f, 1.0f); // green - the direction the controller aims
	const vec4 backColor(1.0f, 0.0f, 0.0f, 1.0f);    // red - the back of the controller, short so it can't be confused with forward
	const vec4 sideColor(0.3f, 0.7f, 1.0f, 1.0f);    // light blue - left/right and up/down, no directional meaning

	Device->Draw3DLine(&MainFrame.Frame, forwardColor, 0, center, center + mainHand.gripCoords.XAxis * forwardUU);
	Device->Draw3DLine(&MainFrame.Frame, backColor, 0, center, center - mainHand.gripCoords.XAxis * backUU);
	Device->Draw3DLine(&MainFrame.Frame, sideColor, 0, center - mainHand.gripCoords.YAxis * armUU, center + mainHand.gripCoords.YAxis * armUU);
	Device->Draw3DLine(&MainFrame.Frame, sideColor, 0, center - mainHand.gripCoords.ZAxis * armUU, center + mainHand.gripCoords.ZAxis * armUU);
}

void RenderSubsystem::DrawTile(FTextureInfo& Info, float X, float Y, float XL, float YL, float U, float V, float UL, float VL, float Z, vec4 Color, vec4 Fog, uint32_t PolyFlags)
{
	Device->DrawTile(&Canvas.Frame, Info, X, Y, XL, YL, U, V, UL, VL, Z, Color, Fog, PolyFlags);
}

vec2 RenderSubsystem::GetTextSize(UFont* font, const std::string& text, float spaceX, float spaceY)
{
	float x = 0.0f;
	float y = 0.0f;
	for (char c : text)
	{
		FontGlyph glyph = font->GetGlyph(c);
		x += (float)glyph.USize + spaceX;
		y = std::max(y, (float)glyph.VSize + spaceY);
	}
	return { x, y };
}

void RenderSubsystem::DrawTimedemoStats()
{
	Canvas.framesDrawn++;
	if (Canvas.startFPSTime == 0 || engine->lastTime - Canvas.startFPSTime >= 1'000'000)
	{
		Canvas.fps = Canvas.framesDrawn;
		Canvas.startFPSTime = engine->lastTime;
		Canvas.framesDrawn = 0;
	}

	if (ShowTimedemoStats)
	{
		Array<std::string> lines;
		lines.push_back(std::to_string(Canvas.fps) + " FPS");
		lines.push_back(std::to_string(engine->Level->Actors.size()) + " actors");
		lines.push_back(std::to_string(GC::GetStats().numObjects) + " GC objects");
		lines.push_back(std::to_string(GC::GetStats().memoryUsage / (1024 * 1024)) + " mb memory used");
		lines.push_back(std::to_string(Stats.Frames) + " visible frames");
		lines.push_back(std::to_string(Stats.Surfaces) + " visible surfaces");
		lines.push_back(std::to_string(Stats.Actors) + " visible actors");

		UFont* font = engine->canvas->SmallFont();
		if (font)
		{
			float curY = 180;
			for (const std::string& text : lines)
			{
				float curX = engine->viewport->ViewportWidth() / (float)Canvas.uiscale - GetTextSize(font, text).x - 16;
				float curXL = 0.0f;
				float curYL = 0.0f;
				DrawText(font, vec4(1.0f), 0.0f, 0.0f, curX, curY, curXL, curYL, false, text, PF_NoSmooth | PF_Masked, false);
				curY += curYL;
			}

			/*
			Array<std::string> leftlines;
			engine->audiodev->AddStats(leftlines);
			curY = 64;
			for (const std::string& text : leftlines)
			{
				float curX = 16.0f;
				float curXL = 0.0f;
				float curYL = 0.0f;
				DrawText(font, vec4(1.0f), 0.0f, 0.0f, curX, curY, curXL, curYL, false, text, PF_NoSmooth | PF_Masked, false);
				curY += curYL;
			}
			*/
		}
	}

	if (ShowRenderStats)
	{
		Array<std::string> lines;
		lines.push_back(std::to_string(Canvas.fps) + " FPS");
		lines.push_back(std::to_string(engine->Level->Actors.size()) + " actors");
		lines.push_back(std::to_string(GC::GetStats().numObjects) + " GC objects");
		lines.push_back(std::to_string(GC::GetStats().memoryUsage / (1024 * 1024)) + " mb memory used");

		/*size_t numCollisionActors = 0;
		for (auto& it : engine->Level->Hash.CollisionActors)
			numCollisionActors += it.second.size();
		lines.push_back(std::to_string(numCollisionActors) + " collision actors");*/

		/*lines.push_back(std::to_string(Scene.OpaqueNodes.size() + Scene.TranslucentNodes.size()) + " visible surfaces");
		lines.push_back(std::to_string(Scene.Actors.size()) + " visible actors");
		lines.push_back(std::to_string(Scene.Coronas.size()) + " visible coronas");

		lines.push_back(std::to_string(Scene.Clipper.numDrawSpans) + " spans");
		lines.push_back(std::to_string(Scene.Clipper.numSurfs) + " checked surfaces");
		lines.push_back(std::to_string(Scene.Clipper.numTris) + " checked triangles");*/

		UFont* font = engine->canvas->MedFont();
		if (font)
		{
			float curY = 180;
			for (const std::string& text : lines)
			{
				float curX = engine->viewport->ViewportWidth() / (float)Canvas.uiscale - GetTextSize(font, text).x - 16;
				float curXL = 0.0f;
				float curYL = 0.0f;
				DrawText(font, vec4(1.0f), 0.0f, 0.0f, curX, curY, curXL, curYL, false, text, PF_NoSmooth | PF_Masked, false);
				curY += curYL;
			}
		}
	}
}

void RenderSubsystem::DrawCollisionDebug()
{
	Array<std::string> lines;
	if (engine->PlayerBspNode)
	{
		BspNode* node = engine->PlayerBspNode;
		vec3& normal = engine->PlayerHitNormal;
		vec3& location = engine->PlayerHitLocation;
		BspSurface* surf = (node->Surf >= 0) ? &engine->Level->Model->Surfaces[node->Surf] : nullptr;

		lines.push_back("BspNode CollisionBound: " + std::to_string(node->CollisionBound));
		lines.push_back("BspNode Surface: " + std::to_string(node->Surf));

		if (surf && surf->Material)
			lines.push_back("BspNode Texture: " + surf->Material->Name.ToString());

		lines.push_back("BspNode Plane: (" +
			std::to_string(node->PlaneX) + ", " +
			std::to_string(node->PlaneY) + ", " +
			std::to_string(node->PlaneZ) + ", " +
			std::to_string(node->PlaneW) + ")"
		);

		BBox box = node->GetCollisionBox(engine->Level->Model);
		lines.push_back("BspNode Bound Min: (" +
			std::to_string(box.min.x) + ", " +
			std::to_string(box.min.y) + ", " +
			std::to_string(box.min.z) + ")"
		);

		lines.push_back("BspNode Bound Max: (" +
			std::to_string(box.max.x) + ", " +
			std::to_string(box.max.y) + ", " +
			std::to_string(box.max.z) + ")"
		);

		lines.push_back("HitNormal: (" +
			std::to_string(normal.x) + ", " +
			std::to_string(normal.y) + ", " +
			std::to_string(normal.z) + ")"
		);

		lines.push_back("HitLocation: (" +
			std::to_string(location.x) + ", " +
			std::to_string(location.y) + ", " +
			std::to_string(location.z) + ")"
		);
	}

	UFont* font = engine->canvas->MedFont();
	if (font)
	{
		float curY = 180;
		for (const std::string& text : lines)
		{
			float curX = engine->viewport->ViewportWidth() / (float)Canvas.uiscale - GetTextSize(font, text).x - 16;
			float curXL = 0.0f;
			float curYL = 0.0f;
			DrawText(font, vec4(1.0f), 0.0f, 0.0f, curX, curY, curXL, curYL, false, text, PF_NoSmooth | PF_Masked, false);
			curY += curYL;
		}
	}
}
