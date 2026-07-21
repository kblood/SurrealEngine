#pragma once

// OpenXR session/swapchain/frame-loop wrapper (M2 steps 2/4/8/9 of
// VR_IMPLEMENTATION_PLAN.md). Deliberately kept separate from
// VulkanRenderDevice.cpp so flatscreen play is completely unaffected when
// --vr isn't passed - this header is included by Engine.cpp (which must NOT
// drag in <vulkan/vulkan.h> or <openxr/openxr.h>), so every function here
// uses opaque void* handles / plain PODs instead of real Vulkan/OpenXR
// types. The .cpp has the real types and does the real work.

#include <string>
#include <vector>
#include <cstdint>

// A single eye's tracked pose + field of view, as reported by xrLocateViews.
// Position is in meters, in the session's reference space (LOCAL). The
// quaternion and position are in OpenXR's convention (right-handed, +Y up,
// +X right, -Z forward) - NOT remapped into UE1's axis convention. Currently
// used for logging/diagnostics only - see VulkanXRSession.cpp's LocateViews
// doc comment for why it is not yet wired into the rendered view.
struct VREyePose
{
	float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
	float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
	float angleLeft = 0.0f, angleRight = 0.0f, angleUp = 0.0f, angleDown = 0.0f; // radians
};

// M-A: a single hand's tracked pose (grip or aim), as reported by
// xrLocateSpace against the session's app space. Same opaque-POD /
// meters / OpenXR-convention style as VREyePose (position in the
// session's LOCAL reference space, +Y up, +X right, -Z forward - NOT
// remapped into UE1 axes here, that happens in Engine.cpp same as for
// eye poses). `valid` requires BOTH XR_SPACE_LOCATION_POSITION_VALID_BIT
// and XR_SPACE_LOCATION_ORIENTATION_VALID_BIT to be set on the
// xrLocateSpace result - false whenever this hand isn't currently
// tracked (controller off/out of tracking volume, session not FOCUSED -
// same focus caveat as VRControllerState::actionsActive below) or when
// there's no session/action at all.
struct VRHandPose
{
	bool valid = false;
	float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
	float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
};

// M3: controller input, read after SyncActions(). Trigger/grip are analog
// [0,1] (Oculus Touch has no trigger "click" input, only /value and
// /touch); everything else is a plain digital button. Unbound actions
// (e.g. every field but leftTrigger/rightTrigger/leftMenu on a runtime that
// only advertises khr/simple_controller) just read as their zero default.
struct VRControllerState
{
	float leftStickX = 0.0f, leftStickY = 0.0f;
	float rightStickX = 0.0f, rightStickY = 0.0f;
	float leftTrigger = 0.0f, rightTrigger = 0.0f;
	float leftGrip = 0.0f, rightGrip = 0.0f;
	bool leftX = false, leftY = false;
	bool rightA = false, rightB = false;
	bool leftMenu = false;
	bool leftStickClick = false, rightStickClick = false;

	// 2026-07-20: true if xrGetActionState*'s isActive came back true for
	// AT LEAST ONE bound action this call. isActive is false whenever the
	// runtime isn't routing input to this session's action set (e.g. the
	// XR session isn't XR_SESSION_STATE_FOCUSED - a system overlay/
	// dashboard has focus instead) - every action then silently reads its
	// zero default with no error, which looks identical to "nothing is
	// pressed/tilted" to a caller with no visibility into isActive. Added
	// after a real-headset report of fire + movement both being
	// unresponsive, to tell "focus/binding problem" apart from "genuinely
	// no input this frame" without guessing - see the throttled diagnostic
	// log in Engine::UpdateVRControllerInput().
	bool actionsActive = false;
};

class VulkanXRSession
{
public:
	VulkanXRSession();
	~VulkanXRSession();

	// True once xrCreateInstance + xrGetSystem both succeeded. Does NOT
	// imply a session exists yet - see HasSession()/CreateSession().
	bool IsAvailable() const { return available; }
	const std::string& LastError() const { return lastError; }

	// ---- M2 step 4: Vulkan instance/device requirement resolution ----

	// Must be called BEFORE vkCreateInstance (spec requirement). Returns the
	// runtime's required Vulkan instance extensions, already split on
	// spaces, ready to feed into VulkanInstanceBuilder::RequireExtensions().
	// Empty on failure (check LastError()).
	std::vector<std::string> GetVulkanInstanceExtensions();

