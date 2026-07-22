
#include "Precomp.h"
#include "RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "GameWindow.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include "VisibleFrame.h"

namespace
{
	struct WebXRHudViewport
	{
		int X = 0;
		int Y = 0;
		int Width = 0;
		int Height = 0;
		bool Clamped = false;
		bool Valid = false;
	};

	bool ProjectWebXRHudPoint(const WebXRSceneView& view, const vec3& point, float& pixelX, float& pixelY)
	{
		vec4 clip = view.Projection * (view.WorldToView * vec4(point, 1.0f));
		if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w) || std::abs(clip.w) < 0.00001f)
			return false;
		float ndcX = clip.x / clip.w;
		float ndcY = clip.y / clip.w;
		pixelX = (ndcX + 1.0f) * 0.5f * view.ViewportWidth;
		pixelY = (ndcY + 1.0f) * 0.5f * view.ViewportHeight;
		return std::isfinite(pixelX) && std::isfinite(pixelY);
	}

	WebXRHudViewport CalculateWebXRHudViewport(const WebXRSceneView* views, uint32_t viewCount,
		uint32_t eyeIndex, const WebXRHudPlaneSettings& settings)
	{
		WebXRHudViewport result;
		if (!views || viewCount == 0 || eyeIndex >= viewCount || views[eyeIndex].ViewportWidth < 4 || views[eyeIndex].ViewportHeight < 4)
			return result;

		vec3 center(0.0f), forward(0.0f), right(0.0f), up(0.0f);
		for (uint32_t index = 0; index < viewCount; index++)
		{
			center += views[index].Location;
			forward += views[index].ViewRotation.XAxis;
			right += views[index].ViewRotation.YAxis;
			up += views[index].ViewRotation.ZAxis;
		}
		center *= 1.0f / viewCount;
		if (length(forward) < 0.0001f || length(right) < 0.0001f || length(up) < 0.0001f)
			return result;
		forward = normalize(forward);
		right = normalize(right);
		up = normalize(up);

		const float halfWidthUU = settings.DistanceUU * std::tan(radians(settings.HorizontalFovDegrees) * 0.5f);
		const float halfHeightUU = halfWidthUU / settings.AspectRatio;
		const vec3 planeCenter = center + forward * settings.DistanceUU;
		const vec3 corners[4] =
		{
			planeCenter - right * halfWidthUU + up * halfHeightUU,
			planeCenter + right * halfWidthUU + up * halfHeightUU,
			planeCenter - right * halfWidthUU - up * halfHeightUU,
			planeCenter + right * halfWidthUU - up * halfHeightUU
		};

		float minimumX = std::numeric_limits<float>::max();
		float minimumY = std::numeric_limits<float>::max();
		float maximumX = -std::numeric_limits<float>::max();
		float maximumY = -std::numeric_limits<float>::max();
		for (const vec3& corner : corners)
		{
			float x = 0.0f, y = 0.0f;
			if (!ProjectWebXRHudPoint(views[eyeIndex], corner, x, y))
				return result;
			minimumX = std::min(minimumX, x);
			minimumY = std::min(minimumY, y);
			maximumX = std::max(maximumX, x);
			maximumY = std::max(maximumY, y);
		}

		const int safeInsetX = std::clamp((int)std::round(views[eyeIndex].ViewportWidth * (1.0f - settings.SafeAreaFraction) * 0.5f), 0, (views[eyeIndex].ViewportWidth - 2) / 2);
		const int safeInsetY = std::clamp((int)std::round(views[eyeIndex].ViewportHeight * (1.0f - settings.SafeAreaFraction) * 0.5f), 0, (views[eyeIndex].ViewportHeight - 2) / 2);
		const int unclampedLeft = (int)std::floor(minimumX);
		const int unclampedTop = (int)std::floor(minimumY);
		const int unclampedRight = (int)std::ceil(maximumX);
		const int unclampedBottom = (int)std::ceil(maximumY);
		const int left = std::clamp(unclampedLeft, safeInsetX, views[eyeIndex].ViewportWidth - safeInsetX - 1);
		const int top = std::clamp(unclampedTop, safeInsetY, views[eyeIndex].ViewportHeight - safeInsetY - 1);
		const int rightEdge = std::clamp(unclampedRight, left + 1, views[eyeIndex].ViewportWidth - safeInsetX);
		const int bottomEdge = std::clamp(unclampedBottom, top + 1, views[eyeIndex].ViewportHeight - safeInsetY);
		result.X = left;
		result.Y = top;
		result.Width = rightEdge - left;
		result.Height = bottomEdge - top;
		result.Clamped = left != unclampedLeft || top != unclampedTop || rightEdge != unclampedRight || bottomEdge != unclampedBottom;
		result.Valid = result.Width > 0 && result.Height > 0;
		return result;
	}
}

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

