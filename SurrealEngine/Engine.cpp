
#include "Precomp.h"
#include "Engine.h"
#include "Utils/File.h"
#include "Utils/StrTools.h"
#include "Utils/SHA1Sum.h"
#include "Utils/CommandLine.h"
#include "Runtime/HeadlessDriver.h"
#include "BotBenchmark/BotBenchmarkDriver.h"
#include "Input/DesktopInputDefaults.h"
#include "Platform/OpenXR/OpenXRProvider.h"
#include "Platform/Browser/BrowserRelativeMouse.h"
#include <surrealwidgets/window/browser_relative_mouse.h>
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
#include "Math/FrustumPlanes.h"
#include "GameWindow.h"
#include "RenderDevice/RenderDevice.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include "XR/XRWeaponRuntime.h"
#include "XR/XRLaunchPolicy.h"
#include "LauncherSettings.h"
#include "Video/VideoPlayer.h"
#include "Video/VideoFrameScheduler.h"
#include <atomic>
#include <chrono>
#include <set>

Engine* engine = nullptr;

namespace
{
	XRInputBindings NativeOpenXRInputBindings(XRHand dominantHand = XRHand::Right)
	{
		return XRInputBindings::NativeOpenXR(dominantHand);
	}
}

#ifdef __EMSCRIPTEN__
class BrowserCinematicPlayback
{
public:
	explicit BrowserCinematicPlayback(const std::string& filename)
		: Player(VideoPlayer::Create(filename, false)), Scheduler(Player.get())
	{
		Texture.CacheID = 0xffffffff'ffffffffULL;
		Texture.Format = TextureFormat::BGRA8;
		Texture.NumMips = 1;
	}

	VideoFrameStep Advance(float elapsedSeconds)
	{
		const VideoFrameStep result = Scheduler.Advance(Started ? elapsedSeconds : 0.0f);
		Started = true;
		UnrealMipmap* frame = Scheduler.CurrentFrame();
		if (frame)
		{
			Texture.Mips = frame;
			Texture.USize = frame->Width;
			Texture.VSize = frame->Height;
			Texture.bRealtimeChanged = result == VideoFrameStep::FrameReady;
		}
		return result;
	}

	std::unique_ptr<AudioSource> TakeAudio() { return Player->GetAudio(); }
	FTextureInfo* FrameTexture() { return Texture.Mips ? &Texture : nullptr; }

private:
	std::unique_ptr<VideoPlayer> Player;
	VideoFrameScheduler Scheduler;
	FTextureInfo Texture;
	bool Started = false;
};
#endif

Engine::Engine(GameLaunchInfo launchinfo)
	: LaunchInfo(launchinfo), openXRInput(NativeOpenXRInputBindings())
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
		dxSaveInfoPackage.set(packages->CreateDeusExSaveInfoPackage());
		dxSaveInfo = UObject::Cast<UDXSaveInfo>(dxSaveInfoPackage.get()->NewObject("MyDeusExSaveInfo", deusExPackage->GetClass("DeusExSaveInfo"), ObjectFlags::Transient));
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
	UninstallXRWeaponCallHook();
	if (audiodev)
		audiodev->ShutdownDevice();

	Logger::Get()->SaveLogAsPlaintext((Directory::localAppData() / "SurrealEngine/SE-Log-LastRun.txt").string());

	engine = nullptr;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

extern "C"
{
	int surreal_browser_audio_resume_js();
	int surreal_browser_audio_suspend_js();
	int surreal_browser_audio_shutdown_js();
	void surreal_browser_audio_set_output_js(float volume, int muted);
	int surreal_browser_audio_state_js();
	int surreal_browser_audio_current_time_ms_js();
}

static bool XRFrameLoopActive = false;
static std::atomic<bool> BrowserEscapeIntentPending = false;
static BrowserRelativeMouseAccumulator BrowserRelativeMouseMotion;

static void EngineMainLoopCallback(void* arg)
{
	Engine* eng = static_cast<Engine*>(arg);
	if (eng->quit)
	{
		emscripten_cancel_main_loop();
		eng->Shutdown();
		return;
	}
	if (XRFrameLoopActive)
		return;
	eng->RunOneFrame();
}

