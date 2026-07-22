#include "Platform/WebXR/WebXRInputBridge.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <mutex>

namespace
{
	WebXR::InputError LastInputError = WebXR::InputError::None;
	WebXR::DecodedInputSnapshot LatestInputSnapshot;
	std::mutex InputSnapshotMutex;

	bool IsFiniteArray(const float* values, size_t count)
	{
		for (size_t index = 0; index < count; index++)
		{
			if (!std::isfinite(values[index]))
				return false;
		}
		return true;
	}

	bool IsValidQuaternion(const float* values)
	{
		const float lengthSquared = values[0] * values[0] + values[1] * values[1] +
			values[2] * values[2] + values[3] * values[3];
		return std::isfinite(lengthSquared) && lengthSquared >= 0.000001f;
	}
}

bool WebXR::DecodeInputSnapshot(const void* inputData, uint32_t bufferBytes,
	DecodedInputSnapshot& result, InputError& error)
{
	result = {};
	if (!inputData || bufferBytes < sizeof(PackedInputHeader))
	{
		error = InputError::InvalidHeader;
		return false;
	}

	PackedInputHeader header = {};
	std::memcpy(&header, inputData, sizeof(header));
	if (header.Version != InputABIVersion || header.SourceCount > MaxInputSources ||
		(header.Flags & ~(InputSessionActive | InputActionFocused)) != 0 ||
		(header.Flags & InputActionFocused) != 0 && (header.Flags & InputSessionActive) == 0 ||
		!std::isfinite(header.Timestamp) ||
		header.ByteSize != bufferBytes ||
		header.ByteSize != sizeof(PackedInputHeader) + header.SourceCount * sizeof(PackedInputSource))
	{
		error = InputError::InvalidHeader;
		return false;
	}

	result.Timestamp = header.Timestamp;
	result.SessionActive = (header.Flags & InputSessionActive) != 0;
	result.ActionFocused = (header.Flags & InputActionFocused) != 0;
	uint32_t seenHands = 0;
	const uint8_t* sourceBytes = static_cast<const uint8_t*>(inputData) + sizeof(PackedInputHeader);
	constexpr uint32_t validSourceFlags = InputSourceConnected | InputAimPoseValid | InputGripPoseValid;
	constexpr uint32_t validButtons = (1u << InputButtonCount) - 1u;
	for (uint32_t sourceIndex = 0; sourceIndex < header.SourceCount; sourceIndex++)
	{
		PackedInputSource packed = {};
		std::memcpy(&packed, sourceBytes + sourceIndex * sizeof(PackedInputSource), sizeof(packed));
		if (packed.Hand < static_cast<uint32_t>(InputHand::Left) ||
			packed.Hand > static_cast<uint32_t>(InputHand::Right) ||
			(packed.Flags & ~validSourceFlags) != 0 || (packed.Flags & InputSourceConnected) == 0 ||
			(packed.PressedButtons & ~validButtons) != 0 || (packed.TouchedButtons & ~validButtons) != 0 ||
			!IsFiniteArray(packed.ButtonValues, InputButtonCount) || !IsFiniteArray(packed.Axes, InputAxisCount) ||
			!IsFiniteArray(packed.AimPosition, 3) || !IsFiniteArray(packed.AimOrientation, 4) ||
			!IsFiniteArray(packed.GripPosition, 3) || !IsFiniteArray(packed.GripOrientation, 4))
		{
			error = InputError::InvalidSource;
			return false;
		}
		for (float value : packed.ButtonValues)
		{
			if (value < 0.0f || value > 1.0f)
			{
				error = InputError::InvalidSource;
				return false;
			}
		}
		for (float value : packed.Axes)
		{
			if (value < -1.0f || value > 1.0f)
			{
				error = InputError::InvalidSource;
				return false;
			}
		}
		if (((packed.Flags & InputAimPoseValid) != 0 && !IsValidQuaternion(packed.AimOrientation)) ||
			((packed.Flags & InputGripPoseValid) != 0 && !IsValidQuaternion(packed.GripOrientation)))
		{
			error = InputError::InvalidSource;
			return false;
		}

		const uint32_t handIndex = packed.Hand - 1;
		const uint32_t handBit = 1u << handIndex;
		if ((seenHands & handBit) != 0)
		{
			error = InputError::DuplicateHand;
			return false;
		}
		seenHands |= handBit;
		DecodedInputSource& decoded = result.Sources[handIndex];
		decoded.Connected = true;
		decoded.AimPoseValid = (packed.Flags & InputAimPoseValid) != 0;
		decoded.GripPoseValid = (packed.Flags & InputGripPoseValid) != 0;
		decoded.PressedButtons = packed.PressedButtons;
		decoded.TouchedButtons = packed.TouchedButtons;
		std::copy(std::begin(packed.ButtonValues), std::end(packed.ButtonValues), decoded.ButtonValues.begin());
		std::copy(std::begin(packed.Axes), std::end(packed.Axes), decoded.Axes.begin());
		std::copy(std::begin(packed.AimPosition), std::end(packed.AimPosition), decoded.AimPosition.begin());
		std::copy(std::begin(packed.AimOrientation), std::end(packed.AimOrientation), decoded.AimOrientation.begin());
		std::copy(std::begin(packed.GripPosition), std::end(packed.GripPosition), decoded.GripPosition.begin());
		std::copy(std::begin(packed.GripOrientation), std::end(packed.GripOrientation), decoded.GripOrientation.begin());
	}

	error = InputError::None;
	return true;
}

