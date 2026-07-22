#include "Precomp.h"
#include "WebXRInputState.h"

#include <algorithm>
#include <cmath>

namespace
{
	WebXRInputSnapshot LatestSnapshot;

	float Clamp(float value, float minimum, float maximum)
	{
		return std::max(minimum, std::min(value, maximum));
	}

	void NormalizePose(WebXRInputPose& pose)
	{
		const float lengthSquared =
			pose.Orientation[0] * pose.Orientation[0] + pose.Orientation[1] * pose.Orientation[1] +
			pose.Orientation[2] * pose.Orientation[2] + pose.Orientation[3] * pose.Orientation[3];
		if (lengthSquared > 0.000001f && std::isfinite(lengthSquared))
		{
			const float inverseLength = 1.0f / std::sqrt(lengthSquared);
			for (float& component : pose.Orientation)
				component *= inverseLength;
		}
		else
		{
			pose.Orientation[0] = 0.0f;
			pose.Orientation[1] = 0.0f;
			pose.Orientation[2] = 0.0f;
			pose.Orientation[3] = 1.0f;
		}
	}

	WebXRControllerState NormalizeController(const WebXRControllerState& source)
	{
		if ((source.Flags & WebXRConnected) == 0)
			return {};

		WebXRControllerState result = source;
		result.Flags &= WebXRAimValid | WebXRGripValid | WebXRConnected | WebXRXRStandard;
		// Preserve standard Gamepad D-pad indices 12..15 in the mask. The frame
		// ABI's eight analog button-value slots are unchanged.
		result.ButtonsPressed &= 0xffffu;
		result.ButtonsTouched &= 0xffffu;
		for (float& axis : result.Axes)
			axis = Clamp(axis, -1.0f, 1.0f);
		for (float& value : result.ButtonValues)
			value = Clamp(value, 0.0f, 1.0f);

		if (result.Flags & WebXRGripValid)
			NormalizePose(result.GripPose);
		else
			result.GripPose = {};
		if (result.Flags & WebXRAimValid)
			NormalizePose(result.AimPose);
		else
			result.AimPose = {};
		return result;
	}

	int PreferredSlot(const WebXRControllerState& controller)
	{
		if (controller.Handedness == WebXRHandLeft)
			return 0;
		if (controller.Handedness == WebXRHandRight)
			return 1;
		return -1;
	}

	WebXRInputSnapshot BuildSnapshot(uint64_t generation,
		const WebXRControllerState* controllers, uint32_t controllerCount,
		const WebXRInputPose* headPose = nullptr)
	{
		WebXRInputSnapshot result;
		result.FrameGeneration = generation;
		if (headPose)
		{
			result.HeadPoseValid = true;
			result.HeadPose = *headPose;
			NormalizePose(result.HeadPose);
		}
		bool occupied[WebXRMaxInputSources] = {};
		const uint32_t count = std::min(controllerCount, WebXRMaxInputSources);
		for (uint32_t index = 0; index < count; index++)
		{
			WebXRControllerState controller = NormalizeController(controllers[index]);
			if ((controller.Flags & WebXRConnected) == 0)
				continue;

			int slot = PreferredSlot(controller);
			if (slot < 0 || occupied[slot])
			{
				slot = !occupied[0] ? 0 : (!occupied[1] ? 1 : -1);
			}
			if (slot < 0)
				continue;
			result.Controllers[slot] = controller;
			occupied[slot] = true;
			result.SourceCount++;
		}
		return result;
	}
}

WebXRInputSnapshot GetLatestWebXRInputSnapshot()
{
	return LatestSnapshot;
}

void PublishWebXRInputSnapshot(const WebXRControllerState* controllers, uint32_t controllerCount,
	const WebXRInputPose* headPose)
{
	const uint64_t nextGeneration = LatestSnapshot.FrameGeneration + 1;
	if (!controllers)
		controllerCount = 0;
	LatestSnapshot = BuildSnapshot(nextGeneration, controllers, controllerCount, headPose);
}

void ResetWebXRInputState()
{
	PublishWebXRInputSnapshot(nullptr, 0);
}

bool RunWebXRInputStateSelfTest()
{
	WebXRControllerState inputs[2];
	inputs[0].SourceId = 17;
	inputs[0].Handedness = WebXRHandRight;
	inputs[0].Flags = WebXRConnected | WebXRXRStandard | WebXRAimValid;
	inputs[0].ButtonsPressed = 0x11001;
	inputs[0].Axes[0] = -2.0f;
	inputs[0].Axes[1] = 0.25f;
	inputs[0].ButtonValues[0] = 1.5f;
	inputs[0].AimPose.Orientation[3] = 2.0f;
	// A quarter roll about aim-forward: forward remains +X while right/up
	// rotate into -Z/+Y. Snapshot normalization must preserve this composed
	// UE1-local basis rather than reducing it to a direction vector.
	inputs[0].AimPose.LocalRight[0] = 0.0f;
	inputs[0].AimPose.LocalRight[1] = 0.0f;
	inputs[0].AimPose.LocalRight[2] = -1.0f;
	inputs[0].AimPose.LocalUp[0] = 0.0f;
	inputs[0].AimPose.LocalUp[1] = 1.0f;
	inputs[0].AimPose.LocalUp[2] = 0.0f;
	inputs[1].SourceId = 9;
	inputs[1].Handedness = WebXRHandLeft;
	inputs[1].Flags = WebXRConnected | WebXRGripValid;
	inputs[1].GripPose.Orientation[1] = 3.0f;
	inputs[1].GripPose.Orientation[3] = 4.0f;
	WebXRInputPose headPose;
	headPose.LocalForward[0] = 0.0f;
	headPose.LocalForward[1] = 1.0f;
	headPose.LocalForward[2] = 0.0f;

	WebXRInputSnapshot snapshot = BuildSnapshot(41, inputs, 2, &headPose);
	if (snapshot.FrameGeneration != 41 || snapshot.SourceCount != 2 ||
		!snapshot.HeadPoseValid || snapshot.HeadPose.LocalForward[1] != 1.0f ||
		snapshot.Controllers[0].SourceId != 9 || snapshot.Controllers[1].SourceId != 17 ||
		snapshot.Controllers[1].ButtonsPressed != 0x1001 || snapshot.Controllers[1].Axes[0] != -1.0f ||
		snapshot.Controllers[1].Axes[1] != 0.25f || snapshot.Controllers[1].ButtonValues[0] != 1.0f ||
		std::abs(snapshot.Controllers[1].AimPose.Orientation[3] - 1.0f) > 0.0001f ||
		std::abs(snapshot.Controllers[1].AimPose.LocalRight[2] + 1.0f) > 0.0001f ||
		std::abs(snapshot.Controllers[1].AimPose.LocalUp[1] - 1.0f) > 0.0001f ||
		std::abs(snapshot.Controllers[0].GripPose.Orientation[1] - 0.6f) > 0.0001f ||
		std::abs(snapshot.Controllers[0].GripPose.Orientation[3] - 0.8f) > 0.0001f)
		return false;

	inputs[0].Flags = 0;
	snapshot = BuildSnapshot(42, inputs, 1);
	return snapshot.FrameGeneration == 42 && snapshot.SourceCount == 0 &&
		!snapshot.HeadPoseValid &&
		(snapshot.Controllers[0].Flags | snapshot.Controllers[1].Flags) == 0;
}