	// Must be called AFTER VkInstance exists (spec requirement - the exact
	// physical device is only resolvable given a live instance). Fills
	// outPhysicalDevice (a VkPhysicalDevice cast to void*) and
	// outDeviceExtensions (split on spaces). Returns false on failure.
	bool ResolveVulkanDevice(void* vkInstance, void** outPhysicalDevice, std::vector<std::string>& outDeviceExtensions);

	// ---- M2 step 2: session lifecycle ----

	// Creates the XrSession bound to the live Vulkan device via
	// XrGraphicsBindingVulkanKHR, and a LOCAL reference space. Logs every
	// XrResult. Returns false on failure (check LastError()).
	bool CreateSession(void* vkInstance, void* vkPhysicalDevice, void* vkDevice, uint32_t queueFamilyIndex, uint32_t queueIndex);
	void DestroySession();
	bool HasSession() const { return session != nullptr; }

	// Pumps xrPollEvent, logs every XrEventDataSessionStateChanged
	// transition, and calls xrBeginSession/xrEndSession at the appropriate
	// transitions per the OpenXR state machine. Returns false once the
	// session has reached XR_SESSION_STATE_EXITING/LOSS_PENDING (caller
	// should stop the XR frame loop, though the flatscreen game loop can
	// keep running).
	bool PollEvents();

	// True once xrBeginSession has been called (session ready for
	// xrWaitFrame/xrBeginFrame/xrEndFrame per spec).
	bool IsSessionRunning() const { return sessionRunning; }

	int GetLastSessionState() const { return lastLoggedState; } // raw XrSessionState value, for the caller's own logging

	// ---- M2 step 8: swapchains + frame loop ----

	// Queries the recommended per-eye swapchain extent and creates one
	// swapchain per eye (stereo view configuration). Must be called after
	// CreateSession(). Returns false on failure.
	bool CreateSwapchains();
	void DestroySwapchains();
	int GetSwapchainWidth() const { return swapchainWidth; }
	int GetSwapchainHeight() const { return swapchainHeight; }

	// xrWaitFrame + xrBeginFrame. Returns false on a hard failure (caller
	// should stop the XR loop). outShouldRender reflects XrFrameState's
	// shouldRender - xrEndFrame must still be called (with no layers) even
	// when false, to keep the frame loop balanced.
	bool WaitAndBeginFrame(bool& outShouldRender);

	// xrLocateViews for the current frame. Returns false on failure.
	bool LocateViews(VREyePose outEyes[2]);

	// xrAcquireSwapchainImage + xrWaitSwapchainImage for the given eye
	// (0=left, 1=right). Returns the VkImage (cast to void*), or nullptr on
	// failure.
	void* AcquireSwapchainImage(int eye);

	// xrReleaseSwapchainImage for the given eye.
	void ReleaseSwapchainImage(int eye);

	// xrEndFrame with a single projection layer covering both eyes (or no
	// layers if submitLayer is false - still required to keep the frame
	// loop balanced).
	void EndFrame(bool submitLayer, const VREyePose eyes[2]);

	// ---- M3: controller input ----

	// Creates the action set/actions and suggests bindings for the Oculus
	// Touch profile (primary target - Quest 2/3/Pro) plus a khr/simple_controller
	// fallback, then attaches the action set to the session. Must be called
	// after CreateSession() succeeds, before the first SyncActions()/
	// GetControllerState() call. Returns false on failure (check LastError()).
	bool CreateActions();

	// xrSyncActions for our one action set. Call once per frame (only while
	// IsSessionRunning()), before any GetControllerState() call that frame.
	// Deliberately does not log anything every frame - see GetLastSyncResult().
	void SyncActions();

	// Raw XrResult of the most recent xrSyncActions call. XR_SESSION_NOT_FOCUSED
	// (a *success* code, XR_FAILED() is false for it) means the runtime is not
	// routing input to this session's action set at all right now - every
	// isActive read this frame will be false regardless of what's physically
	// happening to the controllers. Exposed so the caller's diagnostic log can
	// tell "focus not held" apart from "focus held, genuinely no input" instead
	// of only seeing the isActive=false symptom.
	int GetLastSyncResult() const { return lastSyncResult; }