// Browser smoke-test hooks let host JS observe the RAF-driven loop is live
// and request a clean shutdown, without needing any real rendering.
extern "C"
{
	// uint32_t, not uint64_t: ccall/cwrap don't legalize i64 return values to
	// JS Number without -sWASM_BIGINT, and M1's smoke test only needs "is
	// this advancing", not the full 64-bit range.
	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetTickCount()
	{
		return engine ? static_cast<uint32_t>(engine->tickCount) : 0;
	}

	EMSCRIPTEN_KEEPALIVE void Surreal_RequestQuit()
	{
		surreal_browser_audio_shutdown_js();
		if (engine)
			engine->quit = true;
	}

	EMSCRIPTEN_KEEPALIVE void Surreal_ForwardBrowserEscape()
	{
		BrowserEscapeIntentPending.store(true, std::memory_order_release);
	}

	EMSCRIPTEN_KEEPALIVE void Surreal_ForwardBrowserMouseMotion(int dx, int dy)
	{
		BrowserRelativeMouseMotion.Add(dx, dy);
	}

	EMSCRIPTEN_KEEPALIVE void Surreal_ResetBrowserMouseMotion()
	{
		BrowserRelativeMouseMotion.Reset();
	}

	EMSCRIPTEN_KEEPALIVE void Surreal_SetBrowserMouseMotionActive(int active)
	{
		const bool enabled = active != 0;
		if (!enabled)
			SetBrowserRelativeMouseBridgeActive(false);
		BrowserRelativeMouseMotion.Reset();
		if (enabled)
			SetBrowserRelativeMouseBridgeActive(true);
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_ResumeBrowserAudio() { return surreal_browser_audio_resume_js(); }
	EMSCRIPTEN_KEEPALIVE int Surreal_SuspendBrowserAudio() { return surreal_browser_audio_suspend_js(); }
	EMSCRIPTEN_KEEPALIVE int Surreal_ShutdownBrowserAudio() { return surreal_browser_audio_shutdown_js(); }
	EMSCRIPTEN_KEEPALIVE void Surreal_SetBrowserAudioOutput(float volume, int muted)
	{
		surreal_browser_audio_set_output_js(volume, muted);
	}
	EMSCRIPTEN_KEEPALIVE int Surreal_GetBrowserAudioState() { return surreal_browser_audio_state_js(); }
	EMSCRIPTEN_KEEPALIVE int Surreal_GetBrowserAudioCurrentTimeMs() { return surreal_browser_audio_current_time_ms_js(); }

	EMSCRIPTEN_KEEPALIVE int Surreal_SetXRFrameLoopActive(int active)
	{
		if (!engine || engine->quit)
			return 0;
		const bool requested = active != 0;
		if (requested == XRFrameLoopActive)
			return 1;
		XRFrameLoopActive = requested;
		// Merely returning early from EngineMainLoopCallback still re-enters Wasm
		// on every browser rAF. A WebXR render can be Asyncify-suspended on OPFS,
		// so XR ownership must stop the callback itself until that render drains.
		if (XRFrameLoopActive)
			emscripten_pause_main_loop();
		else
			emscripten_resume_main_loop();
		return 1;
	}
}
#endif

void Engine::Run()
{
	Setup();
	// A selected headless driver runs synchronously during Setup, before any
	// presentation devices are created. Do not enter either presentation loop.
	if (commandline && !commandline->GetArg("", "--headless-driver").empty())
		return;
#ifdef __EMSCRIPTEN__
	// simulate_infinite_loop=0: matches QuakeQuest's main_web.c reference -
	// this returns immediately after registering the RAF callback rather
	// than unwinding the stack via a JS-level throw (simulate_infinite_loop=1
	// relies on that unwind to keep stack-allocated locals like `this` alive,
	// which isn't reliable here). GameApp.cpp gives the Engine static storage
	// duration so it survives this function returning.
	emscripten_set_main_loop_arg(EngineMainLoopCallback, this, 0, 0);
#else
	while (!quit)
		RunOneFrame();
	Shutdown();
#endif
}

void Engine::Setup()
{
	LogMessage("Game: " + LaunchInfo.gameName + " (Version: " + LaunchInfo.gameVersionString + ")");
	LoadEngineSettings();
	XRHand configuredHand = LauncherSettings::Get().XR.DominantHand;
	if (commandline && commandline->HasArg("", "--vr-lefthand"))
		configuredHand = XRHand::Left;
	SetXRDominantHand(configuredHand);
	LogMessage("Loaded Engine settings");
	LoadKeybindings();
	LogMessage("Loaded key bindings");
	LogGamePackageSHA1Sums();
	const bool openXRRequested = ResolveOpenXRLaunchRequest(
		LauncherSettings::Get().XR.Enabled,
		commandline && commandline->HasArg("", "--openxr"),
		commandline && commandline->HasArg("", "--no-openxr"));
	if (openXRRequested)
	{
		openXR = std::make_unique<OpenXRProvider>();
		if (!openXR->IsAvailable())
		{
			LogMessage("OpenXR is unavailable: " + openXR->LastError() + "; continuing in desktop mode");
			openXR.reset();
		}
	}

	const std::string headlessDriverName = commandline ? commandline->GetArg("", "--headless-driver") : std::string();
	if (!headlessDriverName.empty())
	{
		RegisterBotBenchmarkDriver(GetHeadlessDriverRegistry());
		RunHeadlessDriver(headlessDriverName);
		return;
	}

	#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
	LogMessage("[asyncify-stage] OpenWindow begin");
	#endif
	OpenWindow();
	#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
	LogMessage("[asyncify-stage] OpenWindow complete");
	LogMessage("[asyncify-stage] Audio InitDevice begin");
	#endif

	audiodev->InitDevice();
	#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
	LogMessage("[asyncify-stage] Audio InitDevice complete");
	LogMessage("[asyncify-stage] RenderSubsystem begin");
	#endif
	render = std::make_unique<RenderSubsystem>(window->GetRenderDevice());
	#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
	LogMessage("[asyncify-stage] RenderSubsystem complete");
	#endif
	if (openXR && !openXR->IsSessionReady())
	{
		LogMessage("OpenXR did not bind to the selected renderer; continuing in desktop mode");
		openXR.reset();
	}
	else if (openXR)
	{
		openXR->SetUIRenderDevice(render->Device);
		if (!openXRUI.Start(render->XRUISurfaces(), *openXR, 1.0f / 0.0254f))
			LogMessage("OpenXR UI composition is unavailable: " + openXR->LastError());
	}

	const XRStartupLaunchPlan xrStartupPlan =
		MakeUT99OpenXRStartupLaunchPlan(openXR != nullptr,
			LaunchInfo.IsUnrealTournament(), LaunchInfo.url);
	if (xrStartupPlan.Active)
	{
		LaunchInfo.noEntryMap = xrStartupPlan.SkipEntryMap;
		LaunchInfo.url = xrStartupPlan.Map;
		openXRStartupMenu.Begin(true);
		LogMessage("OpenXR UT99 startup: loading " + LaunchInfo.url +
			" behind the compiled menu; waiting for focused session input");
	}

	if (engine->LaunchInfo.ue1Version > 219 && !client->StartupFullscreen)
		viewport->bWindowsMouseAvailable() = true;

	window->LockCursor();

	if (packages->IsKlingonHonorGuard())
	{
		PlayAVI({ "playavi", "INTRO.AVI", "N" });
	}

	if (!LaunchInfo.noEntryMap)
	{
	#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
		LogMessage("[asyncify-stage] Entry map begin");
	#endif
		LoadEntryMap();
	#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
		LogMessage("[asyncify-stage] Entry map complete");
	#endif
	}

	#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
	LogMessage("[asyncify-stage] Main map begin");
	#endif
	if (LaunchInfo.url.empty())
		LoadMap(GetDefaultURL(packages->GetIniValue("system", "URL", "LocalMap")));
	else
		LoadMap(UnrealURL(GetDefaultURL(packages->GetIniValue("system", "URL", "LocalMap")), LaunchInfo.url));
	#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
	LogMessage("[asyncify-stage] Main map complete");
	#endif
	startupIntroActive = LaunchInfo.url.empty() &&
		(LaunchInfo.IsUnrealTournament() || LaunchInfo.IsUnreal1());

	#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
	LogMessage("[asyncify-stage] LoginPlayer begin");
	#endif
	LoginPlayer();
	#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
	LogMessage("[asyncify-stage] LoginPlayer complete");
	#endif

	frameObjProp = GC::Alloc<UObjectProperty>(NameString(), nullptr, ObjectFlags::NoFlags);
	frameVecProp = GC::Alloc<UStructProperty>(NameString(), nullptr, ObjectFlags::NoFlags);
	frameRotProp = GC::Alloc<UStructProperty>(NameString(), nullptr, ObjectFlags::NoFlags);
	InstallXRWeaponCallHook();
}

void Engine::Shutdown()
{
	LogMessage("Shutting down...");
	UninstallXRWeaponCallHook();
	window->UnlockCursor();

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
	UpdateOpenXRStartupIntro(nullptr);
	ReleaseOpenXRControllerEvents();
	openXRInput.Disconnect(*this);
	openXRHapticFeedback.Reset();
	if (openXR && render)
		openXRUI.Stop(render->XRUISurfaces(), *openXR);
	openXR.reset();
	openXRViews.ResetRecenter();
	CloseWindow();
}

void Engine::RunOneFrame()
{
	tickCount++;
	OpenXREyeView eyes[2];
	bool xrFrameBegun = false;
	bool shouldRenderXR = false;
	bool submitXRLayer = false;
	bool uiCompositionBegun = false;
	bool acquired[2] = { false, false };
	XRSpaceSamples xrSpaces;
	XRControllerSnapshot xrControllers;
	bool xrGameplayInputEnabled = false;

	if (openXR && openXR->IsSessionReady())
	{
		if (!openXR->PollEvents())
		{
			LogMessage("OpenXR session stopped; continuing in desktop mode");
			UpdateOpenXRStartupIntro(nullptr);
			ReleaseOpenXRControllerEvents();
			openXRInput.Disconnect(*this);
			openXRHapticFeedback.Reset();
			if (render)
				openXRUI.Stop(render->XRUISurfaces(), *openXR);
			openXR.reset();
			openXRViews.ResetRecenter();
		}
		else if (openXR->IsSessionRunning())
		{
			xrFrameBegun = openXR->WaitBeginAndLocate(shouldRenderXR, eyes, xrSpaces);
			if (xrFrameBegun && openXR->SyncInput(xrSpaces, xrControllers))
			{
				UpdateOpenXRStartupIntro(&xrControllers);
				// Both menu routes must consume the same start-of-frame state.
				// An Escape edge can change UMenu immediately; re-reading afterward
				// lets one controller edge close and reopen it in the same frame.
				const bool menuActiveAtInputStart =
					render && render->IsXRUIMenuActive();
				UpdateOpenXRStartupMenu(lastRealTimeElapsed,
					openXR->SessionState(), xrControllers,
					menuActiveAtInputStart);
				const bool gameplayInputEnabled = !menuActiveAtInputStart;
				xrGameplayInputEnabled = gameplayInputEnabled;
				UpdateOpenXRControllerEvents(openXR->SessionState(), xrControllers,
					gameplayInputEnabled, menuActiveAtInputStart);
				openXRInput.Update(openXR->SessionState(), xrControllers, *this,
					gameplayInputEnabled);
				const XRHapticInputContext context = !gameplayInputEnabled ?
					XRHapticInputContext::UserInterface :
					(IsStartupIntroActive() ? XRHapticInputContext::Disabled :
						XRHapticInputContext::Gameplay);
				openXRHapticFeedback.UpdateInput(openXR->SessionState(), xrControllers,
					context, openXR.get());
			}
			else
			{
				UpdateOpenXRStartupIntro(nullptr);
				ReleaseOpenXRControllerEvents();
				openXRInput.Disconnect(*this);
				openXRHapticFeedback.Reset();
			}
		}
		else
		{
			UpdateOpenXRStartupIntro(nullptr);
			ReleaseOpenXRControllerEvents();
			openXRInput.Disconnect(*this);
			openXRHapticFeedback.Reset();
		}
	}

	// XR waits and samples controls before simulation so this snapshot is
	// composed with keyboard/mouse during the same game update. Weapon aim uses
	// the same provider-neutral pose solver in native OpenXR and WebXR.
	const float realTimeElapsed = CalcTimeElapsed();
	if (openXR && xrFrameBegun)
		UpdateOpenXRLocomotion(xrSpaces.Head, xrControllers, realTimeElapsed,
			xrGameplayInputEnabled && openXR->SessionState().AcceptsInput());
	ClearXRWeaponPose();
	XRWeaponPoseResult xrWeaponPose;
	XRWeaponPoseResult xrOffHandWeaponPose;
	XRWorldTransform xrWeaponWorld;
	if (openXR && xrFrameBegun && openXRViews.CreateWeaponWorldTransform(
		CameraLocation, xrWeaponWorld))
	{
		XRWeaponPoseOptions options;
		options.Mirror = xrHandedness.MirrorWeaponPresentation();
		// The first Quest-qualified integration rendered and fired from the aim
		// pose. Using grip here while ballistics used aim visibly split the gun
		// direction from its shots by the runtime's grip-to-aim angular offset.
		options.VisualAnchor = XRWeaponVisualAnchor::Aim;
		xrWeaponPose = SolveXRWeaponPose(xrSpaces, xrWeaponWorld,
			xrHandedness.Dominant, options);
		options.Mirror = xrHandedness.OffHand() == XRHand::Left;
		xrOffHandWeaponPose = SolveXRWeaponPose(xrSpaces, xrWeaponWorld,
			xrHandedness.OffHand(), options);
		UpdateOpenXRWeaponDiagnostics(realTimeElapsed, xrSpaces,
			xrWeaponWorld, xrWeaponPose);
	}
	if (!xrWeaponPose.Valid)
		xrManualSlaveFirePending = false;
	const float levelElapsed = xrWeaponPose.Valid ?
		AdvanceGameFrameWithXRWeaponAim(xrWeaponPose, xrOffHandWeaponPose,
			realTimeElapsed) :
		AdvanceGameFrame(realTimeElapsed);
	viewport->SetViewportRect(0, 0, engine->window->GetPixelWidth(), engine->window->GetPixelHeight());
	render->SetDirectHudPresentation(false);
	ViewFamily viewFamily = CreateDesktopViewFamily();
	if (openXR && xrFrameBegun && shouldRenderXR)
	{
		void* images[2] = { openXR->AcquireSwapchainImage(0), openXR->AcquireSwapchainImage(1) };
		acquired[0] = images[0] != nullptr;
		acquired[1] = images[1] != nullptr;
		PresentationTarget target{ OpenXRProvider::ProjectionTargetSlot };
		PresentationTargetBinding binding;
		binding.Target = target;
		for (void* image : images)
			binding.Images.push_back({ image, openXR->SwapchainWidth(), openXR->SwapchainHeight() });
		auto atlas = CreateStereoAtlasLayout(openXR->SwapchainWidth(),
			openXR->SwapchainHeight());
		ViewFamily xrViews;
		if (atlas)
			xrViews = openXRViews.CreateViewFamily(eyes, CameraLocation,
				CameraRotation, atlas->Atlas);
		if (atlas && xrViews.Views.size() == 2 &&
			render->Device->BindPresentationTarget(binding))
		{
			xrViews.Presentation.SetLayer(PresentationLayer::World, target);
			xrViews.Presentation.SetLayer(PresentationLayer::WeaponOverlay, target);
			viewFamily = std::move(xrViews);
			render->SetDirectHudPresentation(viewFamily.Hud.Enabled);
			submitXRLayer = true;
			if (openXRUI.IsStarted())
			{
				render->UpdateXRUISurfaceVisibility();
				std::array<XRUISurfaceRay, XRHandCount> rays;
				std::array<bool, XRHandCount> rayValid;
				for (size_t hand = 0; hand < XRHandCount; hand++)
				{
					rays[hand] = openXRViews.CreatePointerRay(xrSpaces.Aim[hand], CameraLocation);
					rayValid[hand] = IsValidXRPose(xrSpaces.Aim[hand]);
				}
				openXRUI.Update(render->XRUISurfaces(), viewFamily,
					openXR->SessionState(), xrControllers, rays, rayValid,
					1.0f / 0.0254f, IsStartupIntroActive());
				ResolveXRUIHapticFeedback(openXRHapticFeedback,
					openXRUI.Feedback(), openXR.get());
				const bool hasComposedUI =
					!render->XRUISurfaces().BuildReplayFrame().Items.empty();
				uiCompositionBegun = openXRUI.BeginComposition(
					render->XRUISurfaces(), *openXR,
					openXRViews.CompositionSpace(CameraLocation));
				if (uiCompositionBegun && openXR->SupportsUIVisualOverlay() &&
					!openXRUI.VisualFrame().Hands.empty())
					render->SetXRUIVisualOverlay(openXRUI.VisualFrame(),
						OpenXRProvider::UIVisualTargets);
				if (uiCompositionBegun && hasComposedUI)
				{
					viewFamily.Presentation.SetLayer(PresentationLayer::UserInterface,
						{ OpenXRProvider::ProjectionTargetSlot }, false);
				}
				if (!uiCompositionBegun)
				{
					LogMessage("OpenXR UI frame failed: " + openXR->LastError() +
						"; disabling native UI composition");
					openXRUI.Stop(render->XRUISurfaces(), *openXR);
				}
			}
		}
	}

	RenderGameFrame(levelElapsed, viewFamily);
	if (uiCompositionBegun)
	{
		if (!openXRUI.FinishComposition(*openXR, true))
		{
			LogMessage("OpenXR UI frame release failed: " + openXR->LastError() +
				"; disabling native UI composition");
			openXRUI.Stop(render->XRUISurfaces(), *openXR);
		}
	}
	if (xrFrameBegun)
	{
		for (int eye = 0; eye < 2; eye++)
		{
			if (acquired[eye])
				openXR->ReleaseSwapchainImage(eye);
		}
		openXR->EndFrame(submitXRLayer, eyes);
	}
	FinishGameFrame(levelElapsed);
}

void Engine::UpdateOpenXRStartupIntro(const XRControllerSnapshot* controllers)
{
	const bool menuActive = render && render->IsXRUIMenuActive();
	for (size_t handIndex = 0; handIndex < XRHandCount; handIndex++)
	{
		const InputSourceId source = handIndex == 0 ? InputSourceId::XRLeft : InputSourceId::XRRight;
		XRStartupIntroFireEvent event = controllers ?
			openXRStartupIntroTrigger.Update(source,
				controllers->Hands[handIndex].Connected && controllers->Hands[handIndex].Select.Pressed,
				IsStartupIntroActive(), menuActive) :
			openXRStartupIntroTrigger.ReleaseSource(source);
		if (!event)
			continue;
		const EInputKey key = event.Control == XRStartupIntroFireControl::Primary ?
			IK_LeftMouse : IK_RightMouse;
		InputEvent(key, event.Pressed ? EInputType::IST_Press : EInputType::IST_Release,
			0.0f, source);
	}
}

void Engine::SetXRDominantHand(XRHand hand)
{
	if (xrHandedness.Dominant != hand)
	{
		ReleaseOpenXRControllerEvents();
		openXRInput.Disconnect(*this);
	}
	xrHandedness.Dominant = hand;
	openXRInput.SetBindings(NativeOpenXRInputBindings(hand));
	openXRStartupIntroTrigger.SetDominantHand(hand);
	openXRUI.SetPointerHand(hand);
}

void Engine::ApplyOpenXRControllerEvents(
	const std::vector<XRNativeKeyEvent>& events)
{
	for (const XRNativeKeyEvent& event : events)
	{
		const InputSourceId source = event.Hand == XRHand::Left ?
			InputSourceId::XRLeft : InputSourceId::XRRight;
		if (event.Kind == XRNativeKeyEventKind::EscapePulse)
		{
			InputEvent(IK_Escape, EInputType::IST_Press, 0.0f, source);
			InputEvent(IK_Escape, EInputType::IST_Release, 0.0f, source);
			continue;
		}
		if (event.Kind == XRNativeKeyEventKind::AlternateFire)
		{
			UPlayerPawn* pawn = viewport ? viewport->Actor() : nullptr;
			UWeapon* master = pawn ? pawn->Weapon() : nullptr;
			UWeapon* slave = GetXRSecondaryWeapon(master);
			const auto action = XRWeaponRuntime::ResolvePairedOffHandTrigger(
				slave != nullptr, event.Pressed, xrAlternateFireKeyDown);
			if (action == XRWeaponRuntime::PairedOffHandTriggerAction::FireSlave)
			{
				xrManualSlaveFirePending = true;
				continue;
			}
			if (action == XRWeaponRuntime::PairedOffHandTriggerAction::ConsumeRelease)
				continue;
		}
		const EInputKey key = event.Kind == XRNativeKeyEventKind::PrimaryFire ?
			IK_LeftMouse : IK_RightMouse;
		InputEvent(key, event.Pressed ? EInputType::IST_Press :
			EInputType::IST_Release, 0.0f, source);
		if (event.Kind == XRNativeKeyEventKind::AlternateFire)
			xrAlternateFireKeyDown = event.Pressed;
	}
}

void Engine::DispatchPendingXRSlaveFire()
{
	if (!xrManualSlaveFirePending)
		return;
	xrManualSlaveFirePending = false;
	UPlayerPawn* pawn = viewport ? viewport->Actor() : nullptr;
	UWeapon* master = pawn ? pawn->Weapon() : nullptr;
	UWeapon* slave = GetXRSecondaryWeapon(master);
	if (!slave || !xrOffHandWeaponPose.Valid)
		return;

	struct ScopedManualSlaveFire
	{
		explicit ScopedManualSlaveFire(bool& active)
			: Active(active), Saved(active) { Active = true; }
		~ScopedManualSlaveFire() { Active = Saved; }
		bool& Active;
		bool Saved;
	} manualFire(xrManualSlaveFireActive);
	LogMessage("[openxr-dual-enforcer] off-hand trigger fired slave only");
	CallEvent(slave, NameString("Fire"));
}

void Engine::UpdateOpenXRControllerEvents(const XRSessionState& session,
	const XRControllerSnapshot& controllers, bool gameplayInputEnabled,
	bool menuActive)
{
	ApplyOpenXRControllerEvents(openXRControllerEvents.Update(session,
		controllers, xrHandedness.Dominant, gameplayInputEnabled,
		IsStartupIntroActive(), menuActive));
}

void Engine::ReleaseOpenXRControllerEvents()
{
	ApplyOpenXRControllerEvents(openXRControllerEvents.Release(
		xrHandedness.Dominant));
	xrManualSlaveFirePending = false;
	xrAlternateFireKeyDown = false;
}

void Engine::UpdateOpenXRStartupMenu(float elapsedSeconds,
	const XRSessionState& session, const XRControllerSnapshot& controllers,
	bool menuActive)
{
	const XRStartupMenuActions startup = openXRStartupMenu.Update(
		elapsedSeconds, session, menuActive);
	if (startup.PrimaryFirePulse)
	{
		const InputSourceId source = xrHandedness.Dominant == XRHand::Left ?
			InputSourceId::XRLeft : InputSourceId::XRRight;
		InputEvent(IK_LeftMouse, EInputType::IST_Press, 0.0f,
			source);
		InputEvent(IK_LeftMouse, EInputType::IST_Release, 0.0f,
			source);
	}
	if (startup.EscapePulse)
	{
		InputEvent(IK_Escape, EInputType::IST_Press, 0.0f,
			InputSourceId::XRRight);
		InputEvent(IK_Escape, EInputType::IST_Release, 0.0f,
			InputSourceId::XRRight);
	}

	auto inputKey = [](XRMenuNavigationKey key)
	{
		switch (key)
		{
		case XRMenuNavigationKey::Up: return IK_Up;
		case XRMenuNavigationKey::Down: return IK_Down;
		case XRMenuNavigationKey::Left: return IK_Left;
		case XRMenuNavigationKey::Right: return IK_Right;
		case XRMenuNavigationKey::Enter: return IK_Enter;
		default: return IK_Escape;
		}
	};
	for (XRMenuNavigationKey key : openXRMenuNavigation.Update(
		elapsedSeconds, session, controllers, menuActive))
	{
		const EInputKey engineKey = inputKey(key);
		InputEvent(engineKey, EInputType::IST_Press, 0.0f,
			InputSourceId::XRRight);
		InputEvent(engineKey, EInputType::IST_Release, 0.0f,
			InputSourceId::XRRight);
	}
}

void Engine::UpdateOpenXRLocomotion(const XRPose& headPose,
	const XRControllerSnapshot& controllers, float realTimeElapsed,
	bool gameplayInputEnabled)
{
	if (!gameplayInputEnabled || IsStartupIntroActive() ||
		(render && render->IsXRUIMenuActive()) || !viewport)
		return;
	UPlayerPawn* pawn = viewport->Actor();
	if (!pawn || !IsValidXRPose(headPose))
		return;

	Rotator headRotation;
	if (!openXRViews.CreateHeadRotation(headPose, pawn->Rotation(), headRotation))
		return;
	const XRHandControllerState& turnHand = controllers.ForHand(XRHand::Right);
	const float rawTurn = turnHand.Connected ? turnHand.Thumbstick.X : 0.0f;
	const float turn = std::fabs(rawTurn) < 0.15f ? 0.0f : rawTurn;
	if (turn != 0.0f)
	{
		openXRViews.ApplyYawTurn(-turn * radians(120.0f) * realTimeElapsed);
		openXRViews.CreateHeadRotation(headPose, pawn->Rotation(), headRotation);
	}

	// Movement scripts derive their axes from ViewRotation. Keep the upright
	// pawn body and its view yaw aligned to the tracked head; pitch belongs on
	// ViewRotation only so the collision cylinder never tilts.
	pawn->Rotation().Yaw = headRotation.Yaw;
	pawn->ViewRotation().Yaw = headRotation.Yaw;
	pawn->ViewRotation().Pitch = headRotation.Pitch;
}

void Engine::UpdateOpenXRWeaponDiagnostics(float elapsedSeconds,
	const XRSpaceSamples& spaces, const XRWorldTransform& worldTransform,
	const XRWeaponPoseResult& pose)
{
	openXRWeaponDiagnosticTime += std::max(elapsedSeconds, 0.0f);
	if (!pose.Valid || openXRWeaponDiagnosticTime < 2.0f)
		return;
	openXRWeaponDiagnosticTime = 0.0f;

	UPlayerPawn* pawn = viewport ? viewport->Actor() : nullptr;
	UWeapon* weapon = pawn ? pawn->Weapon() : nullptr;
	const XREnginePose grip = TransformXRPoseToEngine(
		spaces.GripFor(xrHandedness.Dominant), worldTransform);
	const XREnginePose aim = TransformXRPoseToEngine(
		spaces.AimFor(xrHandedness.Dominant), worldTransform);
	XRWeaponPoseOptions gripOptions;
	gripOptions.VisualAnchor = XRWeaponVisualAnchor::Grip;
	const XRWeaponPoseResult gripResult = SolveXRWeaponPose(
		grip, aim, xrHandedness.Dominant, gripOptions);

	auto asVector = [](const XREngineVector3& value)
	{
		return vec3(value.X, value.Y, value.Z);
	};
	auto angularDelta = [](const vec3& left, const vec3& right)
	{
		if (length(left) < 0.0001f || length(right) < 0.0001f)
			return -1.0f;
		const float cosine = std::clamp(dot(normalize(left), normalize(right)),
			-1.0f, 1.0f);
		return degrees(std::acos(cosine));
	};
	auto xyz = [](const XREngineVector3& value)
	{
		return "(" + std::to_string(value.X) + "," +
			std::to_string(value.Y) + "," + std::to_string(value.Z) + ")";
	};

	const vec3 visualDirection = asVector(pose.VisualForward);
	const vec3 aimDirection = asVector(pose.AimDirection);
	const float visualAimDelta = angularDelta(visualDirection, aimDirection);
	const float gripAimDelta = gripResult.Valid ?
		angularDelta(asVector(gripResult.VisualForward), aimDirection) : -1.0f;
	const float viewAimDelta = pawn ? angularDelta(
		Coords::Rotation(pawn->ViewRotation()).XAxis, aimDirection) : -1.0f;
	const Rotator visualRotation = normalize(Rotator::FromVector(visualDirection));
	const Rotator ballisticRotation = normalize(Rotator::FromVector(aimDirection));
	const std::string weaponName = weapon && weapon->Class ?
		weapon->Class->Name.ToString() : "none";

	LogMessage("[openxr-weapon] weapon=" + weaponName +
		" hand=" + std::string(xrHandedness.Dominant == XRHand::Left ? "left" : "right") +
		" anchor=aim visual_pos=" + xyz(pose.VisualPose.Position) +
		" grip_pos=" + xyz(grip.Position) + " aim_pos=" + xyz(aim.Position) +
		" visual_dir=" + xyz(pose.VisualForward) +
		" aim_dir=" + xyz(pose.AimDirection) +
		" visual_yaw=" + std::to_string(visualRotation.YawDegrees()) +
		" visual_pitch=" + std::to_string(visualRotation.PitchDegrees()) +
		" ballistic_yaw=" + std::to_string(ballisticRotation.YawDegrees()) +
		" ballistic_pitch=" + std::to_string(ballisticRotation.PitchDegrees()) +
		" visual_aim_delta_deg=" + std::to_string(visualAimDelta) +
		" grip_aim_delta_deg=" + std::to_string(gripAimDelta) +
		" pawn_view_aim_delta_deg=" + std::to_string(viewAimDelta));
}

float Engine::AdvanceGameFrame()
{
	return AdvanceGameFrame(CalcTimeElapsed());
}

float Engine::AdvanceGameFrame(float realTimeElapsed)
{
	// Tick everything once. Rendering is deliberately kept in a separate phase so
	// alternate frame loops can schedule presentation independently.
	lastRealTimeElapsed = realTimeElapsed;
#ifdef __EMSCRIPTEN__
	if (browserCinematic)
	{
		AdvanceBrowserCinematic(realTimeElapsed);
		return 0.0f;
	}
#endif
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
		frameVecProp->Struct = UObject::Cast<UStructProperty>(funcPlayerCalcView->Properties[1])->Struct;
		frameRotProp->Struct = UObject::Cast<UStructProperty>(funcPlayerCalcView->Properties[2])->Struct;
		CameraActor = viewport->Actor();
		CameraLocation = viewport->Actor()->Location();
		CameraRotation = viewport->Actor()->Rotation();
		CameraFovAngle = viewport->Actor()->FovAngle();
		CallEvent(viewport->Actor(), EventName::PlayerCalcView, {
			ExpressionValue::Variable(&CameraActor, frameObjProp),
			ExpressionValue::Variable(&CameraLocation, frameVecProp),
			ExpressionValue::Variable(&CameraRotation, frameRotProp)
			});
	}

	UpdateAudio();
	return levelElapsed;
}

