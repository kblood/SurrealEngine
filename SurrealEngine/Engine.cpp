
#include "Precomp.h"
#include "Engine.h"
#include "Utils/File.h"
#include "Utils/StrTools.h"
#include "Utils/CommandLine.h"
#include "Utils/SHA1Sum.h"
#include "Render/RenderSubsystem.h"
#include "Package/PackageManager.h"
#include "Package/ObjectStream.h"
#include "UObject/ULevel.h"
#include "UObject/UFont.h"
#include "UObject/UMesh.h"
#include "UObject/UActor.h"
#include "UObject/ObjectTravelInfo.h"
#include "UObject/UTexture.h"
#include "UObject/UMusic.h"
#include "UObject/USound.h"
#include "UObject/UClass.h"
#include "UObject/UClient.h"
#include "UObject/USubsystem.h"
#include "UObject/UFlag.h"
#include "UObject/UConSys.h"
#include "Math/quaternion.h"
#include "Math/coords.h"
#include "Math/FrustumPlanes.h"
#include "GameWindow.h"
#include "RenderDevice/RenderDevice.h"
#include "RenderDevice/Vulkan/VulkanXRSession.h"
#include "RenderDevice/Vulkan/VulkanRenderDevice.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include "Video/VideoPlayer.h"
#include <chrono>
#include <set>
#include <map>

Engine* engine = nullptr;

Engine::Engine(GameLaunchInfo launchinfo) : LaunchInfo(launchinfo)
{
	engine = this;

	packages = std::make_unique<PackageManager>(LaunchInfo);

	std::srand((unsigned int)std::time(nullptr));

	auto transientpkg = packages->GetTransientPackage();
	auto enginepkg = packages->GetPackage("Engine");
	gameengine = UObject::Cast<UGameEngine>(transientpkg->NewObject("gameengine", enginepkg->GetClass("GameEngine"), ObjectFlags::Transient));
	audiodev = UObject::Cast<USurrealAudioDevice>(transientpkg->NewObject("audiodev", enginepkg->GetClass("SurrealAudioDevice"), ObjectFlags::Transient));
	renderdev = UObject::Cast<USurrealRenderDevice>(transientpkg->NewObject("renderdev", enginepkg->GetClass("SurrealRenderDevice"), ObjectFlags::Transient));
	netdev = UObject::Cast<USurrealNetworkDevice>(transientpkg->NewObject("netdev", enginepkg->GetClass("SurrealNetworkDevice"), ObjectFlags::Transient));
	client = UObject::Cast<USurrealClient>(transientpkg->NewObject("client", enginepkg->GetClass("SurrealClient"), ObjectFlags::Transient));
	viewport = UObject::Cast<UViewport>(transientpkg->NewObject("viewport", enginepkg->GetClass("Viewport"), ObjectFlags::Transient));
	canvas = UObject::Cast<UCanvas>(transientpkg->NewObject("canvas", enginepkg->GetClass("Canvas"), ObjectFlags::Transient));
	DefaultTexture = UObject::Cast<UTexture>(packages->GetPackage("Engine")->GetUObject("Texture", "DefaultTexture"));

	floatprop = GC::Alloc<UFloatProperty>(NameString(), nullptr, ObjectFlags::NoFlags);

	if (LaunchInfo.IsDeusEx())
	{
		auto extpkg = packages->GetPackage("Extension");
		deusExPackage = packages->GetPackage("DeusEx");
		dxgc = UObject::Cast<UGC>(transientpkg->NewObject("gc", extpkg->GetClass("GC"), ObjectFlags::Transient));
		dxgc->Canvas() = canvas;
		dxSaveInfo = UObject::Cast<UDXSaveInfo>(transientpkg->NewObject("DeusExSaveInfo", deusExPackage->GetClass("DeusExSaveInfo"), ObjectFlags::Transient));
		dxConMissionList = UObject::Cast<UConversationMissionList>(packages->GetPackage("DeusExConText")->GetUObject("ConversationMissionList", "ConMissionList"));
	}

	std::string consolestr = packages->GetIniValue("system", "Engine.Engine", "Console");
	std::string consolepkg = consolestr.substr(0, consolestr.find('.'));
	std::string consolecls = consolestr.substr(consolestr.find('.') + 1);
	console = UObject::Cast<UConsole>(transientpkg->NewObject("console", packages->GetPackage(consolepkg)->GetClass(consolecls), ObjectFlags::Transient));

	console->Viewport() = viewport;
	canvas->Viewport() = viewport;
	viewport->Console() = console;
}

Engine::~Engine()
{
	if (audiodev)
		audiodev->ShutdownDevice();

	Logger::Get()->SaveLogAsPlaintext((Directory::localAppData() / "SurrealEngine/SE-Log-LastRun.txt").string());

	engine = nullptr;
}

namespace
{
	// M3: OpenXR is right-handed, +X right, +Y up, -Z forward. UE1 is
	// left-handed, +X forward, +Y right, +Z up. This is a direct component
	// relabeling (not a matrix-conjugated basis change) applied consistently
	// to every vector below, so it composes correctly even though the
	// mapping itself is a reflection (det = -1).
	vec3 XRVecToUE1(float x, float y, float z)
	{
		return vec3(-z, x, y);
	}

	// UE1 units per real-world meter. UT99's default Pawn.BaseEyeHeight is
	// 64 - a normal standing eye height in inches - confirming the well
	// established "1 Unreal unit ~= 1 inch" convention. (DrawSceneStereo's
	// debug halfIPD comment assumed ~32 units/inch instead; that was an
	// unverified guess for a fake, deliberately-exaggerated debug offset,
	// not a real calibration, and reusing it here made real headset motion
	// translate into ~32x too much in-game movement - the root cause of a
	// "giant"/miniature-world scale illusion seen on first real-hardware
	// test.)
	constexpr float UUPerMeter = 1.0f / 0.0254f;

	// Coords::operator*(Coords, vec3) transforms a world point into the
	// Coords' local basis (world-to-local). Composing a headset pose onto
	// the game world needs the opposite direction: turn a vector already
	// expressed in `rot`'s local axes into world space. `rot`'s XAxis/
	// YAxis/ZAxis are themselves a local-to-world rotation (same convention
	// Coords::Rotation(Rotator) produces), so this is just their weighted sum.
	vec3 RotateLocalToWorld(const Coords& rot, const vec3& v)
	{
		return rot.XAxis * v.x + rot.YAxis * v.y + rot.ZAxis * v.z;
	}

	// M-A: the pure axis-remap step of turning a raw OpenXR pose (position
	// in meters, orientation quaternion, both in OpenXR's convention) into
	// UE1 world-axis vectors + a UU-scaled position - factored out of the
	// per-eye composition below (VR_IMPLEMENTATION_PLAN.md / M3) so M-A's
	// hand grip/aim poses can go through the identical math instead of a
	// re-implementation. Does NOT apply the yaw recenter or the
	// CameraLocation anchor - see ComposeXRPoseToWorld() for that.
	struct UEPoseAxes
	{
		vec3 fwd, right, up, pos;
	};

	UEPoseAxes XRPoseAxesToUE(float posX, float posY, float posZ, float qx, float qy, float qz, float qw)
	{
		quaternion q(qx, qy, qz, qw);
		vec3 fwdXR = q * vec3(0.0f, 0.0f, -1.0f);
		vec3 rightXR = q * vec3(1.0f, 0.0f, 0.0f);
		vec3 upXR = q * vec3(0.0f, 1.0f, 0.0f);
		UEPoseAxes axes;
		axes.fwd = XRVecToUE1(fwdXR.x, fwdXR.y, fwdXR.z);
		axes.right = XRVecToUE1(rightXR.x, rightXR.y, rightXR.z);
		axes.up = XRVecToUE1(upXR.x, upXR.y, upXR.z);
		axes.pos = XRVecToUE1(posX, posY, posZ) * UUPerMeter;
		return axes;
	}

	// M-A: applies the one-time yaw recenter (xrYawOffsetUE) and the
	// CameraLocation play-space anchor on top of XRPoseAxesToUE's raw
	// axes - the composition every rendered eye pose AND (M-A) every hand
	// pose goes through, so both consumers agree on where "world space"
	// is. This factors the math that used to be inlined per-eye below out
	// into a shared helper; it does not change it (verified byte-identical
	// against the pre-refactor inline eye-pose code - same operations,
	// same order, same operands).
	struct ComposedXRPose
	{
		vec3 location;
		Coords rotation; // XAxis=forward, YAxis=right, ZAxis=up (local-to-world); Origin=0
	};

	ComposedXRPose ComposeXRPoseToWorld(const UEPoseAxes& axes, float yawOffsetUE, const vec3& cameraLocation)
	{
		Coords recenter = Coords::YawRotation(yawOffsetUE);
		ComposedXRPose result;
		result.rotation.Origin = vec3(0.0f);
		result.rotation.XAxis = RotateLocalToWorld(recenter, axes.fwd);
		result.rotation.YAxis = RotateLocalToWorld(recenter, axes.right);
		result.rotation.ZAxis = RotateLocalToWorld(recenter, axes.up);
		result.location = cameraLocation + RotateLocalToWorld(recenter, axes.pos);
		return result;
	}
}