void RenderSubsystem::DrawSceneView(const vec3& location, const mat4& worldToView, const Coords& viewRotation, const ViewportOverride* viewportOverride)
{
	MainFrame.Process(location, worldToView, viewRotation, false, 0, {}, vec4(0.0f, 0.0f, 0.0f, 1.0f), viewportOverride);
	MainFrame.Draw();
	MainFrame.DrawCoronas();
}

void RenderSubsystem::DrawScene()
{
	if (!PrepareSceneViews())
		return;

	mat4 worldToView = Coords::ViewToRenderDev().ToMatrix() * Coords::Rotation(engine->CameraRotation).Inverse().ToMatrix() * Coords::Location(engine->CameraLocation).ToMatrix();
	DrawSceneView(engine->CameraLocation, worldToView, Coords::Rotation(engine->CameraRotation));
}

void RenderSubsystem::DrawSceneStereo()
{
	if (!PrepareSceneViews())
		return;

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

		DrawSceneView(eyeLocation, worldToView, rotation, &vp);
	}
}

void RenderSubsystem::DrawSceneStereoLayers()
{
	if (!PrepareSceneViews())
		return;

	Coords rotation = Coords::Rotation(engine->CameraRotation);
	Coords invRotation = rotation.Inverse();
	const float halfIPD = 32.0f;
	const float convergence = 500.0f;
	const int width = Device->GetRenderWidth();
	const int height = Device->GetRenderHeight();

	for (uint32_t eye = 0; eye < 2; eye++)
	{
		if (eye != 0 && !Device->SelectExternalRenderTargetLayer(eye))
			return;

		float sign = (eye == 0) ? -1.0f : 1.0f;
		vec3 eyeLocation = engine->CameraLocation + rotation.YAxis * (halfIPD * sign);
		mat4 worldToView = Coords::ViewToRenderDev().ToMatrix() * invRotation.ToMatrix() * Coords::Location(eyeLocation).ToMatrix();

		ViewportOverride vp;
		vp.XB = 0;
		vp.YB = 0;
		vp.X = width;
		vp.Y = height;

		float aspect = (float)height / (float)width;
		float rProjZ = (float)std::tan(radians(engine->CameraFovAngle) * 0.5f);
		float frustumShift = -sign * halfIPD * (1.0f / convergence);
		mat4 projection = mat4::frustum(-rProjZ + frustumShift, rProjZ + frustumShift,
			-aspect * rProjZ, aspect * rProjZ, 1.0f, 32768.0f,
			handedness::left, clipzrange::zero_positive_w);
		vp.Projection = &projection;

		DrawSceneView(eyeLocation, worldToView, rotation, &vp);
	}
}

