#pragma once

#include "Utils/Logger.h"
#include "Math/vec.h"
#include "Math/mat.h"
#include "Math/floating.h"
#include "RenderDevice/RenderDevice.h"
#include "Render/ViewFamily.h"
#include "Platform/OpenXR/OpenXRView.h"
#include "Platform/OpenXR/OpenXRUIRuntime.h"
#include "GameWindow.h"
#include "UObject/UActor.h"
#include "UObject/UnrealURL.h"
#include "UObject/UWindow.h"
#include "UObject/UDXSaveInfo.h"
#include "UObject/UDeusExLevelInfo.h"
#include "GameFolder.h"
#include "Input/InputComposition.h"
#include "Input/XRInputAdapter.h"
#include "XR/XRHapticFeedbackPolicy.h"
#include "XR/XRStartupMenuRoute.h"
#include "XR/XRStartupIntroRoute.h"
#include "XR/XRWeaponPoseSolver.h"
#include "XR/XRHandedness.h"
#include <set>
#include <list>

class RenderSubsystem;
class PackageManager;
class UObject;
class ULevel;
class UModel;
class GameWindow;
class UTexture;
class UActor;
class UFont;
class UMesh;
class ULodMesh;
class USkeletalMesh;
class ULevelSummary;
class ULevelInfo;
class UZoneInfo;
class UClient;
class UViewport;
class UCanvas;
class UConsole;
class UPlayerPawn;
class UGameInfo;
class UGameReplicationInfo;
class UPlayerReplicationInfo;
class UGameEngine;
class USurrealRenderDevice;
class USurrealAudioDevice;
class USurrealNetworkDevice;
class USurrealClient;
class BspSurface;
class BspNode;
class LightMapIndex;
class FrustumPlanes;
class AudioSubsystem;
class Rotator;
class ExpressionValue;
class UnrealURL;
class VideoPlayer;
class BrowserCinematicPlayback;
class OpenXRProvider;
class BotSpectatorMatch;
class UnrealMipmap;
class UFloatProperty;
class UObjectProperty;
class UStructProperty;
class UConversationMissionList;
class UConversationList;
struct FTextureInfo;
struct FSceneNode;
struct FSurfaceFacet;
struct MeshFace;

static constexpr int32_t DONT_SAVE_GAME = -2; // Since -1 might be used as the autosave slot in some UE1 games...

class Engine : public GameWindowHost, public XRInputTarget
{
public:
	Engine(GameLaunchInfo launchinfo);
	~Engine();