void Engine::Run()
{
	LogMessage("Game: " + LaunchInfo.gameName + " (Version: " + LaunchInfo.gameVersionString + ")");
	LoadEngineSettings();
	LogMessage("Loaded Engine settings");
	LoadKeybindings();
	LogMessage("Loaded key bindings");
	LogGamePackageSHA1Sums();

	// M2 step 4: when --vr is passed, the XrInstance + XrSystemId must exist
	// BEFORE the VkInstance/VkDevice are created (OpenWindow() below is what
	// triggers VulkanRenderDevice's constructor), because the runtime
	// mandates specific instance/device extensions and even a specific
	// VkPhysicalDevice. If the probe fails (no runtime, no HMD system, etc.)
	// we log why and fall back to completely normal flatscreen play - xrSession
	// stays null, which every other new code path in this file treats
	// identically to "not passed --vr" (see VulkanRenderDevice's constructor:
	// `useXR = xrSession && xrSession->IsAvailable()`).
	if (commandline && commandline->HasArg("", "--vr"))
	{
		xrSession = std::make_unique<VulkanXRSession>();
		if (xrSession->IsAvailable())
			LogMessage("--vr: OpenXR instance + HMD system OK");
		else
		{
			LogMessage("--vr: OpenXR unavailable (" + xrSession->LastError() + ") - falling back to flatscreen");
			xrSession.reset();
		}
	}

	OpenWindow();

	audiodev->InitDevice();
	render = std::make_unique<RenderSubsystem>(window->GetRenderDevice());

	// M2 step 2/8: now that the live VkInstance/VkPhysicalDevice/VkDevice
	// exist (created above, folding in OpenXR's required extensions and
	// physical device selection - see VulkanRenderDevice.cpp), create the
	// XrSession bound to them, then the per-eye swapchains. Only the Vulkan
	// backend is supported for VR (D3D11RenderDevice is never touched here).
	if (xrSession)
	{
		VulkanRenderDevice* vulkanDevice = dynamic_cast<VulkanRenderDevice*>(render->Device);
		if (vulkanDevice && vulkanDevice->Device)
		{
			bool ok = xrSession->CreateSession(
				(void*)vulkanDevice->Device->Instance->Instance,
				(void*)vulkanDevice->Device->PhysicalDevice.Device,
				(void*)vulkanDevice->Device->device,
				(uint32_t)vulkanDevice->Device->GraphicsFamily,
				0);
			if (ok)
				ok = xrSession->CreateSwapchains();

			if (ok)
			{
				xrSessionActive = true;
				LogMessage("--vr: XR session + swapchains created (" + std::to_string(xrSession->GetSwapchainWidth()) + "x" + std::to_string(xrSession->GetSwapchainHeight()) + " per eye)");

				// M3: controller input. Non-fatal if it fails - the session
				// still runs, just with no controller input (LastError()
				// already logged the reason inside CreateActions()).
				if (!xrSession->CreateActions())
					LogMessage("--vr: controller action setup failed (" + xrSession->LastError() + ") - continuing without controller input");
			}
			else
			{
				LogMessage("--vr: session/swapchain creation failed (" + xrSession->LastError() + ") - continuing flatscreen-only (VkInstance/VkDevice were already created against the XR runtime's requirements, so this is not undone, but no XR frames will be submitted)");
			}
		}
		else
		{
			LogMessage("--vr: render device is not Vulkan - VR requires the Vulkan backend - continuing flatscreen-only");
		}
	}

	if (commandline && commandline->HasArg("", "--debugfixedsize"))
	{
		// Non-interactive diagnostic: proves the scene render target can be
		// pinned to a size independent of the OS window (needed for VR,
		// where the OpenXR swapchain resolution has nothing to do with the
		// desktop mirror window size), without needing a real OpenXR
		// session. See VR_IMPLEMENTATION_PLAN.md M2 step 8.
		std::string sizeArg = commandline->GetArg("", "--debugfixedsize");
		size_t xpos = sizeArg.find('x');
		if (xpos != std::string::npos)
		{
			int w = std::atoi(sizeArg.substr(0, xpos).c_str());
			int h = std::atoi(sizeArg.substr(xpos + 1).c_str());
			if (w > 0 && h > 0)
				render->Device->SetFixedRenderSize(w, h);
		}
	}

	// M-B: --debugvrhands - synthesizes two fake, slowly-orbiting hand poses
	// with NO XR session/headset required at all (see Engine.h's doc comment
	// on debugVRHandsEnabled/UpdateDebugVRHands and
	// Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's M-B section). This is what
	// makes M-B screenshot-verifiable on a machine with no HMD connected -
	// works standalone or together with --vr (a real session's per-frame
	// hand composition, when it runs, simply overwrites xrHands[] again
	// afterward - see the XR frame block below).
	if (commandline && commandline->HasArg("", "--debugvrhands"))
	{
		debugVRHandsEnabled = true;
		LogMessage("--debugvrhands: synthesizing fake orbiting hand poses (no XR session required)");
	}

	// M-C: --debugvrfire - see Engine.h's doc comment on debugVRFireEnabled.
	if (commandline && commandline->HasArg("", "--debugvrfire"))
	{
		debugVRFireEnabled = true;
		LogMessage("--debugvrfire: synthesizing a timed fire press (no XR session required)");
	}

	// M-B: VM interception seam install - see VM/Frame.h's
	// Frame::InterceptCall doc comment. Only installed while VR is actually
	// active (a real running session OR --debugvrhands), so a plain
	// flatscreen run (no --vr, no --debugvrhands) leaves Frame::InterceptCall
	// null and Frame::Call byte-identical to its pre-M-B behavior - the
	// no-regression requirement from Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's
	// M-B section.
	if (xrSessionActive || debugVRHandsEnabled || debugVRFireEnabled)
	{
		Frame::InterceptCall = [this](UObject* instance, UFunction* func, Array<ExpressionValue>& args, ExpressionValue& result)
		{
			return HandleFrameCallIntercept(instance, func, args, result);
		};
		// M-C: pairs with HandleFrameCallIntercept above via the new
		// Frame::InterceptCallPost seam (VM/Frame.h) - see
		// HandleFrameCallInterceptPost's doc comment in Engine.h. Installed
		// under the exact same condition as InterceptCall so a plain
		// flatscreen run leaves both null and Frame::Call byte-identical.
		Frame::InterceptCallPost = [this](UObject* instance, UFunction* func, ExpressionValue& result)
		{
			HandleFrameCallInterceptPost(instance, func, result);
		};
		LogMessage("VR: weapon RenderOverlays/TraceFire/CalcDrawOffset interception hooks installed (M-B/M-C)");
	}

	if (engine->LaunchInfo.ue1Version > 219 && !client->StartupFullscreen)
		viewport->bWindowsMouseAvailable() = true;

	window->LockCursor();

	if (packages->IsKlingonHonorGuard())
	{
		PlayAVI({ "playavi", "INTRO.AVI", "N" });
	}

	if (!LaunchInfo.noEntryMap)
		LoadEntryMap();

	if (LaunchInfo.url.empty())
		LoadMap(GetDefaultURL(packages->GetIniValue("system", "URL", "LocalMap")));
	else
		LoadMap(UnrealURL(GetDefaultURL(packages->GetIniValue("system", "URL", "LocalMap")), LaunchInfo.url));

	LoginPlayer();

	auto objprop = GC::Alloc<UObjectProperty>(NameString(), nullptr, ObjectFlags::NoFlags);
	auto vecprop = GC::Alloc<UStructProperty>(NameString(), nullptr, ObjectFlags::NoFlags);
	auto rotprop = GC::Alloc<UStructProperty>(NameString(), nullptr, ObjectFlags::NoFlags);

	bool firstCall = true;
	while (!quit)
	{
		// Main game loop should consist of these 4 steps:
		// Tick everything
		// Render the scene
		// Check if there is a request to save the game: save the game if that's the case
		// Check if there is a new map to load (next level, saved game etc.): load it if that's the case

		// Tick everything
		float realTimeElapsed = CalcTimeElapsed();
		float entryLevelElapsed = EntryLevel ? realTimeElapsed * clamp(EntryLevelInfo->TimeDilation(), 0.0025f, 25.0f) : 0.0f;
		float levelElapsed = realTimeElapsed * clamp(LevelInfo->TimeDilation(), 0.0025f, 25.0f);

		TotalTime += realTimeElapsed;

		if (EntryLevel)
			EntryLevelInfo->TimeSeconds() += entryLevelElapsed;
		LevelInfo->TimeSeconds() += levelElapsed;
		Logger::Get()->SetTimeSeconds(LevelInfo->TimeSeconds());

		// Update the time fields
		std::time_t now = std::time(nullptr);
		std::tm* timedesc = std::localtime(&now);

		LevelInfo->Year() = timedesc->tm_year;
		LevelInfo->Month() = timedesc->tm_mon;
		LevelInfo->Day() = timedesc->tm_mday;
		LevelInfo->DayOfWeek() = timedesc->tm_wday;
		LevelInfo->Hour() = timedesc->tm_hour;
		LevelInfo->Minute() = timedesc->tm_min;
		LevelInfo->Second() = timedesc->tm_sec;
		LevelInfo->Millisecond() = 0; // No timedesc equivalent for LevelInfo->Millisecond()

		UpdateInput(realTimeElapsed);

		// M3: controller input. Runs after UpdateInput() so, when both a
		// keyboard/mouse binding and a VR controller drive the same pawn
		// property in the same tick, the controller wins - VR play is the
		// point once a session is active. No-op (early-returns) when no XR
		// session is running.
		UpdateVRControllerInput(realTimeElapsed);

		SetPause(!LevelInfo->Pauser().empty());

		// Do NOT pause this Tick event otherwise some messages will stay on screen forever.
		CallEvent(console, EventName::Tick, { ExpressionValue::FloatValue(levelElapsed) });

		// To do: set these to true if the frame rate is too low
		if (LaunchInfo.ue1Version >= 436)
		{
			LevelInfo->bDropDetail() = false;
			LevelInfo->bAggressiveLOD() = false;
		}

		if (EntryLevel)
			EntryLevel->Tick(entryLevelElapsed, m_GamePaused);
		Level->Tick(levelElapsed, m_GamePaused);

		if (dxRootWindow)
			dxRootWindow->Tick(levelElapsed); // Should this maybe be realTimeElapsed?

		// To do: improve CallEvent so parameter passing isn't this painful
		UFunction* funcPlayerCalcView = viewport->Actor() ? FindEventFunction(viewport->Actor(), "PlayerCalcView") : nullptr;
		if (funcPlayerCalcView)
		{
			vecprop->Struct = UObject::Cast<UStructProperty>(funcPlayerCalcView->Properties[1])->Struct;
			rotprop->Struct = UObject::Cast<UStructProperty>(funcPlayerCalcView->Properties[2])->Struct;
			CameraActor = viewport->Actor();
			CameraLocation = viewport->Actor()->Location();
			CameraRotation = viewport->Actor()->Rotation();
			CameraFovAngle = viewport->Actor()->FovAngle();
			CallEvent(viewport->Actor(), EventName::PlayerCalcView, {
				ExpressionValue::Variable(&CameraActor, objprop),
				ExpressionValue::Variable(&CameraLocation, vecprop),
				ExpressionValue::Variable(&CameraRotation, rotprop)
				});
		}

		// M-B: fake orbiting hand poses for --debugvrhands - run right
		// after CameraLocation/CameraRotation are refreshed above (this
		// frame's anchor, same one the real XR hand composition below
		// uses) and unconditionally of xrSession (a no-op when
		// debugVRHandsEnabled is false) so it also works without --vr at
		// all, per Engine.h's doc comment on UpdateDebugVRHands.
		UpdateDebugVRHands(realTimeElapsed);

		// M-C: --debugvrfire's synthesized trigger squeeze - see Engine.h's
		// doc comment on debugVRFireEnabled/UpdateDebugVRFire. Runs
		// unconditionally of xrSession (no-op when debugVRFireEnabled is
		// false), same pattern as UpdateDebugVRHands just above.
		UpdateDebugVRFire(realTimeElapsed);

		UpdateAudio();

		viewport->SetViewportRect(0, 0, engine->window->GetPixelWidth(), engine->window->GetPixelHeight());

		// M2 step 8/9 + M3: real OpenXR frame loop. The desktop window keeps
		// rendering/presenting every frame regardless (DrawGame() below is
		// unconditional) so PrintWindow-based screenshots of the window stay
		// meaningful while a VR session is active (step 9's "windowed
		// mirror") - VulkanRenderDevice::DrawPresentTexture() additionally
		// blits that same composited image into whichever eye swapchain
		// images we acquired this tick, see VulkanRenderDevice.cpp.
		//
		// xrLocateViews's per-eye pose+fov is folded into the rendered view
		// matrix below via RenderSubsystem::SetPendingVREyes/DrawSceneVR -
		// see VR_IMPLEMENTATION_PLAN.md's M3 section for the axis/scale
		// conversion and one-time yaw-recenter this composition applies.
		if (xrSession && xrSessionActive)
		{
			bool keepGoing = xrSession->PollEvents();
			if (!keepGoing)
			{
				LogMessage("--vr: XR session exiting - destroying XR session/swapchains, continuing flatscreen-only");
				xrSession->DestroySwapchains();
				xrSession->DestroySession();
				xrSessionActive = false;
			}
		}

		bool xrFrameActive = false;
		if (xrSession && xrSessionActive && xrSession->IsSessionRunning())
		{
			bool shouldRender = false;
			bool waitBeginOk = xrSession->WaitAndBeginFrame(shouldRender);
			if (waitBeginOk)
			{
				// xrBeginFrame succeeded, so per spec xrEndFrame MUST be
				// called to balance the frame loop, whether or not
				// shouldRender is true (in which case it's called with no
				// layers below).
				VREyePose eyes[2] = {};
				void* leftImage = nullptr;
				void* rightImage = nullptr;

				if (shouldRender)
				{
					xrSession->LocateViews(eyes); // logged internally

					leftImage = xrSession->AcquireSwapchainImage(0);
					rightImage = xrSession->AcquireSwapchainImage(1);
					if (leftImage && rightImage)
					{
						VulkanRenderDevice* vulkanDevice = dynamic_cast<VulkanRenderDevice*>(render->Device);
						if (vulkanDevice)
							vulkanDevice->SetPendingXRTargets(leftImage, rightImage, xrSession->GetSwapchainWidth(), xrSession->GetSwapchainHeight());
					}

					// M3: compose the real per-eye OpenXR pose onto the game
					// world, using CameraLocation as the play-space anchor
					// (so keyboard/joystick locomotion still moves the VR
					// view - only the recentered head offset rides on top).
					vec3 eyeLocationUE[2];
					Coords eyeRotationUE[2];
					float eyeFovUE[2][4];
					for (int eye = 0; eye < 2; eye++)
					{
						const VREyePose& pose = eyes[eye];

						// M-A refactor: this used to inline the quaternion ->
						// UE-axis decomposition here; it's now
						// XRPoseAxesToUE(), shared with the M-A hand pose
						// composition below - same operations, same order, no
						// behavior change (fwdUE/rightUE/upUE/posUE are now
						// axes.fwd/right/up/pos).
						UEPoseAxes axes = XRPoseAxesToUE(pose.posX, pose.posY, pose.posZ, pose.qx, pose.qy, pose.qz, pose.qw);

						if (eye == 0)
						{
							float rawYaw = std::atan2(-axes.fwd.y, axes.fwd.x);
							if (!xrPoseRecentered)
							{
								// 2026-07-21: CameraRotation is a Rotator - UE1's native
							// GetAxes(rotator) (see Native/NObject.cpp GetAxes ->
							// Coords::Rotation) composes with the OPPOSITE sign from
							// Coords::YawRotation (verified: for a pure-yaw Rotator,
							// Coords::Rotation's XAxis works out to (cos yaw, +sin yaw,
							// 0), while Coords::YawRotation's is (cos yaw, -sin yaw,
							// 0)). rawYaw/xrYawOffsetUE/xrHeadYawUE are all in the
							// Coords::YawRotation (VR) convention, so converting a
							// Rotator-space yaw into that convention requires negating
							// it - without this the initial recenter anchors the view
							// to the mirror of the pawn's actual spawn facing.
							xrYawOffsetUE = -CameraRotation.YawRadians() - rawYaw;
								xrPoseRecentered = true;
							}
							// M3: current head yaw (recenter offset + live
							// tracked yaw), UE1 radians. Consumed one frame
							// later by UpdateVRControllerInput() to keep the
							// pawn's body Rotation.Yaw following wherever the
							// player is actually looking - see that function's
							// doc comment for why this is necessary (movement
							// direction and the weapon viewmodel are both
							// driven off Pawn.Rotation, not off the render-only
							// head pose composed below).
							xrHeadYawUE = xrYawOffsetUE + rawYaw;

							// 2026-07-21: live head pitch, already in
							// Rotator/game convention (see xrHeadPitchUE's doc
							// comment in Engine.h) - no offset/recenter needed,
							// consumed directly by UpdateVRControllerInput() to
							// drive Pawn.ViewRotation.Pitch.
							float horizLenUE = std::sqrt(axes.fwd.x * axes.fwd.x + axes.fwd.y * axes.fwd.y);
							xrHeadPitchUE = std::atan2(axes.fwd.z, horizLenUE);
						}

						ComposedXRPose composed = ComposeXRPoseToWorld(axes, xrYawOffsetUE, CameraLocation);
						eyeRotationUE[eye] = composed.rotation;
						eyeLocationUE[eye] = composed.location;

						eyeFovUE[eye][0] = pose.angleLeft;
						eyeFovUE[eye][1] = pose.angleRight;
						eyeFovUE[eye][2] = pose.angleUp;
						eyeFovUE[eye][3] = pose.angleDown;
					}
					render->SetPendingVREyes(eyeLocationUE, eyeRotationUE, eyeFovUE);

					// M-A: per-hand grip/aim world-space pose - same
					// predicted-display-time source and composition math as
					// the eye poses just above, computed once per XR frame
					// here and consumed via MainHand()/OffHand() everywhere
					// else (M-B onward) so a later handedness swap (M-F)
					// only touches the `mainHand` index, never this
					// composition. A hand's `valid` (and its
					// gripPos/gripCoords/aimRotator) is simply left at its
					// last known value whenever the runtime doesn't report a
					// tracked pose this tick (controller off/out of view/
					// session unfocused, or pose actions failed to create) -
					// see Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's M-A section.
					VRHandPose gripPoses[2];
					VRHandPose aimPoses[2];
					bool gotHandPoses = xrSession->LocateHandPoses(gripPoses, aimPoses);
					for (int hand = 0; hand < 2; hand++)
					{
						VRHandState& handState = xrHands[hand];
						handState.valid = gotHandPoses && gripPoses[hand].valid && aimPoses[hand].valid;
						if (!handState.valid)
							continue;

						const VRHandPose& grip = gripPoses[hand];
						UEPoseAxes gripAxes = XRPoseAxesToUE(grip.posX, grip.posY, grip.posZ, grip.qx, grip.qy, grip.qz, grip.qw);
						ComposedXRPose composedGrip = ComposeXRPoseToWorld(gripAxes, xrYawOffsetUE, CameraLocation);
						handState.gripPos = composedGrip.location;
						handState.gripCoords = composedGrip.rotation;

						// handAimRotator: aim-pose forward converted to a
						// Rotator via the engine's existing FromVector helper
						// (Math/rotator.h) - algebraically the same sign
						// convention already verified for the head
						// (yaw = atan2(fwd.y, fwd.x), pitch =
						// atan2(fwd.z, sqrt(fwd.x^2+fwd.y^2)), scaled by
						// 32768/pi) once the composed forward is in WORLD
						// space (post yaw-recenter) rather than the raw,
						// pre-recenter axes the head math above works with.
						const VRHandPose& aim = aimPoses[hand];
						UEPoseAxes aimAxes = XRPoseAxesToUE(aim.posX, aim.posY, aim.posZ, aim.qx, aim.qy, aim.qz, aim.qw);
						ComposedXRPose composedAim = ComposeXRPoseToWorld(aimAxes, xrYawOffsetUE, CameraLocation);
						handState.aimRotator = Rotator::FromVector(composedAim.rotation.XAxis);
					}
				}

				render->DrawGame(levelElapsed);
				xrFrameActive = true;

				if (leftImage)
					xrSession->ReleaseSwapchainImage(0);
				if (rightImage)
					xrSession->ReleaseSwapchainImage(1);

				xrSession->EndFrame(shouldRender && leftImage && rightImage, eyes);
			}
			else
			{
				// Hard failure from xrWaitFrame/xrBeginFrame itself - do NOT
				// call xrEndFrame (nothing to balance), just fall back to a
				// normal flatscreen-only DrawGame() this tick.
				LogMessage("--vr: WaitAndBeginFrame failed (" + xrSession->LastError() + ")");
			}
		}

		if (!xrFrameActive)
			render->DrawGame(levelElapsed);

		// Save the game if there is a request for it
		if (SaveGameInfo.SaveGameSlot != DONT_SAVE_GAME)
		{
			SaveGameToSlot(SaveGameInfo.SaveGameSlot, SaveGameInfo.SaveGameDescription);

			SaveGameInfo.SaveGameSlot = DONT_SAVE_GAME;
			SaveGameInfo.SaveGameDescription.clear();
		}

		// Check if there is a new map to load
		if (!LevelInfo->NextURL().empty())
		{
			LevelInfo->NextSwitchCountdown() -= levelElapsed;
			if (LevelInfo->NextSwitchCountdown() <= 0.0f)
			{
				if (UnrealURL(LevelInfo->NextURL()).HasOption("restart"))
				{
					LoadMap(LevelInfo->URL, Level->TravelInfo);
					LoginPlayer();
				}
				else if (LevelInfo->bNextItems())
				{
					LoadMap(UnrealURL(LevelInfo->URL, LevelInfo->NextURL()), CreateTravelInfo(true));
					LoginPlayer();
				}
				else
				{
					LoadMap(UnrealURL(LevelInfo->URL, LevelInfo->NextURL()), {});
					LoginPlayer();
				}
			}
		}

		if (ClientTravelInfo.URL.HasOption("restart"))
		{
			LoadMap(LevelInfo->URL, Level->TravelInfo);
			LoginPlayer();
		}

		if (ClientTravelInfo.URL.HasOption("load"))
		{
			UnrealURL url(ClientTravelInfo.URL);
			LoadFromSaveFile(url);
			LoginPlayer();
		}

		if (!ClientTravelInfo.URL.Map.empty())
		{
			// To do: need to do something about that travel type and transfering of items

			UnrealURL url(ClientTravelInfo.URL);
			LogMessage("Client travel to " + url.ToString());
			LoadMap(url, CreateTravelInfo(ClientTravelInfo.TransferItems));
			LoginPlayer();
		}
	}

	LogMessage("Shutting down...");
	window->UnlockCursor();

	if (xrSessionActive)
	{
		xrSession->DestroySwapchains();
		xrSession->DestroySession();
		xrSessionActive = false;
	}

	LogMessage("Saving configurations...");
	if (packages->MissingSESystemIni())
	{
		// Add the missing Subsystem entries
		client->SaveConfig();
		audiodev->SaveConfig();
		renderdev->SaveConfig();
	}
	packages->SetIniValue("System", "Engine.SurrealWindowSystem", "WindowSystem", windowingSystemName);
	packages->SaveAllIniFiles();

	LogMessage("Closing window...");
	CloseWindow();
}