float Engine::AdvanceGameFrameWithXRWeaponAim(const XRWeaponPoseResult& pose)
{
	return AdvanceGameFrameWithXRWeaponAim(pose, CalcTimeElapsed());
}

float Engine::AdvanceGameFrameWithXRWeaponAim(const XRWeaponPoseResult& pose,
	float realTimeElapsed)
{
	return AdvanceGameFrameWithXRWeaponAim(pose, {}, realTimeElapsed);
}

float Engine::AdvanceGameFrameWithXRWeaponAim(const XRWeaponPoseResult& pose,
	const XRWeaponPoseResult& offHandPose, float realTimeElapsed)
{
	ClearXRWeaponPose();
	UPlayerPawn* pawn = viewport ? viewport->Actor() : nullptr;
	UWeapon* weapon = pawn ? pawn->Weapon() : nullptr;
	const bool menuActive = render && render->IsXRUIMenuActive();
	bool cinematicActive = false;
#ifdef __EMSCRIPTEN__
	cinematicActive = browserCinematic != nullptr;
#endif
	if (!pose.Valid || menuActive || cinematicActive || !pawn || !weapon ||
		weapon->Owner() != pawn)
	{
		xrManualSlaveFirePending = false;
		return AdvanceGameFrame(realTimeElapsed);
	}

	const vec3 aimDirection(pose.AimDirection.X, pose.AimDirection.Y,
		pose.AimDirection.Z);
	if (!std::isfinite(aimDirection.x) || !std::isfinite(aimDirection.y) ||
		!std::isfinite(aimDirection.z) || length(aimDirection) < 0.0001f)
	{
		xrManualSlaveFirePending = false;
		return AdvanceGameFrame(realTimeElapsed);
	}

	XRWeaponPoseResult validatedOffHandPose;
	const vec3 offHandAim(offHandPose.AimDirection.X, offHandPose.AimDirection.Y,
		offHandPose.AimDirection.Z);
	if (offHandPose.Valid && std::isfinite(offHandAim.x) &&
		std::isfinite(offHandAim.y) && std::isfinite(offHandAim.z) &&
		length(offHandAim) >= 0.0001f)
		validatedOffHandPose = offHandPose;
	SetXRWeaponPoses(pose, validatedOffHandPose);
	DispatchPendingXRSlaveFire();
	return AdvanceGameFrame(realTimeElapsed);
}