	void Run();
	void Setup();
	// A frame advances simulation once, renders its resulting state, then handles
	// deferred save and travel requests. RunOneFrame preserves the native ordering.
	void RunOneFrame();
	void UpdateOpenXRStartupIntro(const XRControllerSnapshot* controllers);
	void UpdateOpenXRStartupMenu(float elapsedSeconds,
		const XRSessionState& session, const XRControllerSnapshot& controllers,
		bool menuActive);
	float AdvanceGameFrame();
	// XR providers publish one provider-neutral weapon pose before simulation.
	// Narrow VM weapon scopes consume its hand direction while movement and the
	// ordinary gameplay view remain unchanged outside classified weapon calls.
	float AdvanceGameFrameWithXRWeaponAim(const XRWeaponPoseResult& pose);
	void SetXRWeaponPose(const XRWeaponPoseResult& pose) { xrWeaponPose = pose; }
	void SetXRWeaponPoses(const XRWeaponPoseResult& pose,
		const XRWeaponPoseResult& offHandPose)
	{
		xrWeaponPose = pose;
		xrOffHandWeaponPose = offHandPose;
	}
	const XRWeaponPoseResult& GetXRWeaponPose() const { return xrWeaponPose; }
	const XRWeaponPoseResult& GetXROffHandWeaponPose() const { return xrOffHandWeaponPose; }
	void ClearXRWeaponPose() { xrWeaponPose = {}; xrOffHandWeaponPose = {}; }
	UWeapon* GetXRSecondaryWeapon(UWeapon* master);
	const XRWeaponPoseResult* GetXRWeaponPoseForActor(UActor* actor);
	void SetXRDominantHand(XRHand hand);
	XRHand GetXRDominantHand() const { return xrHandedness.Dominant; }
	const XRHandedness& GetXRHandedness() const { return xrHandedness; }
	void RenderGameFrame(float levelElapsed);
	void RenderGameFrame(float levelElapsed, const ViewFamily& viewFamily);
	void FinishGameFrame(float levelElapsed);
	void Shutdown();
	int GetRunExitCode() const { return m_RunExitCode; }
	bool IsBotBenchmarkWalkingPreflightEnabled() const
	{
		return botBenchmarkWalkingPreflightEnabled;
	}
	bool IsBotBenchmarkFallingHitWallCallbackWitnessEnabled() const
	{
		return botBenchmarkFallingHitWallCallbackWitnessEnabled;
	}
	void SetBotBenchmarkFallingHitWallCallbackWitnessEnabled(bool enabled)
	{
		botBenchmarkFallingHitWallCallbackWitnessEnabled = enabled;
	}
	bool IsBotBenchmarkHarmfulZoneEscapeEnabled() const
	{
		return botBenchmarkHarmfulZoneEscapeEnabled;
	}
	void SetBotBenchmarkHarmfulZoneEscapeEnabled(bool enabled)
	{
		botBenchmarkHarmfulZoneEscapeEnabled = enabled;
	}
	bool IsBotBenchmarkWalkingPreflightPositiveDpsVetoEnabled() const
	{
		return botBenchmarkWalkingPreflightPositiveDpsVetoEnabled;
	}
	void SetBotBenchmarkWalkingPreflightPositiveDpsVetoEnabled(bool enabled)
	{
		botBenchmarkWalkingPreflightPositiveDpsVetoEnabled = enabled;
	}
	bool IsBotBenchmarkHazardSwimEgressEnabled() const
	{
		return botBenchmarkHazardSwimEgressEnabled;
	}
	void SetBotBenchmarkHazardSwimEgressEnabled(bool enabled)
	{
		botBenchmarkHazardSwimEgressEnabled = enabled;
	}
	bool IsBotBenchmarkHazardSwimEgressLiveEnabled() const
	{
		return botBenchmarkHazardSwimEgressLiveEnabled;
	}
	void SetBotBenchmarkHazardSwimEgressLiveEnabled(bool enabled)
	{
		botBenchmarkHazardSwimEgressLiveEnabled = enabled;
	}
	bool IsBotBenchmarkFallingHazardRecoveryEnabled() const
	{
		return botBenchmarkFallingHazardRecoveryEnabled;
	}
	void SetBotBenchmarkFallingHazardRecoveryEnabled(bool enabled)
	{
		botBenchmarkFallingHazardRecoveryEnabled = enabled;
	}
	bool IsBotBenchmarkFallingHazardRecoveryLiveEnabled() const
	{
		return botBenchmarkFallingHazardRecoveryLiveEnabled;
	}
	void SetBotBenchmarkFallingHazardRecoveryLiveEnabled(bool enabled)
	{
		botBenchmarkFallingHazardRecoveryLiveEnabled = enabled;
	}
	bool IsBotBenchmarkFailedNavigationAvoidanceEnabled() const
	{
		return botBenchmarkFailedNavigationAvoidanceEnabled;
	}
	void SetBotBenchmarkFailedNavigationAvoidanceEnabled(bool enabled)
	{
		botBenchmarkFailedNavigationAvoidanceEnabled = enabled;
	}
	bool IsBotBenchmarkTargetlessMoveToTimeoutEnabled() const
	{
		return botBenchmarkTargetlessMoveToTimeoutEnabled;
	}
	void SetBotBenchmarkTargetlessMoveToTimeoutEnabled(bool enabled)
	{
		botBenchmarkTargetlessMoveToTimeoutEnabled = enabled;
	}
	bool IsBotBenchmarkDirectActorMoveTowardTimeoutEnabled() const
	{
		return botBenchmarkDirectActorMoveTowardTimeoutEnabled;
	}
	void SetBotBenchmarkDirectActorMoveTowardTimeoutEnabled(bool enabled)
	{
		botBenchmarkDirectActorMoveTowardTimeoutEnabled = enabled;
	}
	bool IsBotBenchmarkPickTargetObserverEnabled() const
	{
		return botBenchmarkPickTargetObserverEnabled;
	}
	void SetBotBenchmarkPickTargetObserverEnabled(bool enabled)
	{
		botBenchmarkPickTargetObserverEnabled = enabled;
	}
	bool IsBotBenchmarkInventoryDirectReachSupportObserverEnabled() const
	{
		return botBenchmarkInventoryDirectReachSupportObserverEnabled;
	}
	void SetBotBenchmarkInventoryDirectReachSupportObserverEnabled(bool enabled)
	{
		botBenchmarkInventoryDirectReachSupportObserverEnabled = enabled;
	}
	bool IsBotBenchmarkInventoryMarkerDirectReachSafetyEnabled() const
	{
		return botBenchmarkInventoryMarkerDirectReachSafetyEnabled;
	}
	void SetBotBenchmarkInventoryMarkerDirectReachSafetyEnabled(bool enabled)
	{
		botBenchmarkInventoryMarkerDirectReachSafetyEnabled = enabled;
	}
	bool IsBotBenchmarkNativePathCommitObserverEnabled() const
	{
		return botBenchmarkNativePathCommitObserverEnabled;
	}
	void SetBotBenchmarkNativePathCommitObserverEnabled(bool enabled)
	{
		botBenchmarkNativePathCommitObserverEnabled = enabled;
	}
	bool IsBotBenchmarkDirectReachCommandObserverEnabled() const
	{
		return botBenchmarkDirectReachCommandObserverEnabled;
	}
	void SetBotBenchmarkDirectReachCommandObserverEnabled(bool enabled)
	{
		botBenchmarkDirectReachCommandObserverEnabled = enabled;
	}
	uint64_t BotBenchmarkObserverTick() const { return botBenchmarkObserverTick; }
	void SetBotBenchmarkObserverTick(uint64_t tick) { botBenchmarkObserverTick = tick; }
	void ClientTravel(const std::string& URL, ETravelType travelType, bool transferItems);
	UnrealURL GetDefaultURL(const std::string& map);
	void LoadEntryMap();
	void LoadMap(const UnrealURL& url, const std::map<std::string, std::string>& travelInfo = {});
	void LoadFromSaveFile(const UnrealURL& url);
	void SaveGameToSlot(int32_t slotNum, const std::string& saveDescription) const;
	void UnloadMap();
	void LoginPlayer();
	void PossessSavedPlayer();