void Engine::PlayAVI(const Array<std::string>& args)
{
	if (args.size() < 3)
		return;

	// What did KHG request?
	std::string buildup, breakdown, video;
	if (args.size() > 4 && args[2] == "C")
	{
		buildup = args[1];
		video = args[3];
		breakdown = "breakdn.avi";
	}
	else if (args.size() > 3 && args[2] == "Y")
	{
		buildup = "buildup.avi";
		video = args[1];
		breakdown = "breakdn.avi";
	}
	else
	{
		video = args[1];
	}

	playingAvi = true;
	skipAvi = false;

	try
	{
		// Load the videos

		std::unique_ptr<VideoPlayer> buildupPlayer, videoPlayer, breakdownPlayer;
		if (!buildup.empty())
			buildupPlayer = VideoPlayer::Create(packages->GetVideoFilename(buildup));
		if (!video.empty())
			videoPlayer = VideoPlayer::Create(packages->GetVideoFilename(video));
		if (!breakdown.empty())
			breakdownPlayer = VideoPlayer::Create(packages->GetVideoFilename(breakdown));

		CalcTimeElapsed(); // Reset so load time doesn't affect playback

		UnrealMipmap* background = nullptr;
		if (buildupPlayer)
		{
			background = PlayVideo(buildupPlayer.get(), nullptr);
			if (!background)
			{
				CalcTimeElapsed();
				return;
			}

			// Cut a hole in the background image
			uint32_t* pixels = (uint32_t*)background->Data.data();
			int w = background->Width;
			int h = background->Height;

			ivec2 v0 = { 232, 125 };
			ivec2 v1 = { 161, 246 };
			ivec2 v2 = { 406, 125 };
			ivec2 v3 = { 475, 246 };
			ivec2 v4 = { 258, 125 };
			ivec2 v5 = { 270, 144 };
			ivec2 v6 = { 380, 125 };
			ivec2 v7 = { 368, 144 };
			ivec2 v8 = { 210, 327 };
			ivec2 v9 = { 427, 327 };

			for (int y = 126; y < 144; y++)
			{
				int x0 = (int)(v0.x + 0.5f + (y - v0.y + 0.5f) * (v1.x - v0.x) / (v1.y - v0.y));
				int x1 = (int)(v4.x + 0.5f + (y - v4.y + 0.5f) * (v5.x - v4.x) / (v5.y - v4.y));
				int x2 = (int)(v6.x + 0.5f + (y - v6.y + 0.5f) * (v7.x - v6.x) / (v7.y - v6.y));
				int x3 = (int)(v2.x + 0.5f + (y - v2.y + 0.5f) * (v3.x - v2.x) / (v3.y - v2.y));

				pixels[x0 + y * w] = 0x80000000;
				pixels[x1 - 1 + y * w] = 0x80000000;
				for (int x = x0 + 1; x < x1 - 1; x++)
					pixels[x + y * w] = 0;

				pixels[x2 + y * w] = 0x80000000;
				pixels[x3 - 1 + y * w] = 0x80000000;
				for (int x = x2 + 1; x < x3 - 1; x++)
					pixels[x + y * w] = 0;
			}

			for (int y = 144; y < 246; y++)
			{
				int x0 = (int)(v0.x + 0.5f + (y - v0.y + 0.5f) * (v1.x - v0.x) / (v1.y - v0.y));
				int x1 = (int)(v2.x + 0.5f + (y - v2.y + 0.5f) * (v3.x - v2.x) / (v3.y - v2.y));
				pixels[x0 + y * w] = 0x80000000;
				pixels[x1 - 1 + y * w] = 0x80000000;
				for (int x = x0 + 1; x < x1 - 1; x++)
					pixels[x + y * w] = 0;
			}

			for (int y = 246; y < 325; y++)
			{
				int x0 = (int)(v1.x + 0.5f + (y - v1.y + 0.5f) * (v8.x - v1.x) / (v8.y - v1.y));
				int x1 = (int)(v3.x + 0.5f + (y - v3.y + 0.5f) * (v9.x - v3.x) / (v9.y - v3.y));
				pixels[x0 + y * w] = 0x80000000;
				pixels[x1 - 1 + y * w] = 0x80000000;
				for (int x = x0 + 1; x < x1 - 1; x++)
					pixels[x + y * w] = 0;
			}
		}

		if (videoPlayer)
		{
			if (!PlayVideo(videoPlayer.get(), background))
			{
				playingAvi = false;
				CalcTimeElapsed();
				return;
			}
		}

		if (breakdownPlayer)
		{
			PlayVideo(breakdownPlayer.get(), nullptr);
		}
	}
	catch (const std::exception& e)
	{
		LogMessage("Error playing " + video + ": " + e.what());
	}

	playingAvi = false;
	CalcTimeElapsed(); // Reset so game isn't affected
}

UnrealMipmap* Engine::PlayVideo(VideoPlayer* video, UnrealMipmap* background)
{
	UnrealMipmap* frame = nullptr;

	FTextureInfo texinfo[2];
	texinfo[0].CacheID = 0xffffffff'ffffffffULL;
	texinfo[0].Format = TextureFormat::BGRA8;
	texinfo[0].NumMips = 1;

	if (background)
	{
		texinfo[1].CacheID = 0xffffffff'fffffffeULL;
		texinfo[1].Format = TextureFormat::BGRA8;
		texinfo[1].NumMips = 1;
		texinfo[1].Mips = background;
		texinfo[1].USize = background->Width;
		texinfo[1].VSize = background->Height;
		texinfo[1].bRealtimeChanged = true;
	}

	audiodev->SetViewport(nullptr);
	audiodev->GetDevice()->PlayMusic(video->GetAudio());

	float timestamp = 0.0f;
	int curframe = -1;
	while (!quit && !skipAvi)
	{
		timestamp += CalcTimeElapsed();

		bool done = false;
		while (curframe < video->GetFrameIndexForTime(timestamp))
		{
			while (true)
			{
				UnrealMipmap* nextframe = video->NextVideoFrame();
				if (nextframe)
				{
					frame = nextframe;
					curframe++;
					texinfo[0].bRealtimeChanged = true;
					break;
				}
				if (!video->Decode())
				{
					done = true;
					break;
				}
			}
			if (done)
				break;
		}
		if (done)
			break;

		audiodev->GetDevice()->Update();
		GameWindow::ProcessEvents();

		if (frame)
		{
			texinfo[0].Mips = frame;
			texinfo[0].USize = frame->Width;
			texinfo[0].VSize = frame->Height;

			viewport->SetViewportRect(0, 0, engine->window->GetPixelWidth(), engine->window->GetPixelHeight());
			render->DrawVideoFrame(&texinfo[0], background ? &texinfo[1] : nullptr);
		}
	}

	audiodev->GetDevice()->PlayMusic(nullptr);

	if (quit || skipAvi)
		return nullptr;

	return frame;
}

UConversationList* Engine::GetDeusExMission()
{
	if (!dxConMissionList || !DeusExLevelInfo)
		return nullptr;

	int missionNumber = DeusExLevelInfo->MissionNumber();
	for (UConItem* item = dxConMissionList->missions(); item; item = item->Next())
	{
		auto mission = UObject::Cast<UConversationList>(item->ConObject());
		if (mission->missionNumber() == missionNumber)
		{
			return mission;
		}
	}
	return nullptr;
}

void Engine::UpdateAudio()
{
	mat4 translate = mat4::translate(vec3(0.0f) - CameraLocation);
	mat4 listener = Coords::ViewToAudioDev().ToMatrix() * Coords::Rotation(CameraRotation).ToMatrix() * translate;

	audiodev->SetViewport(viewport);
	audiodev->Update(listener);
}

void Engine::ClientTravel(const std::string& newURL, ETravelType travelType, bool transferItems)
{
	UnrealURL url(newURL);

	// If the URL doesn't contain the player info, add them here.
	// As they have to persist somehow
	for (std::string optionKey : { "Name", "Class", "team", "skin", "Face", "Voice", "OverrideClass" })
	{
		if (engine->LaunchInfo.ue1Version > 219)
		{
			if (url.HasOption(optionKey))
				engine->packages->SetIniValue("User", "DefaultPlayer", optionKey, url.GetOption(optionKey));
			else
				url.AddOrReplaceOption(optionKey + "=" + packages->GetIniValue("user", "DefaultPlayer", optionKey));
		}
		else
		{
			if (url.HasOption(optionKey))
				engine->packages->SetIniValue("System", "URL", optionKey, url.GetOption(optionKey));
			else
				url.AddOrReplaceOption(optionKey + "=" + packages->GetIniValue("System", "URL", optionKey));
		}
	}

	if (travelType == ETravelType::TRAVEL_Absolute)
		ClientTravelInfo.URL = url;
	else if (travelType == ETravelType::TRAVEL_Partial)
	{
		auto name = ClientTravelInfo.URL.GetOption("name");
		ClientTravelInfo.URL = url;
		ClientTravelInfo.URL.AddOrReplaceOption("name=" + name);
	}
	else if (travelType == ETravelType::TRAVEL_Relative)
		ClientTravelInfo.URL = UnrealURL(ClientTravelInfo.URL, url);
	ClientTravelInfo.TravelType = travelType;
	ClientTravelInfo.TransferItems = transferItems;
}

UnrealURL Engine::GetDefaultURL(const std::string& map)
{
	UnrealURL url;
	std::string teleporterTag = "";
	std::string finalMapName = map;

	size_t tagPos = map.find('#');

	if (tagPos != std::string::npos)
	{
		teleporterTag = map.substr(tagPos + 1);
		finalMapName = map.substr(0, tagPos);
	}
	if (map.find("." + packages->GetMapExtension()) == std::string::npos)
		url.Map = finalMapName + "." + packages->GetMapExtension();
	else
		url.Map = finalMapName;

	if (!teleporterTag.empty())
		url.Portal = teleporterTag;
	for (std::string optionKey : { "Name", "Class", "team", "skin", "Face", "Voice", "OverrideClass" })
	{
		url.Options.push_back(optionKey + "=" + packages->GetIniValue("user", "DefaultPlayer", optionKey));
	}
	return url;
}

void Engine::LoadEntryMap()
{
	// The entry map is the map you see in the game when no other map is playing. For example when disconnected from a server. It is always loaded and running.
	const auto entryMapName = packages->GetIniValue("System", "URL", "EntryMap", "Entry");
	LoadMap(GetDefaultURL(entryMapName));
	EntryLevelInfo = LevelInfo;
	EntryLevel = Level;
	EntryLevelPackage = std::move(LevelPackage);
	LevelInfo = nullptr;
	Level = nullptr;
	viewport->Actor() = nullptr;
}

void Engine::UnloadMap()
{
	if (!LevelPackage)
		return;

	LevelInfo = nullptr;
	if (packages->IsDeusEx())
		DeusExLevelInfo = nullptr;
	Level = nullptr;
	viewport->Actor() = nullptr;
	dxRootWindow = nullptr;
	packages->UnloadPackage(std::move(LevelPackage));
}

void Engine::LoadMap(const UnrealURL& url, const std::map<std::string, std::string>& travelInfo)
{
	ClientTravelInfo.URL.Clear();

	if (Level)
		CallEvent(console, EventName::NotifyLevelChange);

	if (url.HasOption("entry")) // Not sure what the purpose of this kind of travel is - do nothing for now.
		return;

	audiodev->StopSounds();
	UnloadMap();

	// Load map objects

	LevelPackage = packages->LoadMap(url.Map);

	GetLevelInfoObject();

	LevelInfo->ComputerName() = "MyComputer";
	LevelInfo->HubStackLevel() = 0; // To do: handle level hubs
	LevelInfo->EngineVersion() = LaunchInfo.gameVersionString + " SE";
	if (LaunchInfo.ue1Version > 219)
		LevelInfo->MinNetVersion() = LaunchInfo.gameVersionString + " SE";
	LevelInfo->bHighDetailMode() = true;
	LevelInfo->NetMode() = 0; // NM_StandAlone
	LevelInfo->DefaultTexture() = engine->DefaultTexture;

	LevelInfo->URL = url;

	GetLevelObject();

	Level->TravelInfo = travelInfo; // Initially used travel info for level restart

	// Remove the actors meant for the editor (to do: should we do this at the package manager level?)
	for (UActor*& actor : Level->Actors)
	{
		if (actor && AllFlags(actor->Flags, ObjectFlags::NotForServer))
		{
			actor->bDeleteMe() = true;
			actor = nullptr;
		}
	}

	LinkActorsToLevel();

	// Find the game info class
	UClass* gameInfoClass = packages->FindClass(LevelInfo->URL.GetOption("game"));
	if (!gameInfoClass)
		gameInfoClass = LevelInfo->DefaultGameType();
	if (!gameInfoClass)
		gameInfoClass = packages->FindClass(packages->GetIniValue("system", "Engine.Engine", "DefaultGame"));
	if (!gameInfoClass)
		gameInfoClass = packages->FindClass("Botpack.DeathMatchPlus");
	if (!gameInfoClass)
		Exception::Throw("Could not find any gameinfo class!");

	// Spawn GameInfo actor
	GameInfo = UObject::Cast<UGameInfo>(LevelPackage->NewObject("gameinfo", gameInfoClass, ObjectFlags::NoFlags));
	GameInfo->XLevel() = Level;
	GameInfo->Level() = LevelInfo;
	Level->Collision.AddToCollision(GameInfo);
	Level->Light.AddLight(GameInfo);
	GameInfo->Tag() = gameInfoClass->Name;
	GameInfo->bTicked() = false;
	GameInfo->InitActorZone();
	GameInfo->Index = (int)Level->Actors.size();
	Level->Actors.push_back(GameInfo);

	LevelInfo->Game() = GameInfo;

	if (!LevelInfo->bBegunPlay())
	{
		LevelInfo->TimeSeconds() = 0.0f;
		LevelInfo->bBegunPlay() = true;

		std::string options = url.GetOptions();

		auto stringProp = GC::Alloc<UStringProperty>("", nullptr, ObjectFlags::NoFlags);
		std::string error;

		// Only call PreBegin/Begin/PostBegin/SetInitialState for loaded objects. Spawned objects are added at the end of the Actors array.
		size_t loadActorCount = Level->Actors.size();

		LevelInfo->bStartup() = true;
		CallEvent(GameInfo, EventName::InitGame, { ExpressionValue::StringValue(options), ExpressionValue::Variable(&error, stringProp) });
		if (!error.empty())
			Exception::Throw("InitGame failed: " + error);

		// Note: the events may spawn actors. We can't use iterators here.
		for (size_t i = 0; i < loadActorCount; i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], EventName::PreBeginPlay); }
		for (size_t i = 0; i < loadActorCount; i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], EventName::BeginPlay); }
		for (size_t i = 0; i < loadActorCount; i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], EventName::PostBeginPlay); }
		for (size_t i = 0; i < loadActorCount; i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], EventName::SetInitialState); }

		if (engine->LaunchInfo.IsDeusEx())
		{
			for (size_t i = 0; i < loadActorCount; i++) { if (Level->Actors[i]) CallEvent(Level->Actors[i], "PostPostBeginPlay"); }
		}

		for (size_t i = 0; i < loadActorCount; i++) { if (Level->Actors[i]) Level->Actors[i]->InitBase(); }
		LevelInfo->bStartup() = false;
	}

	if (LevelInfo->Game())
		CallEvent(LevelInfo->Game(), "DetailChange", {});
}

void Engine::LoadFromSaveFile(const UnrealURL& url)
{
	ClientTravelInfo.URL.Clear();

	if (Level)
		CallEvent(console, EventName::NotifyLevelChange);

	if (url.HasOption("entry")) // Not sure what the purpose of this kind of travel is - do nothing for now.
		return;

	Package* savefilePackage = nullptr;

	if (url.HasOption("load"))
	{
		uint32_t slotNum = Convert::to_uint32(url.GetOption("load"));
		savefilePackage = packages->LoadSaveSlot(slotNum);
	}

	if (!savefilePackage)
		return;

	audiodev->StopSounds();
	UnloadMap();

	LevelPackage = savefilePackage;

	GetLevelInfoObject();

	/*
	LevelInfo->ComputerName() = "MyComputer";
	LevelInfo->HubStackLevel() = 0; // To do: handle level hubs
	*/
	LevelInfo->EngineVersion() = LaunchInfo.gameVersionString + " SE";
	if (LaunchInfo.ue1Version > 219)
		LevelInfo->MinNetVersion() = LaunchInfo.gameVersionString + " SE";
	LevelInfo->bHighDetailMode() = true;
	/*
	LevelInfo->NetMode() = 0; // NM_StandAlone
	LevelInfo->DefaultTexture() = engine->DefaultTexture;
	*/

	GetLevelObject();

	LinkActorsToLevel();
}