void WebXR::StoreInputSnapshot(const DecodedInputSnapshot& snapshot)
{
	std::lock_guard<std::mutex> lock(InputSnapshotMutex);
	LatestInputSnapshot = snapshot;
}

WebXR::DecodedInputSnapshot WebXR::GetInputSnapshot()
{
	std::lock_guard<std::mutex> lock(InputSnapshotMutex);
	return LatestInputSnapshot;
}

void WebXR::ClearInputSnapshot(double timestamp)
{
	DecodedInputSnapshot neutral;
	neutral.Timestamp = std::isfinite(timestamp) ? timestamp : 0.0;
	StoreInputSnapshot(neutral);
}

void WebXR::SetLastInputError(InputError error)
{
	LastInputError = error;
}

WebXR::InputError WebXR::GetLastInputError()
{
	return LastInputError;
}

extern "C"
{
	uint32_t Surreal_GetWebXRInputABIVersion() { return WebXR::InputABIVersion; }
	uint32_t Surreal_GetWebXRInputHeaderSize() { return sizeof(WebXR::PackedInputHeader); }
	uint32_t Surreal_GetWebXRInputSourceSize() { return sizeof(WebXR::PackedInputSource); }
	uint32_t Surreal_GetWebXRInputMaxSources() { return WebXR::MaxInputSources; }

	int Surreal_ValidateWebXRInputSnapshot(const void* inputData, uint32_t bufferBytes)
	{
		WebXR::DecodedInputSnapshot snapshot;
		WebXR::InputError error;
		const bool valid = WebXR::DecodeInputSnapshot(inputData, bufferBytes, snapshot, error);
		WebXR::SetLastInputError(error);
		return valid ? 1 : 0;
	}

	int Surreal_SubmitWebXRInputSnapshot(const void* inputData, uint32_t bufferBytes)
	{
		WebXR::DecodedInputSnapshot snapshot;
		WebXR::InputError error;
		if (!WebXR::DecodeInputSnapshot(inputData, bufferBytes, snapshot, error))
		{
			WebXR::SetLastInputError(error);
			return 0;
		}
		WebXR::StoreInputSnapshot(snapshot);
		WebXR::SetLastInputError(WebXR::InputError::None);
		return 1;
	}

	int Surreal_GetWebXRInputLastError() { return static_cast<int>(WebXR::GetLastInputError()); }
	void Surreal_ClearWebXRInputSnapshot() { WebXR::ClearInputSnapshot(); }
}