	UObject* FindObject(NameString name, NameString className);

	std::string ConsoleCommand(UObject* context, const std::string& command, BitfieldBool& found);

	void UpdateInput(float timeElapsed);
	void InputCommand(const std::string& command, InputControlId control, float delta) override;
	void ReleaseInputControl(InputControlId control) override;

	void LockCursor();
	void UnlockCursor();

	bool ExecCommand(const Array<std::string>& args);
	Array<std::string> GetArgs(const std::string& commandline);
	Array<std::string> GetSubcommands(const std::string& commandline);

	void PlayAVI(const Array<std::string>& args);
	UnrealMipmap* PlayVideo(VideoPlayer* video, UnrealMipmap* background);

	UConversationList* GetDeusExMission();

	void UpdateAudio();

	void OpenWindow();
	void CloseWindow();
	void TickWindow();

	void Key(std::string key);
	void InputEvent(EInputKey key, EInputType type, float delta = 0.0f, InputSourceId source = InputSourceId::KeyboardMouse);
	void ReleaseInputSource(InputSourceId source) override;
	bool IsStartupIntroActive() const { return startupIntroActive; }
	void CompleteStartupIntro() { startupIntroActive = false; }

	void OnWindowPaint() override;
	void OnWindowMouseMove(const Point& pos) override;
	void OnWindowMouseDown(const Point& pos, EInputKey key) override;
	void OnWindowMouseDoubleclick(const Point& pos, EInputKey key) override;
	void OnWindowMouseUp(const Point& pos, EInputKey key) override;
	void OnWindowMouseWheel(const Point& pos, EInputKey key) override;
	void OnWindowRawMouseMove(int dx, int dy) override;
	void OnWindowKeyChar(std::string chars) override;
	void OnWindowKeyDown(EInputKey key) override;
	void OnWindowKeyUp(EInputKey key) override;
	void OnWindowGeometryChanged() override;
	void OnWindowClose() override;
	void OnWindowActivated() override;
	void OnWindowDeactivated() override;
	void OnWindowDpiScaleChanged() override;