void Engine::SaveGameToSlot(int32_t slotNum, const std::string& saveDescription) const
{
	if (slotNum < -1 || (!packages->IsDeusEx() && slotNum < 0))
		Exception::Throw("Invalid save slot: " + std::to_string(slotNum));

	// First and foremost ensure the Save folder exists
	const auto saveFolderPath = packages->GetSaveFolderPath();
	if (!fs::exists(saveFolderPath))
		fs::create_directory(saveFolderPath);

	if (packages->IsDeusEx())
	{
		// Saving a game on Deus Ex does the following:
		// - Create a folder using the slotNum (e.g. 1 -> "Save0001")
		// - Save the level package using the name [MapName].dxs
		// - Save the associated DeusExSaveInfo class as SaveInfo.dxs within that same folder,
		// in which saveDescription parameter will be used in DeusExSaveInfo.Description
		auto slotNumStr = std::to_string(slotNum);
		slotNumStr.insert(0, 4 - slotNumStr.length(), '0'); // Pad it with 0s
		auto saveFolder = "Save" + slotNumStr;

		auto saveSlotFolder = saveFolderPath / saveFolder;
		if (!fs::exists(saveSlotFolder) || !fs::is_directory(saveSlotFolder))
			fs::create_directory(saveSlotFolder);

		auto levelName = Level->package->GetPackageName().ToString() + "." + packages->GetSaveExtension();
		auto saveInfoName = "SaveInfo." + packages->GetSaveExtension();
		auto saveFileFullPath = (saveSlotFolder / levelName).string();
		auto saveInfoFullPath = (saveSlotFolder / saveInfoName).string();
		LevelPackage->Save(Level, saveFileFullPath);

		dxSaveInfo->DirectoryIndex() = slotNum;
		dxSaveInfo->Description() = saveDescription;
		dxSaveInfo->MissionLocation() = DeusExLevelInfo ? DeusExLevelInfo->MissionLocation() : "";
		dxSaveInfo->MapName() = Level->package->GetPackageName().ToString();
		dxSaveInfo->UpdateTimeStamp();
		deusExPackage->Save(dxSaveInfo, saveInfoFullPath);
	}
	else
	{
		const std::string saveFileName = "Save" + std::to_string(slotNum) + "." + packages->GetSaveExtension();
		const std::string saveFileFullPath = (saveFolderPath / saveFileName).string();
		LevelPackage->Save(Level, saveFileFullPath);
	}
}

std::map<std::string, std::string> Engine::CreateTravelInfo(bool transferItems)
{
	auto travelInfo = Level->TravelInfo;
	for (UActor* actor : Level->Actors)
	{
		UPlayerPawn* pawn = UObject::TryCast<UPlayerPawn>(actor);
		if (pawn && pawn->Player())
		{
			std::string playerName = engine->LaunchInfo.ue1Version > 219 ? pawn->PlayerReplicationInfo()->PlayerName() : std::string("Player"); // To do: how to get the travel player name?
			travelInfo[playerName] = ActorTravelInfo::Create(pawn, transferItems);
		}
	}
	return travelInfo;
}

void Engine::LoginPlayer()
{
	UnrealURL url = LevelInfo->URL;
	std::map<std::string, std::string> travelInfo = Level->TravelInfo;

	auto stringProp = GC::Alloc<UStringProperty>("", nullptr, ObjectFlags::NoFlags);
	std::string error, failcode;

	std::string portal = url.GetPortal();
	std::string options = url.GetOptions();

	std::string playerPawnClass = url.GetOption("Class");
	if (playerPawnClass.empty())
		playerPawnClass = packages->GetIniValue("system", "URL", "Class");
	UClass* pawnClass = packages->FindClass(playerPawnClass);

	// Perform PreLogin check (used for early rejection in network games)
	CallEvent(LevelInfo->Game(), EventName::PreLogin, {
		ExpressionValue::StringValue(options),
		ExpressionValue::Variable(&error, stringProp),
		ExpressionValue::Variable(&failcode, stringProp),
		});
	if (!error.empty() || !failcode.empty())
		Exception::Throw("GameInfo prelogin failed: " + error + " (" + failcode + ")");

	// Create viewport pawn
	size_t numActors = Level->Actors.size();
	UPlayerPawn* pawn = UObject::Cast<UPlayerPawn>(CallEvent(LevelInfo->Game(), EventName::Login, {
		ExpressionValue::StringValue(portal),
		ExpressionValue::StringValue(options),
		ExpressionValue::Variable(&error, stringProp),
		ExpressionValue::ObjectValue(pawnClass)
		}).ToObject());
	if (!pawn || !error.empty())
		Exception::Throw("GameInfo login failed: " + error);
	bool actorActuallySpawned = Level->Actors.size() != numActors;

	pawn->LoadProperties();

	if (auto pawnExt = UObject::TryCast<UPlayerPawnExt>(pawn))
	{
		// Unclear if this is how DeusEx spawned this object
		if (!pawnExt->FlagBase())
		{
			auto flagBaseCls = packages->FindClass("Extension.FlagBase");
			pawnExt->FlagBase() = UObject::Cast<UFlagBase>(packages->GetTransientPackage()->NewObject("FlagBase", flagBaseCls, ObjectFlags::Transient));
		}
	}

	// Assign the pawn to the viewport
	viewport->Actor() = pawn;
	viewport->Actor()->Player() = viewport;
	CallEvent(viewport->Actor(), EventName::Possess);

	// Transfer travel actors to the new map

	CallEvent(pawn, EventName::TravelPreAccept);

	Array<UActor*> acceptedActors;
	if (actorActuallySpawned && ClientTravelInfo.TravelType == ETravelType::TRAVEL_Relative)
	{
		std::string playerName = url.GetOption("Name");
		if (playerName.empty())
			playerName = packages->GetIniValue("system", "URL", "Name");

		auto it = travelInfo.find(playerName);
		if (!playerName.empty() && it != travelInfo.end())
		{
			acceptedActors = ActorTravelInfo::Accept(pawn, it->second);
		}
		else
		{
			if (travelInfo.empty())
				LogMessage("Skipping travel transfer. No travel data");
			else if (playerName.empty())
				LogMessage("Skipping travel transfer. Player name is empty");
			else
				LogMessage("Skipping travel transfer. Player '" + playerName + "' not found in travel info");
		}
	}

	for (UActor* actor : acceptedActors)
		CallEvent(actor, EventName::TravelPreAccept);

	CallEvent(LevelInfo->Game(), EventName::AcceptInventory, { ExpressionValue::ObjectValue(pawn) });

	for (UActor* actor : acceptedActors)
		CallEvent(actor, EventName::TravelPostAccept);

	CallEvent(pawn, EventName::TravelPostAccept);
	CallEvent(LevelInfo->Game(), EventName::PostLogin, { ExpressionValue::ObjectValue(pawn) });

	render->OnMapLoaded();
}

UZoneInfo* Engine::GetZoneActor(int zoneIndex)
{
	if (auto zone = UObject::TryCast<UZoneInfo>(Level->Model->Zones[zoneIndex].ZoneActor))
		return zone;
	else
		return LevelInfo;
}

UObject* Engine::FindObject(NameString name, NameString className)
{
	for (auto actor : Level->Actors)
	{
		if (actor && actor->Name == name && UObject::GetUClassFullName(actor) == className)
			return actor;
	}

	return nullptr;
}

float Engine::CalcTimeElapsed()
{
	using namespace std::chrono;

	uint64_t currentTime = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
	if (lastTime == 0)
		lastTime = currentTime;

	uint64_t deltaTime = currentTime - lastTime;
	lastTime = currentTime;
	return clamp(deltaTime / 1'000'000.0f, 0.0f, 1.0f);
}

std::string Engine::ParseClassName(std::string className)
{
	// Workaround for broken unrealscript code referencing windrv.windowsclient directly
	if (className == "windrv.windowsclient")
	{
		className = "ini:Engine.ViewportManager";
	}

	if (className.size() < 4 || className.substr(0, 4) != "ini:")
		return className;

	size_t pos = className.find_last_of('.');
	if (pos == std::string::npos)
		Exception::Throw("Parse error");

	NameString sectionName = className.substr(4, pos - 4);
	NameString keyName = className.substr(pos + 1);

	// Override the ini file for things that are internal in Surreal Engine
	if (sectionName == "Engine.Engine")
	{
		if (keyName == "GameRenderDevice" || keyName == "WindowedRenderDevice")
		{
			return renderdev->Class;
		}
		else if (keyName == "AudioDevice")
		{
			return audiodev->Class;
		}
		else if (keyName == "NetworkDevice")
		{
			return netdev->Class;
		}
		else if (keyName == "ViewportManager")
		{
			return client->Class;
		}
	}

	return packages->GetIniValue("system", sectionName, keyName);
}

std::string Engine::ConsoleCommand(UObject* context, const std::string& commandline, BitfieldBool& found)
{
	found = false;

	Array<std::string> args = GetArgs(commandline);
	if (args.empty())
	{
		return {};
	}

	std::string command = args[0];
	for (char& c : command) c = std::tolower(c);

	found = true;
	if (command == "exit" || command == "quit")
	{
		quit = true;
	}
	else if (command == "timedemo" && args.size() == 2)
	{
		render->ShowTimedemoStats = args[1] == "1";
	}
	else if (command == "stat" && args.size() == 2)
	{
		render->ShowRenderStats = 0;

		if (args[1] == "render")
			render->ShowRenderStats = 1;
	}
	else if (command == "collisiondebug" && args.size() == 2)
	{
		render->ShowCollisionDebug = args[1] == "1";
	}
	else if (command == "dxwindowdebug" && LaunchInfo.IsDeusEx())
	{
		m_DrawDebugDXWindowHierarchy = !m_DrawDebugDXWindowHierarchy;
	}
	else if (command == "showlog")
	{
		//Frame::ShowDebuggerWindow();
	}
	/*else if (command == "playsong")
	{
		auto music = LevelInfo->Song();
		if (music)
			audio->PlayMusic(AudioSource::CreateMod(music->Data, true, 0, LevelInfo->SongSection()));
	}
	else if (command == "stopsong")
	{
		audio->PlayMusic(nullptr);
	}*/
	else if (command == "getres")
	{
		return window->GetAvailableResolutions();
	}
	else if (command == "getcolordepths")
	{
		return "32 16";
	}
	else if (command == "getcurrentres")
	{
		int width = window->GetPixelWidth();
		int height = window->GetPixelHeight();

		return std::to_string(width) + "x" + std::to_string(height);
	}
	else if (command == "getcurrentcolordepth")
	{
		return "32";
	}
	else if (command == "getping")
	{
		return "0";
	}
	else if (command == "getloss")
	{
		return "0";
	}
	else if (command == "keyname" && args.size() == 2)
	{
		uint8_t index = Convert::to_uint8(args[1]);
		return keynames[index];
	}
	else if (command == "keybinding" && args.size() == 2)
	{
		const std::string& name = args[1];
		return keybindings[name];
	}
	else if ((command == "open" || command == "start") && args.size() == 2)
	{
		const std::string& maparg = args[1];

		UnrealURL url(maparg);

		for (auto& map : packages->GetMaps())
		{
			std::string mapname = fs::path(map).stem().string();

			if (StrTools::equals_ignore_case(mapname, url.Map))
			{
				ClientTravel(url.ToString(), ETravelType::TRAVEL_Absolute, false);
				return {};
			}	
		}

		LogMessage("Couldn't find map " + maparg);
	}
	else if (command == "switchlevel" && args.size() == 2)
	{
		// This works like open/start, but keeps the difficulty level
		// As well as the game type
		const std::string& maparg = args[1];

		UnrealURL url(maparg);

		// First check if the provided URL has a difficulty option
		std::string difficulty = url.GetOption("difficulty");

		// If there isn't, try to get it from the current level's options
		if (difficulty.empty())
		{
			difficulty = LevelInfo->URL.GetOption("difficulty");
			if (!difficulty.empty())
				url.AddOrReplaceOption("difficulty=" + difficulty);
		}

		// If there still isn't, try to figure the current difficulty out using LevelInfo
		if (difficulty.empty())
		{
			if (LevelInfo->bDifficulty0())
				difficulty = "0";
			else if (LevelInfo->bDifficulty1())
				difficulty = "1";
			else if (LevelInfo->bDifficulty2())
				difficulty = "2";
			else if (LevelInfo->bDifficulty3())
				difficulty = "3";
			else
				difficulty = "2"; // Assume "Normal" difficulty

			url.AddOrReplaceOption("difficulty=" + difficulty);
		}

		for (auto& map : packages->GetMaps())
		{
			std::string mapname = fs::path(map).stem().string();

			if (StrTools::equals_ignore_case(mapname, url.Map))
			{
				LevelInfo->NextURL() = url.ToString();
				return {};
			}
		}

		LogMessage("Couldn't find map " + maparg);
	}
	else if (command == "savegame" && (args.size() == 2 || args.size() == 3))
	{

		int32_t slotNum;
		// slotNum not being parsable shouldn't cause a crash
		try
		{
			slotNum = Convert::to_int32(args[1]);
		}
		catch (...)
		{
			return {};
		}

		SaveGameInfo.SaveGameSlot = slotNum;

		if (args.size() == 3)
			SaveGameInfo.SaveGameDescription = args[2];

		// SaveGameToSlot(slotNum, "");

		//LogMessage("SaveGame command not fully implemented yet!");
		return {};
	}
	else if (command == "get" && args.size() == 3)
	{
		NameString className = ParseClassName(args[1]);
		NameString propertyName = args[2];

		UClass* cls = packages->FindClass(className);
		if (!cls)
		{
			LogMessage("Could not find class '" + className.ToString() + "': " + commandline);
			return {};
		}

		if (className == renderdev->Class)
		{
			return renderdev->GetPropertyAsString(propertyName);
		}
		else if (className == audiodev->Class)
		{
			return audiodev->GetPropertyAsString(propertyName);
		}
		else if (className == netdev->Class)
		{
			return netdev->GetPropertyAsString(propertyName);
		}
		else if (className == client->Class)
		{
			return client->GetPropertyAsString(propertyName);
		}
		else
		{
			try
			{
				return cls->GetPropertyAsString(propertyName);
			}
			catch (const std::exception&)
			{
				LogMessage("Could not get property '" + propertyName.ToString() + "': " + commandline);
				return {};
			}
		}
	}
	else if (command == "set" && args.size() == 4)
	{
		NameString className = ParseClassName(args[1]);
		NameString propertyName = args[2];
		std::string value = args[3];

		// Special input setting handling
		if (className == "input")
		{
			keybindings[propertyName.ToString()] = value;
			packages->SetIniValue("user", "Engine.Input", propertyName, value);
			return {};
		}

		UClass* cls = packages->FindClass(className);
		if (!cls)
		{
			LogMessage("Could not find class '" + className.ToString() + "': " + commandline);
			return {};
		}

		if (className == renderdev->Class)
		{
			renderdev->SetPropertyFromString(propertyName, value);
		}
		else if (className == audiodev->Class)
		{
			audiodev->SetPropertyFromString(propertyName, value);
		}
		else if (className == netdev->Class)
		{
			netdev->SetPropertyFromString(propertyName, value);
		}
		else if (className == client->Class)
		{
			client->SetPropertyFromString(propertyName, value);
		}
		else
		{
			try
			{
				cls->SetPropertyFromString(propertyName, value);
			}
			catch (const std::exception&)
			{
				LogMessage("Could not set property '" + propertyName.ToString() + "': " + commandline);
			}
			return {};
		}
	}
	else if (command == "setres" && args.size() == 2)
	{
		window->SetResolution(args[1]);
	}
	else if (command == "togglefullscreen")
	{
		bool isFullscreen = window->IsFullscreen();

		// Get the resolution to SWITCH TO
		int width = isFullscreen ? client->WindowedViewportX : client->FullscreenViewportX;
		int height = isFullscreen ? client->WindowedViewportY : client->FullscreenViewportY;

		Size resolution;
		resolution.width = width;
		resolution.height = height;

		window->ToggleWindowFullscreen(resolution);
		viewport->SetViewportRect(0, 0, width, height);

		return {};
	}
	else if (command == "prsq")
	{
		// Klingon Honor Guard: CD check
		// "mpgameplay"
		// "mpinstall"
		return "mpgameplay";
	}
	else if (command == "os")
	{
		// 227 Seems to have this command
#ifdef WIN32
		return "Windows";
#elif __APPLE__
		return "macOS";
#else
		return "Linux"; // With apologies to BSD, Haiku and others...
#endif
	}
	else if (command == "getsplash")
	{
		return khgSplashScreen ? "true" : "false";
	}
	else if (command == "setsplash")
	{
		khgSplashScreen = true;
		return {};
	}
	else if (command == "playavi")
	{
		PlayAVI(args);
		return {};
	}
	else if (command == "flush")
	{
		engine->render->Device->Flush(1);
		return {};
	}
	else
	{
		if (!ExecCommand(args))
		{
			LogMessage("Unknown command: " + commandline);
			found = false;
		}
	}
	return {};
}

