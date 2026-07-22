
#include "Precomp.h"
#include "RenderSubsystem.h"
#include "VisibleMesh.h"
#include "RenderDevice/RenderDevice.h"
#include "UObject/USubsystem.h"
#include "GameWindow.h"
#include "VM/ScriptCall.h"
#include "Engine.h"

namespace
{
	bool IsFiniteWebXRHudVector(const vec3& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	bool IsValidWebXRHudActorRectangle(int width, int height, int x, int y,
		int layoutWidth, int layoutHeight)
	{
		if (width <= 0 || height <= 0 || layoutWidth <= 0 || layoutHeight <= 0)
			return false;
		const int64_t farX = (int64_t)x + width;
		const int64_t farY = (int64_t)y + height;
		const int64_t coordinateLimit = (int64_t)std::max(layoutWidth, layoutHeight) * 16;
		return std::abs((int64_t)x) <= coordinateLimit && std::abs((int64_t)y) <= coordinateLimit &&
			std::abs(farX) <= coordinateLimit && std::abs(farY) <= coordinateLimit;
	}
}

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
	Canvas.Frame.ClipSpaceYConvention = WebGPUClipSpaceYConvention::EngineProjection;
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

bool RenderSubsystem::RenderWebXRWeaponOverlay()
{
	UPlayerPawn* viewActor = engine->viewport->Actor();
	UWeapon* weapon = viewActor ? viewActor->Weapon() : nullptr;
	if (!weapon)
		return false;

	// Keep this pass weapon-only. Calling PlayerPawn.RenderOverlays here would
	// also restore HUD/crosshair work that belongs to the later WebXR UI
	// milestone and could duplicate player-owned overlay side effects per eye.
	// The weapon still uses its stock overlay placement; controller-relative
	// viewmodel pose is supplied by the separate Engine/VM integration.
	struct ScopedCanvasStateRestore
	{
		ScopedCanvasStateRestore(FSceneNode& frame, UCanvas* canvas, RenderDevice* device, UWeapon* weapon)
			: Frame(frame), CanvasObject(canvas), DeviceObject(device), WeaponObject(weapon), SavedFrame(frame),
			SavedSizeX(canvas->SizeX()), SavedSizeY(canvas->SizeY()),
			SavedClipX(canvas->ClipX()), SavedClipY(canvas->ClipY()),
			SavedCurX(canvas->CurX()), SavedCurY(canvas->CurY()),
			SavedWeaponLocation(weapon->Location()), SavedWeaponRotation(weapon->Rotation())
		{
		}

		~ScopedCanvasStateRestore()
		{
			// Stock weapon overlays reposition the inventory actor as part of
			// drawing the viewmodel. Do not let the final eye's render transform
			// escape into simulation or the next game frame.
			WeaponObject->Location() = SavedWeaponLocation;
			WeaponObject->Rotation() = SavedWeaponRotation;
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
	} restore(Canvas.Frame, engine->canvas, Device, weapon);

	// Canvas.DrawActor uses MainFrame.Frame for the 3D weapon. Restrict the
	// canvas scene node as well so any weapon-specific tiles remain inside the
	// active eye viewport/layer instead of using the desktop-sized canvas.
	Canvas.Frame.XB = MainFrame.Frame.XB;
	Canvas.Frame.YB = MainFrame.Frame.YB;
	Canvas.Frame.X = MainFrame.Frame.X;
	Canvas.Frame.Y = MainFrame.Frame.Y;
	Canvas.Frame.FX = MainFrame.Frame.FX;
	Canvas.Frame.FY = MainFrame.Frame.FY;
	Canvas.Frame.FX2 = MainFrame.Frame.FX2;
	Canvas.Frame.FY2 = MainFrame.Frame.FY2;
	Canvas.Frame.ClipSpaceYConvention = MainFrame.Frame.ClipSpaceYConvention;
	int eyeSizeX = std::max((int)(Canvas.Frame.FX / (float)Canvas.uiscale), 1);
	int eyeSizeY = std::max((int)(Canvas.Frame.FY / (float)Canvas.uiscale), 1);
	engine->canvas->CurX() = 0.0f;
	engine->canvas->CurY() = 0.0f;
	engine->canvas->ClipX() = (float)eyeSizeX;
	engine->canvas->ClipY() = (float)eyeSizeY;
	engine->canvas->SizeX() = eyeSizeX;
	engine->canvas->SizeY() = eyeSizeY;
	Device->SetSceneNode(&Canvas.Frame);

	if (engine->LaunchInfo.ue1Version > 219)
	{
		CallEvent(weapon, EventName::RenderOverlays, { ExpressionValue::ObjectValue(engine->canvas) });
	}
	else
	{
		CallEvent(weapon, "InvCalcView", {});
		DrawActor(weapon, false, false);
	}

	return true;
}

bool RenderSubsystem::CaptureWebXRHud()
{
	WebXRHudCommands.clear();
	WebXRHudStats.LastFrameCommandTypeOrderDigest = 0;
	if (!WebXRHudSettings.Enabled)
		return false;
	UPlayerPawn* viewActor = engine->viewport->Actor();
	const bool capturePlayerHud = viewActor && viewActor->myHUD();
	const bool captureConsoleUi = engine->console != nullptr;
	if (!capturePlayerHud && !captureConsoleUi)
		return false;

	// Match the desktop PostRender order exactly: the local player owns the
	// HUD/crosshair, then Console owns the console, loading messages and UT99
	// menu/UWindow presentation. Invoke each eligible script entry point once
	// against a stable 4:3 logical canvas and capture its view-independent
	// primitive stream. Per-eye work remains replay-only, so menu focus,
	// animations, message queues and other script state never advance twice.
	FSceneNode savedFrame = Canvas.Frame;
	int savedUIScale = Canvas.uiscale;
	int savedSizeX = engine->canvas->SizeX();
	int savedSizeY = engine->canvas->SizeY();
	float savedClipX = engine->canvas->ClipX();
	float savedClipY = engine->canvas->ClipY();
	float savedCurX = engine->canvas->CurX();
	float savedCurY = engine->canvas->CurY();
	auto restoreCaptureState = [&]()
	{
		WebXRHudCaptureActive = false;
		Canvas.Frame = savedFrame;
		Canvas.uiscale = savedUIScale;
		engine->canvas->SizeX() = savedSizeX;
		engine->canvas->SizeY() = savedSizeY;
		engine->canvas->ClipX() = savedClipX;
		engine->canvas->ClipY() = savedClipY;
		engine->canvas->CurX() = savedCurX;
		engine->canvas->CurY() = savedCurY;
		Device->SetSceneNode(&Canvas.Frame);
	};

	Canvas.uiscale = 1;
	Canvas.Frame.XB = 0;
	Canvas.Frame.YB = 0;
	Canvas.Frame.X = WebXRHudLayoutWidth;
	Canvas.Frame.Y = WebXRHudLayoutHeight;
	Canvas.Frame.FX = (float)WebXRHudLayoutWidth;
	Canvas.Frame.FY = (float)WebXRHudLayoutHeight;
	Canvas.Frame.FX2 = Canvas.Frame.FX * 0.5f;
	Canvas.Frame.FY2 = Canvas.Frame.FY * 0.5f;
	Canvas.Frame.ObjectToWorld = mat4::identity();
	Canvas.Frame.WorldToView = mat4::identity();
	Canvas.Frame.ProjectionOverride = false;
	engine->canvas->SizeX() = WebXRHudLayoutWidth;
	engine->canvas->SizeY() = WebXRHudLayoutHeight;
	engine->canvas->ClipX() = (float)WebXRHudLayoutWidth;
	engine->canvas->ClipY() = (float)WebXRHudLayoutHeight;
	engine->canvas->CurX() = 0.0f;
	engine->canvas->CurY() = 0.0f;
	WebXRHudCaptureActive = true;
	WebXRHudStats.StateUpdates++;
	WebXRHudStats.LastFrameStateUpdates++;
	try
	{
		if (capturePlayerHud)
		{
			CallEvent(viewActor, EventName::PostRender, { ExpressionValue::ObjectValue(engine->canvas) });
			WebXRHudStats.LastFramePlayerPostRenderCalls++;
		}
		if (captureConsoleUi)
		{
			CallEvent(engine->console, EventName::PostRender,
				{ ExpressionValue::ObjectValue(engine->canvas) });
			WebXRHudStats.LastFrameConsolePostRenderCalls++;
		}
	}
	catch (...)
	{
		// Never replay a partially captured primitive stream. AH1 actor-style
		// calls own no roots, but the same all-or-nothing lifetime boundary is
		// required before AH2 adds rooted snapshots.
		WebXRHudCommands.clear();
		WebXRHudStats.LastFrameCommandTypeOrderDigest = 0;
		restoreCaptureState();
		throw;
	}
	restoreCaptureState();

	WebXRHudStats.CapturedCommands += WebXRHudCommands.size();
	WebXRHudStats.LastFrameCapturedCommands = (uint32_t)WebXRHudCommands.size();
	return !WebXRHudCommands.empty();
}

RenderSubsystem::WebXRHudActorRejectionReason RenderSubsystem::ClassifyWebXRHudActor(
	const WebXRHudActorClassificationInput& input)
{
	if (input.CommandLimitReached) return WebXRHudActorRejectionReason::CommandLimit;
	if (!input.ActorPresent) return WebXRHudActorRejectionReason::NullActor;
	if (input.Deleted) return WebXRHudActorRejectionReason::DeletedActor;
	if (!input.RectangleValid) return WebXRHudActorRejectionReason::InvalidRectangle;
	if (!input.FiniteState) return WebXRHudActorRejectionReason::NonFiniteState;
	if (input.StaleLevel) return WebXRHudActorRejectionReason::StaleLevel;
	if (!input.DependenciesPresent) return WebXRHudActorRejectionReason::MissingDependency;
	if (input.ForbiddenLocalActor) return WebXRHudActorRejectionReason::ForbiddenLocalActor;
	if (input.OwnerAnimated) return WebXRHudActorRejectionReason::OwnerAnimation;
	if (input.DynamicallyLit) return WebXRHudActorRejectionReason::DynamicLighting;
	if (!input.SupportedMeshType) return WebXRHudActorRejectionReason::UnsupportedMeshType;
	return WebXRHudActorRejectionReason::SnapshotUnavailable;
}

uint64_t RenderSubsystem::HashWebXRHudCommandType(uint64_t digest, WebXRHudCommandType type)
{
	// FNV-1a over type tags only: deterministic ordering evidence without
	// object names, pointers, package paths, or imported-content metadata.
	if (digest == 0)
		digest = 14695981039346656037ull;
	digest ^= (uint8_t)type;
	digest *= 1099511628211ull;
	return digest;
}

bool RenderSubsystem::AppendWebXRHudCommand(const WebXRHudCommand& command)
{
	if (WebXRHudCommands.size() >= WebXRHudDiagnostics::MaximumTotalCommandsPerFrame)
	{
		WebXRHudStats.TotalCommandLimitRejects++;
		WebXRHudStats.LastFrameTotalCommandLimitRejects++;
		WebXRHudStats.UnsupportedDraws++;
		WebXRHudStats.LastFrameUnsupportedDraws++;
		return false;
	}
	WebXRHudCommands.push_back(command);
	WebXRHudStats.LastFrameCommandTypeOrderDigest = HashWebXRHudCommandType(
		WebXRHudStats.LastFrameCommandTypeOrderDigest, command.Type);
	WebXRHudStats.ObservedMaximumCapturedCommands = std::max(
		WebXRHudStats.ObservedMaximumCapturedCommands, (uint32_t)WebXRHudCommands.size());
	return true;
}

void RenderSubsystem::RejectWebXRHudActor(WebXRHudCommandType type,
	WebXRHudActorRejectionReason reason)
{
	WebXRHudTypedCommandDiagnostics& typed = type == WebXRHudCommandType::ActorWorld ?
		WebXRHudStats.ActorWorld : WebXRHudStats.ActorClipped;
	typed.Attempts++;
	typed.LastFrameAttempts++;
	const uint32_t attempts = WebXRHudStats.ActorWorld.LastFrameAttempts +
		WebXRHudStats.ActorClipped.LastFrameAttempts;
	WebXRHudStats.ObservedMaximumActorAttempts = std::max(
		WebXRHudStats.ObservedMaximumActorAttempts, attempts);

	auto increment = [reason](WebXRHudActorRejectionDiagnostics& counters)
	{
		switch (reason)
		{
		case WebXRHudActorRejectionReason::NullActor: counters.NullActor++; break;
		case WebXRHudActorRejectionReason::DeletedActor: counters.DeletedActor++; break;
		case WebXRHudActorRejectionReason::MissingDependency: counters.MissingDependency++; break;
		case WebXRHudActorRejectionReason::NonFiniteState: counters.NonFiniteState++; break;
		case WebXRHudActorRejectionReason::ForbiddenLocalActor: counters.ForbiddenLocalActor++; break;
		case WebXRHudActorRejectionReason::OwnerAnimation: counters.OwnerAnimation++; break;
		case WebXRHudActorRejectionReason::DynamicLighting: counters.DynamicLighting++; break;
		case WebXRHudActorRejectionReason::UnsupportedMeshType: counters.UnsupportedMeshType++; break;
		case WebXRHudActorRejectionReason::InvalidRectangle: counters.InvalidRectangle++; break;
		case WebXRHudActorRejectionReason::CommandLimit: counters.CommandLimit++; break;
		case WebXRHudActorRejectionReason::StaleLevel: counters.StaleLevel++; break;
		case WebXRHudActorRejectionReason::SnapshotUnavailable: counters.SnapshotUnavailable++; break;
		}
	};
	increment(WebXRHudStats.ActorRejections);
	increment(WebXRHudStats.LastFrameActorRejections);
	WebXRHudStats.UnsupportedDraws++;
	WebXRHudStats.LastFrameUnsupportedDraws++;
}

void RenderSubsystem::RejectWebXRHudLine3D(WebXRHudLine3DRejectionReason reason)
{
	WebXRHudStats.Line3D.Attempts++;
	WebXRHudStats.Line3D.LastFrameAttempts++;
	auto increment = [reason](WebXRHudLine3DRejectionDiagnostics& counters)
	{
		switch (reason)
		{
		case WebXRHudLine3DRejectionReason::NonFiniteEndpoints: counters.NonFiniteEndpoints++; break;
		case WebXRHudLine3DRejectionReason::CommandLimit: counters.CommandLimit++; break;
		case WebXRHudLine3DRejectionReason::ProductionDisabled: counters.ProductionDisabled++; break;
		}
	};
	increment(WebXRHudStats.Line3DRejections);
	increment(WebXRHudStats.LastFrameLine3DRejections);
	WebXRHudStats.UnsupportedDraws++;
	WebXRHudStats.LastFrameUnsupportedDraws++;
}

void RenderSubsystem::SubmitCanvasTile(FTextureInfo& info, float x, float y, float width, float height,
	float u, float v, float uLength, float vLength, float z, vec4 color, vec4 fog, uint32_t flags)
{
	if (WebXRHudCaptureActive)
	{
		WebXRHudCommand command;
		command.Type = WebXRHudCommandType::Tile;
		command.Texture = info;
		command.X = x;
		command.Y = y;
		command.Width = width;
		command.Height = height;
		command.U = u;
		command.V = v;
		command.ULength = uLength;
		command.VLength = vLength;
		command.Z = z;
		command.Color = color;
		command.Fog = fog;
		command.Flags = flags;
		AppendWebXRHudCommand(command);
		return;
	}
	Device->DrawTile(&Canvas.Frame, info, x, y, width, height, u, v, uLength, vLength, z, color, fog, flags);
}

void RenderSubsystem::SubmitCanvas2DLine(vec4 color, uint32_t flags, vec3 p1, vec3 p2)
{
	if (WebXRHudCaptureActive)
	{
		WebXRHudCommand command;
		command.Type = WebXRHudCommandType::Line2D;
		command.Color = color;
		command.Flags = flags;
		command.P1 = p1;
		command.P2 = p2;
		AppendWebXRHudCommand(command);
		return;
	}
	Device->Draw2DLine(&Canvas.Frame, color, flags, p1, p2);
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

void RenderSubsystem::PostRenderFlash()
{
	Device->SetSceneNode(&Canvas.Frame);
	if (engine->viewport->Actor())
		CallEvent(engine->viewport->Actor(), "PostRenderFlash", {ExpressionValue::ObjectValue(engine->canvas)});
}

void RenderSubsystem::DrawActor(UActor* actor, bool WireFrame, bool ClearZ)
{
	if (WebXRHudCaptureActive)
	{
		WebXRHudActorClassificationInput input;
		const uint32_t nextAttempt = WebXRHudStats.ActorWorld.LastFrameAttempts +
			WebXRHudStats.ActorClipped.LastFrameAttempts + 1;
		input.CommandLimitReached = nextAttempt > WebXRHudDiagnostics::MaximumActorCommandsPerFrame ||
			WebXRHudCommands.size() >= WebXRHudDiagnostics::MaximumTotalCommandsPerFrame;
		input.ActorPresent = actor != nullptr;
		if (actor)
		{
			input.Deleted = actor->bDeleteMe();
			input.DependenciesPresent = actor->Class && actor->Level() && actor->XLevel() && actor->Mesh();
			input.FiniteState = IsFiniteWebXRHudVector(actor->Location()) &&
				IsFiniteWebXRHudVector(actor->PrePivot()) && std::isfinite(actor->DrawScale()) &&
				std::isfinite(actor->AnimFrame()) && std::isfinite(actor->AnimRate()) &&
				std::isfinite(actor->TweenRate()) && std::isfinite(actor->ScaleGlow());
			UPlayerPawn* localPawn = engine->viewport ? engine->viewport->Actor() : nullptr;
			input.ForbiddenLocalActor = actor == localPawn ||
				(localPawn && actor == localPawn->Weapon()) ||
				(localPawn && actor->IsA("Inventory") && actor->IsOwnedBy(localPawn));
			input.OwnerAnimated = actor->bAnimByOwner();
			input.DynamicallyLit = !actor->bUnlit() && actor->Region().ZoneNumber != 0;
			input.SupportedMeshType = actor->Mesh() && !UObject::TryCast<USkeletalMesh>(actor->Mesh());
			input.StaleLevel = engine->Level && actor->XLevel() != engine->Level;
		}
		RejectWebXRHudActor(WebXRHudCommandType::ActorWorld, ClassifyWebXRHudActor(input));
		return;
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
	if (WebXRHudCaptureActive)
	{
		WebXRHudActorClassificationInput input;
		const uint32_t nextAttempt = WebXRHudStats.ActorWorld.LastFrameAttempts +
			WebXRHudStats.ActorClipped.LastFrameAttempts + 1;
		input.CommandLimitReached = nextAttempt > WebXRHudDiagnostics::MaximumActorCommandsPerFrame ||
			WebXRHudCommands.size() >= WebXRHudDiagnostics::MaximumTotalCommandsPerFrame;
		input.ActorPresent = actor != nullptr;
		input.RectangleValid = IsValidWebXRHudActorRectangle(X, Y, XB, YB,
			WebXRHudLayoutWidth, WebXRHudLayoutHeight);
		if (actor)
		{
			input.Deleted = actor->bDeleteMe();
			input.DependenciesPresent = actor->Class && actor->Level() && actor->XLevel() && actor->Mesh();
			input.FiniteState = IsFiniteWebXRHudVector(actor->Location()) &&
				IsFiniteWebXRHudVector(actor->PrePivot()) && std::isfinite(actor->DrawScale()) &&
				std::isfinite(actor->AnimFrame()) && std::isfinite(actor->AnimRate()) &&
				std::isfinite(actor->TweenRate()) && std::isfinite(actor->ScaleGlow());
			UPlayerPawn* localPawn = engine->viewport ? engine->viewport->Actor() : nullptr;
			input.ForbiddenLocalActor = actor == localPawn ||
				(localPawn && actor == localPawn->Weapon()) ||
				(localPawn && actor->IsA("Inventory") && actor->IsOwnedBy(localPawn));
			input.OwnerAnimated = actor->bAnimByOwner();
			input.DynamicallyLit = !actor->bUnlit() && actor->Region().ZoneNumber != 0;
			input.SupportedMeshType = actor->Mesh() && !UObject::TryCast<USkeletalMesh>(actor->Mesh());
			input.StaleLevel = engine->Level && actor->XLevel() != engine->Level;
		}
		RejectWebXRHudActor(WebXRHudCommandType::ActorClipped, ClassifyWebXRHudActor(input));
		return;
	}
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

	SubmitCanvasTile(texinfo, x * Canvas.uiscale, y * Canvas.uiscale, XL * Canvas.uiscale, YL * Canvas.uiscale, U, V, UL, VL, Z, color, fog, flags);
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

			SubmitCanvasTile(texinfo, x * Canvas.uiscale, y * Canvas.uiscale, (float)width * Canvas.uiscale, (float)height * Canvas.uiscale, StartU, StartV, USize, VSize, 1.0f, color, vec4(0.0f), polyflags);

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
		SubmitCanvasTile(texinfo, dest.left * Canvas.uiscale, dest.top * Canvas.uiscale, (dest.right - dest.left) * Canvas.uiscale, (dest.bottom - dest.top) * Canvas.uiscale, src.left, src.top, src.right - src.left, src.bottom - src.top, Z, color, fog, flags);
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
			SubmitCanvasTile(texinfo, d.left * Canvas.uiscale, d.top * Canvas.uiscale, (d.right - d.left) * Canvas.uiscale, (d.bottom - d.top) * Canvas.uiscale, s.left, s.top, s.right - s.left, s.bottom - s.top, Z, color, fog, flags);
	}
}

void RenderSubsystem::Draw2DLine(vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2)
{
	auto uiscale = static_cast<float>(Canvas.uiscale);
	SubmitCanvas2DLine(Color, LineFlags, vec3(P1.xy() * uiscale, P1.z), vec3(P2.xy() * uiscale, P2.z));
}

void RenderSubsystem::Draw3DLine(vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2)
{
	if (WebXRHudCaptureActive)
	{
		WebXRHudLine3DRejectionReason reason = WebXRHudLine3DRejectionReason::ProductionDisabled;
		if (WebXRHudCommands.size() >= WebXRHudDiagnostics::MaximumTotalCommandsPerFrame)
			reason = WebXRHudLine3DRejectionReason::CommandLimit;
		else if (!IsFiniteWebXRHudVector(P1) || !IsFiniteWebXRHudVector(P2))
			reason = WebXRHudLine3DRejectionReason::NonFiniteEndpoints;
		RejectWebXRHudLine3D(reason);
		return;
	}
	Device->Draw3DLine(&Canvas.Frame, Color, LineFlags, P1, P2);
}

void RenderSubsystem::DrawTile(FTextureInfo& Info, float X, float Y, float XL, float YL, float U, float V, float UL, float VL, float Z, vec4 Color, vec4 Fog, uint32_t PolyFlags)
{
	SubmitCanvasTile(Info, X, Y, XL, YL, U, V, UL, VL, Z, Color, Fog, PolyFlags);
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