bool RenderSubsystem::DrawSceneWebXRViews(const WebXRSceneView* views, uint32_t viewCount)
{
	if (!views || viewCount == 0 || !PrepareSceneViews())
		return false;

	WebXRWeaponOverlayStats.Frames++;
	WebXRWeaponOverlayStats.LastFrameExpectedEyePasses = viewCount;
	WebXRWeaponOverlayStats.LastFrameEyePasses = 0;
	WebXRWeaponOverlayStats.LastFrameWeaponCalls = 0;
	WebXRHudStats.Frames++;
	WebXRHudStats.LastFrameExpectedEyePresentations = 0;
	WebXRHudStats.LastFrameStateUpdates = 0;
	WebXRHudStats.LastFrameEyePresentations = 0;
	WebXRHudStats.LastFrameCapturedCommands = 0;
	WebXRHudStats.LastFrameUnsupportedDraws = 0;
	WebXRHudStats.LastFrameClampedViewports = 0;

	for (uint32_t index = 0; index < viewCount; index++)
	{
		const WebXRSceneView& view = views[index];
		if (index != 0 && !Device->SelectExternalRenderTargetView(view.ArrayLayer,
			view.ViewportX, view.ViewportY, view.ViewportWidth, view.ViewportHeight))
			return false;

		ViewportOverride viewport;
		viewport.XB = 0;
		viewport.YB = 0;
		viewport.X = view.ViewportWidth;
		viewport.Y = view.ViewportHeight;
		viewport.Projection = &view.Projection;
		DrawSceneView(view.Location, view.WorldToView, view.ViewRotation, &viewport);

		// Restore only the first-person weapon here. The full player/HUD/menu
		// overlay lifecycle remains a single, separate M9 concern.
		WebXRWeaponOverlayStats.EyePasses++;
		WebXRWeaponOverlayStats.LastFrameEyePasses++;
		if (RenderWebXRWeaponOverlay())
		{
			WebXRWeaponOverlayStats.WeaponCalls++;
			WebXRWeaponOverlayStats.LastFrameWeaponCalls++;
		}
	}

	if (CaptureWebXRHud())
	{
		WebXRHudStats.LastFrameExpectedEyePresentations = viewCount;
		if (!PresentWebXRHud(views, viewCount))
			return false;
	}
	return true;
}

bool RenderSubsystem::PresentWebXRHud(const WebXRSceneView* views, uint32_t viewCount)
{
	if (!views || viewCount == 0 || WebXRHudCommands.empty())
		return false;

	struct ScopedHudPresentationRestore
	{
		ScopedHudPresentationRestore(FSceneNode& frame, UCanvas* canvas, RenderDevice* device)
			: Frame(frame), CanvasObject(canvas), DeviceObject(device), SavedFrame(frame),
			SavedSizeX(canvas->SizeX()), SavedSizeY(canvas->SizeY()),
			SavedClipX(canvas->ClipX()), SavedClipY(canvas->ClipY()),
			SavedCurX(canvas->CurX()), SavedCurY(canvas->CurY())
		{
		}

		~ScopedHudPresentationRestore()
		{
			Frame = SavedFrame;
			CanvasObject->SizeX() = SavedSizeX;
			CanvasObject->SizeY() = SavedSizeY;
			CanvasObject->ClipX() = SavedClipX;
			CanvasObject->ClipY() = SavedClipY;
			CanvasObject->CurX() = SavedCurX;
			CanvasObject->CurY() = SavedCurY;
			DeviceObject->SetSceneNode(&Frame);
		}

		FSceneNode& Frame;
		UCanvas* CanvasObject;
		RenderDevice* DeviceObject;
		FSceneNode SavedFrame;
		int SavedSizeX;
		int SavedSizeY;
		float SavedClipX;
		float SavedClipY;
		float SavedCurX;
		float SavedCurY;
	} restore(Canvas.Frame, engine->canvas, Device);
	bool success = true;

	for (uint32_t index = 0; index < viewCount; index++)
	{
		const WebXRSceneView& view = views[index];
		const WebXRHudViewport viewport = CalculateWebXRHudViewport(views, viewCount, index, WebXRHudSettings);
		if (!viewport.Valid || !Device->SelectExternalRenderTargetView(view.ArrayLayer,
			view.ViewportX, view.ViewportY, view.ViewportWidth, view.ViewportHeight))
		{
			success = false;
			break;
		}
		if (viewport.Clamped)
		{
			WebXRHudStats.ClampedViewports++;
			WebXRHudStats.LastFrameClampedViewports++;
		}

		Canvas.Frame = restore.SavedFrame;
		Canvas.Frame.XB = viewport.X;
		Canvas.Frame.YB = viewport.Y;
		Canvas.Frame.X = viewport.Width;
		Canvas.Frame.Y = viewport.Height;
		Canvas.Frame.FX = (float)viewport.Width;
		Canvas.Frame.FY = (float)viewport.Height;
		Canvas.Frame.FX2 = Canvas.Frame.FX * 0.5f;
		Canvas.Frame.FY2 = Canvas.Frame.FY * 0.5f;
		Canvas.Frame.ObjectToWorld = mat4::identity();
		Canvas.Frame.WorldToView = mat4::identity();
		Canvas.Frame.ProjectionOverride = false;
		Device->SetSceneNode(&Canvas.Frame);

		const float scaleX = viewport.Width / (float)WebXRHudLayoutWidth;
		const float scaleY = viewport.Height / (float)WebXRHudLayoutHeight;
		for (const WebXRHudCommand& command : WebXRHudCommands)
		{
			if (command.Type == WebXRHudCommandType::Tile)
			{
				Device->DrawTile(&Canvas.Frame, command.Texture,
					command.X * scaleX, command.Y * scaleY, command.Width * scaleX, command.Height * scaleY,
					command.U, command.V, command.ULength, command.VLength, command.Z,
					command.Color, command.Fog, command.Flags);
			}
			else
			{
				vec3 p1(command.P1.x * scaleX, command.P1.y * scaleY, command.P1.z);
				vec3 p2(command.P2.x * scaleX, command.P2.y * scaleY, command.P2.z);
				Device->Draw2DLine(&Canvas.Frame, command.Color, command.Flags, p1, p2);
			}
		}
		WebXRHudStats.EyePresentations++;
		WebXRHudStats.LastFrameEyePresentations++;
	}

	return success;
}