Array<std::string> Engine::GetArgs(const std::string& commandline)
{
	Array<std::string> args;
	size_t i = 0;
	while (i < commandline.size())
	{
		size_t j = commandline.find_first_not_of(" \t", i);
		if (j == std::string::npos)
			break;
		i = j;
		j = commandline.find_first_of(" \t", i);
		if (j == std::string::npos)
			j = commandline.size();
		if (j > i)
			args.push_back(commandline.substr(i, j - i));
		i = j;
	}
	return args;
}

Array<std::string> Engine::GetSubcommands(const std::string& command)
{
	Array<std::string> subcommands;
	size_t pos = 0;
	while (pos < command.size())
	{
		size_t endpos = command.find('|', pos);
		if (endpos == std::string::npos)
			endpos = command.size();

		std::string subcommand = command.substr(pos, endpos - pos);
		if (!subcommand.empty())
			subcommands.push_back(subcommand);
		pos = endpos + 1;
	}
	return subcommands;
}

void Engine::LoadEngineSettings()
{
	if (packages->MissingSESystemIni())
	{
		client->LoadProperties("WinDrv.WindowsClient");
		audiodev->LoadProperties("Galaxy.GalaxyAudioSubsystem");
		renderdev->LoadProperties("D3DDrv.Direct3DRenderDevice");
	}
	else
	{
		client->LoadProperties();
		audiodev->LoadProperties();
		renderdev->LoadProperties();
	}

#ifdef WIN32
	windowingSystemName = packages->GetIniValue("System", "Engine.SurrealWindowSystem", "WindowSystem", "Win32");
#else
	windowingSystemName = packages->GetIniValue("System", "Engine.SurrealWindowSystem", "WindowSystem", "SDL2");
#endif
}

void Engine::LoadKeybindings()
{
	for (int i = 0; i < 256; i++)
	{
		std::string keyname = keynames[i];
		keybindings[keyname] = packages->GetIniValue("user", "Engine.Input", keyname);
	}

	for (int i = 0; i < 40; i++)
	{
		std::string alias = packages->GetIniValue("user", "Engine.Input", "Aliases[" + std::to_string(i) + "]");

		// Total trash parsing, but it will do for the aliases I have! Feel free to improve it!
		std::string commandStart = "(Command=\"";
		std::string commandSplit = "\",Alias=";
		std::string commandEnd = ")";
		if (alias.size() > commandStart.size() + commandSplit.size() + commandEnd.size())
		{
			size_t pos = alias.find(commandSplit, commandStart.size());
			if (pos != std::string::npos)
			{
				size_t pos2 = alias.find(commandEnd, pos);
				if (pos2 != std::string::npos)
				{
					std::string aliasCommand = alias.substr(commandStart.size(), pos - commandStart.size());
					std::string aliasName = alias.substr(pos + commandSplit.size(), pos2 - pos - commandSplit.size());
					if (!aliasName.empty() && aliasName != "None")
						inputAliases[aliasName] = aliasCommand;
				}
			}
		}
	}
}

void Engine::UpdateInput(float timeElapsed)
{
	if (timeElapsed <= 0.0f)
		return;

	TickWindow();
	if (tickDebugger)
		tickDebugger();

	if (!viewport->Actor())
		return;

	for (auto& it : activeInputButtons)
		viewport->Actor()->SetBool(it.first, true);
	for (auto& it : activeInputAxes)
	{
		if (it.first == "aMouseX" || it.first == "aMouseY")
		{
			viewport->Actor()->SetFloat(it.first, it.second.Value / (timeElapsed * 150.0f));
		}
		else
		{
			viewport->Actor()->SetFloat(it.first, it.second.Value);
		}
	}
}

// M3: controller input. Bypasses the keybindings/activeInputButtons/
// activeInputAxes machinery entirely (no User.ini ships in this repo to
// bind gamepad/joystick keys in the first place, and continuous analog
// stick/trigger values map more directly onto SetBool/SetFloat than onto
// the discrete key-press model that machinery was built for). Held
// movement/fire axes and buttons are re-applied every frame from the
// current controller state; only single-shot actions (jump, weapon
// switch, menu, recenter) use edge detection against last frame's state.
//
// Two corrections found via real-headset testing, both load-bearing:
//
// 1. bFire/bAltFire/bDuck are BYTE properties on UPawn (see UActor.h's
//    `uint8_t& bFire()` etc., confirmed against the actually-loaded
//    package's property list, not guessed), not bool. UObject::SetBool()
//    used to unconditionally static_cast the resolved UProperty to
//    UBoolProperty* and treat the target memory as a 32-bit bitfield word -
//    calling it on a byte property was a type-confusion bug that corrupted
//    up to 3 neighboring property bytes rather than setting the intended
//    flag. Originally worked around here by writing through the native
//    byte accessors directly; SetBool() itself was fixed 2026-07-20 (see
//    its doc comment in UObject.cpp) to check the property's actual type,
//    so this function now calls it directly like the keyboard/mouse path
//    always has - see the doc comment above the bFire/bAltFire/bDuck
//    writes below for why that's preferable to the accessor workaround.
//
// 2. aBaseY/aStrafe/aUp's expected scale is NOT "UU/sec" - it's whatever a
//    full digital key press produces going through InputCommand's Axis
//    path: `activeInputAxes[name] = Speed * delta`, where a IST_Press
//    delta is a fixed 20 (see InputEvent()) and a typical default bind is
//    "Axis aBaseY Speed=350" - i.e. full-press feeds ~7000, not something
//    UU/sec-scaled. MOVE_SCALE below matches that convention so a fully
//    deflected stick feels like a fully held movement key.
//
// Mapping (Oculus Touch primary target - see VulkanXRSession.cpp's
// CreateActions() for the full interaction-profile bindings):
//   left stick Y/X    -> aBaseY / aStrafe (move forward, strafe - relative
//                        to Pawn.Rotation, which this function keeps
//                        synced to the live head yaw every frame, below)
//   right stick X      -> incrementally rotates xrYawOffsetUE (comfort
//                         turning - shifts where "physical forward" maps
//                         to in-game forward, same knob the one-time
//                         recenter uses, rather than fighting the
//                         head-yaw sync below by also driving aTurn)
//   right stick Y      -> aUp (swim/fly vertical thrust)
//   right trigger      -> Fire
//   left trigger       -> AltFire
//   right grip         -> Duck (held)
//   right A            -> Jump (edge)
//   left X / left Y    -> PrevWeapon / NextWeapon (edge)
//   left menu          -> ShowMenu (edge)
//   right stick click  -> recenter view (edge, resets xrPoseRecentered)
//
// Also syncs Pawn.Rotation.Yaw to the live tracked head yaw
// (xrHeadYawUE, computed one frame earlier in Run()'s XR frame loop) every
// frame. Movement direction and the first-person weapon viewmodel are both
// positioned relative to Pawn.Rotation, not the render-only camera pose -
// without this sync the pawn keeps facing wherever it last faced
// (spawn/last aTurn), so strafing/forward move independently of where the
// player is actually looking with their head, and the weapon (drawn
// relative to the stale facing) drifts out of view as soon as the player
// turns their head away from it. Pawn.Rotation only ever gets Yaw written -
// Pitch/Roll are left alone so looking up/down doesn't tilt the pawn's
// collision cylinder. Pawn.ViewRotation gets both Yaw and Pitch (weapon aim
// and swim direction are ViewRotation-driven, not collision-driven).
void Engine::UpdateVRControllerInput(float timeElapsed)
{
	if (!xrSession || !xrSessionActive || !xrSession->IsSessionRunning())
		return;
	if (!viewport->Actor())
		return;

	xrSession->SyncActions();
	VRControllerState state;
	xrSession->GetControllerState(state);

	UActor* pawn = viewport->Actor();

	// 2026-07-20 diagnostic (throttled to ~once/2s, not every frame): a
	// real-headset test reported fire not registering (couldn't even get
	// past the "click fire to start" prompt) and movement direction being
	// unclear relative to head turning. GetControllerState() now surfaces
	// whether xrGetActionState*'s isActive came back true for anything
	// this call (VRControllerState::actionsActive) - isActive silently
	// goes false whenever the runtime isn't routing input to this
	// session's action set (e.g. focus stolen by a system overlay/
	// dashboard), which reads identically to "nothing pressed" with no
	// error. This log turns "was it focus, or something else" into a
	// one-line answer from the next headset run's SE-Log-LastRun.txt
	// instead of another guess.
	static float diagAccum = 0.0f;
	diagAccum += timeElapsed;
	bool logDiagThisTick = diagAccum >= 2.0f;
	if (logDiagThisTick)
	{
		diagAccum = 0.0f;
		LogMessage("VR input diag: actionsActive=" + std::to_string(state.actionsActive) +
			" xrState=" + std::to_string(xrSession->GetLastSessionState()) +
			" syncResult=" + std::to_string(xrSession->GetLastSyncResult()) +
			" lStick=(" + std::to_string(state.leftStickX) + "," + std::to_string(state.leftStickY) + ")" +
			" rStick=(" + std::to_string(state.rightStickX) + "," + std::to_string(state.rightStickY) + ")" +
			" lTrig=" + std::to_string(state.leftTrigger) + " rTrig=" + std::to_string(state.rightTrigger) +
			" lGrip=" + std::to_string(state.leftGrip) + " rGrip=" + std::to_string(state.rightGrip));

		// 2026-07-21: user reports "walk left when I look right, walk right
		// when I look left" - not simply "unaffected" anymore (the
		// ViewRotation fix changed something), but possibly mirrored. Log
		// everything needed to compare "which way is the head/pawn facing"
		// against "which way is the pawn actually moving" in the SAME units
		// and the SAME sign convention (degrees, atan2(-y,x) - see the
		// rawYaw comment above in the eye-pose composition code), without
		// needing to guess from in-headset feel:
		//  - headYawDeg: live tracked head yaw this frame (pre-write)
		//  - rotYawDeg/viewYawDeg: what's actually stored on the pawn RIGHT
		//    NOW (i.e. last frame's write, read back - confirms the write
		//    stuck and wasn't overwritten by something else before this
		//    frame)
		//  - velHeadingDeg/velSpeed: the pawn's ACTUAL resulting Velocity
		//    from last frame's physics tick, decomposed with the same
		//    atan2(-y,x) formula - this is ground truth for "which way did
		//    the pawn really go," independent of any rotation bookkeeping
		const float ue1YawToRadians = 3.14159265359f / 32768.0f;
		vec3 vel = pawn->Velocity();
		float velHeadingDeg = degrees(std::atan2(-vel.y, vel.x));
		float velSpeed = length(vel);
		LogMessage("VR movement diag: headYawDeg=" + std::to_string(degrees(xrHeadYawUE)) +
			" rotYawDeg=" + std::to_string(degrees((float)(pawn->Rotation().Yaw & 0xffff) * ue1YawToRadians)) +
			" viewYawDeg=" + std::to_string(UObject::TryCast<UPawn>(pawn) ? degrees((float)(UObject::TryCast<UPawn>(pawn)->ViewRotation().Yaw & 0xffff) * ue1YawToRadians) : -1.0f) +
			" velHeadingDeg=" + std::to_string(velSpeed > 1.0f ? velHeadingDeg : 0.0f) +
			" velSpeed=" + std::to_string(velSpeed) +
			" physics=" + std::to_string((int)pawn->Physics()) +
			" lStickY(fwd)=" + std::to_string(state.leftStickY));

		// M-A: both hands' world-space grip position + aim yaw/pitch, same
		// throttle as the lines above - see
		// Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's M-A DoD ("per-hand diag
		// lines"). xrHands[] is computed once per XR frame in Run()'s XR
		// frame block (one frame before this read, same as xrHeadYawUE);
		// `valid=false` means no tracked pose this frame - position/aim
		// values are whatever was last composed (stale), not NaN/garbage.
		LogMessage("VR hand diag: L.valid=" + std::to_string(xrHands[0].valid) +
			" L.pos=(" + std::to_string(xrHands[0].gripPos.x) + "," + std::to_string(xrHands[0].gripPos.y) + "," + std::to_string(xrHands[0].gripPos.z) + ")" +
			" L.aimYawDeg=" + std::to_string(xrHands[0].aimRotator.YawDegrees()) +
			" L.aimPitchDeg=" + std::to_string(xrHands[0].aimRotator.PitchDegrees()) +
			" R.valid=" + std::to_string(xrHands[1].valid) +
			" R.pos=(" + std::to_string(xrHands[1].gripPos.x) + "," + std::to_string(xrHands[1].gripPos.y) + "," + std::to_string(xrHands[1].gripPos.z) + ")" +
			" R.aimYawDeg=" + std::to_string(xrHands[1].aimRotator.YawDegrees()) +
			" R.aimPitchDeg=" + std::to_string(xrHands[1].aimRotator.PitchDegrees()) +
			" mainHand=" + std::to_string(mainHand));
	}

	const float deadzone = 0.15f;
	auto applyDeadzone = [](float v, float dz) { return (std::fabs(v) < dz) ? 0.0f : v; };

	const float MOVE_SCALE = 7000.0f; // see doc comment above - matches InputCommand's Speed(350)*press-delta(20) convention, not raw UU/sec
	const float turnRateRadiansPerSec = radians(120.0f); // comfort-turn rate for right stick X

	pawn->SetFloat("aBaseY", applyDeadzone(state.leftStickY, deadzone) * MOVE_SCALE);
	pawn->SetFloat("aStrafe", applyDeadzone(state.leftStickX, deadzone) * MOVE_SCALE);
	pawn->SetFloat("aUp", applyDeadzone(state.rightStickY, deadzone) * MOVE_SCALE);

	// Sign: Coords::YawRotation(yaw) rotates local forward (1,0,0) toward
	// (cos(yaw), -sin(yaw), 0) - i.e. toward -Y for positive yaw. UE1's +Y is
	// "right" (see aStrafe below: positive = strafe right, the established
	// UT99 keybind convention), so positive yaw in this codebase turns the
	// view LEFT, not right. state.rightStickX is positive when the stick is
	// pushed right (standard OpenXR convention) - pushing right must turn
	// right, i.e. DEcrease yaw, so this subtracts rather than adds.
	if (timeElapsed > 0.0f)
		xrYawOffsetUE -= applyDeadzone(state.rightStickX, deadzone) * turnRateRadiansPerSec * timeElapsed;

	if (xrPoseRecentered)
	{
		const float ue1RadiansToYaw = 32768.0f / 3.14159265359f;
		// 2026-07-21: real-headset report - movement now responds to head/
		// stick direction (see ViewRotation doc comment below) but is
		// MIRRORED (look left -> walk right). xrHeadYawUE is in the
		// Coords::YawRotation convention (XAxis = (cos yaw, -sin yaw, 0)),
		// but UT99's native GetAxes(Rotator) - what PlayerMove actually
		// calls to turn ViewRotation into a movement direction, see
		// Native/NObject.cpp GetAxes -> Coords::Rotation - composes with
		// the OPPOSITE sign (XAxis = (cos yaw, +sin yaw, 0) for a pure-yaw
		// Rotator, verified algebraically from Coords::operator* combined
		// with Coords::YawRotation). Writing xrHeadYawUE straight into a
		// Rotator's Yaw therefore mirrors it about the X axis once GetAxes
		// decodes it. Negating here converts VR-space yaw into Rotator-space
		// yaw correctly; confirmed against real headset log data (six
		// full-speed samples fit velHeading = -rotYaw + stickOffset to <1
		// degree - see SE-Log-LastRun.txt from the 2026-07-20 test and the
		// VR movement diag line added below).
		int yaw = (int)(-xrHeadYawUE * ue1RadiansToYaw);
		pawn->Rotation().Yaw = yaw;

		// UT99's own PlayerPawn.PlayerMove (UnrealScript, runs via
		// CallEvent(PlayerTick) below in Level->Tick(), not something this
		// engine reimplements natively - see aBaseY/aStrafe having no
		// native C++ consumer anywhere in this codebase) computes its
		// movement axes from ViewRotation, not Rotation - Rotation is the
		// replicated body facing, ViewRotation is where the player is
		// actually looking/moving relative to. For keyboard/mouse play,
		// ViewRotation is what aTurn/aLookUp directly drive every tick; VR
		// never touches aTurn/aLookUp (always 0), so script-side logic that
		// re-derives ViewRotation from its own persisted state each tick
		// (a no-op when aTurn=0) was simply leaving it wherever it last
		// was (spawn-time), regardless of what we wrote to Rotation alone.
		// Writing ViewRotation.Yaw here too - before Level->Tick() runs -
		// means that no-op re-derivation now holds at OUR value instead of
		// a stale one. ViewRotation is UPawn-specific (not on the base
		// UActor `pawn` is typed as here), hence the cast.
		UPawn* vrPawn = UObject::TryCast<UPawn>(pawn);
		if (vrPawn)
		{
			vrPawn->ViewRotation().Yaw = yaw;

			// 2026-07-21: real-headset report - weapon doesn't aim up/down
			// with head look, and swimming is unplayable (PHYS_Swimming's
			// script-side movement, like PlayerMove's walk axes, is driven
			// off ViewRotation - looking up/down should swim up/down).
			// xrHeadPitchUE is already in Rotator/game convention (see its
			// doc comment in Engine.h) so, unlike yaw, no negation is
			// needed here. Deliberately only ViewRotation.Pitch is written,
			// never Rotation.Pitch - Rotation is the body/collision facing,
			// and UE1 Pawns are upright collision cylinders that were never
			// designed to pitch (see the function-level doc comment above).
			vrPawn->ViewRotation().Pitch = (int)(xrHeadPitchUE * ue1RadiansToYaw);
		}
	}

	const float triggerThreshold = 0.5f;
	bool fireHeld = state.rightTrigger > triggerThreshold;
	bool altFireHeld = state.leftTrigger > triggerThreshold;
	// M-C: rebound off right-grip analog (was `state.rightGrip >
	// triggerThreshold`) to left-stick-click - see
	// Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's M-C section and the "Ground
	// truth" note that right grip needs to free up for M-D's foregrip-grab
	// detection (the off-hand's grip analog is what M-D reads to detect a
	// two-handed hold; leaving crouch on it would fight that). leftStickClick
	// is an existing, already-bound-but-unused action (see
	// VulkanXRSession.cpp's left_stick_click action/binding/state read) so
	// this needed no new OpenXR action - just switching which state field
	// bDuck reads.
	pawn->SetBool("bDuck", state.leftStickClick);

	// 2026-07-21: the previous approach here (this function directly calling
	// `pawn->SetBool("bFire", fireHeld)` every tick, same as the doc comment
	// this replaced explained) turned out to be verifiably NOT enough - a
	// real-headset diagnostic (rising-edge log, since removed) proved the
	// write really was happening (logged rTrig up to 1.0, actionsActive=1,
	// syncResult=0 - a perfectly healthy read), yet nothing in-game
	// responded. What broke the case open: the user then clicked the mouse
	// to give the window OS focus, and THAT (a real IK_LeftMouse press,
	// going through GameWindow::OnMouseDown -> InputEvent()) is what
	// finally got them past the "click fire to start" prompt - proving the
	// gate (and, it turns out, actual gameplay reactions in general) needs
	// a real synthesized key event through Engine::InputEvent(), not just
	// the bFire property being true. InputEvent() dispatches to
	// CallEvent(console, KeyEvent) first (UI/HUD-level interception - e.g.
	// a "waiting to start" prompt) before ever falling through to the
	// InputCommand()->SetBool() property path keyboard/mouse ALSO ends up
	// at - our old code only ever did the property-write half, silently
	// skipping the KeyEvent dispatch this apparently depends on. Default
	// UT99 bindings map Fire to LeftMouse and AltFire to RightMouse (this
	// is a UI/game-loop key, unrelated to any physical mouse on this PC -
	// nothing else uses these two EInputKey slots while a VR session is
	// active), so trigger edges are synthesized as those two "keys" through
	// the exact same InputEvent() call sites a real click/keypress uses.
	static bool prevVRFireHeld = false;
	static bool prevVRAltFireHeld = false;
	if (fireHeld != prevVRFireHeld)
		InputEvent(IK_LeftMouse, fireHeld ? EInputType::IST_Press : EInputType::IST_Release);
	if (altFireHeld != prevVRAltFireHeld)
		InputEvent(IK_RightMouse, altFireHeld ? EInputType::IST_Press : EInputType::IST_Release);
	if (fireHeld && !prevVRFireHeld)
	{
		LogMessage("VR fire diag: Fire rising edge (InputEvent IK_LeftMouse) - rTrig=" + std::to_string(state.rightTrigger) +
			" actionsActive=" + std::to_string(state.actionsActive) +
			" syncResult=" + std::to_string(xrSession->GetLastSyncResult()) +
			" xrState=" + std::to_string(xrSession->GetLastSessionState()) +
			" pawnClass=" + UObject::GetUClassFullName(pawn).ToString());
	}
	prevVRFireHeld = fireHeld;
	prevVRAltFireHeld = altFireHeld;

	if (state.rightA && !prevVRRightA)
		ExecCommand({ "Jump" });
	if (state.leftX && !prevVRLeftX)
		ExecCommand({ "PrevWeapon" });
	if (state.leftY && !prevVRLeftY)
		ExecCommand({ "NextWeapon" });
	if (state.leftMenu && !prevVRLeftMenu)
		ExecCommand({ "ShowMenu" });
	if (state.rightStickClick && !prevVRRightStickClick)
		xrPoseRecentered = false; // re-captured on the next LocateViews() in Run()'s XR frame loop

	prevVRRightA = state.rightA;
	prevVRLeftX = state.leftX;
	prevVRLeftY = state.leftY;
	prevVRLeftMenu = state.leftMenu;
	prevVRRightStickClick = state.rightStickClick;
}

