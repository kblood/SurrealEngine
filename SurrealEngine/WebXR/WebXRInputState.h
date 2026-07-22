#pragma once

#include <cstdint>

constexpr uint32_t WebXRMaxInputSources = 2;

enum WebXRInputHandedness : uint32_t
{
	WebXRHandNone = 0,
	WebXRHandLeft = 1,
	WebXRHandRight = 2
};

enum WebXRControllerFlags : uint32_t
{
	WebXRAimValid = 1u << 0,
	WebXRGripValid = 1u << 1,
	WebXRConnected = 1u << 2,
	WebXRXRStandard = 1u << 3
};

struct WebXRInputPose
{
	// Raw WebXR reference-space position in metres and normalized WebXR
	// orientation. The validity bits live in WebXRControllerState::Flags.
	float Position[3] = {};
	float Orientation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	// Recentered UE1-local pose, prepared from the same eye-center origin as
	// the stereo views before the simulation tick. Engine input composes this
	// with the current camera anchor and body yaw, including a snap turn that
	// may be applied while consuming this snapshot.
	float LocalPositionUU[3] = {};
	float LocalForward[3] = { 1.0f, 0.0f, 0.0f };
	// Full left-handed UE1 controller basis. Keeping right/up alongside forward
	// preserves controller roll for presentation without changing the existing
	// direction-only ballistic path.
	float LocalRight[3] = { 0.0f, 1.0f, 0.0f };
	float LocalUp[3] = { 0.0f, 0.0f, 1.0f };
};

struct WebXRControllerState
{
	uint32_t SourceId = 0;
	uint32_t Handedness = WebXRHandNone;
	uint32_t Flags = 0;
	uint32_t ButtonsPressed = 0;
	uint32_t ButtonsTouched = 0;
	// Browser-normalized transport values: radial 0.15 deadzone per pair,
	// X unchanged, Y flipped so positive means engine forward/up. Gameplay
	// still owns action mapping and any additional accessibility response curve.
	float Axes[4] = {};
	float ButtonValues[8] = {};
	WebXRInputPose GripPose;
	WebXRInputPose AimPose;
};

struct WebXRInputSnapshot
{
	uint64_t FrameGeneration = 0;
	uint32_t SourceCount = 0;
	// Stable normalized slots: left prefers 0, right prefers 1, unhanded
	// sources fill an unused slot. Consumers must inspect Connected rather
	// than assuming [0, SourceCount) are occupied.
	WebXRControllerState Controllers[WebXRMaxInputSources];
};

// Same-thread latest-state exchange. Publishing always replaces the complete
// snapshot and advances FrameGeneration, including a zero-source frame.
WebXRInputSnapshot GetLatestWebXRInputSnapshot();
void PublishWebXRInputSnapshot(const WebXRControllerState* controllers, uint32_t controllerCount);
void ResetWebXRInputState();
bool RunWebXRInputStateSelfTest();
