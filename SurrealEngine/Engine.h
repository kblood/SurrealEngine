#pragma once

#include "Utils/Logger.h"
#include "Math/vec.h"
#include "Math/mat.h"
#include "Math/floating.h"
#include "RenderDevice/RenderDevice.h"
#include "GameWindow.h"
#include "UObject/UActor.h"
#include "UObject/UnrealURL.h"
#include "UObject/UWindow.h"
#include "UObject/UDXSaveInfo.h"
#include "UObject/UDeusExLevelInfo.h"
#include "GameFolder.h"
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
class UnrealMipmap;
class UFloatProperty;
class UConversationMissionList;
class UConversationList;
struct FTextureInfo;
struct FSceneNode;
struct FSurfaceFacet;
struct MeshFace;

static constexpr int32_t DONT_SAVE_GAME = -2; // Since -1 might be used as the autosave slot in some UE1 games...

class Engine : public GameWindowHost
{
public:
	Engine(GameLaunchInfo launchinfo);
	~Engine();

	void Run();
	void ClientTravel(const std::string& URL, ETravelType travelType, bool transferItems);
	UnrealURL GetDefaultURL(const std::string& map);
	void LoadEntryMap();
	void LoadMap(const UnrealURL& url, const std::map<std::string, std::string>& travelInfo = {});
	void LoadFromSaveFile(const UnrealURL& url);
	void SaveGameToSlot(int32_t slotNum, const std::string& saveDescription) const;
	void UnloadMap();
	void LoginPlayer();

	UObject* FindObject(NameString name, NameString className);

	std::string ConsoleCommand(UObject* context, const std::string& command, BitfieldBool& found);

	void UpdateInput(float timeElapsed);
	void InputCommand(const std::string& command, EInputKey key, int delta);

	// M3: reads the current OpenXR controller state (via xrSession) and
	// drives pawn input the same way UpdateInput() drives keyboard/mouse
	// input - SetBool/SetFloat for held movement/fire, ExecCommand for
	// edge-triggered actions (jump, weapon switch, menu). No-op unless an
	// XR session is actually running. See Engine.cpp's Run() for where
	// this is called each frame, and VulkanXRSession.h's VRControllerState
	// for what's available.
	void UpdateVRControllerInput(float timeElapsed);

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
	void InputEvent(EInputKey key, EInputType type, int delta = 0);

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

	// M2 step 4/8/9: only constructed when --vr is passed AND the OpenXR
	// runtime probe succeeds; null otherwise (normal flatscreen play). See
	// VulkanXRSession.h and the XR frame loop wrapped around DrawGame() in
	// Run(). Forward-declared via GameWindow.h; full type only needed in
	// Engine.cpp, which is also where it's constructed/destroyed.
	std::unique_ptr<VulkanXRSession> xrSession;
	bool xrSessionActive = false; // true once CreateSession()+CreateSwapchains() both succeeded

	// M3: real head-pose composition. One-time recenter so the room's
	// physical forward (wherever the headset faced when the LOCAL
	// reference space was established) lines up with the game's own
	// facing at that moment, captured from the first successful
	// LocateViews() after the session starts rendering. See Run()'s XR
	// frame loop and VR_IMPLEMENTATION_PLAN.md's M3 section.
	bool xrPoseRecentered = false;
	float xrYawOffsetUE = 0.0f; // radians, in Coords::YawRotation's convention
	float xrHeadYawUE = 0.0f; // radians; xrYawOffsetUE + live tracked yaw, updated once per XR frame - see UpdateVRControllerInput()
	// 2026-07-21: live tracked head pitch, radians, in Rotator/game convention
	// already (atan2(fwdUE.z, horizontalLen) - same formula as
	// Rotator::FromVector's Pitch, see rotator.h - so unlike yaw this needs
	// no sign flip and no recenter offset when written to ViewRotation.Pitch).
	// Written into Pawn.ViewRotation.Pitch (never Pawn.Rotation.Pitch - see
	// UpdateVRControllerInput()) so weapon aim and swim-direction (both
	// ViewRotation-driven) follow the player looking up/down.
	float xrHeadPitchUE = 0.0f;