// M-B: --debugvrhands - see Engine.h's doc comment on debugVRHandsEnabled.
// Deliberately independent of xrSession/xrSessionActive (early-returns only
// on the flag itself) so this is exercisable with no OpenXR runtime and no
// headset connected at all - the primary non-interactive verification path
// for M-B (Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's M-B section). Writes
// straight into xrHands[] through the exact same fields
// (valid/gripPos/gripCoords/aimRotator) the real M-A OpenXR composition
// writes, so every consumer (MainHand()/OffHand(), the weapon RenderOverlays
// intercept, the off-hand marker renderer) needs zero debug-specific
// branching. Both position (orbiting a small circle) and orientation
// (yaw/pitch oscillating) change continuously over time - not a one-time
// static offset - so two screenshots taken a few seconds apart visibly
// differ, which is the whole point of the DoD's "proves it's tracking a
// moving pose" screenshot pair.
void Engine::UpdateDebugVRHands(float timeElapsed)
{
	if (!debugVRHandsEnabled)
		return;

	debugVRHandsTime += timeElapsed;

	const float angularSpeed = 0.6f;      // rad/s - slow enough to read clearly in a still screenshot
	const float orbitRadiusUU = 3.0f;     // ~3in circle
	const float forwardUU = 18.0f;        // ~18in in front of the head
	const float downUU = -6.0f;           // ~6in below eye height
	const float handSeparationUU = 7.0f;  // ~7in either side of center - keeps left/right hands visibly distinct
	const float aimSwingDeg = 15.0f;      // +/- yaw/pitch swing amplitude, degrees
	const float ue1RadiansToYaw = 32768.0f / 3.14159265359f;

	// Anchor to the current camera pose (CameraLocation/CameraRotation, just
	// refreshed by PlayerCalcView this same tick - see the call site in
	// Run()) exactly like the real hand poses anchor to it via
	// ComposeXRPoseToWorld's CameraLocation parameter, so the fake hands
	// move together with the player instead of staying fixed in the level.
	Coords headCoords = Coords::Rotation(CameraRotation); // XAxis=fwd, YAxis=right, ZAxis=up (GetAxes convention)

	for (int hand = 0; hand < 2; hand++)
	{
		float side = (hand == mainHand) ? handSeparationUU : -handSeparationUU;
		float phase = debugVRHandsTime * angularSpeed + (hand == mainHand ? 0.0f : 3.14159265359f);

		vec3 center = CameraLocation
			+ headCoords.XAxis * forwardUU
			+ headCoords.ZAxis * downUU
			+ headCoords.YAxis * side;
		vec3 orbit = headCoords.YAxis * (orbitRadiusUU * std::cos(phase))
			+ headCoords.ZAxis * (orbitRadiusUU * std::sin(phase));

		VRHandState& handState = xrHands[hand];
		handState.valid = true;
		handState.gripPos = center + orbit;

		float swingRad = radians(aimSwingDeg) * std::sin(phase * 0.5f);
		Rotator handRotator = CameraRotation;
		handRotator.Yaw += (int)(swingRad * ue1RadiansToYaw);
		handRotator.Pitch += (int)(swingRad * 0.5f * ue1RadiansToYaw);

		handState.gripCoords = Coords::Rotation(handRotator);
		handState.aimRotator = handRotator;
	}
}

// M-C: --debugvrfire - see Engine.h's doc comment on debugVRFireEnabled.
// Synthesizes one ~0.5s trigger squeeze (press then release) through the
// exact same Engine::InputEvent(IK_LeftMouse, ...) call UpdateVRControllerInput
// uses for a real trigger edge, a few seconds into the run so
// --debugvrhands's fake hand poses (and this run's log) have settled first.
// Held for half a second (not an instantaneous press+release in the same
// tick) so Level->Tick()'s script processing - which runs after this
// function each frame, see Run()'s loop - actually observes bFire=true for
// at least one full tick, and so automatic weapons get a chance to re-fire
// more than once from their own state code while "held", exercising the
// M-C DoD's "automatic re-fire tracks a moving controller" case even
// without a real headset (the fake hand keeps orbiting throughout the
// hold).
void Engine::UpdateDebugVRFire(float timeElapsed)
{
	if (!debugVRFireEnabled)
		return;

	debugVRFireTime += timeElapsed;

	// 2026-07-21: a single ~0.5s pulse at t=3s (this function's first cut)
	// produced zero TraceFire/ProjectileFire intercepts in an --autoplay run
	// (RenderOverlays intercept lines were present throughout, proving a
	// weapon was out and the VM seam was live, but no "VR fire intercept"
	// line ever appeared). This matches the exact real-headset precedent
	// already documented on UpdateVRControllerInput's fire path: UT99
	// commonly consumes the FIRST Fire click as a UI-level "click to
	// start"/focus dismissal (CallEvent(console, KeyEvent) returning
	// handled=true) before any click reaches actual weapon fire logic - see
	// that function's 2026-07-21 doc comment for the same phenomenon during
	// real-headset testing. Rather than one pulse, fire several spaced
	// pulses so at least one lands after any such one-time dismissal is
	// consumed, without needing to special-case detecting that gate.
	const float firstPressAtSeconds = 3.0f; // let the run/log settle first
	const float holdSeconds = 0.4f;
	const float periodSeconds = 1.0f;       // pulse start-to-start spacing
	const int pulseCount = 5;

	bool fireHeld = false;
	for (int i = 0; i < pulseCount; i++)
	{
		float pressAt = firstPressAtSeconds + i * periodSeconds;
		if (debugVRFireTime >= pressAt && debugVRFireTime < pressAt + holdSeconds)
		{
			fireHeld = true;
			break;
		}
	}

	static bool prevDebugVRFireHeld = false;
	if (fireHeld != prevDebugVRFireHeld)
	{
		InputEvent(IK_LeftMouse, fireHeld ? EInputType::IST_Press : EInputType::IST_Release);
		LogMessage(std::string("--debugvrfire: synthesized ") + (fireHeld ? "press" : "release") +
			" (InputEvent IK_LeftMouse) at t=" + std::to_string(debugVRFireTime));
	}
	prevDebugVRFireHeld = fireHeld;
}