UWeapon* Engine::GetXRSecondaryWeapon(UWeapon* master)
{
	if (!master || !master->Class || master->Class->Name != NameString("Enforcer"))
		return nullptr;

	if (xrEnforcerClass != master->Class)
	{
		xrEnforcerClass = master->Class;
		xrEnforcerSlaveOffset = master->GetPropertyDataOffset("SlaveEnforcer");
		LogMessage(std::string("[openxr-dual-enforcer] SlaveEnforcer property ") +
			(xrEnforcerSlaveOffset.DataOffset == ~(size_t)0 ? "not found" : "resolved"));
	}
	if (xrEnforcerSlaveOffset.DataOffset == ~(size_t)0)
		return nullptr;

	UWeapon* slave = UObject::TryCast<UWeapon>(
		master->Value<UObject*>(xrEnforcerSlaveOffset));
	if (!slave || slave == master || slave->Owner() != master->Owner())
		return nullptr;
	return slave;
}

const XRWeaponPoseResult* Engine::GetXRWeaponPoseForActor(UActor* actor)
{
	UPlayerPawn* pawn = viewport ? viewport->Actor() : nullptr;
	UWeapon* master = pawn ? pawn->Weapon() : nullptr;
	if (!actor || !master || master->Owner() != pawn)
		return nullptr;
	return SelectXRWeaponActorPose(actor, master, xrWeaponPose,
		GetXRSecondaryWeapon(master), xrOffHandWeaponPose);
}