	void SetPause(bool value);

	UZoneInfo* GetZoneActor(int zoneIndex);

	std::string ParseClassName(std::string className);

	UGameEngine* gameengine = nullptr;
	USurrealRenderDevice* renderdev = nullptr;
	USurrealAudioDevice* audiodev = nullptr;
	USurrealNetworkDevice* netdev = nullptr;
	USurrealClient* client = nullptr;
	UViewport* viewport = nullptr;
	UCanvas* canvas = nullptr;
	UGC* dxgc = nullptr;
	UDXSaveInfo* dxSaveInfo = nullptr;
	UConversationMissionList* dxConMissionList = nullptr;
	UConsole* console = nullptr;
	URootWindow* dxRootWindow = nullptr;

	UFloatProperty* floatprop = nullptr;

	ULevelInfo* EntryLevelInfo = nullptr;
	ULevel* EntryLevel = nullptr;
	UGameInfo* EntryGameInfo = nullptr;
	Package* EntryLevelPackage = nullptr;

	double TotalTime = 0.0;

	ULevelInfo* LevelInfo = nullptr;
	ULevel* Level = nullptr;
	Package* LevelPackage;
	UGameInfo* GameInfo = nullptr;
	UTexture* DefaultTexture = nullptr;

	Package* deusExPackage = nullptr;
	UDeusExLevelInfo* DeusExLevelInfo = nullptr;
	struct
	{
		UnrealURL URL;
		ETravelType TravelType = ETravelType::TRAVEL_Absolute;
		bool TransferItems = false;
	} ClientTravelInfo;

	struct
	{
		int32_t SaveGameSlot = DONT_SAVE_GAME;
		std::string SaveGameDescription;
	} SaveGameInfo;

	GameLaunchInfo LaunchInfo;
	std::unique_ptr<PackageManager> packages;
	std::unique_ptr<GameWindow> window; // TODO: Move into UViewport
	std::unique_ptr<RenderSubsystem> render;
	std::unique_ptr<OpenXRProvider> openXR;
	OpenXRViewTranslator openXRViews;
	OpenXRUIRuntime openXRUI;
	XRStartupIntroTriggerRoute openXRStartupIntroTrigger;
	XRStartupMenuRoute openXRStartupMenu;
	XRMenuNavigationRoute openXRMenuNavigation;
	XRNativeControllerEventRoute openXRControllerEvents;
	XRHapticFeedbackPolicy openXRHapticFeedback;
	XRHandedness xrHandedness;

	int MouseMoveX = 0;
	int MouseMoveY = 0;

	float CalcTimeElapsed();

	UActor* CameraActor = nullptr;
	vec3 CameraLocation = vec3(0.0f);
	Rotator CameraRotation = Rotator(0,0,0);
	float CameraFovAngle = 95.0f;

	std::string windowingSystemName;

	// Collision debug
	BspNode* PlayerBspNode = nullptr;
	vec3 PlayerHitNormal = vec3(0.0f);
	vec3 PlayerHitLocation = vec3(0.0f);

	bool quit = false;
	uint64_t tickCount = 0;

	uint64_t lastTime = 0;

	void LoadEngineSettings();

	void LoadKeybindings();
	std::map<std::string, std::string> keybindings;
	std::map<std::string, std::string> inputAliases;
	static const char* keynames[256];

	InputComposition inputComposition;
	XRInputAdapter openXRInput;

	std::function<void()> tickDebugger;

	bool getEditorMode() const { return m_EditorMode; }
	void setEditorMode(const bool value) { m_EditorMode = value; }

