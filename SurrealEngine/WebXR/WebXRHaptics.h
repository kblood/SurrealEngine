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

// Stable event identifiers are exposed to browser smoke tests. Keep Count last.
enum class WebXRHapticEvent : uint32_t
{
	Fire = 0,
	Damage = 1,
	Pickup = 2,
	UIConfirm = 3,
	Count = 4
};

struct WebXRHapticPulse
{
	float Intensity = 0.0f;
	uint32_t DurationMs = 0;
};

// Converts a semantic event and optional magnitude into the single bounded
// policy used by the engine. Magnitude is health loss for Damage and ignored
// for the fixed-strength events.
WebXRHapticPulse GetWebXRHapticPulse(WebXRHapticEvent event, float magnitude = 1.0f);

// Resolves the centralized event policy and queues it for one browser-owned
// controller. Invalid event identifiers never reach JavaScript.
bool QueueWebXRHapticEvent(WebXRHapticEvent event, WebXRHapticHand hand,
	float magnitude = 1.0f);

// Queues a pulse with the browser-side WebXR haptic policy. The call is
// synchronous, but the actuator pulse itself is dispatched by the WebXR frame
// loop. Native builds have no browser actuator and return false.
bool QueueWebXRHapticPulse(WebXRHapticHand hand, float intensity, uint32_t durationMs);

// Enables or disables browser-side haptics. Returns whether the browser setter
// was available and accepted the requested state. Native builds return false.
bool SetWebXRHapticsEnabled(bool enabled);

// Pure ABI and bounded-policy validation which is safe to run without a
// browser, game data, an XR session, or a connected controller.
bool RunWebXRHapticsBridgeSelfTest();