// M-B: VM interception seam consumer - installed into Frame::InterceptCall
// from Run() (see the install site's doc comment) only while VR is active.
//
// 2026-07-21 deviation from the plan doc: the plan's ground-truth section
// named the intercepted function `InvCalcView`, based on
// RenderCanvas.cpp's ue1Version<=219 fallback path
// (`CallEvent(weapon, "InvCalcView", {})` at RenderCanvas.cpp:78/185). This
// build's actual target game is UT99 v436 (ue1Version > 219), which takes
// the OTHER branch instead - `CallEvent(..., EventName::RenderOverlays,
// ...)` on the player pawn - and a one-time diagnostic (logging every
// distinct function name Frame::Call saw invoked on the current weapon
// instance, see the 2026-07-21 SE-Log-LastRun.txt capture) proved
// `InvCalcView` is never called anywhere in that path. What actually
// computes and applies the viewmodel transform is the weapon's OWN
// `RenderOverlays` (defined on the `Weapon` base class, commonly overridden
// per-weapon - e.g. `enforcer.RenderOverlays` calls `CalcDrawOffset`, some
// vector/rotator math, then native `SetLocation`/`SetRotation` on itself),
// invoked by `PlayerPawn.RenderOverlays` (a *different* UObject instance -
// the pawn, not the weapon - so the instance filter below already
// disambiguates the two same-named functions without any extra work).
// Intercepting `RenderOverlays` on the weapon is the direct equivalent of
// the plan's `InvCalcView` idea (skip the whole compute-and-set script
// body, do it natively instead) - same seam, same mechanism, just the
// correct function name for this engine version. Falls through (returns
// false, zero side effects) for every other function or instance, and also
// whenever the main hand has no valid pose this frame - degrading to stock
// behavior rather than ever leaving the gun frozen or half-updated (the
// plan's open risk #6).
bool Engine::HandleFrameCallIntercept(UObject* instance, UFunction* func, Array<ExpressionValue>& args, ExpressionValue& result)
{
	(void)args;

	if (func->Name == "RenderOverlays")
	{
		UPlayerPawn* playerActor = viewport->Actor();
		if (!playerActor)
			return false;

		UWeapon* weapon = playerActor->Weapon();
		if (!weapon || instance != weapon)
			return false;

		VRHandState& hand = MainHand();
		if (!hand.valid)
			return false;

		VRWeaponGripInfo grip = GetWeaponGripInfo(weapon);

		vec3 worldGripOffset = hand.gripCoords.XAxis * grip.gripOffset.x
			+ hand.gripCoords.YAxis * grip.gripOffset.y
			+ hand.gripCoords.ZAxis * grip.gripOffset.z;
		weapon->Location() = hand.gripPos + worldGripOffset;
		weapon->Rotation() = WeaponAimRotator(hand) + grip.rotationTrim;

		// 2026-07-21: skipping the script body entirely also skips its own
		// internal `Canvas.DrawActor(Self, false, false)` call - for
		// ue1Version>219 (this build), RenderCanvas.cpp's RenderOverlays()/
		// RenderOverlaysVR() do NOT draw the weapon themselves (unlike the
		// <=219 fallback, which calls DrawActor explicitly right after
		// InvCalcView - see those functions), so that in-script draw call is
		// the ONLY thing that puts the mesh on screen. Confirmed by a
		// first-pass build that set Location/Rotation correctly (proven by the
		// throttled log below and by the flatscreen viewmodel disappearing
		// from its normal spot) but rendered no weapon anywhere on screen at
		// all. Fix: issue the same native draw call here, using the
		// Location/Rotation we just set, so the mesh still appears.
		render->DrawActor(weapon, false, false);

		// Throttled by call count (once every 200 calls) rather than every call -
		// RenderOverlays runs once per eye per frame in the VR HUD split (up to
		// ~180/s at 90fps), and this function has no timeElapsed parameter of
		// its own to accumulate against. Proves the intercept is actually
		// firing (M-B DoD) without flooding the log.
		static int logCallCounter = 0;
		if ((logCallCounter++ % 200) == 0)
		{
			LogMessage("VR RenderOverlays intercept: weapon=" + weapon->Class->Name.ToString() +
				" loc=(" + std::to_string(weapon->Location().x) + "," + std::to_string(weapon->Location().y) + "," + std::to_string(weapon->Location().z) + ")" +
				" rotYawDeg=" + std::to_string(weapon->Rotation().YawDegrees()) +
				" rotPitchDeg=" + std::to_string(weapon->Rotation().PitchDegrees()));
		}

		result = ExpressionValue::NothingValue();
		return true;
	}

	// M-C: fire-scoped ViewRotation swap (Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's
	// M-C section, "Single-hand fire redirect"). Unlike the RenderOverlays
	// case above, this does NOT skip the original script call - TraceFire/
	// ProjectileFire must actually run so their internal AdjustAim()/spread/
	// spawn logic executes for real, just with a swapped ViewRotation it
	// reads internally. Returns false so Frame::Call's normal dispatch below
	// proceeds unmodified; HandleFrameCallInterceptPost (the new
	// Frame::InterceptCallPost consumer) restores the saved value right
	// after that same call returns - see its doc comment and Frame.h's
	// InterceptCallPost doc comment for why one hook can't do both halves.
	if (func->Name == "TraceFire" || func->Name == "ProjectileFire")
	{
		UPlayerPawn* playerActor = viewport->Actor();
		if (!playerActor)
			return false;

		UWeapon* weapon = playerActor->Weapon();
		if (!weapon || instance != weapon)
			return false;

		VRHandState& hand = MainHand();
		if (!hand.valid)
			return false;

		VRFireViewRotationSave save;
		save.pawn = playerActor; // UPlayerPawn IS-A UPawn (UActor.h:2094) - ViewRotation() lives on UPawn
		save.saved = playerActor->ViewRotation();
		vrFireViewRotationStack.push_back(save);

		Rotator swapped = WeaponAimRotator(hand);
		playerActor->ViewRotation() = swapped;

		// Throttled (every 4th MATCHED call, not every call) - automatic
		// weapons re-fire from state code every tick while held, so an
		// unthrottled log here could flood during sustained minigun/pulse
		// fire; still frequent enough that a single test shot always
		// produces at least one line. Confirms the swap actually happened
		// and with what value (M-C DoD: "log shows intercepted fires whose
		// direction matches the fake hand rotator").
		//
		// 2026-07-21 finding (from an unthrottled diagnostic capture during
		// this milestone's own verification, kept here as it turned out to
		// matter for reading these logs): the Enforcer's own TraceFire
		// override calls `Super.TraceFire(...)` internally - same
		// func->Name, same `instance` (Self) - so this PRE hook legitimately
		// fires TWICE per shot (outer override, then the nested Super call),
		// and the POST hook twice to match. This is exactly the "nested fire
		// calls" scenario the plan's save/restore STACK (rather than a
		// single saved value) was built for, and it behaves correctly: the
		// inner call's baseline is the outer's already-swapped value (it
		// correctly restores back to that, not to the true pre-fire value),
		// and only the OUTER call's restore reaches the true baseline - see
		// HandleFrameCallInterceptPost's restore log, where the true
		// baseline (logged there as "restoredYawDeg") is the value that
		// stays constant across every shot in a run, proving no drift.
		static int fireLogCounter = 0;
		if ((fireLogCounter++ % 4) == 0)
		{
			LogMessage("VR fire intercept: #" + std::to_string(fireLogCounter) + " func=" + func->Name.ToString() +
				" weapon=" + weapon->Class->Name.ToString() +
				" baselineYawDeg=" + std::to_string(save.saved.YawDegrees()) +
				" baselinePitchDeg=" + std::to_string(save.saved.PitchDegrees()) +
				" swappedYawDeg=" + std::to_string(swapped.YawDegrees()) +
				" swappedPitchDeg=" + std::to_string(swapped.PitchDegrees()));
		}

		return false;
	}

	// M-C phase 2: fire-origin fix via CalcDrawOffset (plan's "Fire origin
	// (phase 2 of the same mechanism)" section). Two-phase: diagnose first
	// (let the stock script run, compare its real return value against what
	// the hand-based formula would produce - see
	// HandleFrameCallInterceptPost), only start actually overriding
	// (returning true, fully replacing the call like the RenderOverlays case
	// above) once that one-time diagnostic confirms the documented
	// Owner-relative convention (plan's open risk #3).
	if (func->Name == "CalcDrawOffset")
	{
		UPlayerPawn* playerActor = viewport->Actor();
		if (!playerActor)
			return false;

		UWeapon* weapon = playerActor->Weapon();
		if (!weapon || instance != weapon)
			return false;

		VRHandState& hand = MainHand();
		if (!hand.valid)
			return false;

		VRWeaponGripInfo grip = GetWeaponGripInfo(weapon);
		vec3 worldMuzzleOffset = hand.gripCoords.XAxis * grip.muzzleOffset.x
			+ hand.gripCoords.YAxis * grip.muzzleOffset.y
			+ hand.gripCoords.ZAxis * grip.muzzleOffset.z;
		vec3 worldMuzzlePos = hand.gripPos + worldMuzzleOffset;
		UActor* owner = weapon->Owner();
		vec3 candidate = owner ? (worldMuzzlePos - owner->Location()) : worldMuzzlePos;

		if (!vrCalcDrawOffsetConfirmedOwnerRelative)
		{
			// Diagnostic phase: don't touch the call at all - let the stock
			// script run and return its real value. Stash our candidate so
			// HandleFrameCallInterceptPost (which sees the exact same
			// instance/func for the SAME call, right after it returns) can
			// log the side-by-side comparison and flip
			// vrCalcDrawOffsetConfirmedOwnerRelative once.
			vrCalcDrawOffsetPendingCandidate = candidate;
			vrCalcDrawOffsetPendingCandidateValid = true;
			return false;
		}

		// Confirmed - override for real: same skip-the-script,
		// set-natively mechanism as RenderOverlays above, just returning a
		// vector instead of Nothing.
		result = ExpressionValue::VectorValue(candidate);
		return true;
	}

	return false;
}

// M-C: Frame::InterceptCallPost consumer - see Engine.h's doc comment on
// HandleFrameCallInterceptPost for the two responsibilities (TraceFire/
// ProjectileFire ViewRotation restore, CalcDrawOffset one-time diagnostic +
// override arming). Called unconditionally for EVERY Frame::Call while
// installed (see the install site in Run()), so both branches below re-check
// their own name/instance filter rather than assuming anything about why
// they were called - this function is the sole place that decides whether a
// given call is one HandleFrameCallIntercept (the PRE hook) cared about.
void Engine::HandleFrameCallInterceptPost(UObject* instance, UFunction* func, ExpressionValue& result)
{
	if (func->Name == "TraceFire" || func->Name == "ProjectileFire")
	{
		// Only pop if the top of the stack is actually for this instance's
		// pawn - guards against a mismatched pop if, e.g., the PRE hook
		// bailed early (no valid hand pose, no weapon) for THIS call but a
		// still-pending nested call from earlier is on top for a different
		// reason. Nesting isn't just theoretical here: an unthrottled
		// diagnostic capture during this milestone's verification showed the
		// Enforcer's own TraceFire override calling `Super.TraceFire(...)`
		// internally (same func->Name, same instance) - i.e. this PRE/POST
		// pair legitimately fires twice per shot, and the stack correctly
		// unwinds both (inner restores to the outer's already-swapped
		// value, outer restores to the true pre-fire baseline) - see the
		// #N-numbered "VR fire intercept"/"VR fire ViewRotation restore"
		// log pairs for a worked example.
		UPlayerPawn* playerActor = viewport->Actor();
		if (playerActor && !vrFireViewRotationStack.empty() && vrFireViewRotationStack.back().pawn == playerActor)
		{
			Rotator restored = vrFireViewRotationStack.back().saved;
			playerActor->ViewRotation() = restored;
			vrFireViewRotationStack.pop_back();

			// M-C DoD ("aim and movement are demonstrably decoupled in the
			// same log capture"): on a dev machine with no HMD attached, a
			// real xrSession never reaches FOCUSED (see the OpenXR probe
			// line logged at startup), so UpdateVRControllerInput's own
			// "VR movement diag"/"VR hand diag" lines - gated on
			// xrSessionActive, pre-existing from M-A/M-B, unchanged here -
			// never fire in a --debugvrhands-only capture (that gate is
			// orthogonal to whether hand poses/intercepts are active, and
			// is out of M-C's scope to loosen). This throttled line is the
			// non-interactive substitute available in that situation: it
			// proves ViewRotation is back to its pre-fire value (movement-
			// relevant state untouched) the instant TraceFire/ProjectileFire
			// returns, immediately below a "VR fire intercept" line showing
			// what it was swapped TO during the call - the two lines
			// together show the swap was real, scoped, and fully reverted,
			// which is what "movement never sees it" means structurally.
			static int restoreLogCounter = 0;
			if ((restoreLogCounter++ % 4) == 0)
			{
				LogMessage("VR fire ViewRotation restore: #" + std::to_string(restoreLogCounter) + " func=" + func->Name.ToString() +
					" restoredYawDeg=" + std::to_string(restored.YawDegrees()) +
					" restoredPitchDeg=" + std::to_string(restored.PitchDegrees()));
			}
		}
		return;
	}

	if (func->Name == "CalcDrawOffset" && vrCalcDrawOffsetPendingCandidateValid)
	{
		vrCalcDrawOffsetPendingCandidateValid = false;

		if (!vrCalcDrawOffsetDiagLogged)
		{
			vrCalcDrawOffsetDiagLogged = true;

			vec3 stockValue = result.ToVector();
			vec3 candidate = vrCalcDrawOffsetPendingCandidate;

			UPlayerPawn* playerActor = viewport->Actor();
			UWeapon* weapon = playerActor ? playerActor->Weapon() : nullptr;
			vec3 ownerLoc = weapon && weapon->Owner() ? weapon->Owner()->Location() : vec3(0.0f);

			// Heuristic (plan's open risk #3, "verify empirically before
			// trusting it"): a genuinely Owner-relative offset should be
			// small - same ballpark as PlayerViewOffset/FireOffset, at most
			// a few hundred UU - not comparable in magnitude to the actual
			// world-space Owner location (typically thousands of UU on a
			// real map). If the stock return value's length is already
			// close to (or larger than) a sane "near the player" bound, the
			// convention does NOT hold as documented and the override below
			// must stay off until that's investigated further.
			float stockLen = length(stockValue);
			const float ownerRelativeSanityBoundUU = 500.0f;
			vrCalcDrawOffsetConfirmedOwnerRelative = stockLen < ownerRelativeSanityBoundUU;

			LogMessage("VR CalcDrawOffset diagnostic (one-time): stock=(" +
				std::to_string(stockValue.x) + "," + std::to_string(stockValue.y) + "," + std::to_string(stockValue.z) + ")" +
				" stockLen=" + std::to_string(stockLen) +
				" candidate=(" + std::to_string(candidate.x) + "," + std::to_string(candidate.y) + "," + std::to_string(candidate.z) + ")" +
				" ownerLoc=(" + std::to_string(ownerLoc.x) + "," + std::to_string(ownerLoc.y) + "," + std::to_string(ownerLoc.z) + ")" +
				" confirmedOwnerRelative=" + std::to_string(vrCalcDrawOffsetConfirmedOwnerRelative));
		}
	}
}

// M-B: per-weapon grip/aim tuning table - see Engine.h's doc comment on
// VRWeaponGripInfo/GetWeaponGripInfo for why this is a plain hardcoded map
// rather than a new ini schema. No headset-verified entries exist yet (M-B
// has no headset access on this build machine); the Enforcer row below is a
// placeholder proving the lookup path end-to-end, not a tuned value -
// M-C/M-D/M-E populate real numbers as headset tuning happens.
Engine::VRWeaponGripInfo Engine::GetWeaponGripInfo(UWeapon* weapon)
{
	static const std::map<NameString, VRWeaponGripInfo> table = []()
	{
		std::map<NameString, VRWeaponGripInfo> t;
		VRWeaponGripInfo enforcer;
		enforcer.gripOffset = vec3(4.0f, 0.0f, -2.0f);
		t[NameString("Enforcer")] = enforcer;
		return t;
	}();

	if (weapon)
	{
		auto it = table.find(weapon->Class->Name);
		if (it != table.end())
			return it->second;
	}

	// Default: derive a sane starting offset from this weapon's own
	// authored flatscreen viewmodel numbers (PlayerViewOffset/FireOffset -
	// UActor.h:901/951) so an unlisted weapon's un-tuned VR anchor starts in
	// the same ballpark as its 2D viewmodel position instead of at the raw
	// hand origin - see the plan's M-B "Per-weapon grip table" section.
	VRWeaponGripInfo info;
	if (weapon)
	{
		info.gripOffset = weapon->PlayerViewOffset();
		info.muzzleOffset = weapon->FireOffset();
	}
	return info;
}

void Engine::OpenWindow()
{
	if (!window)
		window = GameWindow::Create(this, xrSession.get());

	int width = client->StartupFullscreen ? client->FullscreenViewportX : client->WindowedViewportX;
	int height = client->StartupFullscreen ? client->FullscreenViewportY : client->WindowedViewportY;
	bool fullscreen = client->StartupFullscreen;

	std::string versionString = !LaunchInfo.gameVersionString.empty() ? " (v" + LaunchInfo.gameVersionString + ")" : "";

	window->SetWindowTitle(LaunchInfo.gameName + versionString + " - Surreal Engine");
	window->SetFrameGeometry(Rect::xywh(0.0, 0.0, width, height));
	viewport->SetViewportRect(0, 0, width, height);

	if (fullscreen)
		window->ShowFullscreen();
	else
		window->ShowNormal();
}

void Engine::CloseWindow()
{
	window.reset();
}

void Engine::TickWindow()
{
	if (window && engine->LaunchInfo.ue1Version > 219)
	{
		if (viewport->bShowWindowsMouse() && viewport->bWindowsMouseAvailable())
			window->UnlockCursor();
		else
			window->LockCursor();
	}

	InputEvent(IK_MouseX, IST_Axis, 0);
	InputEvent(IK_MouseY, IST_Axis, 0);

	GameWindow::ProcessEvents();

	if (MouseMoveX != 0 || MouseMoveY != 0)
	{
		int dx = MouseMoveX;
		int dy = MouseMoveY;
		MouseMoveX = 0;
		MouseMoveY = 0;

		// Send to input subsystem.
		if (dx)
			InputEvent(IK_MouseX, IST_Axis, dx);
		if (dy)
			InputEvent(IK_MouseY, IST_Axis, -dy);
	}
}

void Engine::OnWindowPaint()
{
}

void Engine::OnWindowMouseMove(const Point& pos)
{
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseMove(pos))
		return;

	if (engine->LaunchInfo.ue1Version > 219)
	{
		viewport->WindowsMouseX() = (float)(pos.x * window->GetDpiScale());
		viewport->WindowsMouseY() = (float)(pos.y * window->GetDpiScale());
	}
}

void Engine::OnWindowMouseDown(const Point& pos, EInputKey key)
{
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseDown(pos, key))
		return;

	InputEvent(key, IST_Press);
}

void Engine::OnWindowMouseDoubleclick(const Point& pos, EInputKey key)
{
	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseDoubleclick(pos, key))
		return;
}

void Engine::OnWindowMouseUp(const Point& pos, EInputKey key)
{
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseUp(pos, key))
		return;

	InputEvent(key, IST_Release);
}

void Engine::OnWindowMouseWheel(const Point& pos, EInputKey key)
{
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowMouseWheel(pos, key))
		return;

	InputEvent(key, IST_Press);
	InputEvent(key, IST_Release);
}

void Engine::OnWindowRawMouseMove(int dx, int dy)
{
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowRawMouseMove(dx, dy))
		return;

	MouseMoveX += dx;
	MouseMoveY += dy;
}

void Engine::OnWindowKeyChar(std::string chars)
{
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowKeyChar(chars))
		return;

	Key(chars);
}

void Engine::OnWindowKeyDown(EInputKey key)
{
	if (playingAvi)
	{
		if (key == EInputKey::IK_Escape)
			skipAvi = true;
		return;
	}

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowKeyDown(key))
		return;

	InputEvent(key, IST_Press);
}

void Engine::OnWindowKeyUp(EInputKey key)
{
	if (playingAvi)
		return;

	if (engine->dxRootWindow && engine->dxRootWindow->OnWindowKeyUp(key))
		return;

	InputEvent(key, IST_Release);
}

void Engine::OnWindowGeometryChanged()
{
}

void Engine::OnWindowClose()
{
	quit = true;
}

void Engine::OnWindowActivated()
{
	//SetPause(false);
}

void Engine::OnWindowDeactivated()
{
	//SetPause(true);
}

