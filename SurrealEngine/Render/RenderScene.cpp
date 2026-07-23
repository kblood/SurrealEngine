
#include "Precomp.h"
#include "RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "GameWindow.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include "VisibleFrame.h"
#include "XR/Avatar/AvatarRenderer.h"

bool RenderSubsystem::PrepareSceneViews()
{
	if (!engine->Level)
		return false;

	Light.FogFrameCounter++;
	TextureFrameCounter++;

	// Make sure all actors are at the right location in the BSP
	for (UActor* actor : engine->Level->Actors)
	{
		if (actor)
			actor->UpdateBspInfo();
	}
	return true;
}

void RenderSubsystem::DrawSceneView(const ViewDescription& view)
{
	// A small amount of legacy rendering still consults the engine camera
	// location directly (fog and corona occlusion). Keep it synchronized with
	// the explicitly selected view for the duration of this pass.
	struct CameraLocationScope
	{
		CameraLocationScope(vec3& location, const vec3& value) : Location(location), Saved(location) { Location = value; }
		~CameraLocationScope() { Location = Saved; }
		vec3& Location;
		vec3 Saved;
	} cameraLocationScope(engine->CameraLocation, view.Location);

	MainFrame.Process(view.Location, view.WorldToView, view.Rotation, false, 0, {}, vec4(0.0f, 0.0f, 0.0f, 1.0f), &view);
	MainFrame.Draw();
	MainFrame.DrawCoronas();

	// With --avatar-autorig-debug, draw the local player's auto-rigged avatar
	// beside their normal render for visual comparison. Never runs unless
	// explicitly enabled - no effect on normal play. M2: drives it with the
	// frame's IK-solved pose when a head/hand sample is available (real
	// OpenXR or --avatar-ik-synthetic), falling back to the M1 static bind
	// pose otherwise.
	if (AvatarRenderer::DiagnosticsEnabled() && engine->viewport && engine->viewport->Actor())
	{
		UActor* playerActor = engine->viewport->Actor();
		Coords rotation = Coords::Rotation(playerActor->Rotation());
		vec3 sideOffset = rotation.YAxis * 80.0f;
		const AvatarIKFrameInput& avatarInput = engine->GetXRAvatarInput();
		bool haveLiveInput = avatarInput.Head.Valid || avatarInput.LeftHandGrip.Valid || avatarInput.RightHandGrip.Valid;
		AvatarIKOptions options;
		options.CullHeadForFirstPerson = AvatarRenderer::CullHeadDebugEnabled();
		if (!haveLiveInput || !AvatarRenderer::DrawActorWithIK(&MainFrame, playerActor, sideOffset, avatarInput, options, LevelTimeElapsed))
			AvatarRenderer::DrawActorBindPose(&MainFrame, playerActor, sideOffset);
	}
}

void RenderSubsystem::DrawScene()
{
	if (!PrepareSceneViews())
		return;

	ViewDescription view;
	view.Location = engine->CameraLocation;
	view.Rotation = Coords::Rotation(engine->CameraRotation);
	view.WorldToView = Coords::ViewToRenderDev().ToMatrix() * view.Rotation.Inverse().ToMatrix() * Coords::Location(view.Location).ToMatrix();
	view.Viewport.X = engine->viewport->ViewportX();
	view.Viewport.Y = engine->viewport->ViewportY();
	view.Viewport.Width = engine->viewport->ViewportWidth();
	view.Viewport.Height = engine->viewport->ViewportHeight();
	view.FovAngle = engine->CameraFovAngle;
	view.ApplyGameViewport = true;
	DrawSceneView(view);
}

void RenderSubsystem::DrawScene(const ViewFamily& viewFamily, bool renderWeaponPerView)
{
	if (!PrepareSceneViews())
		return;

	PresentationTarget target = viewFamily.Presentation.GetLayer(PresentationLayer::World).Target;
	for (size_t viewIndex = 0; viewIndex < viewFamily.Views.size(); viewIndex++)
	{
		const ViewDescription& view = viewFamily.Views[viewIndex];
		if (view.Viewport.Width > 0 && view.Viewport.Height > 0)
		{
			if (Device->BeginPresentationView(target, viewIndex))
			{
				DrawSceneView(view);
				if (renderWeaponPerView)
					RenderXRWeaponOverlay();
				Device->EndPresentationView(target, viewIndex);
			}
		}
	}
}
