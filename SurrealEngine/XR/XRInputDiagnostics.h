#pragma once

#include "XR/XRCommon.h"

#include <array>
#include <cstdint>
#include <vector>

enum XRInputButtonAvailability : uint32_t
{
	XRInputButtonSelect = 1u << 0,
	XRInputButtonSqueeze = 1u << 1,
	XRInputButtonPrimary = 1u << 2,
	XRInputButtonSecondary = 1u << 3,
	XRInputButtonThumbstickClick = 1u << 4,
	XRInputButtonMenu = 1u << 5,
};

enum XRInputAxisAvailability : uint32_t
{
	XRInputAxisThumbstick = 1u << 0,
};

struct XRInputHandAvailability
{
	bool ProfileOrBindingAvailable = false;
	uint32_t ButtonMask = 0;
	uint32_t AxisMask = 0;

	bool operator==(const XRInputHandAvailability&) const = default;
};

struct XRInputDiagnosticCounters
{
	uint64_t PrimaryTriggerPressEdges = 0;
	uint64_t MenuBackPressEdges = 0;
	uint64_t NonzeroThumbstickSamples = 0;
};

enum class XRInputDiagnosticEventType : uint8_t
{
	SessionState,
	HandAvailability,
	ActivitySummary,
};

struct XRInputDiagnosticEvent
{
	XRInputDiagnosticEventType Type = XRInputDiagnosticEventType::SessionState;
	bool Initial = false;
	XRSessionState PreviousSession;
	XRSessionState Session;
	XRHand Hand = XRHand::Left;
	bool Connected = false;
	XRInputHandAvailability Availability;
	XRInputDiagnosticCounters Counters;
};

// Bounded, provider-neutral input observations for hardware gates. This class
// deliberately retains no poses, paths, profiles, or raw per-frame values.
class XRInputDiagnosticsAccumulator
{
public:
	std::vector<XRInputDiagnosticEvent> ObserveSession(const XRSessionState& session);
	std::vector<XRInputDiagnosticEvent> ObserveInput(
		const XRSessionState& session,
		const XRControllerSnapshot& controllers,
		const std::array<XRInputHandAvailability, XRHandCount>& availability);

	const XRInputDiagnosticCounters& Counters(XRHand hand) const;

private:
	static constexpr uint32_t MaxSessionEvents = 12;
	static constexpr uint32_t MaxAvailabilityEventsPerHand = 8;
	static constexpr uint32_t MaxActivityEventsPerHand = 6;

	struct HandState
	{
		bool Initialized = false;
		bool Connected = false;
		bool SelectPressed = false;
		bool MenuBackPressed = false;
		XRInputHandAvailability Availability;
		XRInputDiagnosticCounters Counters;
		XRInputDiagnosticCounters LastReportedCounters;
		uint32_t AvailabilityEvents = 0;
		uint32_t ActivityEvents = 0;
	};

	bool SessionInitialized = false;
	XRSessionState LastSession;
	uint32_t SessionEvents = 0;
	std::array<HandState, XRHandCount> Hands;

	void AppendActivitySummary(std::vector<XRInputDiagnosticEvent>& events, XRHand hand);
};