	// M-A: per-hand grip/aim world-space state
	// (Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's M-A section) - computed once
	// per XR frame in Run()'s XR frame block, right after LocateViews()/
	// LocateHandPoses(), through the exact same pose-composition math the
	// eye poses use (axis remap via XRVecToUE1, UU scale via UUPerMeter,
	// yaw recenter via xrYawOffsetUE, CameraLocation anchor - see
	// Engine.cpp's anonymous-namespace ComposeXRPoseToWorld()). `valid` is
	// false (gripPos/gripCoords/aimRotator left at their last-known value
	// otherwise) whenever this hand's grip or aim pose isn't currently
	// tracked this frame - every consumer (M-B onward) must check it
	// before using a hand's pose, same "degrade gracefully" contract as
	// every other VR input path in this file.
	struct VRHandState
	{
		bool valid = false;
		vec3 gripPos = vec3(0.0f); // world space, UE units - viewmodel anchor / two-hand-vector endpoint (M-B/M-D)
		Coords gripCoords = Coords::Identity(); // world-space grip orientation (XAxis=forward, YAxis=right, ZAxis=up)
		Rotator aimRotator = Rotator(0, 0, 0); // world-space aim-pose forward as a Rotator (Rotator::FromVector) - what weaponAimRotator(hand) reads from in M-C
	};
	VRHandState xrHands[2]; // index 0=left, 1=right (/user/hand/left,right order) - consume via MainHand()/OffHand(), don't index this directly

	// M-A: which physical hand is the "main" (weapon) hand - 0=left,
	// 1=right. Right-handed default per the plan; M-F's --vr-lefthand
	// flips this one variable. Every later milestone (M-B viewmodel/fire,
	// M-D two-hand aim, M-F handedness) is meant to consume hands ONLY
	// through MainHand()/OffHand(), never xrHands[] directly, so the
	// handedness swap stays a one-variable change - see the plan's M-F
	// section for the explicit design intent.
	int mainHand = 1;
	VRHandState& MainHand() { return xrHands[mainHand]; }
	VRHandState& OffHand() { return xrHands[1 - mainHand]; }

	// M3: edge-detection state for controller buttons that should fire
	// once per press rather than stay held (Jump, weapon switch, menu,
	// recenter) - see UpdateVRControllerInput() in Engine.cpp.
	bool prevVRRightA = false;
	bool prevVRLeftX = false;
	bool prevVRLeftY = false;
	bool prevVRLeftMenu = false;
	bool prevVRRightStickClick = false;

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

	uint64_t lastTime = 0;

	void LoadEngineSettings();

	void LoadKeybindings();
	std::map<std::string, std::string> keybindings;
	std::map<std::string, std::string> inputAliases;
	static const char* keynames[256];

	struct ActiveInputAxis
	{
		float Value;
		EInputKey Key;
	};

	std::map<std::string, EInputKey> activeInputButtons;
	std::map<std::string, ActiveInputAxis> activeInputAxes;

	std::function<void()> tickDebugger;

	bool getEditorMode() const { return m_EditorMode; }
	void setEditorMode(const bool value) { m_EditorMode = value; }

	bool getDXWindowDebugMode() const { return m_DrawDebugDXWindowHierarchy; }

private:
	std::map<std::string, std::string> CreateTravelInfo(bool transferItems);

	void LogGamePackageSHA1Sums() const;
	void GetLevelInfoObject();
	void GetLevelObject();
	void LinkActorsToLevel();

	bool m_EditorMode = false; // Set this to true to allow rendering of invisible polys.
	bool m_GamePaused = false;

	bool m_DrawDebugDXWindowHierarchy = false; // If set to true, engine will show the Deus Ex window hierarchy

	bool khgSplashScreen = false;
	bool playingAvi = false;
	bool skipAvi = false;
};

extern Engine* engine;
