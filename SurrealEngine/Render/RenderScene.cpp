
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
	int fullWidth = engine->viewport->ViewportWidth();
	int fullHeight = engine->viewport->ViewportHeight();
	int halfWidth = fullWidth / 2;

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

		// angleLeft/angleDown are negative per the OpenXR spec, so these
		// are already the correct signed frustum bounds - near=1 matches
		// DrawSceneStereo's convention so tan(angle) needs no extra
		// near-plane scaling.
		float l = std::tan(VREyeFov[eye][0]);
		float r = std::tan(VREyeFov[eye][1]);
		float u = std::tan(VREyeFov[eye][2]);
		float d = std::tan(VREyeFov[eye][3]);
		mat4 projection = mat4::frustum(l, r, d, u, 1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);
		vp.Projection = &projection;

		MainFrame.Process(VREyeLocation[eye], worldToView, rotation, false, 0, {}, vec4(0.0f, 0.0f, 0.0f, 1.0f), &vp);
		VREyeFrame[eye] = MainFrame.Frame;
		MainFrame.Draw();
		MainFrame.DrawCoronas();
	}
}
