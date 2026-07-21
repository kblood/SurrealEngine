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
class UFunction;
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

	// M-F: left/right-handed mode (Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's
	// M-F section). Resolved once in Run() from `--vr-lefthand` OR the
	// persisted `[Engine.VR] LeftHanded` ini entry (same
	// packages->GetIniValue mechanism M-D's TwoHandAimFilterAlpha already
	// uses, see its parse site) into `mainHand` (0 = left main hand). Kept
	// as its own bool (rather than only inspecting `mainHand == 0`
	// elsewhere) because the weapon-mesh mirroring intercept
	// (Render/VisibleMesh.cpp) and the fire/alt-fire trigger swap
	// (UpdateVRControllerInput) both need a readable "is left-handed mode
	// on" flag distinct from "which physical index is main this frame" -
	// same value, clearer call sites. Default false so the entire M-F
	// feature is a no-op (byte-for-byte unchanged behavior) unless
	// explicitly requested.
	bool vrLeftHanded = false;

	// M-B: --debugvrhands - synthesizes two fake, slowly-orbiting hand
	// poses with no XR session/headset required at all (see
	// Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's M-B section), so the
	// RenderOverlays intercept below and its viewmodel/off-hand-marker
	// rendering are exercisable on a machine with no HMD connected.
	// Parsed once from the command line in Run(); UpdateDebugVRHands() is
	// called every tick from Run()'s main loop independent of xrSession
	// (works with or without --vr) and simply overwrites xrHands[] the
	// same way the M-A OpenXR composition does, so every consumer
	// (MainHand()/OffHand(), the intercept, the renderer) needs zero
	// debug-specific branching - it only ever looks at `valid`.
	bool debugVRHandsEnabled = false;
	float debugVRHandsTime = 0.0f;
	void UpdateDebugVRHands(float timeElapsed);

	// M-C: --debugvrfire - the non-interactive way to exercise the
	// TraceFire/ProjectileFire ViewRotation-swap intercept (and, once
	// confirmed, the CalcDrawOffset fire-origin override) without a headset
	// or any interactive input at all, same rationale as --debugvrhands
	// above (see Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's M-C DoD, which
	// calls for "a synthesized fire press... from a debug path or timed
	// script" as the verification path). Synthesizes several ~0.4s trigger
	// pulses (press then release through the exact same Engine::InputEvent
	// IK_LeftMouse path real trigger edges use, see
	// UpdateVRControllerInput's doc comment for why a real InputEvent -
	// not just a property write - is required), a second or so apart,
	// starting a few seconds into the run. Multiple pulses rather than one:
	// a first-cut single pulse produced zero fire intercepts in testing -
	// UT99 commonly consumes the FIRST Fire click as a UI-level "click to
	// start"/focus-dismissal KeyEvent before any click reaches actual
	// weapon fire logic (the exact phenomenon already documented on
	// UpdateVRControllerInput's real-headset fire path), so later pulses
	// are what actually reach TraceFire/ProjectileFire. Each pulse is also
	// long enough for the VM's normal Level->Tick() cadence to observe
	// bFire=true across at least one tick and for automatic weapons to
	// re-fire from their own state code while "held". Independent of
	// xrSession/--vr, same as --debugvrhands; intended to be paired with
	// --debugvrhands (fake hand poses) rather than a real VR session.
	bool debugVRFireEnabled = false;
	float debugVRFireTime = 0.0f;
	void UpdateDebugVRFire(float timeElapsed);

	// M-D: --debugvrtwohand - the non-interactive way to exercise the
	// foregrip grab/release state machine (dual hysteresis, ~100ms blend,
	// baseline-shrink stability) without a headset - see
	// UpdateDebugVRTwoHand()'s doc comment in Engine.cpp for the scripted
	// approach/hold/release timeline and
	// Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's M-D DoD. Layers on top of
	// --debugvrhands (auto-enabled if not already passed - the grab test
	// needs a valid, moving MAIN hand pose to measure the off-hand
	// against); independent of xrSession/--vr, same pattern as
	// --debugvrhands/--debugvrfire.
	bool debugVRTwoHandEnabled = false;
	float debugVRTwoHandTime = 0.0f;
	void UpdateDebugVRTwoHand(float timeElapsed);

	// M-E1: --debugvrdualenforcer - the non-interactive way to acquire a
	// REAL, stock-paired double-Enforcer for testing GetSlaveEnforcer()/
	// ResolveVRWeaponHand() against the user's actual UT99 game data,
	// without a headset or any interactive input, per
	// Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's M-E1 verification note ("a
	// way to actually acquire a double-Enforcer non-interactively"). Test-
	// only tooling; never active without this explicit flag, and touches
	// nothing outside this one debug function. See
	// UpdateDebugVRDualEnforcer()'s doc comment in Engine.cpp for exactly
	// what it does and why every step is a generic, publicly-documented
	// engine mechanism rather than anything Enforcer/Botpack-specific.
	bool debugVRDualEnforcerEnabled = false;
	float debugVRDualEnforcerTime = 0.0f;
	void UpdateDebugVRDualEnforcer(float timeElapsed);

	// M-D: foregrip grab state - see UpdateVRTwoHandGrip()'s doc comment in
	// Engine.cpp for the full dual-hysteresis grab/release contract.
	// `vrTwoHandGripActive` is the raw on/off state; `vrTwoHandBlendWeight`
	// ramps toward it (0=one-handed, 1=two-handed) over ~100ms so
	// WeaponAimRotator()'s output never jumps discontinuously at a grab/
	// release transition (plan's M-D "stability" requirement).
	bool vrTwoHandGripActive = false;
	float vrTwoHandBlendWeight = 0.0f;

	// M-D: optional EMA low-pass filter on WeaponAimRotator()'s final
	// output (plan's M-D "stability" nice-to-have, H3VR-style "hand
	// smoothing") - ini-tunable via a [Engine.VR] TwoHandAimFilterAlpha
	// entry (0..1; 1.0 = fully off, the default - loaded once in Run(),
	// see its parse site). Lower values smooth more (and add more lag).
	// The plan explicitly asks this to default off or very light, never
	// forced on - see ApplyAimFilter()'s doc comment for why alpha=1.0 is
	// an exact passthrough, not just "very light" smoothing.
	float vrAimFilterAlpha = 1.0f;
	Rotator vrAimFilterState = Rotator(0, 0, 0);
	bool vrAimFilterInitialized = false;
	Rotator ApplyAimFilter(const Rotator& raw);

	// M-D: foregrip grab detection + blend update - see its doc comment in
	// Engine.cpp. Called once per tick from both the real controller path
	// (UpdateVRControllerInput(), with the off-hand's real OpenXR grip
	// analog) and the debug path (UpdateDebugVRTwoHand(), with a scripted
	// analog), so there is exactly one place the grab/release/blend logic
	// lives. `forceTwoHandedForTest` (default false, never set by the real
	// path) lets --debugvrtwohand exercise the full grab-active state
	// machine without depending on --autoplay actually picking up and
	// switching to one of the (currently few) weapons flagged two-handed
	// in GetWeaponGripInfo()'s table - see UpdateDebugVRTwoHand()'s doc
	// comment for why. It does not change which weapon is held or bypass
	// anything else about the real per-weapon table.
	void UpdateVRTwoHandGrip(float timeElapsed, float offHandGripAnalog, bool forceTwoHandedForTest = false);

	// M-D: transforms a weapon's authored, weapon-local foregrip point
	// (VRWeaponGripInfo::foregripPoint) into world space, using the exact
	// same viewmodel placement math the M-B RenderOverlays intercept uses -
	// see its doc comment in Engine.cpp.
	vec3 WorldForegripPoint(UWeapon* weapon);

	// M-B: weaponAimRotator(hand) - the named extension point from the
	// plan's "Aim-source model" section. One-handed: the hand's own
	// aim-pose forward, already composed into a world-space Rotator (see
	// VRHandState::aimRotator's doc comment). M-D extension (see the .cpp
	// definition): while the M-D foregrip grip's blend weight
	// (vrTwoHandBlendWeight) is above zero AND the off-hand has a valid
	// pose AND the hands aren't too close together (a further,
	// baseline-length-based blend - see the plan's M-D "stability"
	// paragraph), this blends toward the rotator of
	// normalize(OffHand().gripPos - hand.gripPos), with roll solved to
	// align with hand's up axis, instead of the one-handed aimRotator -
	// blended (not hard-cut) via a shortest-arc-per-component lerp so the
	// value this returns never jumps discontinuously at a grab/release/
	// baseline transition. Now defined out-of-line in Engine.cpp (used to
	// be a one-line inline before M-D) so it can share the file-local
	// helpers (UUPerMeter, ShortestAngleDelta/LerpRotatorShortest,
	// SolveRollForUpAxis) the rest of the VR pose composition code already
	// uses there.
	Rotator WeaponAimRotator(const VRHandState& hand);

	// M-B: per-weapon grip/aim tuning table (plan's M-B "Per-weapon grip
	// table" section). Keyed by weapon UClass name; GetWeaponGripInfo()
	// falls back to a computed default (this weapon's own authored
	// Inventory.PlayerViewOffset/Weapon.FireOffset - UActor.h:901/951 - so
	// an unlisted weapon's un-tuned VR anchor starts in the same ballpark
	// as its flatscreen viewmodel position) for any class not explicitly
	// listed. M-C/M-D/M-E add real per-weapon entries as headset tuning
	// happens. Deliberately a plain hardcoded map, not a new ini schema:
	// this codebase's ini mechanism (PackageManager::GetIniValue, a flat
	// section/key -> single string value) doesn't fit a table of
	// vec3/Rotator tuples without inventing a bespoke serialization for a
	// single v1 entry, which the plan explicitly says not to over-engineer.
	struct VRWeaponGripInfo
	{
		vec3 gripOffset = vec3(0.0f);            // weapon-local (X=fwd,Y=right,Z=up - GetAxes convention), added to mainHand.gripPos
		Rotator rotationTrim = Rotator(0, 0, 0); // added on top of WeaponAimRotator(hand)
		vec3 muzzleOffset = vec3(0.0f);          // weapon-local - M-C's fire-origin intercept
		vec3 foregripPoint = vec3(0.0f);         // weapon-local - M-D's two-hand grab test
		// M-D: only weapons flagged two-handed here ever participate in
		// UpdateVRTwoHandGrip()'s grab detection / WeaponAimRotator()'s
		// two-hand blend - default false (one-handed-only) for anything
		// unlisted, per the plan's explicit "conservative default" call
		// (rifles yes, Enforcer no).
		bool twoHanded = false;
	};
	VRWeaponGripInfo GetWeaponGripInfo(UWeapon* weapon);

	// M-E1: dual-wield via UT99's stock double-Enforcer master/slave pair
	// (Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's M-E section, phase E1;
	// behavioral source: Docs/VR/ENFORCER_DUALWIELD_SPEC.md - no decompiled
	// Enforcer/Botpack source was read for this milestone, see that spec's
	// own sourcing notes). Per the spec's section 2, `Pawn.Weapon()` always
	// points at the MASTER Enforcer; the slave is a second, independently-
	// existing Enforcer actor the master holds a reference to. Botpack's
	// Enforcer class has no native C++ mirror in this codebase (unlike
	// Engine.u's base `Weapon` class - PropertyOffsets.cpp's generated
	// PropOffsets_Weapon - the generated PropertyOffsets_* tables only cover
	// "core engine" classes, not individual Botpack weapon subclasses), so
	// rather than inventing a whole new generated UEnforcer/PropOffsets_
	// Enforcer pair for one weapon, the slave-reference and slave-flag
	// property offsets are resolved directly against the actual LOADED
	// Enforcer UClass's real property list, via the exact same generic
	// name->offset mechanism PropertyOffsets.cpp itself is built on
	// (UObject::GetPropertyDataOffset / UObject::Value<T> / UObject::
	// BoolValue - all already public on the base UObject class every weapon
	// instance is-a). The two property names themselves ("SlaveEnforcer",
	// an ObjectProperty; "bIsSlave", a BoolProperty) were found empirically,
	// with zero decompiled source read: a one-time diagnostic
	// (vrEnforcerPropDiagLogged, HandleFrameCallIntercept's RenderOverlays
	// branch) dumped the real, already-loaded Enforcer class's full
	// property list from a live --autoplay --debugvrhands run against the
	// user's own legitimate UT99 GOTY install, and "SlaveEnforcer"/
	// "bIsSlave" were the two whose names and types (an Enforcer-typed
	// object reference; a bool flag) unambiguously matched the spec's
	// description of the master-side reference and the slave-side marker.
	bool vrEnforcerOffsetsResolved = false;
	PropertyDataOffset vrEnforcerSlaveEnforcerOffset; // ObjectProperty - master's reference to its slave (None if not paired)
	PropertyDataOffset vrEnforcerBIsSlaveOffset;      // BoolProperty - true on the slave instance itself

	// M-E1: resolves (once, cached above) and returns the live slave
	// Enforcer linked to `master`, or nullptr whenever `master` isn't an
	// Enforcer, isn't currently paired (SlaveEnforcer is None), or the
	// expected properties can't be found on this class at all (defensive -
	// never throws/asserts, just degrades to "no slave" so a stock single
	// Enforcer, or any other weapon entirely, is unaffected). See the .cpp
	// definition for the full doc comment.
	UWeapon* GetSlaveEnforcer(UWeapon* master);

	// M-E1: given the local player's actual `Pawn.Weapon()` (always the
	// dual-wield MASTER, per the spec's section 2) and the UObject a
	// Frame::Call is currently invoking `func` on, decides which physical
	// hand should drive that call: MainHand() when `instance` IS the
	// master, OffHand() when `instance` is the master's linked slave
	// (GetSlaveEnforcer(master)), or nullptr (hand left unset) for anything
	// else - the exact same "falls through untouched" contract every VR
	// intercept branch already had before M-E1, just resolved once instead
	// of duplicated per branch. Returns the weapon actor to treat as the
	// call's target (master or slave) alongside `*outHand`, or nullptr.
	UWeapon* ResolveVRWeaponHand(UWeapon* master, UObject* instance, VRHandState** outHand);

	// M-C: VM interception seam consumer, PRE side - see
	// Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's M-C section ("Single-hand fire
	// redirect"). Extends HandleFrameCallIntercept (still the single
	// Frame::InterceptCall installer, see its .cpp definition) with two more
	// name/instance-filtered cases on top of M-B's RenderOverlays one:
	//
	// - `TraceFire`/`ProjectileFire` on the local player's current weapon:
	//   saves Pawn.ViewRotation onto vrFireViewRotationStack, overwrites it
	//   with WeaponAimRotator(MainHand()), and returns FALSE (unlike M-B's
	//   RenderOverlays case) - the original script call must actually run
	//   this time (see Frame::InterceptCallPost's doc comment for why a
	//   single true/false hook can't do "run original, but wrap it", and
	//   HandleFrameCallInterceptPost below for the matching restore).
	// - `CalcDrawOffset` on the local player's current weapon: see
	//   HandleFrameCallInterceptPost's doc comment for the full two-phase
	//   (diagnose-then-override) story; this PRE side only ever returns TRUE
	//   (fully replacing the call, M-B-InvCalcView-style) once
	//   vrCalcDrawOffsetConfirmedOwnerRelative is true - before that it
	//   returns false so the stock script runs unmodified and the POST hook
	//   can observe its real return value for the one-time diagnostic.
	//
	// Falls through (false, zero side effects) for every other function/
	// instance and whenever the main hand has no valid pose this frame, same
	// degrade-gracefully contract as the RenderOverlays case.
	bool HandleFrameCallIntercept(UObject* instance, UFunction* func, Array<ExpressionValue>& args, ExpressionValue& result);

	// M-C: VM interception seam consumer, POST side - installed into the new
	// Frame::InterceptCallPost (see its doc comment in VM/Frame.h) from Run()
	// alongside HandleFrameCallIntercept, same activation condition
	// (xrSessionActive || debugVRHandsEnabled).
	//
	// Two responsibilities, both keyed on the same (instance, func) pair
	// HandleFrameCallIntercept saw at entry to this same Frame::Call
	// invocation:
	//
	// 1. `TraceFire`/`ProjectileFire` restore: pops vrFireViewRotationStack
	//    and writes the saved Rotator back onto the SAME pawn the pre-hook
	//    saved it from (not just "whatever the current pawn is now" - the
	//    stack stores the pawn pointer alongside the saved value), restoring
	//    Pawn.ViewRotation to what it was before the swap. A stack (not a
	//    single saved value) makes this safe if a fire call somehow nests
	//    inside another fire call (state-code re-fire calling a script
	//    helper that itself calls TraceFire again, for instance) - push/pop
	//    is strictly LIFO-ordered by construction, matching how the C++ call
	//    stack itself nests pre/post pairs (see Frame::InterceptCallPost's
	//    doc comment).
	// 2. `CalcDrawOffset` one-time diagnostic + override arming: on the
	//    FIRST call for the local player's weapon (vrCalcDrawOffsetDiagLogged
	//    still false), logs the stock script's real, UNMODIFIED return value
	//    (`result`) side by side with what the hand-based replacement formula
	//    would have produced, then checks the plan's "documented as
	//    Owner-relative, verify empirically" open risk #3 via a magnitude
	//    heuristic: a small return value (same ballpark as
	//    PlayerViewOffset/FireOffset, at most a few hundred UU) is consistent
	//    with an Owner-relative offset about to be added to Owner.Location by
	//    the caller; a return value with a magnitude near Owner->Location()'s
	//    own (thousands of UU, i.e. already looks like an absolute world
	//    position) would mean the convention does NOT hold. Sets
	//    vrCalcDrawOffsetConfirmedOwnerRelative accordingly - only when true
	//    does HandleFrameCallIntercept's PRE side ever start overriding
	//    later calls. This is the best empirical test available without a
	//    decompile of CalcDrawOffset's actual script body (none exists in
	//    this repo - see the plan's ground-truth section).
	void HandleFrameCallInterceptPost(UObject* instance, UFunction* func, ExpressionValue& result);

	// M-C: save/restore stack for HandleFrameCallIntercept/
	// HandleFrameCallInterceptPost's TraceFire/ProjectileFire ViewRotation
	// swap - see those two functions' doc comments. Plain LIFO Array (this
	// codebase's std::vector-alike, Utils/Array.h) rather than a single
	// saved value, for re-entrancy safety against nested fire calls (plan's
	// M-C section explicitly calls for "a simple vector/array push-pop").
	struct VRFireViewRotationSave
	{
		UPawn* pawn = nullptr;
		Rotator saved = Rotator(0, 0, 0);
	};
	Array<VRFireViewRotationSave> vrFireViewRotationStack;

	// M-C: CalcDrawOffset diagnose-then-override state - see
	// HandleFrameCallInterceptPost's doc comment for the full story.
	bool vrCalcDrawOffsetDiagLogged = false;             // one-time diagnostic sample taken (stock vs. computed, logged once)
	bool vrCalcDrawOffsetConfirmedOwnerRelative = false; // only true once the diagnostic confirms the documented Owner-relative convention; arms the override
	vec3 vrCalcDrawOffsetPendingCandidate = vec3(0.0f);  // candidate computed by the PRE hook, read back by the POST hook for the same call
	bool vrCalcDrawOffsetPendingCandidateValid = false;  // guards against the POST hook reading a stale candidate from an unrelated call

	// M-E1: one-time empirical property-discovery diagnostic - see
	// HandleFrameCallIntercept's RenderOverlays branch in Engine.cpp for the
	// full doc comment. Per the clean-room constraint on this milestone (no
	// decompiled Enforcer/Botpack source may be consulted), the master/slave
	// pair's actual property names were found by dumping the ALREADY-LOADED
	// Enforcer UClass's real property list (weapon->Class->Properties) the
	// first time the local player's current weapon's class name contains
	// "Enforcer" - the same generic name-keyed property system
	// PropertyOffsets.cpp already uses for every other native accessor in
	// this codebase (UObject::GetPropertyDataOffset), just walked in full
	// instead of looked up by an assumed name. Logged once per run,
	// independent of hand-pose validity, so it fires even on a machine with
	// no headset connected as long as --debugvrhands (or a real VR session)
	// is active and the player is holding an Enforcer.
	bool vrEnforcerPropDiagLogged = false;

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