void Engine::InstallXRWeaponCallHook()
{
	if (xrWeaponCallHook || !LaunchInfo.IsUnrealTournament())
		return;

	xrWeaponCallHook = Frame::CallHooks().Register(
		XRWeaponRuntime::MakeUT99WeaponCallHook(
			[this](UFunction* function, UObject* instance)
				-> std::optional<XRWeaponRuntime::ScopeRequest>
			{
				if (!xrWeaponPose.Valid || !function || !instance || !viewport)
					return {};

				UPlayerPawn* pawn = viewport->Actor();
				UWeapon* weapon = pawn ? pawn->Weapon() : nullptr;
				if (!pawn || !weapon || weapon->Owner() != pawn)
					return {};

				UWeapon* secondaryWeapon = GetXRSecondaryWeapon(weapon);
				UActor* callActor = UObject::TryCast<UActor>(instance);
				const bool pulseBeamTick = callActor && callActor->Class &&
					weapon->Class && weapon->Class->Name == NameString("PulseGun") &&
					callActor->Class->Name == NameString("StarterBolt") &&
					callActor->Instigator() == pawn && function->Name == "Tick";
				const bool pawnAimCall = instance == pawn &&
					(function->Name == "AdjustAim" || function->Name == "AdjustToss");
				if (instance != weapon && instance != secondaryWeapon &&
					!pawnAimCall && !pulseBeamTick)
					return {};

				const bool slaveCall = secondaryWeapon && instance == secondaryWeapon;
				UWeapon* scopedWeapon = slaveCall ? secondaryWeapon : weapon;
				XRWeaponRuntime::ScopeRequest request;
				UObject* callClassSource = pulseBeamTick ? instance : scopedWeapon;
				if (callClassSource->Class && callClassSource->Class->package)
					request.Call.PackageName = callClassSource->Class->package->GetPackageName().ToString();
				if (callClassSource->Class)
					request.Call.ClassName = callClassSource->Class->Name.ToString();
				request.Call.ActiveStateName = instance->GetStateName().ToString();
				request.Call.FunctionName = function->Name.ToString();
				UObject* outer = function->Outer();
				if (UState* state = UObject::TryCast<UState>(outer))
				{
					if (!UObject::TryCast<UClass>(outer))
						request.Call.DeclaringStateName = state->Name.ToString();
				}

				const bool slaveBallisticCall = slaveCall &&
					(function->Name == "TraceFire" || function->Name == "ProjectileFire");
				if (slaveBallisticCall && !xrManualSlaveFireActive)
				{
					request.SuppressDispatch = true;
					static uint64_t suppressedEchoCount = 0;
					if ((suppressedEchoCount++ % 4) == 0)
						LogMessage("[openxr-dual-enforcer] suppressed stock slave echo");
					return request;
				}

				const XRWeaponPoseResult* pose = &xrWeaponPose;
				if (slaveCall)
				{
					if (!xrOffHandWeaponPose.Valid)
						return {};
					pose = &xrOffHandWeaponPose;
					scopedWeapon = secondaryWeapon;
					static bool loggedSlaveAim = false;
					if (!loggedSlaveAim && (function->Name == "TraceFire" ||
						function->Name == "ProjectileFire"))
					{
						loggedSlaveAim = true;
						LogMessage("[openxr-dual-enforcer] slave fire routed to off-hand aim");
					}
				}
				else if (pawnAimCall && secondaryWeapon && xrOffHandWeaponPose.Valid)
				{
					const vec3 secondaryAim(xrOffHandWeaponPose.AimDirection.X,
						xrOffHandWeaponPose.AimDirection.Y,
						xrOffHandWeaponPose.AimDirection.Z);
					const Rotator secondaryRotation = normalize(
						Rotator::FromVector(normalize(secondaryAim)));
					if (pawn->ViewRotation() == secondaryRotation)
					{
						pose = &xrOffHandWeaponPose;
						scopedWeapon = secondaryWeapon;
					}
				}

				const vec3 aimDirection(pose->AimDirection.X,
					pose->AimDirection.Y, pose->AimDirection.Z);
				if (!std::isfinite(aimDirection.x) || !std::isfinite(aimDirection.y) ||
					!std::isfinite(aimDirection.z) || length(aimDirection) < 0.0001f)
					return {};

				request.Targets = { &pawn->ViewRotation(), &scopedWeapon->Rotation() };
				request.Transforms.BallisticRotationValid = true;
				request.Transforms.BallisticRotation = normalize(
					Rotator::FromVector(normalize(aimDirection)));
				if (pulseBeamTick)
				{
					static bool loggedPulseBeamAim = false;
					if (!loggedPulseBeamAim)
					{
						loggedPulseBeamAim = true;
						LogMessage("[openxr-pulse-beam] StarterBolt.Tick routed to controller aim");
					}
				}
				return request;
			}));
}

