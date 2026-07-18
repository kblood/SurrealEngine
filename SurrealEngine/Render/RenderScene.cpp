
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
