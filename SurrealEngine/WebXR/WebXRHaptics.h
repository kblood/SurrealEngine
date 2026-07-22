#pragma once

#include <cstdint>

// Keep these values aligned with XRInputSource.handedness at the browser
// boundary. JavaScript receives only this scalar identifier; live WebXR input
// sources and actuators remain browser-owned.
enum class WebXRHapticHand : uint32_t
{
	Left = 1,
	Right = 2
};

// Queues a pulse with the browser-side WebXR haptic policy. The call is
// synchronous, but the actuator pulse itself is dispatched by the WebXR frame
// loop. Native builds have no browser actuator and return false.
bool QueueWebXRHapticPulse(WebXRHapticHand hand, float intensity, uint32_t durationMs);

// Enables or disables browser-side haptics. Returns whether the browser setter
// was available and accepted the requested state. Native builds return false.
bool SetWebXRHapticsEnabled(bool enabled);

// Pure ABI validation which is safe to run without a browser, game data, an XR
// session, or a connected controller.
bool RunWebXRHapticsBridgeSelfTest();