void Engine::OnWindowDpiScaleChanged()
{
}

void Engine::LockCursor()
{
	if (window)
		window->LockCursor();
}

void Engine::UnlockCursor()
{
	if (window)
		window->UnlockCursor();
}

void Engine::Key(std::string key)
{
	if (Frame::RunState != FrameRunState::Running || playingAvi)
		return;

	if (LaunchInfo.IsDeusEx() && key == "~" && window->GetKeyState(IK_Shift))
	{
		// Did they REALLY hack UE1 to do something as lame as this??
		console->GotoState("Typing", {});
	}

	for (char c : key)
	{
		CallEvent(console, EventName::KeyType, { ExpressionValue::ByteValue(c) });
	}
}

void Engine::InputEvent(EInputKey key, EInputType type, int delta)
{
	if (Frame::RunState != FrameRunState::Running || playingAvi)
		return;

	bool handled = CallEvent(console, EventName::KeyEvent, { ExpressionValue::ByteValue(key), ExpressionValue::ByteValue(type), ExpressionValue::FloatValue((float)delta) }).ToBool();
	
	if (!handled)
	{
		if ((type == EInputType::IST_Press || type == EInputType::IST_Axis) && key >= 0 && key < 256)
		{
			if (type == EInputType::IST_Press)
				delta = 20;
			else
				delta *= 16;

			for (const std::string& command : GetSubcommands(keybindings[keynames[key]]))
			{
				auto it = inputAliases.find(command);
				if (it != inputAliases.end())
				{
					InputCommand(it->second, key, delta);
				}
				else
				{
					InputCommand(command, key, delta);
				}
			}
		}
		else if (type == EInputType::IST_Release)
		{
			for (auto it = activeInputButtons.begin(); it != activeInputButtons.end();)
			{
				if (it->second == key)
				{
					viewport->Actor()->SetBool(it->first, false);
					it = activeInputButtons.erase(it);
				}
				else
				{
					++it;
				}
			}

			for (auto it = activeInputAxes.begin(); it != activeInputAxes.end();)
			{
				if (it->second.Key == key)
				{
					viewport->Actor()->SetFloat(it->first, 0.0f);
					it = activeInputAxes.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
	}
}

bool Engine::ExecCommand(const Array<std::string>& args)
{
	for (UObject* target : { static_cast<UObject*>(viewport->Actor()), static_cast<UObject*>(console) })
	{
		if (!target)
			continue;

		UFunction* func = FindEventFunction(target, args[0]);
		if (func && AllFlags(func->FuncFlags, FunctionFlags::Exec))
		{
			Array<ExpressionValue> vmArgs;
			int argindex = 0;
			for (UField* field = func->Children; field != nullptr; field = field->Next)
			{
				UProperty* prop = UObject::TryCast<UProperty>(field);
				if (!prop)
					continue;

				if (AllFlags(prop->PropFlags, PropertyFlags::ReturnParm))
					continue;

				if (!AllFlags(prop->PropFlags, PropertyFlags::Parm))
					continue;

				if (argindex + 1 < args.size())
				{
					const std::string& arg = args[1 + argindex];
					switch (prop->ValueType)
					{
					case ExpressionValueType::Nothing: vmArgs.push_back(ExpressionValue::NothingValue()); break;
					case ExpressionValueType::ValueByte: vmArgs.push_back(ExpressionValue::ByteValue(std::atoi(arg.c_str()))); break;
					case ExpressionValueType::ValueInt: vmArgs.push_back(ExpressionValue::IntValue(std::atoi(arg.c_str()))); break;
					case ExpressionValueType::ValueBool: vmArgs.push_back(ExpressionValue::BoolValue(arg == "1" || arg == "true")); break;
					case ExpressionValueType::ValueFloat: vmArgs.push_back(ExpressionValue::FloatValue((float)std::atof(arg.c_str()))); break;
					case ExpressionValueType::ValueString: vmArgs.push_back(ExpressionValue::StringValue(arg)); break;
					case ExpressionValueType::ValueName: vmArgs.push_back(ExpressionValue::NameValue(arg)); break;
					default: LogMessage("Unsupported value type found in Engine.ExecCommand"); return false;
					}
				}
				else if (AllFlags(prop->PropFlags, PropertyFlags::OptionalParm))
				{
					vmArgs.push_back(ExpressionValue::NothingValue());
				}
				else
				{
					switch (prop->ValueType)
					{
					case ExpressionValueType::Nothing: vmArgs.push_back(ExpressionValue::NothingValue()); break;
					case ExpressionValueType::ValueByte: vmArgs.push_back(ExpressionValue::ByteValue(0)); break;
					case ExpressionValueType::ValueInt: vmArgs.push_back(ExpressionValue::IntValue(0)); break;
					case ExpressionValueType::ValueBool: vmArgs.push_back(ExpressionValue::BoolValue(false)); break;
					case ExpressionValueType::ValueFloat: vmArgs.push_back(ExpressionValue::FloatValue(0.0f)); break;
					case ExpressionValueType::ValueString: vmArgs.push_back(ExpressionValue::StringValue({})); break;
					case ExpressionValueType::ValueName: vmArgs.push_back(ExpressionValue::NameValue({})); break;
					default: LogMessage("Unsupported value type found in Engine.ExecCommand"); return false;
					}
				}
				argindex++;
			}

			CallEvent(target, func->Name, vmArgs);
			return true;
		}
	}

	return false;
}

void Engine::InputCommand(const std::string& commands, EInputKey key, int delta)
{
	for (const std::string& commandline : GetSubcommands(commands))
	{
		Array<std::string> args = GetArgs(commandline);
		if (!args.empty())
		{
			std::string command = args[0];
			for (char& c : command) c = std::tolower(c);

			if (command == "button" && args.size() == 2)
			{
				activeInputButtons[args[1]] = key;
			}
			else if (command == "axis" && args.size() == 3)
			{
				float speed = 1.0f;
				if (args[2].size() > 6 && args[2].substr(0, 6) == "Speed=")
					speed = (float)std::atof(args[2].substr(6).c_str());
				activeInputAxes[args[1]] = { speed * delta, key };
			}
			else
			{
				ExecCommand(args);
			}
		}
	}
}

void Engine::SetPause(bool value)
{
	m_GamePaused = value;
}

void Engine::LogGamePackageSHA1Sums() const
{
	auto systemPath = packages->GetSystemFolderPath();

	LogMessage("SHA1Sums of some system files:");
	LogMessage("Core.u: " + SHA1Sum::of_file(systemPath / "Core.u"));
	LogMessage("Engine.u: " + SHA1Sum::of_file(systemPath / "Engine.u"));

	if (packages->IsUnrealTournament())
	{
		LogMessage("Botpack.u: " + SHA1Sum::of_file(systemPath / "Botpack.u"));
		LogMessage("UnrealI.u: " + SHA1Sum::of_file(systemPath / "UnrealI.u"));
		LogMessage("UnrealShare.u: " + SHA1Sum::of_file(systemPath / "UnrealShare.u"));
	}
	else if (packages->IsUnreal1())
	{
		LogMessage("UnrealI.u: " + SHA1Sum::of_file(systemPath / "UnrealI.u"));
		LogMessage("UnrealShare.u: " + SHA1Sum::of_file(systemPath / "UnrealShare.u"));
	}
	else if (packages->IsKlingonHonorGuard())
	{
		LogMessage("Klingons.u: " + SHA1Sum::of_file(systemPath / "Klingons.u"));
	}
}

void Engine::GetLevelInfoObject()
{
	LevelInfo = UObject::Cast<ULevelInfo>(LevelPackage->GetUObject("LevelInfo", "LevelInfo0"));
	if (LaunchInfo.ue1Version < 300) // Unknown when this changed
	{
		for (int grr = 1; !LevelInfo && grr < 20; grr++)
			LevelInfo = UObject::Cast<ULevelInfo>(LevelPackage->GetUObject("LevelInfo", "LevelInfo" + std::to_string(grr)));
	}
	if (!LevelInfo)
		Exception::Throw("Could not find the LevelInfo object for " + LevelPackage->GetPackageName().ToString() + "!");
}

void Engine::GetLevelObject()
{
	Level = UObject::Cast<ULevel>(LevelPackage->GetUObject("Level", "MyLevel"));
	if (!Level)
		Exception::Throw("Could not find the Level object for" + LevelPackage->GetPackageName().ToString() + "!");

	if (LaunchInfo.IsDeusEx())
	{
		// Also try to find DeusExLevelInfo
		DeusExLevelInfo = UObject::Cast<UDeusExLevelInfo>(LevelPackage->GetUObject("DeusExLevelInfo", "DeusExLevelInfo0"));

		// Didn't find it. Keep searching.
		for (int grr = 1; !DeusExLevelInfo && grr < 20; grr++)
			DeusExLevelInfo = UObject::Cast<UDeusExLevelInfo>(LevelPackage->GetUObject("DeusExLevelInfo", "DeusExLevelInfo" + std::to_string(grr)));

		// Entry.dx does not have a DeusExLevelInfo
		/*
		if (!DeusExLevelInfo)
			Exception::Throw("Could not find the DeusExLevelInfo object for " + url.Map + "!");
		*/

		// Link giveObject for all events in the mission
		if (UConversationList* conList = GetDeusExMission())
		{
			for (UConItem* item = conList->conversations(); item; item = item->Next())
			{
				auto conversation = UObject::Cast<UConversation>(item->ConObject());
				for (UConEvent* e = conversation->eventList(); e; e = e->nextEvent())
				{
					EEventType eventType = (EEventType)e->eventType();
					if (eventType == EEventType::TransferObject)
					{
						if (auto transfer = UObject::Cast<UConEventTransferObject>(e))
						{
							UClass* cls = engine->packages->FindClass("DeusEx." + transfer->ObjectName());
							if (!cls)
								LogMessage("Could not find class for TransferObject: " + transfer->ObjectName());
							transfer->giveObject() = cls;
						}
					}
					else if (eventType == EEventType::CheckObject)
					{
						if (auto eventCheckObject = UObject::Cast<UConEventCheckObject>(e))
						{
							if (eventCheckObject->ObjectName().starts_with("NK_"))
							{
								eventCheckObject->checkObject() = nullptr;
							}
							else
							{
								UClass* cls = engine->packages->FindClass("DeusEx." + eventCheckObject->ObjectName());
								if (!cls)
									LogMessage("Could not find class for CheckObject: " + eventCheckObject->ObjectName());
								eventCheckObject->checkObject() = cls;
							}
						}
					}
				}

				// Remove comments from event lists:
				while (conversation->eventList() && (EEventType)conversation->eventList()->eventType() == EEventType::Comment)
					conversation->eventList() = conversation->eventList()->nextEvent();
				UConEvent* cur = conversation->eventList();
				while (cur != nullptr)
				{
					auto next = cur->nextEvent();
					if (next && (EEventType)next->eventType() == EEventType::Comment)
					{
						cur->nextEvent() = next->nextEvent();
					}
					else
					{
						cur = next;
					}
				}
			}
		}
	}
}

void Engine::LinkActorsToLevel()
{
	// Link actors to the level
	for (UActor* actor : Level->Actors)
	{
		if (actor)
		{
			actor->XLevel() = Level;
			Level->Collision.AddToCollision(actor);
			Level->Light.AddLight(actor);
		}
	}
}

const char* Engine::keynames[256] =
{
	/*00*/ "None", "LeftMouse", "RightMouse", "Cancel",
	/*04*/ "MiddleMouse", "Unknown05", "Unknown06", "Unknown07",
	/*08*/ "Backspace", "Tab", "Unknown0A", "Unknown0B",
	/*0C*/ "Unknown0C", "Enter", "Unknown0E", "Unknown0F",
	/*10*/ "Shift", "Ctrl", "Alt", "Pause",
	/*14*/ "CapsLock", "Unknown15", "Unknown16", "Unknown17",
	/*18*/ "Unknown18", "Unknown19", "Unknown1A", "Escape",
	/*1C*/ "Unknown1C", "Unknown1D", "Unknown1E", "Unknown1F",
	/*20*/ "Space", "PageUp", "PageDown", "End",
	/*24*/ "Home", "Left", "Up", "Right",
	/*28*/ "Down", "Select", "Print", "Execute",
	/*2C*/ "PrintScrn", "Insert", "Delete", "Help",
	/*30*/ "0", "1", "2", "3",
	/*34*/ "4", "5", "6", "7",
	/*38*/ "8", "9", "Unknown3A", "Unknown3B",
	/*3C*/ "Unknown3C", "Unknown3D", "Unknown3E", "Unknown3F",
	/*40*/ "Unknown40", "A", "B", "C",
	/*44*/ "D", "E", "F", "G",
	/*48*/ "H", "I", "J", "K",
	/*4C*/ "L", "M", "N", "O",
	/*50*/ "P", "Q", "R", "S",
	/*54*/ "T", "U", "V", "W",
	/*58*/ "X", "Y", "Z", "Unknown5B",
	/*5C*/ "Unknown5C", "Unknown5D", "Unknown5E", "Unknown5F",
	/*60*/ "NumPad0", "NumPad1", "NumPad2", "NumPad3",
	/*64*/ "NumPad4", "NumPad5", "NumPad6", "NumPad7",
	/*68*/ "NumPad8", "NumPad9", "GreyStar", "GreyPlus",
	/*6C*/ "Separator", "GreyMinus", "NumPadPeriod", "GreySlash",
	/*70*/ "F1", "F2", "F3", "F4",
	/*74*/ "F5", "F6", "F7", "F8",
	/*78*/ "F9", "F10", "F11", "F12",
	/*7C*/ "F13", "F14", "F15", "F16",
	/*80*/ "F17", "F18", "F19", "F20",
	/*84*/ "F21", "F22", "F23", "F24",
	/*88*/ "Unknown88", "Unknown89", "Unknown8A", "Unknown8B",
	/*8C*/ "Unknown8C", "Unknown8D", "Unknown8E", "Unknown8F",
	/*90*/ "NumLock", "ScrollLock", "Unknown92", "Unknown93",
	/*94*/ "Unknown94", "Unknown95", "Unknown96", "Unknown97",
	/*98*/ "Unknown98", "Unknown99", "Unknown9A", "Unknown9B",
	/*9C*/ "Unknown9C", "Unknown9D", "Unknown9E", "Unknown9F",
	/*A0*/ "LShift", "RShift", "LControl", "RControl",
	/*A4*/ "UnknownA4", "UnknownA5", "UnknownA6", "UnknownA7",
	/*A8*/ "UnknownA8", "UnknownA9", "UnknownAA", "UnknownAB",
	/*AC*/ "UnknownAC", "UnknownAD", "UnknownAE", "UnknownAF",
	/*B0*/ "UnknownB0", "UnknownB1", "UnknownB2", "UnknownB3",
	/*B4*/ "UnknownB4", "UnknownB5", "UnknownB6", "UnknownB7",
	/*B8*/ "UnknownB8", "UnknownB9", "Semicolon", "Equals",
	/*BC*/ "Comma", "Minus", "Period", "Slash",
	/*C0*/ "Tilde", "UnknownC1", "UnknownC2", "UnknownC3",
	/*C4*/ "UnknownC4", "UnknownC5", "UnknownC6", "UnknownC7",
	/*C8*/ "Joy1", "Joy2", "Joy3", "Joy4",
	/*CC*/ "Joy5", "Joy6", "Joy7", "Joy8",
	/*D0*/ "Joy9", "Joy10", "Joy11", "Joy12",
	/*D4*/ "Joy13", "Joy14", "Joy15", "Joy16",
	/*D8*/ "UnknownD8", "UnknownD9", "UnknownDA", "LeftBracket",
	/*DC*/ "Backslash", "RightBracket", "SingleQuote", "UnknownDF",
	/*E0*/ "JoyX", "JoyY", "JoyZ", "JoyR",
	/*E4*/ "MouseX", "MouseY", "MouseZ", "MouseW",
	/*E8*/ "JoyU", "JoyV", "UnknownEA", "UnknownEB",
	/*EC*/ "MouseWheelUp", "MouseWheelDown", "Unknown10E", "Unknown10F",
	/*F0*/ "JoyPovUp", "JoyPovDown", "JoyPovLeft", "JoyPovRight",
	/*F4*/ "UnknownF4", "UnknownF5", "Attn", "CrSel",
	/*F8*/ "ExSel", "ErEof", "Play", "Zoom",
	/*FC*/ "NoName", "PA1", "OEMClear", ""
};
