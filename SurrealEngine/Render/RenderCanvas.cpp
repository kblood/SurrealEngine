
#include "Precomp.h"
#include "RenderSubsystem.h"
#include "VisibleMesh.h"
#include "RenderDevice/RenderDevice.h"
#include "UObject/USubsystem.h"
#include "GameWindow.h"
#include "VM/ScriptCall.h"
#include "Engine.h"

#include <limits>

void RenderSubsystem::ResetCanvas()
{
	// Scale the UI so it matches what you saw on a 1024x768 CRT monitor for Unreal and other older games.
	// Assume 1280x960 for UT and newer.
	int vertResolution = engine->LaunchInfo.ue1Version < 400 ? 768 : 960;
	Canvas.uiscale = std::max((engine->viewport->ViewportHeight() + vertResolution / 2) / vertResolution, 1);

	FSceneNode frame;
	Canvas.Frame.XB = 0;
	Canvas.Frame.YB = 0;
	Canvas.Frame.X = engine->viewport->ViewportWidth();
	Canvas.Frame.Y = engine->viewport->ViewportHeight();
	Canvas.Frame.FX = (float)engine->viewport->ViewportWidth();
	Canvas.Frame.FY = (float)engine->viewport->ViewportHeight();
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

	int sizeX = (int)(engine->viewport->ViewportWidth() / (float)Canvas.uiscale);
	int sizeY = (int)(engine->viewport->ViewportHeight() / (float)Canvas.uiscale);
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

	if (engine->LaunchInfo.IsHarryPotter1())
	{
		engine->canvas->ClipX() = (float)sizeY * (4.0f / 3.0f);
		engine->canvas->SizeX() = (int)std::round(sizeY * (4.0f / 3.0f));
	}

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

void RenderSubsystem::PostRenderPerViewHud(const ViewFamily& viewFamily)
{
	const FSceneNode savedFrame = Canvas.Frame;
	const float savedCurX = engine->canvas->CurX();
	const float savedCurY = engine->canvas->CurY();
	const float savedClipX = engine->canvas->ClipX();
	const float savedClipY = engine->canvas->ClipY();
	const int savedSizeX = engine->canvas->SizeX();
	const int savedSizeY = engine->canvas->SizeY();
	const PresentationTarget target = viewFamily.Presentation.GetLayer(
		PresentationLayer::UserInterface).Target;

	UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
	UHUD* hud = player ? player->myHUD() : nullptr;
	const int savedCrosshair = hud ? hud->Crosshair() : 0;
	if (hud)
	{
		// UT's ChallengeHUD returns before loading/drawing a crosshair when the
		// selected index is >= CrosshairCount. Scope the override to the two VR
		// PostRender calls so the user's persisted desktop setting is untouched.
		hud->Crosshair() = std::numeric_limits<int>::max();
	}

	for (size_t viewIndex = 0; viewIndex < viewFamily.Views.size(); viewIndex++)
	{
		const std::optional<ViewRect> hudRect = CreatePerViewHudRect(viewFamily,
			viewIndex);
		if (!hudRect || !Device->BeginPresentationView(target, viewIndex))
			continue;

		Canvas.Frame = savedFrame;
		Canvas.Frame.XB = hudRect->X;
		Canvas.Frame.YB = hudRect->Y;
		Canvas.Frame.X = hudRect->Width;
		Canvas.Frame.Y = hudRect->Height;
		Canvas.Frame.FX = static_cast<float>(hudRect->Width);
		Canvas.Frame.FY = static_cast<float>(hudRect->Height);
		Canvas.Frame.FX2 = Canvas.Frame.FX * 0.5f;
		Canvas.Frame.FY2 = Canvas.Frame.FY * 0.5f;

		const int canvasWidth = std::max(static_cast<int>(hudRect->Width /
			static_cast<float>(Canvas.uiscale)), 1);
		const int canvasHeight = std::max(static_cast<int>(hudRect->Height /
			static_cast<float>(Canvas.uiscale)), 1);
		engine->canvas->CurX() = 0.0f;
		engine->canvas->CurY() = 0.0f;
		engine->canvas->ClipX() = static_cast<float>(canvasWidth);
		engine->canvas->ClipY() = static_cast<float>(canvasHeight);
		engine->canvas->SizeX() = canvasWidth;
		engine->canvas->SizeY() = canvasHeight;
		Device->SetSceneNode(&Canvas.Frame);
		if (player)
			CallEvent(player, EventName::PostRender,
				{ ExpressionValue::ObjectValue(engine->canvas) });
		CallEvent(engine->console, EventName::PostRender,
			{ ExpressionValue::ObjectValue(engine->canvas) });
		Device->EndPresentationView(target, viewIndex);
	}

	if (hud)
		hud->Crosshair() = savedCrosshair;
	Canvas.Frame = savedFrame;
	engine->canvas->CurX() = savedCurX;
	engine->canvas->CurY() = savedCurY;
	engine->canvas->ClipX() = savedClipX;
	engine->canvas->ClipY() = savedClipY;
	engine->canvas->SizeX() = savedSizeX;
	engine->canvas->SizeY() = savedSizeY;
	Device->SetSceneNode(&Canvas.Frame);

	DrawTimedemoStats();
	if (ShowCollisionDebug)
		DrawCollisionDebug();
}

void RenderSubsystem::PostRenderFlash()
{
	Device->SetSceneNode(&Canvas.Frame);
	if (engine->viewport->Actor())
		CallEvent(engine->viewport->Actor(), "PostRenderFlash", {ExpressionValue::ObjectValue(engine->canvas)});
}

void RenderSubsystem::DrawActor(UActor* actor, bool WireFrame, bool ClearZ)
{
	struct ScopedXRWeaponTransform
	{
		ScopedXRWeaponTransform(UActor* actor, bool apply,
			const XRWeaponPoseResult* pose)
			: Actor(actor), Applied(apply), SavedLocation(actor->Location()),
			SavedRotation(actor->Rotation()), SavedScale(actor->DrawScale())
		{
			if (!Applied || !pose)
			{
				Applied = false;
				return;
			}
			const XRWeaponActorTransform transform =
				BuildXRWeaponActorTransform(*pose);
			if (!transform.Valid)
			{
				Applied = false;
				return;
			}
			Actor->Location() = vec3(transform.Position.X,
				transform.Position.Y, transform.Position.Z);
			Actor->Rotation() = normalize(Rotator(
				transform.Pitch, transform.Yaw, transform.Roll));
			Actor->DrawScale() = transform.Scale;
		}
		~ScopedXRWeaponTransform()
		{
			if (!Applied)
				return;
			Actor->Location() = SavedLocation;
			Actor->Rotation() = SavedRotation;
			Actor->DrawScale() = SavedScale;
		}
		UActor* Actor;
		bool Applied;
		vec3 SavedLocation;
		Rotator SavedRotation;
		float SavedScale;
	};
	const XRWeaponPoseResult* pose = engine->GetXRWeaponPoseForActor(actor);
	ScopedXRWeaponTransform xrTransform(actor,
		XRWeaponOverlayActive && pose && pose->Valid, pose);
	if (xrTransform.Applied)
	{
		static uint64_t appliedTransformCount = 0;
		if ((appliedTransformCount++ % 180) == 0)
		{
			LogMessage("[openxr-weapon-render] actor=" +
				(actor->Class ? actor->Class->Name.ToString() : "none") +
				" applied_pos=(" + std::to_string(actor->Location().x) + "," +
				std::to_string(actor->Location().y) + "," +
				std::to_string(actor->Location().z) + ") applied_yaw=" +
				std::to_string(actor->Rotation().YawDegrees()) +
				" applied_pitch=" + std::to_string(actor->Rotation().PitchDegrees()) +
				" camera_yaw=" + std::to_string(engine->CameraRotation.YawDegrees()));
		}
	}

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

bool RenderSubsystem::RenderXRWeaponOverlay()
{
	UPlayerPawn* viewActor = engine->viewport->Actor();
	UWeapon* weapon = viewActor ? viewActor->Weapon() : nullptr;
	if (!weapon)
		return false;

	// This pass is deliberately weapon-only. Calling PlayerPawn.RenderOverlays
	// for every eye would repeat HUD and player-owned script side effects that
	// belong to the once-per-family XR UI capture.
	struct ScopedStateRestore
	{
		ScopedStateRestore(FSceneNode& frame, UCanvas* canvas, RenderDevice* device,
			UWeapon* weapon)
			: Frame(frame), CanvasObject(canvas), DeviceObject(device), WeaponObject(weapon),
			SavedFrame(frame), SavedSizeX(canvas->SizeX()), SavedSizeY(canvas->SizeY()),
			SavedClipX(canvas->ClipX()), SavedClipY(canvas->ClipY()),
			SavedCurX(canvas->CurX()), SavedCurY(canvas->CurY()),
			SavedWeaponLocation(weapon->Location()), SavedWeaponRotation(weapon->Rotation()),
			SavedWeaponScale(weapon->DrawScale())
		{
		}

		~ScopedStateRestore()
		{
			WeaponObject->Location() = SavedWeaponLocation;
			WeaponObject->Rotation() = SavedWeaponRotation;
			WeaponObject->DrawScale() = SavedWeaponScale;
			Frame = SavedFrame;
			CanvasObject->CurX() = SavedCurX;
			CanvasObject->CurY() = SavedCurY;
			CanvasObject->ClipX() = SavedClipX;
			CanvasObject->ClipY() = SavedClipY;
			CanvasObject->SizeX() = SavedSizeX;
			CanvasObject->SizeY() = SavedSizeY;
			DeviceObject->SetSceneNode(&Frame);
		}

		FSceneNode& Frame;
		UCanvas* CanvasObject;
		RenderDevice* DeviceObject;
		UWeapon* WeaponObject;
		FSceneNode SavedFrame;
		int SavedSizeX;
		int SavedSizeY;
		float SavedClipX;
		float SavedClipY;
		float SavedCurX;
		float SavedCurY;
		vec3 SavedWeaponLocation;
		Rotator SavedWeaponRotation;
		float SavedWeaponScale;
	} restore(Canvas.Frame, engine->canvas, Device, weapon);
	struct ScopedOverlayFlag
	{
		explicit ScopedOverlayFlag(bool& active) : Active(active), Saved(active)
		{
			Active = true;
		}
		~ScopedOverlayFlag() { Active = Saved; }
		bool& Active;
		bool Saved;
	} overlayFlag(XRWeaponOverlayActive);

	// Canvas.DrawActor consumes MainFrame.Frame. Match the 2D canvas state to
	// that same eye so weapon-specific tiles cannot spill into the other eye.
	Canvas.Frame.XB = MainFrame.Frame.XB;
	Canvas.Frame.YB = MainFrame.Frame.YB;
	Canvas.Frame.X = MainFrame.Frame.X;
	Canvas.Frame.Y = MainFrame.Frame.Y;
	Canvas.Frame.FX = MainFrame.Frame.FX;
	Canvas.Frame.FY = MainFrame.Frame.FY;
	Canvas.Frame.FX2 = MainFrame.Frame.FX2;
	Canvas.Frame.FY2 = MainFrame.Frame.FY2;
	const int eyeSizeX = std::max(static_cast<int>(Canvas.Frame.FX /
		static_cast<float>(Canvas.uiscale)), 1);
	const int eyeSizeY = std::max(static_cast<int>(Canvas.Frame.FY /
		static_cast<float>(Canvas.uiscale)), 1);
	engine->canvas->CurX() = 0.0f;
	engine->canvas->CurY() = 0.0f;
	engine->canvas->ClipX() = static_cast<float>(eyeSizeX);
	engine->canvas->ClipY() = static_cast<float>(eyeSizeY);
	engine->canvas->SizeX() = eyeSizeX;
	engine->canvas->SizeY() = eyeSizeY;
	Device->SetSceneNode(&Canvas.Frame);

	if (engine->GetXRWeaponPose().Valid)
	{
		// Match the physically proven vr-m2 route: the XR pass owns the
		// visible mesh draw. Stock RenderOverlays computes a camera-relative
		// viewmodel transform and can overwrite controller/world rotation.
		DrawActor(weapon, false, false);
		UWeapon* secondaryWeapon = engine->GetXRSecondaryWeapon(weapon);
		if (secondaryWeapon && engine->GetXROffHandWeaponPose().Valid)
		{
			DrawActor(secondaryWeapon, false, false);
			static uint64_t dualDrawCount = 0;
			if ((dualDrawCount++ % 180) == 0)
			{
				const XRWeaponPoseResult& mainPose = engine->GetXRWeaponPose();
				const XRWeaponPoseResult& offPose = engine->GetXROffHandWeaponPose();
				LogMessage("[openxr-dual-enforcer] master=" + weapon->Name.ToString() +
					" main_yaw=" + std::to_string(Rotator::FromVector(vec3(
						mainPose.AimDirection.X, mainPose.AimDirection.Y,
						mainPose.AimDirection.Z)).YawDegrees()) +
					" slave=" + secondaryWeapon->Name.ToString() +
					" off_yaw=" + std::to_string(Rotator::FromVector(vec3(
						offPose.AimDirection.X, offPose.AimDirection.Y,
						offPose.AimDirection.Z)).YawDegrees()));
			}
		}
	}
	else if (engine->LaunchInfo.ue1Version > 219)
		CallEvent(weapon, EventName::RenderOverlays,
			{ ExpressionValue::ObjectValue(engine->canvas) });
	else
	{
		CallEvent(weapon, "InvCalcView", {});
		DrawActor(weapon, false, false);
	}
	return true;
}
