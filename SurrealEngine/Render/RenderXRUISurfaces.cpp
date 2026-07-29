#include "Precomp.h"
#include "RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "Engine.h"
#include "GameWindow.h"
#include "Packages/Engine/UCanvas.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Extension/Windows/TabGroup/URootWindow.h"
#include "Packages/Extension/Windows/UWindow.h"
#include "VM/ScriptCall.h"

bool RenderSubsystem::BeginXRUICanvasCapture(const XRUICanvasReplayItem& item)
{
	if (XRUICanvasRestore.Active || item.Target.IsDefault())
		return false;

	XRUICaptureLayer = { item.Surface.Descriptor.ContentLayer, item.Target, true };
	if (!Device->BeginPresentationLayer(XRUICaptureLayer))
		return false;

	XRUICanvasRestore.Active = true;
	XRUICanvasRestore.Frame = Canvas.Frame;
	XRUICanvasRestore.UIScale = Canvas.uiscale;
	XRUICanvasRestore.CurX = engine->canvas->CurX();
	XRUICanvasRestore.CurY = engine->canvas->CurY();
	XRUICanvasRestore.ClipX = engine->canvas->ClipX();
	XRUICanvasRestore.ClipY = engine->canvas->ClipY();
	XRUICanvasRestore.SizeX = engine->canvas->SizeX();
	XRUICanvasRestore.SizeY = engine->canvas->SizeY();
	XRUICanvasRestore.HasConsoleFrame = engine->LaunchInfo.ue1Version > 219;
	if (XRUICanvasRestore.HasConsoleFrame)
	{
		XRUICanvasRestore.ConsoleFrameX = engine->console->FrameX();
		XRUICanvasRestore.ConsoleFrameY = engine->console->FrameY();
	}
	XRUICanvasRestore.ViewportX = engine->viewport->ViewportX();
	XRUICanvasRestore.ViewportY = engine->viewport->ViewportY();
	XRUICanvasRestore.ViewportWidth = engine->viewport->ViewportWidth();
	XRUICanvasRestore.ViewportHeight = engine->viewport->ViewportHeight();

	int width = item.Surface.Descriptor.PixelWidth;
	int height = item.Surface.Descriptor.PixelHeight;
	Canvas.uiscale = item.CanvasScale;
	Canvas.Frame.XB = 0;
	Canvas.Frame.YB = 0;
	Canvas.Frame.X = width;
	Canvas.Frame.Y = height;
	Canvas.Frame.FX = static_cast<float>(width);
	Canvas.Frame.FY = static_cast<float>(height);
	Canvas.Frame.FX2 = Canvas.Frame.FX * 0.5f;
	Canvas.Frame.FY2 = Canvas.Frame.FY * 0.5f;
	Canvas.Frame.ObjectToWorld = mat4::identity();
	Canvas.Frame.WorldToView = mat4::identity();
	Canvas.Frame.FovAngle = engine->CameraFovAngle;
	float aspect = Canvas.Frame.FY / Canvas.Frame.FX;
	float projectionZ = std::tan(radians(Canvas.Frame.FovAngle) * 0.5f);
	Canvas.Frame.Projection = mat4::frustum(-projectionZ, projectionZ, -aspect * projectionZ, aspect * projectionZ, 1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);

	engine->canvas->CurX() = 0.0f;
	engine->canvas->CurY() = 0.0f;
	engine->canvas->ClipX() = static_cast<float>(item.CanvasWidth());
	engine->canvas->ClipY() = static_cast<float>(item.CanvasHeight());
	engine->canvas->SizeX() = item.CanvasWidth();
	engine->canvas->SizeY() = item.CanvasHeight();
	if (engine->LaunchInfo.ue1Version > 219)
	{
		engine->console->FrameX() = engine->canvas->ClipX();
		engine->console->FrameY() = engine->canvas->ClipY();
	}
	engine->viewport->SetViewportRect(0, 0, width, height);
	Device->SetSceneNode(&Canvas.Frame);
	CallEvent(engine->canvas, EventName::Reset);
	return true;
}

void RenderSubsystem::ReplayXRUICanvas(const XRUICanvasReplayItem& item)
{
	Device->SetSceneNode(&Canvas.Frame);
	switch (item.Source)
	{
	case XRUICanvasReplaySource::PlayerAndConsolePostRender:
		if (engine->viewport->Actor())
			CallEvent(engine->viewport->Actor(), EventName::PostRender, { ExpressionValue::ObjectValue(engine->canvas) });
		CallEvent(engine->console, EventName::PostRender, { ExpressionValue::ObjectValue(engine->canvas) });
		break;
	case XRUICanvasReplaySource::CinematicFrame:
		DrawVideoContents(XRUICinematicFrame, XRUICinematicBackground);
		break;
	case XRUICanvasReplaySource::LoadingFrame:
		CallEvent(engine->console, EventName::PostRender, { ExpressionValue::ObjectValue(engine->canvas) });
		break;
	}
}