uint32_t RenderSubsystem::RunWebXRHudSelfTest()
{
	uint32_t mask = 0;
	const uint32_t presentUpdates = 1;
	const uint32_t presentEyes = 2;
	if (presentUpdates == 1 && presentEyes == 2)
		mask |= WebXRHudSelfTestSingleUpdateStereoPresentation;
	const uint32_t absentUpdates = 0;
	const uint32_t absentEyes = 0;
	if (absentUpdates == 0 && absentEyes == 0)
		mask |= WebXRHudSelfTestAbsentHud;

	WebXRSceneView testViews[2] = {};
	for (uint32_t index = 0; index < 2; index++)
	{
		testViews[index].ViewportWidth = 1000;
		testViews[index].ViewportHeight = 1000;
		testViews[index].Location = vec3(0.0f, index == 0 ? -1.25f : 1.25f, 0.0f);
		testViews[index].ViewRotation = Coords::Identity();
		testViews[index].WorldToView = Coords::ViewToRenderDev().ToMatrix() *
			Coords::Location(testViews[index].Location).ToMatrix();
		testViews[index].Projection = index == 0 ?
			mat4::frustum(-0.8f, 1.2f, -1.0f, 1.0f, 1.0f, 1000.0f, handedness::left, clipzrange::zero_positive_w) :
			mat4::frustum(-1.2f, 0.8f, -1.0f, 1.0f, 1.0f, 1000.0f, handedness::left, clipzrange::zero_positive_w);
	}
	WebXRHudPlaneSettings settings;
	const WebXRHudViewport left = CalculateWebXRHudViewport(testViews, 2, 0, settings);
	const WebXRHudViewport right = CalculateWebXRHudViewport(testViews, 2, 1, settings);
	const float leftCenter = left.X + left.Width * 0.5f;
	const float rightCenter = right.X + right.Width * 0.5f;
	if (left.Valid && right.Valid && leftCenter < 490.0f && rightCenter > 510.0f)
		mask |= WebXRHudSelfTestAsymmetricProjection;

	settings.HorizontalFovDegrees = 75.0f;
	settings.SafeAreaFraction = 0.5f;
	const WebXRHudViewport clamped = CalculateWebXRHudViewport(testViews, 2, 0, settings);
	if (clamped.Valid && clamped.Clamped && clamped.X >= 250 && clamped.Y >= 250 &&
		clamped.X + clamped.Width <= 750 && clamped.Y + clamped.Height <= 750)
		mask |= WebXRHudSelfTestViewportClamping;
	return mask;
}