void Engine::UninstallXRWeaponCallHook()
{
	if (!xrWeaponCallHook)
		return;
	Frame::CallHooks().Unregister(xrWeaponCallHook);
	xrWeaponCallHook = 0;
}

void Engine::RenderGameFrame(float levelElapsed)
{
	viewport->SetViewportRect(0, 0, engine->window->GetPixelWidth(), engine->window->GetPixelHeight());
	RenderGameFrame(levelElapsed, CreateDesktopViewFamily());
}

void Engine::RenderGameFrame(float levelElapsed, const ViewFamily& viewFamily)
{
#ifdef __EMSCRIPTEN__
	if (browserCinematic)
	{
		FTextureInfo* frame = browserCinematic->FrameTexture();
		if (frame)
			render->DrawVideoFrame(frame, nullptr, viewFamily.Presentation);
		return;
	}
#endif
	render->DrawGame(levelElapsed, viewFamily);
}

ViewFamily Engine::CreateDesktopViewFamily() const
{
	ViewDescription view;
	view.Location = CameraLocation;
	view.Rotation = Coords::Rotation(CameraRotation);
	view.WorldToView = Coords::ViewToRenderDev().ToMatrix() * view.Rotation.Inverse().ToMatrix() * Coords::Location(view.Location).ToMatrix();
	view.Viewport.X = viewport->ViewportX();
	view.Viewport.Y = viewport->ViewportY();
	view.Viewport.Width = viewport->ViewportWidth();
	view.Viewport.Height = viewport->ViewportHeight();
	view.FovAngle = CameraFovAngle;
	view.ApplyGameViewport = true;

	ViewFamily family;
	family.Views.push_back(view);
	return render->ShowMultiViewDiagnostic ? CreateSideBySideDiagnosticViewFamily(view) : family;
}