void RenderSubsystem::EndXRUICanvasCapture(const XRUICanvasReplayItem&)
{
	if (!XRUICanvasRestore.Active)
		return;

	Canvas.Frame = XRUICanvasRestore.Frame;
	Canvas.uiscale = XRUICanvasRestore.UIScale;
	engine->canvas->CurX() = XRUICanvasRestore.CurX;
	engine->canvas->CurY() = XRUICanvasRestore.CurY;
	engine->canvas->ClipX() = XRUICanvasRestore.ClipX;
	engine->canvas->ClipY() = XRUICanvasRestore.ClipY;
	engine->canvas->SizeX() = XRUICanvasRestore.SizeX;
	engine->canvas->SizeY() = XRUICanvasRestore.SizeY;
	if (XRUICanvasRestore.HasConsoleFrame)
	{
		engine->console->FrameX() = XRUICanvasRestore.ConsoleFrameX;
		engine->console->FrameY() = XRUICanvasRestore.ConsoleFrameY;
	}
	engine->viewport->SetViewportRect(XRUICanvasRestore.ViewportX, XRUICanvasRestore.ViewportY, XRUICanvasRestore.ViewportWidth, XRUICanvasRestore.ViewportHeight);
	Device->EndPresentationLayer(XRUICaptureLayer);
	Device->SetSceneNode(&Canvas.Frame);
	XRUICanvasRestore.Active = false;
}

void RenderSubsystem::MoveXRUICursor(const XRUIPointerSource& source, XRUISurfaceKind, const Pointf& canvasPixel)
{
	if (engine->dxRootWindow)
	{
		engine->dxRootWindow->SetRootCursorPos(canvasPixel.x, canvasPixel.y);
	}
	else
	{
		if (source.Kind == XRUIPointerSourceKind::Tracked)
		{
			if (!XRUIMouseStateSaved)
			{
				XRUIPreviousMouseAvailable = engine->viewport->bWindowsMouseAvailable();
				XRUIPreviousShowMouse = engine->viewport->bShowWindowsMouse();
				XRUIMouseStateSaved = true;
			}
			engine->viewport->bWindowsMouseAvailable() = true;
			engine->viewport->bShowWindowsMouse() = true;
		}
		engine->viewport->WindowsMouseX() = canvasPixel.x;
		engine->viewport->WindowsMouseY() = canvasPixel.y;
	}
}

void RenderSubsystem::PressXRUIPrimary(const XRUIPointerSource&, XRUISurfaceKind, const Pointf& canvasPixel)
{
	if (engine->dxRootWindow)
		engine->dxRootWindow->OnWindowMouseDown(Point((int)std::lround(canvasPixel.x), (int)std::lround(canvasPixel.y)), IK_LeftMouse);
	else
		engine->InputEvent(IK_LeftMouse, IST_Press);
}

void RenderSubsystem::ReleaseXRUIPrimary(const XRUIPointerSource&, XRUISurfaceKind, const Pointf& canvasPixel, bool)
{
	if (engine->dxRootWindow)
		engine->dxRootWindow->OnWindowMouseUp(Point((int)std::lround(canvasPixel.x), (int)std::lround(canvasPixel.y)), IK_LeftMouse);
	else
		engine->InputEvent(IK_LeftMouse, IST_Release);
}

void RenderSubsystem::EndXRUIPointerSession()
{
	if (!XRUIMouseStateSaved || !engine->viewport)
		return;
	engine->viewport->bWindowsMouseAvailable() = XRUIPreviousMouseAvailable;
	engine->viewport->bShowWindowsMouse() = XRUIPreviousShowMouse;
	XRUIMouseStateSaved = false;
}

bool RenderSubsystem::IsXRUIMenuActive() const
{
	UPlayerPawn* player = engine->viewport ? engine->viewport->Actor() : nullptr;
	bool scriptedMenu = player && player->bShowMenu();
	bool windowConsole = engine->console && engine->console->GetStateName() == "UWindow";
	bool nativeWindow = engine->dxRootWindow && (engine->dxRootWindow->IsModalOpen() || engine->dxRootWindow->IsCursorVisible());
	return scriptedMenu || windowConsole || nativeWindow;
}