	// Reads the current (post-SyncActions) state of every bound action into
	// outState. Always succeeds - missing/unbound/inactive actions just
	// read as their zero default. Deliberately does not log anything - this
	// runs every frame.
	void GetControllerState(VRControllerState& outState);

	// ---- M-A: per-hand grip/aim pose tracking ----

	// xrLocateSpace for both hands' grip pose AND aim pose action spaces
	// (outGrip/outAim index 0=left, 1=right, matching /user/hand/left,right),
	// against the session's app space, at the SAME predicted display time
	// LocateViews() uses this frame (lastPredictedDisplayTime) - so hand
	// poses and eye poses agree on "when" within a frame. Each of the four
	// output poses' `valid` is independent (e.g. one controller tracked,
	// the other not, or grip tracked but aim not) - callers must check
	// per-pose, not assume all-or-nothing. Call once per frame, after
	// SyncActions() has run this frame (Engine::UpdateVRControllerInput()
	// already calls SyncActions() earlier in the same tick - see Run()'s
	// XR frame block for where this is called from). Returns false (every
	// output pose left at its invalid default) if no session/actions exist.
	bool LocateHandPoses(VRHandPose outGrip[2], VRHandPose outAim[2]);

private:
	bool available = false;
	std::string lastError;
	void* instance = nullptr; // XrInstance
	uint64_t systemId = 0; // XrSystemId

	void* session = nullptr; // XrSession
	void* appSpace = nullptr; // XrSpace
	bool sessionRunning = false;
	int lastLoggedState = 0; // XR_SESSION_STATE_UNKNOWN

	int lastSyncResult = 0; // XR_SUCCESS; see GetLastSyncResult()

	int swapchainWidth = 0;
	int swapchainHeight = 0;
	void* swapchain[2] = { nullptr, nullptr }; // XrSwapchain, one per eye
	std::vector<void*> swapchainImages[2]; // VkImage per swapchain image, one vector per eye
	uint32_t acquiredIndex[2] = { 0, 0 };

	double lastPredictedDisplayTime = 0.0;
	int64_t predictedDisplayPeriod = 0;

	// Diagnostics for the 2026-07-20 rendering investigation (stereo HUD
	// double vision + world-geometry warp) - logs the real per-eye FOV
	// angles/pose/IPD once, the first time LocateViews() succeeds, so their
	// magnitude (esp. whether angleUp == |angleDown|, which masks the
	// DrawSceneVR() vertical-frustum bug) is visible in SE-Log-LastRun.txt
	// without needing a debugger or an in-headset screenshot.
	bool loggedFirstLocateViews = false;

	// M3: controller input action set/actions. All XrAction handles.
	bool actionsReady = false;
	void* actionSet = nullptr; // XrActionSet
	void* leftStickAction = nullptr; // XrAction (Vector2f)
	void* rightStickAction = nullptr; // XrAction (Vector2f)
	void* leftTriggerAction = nullptr; // XrAction (Float)
	void* rightTriggerAction = nullptr; // XrAction (Float)
	void* leftGripAction = nullptr; // XrAction (Float)
	void* rightGripAction = nullptr; // XrAction (Float)
	void* leftXAction = nullptr; // XrAction (Boolean)
	void* leftYAction = nullptr; // XrAction (Boolean)
	void* rightAAction = nullptr; // XrAction (Boolean)
	void* rightBAction = nullptr; // XrAction (Boolean)
	void* leftMenuAction = nullptr; // XrAction (Boolean)
	void* leftStickClickAction = nullptr; // XrAction (Boolean)
	void* rightStickClickAction = nullptr; // XrAction (Boolean)

	// M-A: pose actions + their action spaces. XrSpace handles are only
	// valid once xrCreateActionSpace succeeds inside CreateActions() (after
	// the action itself is created and the action set is attached) - a
	// null space here means "couldn't create it" and LocateHandPoses()
	// leaves the corresponding pose at its invalid default.
	void* leftGripPoseAction = nullptr; // XrAction (Pose)
	void* rightGripPoseAction = nullptr; // XrAction (Pose)
	void* leftAimPoseAction = nullptr; // XrAction (Pose)
	void* rightAimPoseAction = nullptr; // XrAction (Pose)
	void* leftGripSpace = nullptr; // XrSpace
	void* rightGripSpace = nullptr; // XrSpace
	void* leftAimSpace = nullptr; // XrSpace
	void* rightAimSpace = nullptr; // XrSpace
};