	bool getDXWindowDebugMode() const { return m_DrawDebugDXWindowHierarchy; }

private:
	std::unique_ptr<BotSpectatorMatch> botSpectatorMatch;
	XRWeaponPoseResult xrWeaponPose;
	XRWeaponPoseResult xrOffHandWeaponPose;
	UClass* xrEnforcerClass = nullptr;
	PropertyDataOffset xrEnforcerSlaveOffset;
	bool xrManualSlaveFireActive = false;
	bool xrManualSlaveFirePending = false;
	bool xrAlternateFireKeyDown = false;
	uint64_t xrWeaponCallHook = 0;
	bool botBenchmarkWalkingPreflightEnabled = false;
	bool botBenchmarkFallingHitWallCallbackWitnessEnabled = false;
	bool botBenchmarkHarmfulZoneEscapeEnabled = false;
	bool botBenchmarkWalkingPreflightPositiveDpsVetoEnabled = false;
	bool botBenchmarkHazardSwimEgressEnabled = false;
	bool botBenchmarkHazardSwimEgressLiveEnabled = false;
	bool botBenchmarkFallingHazardRecoveryEnabled = false;
	bool botBenchmarkFallingHazardRecoveryLiveEnabled = false;
	bool botBenchmarkFailedNavigationAvoidanceEnabled = false;
	bool botBenchmarkTargetlessMoveToTimeoutEnabled = false;
	bool botBenchmarkDirectActorMoveTowardTimeoutEnabled = false;
	bool botBenchmarkPickTargetObserverEnabled = false;
	bool botBenchmarkInventoryDirectReachSupportObserverEnabled = false;
	bool botBenchmarkInventoryMarkerDirectReachSafetyEnabled = false;
	bool botBenchmarkNativePathCommitObserverEnabled = false;
	bool botBenchmarkDirectReachCommandObserverEnabled = false;
	uint64_t botBenchmarkObserverTick = 0;
	ViewFamily CreateDesktopViewFamily() const;
	void InstallXRWeaponCallHook();
	void UninstallXRWeaponCallHook();
	float AdvanceGameFrame(float realTimeElapsed);
	float AdvanceGameFrameWithXRWeaponAim(const XRWeaponPoseResult& pose,
		float realTimeElapsed);
	float AdvanceGameFrameWithXRWeaponAim(const XRWeaponPoseResult& pose,
		const XRWeaponPoseResult& offHandPose, float realTimeElapsed);
	void UpdateOpenXRLocomotion(const XRPose& headPose,
		const XRControllerSnapshot& controllers, float realTimeElapsed,
		bool gameplayInputEnabled);
	void UpdateOpenXRControllerEvents(const XRSessionState& session,
		const XRControllerSnapshot& controllers, bool gameplayInputEnabled,
		bool menuActive);
	void ReleaseOpenXRControllerEvents();
	void ApplyOpenXRControllerEvents(const std::vector<XRNativeKeyEvent>& events);
	void DispatchPendingXRSlaveFire();
	void UpdateOpenXRWeaponDiagnostics(float elapsedSeconds,
		const XRSpaceSamples& spaces, const XRWorldTransform& worldTransform,
		const XRWeaponPoseResult& pose);
	float openXRWeaponDiagnosticTime = 0.0f;

	// Scratch properties used by PlayerCalcView during AdvanceGameFrame.
	UObjectProperty* frameObjProp = nullptr;
	UStructProperty* frameVecProp = nullptr;
	UStructProperty* frameRotProp = nullptr;

	void RunHeadlessDriver(const std::string& driverName);
	std::map<std::string, std::string> CreateTravelInfo(bool transferItems);

	void LogGamePackageSHA1Sums() const;
	void GetLevelInfoObject();
	void GetLevelObject();
	void LinkActorsToLevel();

	bool m_EditorMode = false; // Set this to true to allow rendering of invisible polys.
	bool m_GamePaused = false;
	int m_RunExitCode = 0;

	bool m_DrawDebugDXWindowHierarchy = false; // If set to true, engine will show the Deus Ex window hierarchy

	bool khgSplashScreen = false;
	bool playingAvi = false;
	bool skipAvi = false;
	bool startupIntroActive = false;
	float lastRealTimeElapsed = 0.0f;
#ifdef __EMSCRIPTEN__
	std::unique_ptr<BrowserCinematicPlayback> browserCinematic;
	bool AdvanceBrowserCinematic(float elapsedSeconds);
	void FinishBrowserCinematic();
#endif
};

extern Engine* engine;
