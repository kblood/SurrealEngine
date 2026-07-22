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
#include <array>
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
class MutableCallArguments;
class UnrealURL;
class VideoPlayer;
class UnrealMipmap;
class UFloatProperty;
class UFunction;
class UObjectProperty;
class UStructProperty;
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
	struct VRTrackedPoseState
	{
		bool Tracked = false;
		vec3 Position = vec3(0.0f); // Raw WebXR reference-space metres.
		vec4 Orientation = vec4(0.0f, 0.0f, 0.0f, 1.0f);
		// Gameplay-ready pose in UE1 world coordinates. WorldRotation is derived
		// from WorldForward and intentionally has zero roll for traces/projectiles;
		// WorldPresentationRotation retains the full basis for the viewmodel.
		vec3 WorldPosition = vec3(0.0f);
		vec3 WorldForward = vec3(1.0f, 0.0f, 0.0f);
		vec3 WorldRight = vec3(0.0f, 1.0f, 0.0f);
		vec3 WorldUp = vec3(0.0f, 0.0f, 1.0f);
		Rotator WorldRotation = Rotator(0, 0, 0);
		Rotator WorldPresentationRotation = Rotator(0, 0, 0);
	};

	struct VRControllerInputState
	{
		bool Connected = false;
		uint32_t SourceId = 0;
		uint32_t Handedness = 0;
		uint32_t Flags = 0;
		uint32_t ButtonsPressed = 0;
		uint32_t ButtonsTouched = 0;
		std::array<float, 4> Axes = {};
		std::array<float, 8> ButtonValues = {};
		float TriggerValue = 0.0f;
		VRTrackedPoseState GripPose;
		VRTrackedPoseState AimPose;
	};

	struct VRInputState
	{
		uint64_t Generation = 0;
		uint32_t SourceCount = 0;
		VRTrackedPoseState HeadPose;
		std::array<VRControllerInputState, 2> Controllers;
		uint32_t DominantHandedness = 2; // WebXR right hand by default.
		int32_t DominantControllerIndex = -1;
		uint32_t SynthesizedButtonsHeld = 0;
		std::array<float, 4> SynthesizedAxes = {};
	};

	enum class WebXRTurnMode : uint32_t
	{
		Snap = 0,
		Smooth = 1,
		Binding = 2,
		Disabled = 3
	};

	enum class WebXRMovementReference : uint32_t
	{
		Body = 0,
		Head = 1,
		DominantHand = 2
	};

	struct WebXRLocomotionDiagnostics
	{
		uint32_t DefaultBindingMask = 0;
		uint32_t SnapTurnCount = 0;
		float LastMoveForward = 0.0f;
		float LastMoveStrafe = 0.0f;
		int LastTurnDelta = 0;
		WebXRMovementReference LastMovementReferenceUsed = WebXRMovementReference::Body;
		uint32_t MovementFallbackCount = 0;
		uint32_t RecenterActionCount = 0;
		uint32_t MenuActionCount = 0;
		uint32_t EffectiveRecenterButton = 0;
		uint32_t EffectiveMenuButton = 0;
	};

	struct WebXRMenuNavigationDiagnostics
	{
		bool Active = false;
		uint32_t Direction = 0;
		uint32_t DirectionPulseCount = 0;
		uint32_t RepeatPulseCount = 0;
		uint32_t ConfirmCount = 0;
		uint32_t CancelCount = 0;
		uint32_t SuppressedButtonEdgeCount = 0;
		uint32_t GameplayReleaseCount = 0;
		uint32_t SourceResetCount = 0;
		uint32_t SourceId = 0;
	};

	struct WebXRAudioListenerDiagnostics
	{
		uint32_t UpdateCount = 0;
		uint32_t VelocityResetCount = 0;
		vec3 LastVelocity = vec3(0.0f);
		bool Active = false;
	};

	struct WebXRWeaponAimDiagnostics
	{
		uint32_t BallisticScopeCount = 0;
		uint32_t TargetAcquisitionScopeCount = 0;
		uint32_t PresentationScopeCount = 0;
		uint32_t RestoreCount = 0;
		uint32_t HapticRequestCount = 0;
		uint32_t HapticAcceptedCount = 0;
		uint32_t VisualDrawScopeCount = 0;
		uint32_t VisualDrawRestoreCount = 0;
		uint32_t VisualZeroFallbackCount = 0;
		uint32_t VisualCalibratedOffsetCount = 0;
		uint32_t VisualRejectedTransformCount = 0;
		vec3 LastVisualGripOffset = vec3(0.0f);
		vec3 LastVisualPosition = vec3(0.0f);
	};

	struct WebXRAuthoritativeFireDiagnostics
	{
		uint32_t RequestCount = 0;
		uint32_t QualifiedHitscanContextCount = 0;
		uint32_t ContextRestoreCount = 0;
		uint32_t MissingCalibrationRejectCount = 0;
		uint32_t ProductionDisabledRejectCount = 0;
		uint32_t RemotePawnRejectCount = 0;
		uint32_t StaleWeaponRejectCount = 0;
		uint32_t WrongOwnerRejectCount = 0;
		uint32_t UntrackedPoseRejectCount = 0;
		uint32_t NonFinitePoseRejectCount = 0;
		uint32_t UnqualifiedPathRejectCount = 0;
		uint32_t CalcDrawOffsetObservationCount = 0;
		uint32_t PostSinkCalcRejectCount = 0;
		uint32_t TraceShotObservationCount = 0;
		uint32_t EndpointTranslationCount = 0;
		uint32_t MutableArgumentRejectCount = 0;
		vec3 LastObservedCalcDrawOffset = vec3(0.0f);
		vec3 LastStockTraceStart = vec3(0.0f);
		vec3 LastStockTraceEnd = vec3(0.0f);
		vec3 LastDesiredMuzzle = vec3(0.0f);
		vec3 LastAppliedDelta = vec3(0.0f);
		// This remains false until immutable per-weapon calibration, obstruction
		// policy, complete fixtures, and physical-headset gates are satisfied.
		bool ProductionRewriteEnabled = false;
	};

	// Explicit browser-test fixture only. No normal engine path calls the
	// fixture; the exported diagnostic entry point below is its sole trigger.
	struct WebXRLoadedWeaponFixtureDiagnostics
	{
		uint32_t AttemptCount = 0;
		uint32_t StockGiveWeaponCallCount = 0;
		uint32_t StockChangedWeaponCallCount = 0;
		bool InventoryFound = false;
		bool Equipped = false;
		bool Succeeded = false;
	};

	// Explicit browser-test fixture only. It invokes the loaded retail
	// Botpack.ShockRifle.TraceFire function once while suppressing cosmetic and
	// feedback side effects, then restores all directly touched fixture state.
	struct WebXRLoadedWeaponFireFixtureDiagnostics
	{
		uint32_t AttemptCount = 0;
		uint32_t StockTraceFireCallCount = 0;
		bool ExactStockPath = false;
		bool CounterContractMatched = false;
		bool AmmoPreserved = false;
		bool WeaponStateRestored = false;
		bool SuppressionStateRestored = false;
		bool ActorCountPreserved = false;
		bool HapticsSuppressed = false;
		bool Equipped = false;
		bool Succeeded = false;
	};

	struct WebXRHapticEventDiagnostics
	{
		uint32_t ConfirmedOutcomeCount = 0;
		uint32_t RequestCount = 0;
		uint32_t AcceptedCount = 0;
	};

	struct WebXRHapticDiagnostics
	{
		// Indices are the stable WebXRHapticEvent values: fire, damage,
		// pickup, and UI confirmation.
		std::array<WebXRHapticEventDiagnostics, 4> Events = {};
	};

	Engine(GameLaunchInfo launchinfo);
	~Engine();

	void Run();
	void Setup();
	void RunOneFrame();
	float AdvanceGameFrame();
	void RenderGameFrame(float levelElapsed);
	void FinishGameFrame(float levelElapsed);
	void Shutdown();
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
	void InputCommand(const std::string& command, EInputKey key, float delta);

	void LockCursor();
	void UnlockCursor();

	bool ExecCommand(const Array<std::string>& args);
	Array<std::string> GetArgs(const std::string& commandline);
	Array<std::string> GetSubcommands(const std::string& commandline);

	void PlayAVI(const Array<std::string>& args);
	UnrealMipmap* PlayVideo(VideoPlayer* video, UnrealMipmap* background);

	UConversationList* GetDeusExMission();

	void UpdateAudio(float realTimeElapsed);

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

	// PlayerCalcView call scratch properties - allocated once in Setup(),
	// reused every RunOneFrame() call.
	UObjectProperty* runLoopObjProp = nullptr;
	UStructProperty* runLoopVecProp = nullptr;
	UStructProperty* runLoopRotProp = nullptr;

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
	VRInputState WebXRInput;
	WebXRLocomotionDiagnostics WebXRLocomotion;
	WebXRMenuNavigationDiagnostics WebXRMenuNavigation;
	WebXRAudioListenerDiagnostics WebXRAudioListener;
	WebXRWeaponAimDiagnostics WebXRWeaponAim;
	WebXRAuthoritativeFireDiagnostics WebXRAuthoritativeFire;
	WebXRLoadedWeaponFixtureDiagnostics WebXRLoadedWeaponFixture;
	WebXRLoadedWeaponFireFixtureDiagnostics WebXRLoadedWeaponFireFixture;
	WebXRHapticDiagnostics WebXRHaptics;
	bool RunWebXRLoadedWeaponFixture();
	bool RunWebXRLoadedWeaponFireFixture();

	WebXRTurnMode GetWebXRTurnMode() const { return WebXRTurnModeSetting; }
	WebXRMovementReference GetWebXRMovementReference() const { return WebXRMovementReferenceSetting; }
	uint32_t GetWebXRDominantHand() const { return WebXRInput.DominantHandedness; }
	uint32_t GetWebXRRecenterButton() const { return WebXRRecenterButtonSetting; }
	uint32_t GetWebXRMenuButton() const { return WebXRMenuButtonSetting; }
	bool GetWebXRHudEnabled() const { return WebXRHudEnabled; }
	float GetWebXRHudDistanceUU() const { return WebXRHudDistanceUU; }
	float GetWebXRHudHorizontalFovDegrees() const { return WebXRHudHorizontalFovDegrees; }
	float GetWebXRHudAspectRatio() const { return WebXRHudAspectRatio; }
	float GetWebXRHudSafeAreaFraction() const { return WebXRHudSafeAreaFraction; }
	float GetWebXRSnapTurnDegrees() const { return WebXRSnapTurnDegrees; }
	float GetWebXRSmoothTurnDegreesPerSecond() const { return WebXRSmoothTurnDegreesPerSecond; }
	bool GetWebXRHapticsEnabled() const { return WebXRHapticsEnabled; }
	bool SetWebXRTurnMode(uint32_t mode);
	bool SetWebXRMovementReference(uint32_t reference);
	bool SetWebXRDominantHand(uint32_t handedness);
	// Action buttons use zero for disabled, otherwise one-based normalized
	// slots: left buttons 1..6 and right buttons 7..12. WebXR system buttons
	// are never present in this range.
	bool SetWebXRRecenterButton(uint32_t button);
	bool SetWebXRMenuButton(uint32_t button);
	bool SetWebXRHudEnabled(bool enabled);
	bool SetWebXRHudDistanceUU(float distanceUU);
	bool SetWebXRHudHorizontalFovDegrees(float degrees);
	bool SetWebXRHudAspectRatio(float aspectRatio);
	bool SetWebXRHudSafeAreaFraction(float fraction);
	bool SetWebXRSnapTurnDegrees(float degrees);
	bool SetWebXRSmoothTurnDegreesPerSecond(float degreesPerSecond);
	bool SetWebXRHapticsEnabledSetting(bool enabled);

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
	void UpdateWebXRInput(float timeElapsed);
	void InputAxisEvent(EInputKey key, float delta);
	bool DispatchWebXRMenuPointerEvent(EInputType type);
	bool DispatchWebXRMenuKeyEvent(EInputKey key, EInputType type);
	void ReleaseWebXRMenuNavigationKeys();
	void LoadWebXRInputSettings();
	void SaveWebXRInputSettings();
	void ApplyWebXRHudSettings();
	void RefreshWebXRActionBindings();
	void InstallWebXRDefaultBindings();
	std::function<void()> EnterWebXRCallScope(UFunction* func, UObject* instance,
		const Array<ExpressionValue>& args);
	std::function<void()> EnterWebXRWeaponAimScope(UFunction* func, UObject* instance);
	std::function<void()> EnterWebXRAuthoritativeFireScope(UFunction* func, UObject* instance);
	std::function<void()> EnterWebXRWeaponVisualScope(UFunction* func, UObject* instance,
		const Array<ExpressionValue>& args);
	std::function<void()> EnterWebXRGameplayOutcomeScope(UFunction* func, UObject* instance,
		const Array<ExpressionValue>& args);
	void RecordWebXRHapticOutcome(uint32_t event, float magnitude);
	void MutateWebXRAuthoritativeFireArguments(UFunction* func, UObject* instance,
		MutableCallArguments& args);
	void ObserveWebXRAuthoritativeFireResult(UFunction* func, UObject* instance,
		const Array<ExpressionValue>& args, const ExpressionValue& result);
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
	uint64_t LastProcessedWebXRInputGeneration = 0;
	bool HasProcessedWebXRInput = false;
	bool WebXRMenuPointerTriggerHeld = false;
	int32_t WebXRMenuPointerTriggerButton = -1;
	bool WebXRMenuNavigationActive = false;
	bool WebXRMenuConfirmHeld = false;
	bool WebXRMenuCancelHeld = false;
	bool WebXRMenuActionEscapeHeld = false;
	uint32_t WebXRMenuNavigationDirection = 0;
	float WebXRMenuNavigationRepeatSeconds = 0.0f;
	uint32_t WebXRMenuNavigationSourceId = 0;
	uint32_t WebXRButtonsHeld = 0;
	uint32_t WebXRAxesActive = 0;
	WebXRTurnMode WebXRTurnModeSetting = WebXRTurnMode::Snap;
	WebXRMovementReference WebXRMovementReferenceSetting = WebXRMovementReference::Body;
	uint32_t WebXRRecenterButtonSetting = 10; // Right thumbstick click.
	uint32_t WebXRMenuButtonSetting = 0; // Existing Joy6/Escape binding remains the default.
	bool WebXRHudEnabled = true;
	float WebXRHudDistanceUU = 68.8976f;
	float WebXRHudHorizontalFovDegrees = 50.0f;
	float WebXRHudAspectRatio = 4.0f / 3.0f;
	float WebXRHudSafeAreaFraction = 0.90f;
	bool WebXRHapticsEnabled = true;
	float WebXRSnapTurnDegrees = 30.0f;
	float WebXRSmoothTurnDegreesPerSecond = 120.0f;
	float WebXRSnapTurnThreshold = 0.75f;
	float WebXRSnapTurnRearmThreshold = 0.35f;
	bool WebXRSnapTurnArmed = true;
	vec3 LastWebXRAudioListenerPosition = vec3(0.0f);
	uint64_t LastWebXRAudioListenerGeneration = 0;
	uint32_t LastWebXRAudioRecenterCount = 0;
	bool HasWebXRAudioListenerSample = false;
	bool HasCalculatedCameraView = false;
	uint32_t WebXRDamageScopeDepth = 0;
	uint32_t WebXRPickupScopeDepth = 0;
	struct WebXRWeaponPresentationContext
	{
		UWeapon* Weapon = nullptr;
		int32_t ControllerIndex = -1;
		Rotator PresentationRotation = Rotator(0, 0, 0);
	};
	std::vector<WebXRWeaponPresentationContext> WebXRWeaponPresentationStack;
	struct WebXRAuthoritativeFireContext
	{
		UPlayerPawn* Pawn = nullptr;
		UWeapon* Weapon = nullptr;
		uint32_t Policy = 0;
		uint32_t Rejection = 0;
		uint64_t InputGeneration = 0;
		int32_t ControllerIndex = -1;
		vec3 DesiredMuzzle = vec3(0.0f);
		bool DesiredMuzzleValid = false;
		bool TraceShotObserved = false;
	};
	std::vector<WebXRAuthoritativeFireContext> WebXRAuthoritativeFireStack;
	bool WebXRLoadedWeaponFireFixtureActive = false;
};

extern Engine* engine;