void Engine::FinishGameFrame(float levelElapsed)
{
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
			// LoginPlayer only transfers travel actors when ClientTravelInfo.TravelType is
			// TRAVEL_Relative, and TravelType is otherwise assigned only in ClientTravel. These
			// NextURL routes never go through ClientTravel, so without setting it here they
			// inherit whatever the last ClientTravel left behind (or TRAVEL_Absolute, the
			// default) - which silently discarded the inventory the bNextItems branch exists
			// specifically to carry. State the intent explicitly on each branch instead of
			// depending on leftover state.
			if (UnrealURL(LevelInfo->NextURL()).HasOption("restart"))
			{
				// Passes the level's own TravelInfo back in, so it means to preserve it.
				ClientTravelInfo.TravelType = ETravelType::TRAVEL_Relative;
				LoadMap(LevelInfo->URL, Level->TravelInfo);
				LoginPlayer();
			}
			else if (LevelInfo->bNextItems())
			{
				ClientTravelInfo.TravelType = ETravelType::TRAVEL_Relative;
				LoadMap(UnrealURL(LevelInfo->URL, LevelInfo->NextURL()), CreateTravelInfo(true));
				LoginPlayer();
			}
			else
			{
				// Deliberately carries nothing - it passes no travel info at all.
				ClientTravelInfo.TravelType = ETravelType::TRAVEL_Absolute;
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
		PossessSavedPlayer();
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

void Engine::RunHeadlessDriver(const std::string& driverName)
{
	HeadlessDriverRegistry::Resolution resolution = GetHeadlessDriverRegistry().Resolve(driverName);
	if (!resolution)
	{
		LogMessage(resolution.Error);
		m_RunExitCode = resolution.ExitCode;
		return;
	}

	try
	{
		std::unique_ptr<HeadlessDriver> driver = resolution.Create(*this);
		LogMessage("Running headless driver: " + driverName);
		m_RunExitCode = HeadlessDriverRunner().Run(*driver);
		LogMessage("Headless driver complete with exit code " + std::to_string(m_RunExitCode));
	}
	catch (const std::exception& e)
	{
		LogMessage("Headless driver failed: " + std::string(e.what()));
		m_RunExitCode = 3;
	}
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

#ifdef __EMSCRIPTEN__
	try
	{
		FinishBrowserCinematic();
		browserCinematic = std::make_unique<BrowserCinematicPlayback>(packages->GetVideoFilename(video));
		playingAvi = true;
		skipAvi = false;
		render->XRUISurfaces().SetCinematicActive(true);
		audiodev->SetViewport(nullptr);
		audiodev->GetDevice()->PlayMusic(browserCinematic->TakeAudio());
		if (!buildup.empty() || !breakdown.empty())
			LogMessage("Browser cinematic playback omits Klingon Honor Guard buildup/breakdown transitions");
	}
	catch (const std::exception& e)
	{
		LogMessage("Error preparing " + video + ": " + e.what());
		FinishBrowserCinematic();
	}
	return;
#endif

	playingAvi = true;
	skipAvi = false;
	render->XRUISurfaces().SetCinematicActive(true);
	struct CinematicVisibilityGuard
	{
		XRUISurfaceEngineBinding& Binding;
		~CinematicVisibilityGuard() { Binding.SetCinematicActive(false); }
	} cinematicVisibilityGuard{ render->XRUISurfaces() };

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

#ifdef __EMSCRIPTEN__
bool Engine::AdvanceBrowserCinematic(float elapsedSeconds)
{
	if (!browserCinematic)
		return false;
	if (quit || skipAvi)
	{
		FinishBrowserCinematic();
		return false;
	}

	try
	{
		if (browserCinematic->Advance(elapsedSeconds) != VideoFrameStep::Finished)
		{
			audiodev->GetDevice()->Update();
			return true;
		}
	}
	catch (const std::exception& e)
	{
		LogMessage("Browser cinematic playback failed: " + std::string(e.what()));
	}

	FinishBrowserCinematic();
	return false;
}

void Engine::FinishBrowserCinematic()
{
	if (audiodev && audiodev->GetDevice())
		audiodev->GetDevice()->PlayMusic(nullptr);
	if (render)
		render->XRUISurfaces().SetCinematicActive(false);
	browserCinematic.reset();
	playingAvi = false;
	skipAvi = false;
	CalcTimeElapsed();
}
#endif

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
	startupIntroActive = false;
	openXRViews.ResetRecenter();
	ClientTravelInfo.URL.Clear();

	if (Level)
		CallEvent(console, EventName::NotifyLevelChange);

	if (url.HasOption("entry")) // Not sure what the purpose of this kind of travel is - do nothing for now.
		return;
	XRUILoadingSurfaceScope loadingSurface(render ? &render->XRUISurfaces() : nullptr);

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

		if (engine->LaunchInfo.HasCapability(GameCapability::PostPostBeginPlayEvent))
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
	startupIntroActive = false;
	openXRViews.ResetRecenter();
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
	XRUILoadingSurfaceScope loadingSurface(render ? &render->XRUISurfaces() : nullptr);

	audiodev->StopSounds();
	UnloadMap();

	LevelPackage = savefilePackage;

	GetLevelInfoObject();

	// Same as LoadMap: these are session/engine identity, not save data, and must be
	// re-established on every load regardless of what the package/save file contains.
	LevelInfo->ComputerName() = "MyComputer";
	LevelInfo->HubStackLevel() = 0; // To do: handle level hubs
	LevelInfo->EngineVersion() = LaunchInfo.gameVersionString + " SE";
	if (LaunchInfo.ue1Version > 219)
		LevelInfo->MinNetVersion() = LaunchInfo.gameVersionString + " SE";
	LevelInfo->bHighDetailMode() = true;
	LevelInfo->NetMode() = 0; // NM_StandAlone
	LevelInfo->DefaultTexture() = engine->DefaultTexture;

	// LevelInfo->URL is a native engine field, never a serialized script property, so it is
	// never restored by loading the save package and must be rebuilt here.
	LevelInfo->URL = UnrealURL(LevelPackage->GetPackageName().ToString());

	GetLevelObject();

	LinkActorsToLevel();

	// BUG-006: engine->GameInfo is otherwise only assigned in LoadMap, so without this it keeps
	// pointing at the previous, now-unloaded level's GameInfo. LevelInfo->Game() is a normal
	// script property, so it round-trips through the save correctly; just re-point at it.
	GameInfo = UObject::Cast<UGameInfo>(LevelInfo->Game());
	if (!GameInfo)
		Exception::Throw("Save file has no GameInfo actor for " + LevelPackage->GetPackageName().ToString() + "!");
}

void Engine::PossessSavedPlayer()
{
	// Loading a save must not reuse LoginPlayer, because that always calls GameInfo.Login,
	// which always spawns a brand new pawn. The save package already contains the actual saved
	// pawn - deserialized with its real position, health and inventory - sitting in
	// Level->Actors. Find and possess that one directly instead.
	UPlayerPawn* pawn = nullptr;
	for (UActor* actor : Level->Actors)
	{
		UPlayerPawn* p = UObject::TryCast<UPlayerPawn>(actor);
		if (p && p->bIsPlayer())
		{
			pawn = p;
			break;
		}
	}

	if (!pawn)
		Exception::Throw("Save file has no player pawn for " + LevelPackage->GetPackageName().ToString() + "!");

	if (auto pawnExt = UObject::TryCast<UPlayerPawnExt>(pawn))
	{
		// FlagBase is Transient (not saved), so it needs the same reconstruction LoginPlayer
		// does for a fresh spawn.
		if (!pawnExt->FlagBase())
		{
			auto flagBaseCls = packages->FindClass("Extension.FlagBase");
			pawnExt->FlagBase() = UObject::Cast<UFlagBase>(packages->GetTransientPackage()->NewObject("FlagBase", flagBaseCls, ObjectFlags::Transient));
		}
	}

	viewport->Actor() = pawn;
	viewport->Actor()->Player() = viewport;
	CallEvent(viewport->Actor(), EventName::Possess);

	// Headless drivers intentionally do not construct a renderer.
	if (render)
		render->OnMapLoaded();
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
		dxSaveInfoPackage.get()->Save(dxSaveInfo, saveInfoFullPath);
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

	// Headless drivers intentionally do not construct a renderer.
	if (render)
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
	else if (command == "multiviewdiagnostic" && args.size() == 2)
	{
		render->ShowMultiViewDiagnostic = args[1] == "1";
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
		if (LaunchInfo.IsHarryPotter1()) // FEOptionsPage.IsSupportedResolution is so sad :)
			return "1024x768";
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
	std::string invertMouse = packages->GetIniValue("user", "Engine.PlayerPawn", "bInvertMouse");
	if (DesktopInputDefaults::ApplyModernControls(keybindings, invertMouse))
	{
		for (const char* key : { "W", "A", "S", "D" })
			packages->SetIniValue("user", "Engine.Input", key, keybindings[key]);
		packages->SetIniValue("user", "Engine.PlayerPawn", "bInvertMouse", invertMouse);
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

	for (const auto& it : inputComposition.Buttons())
		viewport->Actor()->SetBool(it.first, !it.second.empty());
	for (const auto& it : inputComposition.Axes())
	{
		float value = inputComposition.GetAxisValue(it.first);
		if (it.first == "aMouseX" || it.first == "aMouseY")
		{
			viewport->Actor()->SetFloat(it.first, value / (timeElapsed * 150.0f));
		}
		else
		{
			viewport->Actor()->SetFloat(it.first, value);
		}
	}
}

void Engine::OpenWindow()
{
	if (!window)
		window = GameWindow::Create(this, openXR.get());

	int width = client->StartupFullscreen ? client->FullscreenViewportX : client->WindowedViewportX;
	int height = client->StartupFullscreen ? client->FullscreenViewportY : client->WindowedViewportY;
#ifdef __EMSCRIPTEN__
	// Never request the browser Fullscreen API, even if StartupFullscreen=True
	// in the ini (UT99's shipped default). Two reasons: (1) it requires a user
	// gesture, which --autoplay boot never has, so the request itself is a
	// no-op/rejected promise; (2) it's the trigger for a real dangling-pointer
	// bug in Emscripten's bundled SDL2 port - Emscripten_SetWindowFullscreen()
	// (SDL_emscriptenvideo.c) hands the SDL_WindowData* to libhtml5.js's
	// registerRestoreOldStyle(), which installs a *document*-level
	// 'fullscreenchange' listener (restoreOldStyle) that SDL's own
	// Emscripten_UnregisterEventHandlers() doesn't know about and never
	// removes. If that listener fires after SDL_DestroyWindow() has already
	// freed window->driverdata, restoreOldStyle() calls back into
	// Emscripten_HandleCanvasResize() with the freed pointer, which reads the
	// now-garbage canvas_id field and passes it to
	// emscripten_get_element_css_size() -> findEventTarget() ->
	// document.querySelector() with a garbage string, throwing an uncaught
	// SyntaxError. Confirmed via a -sSAFE_HEAP=1 -g2 scratch build. A browser
	// shell can request fullscreen later in response to a user gesture.
	bool fullscreen = false;
#else
	bool fullscreen = client->StartupFullscreen;
#endif

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

#ifdef __EMSCRIPTEN__
	const BrowserRelativeMouseDelta browserMouseMotion = BrowserRelativeMouseMotion.Drain();
	if (browserMouseMotion.X != 0 || browserMouseMotion.Y != 0)
		OnWindowRawMouseMove(browserMouseMotion.X, browserMouseMotion.Y);

	// Escape is reserved by the browser while pointer lock is active. The page
	// reports the corresponding lock-loss intent here so it enters the same
	// native intro/menu path after ordinary SDL events have had first chance.
	if (BrowserEscapeIntentPending.exchange(false, std::memory_order_acq_rel))
	{
		OnWindowKeyDown(IK_Escape);
		OnWindowKeyUp(IK_Escape);
	}
#endif

	if (MouseMoveX != 0 || MouseMoveY != 0)
	{
		int dx = MouseMoveX;
		int dy = MouseMoveY;
		MouseMoveX = 0;
		MouseMoveY = 0;

		// Send to input subsystem.
		if (dx)
			InputEvent(IK_MouseX, IST_Axis, static_cast<float>(dx));
		if (dy)
			InputEvent(IK_MouseY, IST_Axis, static_cast<float>(-dy));
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
#ifdef __EMSCRIPTEN__
	// If SDL did deliver the physical Escape after all, suppress the queued
	// pointer-lock-loss fallback rather than sending a duplicate press.
	if (key == EInputKey::IK_Escape)
		BrowserEscapeIntentPending.store(false, std::memory_order_release);
#endif
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

void Engine::InputEvent(EInputKey key, EInputType type, float delta, InputSourceId source)
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
					InputCommand(it->second, { source, static_cast<int32_t>(key) }, delta);
				}
				else
				{
					InputCommand(command, { source, static_cast<int32_t>(key) }, delta);
				}
			}
		}
		else if (type == EInputType::IST_Release)
		{
			ReleasedInputActions released = inputComposition.ReleaseControl({ source, static_cast<int32_t>(key) });
			for (const std::string& action : released.Buttons)
				viewport->Actor()->SetBool(action, false);
			for (const std::string& action : released.Axes)
				viewport->Actor()->SetFloat(action, 0.0f);
		}
	}
}

void Engine::ReleaseInputSource(InputSourceId source)
{
	ReleasedInputActions released = inputComposition.ReleaseSource(source);
	if (!viewport || !viewport->Actor())
		return;
	for (const std::string& action : released.Buttons)
		viewport->Actor()->SetBool(action, false);
	for (const std::string& action : released.Axes)
		viewport->Actor()->SetFloat(action, 0.0f);
}

void Engine::ReleaseInputControl(InputControlId control)
{
	ReleasedInputActions released = inputComposition.ReleaseControl(control);
	if (!viewport || !viewport->Actor())
		return;
	for (const std::string& action : released.Buttons)
		viewport->Actor()->SetBool(action, false);
	for (const std::string& action : released.Axes)
		viewport->Actor()->SetFloat(action, 0.0f);
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

void Engine::InputCommand(const std::string& commands, InputControlId control, float delta)
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
				inputComposition.SetButton(args[1], control);
			}
			else if (command == "axis" && args.size() == 3)
			{
				float speed = 1.0f;
				if (args[2].size() > 6 && args[2].substr(0, 6) == "Speed=")
					speed = (float)std::atof(args[2].substr(6).c_str());
				inputComposition.SetAxis(args[1], control, speed * delta);
			}
			else
			{
				std::string keyName = args.size() == 2 ? args[1] : std::string();
				for (char& c : keyName) c = std::tolower(c);
				if (command == "keypulse" && keyName == "escape")
				{
					InputEvent(IK_Escape, EInputType::IST_Press, 0.0f,
						control.Source);
					InputEvent(IK_Escape, EInputType::IST_Release, 0.0f,
						control.Source);
				}
				else
				{
					ExecCommand(args);
				}
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

	// BasedActors (who is standing on me, so I carry them when I move) is native, runtime-only
	// state - never serialized - while ActorBase() (what am I standing on) is a normal property
	// that does round-trip through a package/save. Without this, an actor that starts a level (or
	// a load) already resting on a mover has its own ActorBase() pointer intact, but the mover's
	// own BasedActors list is empty, so the mover has no way to carry it along once it moves.
	for (UActor* actor : Level->Actors)
	{
		if (actor)
			actor->RelinkBasedActor();
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
